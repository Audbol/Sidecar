//! The 02R over the engine: its panel's operations, which move several of the
//! engine's values at once (a pair's two channels, a fader group's faders, a
//! mute group's ON keys, a gang's pans), and the values as the console has
//! them (fader steps 0-127, pans 0-32, attenuation in dB, delay in samples,
//! EQ and dynamics steps).
//!
//! The strips (`Y2Strip`) are the engine's (see the crate).

use crate::library::{ChannelProgram, Kind, Library, Program, CHANNEL_PROGRAMS};
use crate::params;
use crate::state::{self, DynSettings, KeyIn, State, SurroundPan, CHANNELS, DYN_STRIPS, LINES, MEMBERS, MICS};
use livedaw_dsp::surround;
use livedaw_dsp::fader::FaderLaw;
use livedaw_dsp::y02r::DYN_MODELS;
use livedaw_engine::keytap::KeySource;
use livedaw_engine::profile::y02r;
use livedaw_engine::sends::send_mode;
use livedaw_engine::{Engine, Strip, StripKind};
pub use livedaw_promix::console::{pan_of, pan_step, param_step, step_param};
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

/// An 02R strip.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Y2Strip {
    /// MIC/LINE 1-16 (0-based).
    Mic(usize),
    /// LINE 17/18-23/24 (0-3).
    Line(usize),
    /// TAPE 1-16 (0-based).
    Tape(usize),
    /// EFF1-2 (0-based).
    Eff(usize),
    /// BUS 1-8 (0-based).
    Bus(usize),
    /// AUX 1-8 (0-based).
    Aux(usize),
    Stereo,
}

impl Y2Strip {
    /// The engine's strip.
    pub fn engine_ref(self) -> (StripKind, usize) {
        match self {
            Y2Strip::Mic(c) => (StripKind::Channel, c),
            Y2Strip::Line(l) => (StripKind::Channel, y02r::LINE + l),
            Y2Strip::Tape(t) => (StripKind::Channel, y02r::TAPE + t),
            Y2Strip::Eff(e) => (StripKind::Aux, e),
            Y2Strip::Bus(b) => (StripKind::Bus, b),
            Y2Strip::Aux(a) => (StripKind::Bus, y02r::AUX + a),
            Y2Strip::Stereo => (StripKind::Main, 0),
        }
    }

    /// The input channel `c` (0-35, the engine's numbering).
    pub fn channel(c: usize) -> Y2Strip {
        match c {
            c if c < y02r::LINE => Y2Strip::Mic(c),
            c if c < y02r::TAPE => Y2Strip::Line(c - y02r::LINE),
            c => Y2Strip::Tape(c - y02r::TAPE),
        }
    }

    /// Its number among the strips that take a group (the channels, then
    /// EFF1-2), if it is one.
    pub fn member(self) -> Option<usize> {
        match self.engine_ref() {
            (StripKind::Channel, c) => Some(c),
            (StripKind::Aux, e) => Some(CHANNELS + e),
            _ => None,
        }
    }

    pub fn of_member(m: usize) -> Option<Y2Strip> {
        match m {
            m if m < CHANNELS => Some(Y2Strip::channel(m)),
            m if m < MEMBERS => Some(Y2Strip::Eff(m - CHANNELS)),
            _ => None,
        }
    }

    /// Its number among the strips with a dynamics processor (the channels,
    /// BUS 1-8, STEREO), if it is one.
    pub fn dyn_index(self) -> Option<usize> {
        match self {
            Y2Strip::Eff(_) | Y2Strip::Aux(_) => None,
            Y2Strip::Bus(b) => Some(CHANNELS + b),
            Y2Strip::Stereo => Some(DYN_STRIPS - 1),
            s => s.member(),
        }
    }

    /// Its number among the faders recall safe and fade times cover.
    pub fn fader_index(self) -> Option<usize> {
        match self {
            Y2Strip::Stereo => Some(MEMBERS),
            s => s.member(),
        }
    }

    /// Its index into `State::stereo_pans` (LINE 17/18-23/24, EFF1-2).
    pub fn stereo_index(self) -> Option<usize> {
        match self {
            Y2Strip::Line(l) => Some(l),
            Y2Strip::Eff(e) => Some(LINES + e),
            _ => None,
        }
    }

    /// The fader law it runs on: STEREO's own, else the channels'.
    pub fn law(self) -> FaderLaw {
        if self == Y2Strip::Stereo {
            FaderLaw::Y02R_STEREO
        } else {
            FaderLaw::Y02R
        }
    }
}

/// Who moved a value: the panel (linked values follow: a group's faders, a
/// mute group's ON keys, a gang's pans) or a remote (that value and its pair
/// only).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Origin {
    Panel,
    Remote,
}

/// The EQ bands' parameter ids (`livedaw_dsp::y02r::EQ`): F, G, Q of LOW,
/// L-MID, H-MID, HIGH.
pub const EQ_IDS: [[&str; 3]; 4] = [["lf", "lg", "lq"], ["lmf", "lmg", "lmq"], ["hmf", "hmg", "hmq"], ["hf", "hg", "hq"]];

/// The most delay (samples, MAN p.194), and the engine's delay unit for samples
/// (`livedaw_dsp::delay::DELAY_MODES`: M, FT, MS, SMP).
pub const MAX_DELAY: u16 = 2600;
const SAMPLES: i32 = 3;

/// A fader step (0-127) as dB on `law`.
pub fn step_db(law: FaderLaw, step: u16) -> f32 {
    law.to_db(step.min(127) as f32 / 127.0)
}

/// The fader step of a level in dB on `law`.
pub fn db_step(law: FaderLaw, db: f32) -> u16 {
    (law.to_pos(db) * 127.0).round() as u16
}

/// The 02R over `engine`, with its own `state`.
pub struct Console<'a> {
    pub engine: &'a Engine,
    pub state: &'a mut State,
}

impl<'a> Console<'a> {
    pub fn new(engine: &'a Engine, state: &'a mut State) -> Self {
        Console { engine, state }
    }

    /// The engine's strip, if the console has it.
    pub fn strip(&self, s: Y2Strip) -> Option<Arc<Strip>> {
        let (kind, i) = s.engine_ref();
        self.engine.strip_at(kind, i)
    }

    /// A strip's pair partner: a channel's, or a bus's or aux's.
    pub fn partner(&self, s: Y2Strip) -> Option<Y2Strip> {
        match s.engine_ref() {
            (StripKind::Channel, c) => self.state.partner(c).map(Y2Strip::channel),
            (StripKind::Bus, b) => {
                self.state.bus_partner(b).map(|p| if p < y02r::AUX { Y2Strip::Bus(p) } else { Y2Strip::Aux(p - y02r::AUX) })
            }
            _ => None,
        }
    }

    /// A strip and its pair partner, or the strip alone.
    fn linked(&self, s: Y2Strip) -> Vec<Arc<Strip>> {
        let mut out: Vec<_> = self.strip(s).into_iter().collect();
        out.extend(self.partner(s).and_then(|p| self.strip(p)));
        out
    }

    // ---- faders and ON ------------------------------------------------------------

    /// A strip's fader step (0-127): a channel's, a return's, a master's.
    pub fn level(&self, s: Y2Strip) -> u16 {
        self.strip(s).map_or(0, |st| db_step(s.law(), st.params.fader_db.load()))
    }

    /// Sets a strip's fader: its pair's too; from the panel, its fader group
    /// moves with it by as many steps, keeping their offsets (MAN p.148).
    pub fn set_level(&mut self, s: Y2Strip, step: u16, origin: Origin) {
        let step = step.min(127);
        let before = self.level(s) as i32;
        // MS decoding: M and S on faders of their own.
        let ms = self.ms_pair(s).is_some();
        for st in self.linked(s).into_iter().take(if ms { 1 } else { 2 }) {
            st.params.fader_db.store(step_db(s.law(), step));
        }
        let Some(m) = s.member().filter(|_| origin == Origin::Panel) else { return };
        let delta = step as i32 - before;
        let skip = self.partner(s).filter(|_| !ms).and_then(Y2Strip::member);
        for other in self.state.group_members(m, false) {
            if Some(other) == skip {
                continue;
            }
            let Some(o) = Y2Strip::of_member(other) else { continue };
            let to = (self.level(o) as i32 + delta).clamp(0, 127) as u16;
            for st in self.linked(o) {
                st.params.fader_db.store(step_db(o.law(), to));
            }
        }
    }

    /// A strip's ON (its mute off).
    pub fn on(&self, s: Y2Strip) -> bool {
        self.strip(s).is_some_and(|st| !st.params.mute.load(Relaxed))
    }

    /// Sets a strip's ON (its pair's too); from the panel, a change flips the
    /// ON of its mute group's members, each from its own state (MAN p.149).
    pub fn set_on(&mut self, s: Y2Strip, on: bool, origin: Origin) {
        let changed = self.on(s) != on;
        for st in self.linked(s) {
            st.params.mute.store(!on, Relaxed);
        }
        let Some(m) = s.member().filter(|_| origin == Origin::Panel && changed) else { return };
        let skip = self.partner(s).and_then(Y2Strip::member);
        for other in self.state.group_members(m, true) {
            if Some(other) == skip {
                continue;
            }
            if let Some(o) = Y2Strip::of_member(other) {
                let flipped = !self.on(o);
                for st in self.linked(o) {
                    st.params.mute.store(!flipped, Relaxed);
                }
            }
        }
    }

    // ---- pans, attenuator, phase, delay ------------------------------------------

    /// A mono channel's pan, or STEREO's balance (0-32, 16 the centre; MAN p.37).
    pub fn pan(&self, s: Y2Strip) -> u16 {
        if let Some(i) = s.stereo_index() {
            let p = self.state.stereo_pans[i];
            return ((p[0] as u16) + (p[1] as u16)) / 2;
        }
        self.strip(s).map_or(16, |st| pan_step(st.params.pan.load()))
    }

    /// Sets a mono channel's pan or STEREO's balance; from the panel, a gang
    /// partner moves by as much the same way, keeping their offset (L5 / R10
    /// ganged, the left to L7 takes the right to R8: MAN p.38).
    pub fn set_pan(&mut self, s: Y2Strip, v: u16, origin: Origin) {
        let v = v.min(32);
        if let Some(i) = s.stereo_index() {
            // Both sides by as much.
            let delta = v as i32 - self.pan(s) as i32;
            for side in 0..2 {
                let to = (self.state.stereo_pans[i][side] as i32 + delta).clamp(0, 32) as u16;
                self.set_stereo_pan(s, side, to);
            }
            return;
        }
        let before = self.pan(s) as i32;
        if let Some(st) = self.strip(s) {
            st.params.pan.store(pan_of(v));
        }
        let (StripKind::Channel, c) = s.engine_ref() else { return };
        let Some(g) = self.state.gang_partner(c).filter(|_| origin == Origin::Panel) else { return };
        let other = Y2Strip::channel(g);
        let to = (self.pan(other) as i32 + (v as i32 - before)).clamp(0, 32) as u16;
        if let Some(st) = self.strip(other) {
            st.params.pan.store(pan_of(to));
        }
    }

    /// One of a stereo strip's two pans (LINE 17/18-23/24, EFF1-2): side 0 L, 1 R.
    pub fn stereo_pan(&self, s: Y2Strip, side: usize) -> u16 {
        s.stereo_index().map_or(16, |i| self.state.stereo_pans[i][side.min(1)] as u16)
    }

    /// Sets one of a stereo strip's pans; the engine's pan and width follow
    /// (the centre between them, their spread: L16-R16 is 100 %).
    pub fn set_stereo_pan(&mut self, s: Y2Strip, side: usize, v: u16) {
        let Some(i) = s.stereo_index() else { return };
        let pans = &mut self.state.stereo_pans[i];
        pans[side.min(1)] = v.min(32) as u8;
        let (l, r) = (pan_of(pans[0] as u16), pan_of(pans[1] as u16));
        if let Some(st) = self.strip(s) {
            st.params.pan.store((l + r) / 2.0);
            st.params.width.store((r - l) / 2.0);
        }
    }

    /// The attenuator (MAN p.32) in dB: 0 to -96, STEREO +12 to -96.
    pub fn att(&self, s: Y2Strip) -> i32 {
        self.strip(s).map_or(0, |st| st.params.input.trim_db.load().round() as i32)
    }

    /// Sets the attenuator; a pair's partner keeps its own (V2 p.32).
    pub fn set_att(&mut self, s: Y2Strip, db: i32) {
        let top = if s == Y2Strip::Stereo { 12 } else { 0 };
        if let Some(st) = self.strip(s) {
            st.params.input.trim_db.store(db.clamp(-96, top) as f32);
        }
    }

    /// Phase reverse (not on STEREO; a pair's partner keeps its own).
    pub fn phase(&self, s: Y2Strip) -> bool {
        self.strip(s).is_some_and(|st| st.params.input.invert.load(Relaxed))
    }

    pub fn set_phase(&mut self, s: Y2Strip, reverse: bool) {
        if s == Y2Strip::Stereo {
            return;
        }
        if let Some(st) = self.strip(s) {
            st.params.input.invert.store(reverse, Relaxed);
        }
    }

    /// The delay (MAN p.34): on, and in samples (0-2600). The inputs and returns only.
    pub fn delay(&self, s: Y2Strip) -> (bool, u16) {
        self.strip(s).map_or((false, 0), |st| {
            let i = &st.params.input;
            (i.delay_on.load(Relaxed), i.delay.load().round().clamp(0.0, MAX_DELAY as f32) as u16)
        })
    }

    pub fn set_delay(&mut self, s: Y2Strip, on: bool, samples: u16) {
        if s.member().is_none() {
            return;
        }
        for st in self.linked(s) {
            let i = &st.params.input;
            i.delay_mode.store(SAMPLES, Relaxed);
            i.delay.store(samples.min(MAX_DELAY) as f32);
            i.delay_on.store(on, Relaxed);
        }
    }

    // ---- aux sends and routing ----------------------------------------------------

    /// Whether strip `s` sends to AUX `aux` (0-7): every input does, but EFF1
    /// only to AUX 8 and EFF2 only to AUX 7, which feed the other effect
    /// (MAN p.195).
    pub fn sends_to(s: Y2Strip, aux: usize) -> bool {
        match s {
            Y2Strip::Eff(e) => aux == 7 - e,
            s => s.member().is_some() && aux < 8,
        }
    }

    fn aux_send(&self, s: Y2Strip, aux: usize) -> Option<(Arc<Strip>, usize)> {
        Self::sends_to(s, aux).then_some(())?;
        Some((self.strip(s)?, y02r::AUX + aux))
    }

    /// An input's send to AUX `aux`: its level as a fader step (0-127), ON,
    /// and PRE (else POST) fader (MAN pp.84-88).
    pub fn aux_level(&self, s: Y2Strip, aux: usize) -> u16 {
        self.aux_send(s, aux)
            .and_then(|(st, slot)| st.sends.get_or_create(slot).map(|p| db_step(FaderLaw::Y02R, p.level_db.load())))
            .unwrap_or(0)
    }

    pub fn aux_on(&self, s: Y2Strip, aux: usize) -> bool {
        self.aux_send(s, aux)
            .and_then(|(st, slot)| st.sends.get_or_create(slot).map(|p| p.enabled.load(Relaxed)))
            .unwrap_or(false)
    }

    pub fn aux_pre(&self, s: Y2Strip, aux: usize) -> bool {
        self.aux_send(s, aux)
            .and_then(|(st, slot)| st.sends.get_or_create(slot).map(|p| p.mode.load(Relaxed) == send_mode::PRE))
            .unwrap_or(false)
    }

    /// Sets the send's level, ON or PRE: its pair partner's too, and an aux
    /// pair's other aux the same (V2 p.185).
    fn each_send(&self, s: Y2Strip, aux: usize, f: impl Fn(&livedaw_engine::SendParams)) {
        if !Self::sends_to(s, aux) {
            return;
        }
        let mut slots = vec![y02r::AUX + aux];
        slots.extend(self.state.bus_partner(y02r::AUX + aux));
        for st in self.linked(s) {
            for &slot in &slots {
                if let Some(p) = st.sends.get_or_create(slot) {
                    f(p);
                }
            }
        }
    }

    pub fn set_aux_level(&mut self, s: Y2Strip, aux: usize, step: u16) {
        let db = step_db(FaderLaw::Y02R, step);
        self.each_send(s, aux, |p| p.level_db.store(db));
    }

    pub fn set_aux_on(&mut self, s: Y2Strip, aux: usize, on: bool) {
        self.each_send(s, aux, |p| p.enabled.store(on, Relaxed));
    }

    pub fn set_aux_pre(&mut self, s: Y2Strip, aux: usize, pre: bool) {
        self.each_send(s, aux, |p| p.mode.store(if pre { send_mode::PRE } else { send_mode::POST }, Relaxed));
    }

    /// Whether an input is routed to BUS `bus` (0-7): a post-fader send at 0
    /// dB following its pan (MAN p.39); a send left at -oo (as a new console's
    /// are) isn't a route.
    pub fn bus_on(&self, s: Y2Strip, bus: usize) -> bool {
        // SURROUND has the first buses: the member's own routing waits.
        if let Some(m) = s.member().filter(|_| bus < self.surround_buses()) {
            return self.state.surround_routing[m] >> bus & 1 != 0;
        }
        s.member().is_some()
            && bus < 8
            && self
                .strip(s)
                .and_then(|st| st.sends.get_or_create(bus).map(|p| p.enabled.load(Relaxed) && p.level_db.load() > -100.0))
                .unwrap_or(false)
    }

    pub fn set_bus_on(&mut self, s: Y2Strip, bus: usize, on: bool) {
        if s.member().is_none() || bus >= 8 {
            return;
        }
        if bus < self.surround_buses() {
            for m in self.linked_members(s) {
                let r = &mut self.state.surround_routing[m];
                *r = if on { *r | 1 << bus } else { *r & !(1 << bus) };
            }
            return;
        }
        for st in self.linked(s) {
            if let Some(p) = st.sends.get_or_create(bus) {
                p.level_db.store(0.0);
                p.mode.store(send_mode::POST, Relaxed);
                p.pan_link.store(true, Relaxed);
                p.enabled.store(on, Relaxed);
            }
        }
    }

    /// Whether an input is routed to STEREO (ST).
    pub fn stereo_on(&self, s: Y2Strip) -> bool {
        self.strip(s).and_then(|st| st.main_send(0).map(|m| m.on.load(Relaxed))).unwrap_or(false)
    }

    pub fn set_stereo_on(&mut self, s: Y2Strip, on: bool) {
        if s.member().is_none() {
            return;
        }
        for st in self.linked(s) {
            if let Some(m) = st.main_send(0) {
                m.on.store(on, Relaxed);
            }
        }
    }

    /// MIC 1-16's DIRECT (MAN p.39).
    pub fn direct(&self, mic: usize) -> bool {
        self.state.direct.get(mic).copied().unwrap_or(false)
    }

    pub fn set_direct(&mut self, mic: usize, on: bool) {
        if let Some(d) = self.state.direct.get_mut(mic) {
            *d = on;
        }
    }

    // ---- EQ ----------------------------------------------------------------------------

    pub fn eq_on(&self, s: Y2Strip) -> bool {
        self.strip(s).is_some_and(|st| st.params.eq().on.load(Relaxed))
    }

    pub fn set_eq_on(&mut self, s: Y2Strip, on: bool) {
        for st in self.linked(s) {
            st.params.eq().on.store(on, Relaxed);
        }
    }

    /// A model parameter of a strip's block as its step.
    fn block_step(block: &livedaw_engine::params::DynSlotParams, id: &str) -> Option<u16> {
        let spec = block.model_spec()?.params.iter().find(|p| p.id == id)?;
        Some(param_step(spec, block.param(id)?.load()))
    }

    /// An EQ value as its step: `band` 0-3 (LOW, L-MID, H-MID, HIGH), `what` 0
    /// F, 1 G, 2 Q (Q past its last value: the shelf, then HPF or LPF).
    pub fn eq_value(&self, s: Y2Strip, band: usize, what: usize) -> u16 {
        let id = EQ_IDS[band.min(3)][what.min(2)];
        self.strip(s).and_then(|st| eq_step(&st, id)).unwrap_or(0)
    }

    pub fn set_eq_value(&mut self, s: Y2Strip, band: usize, what: usize, step: u16) {
        let id = EQ_IDS[band.min(3)][what.min(2)];
        for st in self.linked(s) {
            set_eq_step(&st, id, step);
        }
    }

    /// The HPF (LOW) or LPF (HIGH) switch: a filtering band's gain knob (MAN p.50).
    pub fn eq_filter(&self, s: Y2Strip, high: bool) -> bool {
        self.strip(s).and_then(|st| eq_step(&st, if high { "lpf" } else { "hpf" })).unwrap_or(0) != 0
    }

    pub fn set_eq_filter(&mut self, s: Y2Strip, high: bool, on: bool) {
        for st in self.linked(s) {
            set_eq_step(&st, if high { "lpf" } else { "hpf" }, on as u16);
        }
    }

    // ---- dynamics -------------------------------------------------------------------

    pub fn dyn_on(&self, s: Y2Strip) -> bool {
        s.dyn_index().is_some() && self.strip(s).is_some_and(|st| st.params.dynamics().on.load(Relaxed))
    }

    pub fn set_dyn_on(&mut self, s: Y2Strip, on: bool) {
        if s.dyn_index().is_none() {
            return;
        }
        for st in self.dyn_linked(s) {
            st.params.dynamics().on.store(on, Relaxed);
        }
        self.apply_dyn(s);
    }

    /// A strip and, while its pair's processors are linked (ST.LINK), its
    /// partner: the processors move together.
    fn dyn_linked(&self, s: Y2Strip) -> Vec<Arc<Strip>> {
        match s.dyn_index() {
            Some(i) if self.state.dynamics[i].st_link => self.linked(s),
            _ => self.strip(s).into_iter().collect(),
        }
    }

    /// The processor's type (`livedaw_dsp::y02r::DYN_MODELS`: 0 CMP ... 5 DUK).
    pub fn dyn_type(&self, s: Y2Strip) -> usize {
        self.strip(s)
            .and_then(|st| {
                let d = st.params.dynamics();
                let token = d.model_spec()?.token;
                DYN_MODELS.iter().position(|m| m.token == token)
            })
            .unwrap_or(0)
    }

    /// Makes the processor of type `t` on its type's defaults (a type is
    /// chosen by recalling a program of it: MAN-GS p.56).
    pub fn set_dyn_type(&mut self, s: Y2Strip, t: usize) {
        if s.dyn_index().is_none() {
            return;
        }
        let Some(token) = DYN_MODELS.get(t).map(|m| m.token) else { return };
        for st in self.dyn_linked(s) {
            let d = st.params.dynamics();
            if let Some(model) = d.table().iter().position(|m| m.token == token) {
                d.set_model(model);
            }
        }
        self.apply_dyn(s);
    }

    /// Parameter `k` of the processor (its type's order) as its step.
    pub fn dyn_param(&self, s: Y2Strip, k: usize) -> u16 {
        let Some(st) = self.strip(s) else { return 0 };
        let d = st.params.dynamics();
        let Some(id) = d.model_spec().and_then(|m| m.params.get(k)).map(|p| p.id) else { return 0 };
        Self::block_step(d, id).unwrap_or(0)
    }

    pub fn set_dyn_param(&mut self, s: Y2Strip, k: usize, step: u16) {
        for st in self.dyn_linked(s) {
            let d = st.params.dynamics();
            if let (Some(spec), Some(v)) = (d.model_spec().and_then(|m| m.params.get(k)), d.values.get(k)) {
                v.store(step_param(spec, step));
            }
        }
    }

    /// KEY IN, ST.LINK and IN (MAN p.70).
    pub fn dyn_settings(&self, s: Y2Strip) -> DynSettings {
        s.dyn_index().map_or_else(DynSettings::default, |i| self.state.dynamics[i])
    }

    /// Sets them; LEFT can't key MIC 1, TAPE 1, BUS 1 or STEREO (MAN p.70), and
    /// BUS / STEREO keys only an output's processor (MAN p.72).
    pub fn set_dyn_settings(&mut self, s: Y2Strip, mut d: DynSettings) {
        let Some(i) = s.dyn_index() else { return };
        let first = matches!(s, Y2Strip::Mic(0) | Y2Strip::Tape(0) | Y2Strip::Bus(0) | Y2Strip::Stereo);
        let output = matches!(s, Y2Strip::Bus(_) | Y2Strip::Stereo);
        if first && matches!(d.key_in, KeyIn::LeftPostEq | KeyIn::LeftPreEq)
            || !output && matches!(d.key_in, KeyIn::Bus(_) | KeyIn::Stereo)
        {
            d.key_in = KeyIn::SelfPostEq;
        }
        // A pair's processors stay linked while it is made (MAN p.70).
        if self.partner(s).is_some() && matches!(s, Y2Strip::Mic(_) | Y2Strip::Tape(_)) {
            d.st_link = true;
        }
        d.in_mode = d.in_mode.min(2);
        self.state.dynamics[i] = d;
        if let Some(p) = self.partner(s).and_then(Y2Strip::dyn_index) {
            self.state.dynamics[p].st_link = d.st_link;
        }
        self.apply_dyn(s);
    }

    /// Writes a strip's KEY IN onto its dynamics block (and a linked
    /// partner's, which IN keys from one side or both).
    fn apply_dyn(&self, s: Y2Strip) {
        let Some(i) = s.dyn_index() else { return };
        let d = self.state.dynamics[i];
        let mut strips = vec![(s, self.strip(s))];
        if d.st_link {
            if let Some(p) = self.partner(s) {
                strips.push((p, self.strip(p)));
            }
        }
        for (side, (strip, st)) in strips.iter().enumerate() {
            let Some(st) = st else { continue };
            let block = st.params.dynamics();
            // A linked pair keyed by one side: both by that side's signal.
            let keyed = if d.st_link && d.in_mode != 0 && side + 1 != d.in_mode as usize {
                strips[d.in_mode as usize - 1].0
            } else {
                *strip
            };
            let (source, tap) = self.key_source(*strip, keyed, d.key_in);
            block.sc_source.store(source, Relaxed);
            block.sc_tap.store(tap, Relaxed);
        }
    }

    /// A key as the engine's `sc_source` and `sc_tap` for `strip`'s block,
    /// triggered by `keyed` (itself, or its linked partner): the channel
    /// before (tap 1) or after (tap 2) its EQ, its odd neighbour, AUX 1-2,
    /// BUS 1-8 or STEREO (their PFL).
    fn key_source(&self, strip: Y2Strip, keyed: Y2Strip, key: KeyIn) -> (i32, i32) {
        const PRE_EQ: i32 = 1;
        const POST_EQ: i32 = 2;
        const OUTPUT_IN: i32 = 0;
        const OUTPUT_PFL: i32 = 2;
        let of = |s: Y2Strip, pre: bool| match s.engine_ref() {
            (StripKind::Channel, c) => (KeySource::Channel(c).code(), if pre { PRE_EQ } else { POST_EQ }),
            (kind, i) => (KeySource::Strip(kind, i).code(), if pre { OUTPUT_IN } else { OUTPUT_PFL }),
        };
        let left = |s: Y2Strip| match s {
            Y2Strip::Bus(b) if b > 0 => Some(Y2Strip::Bus(b - 1)),
            s => match s.engine_ref() {
                (StripKind::Channel, c) if c > 0 && c != y02r::TAPE => Some(Y2Strip::channel(c - 1)),
                _ => None,
            },
        };
        match key {
            KeyIn::SelfPostEq if keyed == strip => (KeySource::SelfKey.code(), POST_EQ),
            KeyIn::SelfPostEq => of(keyed, false),
            KeyIn::SelfPreEq => of(keyed, true),
            KeyIn::LeftPostEq | KeyIn::LeftPreEq => {
                left(keyed).map_or((KeySource::SelfKey.code(), POST_EQ), |l| of(l, key == KeyIn::LeftPreEq))
            }
            KeyIn::Aux1 => of(Y2Strip::Aux(0), false),
            KeyIn::Aux2 => of(Y2Strip::Aux(1), false),
            KeyIn::Bus(b) => of(Y2Strip::Bus(b.min(7)), false),
            KeyIn::Stereo => of(Y2Strip::Stereo, false),
        }
    }

    // ---- pairs, groups and gangs --------------------------------------------------

    /// Makes or breaks channel pair `pair` (`State::pairs`: MIC 1/2 ... then
    /// TAPE 1/2 ...). Making it copies one side onto the other (`from_odd`:
    /// the odd channel's, else the even's; V2 p.184): the fader, the aux
    /// sends, the routing, the delay, the EQ, the dynamics, the groups, the
    /// fade time and recall safe; not the attenuator, phase or pan. The
    /// processors are linked while the pair is made.
    pub fn set_pair(&mut self, pair: usize, on: bool, from_odd: bool) {
        if pair >= state::PAIRS {
            return;
        }
        let odd = if pair < MICS / 2 { 2 * pair } else { y02r::TAPE + 2 * (pair - MICS / 2) };
        let (from, to) = if from_odd { (odd, odd + 1) } else { (odd + 1, odd) };
        if on && !self.state.pairs[pair] {
            if let (Some(a), Some(b)) = (self.strip(Y2Strip::channel(from)), self.strip(Y2Strip::channel(to))) {
                copy_channel(&a, &b);
            }
            let s = &mut *self.state;
            s.fader_groups[to] = s.fader_groups[from];
            s.mute_groups[to] = s.mute_groups[from];
            s.fade_times[to] = s.fade_times[from];
            s.recall_safe[to] = s.recall_safe[from];
            s.dynamics[to] = s.dynamics[from];
            s.dynamics[from].st_link = true;
            s.dynamics[to].st_link = true;
        }
        self.state.pairs[pair] = on;
        if !on {
            // Breaking the pair turns MS off.
            self.set_ms(pair, false);
        }
        self.apply_dyn(Y2Strip::channel(odd));
    }

    // ---- surround (V2 MAN pp.64-73) ------------------------------------------------------

    /// SURROUND's mode, if it is on.
    pub fn surround(&self) -> Option<surround::Mode> {
        surround::Mode::ALL.get((self.state.surround as usize).checked_sub(1)?).copied()
    }

    /// The buses SURROUND takes (0 while it is off).
    pub fn surround_buses(&self) -> usize {
        self.surround().map_or(0, surround::Mode::buses)
    }

    /// The members `s` and its pair partner are.
    fn linked_members(&self, s: Y2Strip) -> Vec<usize> {
        let mut out: Vec<usize> = s.member().into_iter().collect();
        out.extend(self.partner(s).and_then(Y2Strip::member));
        out
    }

    /// Turns SURROUND on in `mode` (0 off, 1 2+2, 2 3+1, 3 3+2+1): the members'
    /// routing to the buses it takes kept aside, given back when it goes off
    /// (or a smaller mode leaves BUS 5-6).
    pub fn set_surround(&mut self, mode: u8) {
        let mode = mode.min(3);
        let was = self.surround_buses();
        // Give the buses back first...
        for m in 0..MEMBERS {
            let Some(s) = Y2Strip::of_member(m) else { continue };
            let saved = self.state.surround_routing[m];
            for bus in 0..was {
                self.write_bus(s, bus, saved >> bus & 1 != 0);
            }
        }
        self.state.surround = mode;
        // ...then take the new mode's, keeping the members' own routing to them.
        let now = self.surround_buses();
        // The buses sum to mono while SURROUND has them, so each side of a
        // stereo channel reaches its speakers (a bus's output is its left side).
        for bus in 0..now.max(was) {
            if let Some(b) = self.strip(Y2Strip::Bus(bus)) {
                b.params.mono.store(bus < now, Relaxed);
            }
        }
        for m in 0..MEMBERS {
            let Some(s) = Y2Strip::of_member(m) else { continue };
            let mut bits = 0u8;
            for bus in 0..now {
                bits |= (self.bus_send_on(s, bus) as u8) << bus;
            }
            self.state.surround_routing[m] = bits;
            self.apply_surround(m);
        }
    }

    /// A member's surround pan.
    pub fn surround_pan(&self, member: usize) -> Option<SurroundPan> {
        self.state.surround_pans.get(member).copied()
    }

    /// Sets a member's surround pan and moves its sends to the surround buses;
    /// a place along a trajectory puts it there.
    pub fn set_surround_pan(&mut self, member: usize, pan: SurroundPan) {
        let Some(p) = self.state.surround_pans.get_mut(member) else { return };
        let moved = pan.along != p.along || pan.trajectory != p.trajectory;
        let mut pan = pan;
        if moved {
            let t = surround::Trajectory::ALL[pan.trajectory.min(6) as usize];
            let (x, y) = t.at(pan.along as f32 / 64.0, pan.width as f32 / 32.0, pan.depth as f32 / 32.0, place(pan.offset_x), place(pan.offset_y));
            (pan.x, pan.y) = (step(x), step(y));
        }
        *p = pan;
        self.apply_surround(member);
        // ST LINK (V2 MAN p.72): a pair's other channel moves by the pattern
        // (its place only; its CSR and subwoofer stay its own).
        let other = Y2Strip::of_member(member).and_then(|s| self.partner(s)).and_then(Y2Strip::member);
        if let Some(other) = other {
            let pattern = pattern_of(pan);
            let pattern = if member < other { pattern } else { pattern.inverse() };
            let q = &mut self.state.surround_pans[other];
            (q.link, q.pattern) = (pan.link, pan.pattern);
            if pan.link {
                let (x, y) = pattern.place(place(pan.x), place(pan.y));
                (q.x, q.y) = (step(x), step(y));
            }
            self.apply_surround(other);
        }
    }

    /// A member's sends to the surround buses from its pan (nothing while
    /// SURROUND is off): post fader, each its speaker's gain. A stereo
    /// channel's right side sits by ST LINK's pattern (else with the left).
    fn apply_surround(&self, member: usize) {
        let (Some(mode), Some(s)) = (self.surround(), Y2Strip::of_member(member)) else { return };
        let Some(st) = self.strip(s) else { return };
        let p = self.state.surround_pans[member];
        let sub = if p.sub == 0 { 0.0 } else { livedaw_dsp::util::db_to_gain(p.sub as f32 - 60.0) };
        let at = |(x, y)| surround::gains(mode, surround::Position { x, y, csr: p.csr as f32 / 10.0, sub });
        let left = at((place(p.x), place(p.y)));
        let stereo = st.params.stereo.load(Relaxed);
        let right = if stereo && p.link { at(pattern_of(p).place(place(p.x), place(p.y))) } else { left };
        // The buses sum to mono (`set_surround`): a centred mono send is the
        // pan law's centre on both sides, as much more; a stereo one's sides
        // are balanced to their gains, twice them for the mono's half.
        let centre = (self.engine.profile().spec().pan.mono)(0.0).0.max(1e-6);
        for bus in 0..mode.buses() {
            let (level, balance) = if stereo {
                let (a, b) = (left[bus], right[bus]);
                let balance = if a <= 0.0 && b <= 0.0 {
                    0.0
                } else if a >= b {
                    b / a - 1.0
                } else {
                    1.0 - a / b
                };
                (2.0 * a.max(b), balance)
            } else {
                (left[bus] / centre, 0.0)
            };
            if let Some(send) = st.sends.get_or_create(bus) {
                send.mode.store(send_mode::POST, Relaxed);
                send.pan_link.store(false, Relaxed);
                send.pan.store(balance * 100.0);
                send.level_db.store(if level > 0.0 { livedaw_dsp::util::gain_to_db(level) } else { -144.0 });
                send.enabled.store(p.on && level > 0.0, Relaxed);
            }
        }
    }

    /// A send to bus `bus` routed (on, at its level) as ROUTING has it.
    fn bus_send_on(&self, s: Y2Strip, bus: usize) -> bool {
        self.strip(s)
            .and_then(|st| st.sends.get_or_create(bus).map(|p| p.enabled.load(Relaxed) && p.level_db.load() > -100.0))
            .unwrap_or(false)
    }

    /// Routes a member to bus `bus` as ROUTING does (0 dB, post, the channel's pan).
    fn write_bus(&self, s: Y2Strip, bus: usize, on: bool) {
        let Some(st) = self.strip(s) else { return };
        if let Some(p) = st.sends.get_or_create(bus) {
            p.level_db.store(0.0);
            p.mode.store(send_mode::POST, Relaxed);
            p.pan_link.store(true, Relaxed);
            p.enabled.store(on, Relaxed);
        }
    }

    /// The pair `s` decodes as MS, if it does.
    pub fn ms_pair(&self, s: Y2Strip) -> Option<usize> {
        let (StripKind::Channel, c) = s.engine_ref() else { return None };
        state::pair_of(c).filter(|&p| self.state.pairs[p] && self.state.ms[p])
    }

    /// MS decoding of pair `pair` (V2 MAN p.186): on, the channels pair (if
    /// they weren't), the odd one is M and the even one S: L = M + S, R = M -
    /// S, both pans to the centre to start from; the faders move apart.
    pub fn set_ms(&mut self, pair: usize, on: bool) {
        if pair >= state::PAIRS {
            return;
        }
        if on && !self.state.pairs[pair] {
            self.set_pair(pair, true, true);
        }
        let was = self.state.ms[pair];
        self.state.ms[pair] = on;
        let odd = if pair < MICS / 2 { 2 * pair } else { y02r::TAPE + 2 * (pair - MICS / 2) };
        for (c, side) in [(odd, false), (odd + 1, true)] {
            if let Some(st) = self.strip(Y2Strip::channel(c)) {
                st.params.ms_side.store(on && side, Relaxed);
                if on && !was {
                    st.params.pan.store(0.0);
                }
            }
        }
    }

    /// Whether pair `pair` decodes as MS.
    pub fn ms(&self, pair: usize) -> bool {
        self.state.ms.get(pair).copied().unwrap_or(false) && self.state.pairs.get(pair).copied().unwrap_or(false)
    }

    /// Makes or breaks an aux pair (AUX 1/2 ... 7/8, `pair` 0-3) or a bus pair
    /// (BUS 1/2 ...): the even master and the inputs' sends to it take the odd
    /// one's (V2 p.185).
    pub fn set_output_pair(&mut self, aux: bool, pair: usize, on: bool) {
        let flags = if aux { &mut self.state.aux_pairs } else { &mut self.state.bus_pairs };
        let Some(flag) = flags.get_mut(pair) else { return };
        let was = *flag;
        *flag = on;
        if !on || was {
            return;
        }
        let base = if aux { y02r::AUX } else { 0 };
        let (odd, even) = (base + 2 * pair, base + 2 * pair + 1);
        let (Some(a), Some(b)) = (self.engine.strip_at(StripKind::Bus, odd), self.engine.strip_at(StripKind::Bus, even)) else {
            return;
        };
        b.params.fader_db.store(a.params.fader_db.load());
        b.params.mute.store(a.params.mute.load(Relaxed), Relaxed);
        if !aux {
            let (da, db) = (a.params.dynamics(), b.params.dynamics());
            db.set_model(da.model_index());
            for (x, y) in da.values.iter().zip(&db.values) {
                y.store(x.load());
            }
            db.on.store(da.on.load(Relaxed), Relaxed);
        } else {
            for m in 0..MEMBERS {
                let Some(st) = Y2Strip::of_member(m).and_then(|s| self.strip(s)) else { continue };
                if let (Some(x), Some(y)) = (st.sends.get_or_create(odd), st.sends.get_or_create(even)) {
                    y.level_db.store(x.level_db.load());
                    y.enabled.store(x.enabled.load(Relaxed), Relaxed);
                    y.mode.store(x.mode.load(Relaxed), Relaxed);
                }
            }
        }
    }

    /// A member's fader group (`mute`: mute group), 0 none or 1-4; a member is
    /// in one of each at most, and a pair joins and leaves together (MAN p.148).
    pub fn set_group(&mut self, member: usize, mute: bool, group: u8) {
        let partner = Y2Strip::of_member(member).and_then(|s| self.partner(s)).and_then(Y2Strip::member);
        let groups = if mute { &mut self.state.mute_groups } else { &mut self.state.fader_groups };
        for m in std::iter::once(member).chain(partner) {
            if let Some(g) = groups.get_mut(m) {
                *g = group.min(4);
            }
        }
    }

    /// Gangs or ungangs a pan pair (`State::gangs`, MAN p.37).
    pub fn set_gang(&mut self, pair: usize, on: bool) {
        if let Some(g) = self.state.gangs.get_mut(pair) {
            *g = on;
        }
    }
}

impl Console<'_> {
    // ---- libraries (`crate::library`) -------------------------------------------

    /// Recalls EQ program `n` (0-based) onto a strip (and its pair partner).
    pub fn recall_eq(&mut self, lib: &Library, s: Y2Strip, n: usize) -> bool {
        let Some(prog) = lib.get(Kind::Eq, n) else { return false };
        for st in self.linked(s) {
            let eq = st.params.eq();
            if eq.model_spec().map(|m| m.token) != Some(prog.token) {
                continue;
            }
            for (x, v) in eq.values.iter().zip(&prog.values) {
                x.store(*v);
            }
        }
        true
    }

    /// Recalls dynamics program `n` onto a strip's processor (its type and
    /// values; a linked pair's both); KEY IN, ST.LINK and IN stay (MAN p.70).
    pub fn recall_dyn(&mut self, lib: &Library, s: Y2Strip, n: usize) -> bool {
        let Some(prog) = lib.get(Kind::Dynamics, n) else { return false };
        if s.dyn_index().is_none() {
            return false;
        }
        for st in self.dyn_linked(s) {
            let d = st.params.dynamics();
            let Some(model) = d.table().iter().position(|m| m.token == prog.token) else { continue };
            if d.model_index() != model || d.model.load(Relaxed) < 0 {
                d.set_model(model);
            }
            for (x, v) in d.values.iter().zip(&prog.values) {
                x.store(*v);
            }
        }
        self.apply_dyn(s);
        true
    }

    /// Recalls effects program `n` into Effect `slot` (0-1).
    pub fn recall_fx(&mut self, lib: &Library, slot: usize, n: usize) -> bool {
        let Some(prog) = lib.get(Kind::Effects, n) else { return false };
        let Some(fx) = self.engine.controls.fx.get(slot).filter(|_| slot < 2) else { return false };
        let models = self.engine.fx_models();
        let Some(model) = livedaw_dsp::model::model_index(models, prog.token) else { return false };
        if fx.model_index_in(models) != model {
            self.engine.set_fx_model(slot, model, true);
        }
        for (x, v) in fx.values.iter().zip(&prog.values) {
            x.store(*v);
        }
        self.state.fx_programs[slot] = n.min(127) as u8;
        true
    }

    /// Stores a strip's EQ as program `n` named `name`.
    pub fn store_eq(&self, lib: &mut Library, s: Y2Strip, n: usize, name: &str) -> bool {
        let Some(st) = self.strip(s) else { return false };
        let eq = st.params.eq();
        let Some(token) = eq.model_spec().map(|m| m.token) else { return false };
        let values = eq.values.iter().take(eq.model_spec().map_or(0, |m| m.params.len())).map(|v| v.load()).collect();
        lib.store(Kind::Eq, n, Program { name: name.into(), token, values })
    }

    /// Stores a strip's dynamics processor as program `n`.
    pub fn store_dyn(&self, lib: &mut Library, s: Y2Strip, n: usize, name: &str) -> bool {
        let Some(st) = self.strip(s).filter(|_| s.dyn_index().is_some()) else { return false };
        let d = st.params.dynamics();
        let Some(spec) = d.model_spec() else { return false };
        let values = d.values.iter().take(spec.params.len()).map(|v| v.load()).collect();
        lib.store(Kind::Dynamics, n, Program { name: name.into(), token: spec.token, values })
    }

    /// Recalls channel program `n` (0-63; 64 the UNDO) onto input strip `s`
    /// (V2 MAN p.49); what the strip had goes to the UNDO.
    pub fn recall_channel(&mut self, lib: &mut Library, s: Y2Strip, n: usize) -> bool {
        let Some(before) = params::channel_bytes(self, s) else { return false };
        let prog = if n == CHANNEL_PROGRAMS { lib.channel_undo.clone() } else { lib.channel.get(n).cloned().flatten() };
        let Some(prog) = prog else { return false };
        params::set_channel_bytes(self, s, &prog.bytes);
        lib.channel_undo = Some(ChannelProgram { name: prog.name, bytes: before });
        true
    }

    /// Stores input strip `s` as channel program `n` (0-63), titled `name`;
    /// what `n` held goes to the UNDO.
    pub fn store_channel(&self, lib: &mut Library, s: Y2Strip, n: usize, name: &str) -> bool {
        let (Some(bytes), true) = (params::channel_bytes(self, s), n < CHANNEL_PROGRAMS) else { return false };
        let old = lib.channel[n].replace(ChannelProgram { name: name.chars().take(16).collect(), bytes });
        if old.is_some() {
            lib.channel_undo = old;
        }
        true
    }

    /// Stores Effect `slot` as program `n`.
    pub fn store_fx(&self, lib: &mut Library, slot: usize, n: usize, name: &str) -> bool {
        let Some(fx) = self.engine.controls.fx.get(slot).filter(|_| slot < 2) else { return false };
        let models = self.engine.fx_models();
        let Some(spec) = models.get(fx.model_index_in(models)).filter(|m| m.token.starts_with("Y2")) else { return false };
        let values = fx.values.iter().take(spec.params.len()).map(|v| v.load()).collect();
        lib.store(Kind::Effects, n, Program { name: name.into(), token: spec.token, values })
    }
}

/// An EQ parameter of a strip as its step.
fn eq_step(st: &Strip, id: &str) -> Option<u16> {
    let eq = st.params.eq();
    let spec = eq.model_spec()?.params.iter().find(|p| p.id == id)?;
    Some(param_step(spec, eq.param(id)?.load()))
}

fn set_eq_step(st: &Strip, id: &str, step: u16) {
    let eq = st.params.eq();
    if let (Some(spec), Some(v)) = (eq.model_spec().and_then(|m| m.params.iter().find(|p| p.id == id)), eq.param(id)) {
        v.store(step_param(spec, step));
    }
}

/// What making a channel pair copies (V2 p.184): the fader and ON, the aux
/// sends and routing, the delay, the EQ, the dynamics.
fn copy_channel(from: &Strip, to: &Strip) {
    let (f, t) = (&from.params, &to.params);
    t.fader_db.store(f.fader_db.load());
    t.mute.store(f.mute.load(Relaxed), Relaxed);
    t.solo.store(f.solo.load(Relaxed), Relaxed);
    t.input.delay_on.store(f.input.delay_on.load(Relaxed), Relaxed);
    t.input.delay_mode.store(f.input.delay_mode.load(Relaxed), Relaxed);
    t.input.delay.store(f.input.delay.load());
    let (fe, te) = (f.eq(), t.eq());
    te.on.store(fe.on.load(Relaxed), Relaxed);
    for (a, b) in fe.values.iter().zip(&te.values) {
        b.store(a.load());
    }
    let (fd, td) = (f.dynamics(), t.dynamics());
    td.set_model(fd.model_index());
    for (a, b) in fd.values.iter().zip(&td.values) {
        b.store(a.load());
    }
    td.on.store(fd.on.load(Relaxed), Relaxed);
    for slot in 0..16 {
        if let (Some(a), Some(b)) = (from.sends.get_or_create(slot), to.sends.get_or_create(slot)) {
            b.level_db.store(a.level_db.load());
            b.enabled.store(a.enabled.load(Relaxed), Relaxed);
            b.mode.store(a.mode.load(Relaxed), Relaxed);
            b.pan_link.store(a.pan_link.load(Relaxed), Relaxed);
        }
    }
    if let (Some(a), Some(b)) = (from.main_send(0), to.main_send(0)) {
        b.on.store(a.on.load(Relaxed), Relaxed);
    }
}

/// A surround pan step (0-64, 32 the centre) on the pan graph (-1 .. 1), and back.
fn place(v: u8) -> f32 {
    (v as f32 - 32.0) / 32.0
}

fn step(v: f32) -> u8 {
    (v * 32.0 + 32.0).round().clamp(0.0, 64.0) as u8
}

/// A pan's ST LINK pattern.
fn pattern_of(p: SurroundPan) -> surround::Pattern {
    surround::Pattern::ALL[p.pattern.min(7) as usize]
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_engine::config::{ConsoleConfig, Counts};
    use livedaw_engine::Profile;

    fn y02r() -> Engine {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::Yamaha02R, counts: Counts::Y02R });
        e
    }

    #[test]
    fn strips_and_steps() {
        assert_eq!(Y2Strip::Tape(0).engine_ref(), (StripKind::Channel, 20));
        assert_eq!(Y2Strip::Aux(7).engine_ref(), (StripKind::Bus, 15));
        assert_eq!(Y2Strip::channel(17), Y2Strip::Line(1));
        assert_eq!((Y2Strip::Eff(1).member(), Y2Strip::Bus(0).member()), (Some(37), None));
        assert_eq!(
            (Y2Strip::Bus(7).dyn_index(), Y2Strip::Stereo.dyn_index(), Y2Strip::Eff(0).dyn_index()),
            (Some(43), Some(44), None)
        );
        assert_eq!(Y2Strip::Stereo.fader_index(), Some(38));
        for s in [Y2Strip::Mic(0), Y2Strip::Stereo] {
            for k in [0, 1, 50, 103, 126, 127] {
                assert_eq!(db_step(s.law(), step_db(s.law(), k)), k, "{s:?} {k}");
            }
        }
        assert!(step_db(FaderLaw::Y02R, 101).abs() < 1e-4 && step_db(FaderLaw::Y02R_STEREO, 127).abs() < 1e-4);
    }

    #[test]
    fn faders_follow_pairs_and_groups() {
        let e = y02r();
        let mut s = State::default();
        let mut c = Console::new(&e, &mut s);
        c.set_pair(0, true, true);
        c.set_level(Y2Strip::Mic(0), 103, Origin::Panel);
        assert_eq!((c.level(Y2Strip::Mic(0)), c.level(Y2Strip::Mic(1))), (103, 103), "the pair");
        // Group A: MIC 3 and EFF1, offsets kept.
        c.set_group(2, false, 1);
        c.set_group(36, false, 1);
        c.set_level(Y2Strip::Eff(0), 80, Origin::Panel);
        c.set_level(Y2Strip::Mic(2), 100, Origin::Panel);
        assert_eq!(c.level(Y2Strip::Eff(0)), 100, "moved by as many steps");
        c.set_level(Y2Strip::Mic(2), 90, Origin::Remote);
        assert_eq!(c.level(Y2Strip::Eff(0)), 100, "a remote moves one fader");
        c.state.groups_on[0] = false;
        c.set_level(Y2Strip::Mic(2), 50, Origin::Panel);
        assert_eq!(c.level(Y2Strip::Eff(0)), 100, "group A off");
        // STEREO on its own law: step 127 is 0 dB.
        c.set_level(Y2Strip::Stereo, 127, Origin::Panel);
        assert_eq!(e.strip_at(StripKind::Main, 0).unwrap().params.fader_db.load(), 0.0);
    }

    #[test]
    fn a_mute_group_flips_its_members() {
        let e = y02r();
        let mut s = State::default();
        let mut c = Console::new(&e, &mut s);
        for m in [4, 5, 20] {
            c.set_group(m, true, 2);
        }
        c.set_on(Y2Strip::Mic(5), false, Origin::Remote);
        assert!(!c.on(Y2Strip::Mic(5)));
        c.set_on(Y2Strip::Mic(4), false, Origin::Panel);
        assert!(!c.on(Y2Strip::Mic(4)) && c.on(Y2Strip::Mic(5)) && !c.on(Y2Strip::Tape(0)), "each flips from its own state");
    }

    #[test]
    fn pans_gangs_and_stereo_strips() {
        let e = y02r();
        let mut s = State::default();
        let mut c = Console::new(&e, &mut s);
        c.set_gang(2, true); // MIC 5/6
        c.set_pan(Y2Strip::Mic(4), 11, Origin::Panel); // L5
        c.set_pan(Y2Strip::Mic(5), 26, Origin::Remote); // R10
        c.set_pan(Y2Strip::Mic(4), 9, Origin::Panel); // L7
        assert_eq!(c.pan(Y2Strip::Mic(5)), 24, "R8: MAN p.38's example");
        // LINE 19/20's two pans: the engine's pan and width follow.
        c.set_stereo_pan(Y2Strip::Line(1), 0, 8);
        c.set_stereo_pan(Y2Strip::Line(1), 1, 24);
        let st = e.strip_at(StripKind::Channel, 17).unwrap();
        assert_eq!((st.params.pan.load(), st.params.width.load()), (0.0, 50.0));
        c.set_pan(Y2Strip::Line(1), 20, Origin::Panel);
        assert_eq!((c.stereo_pan(Y2Strip::Line(1), 0), c.stereo_pan(Y2Strip::Line(1), 1)), (12, 28), "both sides move");
    }

    #[test]
    fn attenuator_phase_delay_and_routing() {
        let e = y02r();
        let mut s = State::default();
        let mut c = Console::new(&e, &mut s);
        c.set_att(Y2Strip::Mic(0), -120);
        c.set_att(Y2Strip::Stereo, 20);
        assert_eq!((c.att(Y2Strip::Mic(0)), c.att(Y2Strip::Stereo)), (-96, 12));
        c.set_phase(Y2Strip::Stereo, true);
        assert!(!c.phase(Y2Strip::Stereo), "STEREO has no phase");
        c.set_delay(Y2Strip::Tape(3), true, 3000);
        assert_eq!(c.delay(Y2Strip::Tape(3)), (true, 2600));
        // Routing: BUS 3 post-fader at 0 dB; ST.
        assert!(c.stereo_on(Y2Strip::Mic(0)), "everything goes to ST at first");
        c.set_bus_on(Y2Strip::Mic(0), 2, true);
        assert!(c.bus_on(Y2Strip::Mic(0), 2));
        let send = e.channel_at(0).unwrap();
        let p = send.sends.get_or_create(2).unwrap();
        assert_eq!((p.level_db.load(), p.mode.load(Relaxed)), (0.0, send_mode::POST));
        // Aux sends: EFF1 only to AUX 8.
        assert!(!Console::sends_to(Y2Strip::Eff(0), 6) && Console::sends_to(Y2Strip::Eff(0), 7));
        c.set_aux_level(Y2Strip::Eff(0), 6, 103);
        assert_eq!(c.aux_level(Y2Strip::Eff(0), 6), 0);
        c.set_aux_level(Y2Strip::Mic(2), 0, 103);
        c.set_aux_pre(Y2Strip::Mic(2), 0, true);
        assert_eq!((c.aux_level(Y2Strip::Mic(2), 0), c.aux_pre(Y2Strip::Mic(2), 0)), (103, true));
        // An aux pair: the even aux takes the odd one's sends, then both move.
        c.set_output_pair(true, 0, true);
        assert_eq!(c.aux_level(Y2Strip::Mic(2), 1), 103);
        c.set_aux_level(Y2Strip::Mic(2), 0, 90);
        assert_eq!(c.aux_level(Y2Strip::Mic(2), 1), 90);
    }

    #[test]
    fn eq_and_dynamics() {
        let e = y02r();
        let mut s = State::default();
        let mut c = Console::new(&e, &mut s);
        assert_eq!(c.eq_value(Y2Strip::Mic(0), 0, 2), 41, "LOW starts as L.SHELF");
        c.set_eq_value(Y2Strip::Mic(0), 3, 1, 60); // HIGH G step 60: +12 dB
        let eq = e.channel_at(0).unwrap();
        assert_eq!(eq.params.eq().param("hg").unwrap().load(), 12.0);
        c.set_eq_filter(Y2Strip::Mic(0), false, false);
        assert!(!c.eq_filter(Y2Strip::Mic(0), false) && c.eq_filter(Y2Strip::Mic(0), true));
        // Dynamics: a gate on TAPE 2, keyed by its left neighbour before its EQ.
        c.set_dyn_type(Y2Strip::Tape(1), 4);
        c.set_dyn_on(Y2Strip::Tape(1), true);
        assert_eq!(c.dyn_type(Y2Strip::Tape(1)), 4);
        c.set_dyn_param(Y2Strip::Tape(1), 0, 10); // THRESHOLD step 10: -44 dB
        let st = e.channel_at(21).unwrap();
        assert_eq!(st.params.dynamics().values[0].load(), -44.0);
        c.set_dyn_settings(Y2Strip::Tape(1), DynSettings { key_in: KeyIn::LeftPreEq, ..Default::default() });
        let d = st.params.dynamics();
        assert_eq!((d.sc_source.load(Relaxed), d.sc_tap.load(Relaxed)), (KeySource::Channel(20).code(), 1));
        // TAPE 1 has no left neighbour; a channel can't key from BUS 2.
        c.set_dyn_settings(Y2Strip::Tape(0), DynSettings { key_in: KeyIn::LeftPostEq, ..Default::default() });
        assert_eq!(c.dyn_settings(Y2Strip::Tape(0)).key_in, KeyIn::SelfPostEq);
        c.set_dyn_settings(Y2Strip::Mic(3), DynSettings { key_in: KeyIn::Bus(1), ..Default::default() });
        assert_eq!(c.dyn_settings(Y2Strip::Mic(3)).key_in, KeyIn::SelfPostEq);
        // STEREO keyed by BUS 2.
        c.set_dyn_settings(Y2Strip::Stereo, DynSettings { key_in: KeyIn::Bus(1), ..Default::default() });
        let d = e.strip_at(StripKind::Main, 0).unwrap();
        assert_eq!(d.params.dynamics().sc_source.load(Relaxed), KeySource::Strip(StripKind::Bus, 1).code());
        // AUX and the effect returns have none.
        c.set_dyn_on(Y2Strip::Aux(0), true);
        assert!(!c.dyn_on(Y2Strip::Aux(0)));
    }

    #[test]
    fn libraries_recall_and_store() {
        let e = y02r();
        let mut s = State::default();
        let mut lib = Library::default();
        let mut c = Console::new(&e, &mut s);
        // EQ 9 E.Bass 1 onto MIC 1: LOW a shelf at 35 Hz, -7.5 dB.
        assert!(c.recall_eq(&lib, Y2Strip::Mic(0), 8));
        let st = e.channel_at(0).unwrap();
        assert_eq!(st.params.eq().param("lg").unwrap().load(), -7.5);
        assert_eq!(c.eq_value(Y2Strip::Mic(0), 0, 2), 41, "L.SHELF");
        // Dynamics 7 A.Dr.SN gate onto BUS 2.
        assert!(c.recall_dyn(&lib, Y2Strip::Bus(1), 6));
        assert_eq!(c.dyn_type(Y2Strip::Bus(1)), 4);
        assert!(!c.recall_dyn(&lib, Y2Strip::Aux(0), 6), "AUX has none");
        // Effects 28 ECHO into Effect 2.
        assert!(c.recall_fx(&lib, 1, 27));
        assert_eq!(e.fx_models()[e.controls.fx[1].model_index_in(e.fx_models())].token, "Y2ECHO");
        assert_eq!(c.state.fx_programs[1], 27);
        // Stores: into the user range only.
        c.set_eq_value(Y2Strip::Mic(0), 1, 1, 50);
        assert!(!c.store_eq(&mut lib, Y2Strip::Mic(0), 5, "NOPE"));
        assert!(c.store_eq(&mut lib, Y2Strip::Mic(0), 40, "MY BASS"));
        assert!(c.recall_eq(&lib, Y2Strip::Mic(4), 40));
        assert_eq!(c.eq_value(Y2Strip::Mic(4), 1, 1), 50);
        assert!(c.store_dyn(&mut lib, Y2Strip::Bus(1), 41, "BUS GATE") && c.store_fx(&mut lib, 1, 100, "MY ECHO"));
        assert_eq!(lib.get(Kind::Effects, 100).unwrap().token, "Y2ECHO");
    }

    #[test]
    fn a_pair_copies_one_side_and_links_its_processors() {
        let e = y02r();
        let mut s = State::default();
        let mut c = Console::new(&e, &mut s);
        c.set_level(Y2Strip::Tape(2), 90, Origin::Panel);
        c.set_eq_value(Y2Strip::Tape(2), 1, 1, 50);
        c.set_att(Y2Strip::Tape(3), -10);
        c.set_dyn_type(Y2Strip::Tape(2), 1);
        c.set_group(22, false, 3);
        c.set_pair(9, true, true); // TAPE 3/4
        assert_eq!(c.partner(Y2Strip::Tape(2)), Some(Y2Strip::Tape(3)));
        assert_eq!((c.level(Y2Strip::Tape(3)), c.eq_value(Y2Strip::Tape(3), 1, 1)), (90, 50));
        assert_eq!(c.att(Y2Strip::Tape(3)), -10, "the attenuator isn't linked");
        assert_eq!(c.dyn_type(Y2Strip::Tape(3)), 1);
        assert_eq!(c.state.fader_groups[23], 3);
        assert!(c.dyn_settings(Y2Strip::Tape(3)).st_link);
        // Linked: one move sets both processors.
        c.set_dyn_on(Y2Strip::Tape(3), true);
        assert!(c.dyn_on(Y2Strip::Tape(2)));
        c.set_pair(9, false, true);
        c.set_level(Y2Strip::Tape(2), 10, Origin::Remote);
        assert_eq!(c.level(Y2Strip::Tape(3)), 90, "unpaired");
    }

    #[test]
    fn surround_takes_the_buses_and_gives_them_back() {
        let e = y02r();
        let mut s = State::default();
        let mut c = Console::new(&e, &mut s);
        c.set_bus_on(Y2Strip::Mic(0), 0, true); // MIC 1 to BUS 1
        c.set_bus_on(Y2Strip::Mic(0), 6, true); // and BUS 7
        c.set_surround(1); // 2+2
        let send = |c: &Console, bus| {
            let st = c.strip(Y2Strip::Mic(0)).unwrap();
            let p = st.sends.get_or_create(bus).unwrap();
            (p.enabled.load(Relaxed), p.level_db.load())
        };
        // The front centre: FL and FR alike, the rear silent.
        let (fl, fr, rl) = (send(&c, 0), send(&c, 1), send(&c, 2));
        assert!(fl.0 && fr.0 && !rl.0 && (fl.1 - fr.1).abs() < 1e-4, "{fl:?} {fr:?} {rl:?}");
        assert!(c.bus_on(Y2Strip::Mic(0), 0) && !c.bus_on(Y2Strip::Mic(0), 1), "ROUTING shows its own");
        assert!(c.bus_on(Y2Strip::Mic(0), 6), "BUS 7 as it was");
        // To the rear right.
        let pan = SurroundPan { x: 64, y: 0, ..c.surround_pan(0).unwrap() };
        c.set_surround_pan(0, pan);
        assert!(!send(&c, 0).0 && send(&c, 3).0);
        // Along a circle from the front: a quarter round is the right.
        let pan = SurroundPan { trajectory: 6, along: 16, ..c.surround_pan(0).unwrap() };
        c.set_surround_pan(0, pan);
        assert_eq!((c.surround_pan(0).unwrap().x, c.surround_pan(0).unwrap().y), (64, 32));
        // Off: BUS 1 back as routed, BUS 2 not.
        c.set_surround(0);
        assert!(send(&c, 0).0 && send(&c, 0).1 == 0.0 && !send(&c, 1).0);
        let back = State::from_text(&c.state.to_text());
        assert_eq!(back.surround_pans[0], c.state.surround_pans[0]);
    }

    #[test]
    fn st_link_moves_the_other_side_by_its_pattern() {
        let e = y02r();
        let mut s = State::default();
        let mut c = Console::new(&e, &mut s);
        c.set_surround(1); // 2+2
        let bus = |c: &Console, b| c.strip(Y2Strip::Bus(b)).unwrap().params.mono.load(Relaxed);
        assert!(bus(&c, 0) && bus(&c, 3) && !bus(&c, 4), "the surround buses sum to mono");
        // MIC 1/2 paired: MIC 1 to the front left, MIC 2 mirrored to the front right.
        c.set_pair(0, true, true);
        let pan = SurroundPan { x: 0, y: 64, ..c.surround_pan(0).unwrap() };
        c.set_surround_pan(0, pan);
        let q = c.surround_pan(1).unwrap();
        assert_eq!((q.x, q.y), (64, 64));
        // A quarter turn clockwise, moved from MIC 2: MIC 1 turned back.
        let pan = SurroundPan { x: 64, y: 32, pattern: 6, ..c.surround_pan(1).unwrap() };
        c.set_surround_pan(1, pan);
        let p = c.surround_pan(0).unwrap();
        assert_eq!((p.x, p.y, p.pattern), (32, 64, 6), "to the front");
        // ST LINK off: MIC 2 stays.
        let pan = SurroundPan { link: false, x: 0, y: 0, ..c.surround_pan(0).unwrap() };
        c.set_surround_pan(0, pan);
        assert_eq!((c.surround_pan(1).unwrap().x, c.surround_pan(1).unwrap().link), (64, false));
        // LINE 17/18 to the front left: its right side mirrored to the front
        // right, so its left side alone feeds FL, its right alone FR.
        let line = Y2Strip::Line(0).member().unwrap();
        let pan = SurroundPan { x: 0, y: 64, ..c.surround_pan(line).unwrap() };
        c.set_surround_pan(line, pan);
        let st = c.strip(Y2Strip::Line(0)).unwrap();
        let send = |b| {
            let p = st.sends.get_or_create(b).unwrap();
            (p.enabled.load(Relaxed), p.pan.load(), p.level_db.load())
        };
        let (fl, fr) = (send(0), send(1));
        assert!(fl.0 && fl.1 == -100.0 && (fl.2 - 6.0206).abs() < 1e-3, "{fl:?}");
        assert!(fr.0 && fr.1 == 100.0, "{fr:?}");
        assert!(!send(2).0 && !send(3).0, "the rear silent");
        // Off again: the buses no longer mono.
        c.set_surround(0);
        assert!(!bus(&c, 0));
    }

    #[test]
    fn ms_decoding_keeps_the_faders_apart() {
        let e = y02r();
        let mut s = State::default();
        let mut c = Console::new(&e, &mut s);
        c.set_ms(0, true);
        assert!(c.state.pairs[0] && c.ms(0), "MS pairs the channels");
        assert!(c.strip(Y2Strip::Mic(1)).unwrap().params.ms_side.load(Relaxed));
        assert!(!c.strip(Y2Strip::Mic(0)).unwrap().params.ms_side.load(Relaxed), "M is the odd one");
        c.set_level(Y2Strip::Mic(0), 100, Origin::Panel);
        c.set_level(Y2Strip::Mic(1), 60, Origin::Panel);
        assert_eq!((c.level(Y2Strip::Mic(0)), c.level(Y2Strip::Mic(1))), (100, 60));
        c.set_pair(0, false, true);
        assert!(!c.ms(0) && !c.strip(Y2Strip::Mic(1)).unwrap().params.ms_side.load(Relaxed), "breaking the pair ends MS");
    }

    #[test]
    fn channel_library_stores_recalls_and_undoes() {
        let e = y02r();
        let mut s = State::default();
        let mut c = Console::new(&e, &mut s);
        let mut lib = Library::default();
        c.set_level(Y2Strip::Mic(0), 90, Origin::Remote);
        c.set_aux_level(Y2Strip::Mic(0), 2, 70);
        c.set_on(Y2Strip::Mic(0), false, Origin::Remote);
        assert!(c.store_channel(&mut lib, Y2Strip::Mic(0), 4, "Lead Vox"));
        assert!(!c.store_channel(&mut lib, Y2Strip::Bus(0), 5, "x"), "an output");
        // Onto MIC 3: its settings, its old ones to the UNDO.
        let before = params::channel_bytes(&c, Y2Strip::Mic(2)).unwrap();
        assert!(c.recall_channel(&mut lib, Y2Strip::Mic(2), 4));
        assert_eq!((c.level(Y2Strip::Mic(2)), c.aux_level(Y2Strip::Mic(2), 2), c.on(Y2Strip::Mic(2))), (90, 70, false));
        assert!(!c.recall_channel(&mut lib, Y2Strip::Mic(2), 5), "empty");
        // UNDO puts them back (and holds the program's, again).
        assert!(c.recall_channel(&mut lib, Y2Strip::Mic(2), CHANNEL_PROGRAMS));
        assert_eq!(params::channel_bytes(&c, Y2Strip::Mic(2)).unwrap(), before);
        // A clear goes to the UNDO too.
        assert!(lib.clear_channel(4) && lib.channel[4].is_none());
        assert_eq!(lib.channel_undo.as_ref().unwrap().name, "Lead Vox");
    }
}
