//! Extra processing blocks in a strip's chain (the Universal core, Phase 1
//! step 3b): any number of filters, EQs, dynamics, inserts and delays,
//! anywhere among the nodes of the strip's chain (`chain.rs`).
//!
//! A strip without extra blocks runs its WING template. Adding, moving or
//! removing a block builds a new [`ChainLayout`] on the message thread and
//! publishes it on the strip (`Strip::set_layout`); the old one goes to the
//! garbage list. The blocks are shared (`Arc`) between layouts, so a block
//! keeps its parameters and DSP state when the chain around it changes.

use crate::atomic::AtomicF32;
use crate::chain::Node;
use crate::fx::FxRack;
use crate::params::{DynSlotParams, EqBlockParams, FilterParams, InsertParams, DELAY_MIN_M};
use livedaw_dsp::delay::DelayLine;
use livedaw_dsp::model::ModelTables;
use livedaw_dsp::wing::dynamics::WingDynamics;
use livedaw_dsp::wing::eq::WingEq;
use livedaw_dsp::wing::filter::WingFilter;
use livedaw_dsp::wing::{MAX_DYN_PARAMS, MAX_EQ_PARAMS};
use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicU32, Ordering::Relaxed};
use std::sync::Arc;

/// The most extra blocks a strip's chain holds.
pub const MAX_EXTRA_BLOCKS: usize = 32;

/// The kinds of extra block.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum BlockKind {
    Filter,
    Eq,
    Dynamics,
    Insert,
    Delay,
    /// A VST3/AU/LV2 plug-in (or a chain of them) hosted by the app, outside the FX slots.
    Plugin,
}

impl BlockKind {
    pub const ALL: [BlockKind; 6] = [Self::Filter, Self::Eq, Self::Dynamics, Self::Insert, Self::Delay, Self::Plugin];

    /// Name in show files ("filter", "eq", "dyn", "ins", "delay", "plug").
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Filter => "filter",
            Self::Eq => "eq",
            Self::Dynamics => "dyn",
            Self::Insert => "ins",
            Self::Delay => "delay",
            Self::Plugin => "plug",
        }
    }

    pub fn parse(s: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|k| k.as_str() == s)
    }

    /// The kind's number in the C ABI: its place in [`Self::ALL`].
    pub fn index(self) -> usize {
        self as usize
    }

    pub fn from_index(i: usize) -> Option<Self> {
        Self::ALL.get(i).copied()
    }
}

/// A delay block's settings (as the strip's own delay: `InputParams`).
#[repr(C)]
#[derive(Debug)]
pub struct DelayParams {
    pub on: AtomicBool,
    /// Index into `livedaw_dsp::delay::DELAY_MODES` (M, FT, MS, SMP).
    pub mode: AtomicI32,
    /// In `mode` units.
    pub delay: AtomicF32,
}

impl Default for DelayParams {
    fn default() -> Self {
        Self { on: AtomicBool::new(false), mode: AtomicI32::new(0), delay: AtomicF32::new(DELAY_MIN_M) }
    }
}

/// The most plug-in blocks alive at once (their ids index the latency table).
pub const MAX_PLUGIN_BLOCKS: usize = 1024;

static PLUGIN_IDS: [AtomicBool; MAX_PLUGIN_BLOCKS] = [const { AtomicBool::new(false) }; MAX_PLUGIN_BLOCKS];
static PLUGIN_LATENCY: [AtomicU32; MAX_PLUGIN_BLOCKS] = [const { AtomicU32::new(0) }; MAX_PLUGIN_BLOCKS];

/// Any thread (a plug-in may change its latency inside its process call): the
/// latency in samples the host reports for the plug-in of block id `pid`.
pub fn set_plugin_latency(pid: u32, samples: u32) {
    if let Some(l) = PLUGIN_LATENCY.get((pid as usize).wrapping_sub(1)) {
        l.store(samples, Relaxed);
    }
}

/// A plug-in block's settings. The processor (the app's callback set, as an FX
/// slot's) lives in `rack`; `pid` names the block to the host (1-based, 0 = none
/// left) and indexes the lock-free latency table.
#[repr(C)]
pub struct PluginParams {
    /// Running (first, so the app reads it like another block's `on`); off =
    /// bypassed: the signal passes and no latency counts.
    pub on: AtomicBool,
    pid: u32,
    rack: FxRack,
    /// The saved identity of a plug-in that could not be loaded (a show's
    /// `<PLUGIN>` description and state), kept so the block saves it again.
    missing: std::sync::Mutex<Option<(String, String)>>,
}

impl std::fmt::Debug for PluginParams {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PluginParams").field("pid", &self.pid).field("on", &self.on).finish()
    }
}

impl PluginParams {
    fn new() -> Self {
        let slot = PLUGIN_IDS.iter().position(|u| u.compare_exchange(false, true, Relaxed, Relaxed).is_ok());
        let pid = slot.map_or(0, |i| i as u32 + 1);
        if pid != 0 {
            PLUGIN_LATENCY[pid as usize - 1].store(0, Relaxed);
        }
        Self { on: AtomicBool::new(true), pid, rack: FxRack::default(), missing: std::sync::Mutex::new(None) }
    }

    /// The block's host id (0: no id was left, the block can't hold a plug-in).
    pub fn pid(&self) -> u32 {
        self.pid
    }

    pub fn rack(&self) -> &FxRack {
        &self.rack
    }

    pub fn is_loaded(&self) -> bool {
        self.rack.is_loaded()
    }

    /// The plug-in's latency as the host reported it (0 without one).
    pub fn latency(&self) -> u32 {
        match PLUGIN_LATENCY.get((self.pid as usize).wrapping_sub(1)) {
            Some(l) if self.rack.is_loaded() => l.load(Relaxed),
            _ => 0,
        }
    }

    /// What the block adds to the strip's latency now: the plug-in's while it runs.
    pub fn effective_latency(&self) -> u32 {
        if !self.on.load(Relaxed) || self.rack.is_faulted() {
            0
        } else {
            self.latency()
        }
    }

    /// Message thread: installs (or clears) the processor; the host reports its latency after.
    pub fn set(&self, cb: Option<crate::fx::InsertCallbacks>, keep_fault: bool, garbage: &crate::garbage::Garbage) {
        match cb {
            Some(cb) if keep_fault => self.rack.replace(cb, garbage),
            cb => self.rack.set(cb, garbage),
        }
        set_plugin_latency(self.pid, 0);
        if cb.is_some() {
            *self.missing.lock().unwrap_or_else(|e| e.into_inner()) = None;
        }
    }

    /// The saved description and state of a plug-in that wasn't loaded.
    pub fn missing(&self) -> Option<(String, String)> {
        self.missing.lock().unwrap_or_else(|e| e.into_inner()).clone()
    }

    pub fn set_missing(&self, saved: Option<(String, String)>) {
        *self.missing.lock().unwrap_or_else(|e| e.into_inner()) = saved;
    }
}

impl Drop for PluginParams {
    fn drop(&mut self) {
        if let Some(u) = PLUGIN_IDS.get((self.pid as usize).wrapping_sub(1)) {
            u.store(false, Relaxed);
        }
    }
}

/// A block's parameters: the same blocks as the strip's own.
#[derive(Debug)]
pub enum BlockParams {
    Filter(FilterParams),
    Eq(EqBlockParams),
    Dynamics(DynSlotParams),
    Insert(InsertParams),
    Delay(DelayParams),
    Plugin(PluginParams),
}

impl BlockParams {
    /// New WING parameters of `kind` (`new_in` with the WING's models).
    pub fn new(kind: BlockKind, bus: bool) -> Self {
        Self::new_in(kind, bus, &livedaw_dsp::wing::MODELS)
    }

    /// New parameters of `kind` for a console of `models`: EQs off on the
    /// channel (`bus` false) or bus table's first model, dynamics off with
    /// the table's compressor, inserts empty, delays off.
    pub fn new_in(kind: BlockKind, bus: bool, models: &'static ModelTables) -> Self {
        match kind {
            BlockKind::Filter => Self::Filter(FilterParams::default()),
            BlockKind::Eq => {
                let p = EqBlockParams::new_in(if bus { models.eq_bus } else { models.eq_channel });
                p.on.store(false, Relaxed);
                Self::Eq(p)
            }
            BlockKind::Dynamics => Self::Dynamics(DynSlotParams::new_in(models.dynamics, models.comp_default)),
            BlockKind::Insert => Self::Insert(InsertParams::default()),
            BlockKind::Delay => Self::Delay(DelayParams::default()),
            BlockKind::Plugin => Self::Plugin(PluginParams::new()),
        }
    }

    pub fn kind(&self) -> BlockKind {
        match self {
            Self::Filter(_) => BlockKind::Filter,
            Self::Eq(_) => BlockKind::Eq,
            Self::Dynamics(_) => BlockKind::Dynamics,
            Self::Insert(_) => BlockKind::Insert,
            Self::Delay(_) => BlockKind::Delay,
            Self::Plugin(_) => BlockKind::Plugin,
        }
    }
}

/// What a block measures, for the screens (written by the audio thread).
#[repr(C)]
#[derive(Debug, Default)]
pub struct BlockMeters {
    /// Dynamics: gain reduction in the latest block, dB.
    pub reduction_db: AtomicF32,
    /// Dynamics: working (the DYN LED).
    pub active: AtomicBool,
    /// Output peak since the last read (linear).
    pub out_peak: AtomicF32,
}

/// A block's DSP state (the task running the strip owns it).
pub(crate) enum BlockDsp {
    Filter(Box<WingFilter>),
    Eq(Box<WingEq>, [f32; MAX_EQ_PARAMS]),
    Dynamics(Box<WingDynamics>, [f32; MAX_DYN_PARAMS]),
    Insert,
    Delay(Box<DelayLine>),
    Plugin,
}

impl BlockDsp {
    fn new(kind: BlockKind) -> Self {
        match kind {
            BlockKind::Filter => Self::Filter(Box::default()),
            BlockKind::Eq => Self::Eq(Box::default(), [0.0; MAX_EQ_PARAMS]),
            BlockKind::Dynamics => Self::Dynamics(Box::default(), [0.0; MAX_DYN_PARAMS]),
            BlockKind::Insert => Self::Insert,
            BlockKind::Delay => Self::Delay(Box::default()),
            BlockKind::Plugin => Self::Plugin,
        }
    }

    pub(crate) fn prepare(&mut self, sample_rate: f64) {
        match self {
            Self::Filter(f) => f.prepare(sample_rate),
            Self::Eq(e, _) => e.prepare(sample_rate),
            Self::Dynamics(d, _) => d.prepare(sample_rate),
            Self::Insert | Self::Plugin => {}
            Self::Delay(d) => d.prepare(sample_rate),
        }
    }

    pub(crate) fn reset(&mut self) {
        match self {
            Self::Filter(f) => f.reset(),
            Self::Eq(e, _) => e.reset(),
            Self::Dynamics(d, _) => d.reset(),
            Self::Insert | Self::Plugin => {}
            Self::Delay(d) => d.reset(),
        }
    }
}

/// An extra block of a strip's chain.
pub struct Block {
    uid: u32,
    pub params: BlockParams,
    pub meters: BlockMeters,
    dsp: UnsafeCell<BlockDsp>,
}

// SAFETY: `dsp` is only touched by the task running the strip (or by the
// message thread before the block is published); everything else is atomic.
unsafe impl Sync for Block {}
unsafe impl Send for Block {}

impl Block {
    /// A new block of `kind` with the strip-unique id `uid`, its models its
    /// console's (`models`), prepared for `sample_rate`.
    pub fn new(uid: u32, kind: BlockKind, bus: bool, sample_rate: f64, models: &'static ModelTables) -> Self {
        let mut dsp = BlockDsp::new(kind);
        dsp.prepare(sample_rate);
        Self { uid, params: BlockParams::new_in(kind, bus, models), meters: BlockMeters::default(), dsp: UnsafeCell::new(dsp) }
    }

    /// One of the strip's own blocks (`params::OwnBlocks`, id 0), prepared
    /// with the strip (`Strip::prepare`).
    pub(crate) fn own(params: BlockParams) -> Self {
        Self { uid: 0, dsp: UnsafeCell::new(BlockDsp::new(params.kind())), params, meters: BlockMeters::default() }
    }

    /// Message thread, audio stopped or the block unpublished: sizes a plug-in block's buffers.
    pub(crate) fn prepare_block_size(&self, max_block: usize) {
        if let BlockParams::Plugin(p) = &self.params {
            // SAFETY: the audio thread can't reach the block (caller's contract).
            unsafe { p.rack.prepare_light(max_block) };
        }
    }

    /// The block's id, unique on its strip (kept in shows; 0 for the strip's
    /// own blocks).
    pub fn uid(&self) -> u32 {
        self.uid
    }

    pub fn kind(&self) -> BlockKind {
        self.params.kind()
    }

    /// # Safety
    /// The task running the strip only, or the message thread while the
    /// block can't be reached by the audio thread.
    #[allow(clippy::mut_from_ref)]
    pub(crate) unsafe fn dsp_mut(&self) -> &mut BlockDsp {
        &mut *self.dsp.get()
    }
}

/// A step of a published chain: a node of the strip's template, or an extra block.
#[derive(Clone)]
pub enum Step {
    Node(Node),
    Block(Arc<Block>),
}

/// A strip's chain when it isn't its template: the template's nodes with
/// extra blocks among them. Immutable once published.
pub struct ChainLayout {
    pub(crate) steps: Box<[Step]>,
}

impl ChainLayout {
    pub fn new(steps: Vec<Step>) -> Self {
        Self { steps: steps.into_boxed_slice() }
    }

    pub fn steps(&self) -> &[Step] {
        &self.steps
    }

    /// The extra blocks, in chain order.
    pub fn blocks(&self) -> impl Iterator<Item = &Arc<Block>> {
        self.steps.iter().filter_map(|s| match s {
            Step::Block(b) => Some(b),
            Step::Node(_) => None,
        })
    }
}
