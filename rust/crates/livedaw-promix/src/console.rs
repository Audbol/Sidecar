//! The ProMix 01 over the engine: its panel's operations, which move several
//! of the engine's values at once (a pair's two channels, a group's faders, a
//! dynamics processor's patched strips), and the values as the console has
//! them (fader steps 0-127, pans 0-32, EQ steps).
//!
//! The strips (`PmStrip`) are the engine's: channels 1-16, ST IN aux 1,
//! RTN1-2 aux 2-3, SEND1-4 buses 1-4, ST OUT main 1.

use crate::library::Library;
use crate::state::{KeyIn, Patch, State, CHANNELS, PROCESSORS};
use livedaw_dsp::fader::FaderLaw;
use livedaw_dsp::promix::DYN_MODELS;
use livedaw_engine::keytap::KeySource;
use livedaw_engine::routing::strip_source_input;
use livedaw_engine::sends::send_mode;
use livedaw_engine::{Engine, Strip, StripKind};
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

/// A ProMix 01 strip.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PmStrip {
    /// Input channel 1-16 (0-based).
    Ch(usize),
    StIn,
    /// RTN1-2 (0-based).
    Rtn(usize),
    /// SEND1-4 (0-based).
    Send(usize),
    StOut,
}

impl PmStrip {
    /// The engine's strip.
    pub fn engine_ref(self) -> (StripKind, usize) {
        match self {
            PmStrip::Ch(c) => (StripKind::Channel, c),
            PmStrip::StIn => (StripKind::Aux, 0),
            PmStrip::Rtn(r) => (StripKind::Aux, 1 + r),
            PmStrip::Send(s) => (StripKind::Bus, s),
            PmStrip::StOut => (StripKind::Main, 0),
        }
    }

    /// The strips with an EQ, in the CC table's order: CH1-16, ST IN, RTN1-2, ST OUT.
    pub fn eq_strip(i: usize) -> Option<PmStrip> {
        Some(match i {
            0..=15 => PmStrip::Ch(i),
            16 => PmStrip::StIn,
            17 | 18 => PmStrip::Rtn(i - 17),
            19 => PmStrip::StOut,
            _ => return None,
        })
    }
}

/// Who moved a value: the panel (linked values follow: a group's faders) or
/// a received control change (only that value; a group is released and
/// re-linked around it, Add-17).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Origin {
    Panel,
    Midi,
}

/// The EQ bands' parameter ids (`livedaw_dsp::promix::EQ`): F, G, Q of LOW, MID, HIGH.
pub const EQ_IDS: [[&str; 3]; 3] = [["lf", "lg", "lq"], ["mf", "mg", "mq"], ["hf", "hg", "hq"]];

/// The ProMix 01 over `engine`, with its own `state` and program library.
pub struct Console<'a> {
    pub engine: &'a Engine,
    pub state: &'a mut State,
    pub library: &'a Library,
}

/// A fader step (0-127) as dB on the ProMix's law.
pub fn step_db(step: u16) -> f32 {
    FaderLaw::PROMIX01.to_db(step.min(127) as f32 / 127.0)
}

/// The fader step of a level in dB.
pub fn db_step(db: f32) -> u16 {
    (FaderLaw::PROMIX01.to_pos(db) * 127.0).round() as u16
}

/// A pan position (0-32, 16 the centre) as the engine's pan (-100..100).
pub fn pan_of(v: u16) -> f32 {
    (v.min(32) as f32 - 16.0) * 100.0 / 16.0
}

/// The pan position of an engine pan.
pub fn pan_step(pan: f32) -> u16 {
    ((pan.clamp(-100.0, 100.0) * 16.0 / 100.0).round() + 16.0) as u16
}

/// A model parameter's value as its step (0..steps-1).
pub fn param_step(spec: &livedaw_dsp::model::ParamSpec, v: f32) -> u16 {
    use livedaw_dsp::model::ParamKind;
    let last = spec.steps.max(1) - 1;
    match spec.kind {
        ParamKind::Linear | ParamKind::Log if spec.max != spec.min && last > 0 => {
            (((v - spec.min) / (spec.max - spec.min)) * last as f32).round().clamp(0.0, last as f32) as u16
        }
        _ => (v.round().max(0.0) as u32).min(last) as u16,
    }
}

/// Step `k` of a model parameter as its value.
pub fn step_param(spec: &livedaw_dsp::model::ParamSpec, k: u16) -> f32 {
    use livedaw_dsp::model::ParamKind;
    let last = spec.steps.max(1) - 1;
    let k = (k as u32).min(last);
    match spec.kind {
        ParamKind::Linear | ParamKind::Log if last > 0 => spec.min + (spec.max - spec.min) * k as f32 / last as f32,
        _ => k as f32,
    }
}

impl<'a> Console<'a> {
    pub fn new(engine: &'a Engine, state: &'a mut State, library: &'a Library) -> Self {
        Console { engine, state, library }
    }

    /// The engine's strip, if the console has it.
    pub fn strip(&self, s: PmStrip) -> Option<Arc<Strip>> {
        let (kind, i) = s.engine_ref();
        self.engine.strip_at(kind, i)
    }

    /// A strip and its pair partner (a channel of a pair), or the strip alone.
    fn linked(&self, s: PmStrip) -> Vec<Arc<Strip>> {
        let mut out: Vec<_> = self.strip(s).into_iter().collect();
        if let PmStrip::Ch(c) = s {
            if let Some(p) = self.state.partner(c) {
                out.extend(self.strip(PmStrip::Ch(p)));
            }
        }
        out
    }

    // ---- faders and ON ------------------------------------------------------------

    /// A strip's fader step (0-127).
    pub fn level(&self, s: PmStrip) -> u16 {
        self.strip(s).map_or(0, |st| db_step(st.params.fader_db.load()))
    }

    /// Sets a strip's fader: its pair's too; from the panel, a group moves
    /// with it by as many steps (p.56).
    pub fn set_level(&mut self, s: PmStrip, step: u16, origin: Origin) {
        let step = step.min(127);
        let before = self.level(s) as i32;
        for st in self.linked(s) {
            st.params.fader_db.store(step_db(step));
        }
        let member = match s {
            PmStrip::Ch(c) => Some(c),
            PmStrip::StIn => Some(CHANNELS),
            _ => None,
        };
        let Some(m) = member.filter(|_| origin == Origin::Panel) else { return };
        let group = self.state.groups[m];
        if group == 0 {
            return;
        }
        let delta = step as i32 - before;
        let skip = if m < CHANNELS { self.state.partner(m) } else { None };
        for other in 0..self.state.groups.len() {
            if other == m || Some(other) == skip || self.state.groups[other] != group {
                continue;
            }
            let o = if other < CHANNELS { PmStrip::Ch(other) } else { PmStrip::StIn };
            if let Some(st) = self.strip(o) {
                let to = (db_step(st.params.fader_db.load()) as i32 + delta).clamp(0, 127) as u16;
                st.params.fader_db.store(step_db(to));
            }
        }
    }

    /// A strip's ON (its mute off).
    pub fn on(&self, s: PmStrip) -> bool {
        self.strip(s).is_some_and(|st| !st.params.mute.load(Relaxed))
    }

    pub fn set_on(&mut self, s: PmStrip, on: bool) {
        for st in self.linked(s) {
            st.params.mute.store(!on, Relaxed);
        }
    }

    // ---- pans -----------------------------------------------------------------------

    /// A channel's pan, ST OUT's balance, or SEND3-4's output balance
    /// (`PmStrip::Send(2)`), 0-32.
    pub fn pan(&self, s: PmStrip) -> u16 {
        let s = if s == PmStrip::Send(3) { PmStrip::Send(2) } else { s };
        self.strip(s).map_or(16, |st| pan_step(st.params.pan.load()))
    }

    pub fn set_pan(&mut self, s: PmStrip, v: u16) {
        let s = if s == PmStrip::Send(3) { PmStrip::Send(2) } else { s };
        if let Some(st) = self.strip(s) {
            st.params.pan.store(pan_of(v));
        }
    }

    /// A pan of ST IN (0), RTN1 (1) or RTN2 (2): side 0 L, 1 R (0-32).
    pub fn stereo_pan(&self, strip: usize, side: usize) -> u16 {
        self.state.stereo_pans.get(strip).map_or(16, |p| p[side.min(1)] as u16)
    }

    /// Sets one of a stereo strip's two pans; the engine's pan and width
    /// follow (the centre between them, their spread: L0-R32 is 100 %).
    pub fn set_stereo_pan(&mut self, strip: usize, side: usize, v: u16) {
        let Some(pans) = self.state.stereo_pans.get_mut(strip) else { return };
        pans[side.min(1)] = v.min(32) as u8;
        let (l, r) = (pan_of(pans[0] as u16), pan_of(pans[1] as u16));
        let s = if strip == 0 { PmStrip::StIn } else { PmStrip::Rtn(strip - 1) };
        if let Some(st) = self.strip(s) {
            st.params.pan.store((l + r) / 2.0);
            st.params.width.store((r - l) / 2.0);
        }
    }

    // ---- sends ----------------------------------------------------------------------

    /// The send of an input (channel or ST IN) to SEND `bus` (0-3).
    fn send_of(st: &Strip, bus: usize) -> Option<&livedaw_engine::SendParams> {
        st.sends.get_or_create(bus)
    }

    /// An input's send level to SEND `bus` (0-3) as a fader step; in SEND3-4
    /// stereo mode SEND3's is the pair's (SEND4's follows it).
    pub fn send_level(&self, s: PmStrip, bus: usize) -> u16 {
        let bus = if self.state.send34_stereo && bus == 3 { 2 } else { bus };
        self.strip(s).and_then(|st| Self::send_of(&st, bus).map(|p| db_step(p.level_db.load()))).unwrap_or(0)
    }

    pub fn set_send_level(&mut self, s: PmStrip, bus: usize, step: u16) {
        let bus = if self.state.send34_stereo && bus == 3 { 2 } else { bus };
        for st in self.linked(s) {
            if let Some(p) = Self::send_of(&st, bus) {
                p.level_db.store(step_db(step.min(127)));
                p.enabled.store(true, Relaxed);
            }
        }
    }

    /// An input's SEND3-4 pan (stereo mode; ST IN's is its balance), 0-32.
    pub fn send34_pan(&self, s: PmStrip) -> u16 {
        self.strip(s).and_then(|st| Self::send_of(&st, 2).map(|p| pan_step(p.pan.load()))).unwrap_or(16)
    }

    pub fn set_send34_pan(&mut self, s: PmStrip, v: u16) {
        if let Some(st) = self.strip(s) {
            if let Some(p) = Self::send_of(&st, 2) {
                p.pan_link.store(false, Relaxed);
                p.pan.store(pan_of(v));
            }
        }
    }

    /// The inputs: channels 1-16 and ST IN.
    fn inputs(&self) -> impl Iterator<Item = Arc<Strip>> + '_ {
        (0..CHANNELS).map(PmStrip::Ch).chain([PmStrip::StIn]).filter_map(|s| self.strip(s))
    }

    /// Whether SEND `bus`'s sends are PRE fader (p.32, p.45: one setting for
    /// all the inputs).
    pub fn send_pre(&self, bus: usize) -> bool {
        self.strip(PmStrip::Ch(0))
            .and_then(|st| Self::send_of(&st, bus).map(|p| p.mode.load(Relaxed) == send_mode::PRE))
            .unwrap_or(false)
    }

    pub fn set_send_pre(&mut self, bus: usize, pre: bool) {
        let mode = if pre { send_mode::PRE } else { send_mode::POST };
        let buses: &[usize] = if self.state.send34_stereo && bus >= 2 { &[2, 3] } else { std::slice::from_ref(&bus) };
        for st in self.inputs().collect::<Vec<_>>() {
            for &b in buses {
                if let Some(p) = Self::send_of(&st, b) {
                    p.mode.store(mode, Relaxed);
                }
            }
        }
    }

    /// Makes SEND3-4 a stereo pair or two mono sends (`Send3,4 Configuration
    /// Execute`, p.46): stereo, SEND3's bus is stereo and plays on SEND3's and
    /// SEND4's outputs; the inputs' sends take SEND3's levels, pairs' odd
    /// channels pan hard left and even hard right, ST IN's L feeds SEND3 and
    /// R SEND4; SEND3's ON, PRE / POST, fader and patch point hold for both.
    /// A dynamics processor on SEND3 or SEND4 is unpatched.
    pub fn set_send34_stereo(&mut self, stereo: bool) {
        self.state.send34_stereo = stereo;
        for i in 0..PROCESSORS {
            if matches!(self.state.processors[i].patch, Patch::Send3 | Patch::Send4) {
                self.set_patch(i, Patch::Off);
            }
        }
        let pre = self.send_pre(2);
        let (Some(send3), Some(send4)) = (self.strip(PmStrip::Send(2)), self.strip(PmStrip::Send(3))) else { return };
        send3.params.stereo.store(stereo, Relaxed);
        send3.params.pan.store(0.0);
        if stereo {
            send4.params.fader_db.store(send3.params.fader_db.load());
            send4.params.mute.store(send3.params.mute.load(Relaxed), Relaxed);
            send4.params.process_order.store(send3.params.process_order.load(Relaxed), Relaxed);
        }
        // SEND4's output (interface output 4) plays SEND3's right side or SEND4.
        let right = if stereo { strip_source_input(StripKind::Bus, 2, 1) } else { strip_source_input(StripKind::Bus, 3, 0) };
        self.engine.output_patch().set(3, right);
        for c in 0..CHANNELS {
            let pan = match (stereo, self.state.partner(c)) {
                (true, Some(_)) => {
                    if c % 2 == 0 {
                        0
                    } else {
                        32
                    }
                }
                _ => 16,
            };
            self.set_send34_pan(PmStrip::Ch(c), pan);
        }
        self.set_send34_pan(PmStrip::StIn, 16);
        self.set_send_pre(2, pre);
    }

    // ---- phase and EQ ------------------------------------------------------------

    pub fn phase(&self, s: PmStrip) -> bool {
        self.strip(s).is_some_and(|st| st.params.input.invert.load(Relaxed))
    }

    pub fn set_phase(&mut self, s: PmStrip, reverse: bool) {
        for st in self.linked(s) {
            st.params.input.invert.store(reverse, Relaxed);
        }
    }

    pub fn eq_on(&self, s: PmStrip) -> bool {
        self.strip(s).is_some_and(|st| st.params.eq().on.load(Relaxed))
    }

    pub fn set_eq_on(&mut self, s: PmStrip, on: bool) {
        for st in self.linked(s) {
            st.params.eq().on.store(on, Relaxed);
        }
    }

    /// An EQ value as its step: `band` 0-2 (LOW, MID, HIGH), `what` 0 F, 1 G, 2 Q.
    pub fn eq_value(&self, s: PmStrip, band: usize, what: usize) -> u16 {
        let id = EQ_IDS[band.min(2)][what.min(2)];
        let Some(st) = self.strip(s) else { return 0 };
        let eq = st.params.eq();
        let (Some(spec), Some(v)) = (eq.model_spec().and_then(|m| m.params.iter().find(|p| p.id == id)), eq.param(id)) else {
            return 0;
        };
        param_step(spec, v.load())
    }

    pub fn set_eq_value(&mut self, s: PmStrip, band: usize, what: usize, step: u16) {
        let id = EQ_IDS[band.min(2)][what.min(2)];
        for st in self.linked(s) {
            let eq = st.params.eq();
            if let (Some(spec), Some(v)) = (eq.model_spec().and_then(|m| m.params.iter().find(|p| p.id == id)), eq.param(id)) {
                v.store(step_param(spec, step));
            }
        }
    }

    // ---- pairs and groups ------------------------------------------------------------

    /// Pairs or unpairs channels `2 pair + 1` / `2 pair + 2` (`Pair Execute`,
    /// p.58): 0 MONO x 2 (the settings stay), 1 ST (odd -> even: the odd
    /// channel's settings copied), 2 ST (even -> odd), 3 ST (RESET: both
    /// reset, odd panned hard left, even hard right). Pairing takes both out of
    /// their groups and unpatches a dynamics processor on either.
    pub fn execute_pair(&mut self, pair: usize, mode: u16) {
        if pair >= self.state.pairs.len() {
            return;
        }
        let (odd, even) = (2 * pair, 2 * pair + 1);
        if mode == 0 {
            self.state.pairs[pair] = false;
            return;
        }
        for c in [odd, even] {
            self.state.groups[c] = 0;
            if let Some(i) = self.state.processor_at(Patch::Channel(c)) {
                self.set_patch(i, Patch::Off);
            }
        }
        let (Some(o), Some(e)) = (self.strip(PmStrip::Ch(odd)), self.strip(PmStrip::Ch(even))) else { return };
        match mode {
            1 => copy_channel(&o, &e),
            2 => copy_channel(&e, &o),
            _ => {
                for st in [&o, &e] {
                    reset_channel(st);
                }
                o.params.pan.store(-100.0);
                e.params.pan.store(100.0);
            }
        }
        self.state.pairs[pair] = true;
    }

    /// A channel's or ST IN's group: 0 none, 1-4 A-D (a pair's partner too).
    pub fn set_group(&mut self, member: usize, group: u16) {
        let Some(g) = self.state.groups.get_mut(member) else { return };
        *g = group.min(4) as u8;
        if member < CHANNELS {
            if let Some(p) = self.state.partner(member) {
                self.state.groups[p] = group.min(4) as u8;
            }
        }
    }

    // ---- dynamics processors ------------------------------------------------------

    /// The engine's strips a patch reaches (a channel and its pair partner).
    fn patched(&self, patch: Patch) -> Vec<Arc<Strip>> {
        match patch {
            Patch::Off => vec![],
            Patch::Channel(c) => self.linked(PmStrip::Ch(c)),
            Patch::Send3 => self.strip(PmStrip::Send(2)).into_iter().collect(),
            Patch::Send4 => self.strip(PmStrip::Send(3)).into_iter().collect(),
            Patch::Stereo => self.strip(PmStrip::StOut).into_iter().collect(),
        }
    }

    /// Patches processor `i` into `patch` (p.72); a patch another processor
    /// uses is refused (false). The strips it leaves have their dynamics off.
    pub fn set_patch(&mut self, i: usize, patch: Patch) -> bool {
        if i >= PROCESSORS {
            return false;
        }
        let patch = match patch {
            // A pair's even channel is patched through its odd one.
            Patch::Channel(c) if self.state.partner(c).is_some() => Patch::Channel(c & !1),
            p => p,
        };
        if patch != Patch::Off && self.state.processor_at(patch).is_some_and(|other| other != i) {
            return false;
        }
        for st in self.patched(self.state.processors[i].patch) {
            st.params.dynamics().on.store(false, Relaxed);
        }
        self.state.processors[i].patch = patch;
        self.apply(i);
        true
    }

    /// Writes processor `i` onto the strips it is patched into: its type and
    /// values, its key, and ON for the sides its IN mode takes.
    pub fn apply(&self, i: usize) {
        let p = self.state.processors[i];
        let strips = self.patched(p.patch);
        let pair = strips.len() == 2;
        for (side, st) in strips.iter().enumerate() {
            let d = st.params.dynamics();
            let model = d.table().iter().position(|m| m.token == DYN_MODELS[p.model as usize].token).unwrap_or(0);
            if d.model_index() != model || d.model.load(Relaxed) < 0 {
                d.set_model(model);
            }
            for (k, v) in p.values.iter().enumerate() {
                if let Some(x) = d.values.get(k) {
                    x.store(*v);
                }
            }
            let (source, tap) = self.key_source(p.key_in, st.kind());
            d.sc_source.store(source, Relaxed);
            d.sc_tap.store(tap, Relaxed);
            let takes = !pair || p.in_mode == 0 || p.in_mode as usize == side + 1;
            d.on.store(p.on && takes, Relaxed);
        }
    }

    /// A key as the engine's `sc_source` and `sc_tap` for a block on a strip
    /// of `kind`: a channel keys an input channel post-EQ (its tap 3); SEND3,
    /// SEND4 and ST OUT key each other (ST-L and ST-R: ST OUT, both sides). A
    /// key the engine can't take there (a channel keyed by a send) keys itself.
    fn key_source(&self, key: KeyIn, kind: StripKind) -> (i32, i32) {
        const POST_EQ: i32 = 2;
        const OUTPUT_PFL: i32 = 2;
        let input = kind == StripKind::Channel;
        match (key, input) {
            (KeyIn::Channel(c), true) => (KeySource::Channel(c).code(), POST_EQ),
            (KeyIn::Send3, false) => (KeySource::Strip(StripKind::Bus, 2).code(), OUTPUT_PFL),
            (KeyIn::Send4, false) => (KeySource::Strip(StripKind::Bus, 3).code(), OUTPUT_PFL),
            (KeyIn::StereoL | KeyIn::StereoR, false) => (KeySource::Strip(StripKind::Main, 0).code(), OUTPUT_PFL),
            _ => (KeySource::SelfKey.code(), POST_EQ),
        }
    }

    pub fn set_processor_on(&mut self, i: usize, on: bool) {
        if let Some(p) = self.state.processors.get_mut(i) {
            p.on = on;
            self.apply(i);
        }
    }

    pub fn set_key_in(&mut self, i: usize, key: KeyIn) {
        if let Some(p) = self.state.processors.get_mut(i) {
            p.key_in = key;
            self.apply(i);
        }
    }

    pub fn set_in_mode(&mut self, i: usize, mode: u16) {
        if let Some(p) = self.state.processors.get_mut(i) {
            p.in_mode = mode.min(2) as u8;
            self.apply(i);
        }
    }

    /// Recalls dynamics program `n` (0-19) into processor `i` (`Comp n Type`).
    pub fn recall_dyn_program(&mut self, i: usize, n: usize) {
        let Some(prog) = self.library.program(n) else { return };
        if let Some(p) = self.state.processors.get_mut(i) {
            p.program = n as u8;
            p.model = prog.model;
            p.values = prog.values;
            self.apply(i);
        }
    }

    /// Processor `i`'s parameter `k` (0-4) as its step.
    pub fn processor_param(&self, i: usize, k: usize) -> u16 {
        let Some(p) = self.state.processors.get(i) else { return 0 };
        DYN_MODELS[p.model as usize].params.get(k).map_or(0, |spec| param_step(spec, p.values[k]))
    }

    pub fn set_processor_param(&mut self, i: usize, k: usize, step: u16) {
        let Some(p) = self.state.processors.get_mut(i) else { return };
        let Some(spec) = DYN_MODELS[p.model as usize].params.get(k) else { return };
        p.values[k] = step_param(spec, step);
        self.apply(i);
    }

    /// Whether the dynamics patch point of SEND3 (0), SEND4 (1) or ST OUT (2)
    /// is PRE fader (p.74; the strip's process order, 1 PRE).
    pub fn patch_point_pre(&self, which: usize) -> bool {
        let s = [PmStrip::Send(2), PmStrip::Send(3), PmStrip::StOut][which.min(2)];
        self.strip(s).is_some_and(|st| st.params.process_order.load(Relaxed) == 1)
    }

    pub fn set_patch_point_pre(&mut self, which: usize, pre: bool) {
        let s = [PmStrip::Send(2), PmStrip::Send(3), PmStrip::StOut][which.min(2)];
        if let Some(st) = self.strip(s) {
            st.params.process_order.store(pre as i32, Relaxed);
        }
    }

    // ---- effects and the EQ library ------------------------------------------------

    /// Recalls effects program `n` (0-39) into effect `slot` (0-1): its type
    /// and values onto the FX slot (`Internal Effect n Type`, p.34).
    pub fn recall_fx_program(&mut self, slot: usize, n: usize) {
        let Some(prog) = self.library.fx_program(n) else { return };
        let Some(fx) = self.engine.controls.fx.get(slot) else { return };
        let models = self.engine.fx_models();
        let Some(model) = livedaw_dsp::model::model_index(models, prog.token) else { return };
        if fx.model_index_in(models) != model {
            self.engine.set_fx_model(slot, model, true);
        }
        for (k, v) in prog.values.iter().enumerate() {
            if let Some(x) = fx.values.get(k) {
                x.store(*v);
            }
        }
        if let Some(p) = self.state.fx_programs.get_mut(slot) {
            *p = n as u8;
        }
    }

    /// Effect `slot`'s parameter `k` as its step, and how many steps it has.
    pub fn fx_param(&self, slot: usize, k: usize) -> (u16, u16) {
        let Some(fx) = self.engine.controls.fx.get(slot) else { return (0, 1) };
        let models = self.engine.fx_models();
        let spec = &models[fx.model_index_in(models).min(models.len() - 1)];
        match (spec.params.get(k), fx.values.get(k)) {
            (Some(ps), Some(v)) => (param_step(ps, v.load()), ps.steps.max(1) as u16),
            _ => (0, 1),
        }
    }

    pub fn set_fx_param(&mut self, slot: usize, k: usize, step: u16) {
        let Some(fx) = self.engine.controls.fx.get(slot) else { return };
        let models = self.engine.fx_models();
        let spec = &models[fx.model_index_in(models).min(models.len() - 1)];
        if let (Some(ps), Some(v)) = (spec.params.get(k), fx.values.get(k)) {
            v.store(step_param(ps, step));
        }
    }

    /// Recalls EQ program `n` (0-49) onto a strip's EQ (and its pair partner's).
    pub fn recall_eq_program(&mut self, s: PmStrip, n: usize) {
        use livedaw_dsp::promix::{HIGH_FREQS, LOW_FREQS, MID_FREQS};
        let Some(prog) = self.library.eq_program(n) else { return };
        for (band, (g, f, q)) in prog.bands.iter().enumerate() {
            let table = [LOW_FREQS, MID_FREQS, HIGH_FREQS][band];
            let fi = table.iter().position(|x| x == f).unwrap_or(0) as u16;
            self.set_eq_value(s, band, 0, fi);
            self.set_eq_value(s, band, 1, (*g + 15.0) as u16);
            self.set_eq_value(s, band, 2, *q as u16);
        }
    }

    /// The strip's EQ now as a program named `name` (STORE, p.22).
    pub fn eq_program_of(&self, s: PmStrip, name: &str) -> crate::library::EqProgram {
        use livedaw_dsp::promix::{HIGH_FREQS, LOW_FREQS, MID_FREQS};
        let band = |b: usize| {
            let table = [LOW_FREQS, MID_FREQS, HIGH_FREQS][b];
            let f = table.get(self.eq_value(s, b, 0) as usize).copied().unwrap_or(table[0]);
            (self.eq_value(s, b, 1) as f32 - 15.0, f, self.eq_value(s, b, 2) as u8)
        };
        crate::library::EqProgram { name: name.into(), bands: [band(0), band(1), band(2)] }
    }

    /// Processor `i` now as a dynamics program (p.77).
    pub fn dyn_program_of(&self, i: usize, name: &str) -> Option<crate::library::DynProgram> {
        let p = self.state.processors.get(i)?;
        Some(crate::library::DynProgram { name: name.into(), model: p.model, values: p.values })
    }

    /// Effect `slot` now as an effects program (p.36).
    pub fn fx_program_of(&self, slot: usize, name: &str) -> Option<crate::library::FxProgram> {
        let fx = self.engine.controls.fx.get(slot)?;
        let models = self.engine.fx_models();
        let spec = models.get(fx.model_index_in(models))?;
        let values = fx.values.iter().take(spec.params.len()).map(|v| v.load()).collect();
        Some(crate::library::FxProgram { name: name.into(), token: spec.token, values })
    }

    /// CUE (Sidecar's solo) of a strip.
    pub fn cue(&self, s: PmStrip) -> bool {
        self.strip(s).is_some_and(|st| st.params.solo.load(Relaxed))
    }

    pub fn set_cue(&mut self, s: PmStrip, on: bool) {
        for st in self.linked(s) {
            st.params.solo.store(on, Relaxed);
        }
    }
}

/// Copies what a pair links (PHASE, EQ, ON, fader, CUE, SEND1-4: p.58) from
/// one channel to the other.
fn copy_channel(from: &Strip, to: &Strip) {
    let (f, t) = (&from.params, &to.params);
    t.input.invert.store(f.input.invert.load(Relaxed), Relaxed);
    let (fe, te) = (f.eq(), t.eq());
    te.on.store(fe.on.load(Relaxed), Relaxed);
    for (a, b) in fe.values.iter().zip(&te.values) {
        b.store(a.load());
    }
    t.mute.store(f.mute.load(Relaxed), Relaxed);
    t.fader_db.store(f.fader_db.load());
    t.solo.store(f.solo.load(Relaxed), Relaxed);
    for bus in 0..4 {
        if let (Some(a), Some(b)) = (from.sends.get_or_create(bus), to.sends.get_or_create(bus)) {
            b.level_db.store(a.level_db.load());
            b.enabled.store(a.enabled.load(Relaxed), Relaxed);
            b.mode.store(a.mode.load(Relaxed), Relaxed);
        }
    }
}

/// A channel as mix scene 00 has it: EQ flat (program 1 RESET), phase
/// normal, ON, its sends and fader down.
fn reset_channel(st: &Strip) {
    let p = &st.params;
    p.input.invert.store(false, Relaxed);
    let eq = p.eq();
    eq.set_model(eq.model_index());
    p.mute.store(false, Relaxed);
    p.fader_db.store(livedaw_dsp::fader::FADER_MIN);
    for bus in 0..4 {
        if let Some(s) = st.sends.get_or_create(bus) {
            s.level_db.store(livedaw_dsp::fader::FADER_MIN);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_engine::config::{ConsoleConfig, Counts};
    use livedaw_engine::Profile;

    fn promix() -> Engine {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::ProMix01, counts: Counts::PROMIX01 });
        e
    }

    #[test]
    fn steps_and_pans() {
        assert_eq!(db_step(step_db(100)), 100);
        assert!(step_db(100).abs() < 1e-4);
        assert_eq!(step_db(0), livedaw_dsp::fader::FADER_MIN);
        for v in 0..=32 {
            assert_eq!(pan_step(pan_of(v)), v);
        }
        assert_eq!((pan_of(0), pan_of(16), pan_of(32)), (-100.0, 0.0, 100.0));
        let eq = &livedaw_dsp::promix::EQ[0];
        let lf = eq.params.iter().find(|p| p.id == "lf").unwrap();
        assert_eq!(param_step(lf, 8.0), 8);
        let lg = eq.params.iter().find(|p| p.id == "lg").unwrap();
        assert_eq!((param_step(lg, -15.0), param_step(lg, 0.0), step_param(lg, 30)), (0, 15, 15.0));
    }

    #[test]
    fn pairs_link_and_groups_follow_the_panel() {
        let e = promix();
        let (mut s, lib) = (State::default(), Library::default());
        let mut c = Console::new(&e, &mut s, &lib);
        // ST (1->2): channel 1's settings onto 2, then linked.
        c.set_level(PmStrip::Ch(0), 100, Origin::Panel);
        c.set_phase(PmStrip::Ch(0), true);
        c.execute_pair(0, 1);
        assert!(c.state.pairs[0]);
        assert_eq!((c.level(PmStrip::Ch(1)), c.phase(PmStrip::Ch(1))), (100, true));
        c.set_on(PmStrip::Ch(1), false);
        assert!(!c.on(PmStrip::Ch(0)), "ON is linked");
        c.set_eq_value(PmStrip::Ch(0), 1, 1, 27);
        assert_eq!(c.eq_value(PmStrip::Ch(1), 1, 1), 27, "MID G +12 dB on both");
        // RESET: hard left and right.
        c.execute_pair(1, 3);
        let pan = |c: &Console, i| c.strip(PmStrip::Ch(i)).unwrap().params.pan.load();
        assert_eq!((pan(&c, 2), pan(&c, 3)), (-100.0, 100.0));
        c.execute_pair(1, 0);
        assert!(!c.state.pairs[1]);
        // A group: channels 5, 6 and ST IN in A. The panel moves them by as many
        // steps; a received control change moves only its channel.
        for m in [4, 5, 16] {
            c.set_group(m, 1);
        }
        c.set_level(PmStrip::Ch(4), 100, Origin::Midi);
        c.set_level(PmStrip::Ch(5), 90, Origin::Midi);
        c.set_level(PmStrip::StIn, 80, Origin::Midi);
        c.set_level(PmStrip::Ch(4), 110, Origin::Panel);
        assert_eq!((c.level(PmStrip::Ch(5)), c.level(PmStrip::StIn)), (100, 90));
        c.set_level(PmStrip::Ch(5), 50, Origin::Midi);
        assert_eq!((c.level(PmStrip::Ch(4)), c.level(PmStrip::StIn)), (110, 90));
        // Pairing takes channels out of their group.
        c.execute_pair(2, 1);
        assert_eq!((c.state.groups[4], c.state.groups[5]), (0, 0));
    }

    #[test]
    fn the_processors_are_a_pool_patched_into_strips() {
        let e = promix();
        let (mut s, lib) = (State::default(), Library::default());
        let mut c = Console::new(&e, &mut s, &lib);
        let dyn_on = |c: &Console, st: PmStrip| c.strip(st).unwrap().params.dynamics().on.load(Relaxed);
        // COMP1 on channel 3, as GATE FAST.
        c.recall_dyn_program(0, 6);
        c.set_processor_on(0, true);
        assert!(c.set_patch(0, Patch::Channel(2)));
        assert!(dyn_on(&c, PmStrip::Ch(2)) && !dyn_on(&c, PmStrip::Ch(3)));
        let d = c.strip(PmStrip::Ch(2)).unwrap();
        assert_eq!(d.params.dynamics().model_spec().unwrap().token, "PMGATE");
        assert_eq!(d.params.dynamics().param("range").unwrap().load(), -20.0);
        // Another processor can't take the same patch.
        assert!(!c.set_patch(1, Patch::Channel(2)));
        // Its edits reach the strip; a key from channel 9 post-EQ.
        c.set_processor_param(0, 4, 40); // RANGE -40 dB
        assert_eq!(d.params.dynamics().param("range").unwrap().load(), -40.0);
        c.set_key_in(0, KeyIn::Channel(8));
        assert_eq!(d.params.dynamics().sc_source.load(Relaxed), 8);
        // Moved to ST OUT: channel 3's dynamics off, ST OUT's on.
        assert!(c.set_patch(0, Patch::Stereo));
        assert!(!dyn_on(&c, PmStrip::Ch(2)) && dyn_on(&c, PmStrip::StOut));
        assert_eq!(c.processor_param(0, 4), 40);
        // On a pair, IN picks the sides: EVEN only.
        c.execute_pair(4, 3);
        assert!(c.set_patch(1, Patch::Channel(9)), "a pair's even channel patches the pair");
        assert_eq!(c.state.processors[1].patch, Patch::Channel(8));
        c.set_processor_on(1, true);
        assert!(dyn_on(&c, PmStrip::Ch(8)) && dyn_on(&c, PmStrip::Ch(9)));
        c.set_in_mode(1, 2);
        assert!(!dyn_on(&c, PmStrip::Ch(8)) && dyn_on(&c, PmStrip::Ch(9)));
        // Pairing cancels a patch on either channel.
        c.execute_pair(4, 0);
        c.execute_pair(4, 1);
        assert_eq!(c.state.processors[1].patch, Patch::Off);
        assert!(!dyn_on(&c, PmStrip::Ch(8)));
    }

    #[test]
    fn sends_pre_post_and_send34_as_a_stereo_pair() {
        let e = promix();
        let (mut s, lib) = (State::default(), Library::default());
        let mut c = Console::new(&e, &mut s, &lib);
        c.set_send_pre(0, false);
        c.set_send_pre(2, true);
        assert!(!c.send_pre(0) && c.send_pre(2));
        let mode = |c: &Console, st: PmStrip, bus| c.strip(st).unwrap().sends.get_or_create(bus).unwrap().mode.load(Relaxed);
        assert_eq!((mode(&c, PmStrip::Ch(15), 0), mode(&c, PmStrip::StIn, 2)), (send_mode::POST, send_mode::PRE));
        c.set_send_level(PmStrip::Ch(0), 2, 115);
        c.execute_pair(1, 3);
        c.set_send34_stereo(true);
        assert!(c.strip(PmStrip::Send(2)).unwrap().params.stereo.load(Relaxed));
        assert_eq!(e.output_patch().get(3), strip_source_input(StripKind::Bus, 2, 1), "SEND4's output plays SEND3's R");
        assert_eq!((c.send34_pan(PmStrip::Ch(2)), c.send34_pan(PmStrip::Ch(3)), c.send34_pan(PmStrip::Ch(0))), (0, 32, 16));
        assert_eq!(c.send_level(PmStrip::Ch(0), 3), 115, "SEND4's level is SEND3's");
        assert!(c.send_pre(3));
        c.set_send34_stereo(false);
        assert_eq!(e.output_patch().get(3), strip_source_input(StripKind::Bus, 3, 0));
    }

    #[test]
    fn effects_programs_and_eq_programs_recall() {
        let e = promix();
        let (mut s, lib) = (State::default(), Library::default());
        let mut c = Console::new(&e, &mut s, &lib);
        // A new console: effect 1 REVERB HALL 1, effect 2 DELAY L-C-R.
        let token = |c: &Console, slot: usize| {
            let m = c.engine.fx_models();
            m[c.engine.controls.fx[slot].model_index_in(m)].token
        };
        assert_eq!((token(&c, 0), token(&c, 1)), ("PMREV", "PMDLCR"));
        c.recall_fx_program(1, 18); // KARAOKE ECHO 2
        assert_eq!((token(&c, 1), c.state.fx_programs[1]), ("PMECHO", 18));
        assert_eq!(c.engine.controls.fx[1].param_in(c.engine.fx_models(), "fbr").unwrap().load(), -55.0);
        let (step, steps) = c.fx_param(1, 2); // DLY R 180.0 ms
        assert_eq!((step, steps), (1799, 3220));
        c.set_fx_param(1, 2, 0);
        assert!((c.engine.controls.fx[1].param_in(c.engine.fx_models(), "dr").unwrap().load() - 0.1).abs() < 1e-4);
        // EQ program 27 NOTCH 4kHz on channel 1 and its partner.
        c.execute_pair(0, 3);
        c.recall_eq_program(PmStrip::Ch(0), 26);
        let eq = c.strip(PmStrip::Ch(1)).unwrap();
        assert_eq!(eq.params.eq().param("hg").unwrap().load(), -10.0);
        assert_eq!(eq.params.eq().param("hf").unwrap().load(), 12.0, "4.0 kHz, HIGH's 13th");
        assert_eq!(eq.params.eq().param("hq").unwrap().load(), 0.0, "1/6 oct");
    }
}
