//! One console strip, processed the WING way (Operation Manual, chapter 6):
//!
//! * input channels: INPUT (trim, balance, polarity) -> FILTER -> the GATE
//!   slot, EQ, DYNAMICS slot and INS 1 in the strip's process order -> fader
//!   (with DCAs, mute and mute groups) -> INS 2 (or automix) -> delay (post-fader)
//!   -> width/pan. The TAP point is selectable.
//! * aux channels: INPUT -> delay (pre-fader) -> DYNAMICS -> EQ -> INS 1 ->
//!   fader. No filter, gate or INS 2; the TAP point is fixed after INS 1.
//! * buses, mains and matrices: INPUT -> DYNAMICS -> INS 1 -> fader -> EQ
//!   (post-fader) -> INS 2 -> delay -> width/pan. The TAP point is fixed after
//!   INS 1 (pre-fader).
//!
//! The strip runs these as a chain of nodes (`chain.rs`: the WING's template
//! for its kind, the process order resolved each block).
//!
//! Signal taps along the way feed TAP-mode sends, solo (PFL/AFL) and direct outs.
//! Input channels shape their tap signal with TAP EQ and TAP WIDTH. The
//! analysed strip (`analyser`) also copies its signal to the RTA and scope
//! rings, and the listened strip computes its LISTEN signal.

use crate::analyser::{listen_mode, rta_tap, Probes, StripProbe};
use crate::atomic::AtomicF32;
use crate::blocks::{Block, BlockDsp, BlockParams, ChainLayout, Step};
use crate::chain::{self, Node};
use crate::fx::{FxInputs, FxRack};
use crate::garbage::Garbage;
use crate::groups::GroupSet;
use crate::keytap::{key_tap, stage_of, KeySource, StripKeyTaps};
use crate::monitor::SoloPatch;
use crate::params::{DcaParams, DynSlotParams, FxParams, InsertParams, OwnBlocks, StripKind, StripMeters, StripParams};
use crate::params::{MainSendParams, MatrixSendParams, MAIN_SEND_PAGE, MAX_MAINS, MAX_MATRICES};
use crate::pdc::{pdc_tap, StripPdc};
use crate::profile::{Profile, ProfileSpec};
use crate::routing::{return_tap, strip_source_slot, ReturnRing};
use crate::sends::{Bank, SendBank};
use crate::solo::StripConsole;
use crate::sources::{fx_return_channel, internal, osc_channel, usr_channel};
use crate::taps::{StripTaps, FIRST_USER_TAP};
use crate::tapsend::TapShaper;
use livedaw_dsp::biquad::{Biquad, BiquadCoeffs};
use livedaw_dsp::delay::{delay_samples, DelayLine};
use livedaw_dsp::util::*;
use livedaw_dsp::wing::dynamics::{DynStatus, DYN_BANDS};
use livedaw_recorder::{playback_channel, usb_player_channel};
use std::cell::UnsafeCell;
use std::sync::atomic::{
    AtomicBool, AtomicPtr, AtomicU32,
    Ordering::{AcqRel, Acquire, Relaxed},
};
use std::sync::{Arc, Mutex};

/// Tap points (TAP_POINTS order). WING names: INPUT, FILTER, TAP 3/4/5 (after
/// the 1st/2nd/3rd block), PRE FDR, POST FDR (before INS 2), POST PROC (after
/// INS 2 and the post-fader delay, before width/pan).
pub mod tap {
    pub const IN: usize = 0;
    pub const FILT: usize = 1;
    pub const PFL: usize = 5;
    pub const AFL: usize = 6;
    pub const POST: usize = 7;
}

/// What a strip needs from the engine while processing.
pub(crate) struct ProcessContext<'a> {
    pub fx: &'a [FxRack],
    pub fx_params: &'a [FxParams],
    pub dcas: &'a [DcaParams],
    pub mute_groups: GroupSet,
    /// DCA MUTEGROUPS: a muted DCA mutes its strips.
    pub dca_mute_groups: bool,
    /// MAIN LINK: mains 2..=`main_link_last` (0-based 1..) add Main 1's fader level.
    pub main_link_last: usize,
    pub main_link_db: f32,
    /// Hardware inputs by hardware channel (null = inactive).
    pub hw_inputs: &'a [*const f32],
    /// The recorder's playback channels (input `PLAYBACK_SOURCE_BASE + n`),
    /// then the USB PLAYER's (input `USB_PLAYER_SOURCE_BASE + n`).
    pub playback: &'a [*const f32],
    /// The input channels (for sidechain keys from other channels).
    pub channels: &'a [Arc<Strip>],
    /// The aux, bus, main and matrix keys of aux, bus, main and matrix DYNAMICS.
    pub key_strips: KeyStrips<'a>,
    /// KEY TAP (`keytap.rs`): the stage-1 wave of each input channel this
    /// block, and the wave this task runs in (None: a later stage, after
    /// every channel ran).
    pub key_waves: &'a [u8],
    pub wave: Option<u8>,
    pub block: u64,
    /// The analysed and the listened strip of this block.
    pub probes: &'a Probes,
    /// SOURCE SOLO (`monitor.rs`): the host strip and the soloed source's
    /// ports, which it plays instead of its own input this block.
    pub source_solo: Option<(&'a Strip, SoloPatch)>,
}

/// The strips other than input channels a key can come from (`keytap.rs`).
#[derive(Clone, Copy)]
pub(crate) struct KeyStrips<'a> {
    pub auxes: &'a [Arc<Strip>],
    /// The buses by slot (found by id), and the wave each runs in this block
    /// (`routing::BusSchedule`, same order).
    pub buses: &'a [Arc<Strip>],
    pub bus_waves: &'a [u16],
    pub mains: &'a [Arc<Strip>],
    pub matrices: &'a [Arc<Strip>],
}

impl<'a> KeyStrips<'a> {
    /// The strip of `kind` with index `i` (from 0).
    pub fn get(&self, kind: StripKind, i: usize) -> Option<&'a Strip> {
        let found = match kind {
            StripKind::Channel => None,
            StripKind::Aux => self.auxes.get(i),
            StripKind::Bus => self.buses.iter().find(|b| b.id() as usize == i),
            StripKind::Main => self.mains.get(i),
            StripKind::Matrix => self.matrices.get(i),
        };
        found.map(|s| s.as_ref())
    }

    /// The stage strip (`kind`, `id`) runs in this block (`keytap::stage_of`).
    pub fn stage(&self, kind: StripKind, id: i32) -> u32 {
        let wave = match kind {
            StripKind::Bus => {
                self.buses.iter().position(|b| b.id() == id).and_then(|i| self.bus_waves.get(i)).copied().unwrap_or(0)
            }
            _ => 0,
        };
        stage_of(kind, wave)
    }
}

impl ProcessContext<'_> {
    /// The buffer of input source `index` (a hardware input or a playback channel).
    fn source(&self, index: i32) -> Option<*const f32> {
        source_ptr(self.hw_inputs, self.playback, index)
    }

    /// A key from input channel `ch` can take its tap of this block: the
    /// channel ran in an earlier wave or stage (else: the previous block's).
    fn key_is_current(&self, ch: usize) -> bool {
        match self.wave {
            None => true,
            Some(w) => self.key_waves.get(ch).is_some_and(|&c| c < w),
        }
    }
}

/// Realtime state, touched by exactly one audio task at a time (see the
/// stage rules on [`Strip`]).
pub(crate) struct StripDsp {
    sample_rate: f64,
    max_block: usize,
    num_channels: usize,
    /// A mono strip made stereo by an insert in this block (see `run_insert`).
    widened: bool,
    input_connected: bool,
    idle: bool,
    work: [Vec<f32>; 2],
    tap: [Vec<f32>; 2],
    pfl: [Vec<f32>; 2],
    post: [Vec<f32>; 2],
    out: [Vec<f32>; 2],
    key: Vec<f32>,
    delay: DelayLine,
    last_in: [f32; 2],
    last_gain: f32,
    /// 2x2 output matrix (ll, rl, lr, rr): out_l = ll*L + rl*R, out_r = lr*L + rr*R.
    out_matrix: [f32; 4],
    /// Pan gains for taking pre-fader signals into stereo destinations.
    pan_gains: [f32; 2],
    automix_gain: f32,
    automix_power: f32,
    tap_point: usize,
    /// Effective mute in the last block (TAP-mode sends follow it).
    muted: bool,
    /// Matrices: ramp of the direct input (owned by the matrix task).
    last_direct: f32,
    /// LISTEN: the signal (1 or 2 channels), its filter, and what it last played.
    listen: [Vec<f32>; 2],
    listen_channels: usize,
    listen_filter: Biquad,
    listen_coeffs: BiquadCoeffs,
    listen_state: Option<(i32, i32)>,
    /// A filter change crossfades from the old filter (kept here) to the new.
    listen_prev: Biquad,
    listen_scratch: Vec<f32>,
    listen_fresh: bool,
    /// TAP EQ and TAP WIDTH (input channels).
    tap_shaper: TapShaper,
}

impl StripDsp {
    fn new() -> Self {
        Self {
            sample_rate: 48000.0,
            max_block: 0,
            num_channels: 1,
            widened: false,
            input_connected: true,
            idle: false,
            work: [Vec::new(), Vec::new()],
            tap: [Vec::new(), Vec::new()],
            pfl: [Vec::new(), Vec::new()],
            post: [Vec::new(), Vec::new()],
            out: [Vec::new(), Vec::new()],
            key: Vec::new(),
            delay: DelayLine::default(),
            last_in: [1.0; 2],
            last_gain: 1.0,
            out_matrix: [0.0; 4],
            pan_gains: [1.0; 2],
            automix_gain: 1.0,
            automix_power: 0.0,
            tap_point: tap::PFL,
            muted: false,
            last_direct: 0.0,
            listen: [Vec::new(), Vec::new()],
            listen_channels: 1,
            listen_filter: Biquad::default(),
            listen_coeffs: BiquadCoeffs::identity(),
            listen_state: None,
            listen_prev: Biquad::default(),
            listen_scratch: Vec::new(),
            listen_fresh: true,
            tap_shaper: TapShaper::default(),
        }
    }

    fn reset_dsp(&mut self) {
        self.delay.reset();
        self.listen_filter.reset();
        self.tap_shaper.reset();
    }

    fn buffers_mut(&mut self) -> [&mut [Vec<f32>; 2]; 6] {
        [&mut self.work, &mut self.tap, &mut self.pfl, &mut self.post, &mut self.out, &mut self.listen]
    }
}

/// A console strip.
///
/// Thread rules for the realtime state (`dsp` and the `last_*` ramps):
/// * the stage task processing a strip owns its `dsp`; later stages only *read*
///   the buffers of strips processed in earlier stages;
/// * each destination task owns the ramps of the sends feeding it: bus tasks the
///   `last_gain` of their send (`SendParams`, `MainSendParams`,
///   `MatrixSendParams`) on every strip feeding them;
/// * the monitor stage runs on the audio thread alone and owns `last_solo`;
/// * the message thread only touches `dsp` through `prepare`, while the strip is
///   not reachable by the audio thread (device stopped, or not yet published).
pub struct Strip {
    kind: StripKind,
    id: i32,
    pub params: StripParams,
    pub meters: StripMeters,
    /// The GATE and DYNAMICS slots' dynamic EQ bands: live gain in dB, signed
    /// (outside `StripMeters`, whose layout is part of the C ABI).
    pub band_meters: DynBandMeters,
    /// The same peaks as `meters` for a second reader, the WING remote's
    /// meter stream (the UI takes `meters`' peaks; outside the C ABI).
    pub remote_peaks: RemotePeaks,
    /// Sends to buses (channels/aux: all buses; buses: every other bus).
    pub sends: SendBank,
    /// Sends to mains and matrices, by the destination's index (paged: as
    /// many as the console has, up to `MAX_MAINS` / `MAX_MATRICES`).
    pub mains: Bank<MainSendParams>,
    pub matrices: Bank<MatrixSendParams>,
    /// UI metadata (never touched by the audio thread).
    name: Mutex<String>,
    /// The free tags of HOME > TAGS, comma-separated (the WING's `/ch/n/tags`).
    tags: Mutex<String>,
    /// Console-level state: talk tags, MUTEGROUP/SIP OVERRIDE, the block's
    /// effective solo and mute decision (`solo.rs`).
    pub console: StripConsole,
    /// Plugin delay compensation: tap latencies and history rings (`pdc.rs`).
    pub pdc: StripPdc,
    /// Input channels: the taps other strips' keys read (`keytap.rs`).
    pub keys: StripKeyTaps,
    /// User taps read by sends (`taps.rs`).
    pub taps: StripTaps,
    /// Buses, mains and matrices: their output and tap one block late, for
    /// the readers that run before them (`routing.rs`).
    returns: StripReturns,
    /// The console's profile when the strip was made: its chain template and
    /// process orders.
    spec: &'static ProfileSpec,
    /// The chain when it isn't the template (extra blocks, `blocks.rs`);
    /// null = the profile's template. Swapped by the message thread
    /// (`set_layout`), the old one retired through the garbage list.
    layout: AtomicPtr<ChainLayout>,
    /// The id the next extra block gets.
    next_block_uid: AtomicU32,
    dsp: UnsafeCell<StripDsp>,
    last_solo: UnsafeCell<f32>,
}

/// A bus's, main's or matrix's signals read one block late: a channel
/// patched to it, a bus send that closes a loop (`routing.rs`). The ring
/// holds OUT L/R then TAP L/R (`return_tap`) and is only written while some
/// reader wants that signal.
struct StripReturns {
    ring: UnsafeCell<ReturnRing>,
    /// Audio thread, per return tap: a reader asked for it this block, and
    /// its ring holds the signal.
    wanted: [AtomicBool; 2],
    live: [AtomicBool; 2],
}

impl Default for StripReturns {
    fn default() -> Self {
        Self { ring: UnsafeCell::new(ReturnRing::default()), wanted: Default::default(), live: Default::default() }
    }
}

/// Live gains of a strip's dynamic EQ bands (DEQ, DEQ2), written by the audio thread.
#[derive(Debug, Default)]
pub struct DynBandMeters {
    pub gate: [AtomicF32; DYN_BANDS],
    pub dynamics: [AtomicF32; DYN_BANDS],
}

impl DynBandMeters {
    /// Band `band` of the GATE (`gate`) or DYNAMICS slot, dB (0 when out of range).
    pub fn get(&self, gate: bool, band: usize) -> f32 {
        let slot = if gate { &self.gate } else { &self.dynamics };
        slot.get(band).map_or(0.0, AtomicF32::load)
    }

    fn store(&self, gate: bool, gains: [f32; DYN_BANDS]) {
        let slot = if gate { &self.gate } else { &self.dynamics };
        for (a, g) in slot.iter().zip(gains) {
            a.store(g);
        }
    }
}

/// Peaks (linear, max since the last [`RemotePeaks::take`]) at the points
/// the WING's meter stream reports: the input, PRE (after trim, balance and
/// polarity, before the processing: the meters' PRE tap, as a WING's strip
/// meter moves with TRIM and not with the EQ) and output. One reader (the
/// remote's meter clock) takes them; the UI has its own.
#[derive(Debug, Default)]
pub struct RemotePeaks {
    pub input: [AtomicF32; 2],
    pub pre: [AtomicF32; 2],
    pub output: [AtomicF32; 2],
}

impl RemotePeaks {
    /// Takes the peaks since the last take: (input, PRE, output) L/R.
    pub fn take(&self) -> [[f32; 2]; 3] {
        let t = |a: &[AtomicF32; 2]| [a[0].exchange(0.0), a[1].exchange(0.0)];
        [t(&self.input), t(&self.pre), t(&self.output)]
    }
}

// SAFETY: see the thread rules above; everything shared is atomic.
unsafe impl Sync for Strip {}
unsafe impl Send for Strip {}

impl Drop for Strip {
    fn drop(&mut self) {
        let layout = *self.layout.get_mut();
        if !layout.is_null() {
            // SAFETY: created by Box::into_raw in `set_layout`; the strip is
            // dropped once no callback can reach it.
            drop(unsafe { Box::from_raw(layout) });
        }
    }
}

impl Strip {
    /// A WING strip.
    pub fn new(kind: StripKind, id: i32) -> Self {
        Self::of_profile(Profile::Wing.spec(), kind, id)
    }

    /// A strip of a console of `spec`'s profile.
    pub fn of_profile(spec: &'static ProfileSpec, kind: StripKind, id: i32) -> Self {
        Self::of_profile_in(spec, kind, id, usize::try_from(id).unwrap_or(0))
    }

    /// A strip of a console of `spec`'s profile in `slot` (0-based) of its kind:
    /// its factory settings, colour and name are the slot's. A bus's id is its
    /// slot; a channel's id (1-based, kept by shows) isn't, and an 02R's
    /// channels differ by slot (LINE, TAPE).
    pub fn of_profile_in(spec: &'static ProfileSpec, kind: StripKind, id: i32, slot: usize) -> Self {
        Self {
            spec,
            kind,
            id,
            // Buses are numbered by their slot (`id`), whose factory settings differ;
            // a console with its own factory colours (`Palette::factory`) uses them.
            params: {
                let p = StripParams::new_in_slot_of(kind, slot, spec);
                if let Some(colour) = spec.palette.factory {
                    p.colour.store(colour(kind, slot), Relaxed);
                }
                p
            },
            meters: StripMeters::default(),
            band_meters: DynBandMeters::default(),
            remote_peaks: RemotePeaks::default(),
            sends: SendBank::of(spec, kind, usize::try_from(id).unwrap_or(0)),
            mains: Bank::new(MAX_MAINS, MAIN_SEND_PAGE, kind, usize::try_from(id).unwrap_or(0), spec),
            matrices: Bank::new(MAX_MATRICES, MAIN_SEND_PAGE, kind, usize::try_from(id).unwrap_or(0), spec),
            name: Mutex::new(spec.factory_name.and_then(|f| f(kind, slot)).unwrap_or_default()),
            tags: Mutex::new(String::new()),
            console: StripConsole::default(),
            pdc: StripPdc::default(),
            keys: StripKeyTaps::default(),
            taps: StripTaps::default(),
            returns: StripReturns::default(),
            layout: AtomicPtr::new(std::ptr::null_mut()),
            next_block_uid: AtomicU32::new(1),
            dsp: UnsafeCell::new(StripDsp::new()),
            last_solo: UnsafeCell::new(0.0),
        }
    }

    pub fn kind(&self) -> StripKind {
        self.kind
    }

    /// Channels: a stable id. Buses: the slot sends refer to. Others: 0-based index.
    pub fn id(&self) -> i32 {
        self.id
    }

    pub fn name(&self) -> String {
        self.name.lock().unwrap_or_else(|e| e.into_inner()).clone()
    }

    pub fn set_name(&self, name: &str) {
        *self.name.lock().unwrap_or_else(|e| e.into_inner()) = name.to_owned();
    }

    /// The free tags (HOME > TAGS), comma-separated.
    pub fn tags(&self) -> String {
        self.tags.lock().unwrap_or_else(|e| e.into_inner()).clone()
    }

    pub fn set_tags(&self, tags: &str) {
        *self.tags.lock().unwrap_or_else(|e| e.into_inner()) = tags.to_owned();
    }

    /// The strip has an INS 2 slot (every kind but aux).
    pub fn has_post_insert(&self) -> bool {
        self.kind != StripKind::Aux
    }

    /// The strip can join automix X/Y (the INS 2 of input channels only).
    pub fn has_automix(&self) -> bool {
        self.kind == StripKind::Channel
    }

    /// The automix group the strip is a member of (0 = X, 1 = Y): an input
    /// channel whose INS 2 is AUTO X / AUTO Y *and* ON. As on the console,
    /// INS 2's ON key switches automix in and out like an FX insert, so a
    /// channel set to AUTO with INS 2 off is not a member (not gain-shared,
    /// not listed on the automix screen, not spilled). The group's ENABLE is
    /// separate (`EngineControls::automix_enable`).
    pub fn automix_group(&self) -> Option<usize> {
        let ins = self.params.post_insert();
        if !self.has_automix() || !ins.on.load(Relaxed) {
            return None;
        }
        match ins.mode.load(Relaxed) {
            1 => Some(0),
            2 => Some(1),
            _ => None,
        }
    }

    /// The TAP point can be chosen (input channels; the others are fixed after INS 1).
    pub fn has_selectable_tap(&self) -> bool {
        self.kind == StripKind::Channel
    }

    /// Input and aux channels: the strip takes its ALT input now - it has one,
    /// and its MAIN / ALT switch is on ALT. (The global MAIN / ALT switch sets
    /// the switches of the strips in GLOBAL INPUT SELECT ENABLE:
    /// `Engine::set_global_alt`.)
    pub fn uses_alt_input(&self) -> bool {
        let p = &self.params;
        self.kind.is_input() && p.alt_input_left.load(Relaxed) >= 0 && p.alt_selected.load(Relaxed)
    }

    /// The strip's own MAIN / ALT switch can be used (WING SETUP > AUDIO):
    /// always on a strip set to INDIVIDUAL, and on a GLOBAL one (`src_auto`)
    /// only with GLOBAL INPUT SELECT OVERRIDE (`/io/autoaltovr`).
    pub fn can_switch_input(&self, input_select_override: bool) -> bool {
        self.kind.is_input() && (input_select_override || !self.params.src_auto.load(Relaxed))
    }

    /// The input sources (left, right) in use: MAIN or ALT.
    pub fn active_inputs(&self) -> (i32, i32) {
        let p = &self.params;
        if self.uses_alt_input() {
            (p.alt_input_left.load(Relaxed), p.alt_input_right.load(Relaxed))
        } else {
            (p.input_left.load(Relaxed), p.input_right.load(Relaxed))
        }
    }

    /// The INS 2 slot runs an FX slot (not automix), with `fx` its slot index.
    pub(crate) fn post_insert_fx(&self) -> Option<i32> {
        let ins = self.params.post_insert();
        (self.has_post_insert() && ins.on.load(Relaxed) && ins.mode.load(Relaxed) == 0).then(|| ins.fx.load(Relaxed))
    }

    /// True if this strip feeds `slot` from its pre/post insert.
    pub fn uses_fx(&self, slot: usize) -> bool {
        let p = &self.params;
        (p.pre_insert().on.load(Relaxed) && p.pre_insert().fx.load(Relaxed) == slot as i32)
            || self.post_insert_fx() == Some(slot as i32)
            || self.block_inserts().any(|ins| ins.on.load(Relaxed) && ins.fx.load(Relaxed) == slot as i32)
    }

    /// An insert of the strip names FX slot `slot`, on or off.
    pub fn assigns_fx(&self, slot: usize) -> bool {
        let p = &self.params;
        let slot = slot as i32;
        p.pre_insert().fx.load(Relaxed) == slot
            || (self.has_post_insert() && p.post_insert().fx.load(Relaxed) == slot)
            || self.block_inserts().any(|ins| ins.fx.load(Relaxed) == slot)
    }

    fn has_inserts(&self) -> bool {
        let p = &self.params;
        (p.pre_insert().on.load(Relaxed) && p.pre_insert().fx.load(Relaxed) >= 0)
            || self.post_insert_fx().is_some_and(|fx| fx >= 0)
            || self.block_inserts().any(|ins| ins.on.load(Relaxed) && ins.fx.load(Relaxed) >= 0)
    }

    /// The parameters of the extra insert blocks.
    fn block_inserts(&self) -> impl Iterator<Item = &InsertParams> {
        self.layout().into_iter().flat_map(|l| l.blocks()).filter_map(|b| match &b.params {
            BlockParams::Insert(ins) => Some(ins),
            _ => None,
        })
    }

    // ---- the chain (message thread) -------------------------------------------

    /// The published chain when it isn't the template (any thread: the
    /// layout lives until the garbage list frees it, after the audio thread
    /// and the message thread are done with it).
    pub fn layout(&self) -> Option<&ChainLayout> {
        // SAFETY: published by `set_layout` with Release; retired through the
        // garbage list, freed only once no callback can see it.
        unsafe { self.layout.load(Acquire).as_ref() }
    }

    /// The strip's chain as steps: its layout, or its template.
    pub fn steps(&self) -> Vec<Step> {
        match self.layout() {
            Some(l) => l.steps().to_vec(),
            None => self.chain().iter().map(|&n| Step::Node(n)).collect(),
        }
    }

    /// The extra blocks of the chain, in order.
    pub fn blocks(&self) -> Vec<Arc<Block>> {
        self.layout().map(|l| l.blocks().cloned().collect()).unwrap_or_default()
    }

    /// A new id for an extra block (unique on the strip; `ensure_block_uid`
    /// keeps it above the ids loaded from a show).
    pub(crate) fn new_block_uid(&self) -> u32 {
        self.next_block_uid.fetch_add(1, Relaxed)
    }

    pub(crate) fn ensure_block_uid(&self, uid: u32) {
        self.next_block_uid.fetch_max(uid.saturating_add(1), Relaxed);
    }

    /// Message thread: publishes `steps` as the strip's chain (the template's
    /// nodes alone go back to the template) and retires the old layout.
    pub(crate) fn set_layout(&self, steps: Vec<Step>, garbage: &Garbage) {
        let template = steps.iter().all(|s| matches!(s, Step::Node(_)))
            && steps
                .iter()
                .map(|s| match s {
                    Step::Node(n) => Some(*n),
                    Step::Block(_) => None,
                })
                .eq(self.chain().iter().map(|&n| Some(n)));
        let next = if template { std::ptr::null_mut() } else { Box::into_raw(Box::new(ChainLayout::new(steps))) };
        let old = self.layout.swap(next, AcqRel);
        if !old.is_null() {
            // SAFETY: created by Box::into_raw above; now unpublished.
            garbage.retire(unsafe { Box::from_raw(old) });
        }
    }

    /// The plugin latency of each of the strip's taps (`pdc_tap` order)
    /// relative to its input, given the latency of every FX slot: INS 1 counts
    /// from the pre-fader point on (and at the TAP point when that follows
    /// INS 1 in the process order), INS 2 after the fader.
    pub(crate) fn own_latency(&self, fx_latency: &[u32]) -> [u32; pdc_tap::COUNT] {
        let p = &self.params;
        let slot = |fx: i32| usize::try_from(fx).ok().and_then(|i| fx_latency.get(i)).copied().unwrap_or(0);
        let insert = |ins: &InsertParams| if ins.on.load(Relaxed) { slot(ins.fx.load(Relaxed)) } else { 0 };
        let tap_point = if self.has_selectable_tap() { p.tap.load(Relaxed).clamp(0, 7) as usize } else { tap::PFL };
        // The inserts' latency accumulated along the chain, read at the taps.
        let (mut at, mut tap_l, mut pfl, mut post) = (0, 0, 0, 0);
        let mut visit = |node: Option<Node>, block: Option<&Block>| match (node, block) {
            (Some(Node::PreInsert), _) => at += insert(p.pre_insert()),
            (Some(Node::PostInsert), _) => at += self.post_insert_fx().map_or(0, slot),
            (Some(Node::Tap(t)), _) if t as usize == tap_point => tap_l = at,
            (Some(Node::Pfl), _) => pfl = at,
            (Some(Node::Post), _) => post = at,
            (None, Some(b)) => {
                if let BlockParams::Insert(ins) = &b.params {
                    at += insert(ins);
                }
            }
            _ => {}
        };
        let resolve = |node: Node| match node {
            Node::Slot(k) => self.pre_fader_order().get(k as usize).and_then(|&b| chain::order_block(b)),
            other => Some(other),
        };
        match self.layout() {
            Some(layout) => {
                for step in layout.steps() {
                    match step {
                        Step::Node(n) => visit(resolve(*n), None),
                        Step::Block(b) => visit(None, Some(b)),
                    }
                }
            }
            None => {
                for &n in self.chain() {
                    visit(resolve(n), None);
                }
            }
        }
        [tap_l, pfl, post, post]
    }

    /// Writes this block's taps into their compensation rings (the ones that exist).
    ///
    /// # Safety
    /// Owning task only, after the strip ran (and, for input channels, after automix).
    pub(crate) unsafe fn write_pdc(&self, n: usize) {
        let (d, p) = (self.dsp(), &self.pdc);
        p.write(pdc_tap::TAP, d.tap_signal(0, n), d.tap_signal(1, n));
        p.write(pdc_tap::PFL, d.pfl(0, n), d.pfl(1, n));
        p.write(pdc_tap::POST, d.post(0, n), d.post(1, n));
        p.write(pdc_tap::OUT, d.out(0, n), d.out(1, n));
    }

    /// Audio thread, before the block's stages: a reader wants return tap
    /// `tap` (`return_tap`) of this bus, main or matrix this block.
    pub(crate) fn want_return(&self, tap: usize) {
        if let Some(w) = self.returns.wanted.get(tap) {
            w.store(true, Relaxed);
        }
    }

    /// Audio thread, after every reader said what it wants and before any
    /// stage runs: the wanted taps' rings are written this block (one that
    /// starts now starts silent), the others aren't.
    pub(crate) unsafe fn commit_returns(&self) {
        let r = &self.returns;
        for tap in 0..2 {
            let wanted = r.wanted[tap].swap(false, Relaxed);
            if wanted && !r.live[tap].load(Relaxed) {
                let ring = &*r.ring.get();
                ring.clear(2 * tap);
                ring.clear(2 * tap + 1);
            }
            r.live[tap].store(wanted, Relaxed);
        }
    }

    /// Owning task, after the strip ran: this block into the wanted return
    /// rings, at the return clock's position `pos`.
    pub(crate) unsafe fn write_returns(&self, pos: usize, n: usize) {
        let r = &self.returns;
        let ring = &*r.ring.get();
        let d = self.dsp();
        if r.live[return_tap::OUT].load(Relaxed) {
            ring.write(0, pos, d.out(0, n));
            ring.write(1, pos, d.out(1, n));
        }
        if r.live[return_tap::TAP].load(Relaxed) {
            ring.write(2, pos, d.tap_signal(0, n));
            ring.write(3, pos, d.tap_signal(1, n));
        }
    }

    /// Return tap `tap`, side `ch`, one block late (None unless wanted this block).
    pub(crate) unsafe fn read_return(&self, tap: usize, ch: usize, pos: usize, n: usize) -> Option<&[f32]> {
        let r = &self.returns;
        if !r.live.get(tap)?.load(Relaxed) {
            return None;
        }
        (*r.ring.get()).read(2 * tap + ch.min(1), pos, n)
    }

    /// # Safety
    /// The audio thread must not be able to reach this strip (device stopped or
    /// strip not yet published).
    pub(crate) unsafe fn prepare(&self, sample_rate: f64, max_block: usize) {
        self.pdc.drop_rings(); // sized for the old block size; the next plan rebuilds them
        self.keys.drop_all();
        self.taps.drop_all();
        if self.kind.is_output() {
            *self.returns.ring.get() = ReturnRing::new(4, max_block);
            for live in &self.returns.live {
                live.store(false, Relaxed);
            }
        }
        let d = &mut *self.dsp.get();
        d.sample_rate = sample_rate;
        d.max_block = max_block.max(1);
        let mb = d.max_block;
        for buffers in d.buffers_mut() {
            for b in buffers.iter_mut() {
                b.clear();
                b.resize(mb, 0.0); // zeroed: an idle strip relies on it
            }
        }
        d.key.clear();
        d.key.resize(mb, 0.0);
        d.listen_scratch.clear();
        d.listen_scratch.resize(mb, 0.0);
        d.idle = false;
        for b in self.params.own_blocks() {
            b.dsp_mut().prepare(sample_rate);
        }
        d.delay.prepare(sample_rate);
        d.listen_filter.reset();
        d.listen_state = None;
        d.tap_shaper.prepare(sample_rate);
        if let Some(layout) = self.layout() {
            for b in layout.blocks() {
                b.dsp_mut().prepare(sample_rate);
            }
        }
    }

    // ---- read-only views (valid after the strip's stage has run) ------------

    #[inline]
    pub(crate) unsafe fn dsp(&self) -> &StripDsp {
        &*self.dsp.get()
    }

    #[inline]
    #[allow(clippy::mut_from_ref)]
    pub(crate) unsafe fn dsp_mut(&self) -> &mut StripDsp {
        &mut *self.dsp.get()
    }

    /// Any thread: the send to main `i` (None: off; the engine creates the
    /// sends to the console's mains with the strip).
    #[inline]
    pub fn main_send(&self, i: usize) -> Option<&MainSendParams> {
        self.mains.find(i)
    }

    /// Any thread: the send to matrix `i` (None: off).
    #[inline]
    pub fn matrix_send(&self, i: usize) -> Option<&MatrixSendParams> {
        self.matrices.find(i)
    }

    /// Message thread: the strip's sends to `mains` mains and `matrices`
    /// matrices exist (the factory sends; existing ones are kept).
    pub fn create_sends(&self, mains: usize, matrices: usize) {
        for i in 0..mains {
            self.mains.get_or_create(i);
        }
        for i in 0..matrices {
            self.matrices.get_or_create(i);
        }
    }

    #[inline]
    #[allow(clippy::mut_from_ref)]
    pub(crate) unsafe fn last_solo(&self) -> &mut f32 {
        &mut *self.last_solo.get()
    }

    /// Channels processed in the last block (1 or 2). Racy read for the UI is fine.
    pub fn num_channels(&self) -> usize {
        self.meters.current_width.load(Relaxed).clamp(1, 2) as usize
    }

    /// True if the last block took the silent fast path (all outputs zero).
    pub fn is_idle(&self) -> bool {
        // SAFETY: a plain bool read; the UI tolerates a stale value.
        unsafe { std::ptr::read_volatile(&(*self.dsp.get()).idle) }
    }

    /// Effective mute: own mute, an engaged mute group or (with DCA MUTEGROUPS)
    /// a muted DCA.
    pub(crate) fn is_muted(&self, dcas: &[DcaParams], mute_groups: &GroupSet, dca_mute_groups: bool) -> bool {
        let p = &self.params;
        if p.mute.load(Relaxed) || p.mute_group_mask.load().intersects(mute_groups) {
            return true;
        }
        dca_mute_groups && Self::member_dcas(p, dcas).any(|d| d.mute.load(Relaxed))
    }

    /// The DCAs a strip is a member of.
    fn member_dcas<'a>(p: &StripParams, dcas: &'a [DcaParams]) -> impl Iterator<Item = &'a DcaParams> {
        p.dca_mask.load().iter().map_while(move |i| dcas.get(i))
    }

    /// Effective solo: own solo or a soloed DCA.
    pub(crate) fn is_soloed(&self, dcas: &[DcaParams]) -> bool {
        let p = &self.params;
        if p.solo.load(Relaxed) {
            return true;
        }
        Self::member_dcas(p, dcas).any(|d| d.solo.load(Relaxed))
    }

    /// SOLO MODE LIVE: the monitor paths (`solo::path_mask`) a soloed strip
    /// goes to. A soloed DCA sends its members to its own SOLO BUS (`mon`);
    /// the strip's own solo (and a talk or source solo) to the strip's.
    pub(crate) fn live_solo_paths(&self, dcas: &[DcaParams]) -> u32 {
        let p = &self.params;
        let from_dcas = Self::member_dcas(p, dcas)
            .filter(|d| d.solo.load(Relaxed))
            .fold(0, |paths, d| paths | crate::solo::path_mask(d.mon.load(Relaxed)));
        if p.solo.load(Relaxed) || from_dcas == 0 {
            from_dcas | crate::solo::path_mask(p.monitor_bus.load(Relaxed))
        } else {
            from_dcas
        }
    }

    fn dca_offset_db(&self, dcas: &[DcaParams]) -> f32 {
        Self::member_dcas(&self.params, dcas).map(|d| d.fader_db.load()).sum()
    }

    // ---- audio thread ---------------------------------------------------------

    /// Input channels: pull from their source (MAIN or ALT input: hardware
    /// inputs or playback channels).
    ///
    /// # Safety
    /// Owning task only. Non-null pointers must be valid for `n` samples.
    unsafe fn read_hardware_inputs(&self, d: &mut StripDsp, ctx: &ProcessContext, n: usize) {
        let p = &self.params;
        let (left, mut right, stereo) = match ctx.source_solo {
            Some((host, patch)) if std::ptr::eq(host, self) => (patch.left, patch.right, patch.stereo),
            _ => {
                let (l, r) = self.active_inputs();
                (l, r, p.stereo.load(Relaxed))
            }
        };
        d.num_channels = if stereo { 2 } else { 1 };
        d.widened = false;
        if d.num_channels == 2 && right < 0 && left >= 0 {
            right = left + 1; // stereo without an explicit right input: next channel
        }
        self.meters.current_width.store(d.num_channels as i32, Relaxed);

        let sources = [left, right];
        let source = |ch: usize| ctx.source(sources[ch]);

        // Already idle and still unpatched: buffers are still zero, nothing to do.
        if d.idle && (0..d.num_channels).all(|ch| source(ch).is_none()) {
            d.input_connected = false;
            return;
        }

        d.input_connected = false;
        for ch in 0..d.num_channels {
            let dst = &mut d.work[ch][..n];
            match source(ch) {
                Some(src) => {
                    dst.copy_from_slice(std::slice::from_raw_parts(src, n));
                    d.input_connected = true;
                }
                None => dst.fill(0.0),
            }
            let peak = peak_level(dst);
            self.meters.input_peak[ch].fetch_max(peak);
            self.remote_peaks.input[ch].fetch_max(peak);
        }
    }

    /// Summing buses: clear the work buffer before sums are accumulated into it.
    ///
    /// # Safety
    /// Owning task only.
    pub(crate) unsafe fn begin_summed_block(&self, n: usize) {
        let d = self.dsp_mut();
        d.num_channels = 2;
        d.widened = false;
        self.meters.current_width.store(2, Relaxed);
        for ch in 0..2 {
            d.work[ch][..n].fill(0.0);
        }
    }

    /// Runs the whole strip for one block.
    ///
    /// # Safety
    /// Owning task only; inputs are read from `ctx.hw_inputs`, summing buses must
    /// have accumulated their input with `begin_summed_block` + sums first.
    pub(crate) unsafe fn process(&self, ctx: &ProcessContext, n: usize) {
        let d = self.dsp_mut();
        debug_assert!(n <= d.max_block);

        if self.kind.is_input() {
            self.read_hardware_inputs(d, ctx, n);
            // Fast path: an unpatched input without inserts can only output
            // silence, so skip its DSP entirely (big consoles with unused channels).
            if !d.input_connected && !self.has_inserts() {
                if !d.idle {
                    // Clear once on entering idle; nothing writes the buffers after that.
                    for buffers in d.buffers_mut() {
                        buffers.iter_mut().for_each(|b| b.fill(0.0));
                    }
                }
                self.meters.gate_reduction_db.store(0.0);
                self.meters.dyn_reduction_db.store(0.0);
                self.band_meters.store(true, [0.0; DYN_BANDS]);
                self.band_meters.store(false, [0.0; DYN_BANDS]);
                self.meters.gate_active.store(false, Relaxed);
                self.meters.dyn_active.store(false, Relaxed);
                d.idle = true;
                self.keys.clear(ctx.block, n);
                // An analysed idle strip is silent (its buffers are zero).
                let probe = ctx.probes.of(self);
                if probe.rta.is_some() {
                    ctx.probes.push_rta([&d.work[0][..n], &d.work[1][..n]], 1, n);
                }
                if probe.scope {
                    ctx.probes.push_scope([&d.work[0][..n], &d.work[1][..n]], 1, n);
                }
                d.listen_state = None;
                return;
            }
        } else {
            for ch in 0..d.num_channels {
                let peak = peak_level(&d.work[ch][..n]);
                self.meters.input_peak[ch].fetch_max(peak);
                self.remote_peaks.input[ch].fetch_max(peak);
            }
        }

        if d.idle {
            d.reset_dsp(); // coming back from idle: no stale filter state
            self.reset_blocks();
            d.idle = false;
        }

        self.run_chain(d, ctx, n);

        // Never let NaN/inf escape a strip: it would poison every bus it feeds.
        let finite = (0..d.num_channels).all(|ch| all_finite(&d.work[ch][..n]) && all_finite(&d.post[ch][..n]));
        if !finite {
            for buffers in d.buffers_mut() {
                buffers.iter_mut().for_each(|b| b[..n].fill(0.0));
            }
            d.reset_dsp();
            self.reset_blocks();
            self.keys.clear(ctx.block, n);
            self.meters.dsp_faults.fetch_add(1, Relaxed);
        }

        self.run_output_stage(d, n);
    }

    /// Tap point `point`: the TAP-mode copy (if it is the strip's TAP point)
    /// and the key tap other strips' keys read (if any).
    unsafe fn tap_at(&self, d: &mut StripDsp, ctx: &ProcessContext, point: usize, n: usize) {
        // POST, after INS 2: with the automix gain of the last block (automix runs after stage 1).
        let gain = if point == tap::POST { d.automix_gain } else { 1.0 };
        self.keys.write(point, ctx.block, [&d.work[0][..n], &d.work[1][..n]], d.num_channels, gain);
        Self::copy_tap(d, point, n);
    }

    fn copy_tap(d: &mut StripDsp, point: usize, n: usize) {
        if d.tap_point == point {
            // A mono tap fills both sides: an insert after it may make the strip stereo.
            let nc = d.num_channels;
            let (work, tap) = (&d.work, &mut d.tap);
            for (ch, t) in tap.iter_mut().enumerate() {
                t[..n].copy_from_slice(&work[ch.min(nc - 1)][..n]);
            }
        }
    }

    /// The pre-fader blocks in processing order (the profile's: on the WING,
    /// input channels use their process order, aux channels run DYNAMICS, EQ,
    /// INS 1 (Fig. 68), and buses, mains and matrices DYNAMICS and INS 1, their
    /// EQ being post-fader).
    fn pre_fader_order(&self) -> &'static [u8] {
        self.spec.pre_fader_order(self.kind, self.params.process_order.load(Relaxed))
    }

    /// The block that process order slot `k` (`Node::Slot`) runs now.
    pub fn slot_block(&self, k: u8) -> Option<Node> {
        self.pre_fader_order().get(k as usize).and_then(|&b| chain::order_block(b))
    }

    /// The strip's chain (`chain.rs`): its profile's template for its kind.
    pub fn chain(&self) -> &'static [Node] {
        // Aux, buses, mains and matrices: `id` is the 0-based slot.
        self.spec.template_at(self.kind, usize::try_from(self.id).unwrap_or(0))
    }

    /// The console's profile when the strip was made.
    pub fn profile(&self) -> Profile {
        self.spec.profile
    }

    /// What the strip's console is (its profile's spec: chains, laws).
    pub fn spec(&self) -> &'static ProfileSpec {
        self.spec
    }

    unsafe fn run_chain(&self, d: &mut StripDsp, ctx: &ProcessContext, n: usize) {
        d.tap_point = if self.has_selectable_tap() { self.params.tap.load(Relaxed).clamp(0, 7) as usize } else { tap::PFL };
        let probe = ctx.probes.of(self);
        let mut listened = false;
        if d.listen_state != probe.listen {
            d.listen_filter.reset(); // a new LISTEN source (faded in) starts clean
            d.listen_fresh = true;
        }
        match self.layout() {
            Some(layout) => {
                for step in layout.steps() {
                    match step {
                        Step::Node(node) => self.run_template_node(*node, d, ctx, &probe, &mut listened, n),
                        Step::Block(b) => self.run_block(b, d, ctx, n),
                    }
                }
            }
            None => {
                for &node in self.chain() {
                    self.run_template_node(node, d, ctx, &probe, &mut listened, n);
                }
            }
        }

        // LISTEN with nothing to play (no such block on this strip): silence.
        if probe.listen.is_some() && !listened {
            d.listen.iter_mut().for_each(|b| b[..n].fill(0.0));
            d.listen_channels = 1;
        }
        d.listen_state = probe.listen;
    }

    /// A node of the template, its process order `Slot`s resolved.
    unsafe fn run_template_node(
        &self,
        node: Node,
        d: &mut StripDsp,
        ctx: &ProcessContext,
        probe: &StripProbe,
        listened: &mut bool,
        n: usize,
    ) {
        let node = match node {
            Node::Slot(k) => match self.slot_block(k) {
                Some(block) => block,
                None => return,
            },
            other => other,
        };
        self.run_node(node, d, ctx, probe, listened, n);
    }

    /// An extra block (`blocks.rs`).
    unsafe fn run_block(&self, b: &Block, d: &mut StripDsp, ctx: &ProcessContext, n: usize) {
        let nc = d.num_channels;
        match (&b.params, b.dsp_mut()) {
            (BlockParams::Filter(p), BlockDsp::Filter(f)) => {
                let settings = p.snapshot();
                if settings.is_active() {
                    let [w0, w1] = &mut d.work;
                    let mut chans: [&mut [f32]; 2] = [&mut w0[..n], &mut w1[..n]];
                    f.process(&mut chans[..nc], &settings);
                }
            }
            (BlockParams::Eq(p), BlockDsp::Eq(eq, values)) => {
                p.values_snapshot(values);
                let (table, model) = (p.table(), p.model_index());
                let [w0, w1] = &mut d.work;
                let mut chans: [&mut [f32]; 2] = [&mut w0[..n], &mut w1[..n]];
                eq.process(&mut chans[..nc], table, model, p.on.load(Relaxed), p.mix.load(), &values[..]);
            }
            (BlockParams::Dynamics(p), BlockDsp::Dynamics(dynamics, values)) => {
                dynamics.set_table(p.table());
                let on = p.on.load(Relaxed);
                if !dynamics.is_running(on) {
                    b.meters.reduction_db.store(0.0);
                    b.meters.active.store(false, Relaxed);
                    return;
                }
                p.values_snapshot(values);
                let external = self.key_for(d, ctx, p, n);
                let [w0, w1] = &mut d.work;
                let mut chans: [&mut [f32]; 2] = [&mut w0[..n], &mut w1[..n]];
                let key = if external { Some(&d.key[..n]) } else { None };
                let status = dynamics.process_slot(
                    &mut chans[..nc],
                    key,
                    p.model_index(),
                    &values[..],
                    &p.sidechain(),
                    &p.xover(),
                    p.mix.load() / 100.0,
                    p.gain.load(),
                    on,
                );
                b.meters.reduction_db.store(if on { status.gain_reduction_db } else { 0.0 });
                b.meters.active.store(on && status.active, Relaxed);
            }
            (BlockParams::Insert(p), _) => self.run_insert_params(d, ctx, n, p),
            (BlockParams::Delay(p), BlockDsp::Delay(line)) if p.on.load(Relaxed) => {
                let samples = delay_samples(p.delay.load(), p.mode.load(Relaxed).clamp(0, 3) as usize, d.sample_rate);
                let [w0, w1] = &mut d.work;
                let mut chans: [&mut [f32]; 2] = [&mut w0[..n], &mut w1[..n]];
                line.process(&mut chans[..nc], samples);
            }
            _ => {}
        }
        let nc = d.num_channels;
        let peak = (0..nc).map(|ch| peak_level(&d.work[ch][..n])).fold(0.0, f32::max);
        b.meters.out_peak.fetch_max(peak);
    }

    /// Resets the blocks' DSP state, the strip's own and the extra ones
    /// (coming back from idle, after a fault).
    ///
    /// # Safety
    /// The task running the strip only.
    unsafe fn reset_blocks(&self) {
        for b in self.params.own_blocks() {
            b.dsp_mut().reset();
        }
        if let Some(layout) = self.layout() {
            for b in layout.blocks() {
                b.dsp_mut().reset();
            }
        }
    }

    /// One node of the chain. `d.num_channels` is read at each node: an
    /// insert may make a mono strip stereo from there on.
    unsafe fn run_node(
        &self,
        node: Node,
        d: &mut StripDsp,
        ctx: &ProcessContext,
        probe: &StripProbe,
        listened: &mut bool,
        n: usize,
    ) {
        let p = &self.params;
        let nc = d.num_channels;
        match node {
            Node::Input => {
                // INPUT: trim, balance, polarity
                let polarity = if p.input.invert.load(Relaxed) { -1.0 } else { 1.0 };
                let trim = db_to_gain(p.input.trim_db.load()) * polarity;
                let bal = if nc == 2 { p.input.balance_db.load() } else { 0.0 };
                let gains = [trim * db_to_gain(-bal.max(0.0)), trim * db_to_gain(bal.min(0.0))];
                for (ch, &gain) in gains.iter().enumerate().take(nc) {
                    apply_gain_ramp(&mut d.work[ch][..n], d.last_in[ch], gain);
                    let peak = peak_level(&d.work[ch][..n]);
                    self.meters.trim_peak[ch].fetch_max(peak);
                    self.remote_peaks.pre[ch].fetch_max(peak);
                }
                d.last_in = gains;
                // The LR VECTOR SCOPE: after the INPUT block, before pan.
                if probe.scope {
                    ctx.probes.push_scope([&d.work[0][..n], &d.work[1][..n]], nc, n);
                }
            }
            Node::Delay => self.apply_delay(d, n),
            Node::Filter => {
                let settings = p.filter().snapshot();
                if settings.is_active() {
                    let [w0, w1] = &mut d.work;
                    let mut chans: [&mut [f32]; 2] = [&mut w0[..n], &mut w1[..n]];
                    if let BlockDsp::Filter(f) = self.params.own_block(OwnBlocks::FILTER).dsp_mut() {
                        f.process(&mut chans[..nc], &settings);
                    }
                }
            }
            Node::Gate => {
                if let Some((listen_mode::GATE_KEY, _)) = probe.listen {
                    *listened = self.listen_key(d, ctx, p.gate(), n);
                }
                self.run_gate(d, ctx, n)
            }
            Node::Eq => self.run_eq_probed(d, ctx, probe, listened, n),
            Node::Dynamics => {
                match probe.listen {
                    Some((listen_mode::DYN_KEY, _)) => *listened = self.listen_key(d, ctx, p.dynamics(), n),
                    Some((listen_mode::XOVER, _)) => {
                        let c = p.dynamics().xover().coefficients(d.sample_rate);
                        *listened = Self::listen_filtered(d, c, None, n);
                    }
                    _ => {}
                }
                self.run_dynamics(d, ctx, n)
            }
            Node::PreInsert => self.run_insert(d, ctx, n, false),
            Node::PostInsert => {
                // INS 2 (automix is applied by the engine after all channels ran; it
                // follows the post-fader level, so the fader shapes the automix response).
                if self.post_insert_fx().is_some() {
                    self.run_insert(d, ctx, n, true);
                }
                if self.automix_group().is_some() {
                    let nc = d.num_channels;
                    let power: f32 =
                        (0..nc).map(|ch| d.work[ch][..n].iter().map(|x| x * x).sum::<f32>()).sum::<f32>() / (n * nc) as f32;
                    let c = 0.7;
                    d.automix_power = c * d.automix_power + (1.0 - c) * power * db_to_gain(p.post_insert().weight.load() * 2.0);
                }
            }
            Node::Pfl => {
                // PFL: pre-fader (solo, direct outs, the METERS screen).
                for ch in 0..nc {
                    d.pfl[ch][..n].copy_from_slice(&d.work[ch][..n]);
                    self.meters.pfl_peak[ch].fetch_max(peak_level(&d.pfl[ch][..n]));
                }
            }
            Node::Fader => {
                // Fader with DCAs, mute and mute groups (and Main 1's level on linked mains);
                // the console decides over that mute (SIP, OVERRIDE, TALK ON: solo.rs).
                let muted = self.console.apply_mute(self.is_muted(ctx.dcas, &ctx.mute_groups, ctx.dca_mute_groups));
                d.muted = muted;
                let mut level = p.fader_db.load() + self.dca_offset_db(ctx.dcas);
                if self.kind == StripKind::Main && (1..=ctx.main_link_last).contains(&(self.id.max(0) as usize)) {
                    level += ctx.main_link_db;
                }
                let gain = if muted { 0.0 } else { db_to_gain(level) };
                for ch in 0..nc {
                    apply_gain_ramp(&mut d.work[ch][..n], d.last_gain, gain);
                }
                d.last_gain = gain;
            }
            Node::Post => {
                for ch in 0..nc {
                    d.post[ch][..n].copy_from_slice(&d.work[ch][..n]);
                }
            }
            Node::TapShaper => {
                // TAP EQ and TAP WIDTH on what TAP-mode sends take.
                let [t0, t1] = &mut d.tap;
                let mut chans: [&mut [f32]; 2] = [&mut t0[..n], &mut t1[..n]];
                d.tap_shaper.process(&mut chans[..nc], p);
            }
            Node::Tap(id) if id >= FIRST_USER_TAP => {
                self.taps.write(id as usize, ctx.block, [&d.work[0][..n], &d.work[1][..n]], nc, n)
            }
            Node::Tap(point) => {
                self.tap_at(d, ctx, point as usize, n);
                // A send that takes this point (an X32's IN/LC, <-EQ, EQ->).
                self.taps.write(point as usize, ctx.block, [&d.work[0][..n], &d.work[1][..n]], nc, n)
            }
            Node::Rta(at) => Self::probe_rta(d, ctx, probe, at as usize, n),
            Node::Slot(_) => {}
        }
    }

    /// The RTA signal, if this strip is analysed at `at`.
    #[inline]
    fn probe_rta(d: &StripDsp, ctx: &ProcessContext, probe: &StripProbe, at: usize, n: usize) {
        if probe.rta == Some(at) {
            ctx.probes.push_rta([&d.work[0][..n], &d.work[1][..n]], d.num_channels, n);
        }
    }

    /// The EQ with its RTA taps (PRE / POST) and BAND SOLO.
    fn run_eq_probed(&self, d: &mut StripDsp, ctx: &ProcessContext, probe: &StripProbe, listened: &mut bool, n: usize) {
        Self::probe_rta(d, ctx, probe, rta_tap::PRE_EQ, n);
        if let Some((listen_mode::BAND, band)) = probe.listen {
            let eq = self.params.eq();
            // SAFETY: the task running the strip.
            let BlockDsp::Eq(_, values) = (unsafe { self.params.own_block(OwnBlocks::EQ).dsp_mut() }) else { return };
            eq.values_snapshot(values);
            let (table, model) = (eq.table(), eq.model_index());
            let coeffs = crate::listen::band_solo_coeffs(&table[model], values, band.max(0) as usize, d.sample_rate);
            if let Some(c) = coeffs {
                *listened = Self::listen_filtered(d, c, None, n);
            }
        }
        self.run_eq(d, n);
        Self::probe_rta(d, ctx, probe, rta_tap::POST_EQ, n);
    }

    /// KEY SOLO: the slot's key (another channel's input, or the signal
    /// itself) through the key filter.
    unsafe fn listen_key(&self, d: &mut StripDsp, ctx: &ProcessContext, slot: &DynSlotParams, n: usize) -> bool {
        let c = slot.sidechain().coefficients(d.sample_rate);
        let external = self.key_for(d, ctx, slot, n);
        Self::listen_filtered(d, c, external.then_some(()), n)
    }

    /// Fills the LISTEN buffers with the work signal (or, with `key`, the
    /// mono key buffer) through the filter `c`.
    fn listen_filtered(d: &mut StripDsp, c: BiquadCoeffs, key: Option<()>, n: usize) -> bool {
        // A changed filter (a knob moved, another crossover mode) crossfades
        // over the block from the old filter, so it can't click.
        let fade = d.listen_coeffs != c && !d.listen_fresh;
        if d.listen_coeffs != c {
            if fade {
                d.listen_prev = d.listen_filter.clone();
            }
            d.listen_filter.set_coefficients(c);
            d.listen_coeffs = c;
        }
        d.listen_fresh = false;
        let channels = if key.is_some() { 1 } else { d.num_channels };
        for ch in 0..channels {
            let src = if key.is_some() { &d.key[..n] } else { &d.work[ch][..n] };
            d.listen[ch][..n].copy_from_slice(src);
            d.listen_filter.process(&mut d.listen[ch][..n], ch);
            if fade {
                d.listen_scratch[..n].copy_from_slice(src);
                d.listen_prev.process(&mut d.listen_scratch[..n], ch);
                let step = 1.0 / n.max(1) as f32;
                for (i, (new, old)) in d.listen[ch][..n].iter_mut().zip(&d.listen_scratch[..n]).enumerate() {
                    *new = old + (*new - old) * (i + 1) as f32 * step;
                }
            }
        }
        d.listen_channels = channels;
        true
    }

    fn apply_delay(&self, d: &mut StripDsp, n: usize) {
        let p = &self.params.input;
        if !p.delay_on.load(Relaxed) {
            return;
        }
        // As the WING: 0.3 m per ms, at least 16 samples.
        let samples = delay_samples(p.delay.load(), p.delay_mode.load(Relaxed).clamp(0, 3) as usize, d.sample_rate);
        let nc = d.num_channels;
        let [w0, w1] = &mut d.work;
        let mut chans: [&mut [f32]; 2] = [&mut w0[..n], &mut w1[..n]];
        d.delay.process(&mut chans[..nc], samples);
    }

    /// Key signal for a dynamics slot into `d.key`: another channel's signal
    /// at the slot's KEY TAP (`keytap.rs`), or false (the strip keys itself).
    unsafe fn key_for(&self, d: &mut StripDsp, ctx: &ProcessContext, slot: &DynSlotParams, n: usize) -> bool {
        let (source, buffer) = key_tap(slot);
        let source = match source {
            KeySource::SelfKey => return false,
            KeySource::Channel(i) => i,
            KeySource::Strip(kind, i) => {
                // An aux, bus, main or matrix: this block's tap from an earlier
                // stage, else the previous block's; silence until it has a buffer.
                let Some(s) = ctx.key_strips.get(kind, i) else { return false };
                let current = ctx.key_strips.stage(kind, s.id) < ctx.key_strips.stage(self.kind, self.id);
                if !s.keys.read(buffer, ctx.block, current, &mut d.key[..n]) {
                    d.key[..n].fill(0.0);
                }
                return true;
            }
        };
        let Some(ch) = ctx.channels.get(source) else { return false };
        if ch.keys.read(buffer, ctx.block, ctx.key_is_current(source), &mut d.key[..n]) {
            return true;
        }
        // The tap's buffer isn't planned yet (`Engine::update_keys`): the channel's input.
        let ptr = ctx.source(ch.active_inputs().0);
        match ptr {
            Some(src) => d.key[..n].copy_from_slice(std::slice::from_raw_parts(src, n)),
            None => d.key[..n].fill(0.0),
        }
        true
    }

    unsafe fn run_gate(&self, d: &mut StripDsp, ctx: &ProcessContext, n: usize) {
        self.run_dyn_slot(d, ctx, n, true);
    }

    unsafe fn run_dynamics(&self, d: &mut StripDsp, ctx: &ProcessContext, n: usize) {
        self.run_dyn_slot(d, ctx, n, false);
    }

    /// The GATE or DYNAMICS slot. Switching it ON or OFF crossfades (the slot
    /// keeps running for the 5 ms fade after OFF).
    unsafe fn run_dyn_slot(&self, d: &mut StripDsp, ctx: &ProcessContext, n: usize, gate: bool) {
        let slot = if gate { self.params.gate() } else { self.params.dynamics() };
        let m = &self.meters;
        let (reduction, active, key_db, out_peak) = if gate {
            (&m.gate_reduction_db, &m.gate_active, &m.gate_key_db, &m.gate_out_peak)
        } else {
            (&m.dyn_reduction_db, &m.dyn_active, &m.dyn_key_db, &m.dyn_out_peak)
        };
        let on = slot.on.load(Relaxed);
        let BlockDsp::Dynamics(proc, values) =
            self.params.own_block(if gate { OwnBlocks::GATE } else { OwnBlocks::DYNAMICS }).dsp_mut()
        else {
            return;
        };
        proc.set_table(slot.table()); // its console's models
        let running = proc.is_running(on);
        if !running {
            reduction.store(0.0);
            self.band_meters.store(gate, [0.0; DYN_BANDS]);
            active.store(false, Relaxed);
            key_db.store(-120.0);
            return;
        }
        slot.values_snapshot(values);
        let external = self.key_for(d, ctx, slot, n);
        let nc = d.num_channels;
        let [w0, w1] = &mut d.work;
        let mut chans: [&mut [f32]; 2] = [&mut w0[..n], &mut w1[..n]];
        let key = if external { Some(&d.key[..n]) } else { None };
        let status: DynStatus = proc.process_slot(
            &mut chans[..nc],
            key,
            slot.model_index(),
            &values[..],
            &slot.sidechain(),
            &slot.xover(),
            slot.mix.load() / 100.0,
            slot.gain.load(),
            on,
        );
        reduction.store(if on { status.gain_reduction_db } else { 0.0 });
        self.band_meters.store(gate, if on { status.band_gain_db } else { [0.0; DYN_BANDS] });
        active.store(on && status.active, Relaxed);
        key_db.store(status.key_db);
        out_peak.fetch_max(status.out_peak);
    }

    fn run_eq(&self, d: &mut StripDsp, n: usize) {
        let eq = self.params.eq();
        // SAFETY: the task running the strip.
        let BlockDsp::Eq(proc, values) = (unsafe { self.params.own_block(OwnBlocks::EQ).dsp_mut() }) else { return };
        eq.values_snapshot(values);
        let (table, model) = (eq.table(), eq.model_index());
        let nc = d.num_channels;
        let [w0, w1] = &mut d.work;
        let mut chans: [&mut [f32]; 2] = [&mut w0[..n], &mut w1[..n]];
        proc.process(&mut chans[..nc], table, model, eq.on.load(Relaxed), eq.mix.load(), &values[..]);
    }

    unsafe fn run_insert(&self, d: &mut StripDsp, ctx: &ProcessContext, n: usize, post: bool) {
        let ins = if post { self.params.post_insert() } else { self.params.pre_insert() };
        self.run_insert_params(d, ctx, n, ins);
    }

    /// An insert: the FX slot it names, when it is on.
    unsafe fn run_insert_params(&self, d: &mut StripDsp, ctx: &ProcessContext, n: usize, ins: &InsertParams) {
        if !ins.on.load(Relaxed) {
            return;
        }
        let slot = ins.fx.load(Relaxed);
        let (Some(rack), Some(fxp)) = (ctx.fx.get(slot.max(0) as usize), ctx.fx_params.get(slot.max(0) as usize)) else { return };
        // A send effect (fed from a bus) isn't an insert: the strip passes through.
        if slot < 0 || fxp.is_send() {
            return;
        }
        // A mono strip feeds both of the FX's inputs and carries its stereo
        // output from here on, each side at the mono pan law. Measured on a
        // WING: a plate inserted on a mono channel comes out decorrelated
        // (L/R correlation 0.02), at the level of one side panned centre.
        if d.num_channels == 1 {
            let [w0, w1] = &mut d.work;
            w1[..n].copy_from_slice(&w0[..n]);
            d.num_channels = 2;
            d.widened = true;
        }
        let [w0, w1] = &mut d.work;
        let mut chans: [&mut [f32]; 2] = [&mut w0[..n], &mut w1[..n]];
        // An outboard insert's RETURN reads the block's processed sources.
        let inputs = FxInputs { hw: ctx.hw_inputs, playback: ctx.playback };
        rack.process(&mut chans, d.sample_rate, fxp, &inputs, ctx.block);
    }

    fn run_output_stage(&self, d: &mut StripDsp, n: usize) {
        let p = &self.params;
        let nc = d.num_channels;
        let pan = p.pan.load() / 100.0;

        let target = if nc == 1 || d.widened {
            let (l, mut r) = (self.spec.pan.mono)(pan);
            // MS decoding's S: its right side reversed.
            if nc == 1 && p.ms_side.load(Relaxed) {
                r = -r;
            }
            d.pan_gains = [l, r];
            if nc == 1 {
                [l, 0.0, r, 0.0]
            } else {
                [l, 0.0, 0.0, r]
            }
        } else {
            let (gl, gr) = (self.spec.pan.stereo)(pan);
            d.pan_gains = [gl, gr];
            let w = (p.width.load() / 100.0).clamp(-1.5, 1.5);
            let (same, cross) = ((1.0 + w) * 0.5, (1.0 - w) * 0.5);
            [gl * same, gl * cross, gr * cross, gr * same]
        };
        let mono = self.kind.is_output() && p.mono.load(Relaxed);

        let last = d.out_matrix;
        let right = if nc > 1 { 1 } else { 0 };
        let [o0, o1] = &mut d.out;
        let (o0, o1) = (&mut o0[..n], &mut o1[..n]);
        o0.fill(0.0);
        o1.fill(0.0);
        add_with_gain_ramp(o0, &d.post[0][..n], last[0], target[0]);
        add_with_gain_ramp(o1, &d.post[0][..n], last[2], target[2]);
        if nc > 1 {
            add_with_gain_ramp(o0, &d.post[right][..n], last[1], target[1]);
            add_with_gain_ramp(o1, &d.post[right][..n], last[3], target[3]);
        }
        d.out_matrix = target;
        if mono {
            for (l, r) in o0.iter_mut().zip(o1.iter_mut()) {
                let m = (*l + *r) * 0.5;
                *l = m;
                *r = m;
            }
        }

        let (p0, p1) = (peak_level(o0), peak_level(o1));
        self.meters.output_peak[0].fetch_max(p0);
        self.meters.output_peak[1].fetch_max(p1);
        self.remote_peaks.output[0].fetch_max(p0);
        self.remote_peaks.output[1].fetch_max(p1);
    }

    /// Engine automix pass: scales everything after INS 2 (where automix sits)
    /// by `gain`; the POST FDR tap, before INS 2, keeps the plain fader level.
    ///
    /// # Safety
    /// Audio thread, after the input stage, before anything reads this strip.
    pub(crate) unsafe fn apply_automix(&self, gain: f32, n: usize) {
        let d = self.dsp_mut();
        let last = d.automix_gain;
        let nc = d.num_channels;
        for ch in 0..nc {
            apply_gain_ramp(&mut d.post[ch][..n], last, gain);
            if d.tap_point == tap::POST {
                apply_gain_ramp(&mut d.tap[ch][..n], last, gain);
            }
        }
        for ch in 0..2 {
            apply_gain_ramp(&mut d.out[ch][..n], last, gain);
        }
        d.automix_gain = gain;
        self.meters.automix_db.store(gain_to_db(gain));
    }
}

/// The buffer of input source `index`: hardware input `index`, playback
/// channel `index - PLAYBACK_SOURCE_BASE`, USB PLAYER channel `index -
/// USB_PLAYER_SOURCE_BASE`, oscillator `index - OSC_SOURCE_BASE`, USR
/// source `index - USR_SOURCE_BASE`, strip source `index -
/// STRIP_SOURCE_BASE` or FX return `index - FX_RETURN_SOURCE_BASE` (in
/// `playback`, laid out as `sources::internal`).
/// None = not connected.
pub(crate) fn source_ptr(hw: &[*const f32], playback: &[*const f32], index: i32) -> Option<*const f32> {
    let p = if let Some(n) = playback_channel(index) {
        playback.get(internal::PLAYBACK + n)
    } else if let Some(n) = usb_player_channel(index) {
        playback.get(internal::USB + n)
    } else if let Some(n) = osc_channel(index) {
        playback.get(internal::OSC + n)
    } else if let Some(n) = usr_channel(index) {
        playback.get(internal::USR + n)
    } else if let Some(n) = strip_source_slot(index) {
        playback.get(internal::STRIP + n)
    } else if let Some(n) = fx_return_channel(index) {
        playback.get(internal::FX + n)
    } else {
        hw.get(usize::try_from(index).ok()?)
    };
    p.copied().filter(|p| !p.is_null())
}

/// Read access to a processed strip's signal taps (valid after its stage).
impl StripDsp {
    #[inline]
    pub(crate) fn channels(&self) -> usize {
        self.num_channels
    }
    /// A mono source (pans with the mono pan law), also when an insert made it stereo.
    #[inline]
    pub(crate) fn mono_source(&self) -> bool {
        self.num_channels == 1 || self.widened
    }
    #[inline]
    pub(crate) fn idle(&self) -> bool {
        self.idle
    }
    /// Right-channel index for this strip's width (mono strips repeat the left).
    #[inline]
    fn rc(&self, ch: usize) -> usize {
        if self.num_channels > 1 {
            ch.min(1)
        } else {
            0
        }
    }
    #[inline]
    pub(crate) fn tap_signal(&self, ch: usize, n: usize) -> &[f32] {
        &self.tap[self.rc(ch)][..n]
    }
    #[inline]
    pub(crate) fn pfl(&self, ch: usize, n: usize) -> &[f32] {
        &self.pfl[self.rc(ch)][..n]
    }
    #[inline]
    pub(crate) fn post(&self, ch: usize, n: usize) -> &[f32] {
        &self.post[self.rc(ch)][..n]
    }
    #[inline]
    pub(crate) fn out(&self, ch: usize, n: usize) -> &[f32] {
        &self.out[ch.min(1)][..n]
    }
    #[inline]
    pub(crate) fn pan_gains(&self) -> [f32; 2] {
        self.pan_gains
    }
    #[inline]
    pub(crate) fn automix_power(&self) -> f32 {
        self.automix_power
    }
    #[inline]
    pub(crate) fn automix_gain(&self) -> f32 {
        self.automix_gain
    }
    /// The strip was muted (own mute, mute group, DCA) in this block.
    #[inline]
    pub(crate) fn muted(&self) -> bool {
        self.muted
    }
    /// The LISTEN signal (a mono one on both sides).
    #[inline]
    pub(crate) fn listen(&self, ch: usize, n: usize) -> &[f32] {
        &self.listen[if self.listen_channels > 1 { ch.min(1) } else { 0 }][..n]
    }
    #[inline]
    pub(crate) fn last_direct(&self) -> f32 {
        self.last_direct
    }
    #[inline]
    pub(crate) fn set_last_direct(&mut self, g: f32) {
        self.last_direct = g;
    }
    #[inline]
    pub(crate) fn work_mut(&mut self, ch: usize, n: usize) -> &mut [f32] {
        &mut self.work[ch][..n]
    }
}
