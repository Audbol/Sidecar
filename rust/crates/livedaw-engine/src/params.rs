//! Parameter blocks shared lock-free between the UI and the audio thread.
//!
//! The layout mirrors the WING's parameter tree: every strip has the same
//! block (INPUT, FILTER, GATE slot, EQ, DYNAMICS slot, two inserts, sends to
//! the 4 mains and 8 matrices, DCA and mute-group assignments); each kind uses
//! the parts the console gives it. All structs are `#[repr(C)]` and mirrored
//! by `livedaw_engine.h`.

use crate::atomic::AtomicF32;
use crate::blocks::{Block, BlockParams};
use crate::groups::{AtomicGroupSet, MAX_GROUPS};
use crate::monitor::MonitorControls;
use crate::profile::{Profile, ProfileSpec};
use crate::sends::BankItem;
use crate::solo::solo_mode;
use crate::talk::TalkParams;
use livedaw_dsp::model::{ModelSpec, ModelTables};
use livedaw_dsp::wing::{
    dynamics::{SidechainSettings, XoverSettings},
    filter::FilterSettings,
    model_index, DYN_MODELS, EQ_MODELS_BUS, EQ_MODELS_CH, FILTER_TOOLS, FX_MODELS, MAX_DYN_PARAMS, MAX_EQ_PARAMS, MAX_FX_PARAMS,
    MAX_TOOL_PARAMS,
};
use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering::Relaxed};
use std::sync::Arc;

/// The WING's 4 mains, 8 matrices and 8 aux channels. A console has any
/// number (its configuration, `config.rs`), up to `MAX_MAINS` and
/// `MAX_MATRICES`: every strip's sends to them are paged (`sends::Bank`).
pub const WING_MAINS: usize = 4;
pub const WING_MATRICES: usize = 8;
pub const WING_AUX: usize = 8;
pub const MAX_MAINS: usize = 256;
pub const MAX_MATRICES: usize = 256;
/// Sends to mains and matrices per page of a strip's bank.
pub const MAIN_SEND_PAGE: usize = 8;
/// DCAs and mute groups the engine holds; how many the console uses is set
/// at run time (`Engine::set_num_dcas`, `set_num_mute_groups`).
pub const MAX_DCAS: usize = MAX_GROUPS;
pub const MAX_MUTE_GROUPS: usize = MAX_GROUPS;
/// The WING's 16 DCAs and 8 mute groups.
pub const WING_DCAS: usize = 16;
pub const WING_MUTE_GROUPS: usize = 8;
/// FX slots the engine holds. How many of them the console uses is set at run
/// time (`Engine::set_num_fx`, SETUP > GENERAL > FX SLOTS): the WING's 16 by
/// default, up to this many.
pub const MAX_FX: usize = 64;
/// The WING's FX slots: the default number in use, and the slots the WING
/// remote and the WING's control surface see.
pub const WING_FX: usize = 16;
/// FX slot models (indices into `FX_MODELS`): an empty slot, and EXT (a plugin
/// hosted by the application).
pub const FX_NONE: usize = 0;
pub const FX_EXT: usize = 1;

/// The EXT device's parameters (`FX_MODELS[FX_EXT]`: the WING's `/fx/N/EXT/*`
/// in its order, then Sidecar's SOURCE). Manual 11.11 p. 164, Fig. 169.
pub mod ext {
    /// RETURN's input group (`egrp`: OFF, LCL, AUX, A, B, C, SC, USB, CRD, MOD, PLAY, AES).
    pub const GROUP: usize = 0;
    /// RETURN's first input (`ein`, 1-64).
    pub const INPUT: usize = 1;
    /// FX MODE (`emode`): MONO, STEREO, MID/SIDE.
    pub const MODE: usize = 2;
    /// LATENCY (`lat`, samples; see `fx.rs`): the dry signal's delay beyond the round trip
    /// Sidecar knows (one block and the interface's reported latencies).
    pub const LATENCY: usize = 3;
    /// TRIM (`trim`): the send level, dB.
    pub const TRIM: usize = 4;
    /// SOURCE (`src`, Sidecar's own): 0 PLUGIN (a plugin hosted in the slot), 1 OUTBOARD.
    pub const SOURCE: usize = 5;
    pub const MODE_MONO: i32 = 0;
    pub const MODE_STEREO: i32 = 1;
    pub const MODE_MID_SIDE: i32 = 2;
    pub const SOURCE_PLUGIN: f32 = 0.0;
    pub const SOURCE_OUTBOARD: f32 = 1.0;
}
/// The WING's channel and bus counts (this console can grow beyond them).
pub const WING_CHANNELS: usize = 40;
pub const WING_BUSES: usize = 16;

#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum StripKind {
    /// Input channel (40 on the console).
    Channel = 0,
    /// Aux input channel (8).
    Aux = 1,
    /// Stereo mix bus (16).
    Bus = 2,
    /// Stereo main bus (4).
    Main = 3,
    /// Stereo matrix bus (8).
    Matrix = 4,
}

impl StripKind {
    pub const ALL: [StripKind; 5] = [StripKind::Channel, StripKind::Aux, StripKind::Bus, StripKind::Main, StripKind::Matrix];

    pub fn from_i32(v: i32) -> Option<Self> {
        Self::ALL.get(usize::try_from(v).ok()?).copied()
    }

    /// Fed by sources (input channels and aux channels).
    pub fn is_input(self) -> bool {
        matches!(self, Self::Channel | Self::Aux)
    }

    pub fn is_bus(self) -> bool {
        self == Self::Bus
    }

    /// Summing buses (bus, main, matrix).
    pub fn is_output(self) -> bool {
        !self.is_input()
    }

    /// Name used in show files and parameter paths ("ch", "aux", "bus", "main", "mtx").
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Channel => "ch",
            Self::Aux => "aux",
            Self::Bus => "bus",
            Self::Main => "main",
            Self::Matrix => "mtx",
        }
    }

    /// Inverse of [`as_str`](Self::as_str); unknown names are channels.
    pub fn parse(s: &str) -> Self {
        match s {
            "aux" => Self::Aux,
            "bus" => Self::Bus,
            "main" => Self::Main,
            "mtx" => Self::Matrix,
            _ => Self::Channel,
        }
    }

    /// Short label as on the scribble strips ("CH", "AUX", "BUS", "MAIN", "MTX").
    pub fn label(self) -> &'static str {
        match self {
            Self::Channel => "CH",
            Self::Aux => "AUX",
            Self::Bus => "BUS",
            Self::Main => "MAIN",
            Self::Matrix => "MTX",
        }
    }
}

// ---- blocks ---------------------------------------------------------------------------

/// Where the channels' pre-fade sends to a mix leave them (`MixInputParams::mix_source`).
pub mod mix_source {
    pub const POST_PREAMP: i32 = 0;
    pub const POST_INSERT_A: i32 = 1;
    pub const POST_PEQ: i32 = 2;
    /// Their usual tap, and every console's but a dLive's.
    pub const POST_DELAY: i32 = 3;
}

/// A mix's Ext In and Mix Source (a dLive's: Firmware Reference Guide, Processing
/// Screen and Routing).
#[repr(C)]
#[derive(Debug)]
pub struct MixInputParams {
    /// The input summed in before the mix's insert (-1: none). A stereo mix takes it
    /// and the next one.
    pub ext_source: AtomicI32,
    pub ext_trim_db: AtomicF32,
    pub ext_invert: AtomicBool,
    pub mix_source: AtomicI32,
}

impl Default for MixInputParams {
    fn default() -> Self {
        Self {
            ext_source: AtomicI32::new(-1),
            ext_trim_db: AtomicF32::new(0.0),
            ext_invert: AtomicBool::new(false),
            mix_source: AtomicI32::new(mix_source::POST_DELAY),
        }
    }
}

/// INPUT: trim, balance, polarity and delay (input delay on channels, output
/// delay on buses, mains and matrices).
#[repr(C)]
#[derive(Debug)]
pub struct InputParams {
    pub trim_db: AtomicF32,
    pub balance_db: AtomicF32,
    pub invert: AtomicBool,
    pub delay_on: AtomicBool,
    /// Index into `livedaw_dsp::delay::DELAY_MODES` (M, FT, MS, SMP).
    pub delay_mode: AtomicI32,
    /// In `delay_mode` units (0.1 m .. 150 m as the WING's DIST, in any unit).
    pub delay: AtomicF32,
}

/// The WING's delay range (`/ch/n/in/set/dly`, 0.1 .. 150 m) and its
/// factory setting: METERS, 0.1 m (off).
pub const DELAY_MIN_M: f32 = 0.1;
pub const DELAY_MAX_M: f32 = 150.0;

impl Default for InputParams {
    fn default() -> Self {
        Self {
            trim_db: AtomicF32::new(0.0),
            balance_db: AtomicF32::new(0.0),
            invert: AtomicBool::new(false),
            delay_on: AtomicBool::new(false),
            delay_mode: AtomicI32::new(0),
            delay: AtomicF32::new(DELAY_MIN_M),
        }
    }
}

/// FILTER: low cut, high cut, tool filter.
#[repr(C)]
#[derive(Debug)]
pub struct FilterParams {
    pub low_cut: AtomicBool,
    pub high_cut: AtomicBool,
    pub tool: AtomicBool,
    pub low_cut_freq: AtomicF32,
    pub low_cut_slope: AtomicI32,
    pub high_cut_freq: AtomicF32,
    pub high_cut_slope: AtomicI32,
    pub tool_model: AtomicI32,
    pub tool_params: [AtomicF32; MAX_TOOL_PARAMS],
}

impl Default for FilterParams {
    fn default() -> Self {
        let d = FilterSettings::default();
        let p = Self {
            low_cut: AtomicBool::new(d.low_cut),
            high_cut: AtomicBool::new(d.high_cut),
            tool: AtomicBool::new(d.tool),
            low_cut_freq: AtomicF32::new(d.low_cut_freq),
            low_cut_slope: AtomicI32::new(d.low_cut_slope as i32),
            high_cut_freq: AtomicF32::new(d.high_cut_freq),
            high_cut_slope: AtomicI32::new(d.high_cut_slope as i32),
            tool_model: AtomicI32::new(0),
            tool_params: Default::default(),
        };
        p.set_tool_model(0);
        p
    }
}

impl FilterParams {
    /// Selects a tool filter and loads its defaults.
    pub fn set_tool_model(&self, model: usize) {
        let Some(spec) = FILTER_TOOLS.get(model) else { return };
        self.tool_model.store(model as i32, Relaxed);
        for (i, v) in self.tool_params.iter().enumerate() {
            v.store(spec.params.get(i).map_or(0.0, |p| p.default));
        }
    }

    pub fn snapshot(&self) -> FilterSettings {
        FilterSettings {
            low_cut: self.low_cut.load(Relaxed),
            low_cut_freq: self.low_cut_freq.load(),
            low_cut_slope: self.low_cut_slope.load(Relaxed).clamp(0, 3) as usize,
            high_cut: self.high_cut.load(Relaxed),
            high_cut_freq: self.high_cut_freq.load(),
            high_cut_slope: self.high_cut_slope.load(Relaxed).clamp(0, 1) as usize,
            tool: self.tool.load(Relaxed),
            tool_model: self.tool_model.load(Relaxed).max(0) as usize,
            tool_params: [self.tool_params[0].load(), self.tool_params[1].load()],
        }
    }
}

/// A dynamics slot (GATE or DYNAMICS) holding any of the 32 models.
#[repr(C)]
#[derive(Debug)]
pub struct DynSlotParams {
    pub on: AtomicBool,
    /// Index into `DYN_MODELS`.
    pub model: AtomicI32,
    /// The model's parameters, in its table order.
    pub values: [AtomicF32; MAX_DYN_PARAMS],
    /// Wet/dry in %.
    pub mix: AtomicF32,
    /// Make-up gain in dB.
    pub gain: AtomicF32,
    pub sc_filter: AtomicI32,
    pub sc_freq: AtomicF32,
    pub sc_q: AtomicF32,
    /// -1 = the strip itself, else the index of the keying input channel.
    pub sc_source: AtomicI32,
    pub sc_tap: AtomicI32,
    pub xo_kind: AtomicI32,
    pub xo_freq: AtomicF32,
    pub xo_q: AtomicF32,
    pub xo_depth: AtomicF32,
    /// The models `model` indexes: its console's dynamics table (Phase 4; the
    /// C side mirrors it as two words it doesn't read).
    table: &'static [ModelSpec],
}

const _: () = assert!(std::mem::size_of::<&[ModelSpec]>() == 2 * std::mem::size_of::<usize>());

/// The aux channels' dynamics slot holds the WING GATE at -36 dB on a
/// factory-set console.
pub const AUX_GATE_THRESHOLD_DB: f32 = -36.0;

impl DynSlotParams {
    /// A WING slot holding `model_token`.
    pub fn new(model_token: &str) -> Self {
        Self::new_in(DYN_MODELS, model_token)
    }

    /// A slot of a console whose dynamics models are `table`, holding
    /// `model_token` (the first model if the table hasn't it).
    pub fn new_in(table: &'static [ModelSpec], model_token: &str) -> Self {
        let (sc, xo) = (SidechainSettings::default(), XoverSettings::default());
        let p = Self {
            on: AtomicBool::new(false),
            model: AtomicI32::new(0),
            values: Default::default(),
            mix: AtomicF32::new(100.0),
            gain: AtomicF32::new(0.0),
            sc_filter: AtomicI32::new(0),
            sc_freq: AtomicF32::new(sc.freq),
            sc_q: AtomicF32::new(sc.q),
            sc_source: AtomicI32::new(-1),
            sc_tap: AtomicI32::new(0),
            xo_kind: AtomicI32::new(0),
            xo_freq: AtomicF32::new(xo.freq),
            xo_q: AtomicF32::new(xo.q),
            xo_depth: AtomicF32::new(xo.depth),
            table,
        };
        p.set_model(model_index(table, model_token).unwrap_or(0));
        p
    }

    /// The models the slot can hold (its console's).
    pub fn table(&self) -> &'static [ModelSpec] {
        self.table
    }

    /// Selects a model and loads its defaults (the session keeps per-model memories).
    pub fn set_model(&self, model: usize) {
        let Some(spec) = self.table.get(model) else { return };
        self.model.store(model as i32, Relaxed);
        let mut d = [0.0; MAX_DYN_PARAMS];
        spec.defaults_into(&mut d);
        for (v, x) in self.values.iter().zip(d) {
            v.store(x);
        }
    }

    pub fn model_index(&self) -> usize {
        (self.model.load(Relaxed).max(0) as usize).min(self.table.len().max(1) - 1)
    }

    /// The current model.
    pub fn model_spec(&self) -> Option<&'static ModelSpec> {
        self.table.get(self.model_index())
    }

    /// A parameter of the current model by its console id ("thr").
    pub fn param(&self, id: &str) -> Option<&AtomicF32> {
        self.values.get(self.model_spec()?.param_index(id)?)
    }

    pub fn values_snapshot(&self, out: &mut [f32; MAX_DYN_PARAMS]) {
        for (o, v) in out.iter_mut().zip(&self.values) {
            *o = v.load();
        }
    }

    pub fn sidechain(&self) -> SidechainSettings {
        SidechainSettings {
            filter: self.sc_filter.load(Relaxed).clamp(0, 4) as usize,
            freq: self.sc_freq.load(),
            q: self.sc_q.load(),
        }
    }

    pub fn xover(&self) -> XoverSettings {
        XoverSettings {
            kind: self.xo_kind.load(Relaxed).clamp(0, 5) as usize,
            freq: self.xo_freq.load(),
            q: self.xo_q.load(),
            depth: self.xo_depth.load(),
        }
    }
}

/// EQ block: model + its parameters.
#[repr(C)]
#[derive(Debug)]
pub struct EqBlockParams {
    pub on: AtomicBool,
    /// Index into `EQ_MODELS_CH` (channels) or `EQ_MODELS_BUS` (buses, mains, matrices).
    pub model: AtomicI32,
    /// 0..125 %.
    pub mix: AtomicF32,
    pub values: [AtomicF32; MAX_EQ_PARAMS],
    /// The models `model` indexes: its console's channel or bus EQ table
    /// (the C side mirrors it as two words it doesn't read).
    table: &'static [ModelSpec],
}

impl EqBlockParams {
    /// A WING EQ: the bus table's first model (`bus`), else the channel table's.
    pub fn new(bus: bool) -> Self {
        Self::new_in(if bus { EQ_MODELS_BUS } else { EQ_MODELS_CH })
    }

    /// An EQ of a console whose models (channel or bus EQ) are `table`, on its first.
    pub fn new_in(table: &'static [ModelSpec]) -> Self {
        let p = Self {
            on: AtomicBool::new(true),
            model: AtomicI32::new(0),
            mix: AtomicF32::new(100.0),
            values: std::array::from_fn(|_| AtomicF32::new(0.0)),
            table,
        };
        p.set_model(0);
        p
    }

    /// The models the EQ can hold (its console's).
    pub fn table(&self) -> &'static [ModelSpec] {
        self.table
    }

    pub fn set_model(&self, model: usize) {
        let Some(spec) = self.table.get(model) else { return };
        self.model.store(model as i32, Relaxed);
        let mut d = [0.0; MAX_EQ_PARAMS];
        spec.defaults_into(&mut d);
        for (v, x) in self.values.iter().zip(d) {
            v.store(x);
        }
    }

    pub fn model_index(&self) -> usize {
        (self.model.load(Relaxed).max(0) as usize).min(self.table.len().max(1) - 1)
    }

    /// The current model.
    pub fn model_spec(&self) -> Option<&'static ModelSpec> {
        self.table.get(self.model_index())
    }

    /// A parameter of the current model by its console id ("2g").
    pub fn param(&self, id: &str) -> Option<&AtomicF32> {
        self.values.get(self.model_spec()?.param_index(id)?)
    }

    pub fn values_snapshot(&self, out: &mut [f32; MAX_EQ_PARAMS]) {
        for (o, v) in out.iter_mut().zip(&self.values) {
            *o = v.load();
        }
    }
}

/// POST INSERT modes.
pub const POST_INSERT_MODES: [&str; 3] = ["FX", "AUTO_X", "AUTO_Y"];

/// An insert point: one of the 16 FX slots (or automix on the post insert).
#[repr(C)]
#[derive(Debug)]
pub struct InsertParams {
    pub on: AtomicBool,
    /// FX slot index, -1 = none.
    pub fx: AtomicI32,
    /// Post insert only: index into [`POST_INSERT_MODES`].
    pub mode: AtomicI32,
    /// Automix weight in dB.
    pub weight: AtomicF32,
}

impl Default for InsertParams {
    fn default() -> Self {
        Self { on: AtomicBool::new(false), fx: AtomicI32::new(-1), mode: AtomicI32::new(0), weight: AtomicF32::new(0.0) }
    }
}

/// Send from a strip to a main bus (`Strip::mains`, by the main's index).
#[repr(C)]
#[derive(Debug)]
pub struct MainSendParams {
    pub on: AtomicBool,
    /// Taken at the strip's tap point instead of post-fader (the strip's mute
    /// still cuts it).
    pub pre: AtomicBool,
    pub level_db: AtomicF32,
    /// ABI 27: a user tap of the source strip to take the signal at (`taps.rs`; -1 = none).
    pub tap: AtomicI32,
    /// Gain ramp state (left, right), owned by the task that sums the main.
    pub(crate) last_gain: UnsafeCell<[f32; 2]>,
}

// SAFETY: `last_gain` is only touched by the task that sums the main; the rest is atomic.
unsafe impl Sync for MainSendParams {}

impl MainSendParams {
    /// Message thread: back to the factory send `index` of a strip of `kind`
    /// in `slot` (a main added again starts clean). The ramp is left alone.
    pub fn reset(&self, spec: &ProfileSpec, kind: StripKind, slot: usize, index: usize) {
        let f = Self::factory(spec, kind, slot, index);
        self.on.store(f.on.load(Relaxed), Relaxed);
        self.pre.store(f.pre.load(Relaxed), Relaxed);
        self.level_db.store(f.level_db.load());
        self.tap.store(f.tap.load(Relaxed), Relaxed);
    }

    /// The send's gain ramp.
    ///
    /// # Safety
    /// The task summing the main only.
    #[inline]
    #[allow(clippy::mut_from_ref)]
    pub(crate) unsafe fn last(&self) -> &mut [f32; 2] {
        &mut *self.last_gain.get()
    }
}

impl BankItem for MainSendParams {
    /// Channels and aux feed Main 1; buses are aux mixes until routed, but
    /// the console's `buses_to_main` (a factory-set WING's buses 9-16) feed
    /// Main 1 (`StripParams::new_in_slot`).
    fn factory(spec: &ProfileSpec, kind: StripKind, slot: usize, index: usize) -> Self {
        let main1 = index == 0 && (kind.is_input() || (kind == StripKind::Bus && spec.buses_to_main.contains(&slot)));
        // A mono main's send (an X32's M/C level) starts at -oo.
        let level = if spec.mono_mains.contains(&index) { livedaw_dsp::fader::FADER_MIN } else { 0.0 };
        Self {
            on: AtomicBool::new(main1),
            pre: AtomicBool::new(false),
            level_db: AtomicF32::new(level),
            tap: AtomicI32::new(-1),
            last_gain: UnsafeCell::new([0.0; 2]),
        }
    }
}

/// Send from a strip to a matrix (`Strip::matrices`, by the matrix's index).
#[repr(C)]
#[derive(Debug)]
pub struct MatrixSendParams {
    pub on: AtomicBool,
    /// TAP mode: taken at the strip's tap point (pre-fader for buses and mains).
    pub pre: AtomicBool,
    /// IGNORE CHANNEL MUTE (input and aux channels, TAP mode only): the send
    /// stays on while the channel is muted.
    pub ignore_mute: AtomicBool,
    /// PAN LINK (input and aux channels): the send's pan follows the
    /// channel's pan (else `pan` is used). Buses and mains always send their
    /// panned output, balanced by `pan`.
    pub pan_link: AtomicBool,
    /// With `pan_link`: follow the channel's pan mirrored (LINK REVERSED).
    pub pan_reversed: AtomicBool,
    pub level_db: AtomicF32,
    pub pan: AtomicF32,
    /// ABI 27: a user tap of the source strip to take the signal at (`taps.rs`; -1 = none).
    pub tap: AtomicI32,
    /// Gain ramp state (left, right), owned by the task that sums the matrix.
    pub(crate) last_gain: UnsafeCell<[f32; 2]>,
}

// SAFETY: `last_gain` is only touched by the task that sums the matrix; the rest is atomic.
unsafe impl Sync for MatrixSendParams {}

impl MatrixSendParams {
    /// Message thread: back to the factory send of a strip of `kind` (a matrix
    /// added again starts clean). The ramp is left alone.
    pub fn reset(&self, kind: StripKind) {
        let f = Self::factory(Profile::Wing.spec(), kind, 0, 0);
        self.on.store(f.on.load(Relaxed), Relaxed);
        self.pre.store(f.pre.load(Relaxed), Relaxed);
        self.ignore_mute.store(f.ignore_mute.load(Relaxed), Relaxed);
        self.pan_link.store(f.pan_link.load(Relaxed), Relaxed);
        self.pan_reversed.store(f.pan_reversed.load(Relaxed), Relaxed);
        self.level_db.store(f.level_db.load());
        self.pan.store(f.pan.load());
        self.tap.store(f.tap.load(Relaxed), Relaxed);
    }

    /// The send's gain ramp.
    ///
    /// # Safety
    /// The task summing the matrix only.
    #[inline]
    #[allow(clippy::mut_from_ref)]
    pub(crate) unsafe fn last(&self) -> &mut [f32; 2] {
        &mut *self.last_gain.get()
    }
}

impl BankItem for MatrixSendParams {
    /// The WING's factory matrix sends: off at -oo without pan link, input
    /// and aux channels in PRE (TAP), buses and mains post-fader.
    fn factory(spec: &ProfileSpec, kind: StripKind, _slot: usize, _index: usize) -> Self {
        Self {
            // An X32's buses and mains send to its matrices: on at -oo.
            on: AtomicBool::new(spec.sends_on && !kind.is_input()),
            pre: AtomicBool::new(kind.is_input()),
            ignore_mute: AtomicBool::new(false),
            pan_link: AtomicBool::new(false),
            pan_reversed: AtomicBool::new(false),
            level_db: AtomicF32::new(livedaw_dsp::fader::FADER_MIN),
            pan: AtomicF32::new(0.0),
            tap: AtomicI32::new(-1),
            last_gain: UnsafeCell::new([0.0; 2]),
        }
    }
}

/// Monitor bus a solo goes to (SOLO MODE LIVE): A = PHONES, B = SPEAKERS, both.
pub const MONITOR_BUSES: [&str; 3] = ["A", "B", "A+B"];

/// A strip's own blocks (the WING's FILTER, GATE, EQ, DYNAMICS, INS 1 and
/// INS 2): chain blocks like the extra ones (`blocks.rs`), parameters, meters
/// and DSP state together, run by the nodes of the strip's template
/// (`Node::Filter`...; the slots' meters stay in `StripMeters`). The C ABI
/// reads their parameters through the pointers (`StripParams` in
/// `livedaw_engine.h`); `blocks` keeps what they point into.
#[repr(C)]
pub struct OwnBlocks {
    filter: *const FilterParams,
    gate: *const DynSlotParams,
    eq: *const EqBlockParams,
    dynamics: *const DynSlotParams,
    pre_insert: *const InsertParams,
    post_insert: *const InsertParams,
    blocks: [Arc<Block>; 6],
}

// The C side mirrors `blocks` as six pointers.
const _: () = assert!(std::mem::size_of::<Arc<Block>>() == std::mem::size_of::<*const Block>());

// SAFETY: the pointers point into the blocks `OwnBlocks` keeps (`Block` is
// Send and Sync), never written after `new`.
unsafe impl Send for OwnBlocks {}
unsafe impl Sync for OwnBlocks {}

impl OwnBlocks {
    pub const FILTER: usize = 0;
    pub const GATE: usize = 1;
    pub const EQ: usize = 2;
    pub const DYNAMICS: usize = 3;
    pub const PRE_INSERT: usize = 4;
    pub const POST_INSERT: usize = 5;

    fn new(gate: DynSlotParams, eq: EqBlockParams, dynamics: DynSlotParams) -> Self {
        let blocks = [
            BlockParams::Filter(FilterParams::default()),
            BlockParams::Dynamics(gate),
            BlockParams::Eq(eq),
            BlockParams::Dynamics(dynamics),
            BlockParams::Insert(InsertParams::default()),
            BlockParams::Insert(InsertParams::default()),
        ]
        .map(|p| Arc::new(Block::own(p)));
        let dyn_slot = |b: &Block| match &b.params {
            BlockParams::Dynamics(p) => p as *const DynSlotParams,
            _ => unreachable!(),
        };
        let insert = |b: &Block| match &b.params {
            BlockParams::Insert(p) => p as *const InsertParams,
            _ => unreachable!(),
        };
        Self {
            filter: match &blocks[Self::FILTER].params {
                BlockParams::Filter(p) => p,
                _ => unreachable!(),
            },
            gate: dyn_slot(&blocks[Self::GATE]),
            eq: match &blocks[Self::EQ].params {
                BlockParams::Eq(p) => p,
                _ => unreachable!(),
            },
            dynamics: dyn_slot(&blocks[Self::DYNAMICS]),
            pre_insert: insert(&blocks[Self::PRE_INSERT]),
            post_insert: insert(&blocks[Self::POST_INSERT]),
            blocks,
        }
    }
}

impl std::fmt::Debug for OwnBlocks {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_list().entries(self.blocks.iter().map(|b| &b.params)).finish()
    }
}

/// Everything the UI can change on a strip. Bus sends live in its `SendBank`.
#[repr(C)]
#[derive(Debug)]
pub struct StripParams {
    pub input: InputParams,
    /// The strip's own blocks, chain blocks run by its template's nodes
    /// (Phase 3 step 5), reached through `filter()`, `gate()`...
    blocks: OwnBlocks,
    /// Index into `PROCESS_ORDERS` (input channels; aux, buses, mains and
    /// matrices have a fixed order).
    pub process_order: AtomicI32,
    /// Index into `TAP_POINTS`: where TAP-mode sends are taken (input channels;
    /// aux, buses, mains and matrices are fixed at PFL, after the pre insert).
    pub tap: AtomicI32,

    pub fader_db: AtomicF32,
    /// -100 (left) .. +100 (right).
    pub pan: AtomicF32,
    /// Stereo width in % (-150..150).
    pub width: AtomicF32,
    pub mute: AtomicBool,
    pub solo: AtomicBool,
    pub solo_safe: AtomicBool,
    /// Index into [`MONITOR_BUSES`].
    pub monitor_bus: AtomicI32,

    /// Mono or stereo processing (inputs; buses, mains and matrices are stereo).
    pub stereo: AtomicBool,
    /// Buses: sum the bus to mono.
    pub mono: AtomicBool,
    /// Hardware input channels (-1 = none); the right one is used when stereo.
    pub input_left: AtomicI32,
    pub input_right: AtomicI32,
    /// Hardware outputs (-1 = none). Channels: direct out (pre-fader).
    pub output_left: AtomicI32,
    pub output_right: AtomicI32,

    /// The DCAs the strip is a member of (0-based).
    pub dca_mask: AtomicGroupSet,
    /// The mute groups the strip is a member of (0-based).
    pub mute_group_mask: AtomicGroupSet,

    /// Matrices: DIRECT IN (manual 6.4; `/mtx/n/dir`), added to the matrix's sum.
    pub direct_on: AtomicBool,
    pub direct_invert: AtomicBool,
    /// ABI 19: index into `routing::MATRIX_DIRECT_INPUTS` (OFF, AES, MON.PH,
    /// MON.SPK, MON.BUS); before, an input channel's index.
    pub direct_source: AtomicI32,
    pub direct_level_db: AtomicF32,

    /// WING colour index 1..18.
    pub colour: AtomicI32,
    /// WING icon number (0 = none).
    pub icon: AtomicI32,
    /// Scribble strip light.
    pub led: AtomicBool,

    /// Scene recalls skip this strip (snippets still apply).
    pub recall_safe: AtomicBool,

    /// ALT input (WING INPUT SELECT, input and aux channels): the source used
    /// instead of `input_left`/`input_right` when ALT is selected (-1 = none;
    /// the right one is used when stereo). Like the main input, a hardware
    /// input or a playback channel (`PLAYBACK_SOURCE_BASE + n`).
    pub alt_input_left: AtomicI32,
    pub alt_input_right: AtomicI32,
    /// The strip's MAIN / ALT switch (WING `/ch/n/in/set/altsrc`).
    pub alt_selected: AtomicBool,
    /// ABI 22: INPUT SELECT GLOBAL, the strip is in SETUP > AUDIO's GLOBAL
    /// INPUT SELECT ENABLE grid (WING `/ch/n/in/set/srcauto`): the global
    /// MAIN / ALT switch and AUTO INPUT SELECT switch it.
    pub src_auto: AtomicBool,

    // ABI 7: the TAP-mode send signal (WING `/ch/n/tapwid`, `/ch/n/peq`).
    /// TAP WIDTH (input channels, -150..150 %): the stereo width of the
    /// signal TAP-mode sends take (stereo channels; negative swaps L and R).
    pub tap_width: AtomicF32,
    /// TAP EQ (input channels): a 3-band EQ on the TAP-mode send signal.
    pub tap_eq: TapEqParams,

    // ABI 12: LINK CUSTOMIZATION TO SOURCE (WING `/ch/n/clink`).
    /// Input and aux channels: show the name, colour and icon of the source
    /// in use instead of their own (`colour`, `icon` and the name stay the
    /// channel's own, used again when the link is off or there is no source).
    pub cust_link: AtomicBool,

    /// ABI 50: MS decoding's S channel (an 02R pair, V2 MAN p.186): a mono
    /// strip's right side panned with its polarity reversed (R = M - S).
    pub ms_side: AtomicBool,
}

/// One band of the TAP EQ.
#[repr(C)]
#[derive(Debug)]
pub struct TapEqBand {
    /// -15..15 dB.
    pub gain_db: AtomicF32,
    /// 20..20000 Hz.
    pub freq: AtomicF32,
    /// 0.44..10.
    pub q: AtomicF32,
}

/// TAP EQ (the WING's PRE EQ, `/ch/n/peq`): band 1 a low shelf, band 2 a
/// peak, band 3 a high shelf.
#[repr(C)]
#[derive(Debug)]
pub struct TapEqParams {
    pub on: AtomicBool,
    pub bands: [TapEqBand; TAP_EQ_BANDS],
}

pub const TAP_EQ_BANDS: usize = 3;
/// Default frequency and Q of the TAP EQ bands: 100 Hz, 1 kHz and 10 kHz at Q
/// 1.00, as a factory-set console reports them (on the parameters' steps).
pub const TAP_EQ_DEFAULTS: [(f32, f32); TAP_EQ_BANDS] = [(99.68543, 0.9979702), (999.2505, 0.9979702), (10016.53, 0.9979702)];
pub const TAP_EQ_GAIN_RANGE: f32 = 15.0;
pub const TAP_EQ_Q_RANGE: (f32, f32) = (0.442, 10.0);
pub const TAP_WIDTH_RANGE: f32 = 150.0;

impl Default for TapEqParams {
    fn default() -> Self {
        let band = |i: usize| TapEqBand {
            gain_db: AtomicF32::new(0.0),
            freq: AtomicF32::new(TAP_EQ_DEFAULTS[i].0),
            q: AtomicF32::new(TAP_EQ_DEFAULTS[i].1),
        };
        Self { on: AtomicBool::new(false), bands: std::array::from_fn(band) }
    }
}

impl TapEqParams {
    /// Back to the defaults (off, flat).
    pub fn reset(&self) {
        self.on.store(false, Relaxed);
        for (b, (f, q)) in self.bands.iter().zip(TAP_EQ_DEFAULTS) {
            b.gain_db.store(0.0);
            b.freq.store(f);
            b.q.store(q);
        }
    }
}

/// The WING's 18 strip colours (index 1..18 in `colour`): a name for each and
/// the RGB the console draws, measured on a full-size WING's ICON/COLOR screen.
/// The index of each swatch was read from a WING (channel 1's colour number
/// as each swatch was touched); colour 1 (Dark Blue) is the factory colour of
/// channels, sources and DCAs. `WING_COLOUR_GRID` is the screen's layout.
pub const WING_COLOURS: [(&str, u32); 18] = [
    ("Dark Blue", 0x380088),
    ("Indigo", 0x3000d0),
    ("Violet", 0x8800d8),
    ("Teal", 0x0088a0),
    ("Green", 0x00bc00),
    ("Lime", 0x70f400),
    ("Yellow", 0xd0d400),
    ("Brown", 0x904400),
    ("Red", 0xe00000),
    ("Salmon", 0xf05c78),
    ("Pink", 0xf80090),
    ("Magenta", 0xd800f8),
    ("Olive", 0xc89800),
    ("Blue", 0x0084f0),
    ("Orange", 0xd05400),
    ("Turquoise", 0x10a880),
    ("Gray", 0x605460),
    ("Lavender", 0xc8b8e0),
];

/// ICON/COLOR's colour grid, row by row (three to a row): the colour index
/// at each place, as the console lays them out.
pub const WING_COLOUR_GRID: [i32; 18] = [13, 12, 6, 7, 11, 5, 14, 9, 16, 2, 15, 4, 1, 10, 17, 3, 8, 18];

/// The colour the WING gives a new channel, aux, main, matrix, source and DCA (Dark Blue).
pub const DEFAULT_COLOUR: i32 = 1;

/// The RGB values the first versions used for the 18 colours. Shows saved
/// with ARGB colours (legacy files) used these, so they map back exactly.
const PREVIOUS_RGB: [u32; 18] = [
    0x3a6fe8, 0x4fa6f7, 0x5a4fe0, 0x2ec6c6, 0x2fb84a, 0x9acd32, 0xf2d230, 0xb07a3a, 0xe83a3a, 0xf07c6c, 0xe84aa8, 0x9b4fe0,
    0xf0a030, 0x7ec8f0, 0xf06030, 0x6ee0b0, 0x9a9a9a, 0xf0f0f0,
];

/// RGB of WING colour `index` (1..18; anything else is gray).
pub fn colour_rgb(index: i32) -> u32 {
    usize::try_from(index - 1).ok().and_then(|i| WING_COLOURS.get(i)).map_or(0x9a9a9a, |c| c.1)
}

/// The WING colour (1..18) closest to an RGB value (against the console's
/// colours and the values earlier versions saved).
pub fn nearest_colour(rgb: u32) -> i32 {
    let split = |c: u32| [(c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff].map(|v| v as i32);
    let target = split(rgb);
    let distance = |c: u32| split(c).iter().zip(&target).map(|(a, b)| (a - b) * (a - b)).sum::<i32>();
    let best = WING_COLOURS
        .iter()
        .zip(PREVIOUS_RGB)
        .enumerate()
        .min_by_key(|(_, (c, previous))| distance(c.1).min(distance(*previous)))
        .map_or(0, |(i, _)| i);
    best as i32 + 1
}

#[cfg(test)]
mod colour_tests {
    use super::*;

    #[test]
    fn colours_map_back_to_their_index() {
        for (i, (c, previous)) in WING_COLOURS.iter().zip(PREVIOUS_RGB).enumerate() {
            assert_eq!(colour_rgb(i as i32 + 1), c.1);
            assert_eq!(nearest_colour(c.1), i as i32 + 1, "{}", c.0);
            assert_eq!(nearest_colour(previous), i as i32 + 1, "previous {}", c.0);
        }
        assert_eq!(colour_rgb(0), 0x9a9a9a);
        assert_eq!(nearest_colour(0xff0000), 9); // pure red is the WING's Red
        assert_eq!(colour_rgb(DEFAULT_COLOUR), 0x380088); // the factory Dark Blue
        let mut grid = WING_COLOUR_GRID;
        grid.sort_unstable();
        assert_eq!(grid, std::array::from_fn(|i| i as i32 + 1), "every colour once");
    }

    #[test]
    fn the_own_blocks_are_chain_blocks() {
        // The accessors read the blocks' own parameters, of the kind each names.
        let p = StripParams::new(StripKind::Channel);
        let at = |k: usize| &p.own_block(k).params;
        assert!(matches!(at(OwnBlocks::FILTER), BlockParams::Filter(f) if std::ptr::eq(f, p.filter())));
        assert!(matches!(at(OwnBlocks::GATE), BlockParams::Dynamics(d) if std::ptr::eq(d, p.gate())));
        assert!(matches!(at(OwnBlocks::EQ), BlockParams::Eq(e) if std::ptr::eq(e, p.eq())));
        assert!(matches!(at(OwnBlocks::DYNAMICS), BlockParams::Dynamics(d) if std::ptr::eq(d, p.dynamics())));
        assert!(matches!(at(OwnBlocks::PRE_INSERT), BlockParams::Insert(i) if std::ptr::eq(i, p.pre_insert())));
        assert!(matches!(at(OwnBlocks::POST_INSERT), BlockParams::Insert(i) if std::ptr::eq(i, p.post_insert())));
        assert!(p.own_blocks().all(|b| b.uid() == 0));
        // Moving the strip's parameters leaves the pointers good.
        let moved = Box::new(p);
        assert!(matches!(&moved.own_block(OwnBlocks::EQ).params, BlockParams::Eq(e) if std::ptr::eq(e, moved.eq())));
    }

    #[test]
    fn new_strips_start_as_the_wings_factory_init() {
        // A channel: fader down, EQ off, customization linked to its source,
        // Dark Blue, the delay in METERS at its 0.1 m minimum.
        let ch = StripParams::new(StripKind::Channel);
        assert_eq!(ch.fader_db.load(), livedaw_dsp::fader::FADER_MIN);
        assert!(!ch.eq().on.load(Relaxed));
        assert!(ch.cust_link.load(Relaxed));
        assert_eq!(ch.colour.load(Relaxed), DEFAULT_COLOUR);
        assert_eq!(ch.icon.load(Relaxed), 0);
        assert_eq!(ch.input.delay_mode.load(Relaxed), 0);
        assert_eq!(ch.input.delay.load(), DELAY_MIN_M);
        assert!(!ch.input.delay_on.load(Relaxed));
        // The other types too (aux linked to its source like a channel).
        for kind in [StripKind::Aux, StripKind::Bus, StripKind::Main, StripKind::Matrix] {
            let p = StripParams::new(kind);
            assert_eq!(p.fader_db.load(), livedaw_dsp::fader::FADER_MIN, "{kind:?}");
            assert!(!p.eq().on.load(Relaxed), "{kind:?}");
            assert_eq!(p.cust_link.load(Relaxed), kind == StripKind::Aux, "{kind:?}");
        }
        // Buses 9-16 at 0 dB into Main 1, coloured by pairs of four.
        let (b1, b9, b11) = (
            StripParams::new_in_slot(StripKind::Bus, 0),
            StripParams::new_in_slot(StripKind::Bus, 8),
            StripParams::new_in_slot(StripKind::Bus, 10),
        );
        assert_eq!((b1.colour.load(Relaxed), b9.colour.load(Relaxed), b11.colour.load(Relaxed)), (10, 6, 12));
        let main1 = |kind, slot| MainSendParams::factory(Profile::Wing.spec(), kind, slot, 0).on.load(Relaxed);
        assert!(!main1(StripKind::Bus, 0) && main1(StripKind::Bus, 8) && main1(StripKind::Bus, 15));
        assert!(!main1(StripKind::Bus, 16) && main1(StripKind::Channel, 0) && main1(StripKind::Aux, 3));
        assert!(!MainSendParams::factory(Profile::Wing.spec(), StripKind::Channel, 0, 1).on.load(Relaxed), "only Main 1");
        assert_eq!(b9.fader_db.load(), 0.0);
        // TAP "5"; matrix sends off at -oo, not linked, PRE from channels and
        // post-fader from buses; a matrix's direct input at -oo.
        assert_eq!(ch.tap.load(Relaxed), DEFAULT_TAP);
        let m = &MatrixSendParams::factory(Profile::Wing.spec(), StripKind::Channel, 0, 0);
        assert!(m.pre.load(Relaxed) && !m.pan_link.load(Relaxed) && !m.on.load(Relaxed));
        assert_eq!(m.level_db.load(), livedaw_dsp::fader::FADER_MIN);
        assert!(!MatrixSendParams::factory(Profile::Wing.spec(), StripKind::Bus, 0, 0).pre.load(Relaxed));
        assert_eq!(StripParams::new(StripKind::Matrix).direct_level_db.load(), livedaw_dsp::fader::FADER_MIN);
        // AUX 2 plays the USB player's PLAY 1/2 in stereo; AUX 1 has no input.
        let (aux1, aux2) = (StripParams::new_in_slot(StripKind::Aux, 0), StripParams::new_in_slot(StripKind::Aux, 1));
        assert_eq!(aux1.input_left.load(Relaxed), -1);
        assert!(aux2.stereo.load(Relaxed));
        assert_eq!(
            (aux2.input_left.load(Relaxed), aux2.input_right.load(Relaxed)),
            (livedaw_recorder::USB_PLAYER_SOURCE_BASE, livedaw_recorder::USB_PLAYER_SOURCE_BASE + 1)
        );
        let c = EngineControls::default();
        assert!(c.mute_override.load(Relaxed) && c.input_select_override.load(Relaxed));
        let d = DcaParams::default();
        assert_eq!(
            (d.colour.load(Relaxed), d.led.load(Relaxed), d.fader_db.load()),
            (DEFAULT_COLOUR, false, livedaw_dsp::fader::FADER_MIN)
        );
    }
}

#[cfg(test)]
mod factory_tests {
    use super::*;

    #[test]
    fn dynamics_slots_start_as_a_factory_set_console() {
        // Channels: COMP in the dynamics slot; aux: the WING GATE at -36 dB.
        let ch = StripParams::new(StripKind::Channel);
        assert_eq!(DYN_MODELS[ch.dynamics().model_index()].token, "COMP");
        let aux = StripParams::new(StripKind::Aux);
        assert_eq!(DYN_MODELS[aux.dynamics().model_index()].token, "GATE");
        assert_eq!(aux.dynamics().param("thr").unwrap().load(), AUX_GATE_THRESHOLD_DB);
        // Key filters 1k0 Hz / Q 2.00, the crossover 1k0 Hz / Q 0.40 / DEPTH 6 dB.
        let s = ch.gate();
        assert_eq!((s.sc_freq.load(), s.sc_q.load()), (1002.374, 1.995882));
        let s = ch.dynamics();
        assert_eq!((s.xo_freq.load(), s.xo_q.load(), s.xo_depth.load()), (1002.374, 0.4, 6.0));
        assert_eq!(ch.tap_eq.bands[2].q.load(), 0.9979702);
    }
}

/// The TAP point input channels start with (an index into `TAP_POINTS`):
/// "5", after the third block of the process order, as on a factory-set WING.
pub const DEFAULT_TAP: i32 = 4;

/// The aux channel (0-based) a factory-set WING patches to the USB player's
/// first pair (AUX 2, PLAY 1/2).
pub const FACTORY_PLAYER_AUX: usize = 1;

/// Default WING colour per kind (buses: see `StripParams::new_in_slot`).
pub fn default_colour(kind: StripKind) -> i32 {
    match kind {
        StripKind::Bus => 10,
        _ => DEFAULT_COLOUR,
    }
}

impl StripParams {
    /// The strip's own filter block (input channels).
    #[inline]
    pub fn filter(&self) -> &FilterParams {
        // SAFETY: `OwnBlocks` points into the blocks it keeps.
        unsafe { &*self.blocks.filter }
    }

    /// The GATE slot (input channels).
    #[inline]
    pub fn gate(&self) -> &DynSlotParams {
        // SAFETY: as `filter`.
        unsafe { &*self.blocks.gate }
    }

    /// The strip's own EQ.
    #[inline]
    pub fn eq(&self) -> &EqBlockParams {
        // SAFETY: as `filter`.
        unsafe { &*self.blocks.eq }
    }

    /// The DYNAMICS slot.
    #[inline]
    pub fn dynamics(&self) -> &DynSlotParams {
        // SAFETY: as `filter`.
        unsafe { &*self.blocks.dynamics }
    }

    /// INSERT 1 (before the fader) and INSERT 2 (after it).
    #[inline]
    pub fn pre_insert(&self) -> &InsertParams {
        // SAFETY: as `filter`.
        unsafe { &*self.blocks.pre_insert }
    }

    #[inline]
    pub fn post_insert(&self) -> &InsertParams {
        // SAFETY: as `filter`.
        unsafe { &*self.blocks.post_insert }
    }

    /// Own block `k` ([`OwnBlocks::FILTER`]...), with its DSP state.
    #[inline]
    pub fn own_block(&self, k: usize) -> &Block {
        &self.blocks.blocks[k]
    }

    /// The own blocks, in [`OwnBlocks`] order.
    pub fn own_blocks(&self) -> impl Iterator<Item = &Block> {
        self.blocks.blocks.iter().map(|b| &**b)
    }

    /// Where the pointers to the blocks sit in the struct, for the C ABI's
    /// layout check (`ld_layout_info`).
    pub const OFFSET_FILTER: usize = std::mem::offset_of!(StripParams, blocks) + std::mem::offset_of!(OwnBlocks, filter);
    pub const OFFSET_GATE: usize = std::mem::offset_of!(StripParams, blocks) + std::mem::offset_of!(OwnBlocks, gate);
    pub const OFFSET_EQ: usize = std::mem::offset_of!(StripParams, blocks) + std::mem::offset_of!(OwnBlocks, eq);
    pub const OFFSET_DYNAMICS: usize = std::mem::offset_of!(StripParams, blocks) + std::mem::offset_of!(OwnBlocks, dynamics);
    pub const OFFSET_POST_INSERT: usize =
        std::mem::offset_of!(StripParams, blocks) + std::mem::offset_of!(OwnBlocks, post_insert);

    pub fn new(kind: StripKind) -> Self {
        Self::new_with(kind, &livedaw_dsp::wing::MODELS)
    }

    /// A new strip of `kind` whose blocks hold `models` (its console's): the
    /// GATE slot on the table's gate, the DYNAMICS slot on its compressor
    /// (aux: its gate; the WING's GATE and COMP).
    pub fn new_with(kind: StripKind, models: &'static ModelTables) -> Self {
        let output = kind.is_output();
        let p = Self {
            input: InputParams::default(),
            blocks: OwnBlocks::new(
                DynSlotParams::new_in(models.dynamics, models.gate_default),
                EqBlockParams::new_in(if output { models.eq_bus } else { models.eq_channel }),
                DynSlotParams::new_in(
                    models.dynamics,
                    if kind == StripKind::Aux { models.gate_default } else { models.comp_default },
                ),
            ),
            process_order: AtomicI32::new(0),
            tap: AtomicI32::new(DEFAULT_TAP),
            fader_db: AtomicF32::new(0.0),
            pan: AtomicF32::new(0.0),
            width: AtomicF32::new(100.0),
            mute: AtomicBool::new(false),
            solo: AtomicBool::new(false),
            solo_safe: AtomicBool::new(false),
            monitor_bus: AtomicI32::new(0),
            stereo: AtomicBool::new(output),
            mono: AtomicBool::new(false),
            input_left: AtomicI32::new(-1),
            input_right: AtomicI32::new(-1),
            output_left: AtomicI32::new(-1),
            output_right: AtomicI32::new(-1),
            dca_mask: AtomicGroupSet::default(),
            mute_group_mask: AtomicGroupSet::default(),
            direct_on: AtomicBool::new(false),
            direct_invert: AtomicBool::new(false),
            direct_source: AtomicI32::new(crate::routing::matrix_direct::OFF),
            direct_level_db: AtomicF32::new(livedaw_dsp::fader::FADER_MIN),
            colour: AtomicI32::new(default_colour(kind)),
            icon: AtomicI32::new(0),
            led: AtomicBool::new(true),
            recall_safe: AtomicBool::new(false),
            alt_input_left: AtomicI32::new(-1),
            alt_input_right: AtomicI32::new(-1),
            alt_selected: AtomicBool::new(false),
            src_auto: AtomicBool::new(false),
            tap_width: AtomicF32::new(100.0),
            tap_eq: TapEqParams::default(),
            cust_link: AtomicBool::new(kind.is_input()),
            ms_side: AtomicBool::new(false),
        };
        // The WING's factory strips: the fader down, the EQ off.
        p.fader_db.store(livedaw_dsp::fader::FADER_MIN);
        p.eq().on.store(false, Relaxed);
        if kind == StripKind::Aux {
            if let Some(thr) = p.dynamics().param("thr") {
                thr.store(AUX_GATE_THRESHOLD_DB);
            }
        }
        p
    }

    /// A new strip in slot `index` of its kind, as on a factory-initialized
    /// WING: buses 1-8 (aux mixes) Salmon (colour 10) with the fader down;
    /// buses 9-10 Lime (6) and 11-16 Magenta (12), at 0 dB into Main 1 (their
    /// main send: `MainSendParams::factory`);
    /// AUX 2 on the USB player's PLAY 1/2 (the "2TR" pair), in stereo.
    pub fn new_in_slot(kind: StripKind, index: usize) -> Self {
        Self::new_in_slot_of(kind, index, Profile::Wing.spec())
    }

    /// [`Self::new_in_slot`] on a console of `spec` (its models in the
    /// blocks; its `buses_to_main` are the subgroups at 0 dB; its mono mains).
    pub fn new_in_slot_of(kind: StripKind, index: usize, spec: &ProfileSpec) -> Self {
        let p = Self::new_with(kind, spec.run_models());
        if kind == StripKind::Main && spec.mono_mains.contains(&index) {
            p.stereo.store(false, Relaxed);
        }
        if !spec.source_link {
            p.cust_link.store(false, Relaxed);
        }
        p.tap.store(spec.tap_point, Relaxed);
        if kind == StripKind::Bus && spec.buses_to_main.contains(&index) {
            p.colour.store(if index < 10 { 6 } else { 12 }, Relaxed);
            p.fader_db.store(0.0);
        }
        if kind == StripKind::Aux && index == FACTORY_PLAYER_AUX {
            p.stereo.store(true, Relaxed);
            p.input_left.store(livedaw_recorder::USB_PLAYER_SOURCE_BASE, Relaxed);
            p.input_right.store(livedaw_recorder::USB_PLAYER_SOURCE_BASE + 1, Relaxed);
        }
        // Last: a console's own factory has the final word (an X32's aux in 2
        // isn't the WING's USB player).
        if let Some(factory) = spec.factory {
            factory(spec, &p, kind, index);
        }
        p
    }
}

/// Meters written by the audio thread. Peaks are "max since last read" (the UI
/// exchanges them with 0); dynamics meters hold the latest block's value.
#[repr(C)]
#[derive(Debug, Default)]
pub struct StripMeters {
    pub input_peak: [AtomicF32; 2],
    pub output_peak: [AtomicF32; 2],
    pub gate_reduction_db: AtomicF32,
    pub dyn_reduction_db: AtomicF32,
    /// Gate closed (the console's GATE LED).
    pub gate_active: AtomicBool,
    /// Dynamics working (DYN LED).
    pub dyn_active: AtomicBool,
    pub current_width: AtomicI32,
    /// Blocks that produced NaN/inf and were silenced.
    pub dsp_faults: AtomicI32,
    /// Automix gain applied in the last block, dB.
    pub automix_db: AtomicF32,
    /// After trim, balance and polarity (the INPUT screen's "post TRIM" meter).
    pub trim_peak: [AtomicF32; 2],
    /// Pre-fader, at the PFL point (the METERS screen).
    pub pfl_peak: [AtomicF32; 2],
    /// ABI 9: the GATE / DYNAMICS slot's detector level in the latest block,
    /// dB on its static curve's input scale (the transfer graph's dot; -120
    /// when off).
    pub gate_key_db: AtomicF32,
    pub dyn_key_db: AtomicF32,
    /// ABI 9: the slots' output peaks (max since the last read; linear).
    pub gate_out_peak: AtomicF32,
    pub dyn_out_peak: AtomicF32,
}

/// A DCA group.
#[repr(C)]
#[derive(Debug)]
pub struct DcaParams {
    pub fader_db: AtomicF32,
    pub mute: AtomicBool,
    pub solo: AtomicBool,
    /// Scribble strip light.
    pub led: AtomicBool,
    pub colour: AtomicI32,
    /// WING icon number on the scribble strip (0 = none).
    pub icon: AtomicI32,
    /// ABI 13: SOLO BUS (`/dca/n/mon`), an index into [`MONITOR_BUSES`]
    /// (A = PHONES, B = SPEAKERS, A+B): where the members' solo goes in LIVE.
    pub mon: AtomicI32,
}

impl Default for DcaParams {
    fn default() -> Self {
        // The WING's factory DCA: fader down, scribble strip light off.
        Self {
            fader_db: AtomicF32::new(livedaw_dsp::fader::FADER_MIN),
            mute: AtomicBool::new(false),
            solo: AtomicBool::new(false),
            led: AtomicBool::new(false),
            colour: AtomicI32::new(DEFAULT_COLOUR),
            icon: AtomicI32::new(0),
            mon: AtomicI32::new(0),
        }
    }
}

/// One of the 16 FX slots (the console's EFFECTS screen).
#[repr(C)]
#[derive(Debug)]
pub struct FxParams {
    pub bypass: AtomicBool,
    /// Wet/dry in %.
    pub mix: AtomicF32,
    /// Index into `FX_MODELS`: `FX_NONE`, `FX_EXT` (a plugin) or a built-in device.
    pub model: AtomicI32,
    /// The device's parameters, in its table order.
    pub values: [AtomicF32; MAX_FX_PARAMS],
    /// ABI 21: the hardware outputs the slot's FX SEND feeds (WING FX SEND
    /// 2i+1 / 2i+2, ROUTING > OUTPUTS; -1 = none), as a strip's outputs. The
    /// EXT device's outboard insert sends there (manual 11.11 p. 164, SEND).
    pub send_left: AtomicI32,
    pub send_right: AtomicI32,
    /// A send effect's sources (an X32's FX 1-4 SOURCE, MIX 1-16 or M/C):
    /// the strip signals (`routing::strip_source_input`) it processes, one
    /// block late, into the FX return group (`sources::FX_RETURN_SOURCE_BASE`);
    /// -1 = an insert (INS), as on the WING. A right of -1 takes the left.
    pub source_left: AtomicI32,
    pub source_right: AtomicI32,
}

impl Default for FxParams {
    fn default() -> Self {
        Self {
            bypass: AtomicBool::new(false),
            mix: AtomicF32::new(100.0),
            model: AtomicI32::new(FX_NONE as i32),
            values: std::array::from_fn(|_| AtomicF32::new(0.0)),
            send_left: AtomicI32::new(-1),
            send_right: AtomicI32::new(-1),
            source_left: AtomicI32::new(-1),
            source_right: AtomicI32::new(-1),
        }
    }
}

impl FxParams {
    /// A send effect: fed from its sources, not inserted.
    pub fn is_send(&self) -> bool {
        self.source_left.load(Relaxed) >= 0
    }

    /// The model, in the WING's FX table (`model_index_in` for a console's).
    pub fn model_index(&self) -> usize {
        self.model_index_in(FX_MODELS)
    }

    /// The model if it is one of the WING's devices (a Universal console's
    /// slot may hold another console's, past the WING's table), else None.
    pub fn wing_model(&self) -> Option<usize> {
        usize::try_from(self.model.load(Relaxed)).ok().filter(|&m| m < FX_MODELS.len())
    }

    /// The model, in a console's FX table (`Engine::fx_models`).
    pub fn model_index_in(&self, table: &[ModelSpec]) -> usize {
        (self.model.load(Relaxed).max(0) as usize).min(table.len().max(1) - 1)
    }

    /// Selects a WING model (`set_model_in` for a console's).
    pub fn set_model(&self, model: usize, defaults: bool) {
        self.set_model_in(FX_MODELS, model, defaults);
    }

    /// Selects model `model` of a console's FX table; with `defaults`, loads the
    /// device's default settings. (The engine creates the device itself: use
    /// `Engine::set_fx_model`.)
    pub fn set_model_in(&self, table: &[ModelSpec], model: usize, defaults: bool) {
        let Some(spec) = table.get(model) else { return };
        if defaults {
            let mut d = [0.0; MAX_FX_PARAMS];
            spec.defaults_into(&mut d);
            for (v, x) in self.values.iter().zip(d) {
                v.store(x);
            }
        }
        self.model.store(model as i32, Relaxed);
    }

    /// A parameter of the current WING device by its console id ("dcy").
    pub fn param(&self, id: &str) -> Option<&AtomicF32> {
        self.param_in(FX_MODELS, id)
    }

    /// A parameter of the current device of a console's FX table.
    pub fn param_in(&self, table: &[ModelSpec], id: &str) -> Option<&AtomicF32> {
        self.values.get(table[self.model_index_in(table)].param_index(id)?)
    }

    /// The EXT device with SOURCE OUTBOARD (`ext::SOURCE`): without a plugin in
    /// the slot, the console's outboard insert.
    pub fn ext_outboard(&self) -> bool {
        self.model_index() == FX_EXT && self.values[ext::SOURCE].load() >= 0.5
    }

    pub fn values_snapshot(&self, out: &mut [f32; MAX_FX_PARAMS]) {
        for (o, v) in out.iter_mut().zip(&self.values) {
            *o = v.load();
        }
    }
}

/// MAIN LINK choices (SETUP > PREFERENCES): Main 1's fader also moves Main 2
/// (1>2), Mains 2-3 (1>2-3) or Mains 2-4 (1>2-4), like a DCA.
pub const MAIN_LINK_MODES: [&str; 4] = ["OFF", "1 > 2", "1 > 2-3", "1 > 2-4"];

/// Default monitor DIM amount in dB.
pub const DEFAULT_DIM_DB: f32 = 20.0;
/// Default PFL DIM of both monitor paths, dB.
pub const DEFAULT_PFL_DIM_DB: f32 = 12.0;

/// A monitor path's SOURCE choices (`MonitorSourceParams::source`): what the
/// path plays without a solo: MAIN.1-4, MTX.1-8, BUS.1-16, AUX.1-8.
pub const NUM_MONITOR_SOURCES: usize = WING_MAINS + WING_MATRICES + WING_BUSES + WING_AUX;

/// Strips past the WING's counts (a Universal console's BUS 17, MAIN 5) as
/// monitor SOURCE and DIRECT IN choices: `EXTENDED_SOURCE_BASE + kind * 0x1000 +
/// number` (0-based; a bus by its slot). The WING's own choices keep their
/// numbers (`monitor_source`, `monitor::direct_source`), so WING shows and the
/// WING remote see the same values; shows store either as a number.
pub const EXTENDED_SOURCE_BASE: i32 = 0x10000;
const EXTENDED_KIND_STRIDE: i32 = 0x1000;

/// The extended choice of a strip (any kind and number up to 4095).
pub fn extended_source(kind: StripKind, number: usize) -> i32 {
    EXTENDED_SOURCE_BASE + kind as i32 * EXTENDED_KIND_STRIDE + number.min(EXTENDED_KIND_STRIDE as usize - 1) as i32
}

/// The strip of an extended choice (None for a WING choice or OFF).
pub fn decode_extended_source(index: i32) -> Option<(StripKind, usize)> {
    let rest = index.checked_sub(EXTENDED_SOURCE_BASE).filter(|r| *r >= 0)?;
    let kind = StripKind::from_i32(rest / EXTENDED_KIND_STRIDE)?;
    Some((kind, (rest % EXTENDED_KIND_STRIDE) as usize))
}

/// The monitor SOURCE index of a strip: the WING's number while it has one,
/// else its extended choice; -1 for a channel (not a SOURCE).
pub fn monitor_source_index(kind: StripKind, number: usize) -> i32 {
    let mut base = 0;
    for (k, count) in [
        (StripKind::Main, WING_MAINS),
        (StripKind::Matrix, WING_MATRICES),
        (StripKind::Bus, WING_BUSES),
        (StripKind::Aux, WING_AUX),
    ] {
        if k == kind {
            return if number < count { (base + number) as i32 } else { extended_source(kind, number) };
        }
        base += count;
    }
    -1
}

/// The strip a monitor SOURCE index plays: (kind, 0-based number; buses by slot).
pub fn monitor_source(index: i32) -> Option<(StripKind, usize)> {
    if let Some((kind, n)) = decode_extended_source(index) {
        return (kind != StripKind::Channel).then_some((kind, n));
    }
    let mut i = usize::try_from(index).ok()?;
    for (kind, count) in [
        (StripKind::Main, WING_MAINS),
        (StripKind::Matrix, WING_MATRICES),
        (StripKind::Bus, WING_BUSES),
        (StripKind::Aux, WING_AUX),
    ] {
        if i < count {
            return Some((kind, i));
        }
        i -= count;
    }
    None
}

/// The display name of a monitor SOURCE index ("MAIN.1", "BUS.12").
pub fn monitor_source_name(index: i32) -> String {
    match monitor_source(index) {
        Some((StripKind::Matrix, i)) => format!("MTX.{}", i + 1),
        Some((kind, i)) => format!("{}.{}", kind.label(), i + 1),
        None => "OFF".into(),
    }
}

/// SRC MIX default: the source is silent while soloing (-oo).
pub const SOURCE_MIX_OFF_DB: f32 = -144.0;

/// What a monitor path plays besides the solo bus (MONITORS: SOURCE, SRC
/// LEVEL, SRC MIX, PFL DIM).
#[repr(C)]
#[derive(Debug)]
pub struct MonitorSourceParams {
    /// Index of the SOURCE (see [`monitor_source`]); default MAIN.1.
    pub source: AtomicI32,
    /// SRC LEVEL: the source's level in dB.
    pub source_level_db: AtomicF32,
    /// SRC MIX: the source's level while a solo reaches the path (-144 = off).
    pub source_mix_db: AtomicF32,
    /// PFL DIM: attenuation of pre-fader solos in dB (>= 0).
    pub pfl_dim_db: AtomicF32,
}

impl Default for MonitorSourceParams {
    fn default() -> Self {
        Self {
            source: AtomicI32::new(0),
            source_level_db: AtomicF32::new(0.0),
            source_mix_db: AtomicF32::new(SOURCE_MIX_OFF_DB),
            // The WING after INITIALIZE: `/cfg/mon/n/pfldim` 12.
            pfl_dim_db: AtomicF32::new(DEFAULT_PFL_DIM_DB),
        }
    }
}

impl MonitorSourceParams {
    fn reset(&self) {
        let d = Self::default();
        self.source.store(d.source.load(Relaxed), Relaxed);
        self.source_level_db.store(d.source_level_db.load());
        self.source_mix_db.store(d.source_mix_db.load());
        self.pfl_dim_db.store(d.pfl_dim_db.load());
    }
}

/// The PHONES monitor path: its source, level, DIM amount, outputs and meter.
/// The SPEAKERS path is the console's monitor (`monitor_level_db`, ...).
#[repr(C)]
#[derive(Debug)]
pub struct MonitorPathParams {
    pub source: MonitorSourceParams,
    pub level_db: AtomicF32,
    /// DIM amount in dB (an attenuation, >= 0).
    pub dim_db: AtomicF32,
    /// Hardware outputs (-1 = none).
    pub output_left: AtomicI32,
    pub output_right: AtomicI32,
    pub peak: [AtomicF32; 2],
}

impl Default for MonitorPathParams {
    fn default() -> Self {
        Self {
            source: MonitorSourceParams::default(),
            level_db: AtomicF32::new(0.0),
            dim_db: AtomicF32::new(DEFAULT_DIM_DB),
            output_left: AtomicI32::new(-1),
            output_right: AtomicI32::new(-1),
            peak: Default::default(),
        }
    }
}

impl MonitorPathParams {
    fn reset(&self) {
        self.source.reset();
        self.level_db.store(0.0);
        self.dim_db.store(DEFAULT_DIM_DB);
        self.output_left.store(-1, Relaxed);
        self.output_right.store(-1, Relaxed);
    }
}

/// Engine-wide controls and status, shared with the UI.
#[repr(C)]
#[derive(Debug)]
pub struct EngineControls {
    /// CHANNEL SOLO (input and aux channels): AFL instead of PFL.
    pub solo_afl: AtomicBool,
    pub any_solo_active: AtomicBool,
    pub output_protection: AtomicBool,
    pub monitor_level_db: AtomicF32,
    pub monitor_output_left: AtomicI32,
    pub monitor_output_right: AtomicI32,
    pub output_ceiling_db: AtomicF32,
    /// Blocks in which the output safety clip engaged.
    pub output_clip_events: AtomicI32,
    pub monitor_peak: [AtomicF32; 2],
    /// Smoothed callback time relative to the block duration.
    pub dsp_load: AtomicF32,
    /// Engine-level faults (a caught panic silences the block).
    pub engine_faults: AtomicI32,
    /// The engaged mute groups.
    pub mute_groups: AtomicGroupSet,
    /// Every DCA the engine holds; the console uses the first `Engine::num_dcas`.
    pub dcas: [DcaParams; MAX_DCAS],
    pub fx: [FxParams; MAX_FX],

    /// BUS / MAIN / MATRIX SOLO: AFL (the default) instead of PFL.
    pub bus_solo_afl: AtomicBool,
    pub main_solo_afl: AtomicBool,
    pub matrix_solo_afl: AtomicBool,
    /// Monitor keys: DIM attenuates by `monitor_dim_db`, MUTE (hold DIM)
    /// silences, MONO sums L and R, SWAP (hold MONO) swaps them.
    pub monitor_dim: AtomicBool,
    pub monitor_mute: AtomicBool,
    pub monitor_mono: AtomicBool,
    pub monitor_swap: AtomicBool,
    /// DCA MUTEGROUPS: a muted DCA also mutes its strips.
    pub dca_mute_groups: AtomicBool,
    /// STARTUP MAIN MUTE: the application mutes the mains when it starts.
    pub startup_main_mute: AtomicBool,
    /// Automix X ENABLE / Y ENABLE.
    pub automix_enable: [AtomicBool; 2],
    /// DIM amount in dB (an attenuation, >= 0).
    pub monitor_dim_db: AtomicF32,
    /// Index into [`MAIN_LINK_MODES`].
    pub main_link: AtomicI32,
    /// GLOBAL INPUT SELECT OVERRIDE (SETUP > AUDIO > PREFERENCES, WING
    /// `/io/autoaltovr`): the MAIN / ALT switch of a strip in GLOBAL INPUT
    /// SELECT ENABLE (`StripParams::src_auto`) can still be used by hand.
    pub input_select_override: AtomicBool,
    /// The global MAIN / ALT switch (true = ALT, WING `/io/altsw`): set it with
    /// `Engine::set_global_alt`, which switches the strips in GLOBAL INPUT
    /// SELECT ENABLE.
    pub global_alt: AtomicBool,

    // ---- console level, ABI 7: solo modes, talkback, monitor paths -----------
    /// SOLO MODE: `solo::solo_mode` (LIVE, STUDIO, SIP).
    pub solo_mode: AtomicI32,
    /// The solo bus / selected monitoring path (MONITORS: PHONES, SPEAKERS,
    /// PH+SPK) as a `MONITOR_BUSES` choice (A = PHONES, B = SPEAKERS, A+B).
    /// STUDIO and SIP solo to it; DIM, MUTE, MONO, SWAP and the talk MON DIM
    /// act on it.
    pub solo_bus: AtomicI32,
    /// LIVE: a path picked on MONITORS while soloing overrides the strips'
    /// own choice until no solo is left (the engine then clears it).
    pub solo_bus_override: AtomicBool,
    /// MUTEGROUP/SIP OVERRIDE (SETUP > PREFERENCES).
    pub mute_override: AtomicBool,
    /// Status: SIP is muting the channels that aren't soloed (the SIP badge).
    pub sip_active: AtomicBool,
    /// Talkback (`talk.rs`).
    pub talk: TalkParams,
    /// The SPEAKERS path: SOURCE, SRC LEVEL, SRC MIX, PFL DIM (its
    /// level, DIM amount, outputs and meter are the `monitor_*` fields).
    pub speakers: MonitorSourceParams,
    /// The PHONES path.
    pub phones: MonitorPathParams,

    // ---- ABI 8: monitor path processing and SOURCE SOLO (`monitor.rs`) --------
    /// Both paths' DIRECT IN, BAND SOLO, EQ, PAN / WIDTH, INVERT, DELAY and
    /// LIMITER, and SOURCE SOLO.
    pub monitor: MonitorControls,
}

impl Default for EngineControls {
    fn default() -> Self {
        Self {
            solo_afl: AtomicBool::new(false),
            any_solo_active: AtomicBool::new(false),
            output_protection: AtomicBool::new(true),
            monitor_level_db: AtomicF32::new(0.0),
            monitor_output_left: AtomicI32::new(-1),
            monitor_output_right: AtomicI32::new(-1),
            output_ceiling_db: AtomicF32::new(0.0),
            output_clip_events: AtomicI32::new(0),
            monitor_peak: Default::default(),
            dsp_load: AtomicF32::new(0.0),
            engine_faults: AtomicI32::new(0),
            mute_groups: AtomicGroupSet::default(),
            dcas: std::array::from_fn(|_| DcaParams::default()),
            fx: std::array::from_fn(|_| FxParams::default()),
            // A WING after INITIALIZE: buses AFL, mains and matrices PFL.
            bus_solo_afl: AtomicBool::new(true),
            main_solo_afl: AtomicBool::new(false),
            matrix_solo_afl: AtomicBool::new(false),
            monitor_dim: AtomicBool::new(false),
            monitor_mute: AtomicBool::new(false),
            monitor_mono: AtomicBool::new(false),
            monitor_swap: AtomicBool::new(false),
            dca_mute_groups: AtomicBool::new(true),
            startup_main_mute: AtomicBool::new(false),
            automix_enable: [AtomicBool::new(true), AtomicBool::new(true)],
            monitor_dim_db: AtomicF32::new(DEFAULT_DIM_DB),
            main_link: AtomicI32::new(0),
            // A WING after INITIALIZE: `/io/autoaltovr` and `/$ctl/cfg/muteovr` on.
            input_select_override: AtomicBool::new(true),
            global_alt: AtomicBool::new(false),
            solo_mode: AtomicI32::new(solo_mode::LIVE),
            solo_bus: AtomicI32::new(0),
            solo_bus_override: AtomicBool::new(false),
            mute_override: AtomicBool::new(true),
            sip_active: AtomicBool::new(false),
            talk: TalkParams::default(),
            speakers: MonitorSourceParams::default(),
            phones: MonitorPathParams::default(),
            monitor: MonitorControls::default(),
        }
    }
}

impl EngineControls {
    /// Solo on a strip of `kind` listens after the fader (AFL), else before it (PFL).
    pub fn solo_is_afl(&self, kind: StripKind) -> bool {
        match kind {
            StripKind::Channel | StripKind::Aux => &self.solo_afl,
            StripKind::Bus => &self.bus_solo_afl,
            StripKind::Main => &self.main_solo_afl,
            StripKind::Matrix => &self.matrix_solo_afl,
        }
        .load(Relaxed)
    }

    /// Main 2-4 linked to Main 1's fader (MAIN LINK): the highest linked main index.
    pub fn main_link_last(&self) -> usize {
        self.main_link.load(Relaxed).clamp(0, MAIN_LINK_MODES.len() as i32 - 1) as usize
    }

    /// Resets the console-wide settings (not the status counters).
    pub fn reset(&self) {
        self.solo_afl.store(false, Relaxed);
        self.bus_solo_afl.store(true, Relaxed);
        self.main_solo_afl.store(false, Relaxed);
        self.matrix_solo_afl.store(false, Relaxed);
        for key in [&self.monitor_dim, &self.monitor_mute, &self.monitor_mono, &self.monitor_swap] {
            key.store(false, Relaxed);
        }
        self.monitor_dim_db.store(DEFAULT_DIM_DB);
        self.dca_mute_groups.store(true, Relaxed);
        self.startup_main_mute.store(false, Relaxed);
        for enable in &self.automix_enable {
            enable.store(true, Relaxed);
        }
        self.main_link.store(0, Relaxed);
        self.input_select_override.store(true, Relaxed);
        self.global_alt.store(false, Relaxed);
        self.solo_mode.store(solo_mode::LIVE, Relaxed);
        self.solo_bus.store(0, Relaxed);
        self.solo_bus_override.store(false, Relaxed);
        self.mute_override.store(true, Relaxed);
        self.talk.reset();
        self.speakers.reset();
        self.phones.reset();
        self.monitor.reset();
        self.monitor_level_db.store(0.0);
        self.mute_groups.clear();
        for d in &self.dcas {
            d.fader_db.store(livedaw_dsp::fader::FADER_MIN);
            d.mute.store(false, Relaxed);
            d.solo.store(false, Relaxed);
            d.led.store(false, Relaxed);
            d.colour.store(DEFAULT_COLOUR, Relaxed);
            d.icon.store(0, Relaxed);
            d.mon.store(0, Relaxed);
        }
        for f in &self.fx {
            f.bypass.store(false, Relaxed);
            f.mix.store(100.0);
            f.set_model(FX_NONE, true);
            f.send_left.store(-1, Relaxed);
            f.send_right.store(-1, Relaxed);
            f.source_left.store(-1, Relaxed);
            f.source_right.store(-1, Relaxed);
        }
    }
}

#[cfg(test)]
mod monitor_source_tests {
    use super::*;

    #[test]
    fn monitor_sources_past_the_wings_counts() {
        // The WING's keep their numbers; past them, extended choices.
        assert_eq!(monitor_source_index(StripKind::Main, 0), 0);
        assert_eq!(monitor_source_index(StripKind::Bus, 15), (WING_MAINS + WING_MATRICES + 15) as i32);
        assert_eq!(monitor_source_index(StripKind::Channel, 0), -1, "a channel isn't a SOURCE");
        for (kind, n) in
            [(StripKind::Bus, 16), (StripKind::Bus, 255), (StripKind::Main, 4), (StripKind::Matrix, 8), (StripKind::Aux, 8)]
        {
            let i = monitor_source_index(kind, n);
            assert!(i >= EXTENDED_SOURCE_BASE);
            assert_eq!(monitor_source(i), Some((kind, n)));
        }
        assert_eq!(monitor_source_name(monitor_source_index(StripKind::Bus, 23)), "BUS.24");
        assert_eq!(monitor_source(extended_source(StripKind::Channel, 3)), None);
        assert_eq!(decode_extended_source(-1), None);
        assert_eq!(decode_extended_source(5), None);
    }
}
