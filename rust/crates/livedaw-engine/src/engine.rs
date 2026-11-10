//! The live mixing engine, laid out like a WING: input channels and aux
//! channels feed 16 (or more) stereo buses, 4 stereo mains and 8 stereo
//! matrices; DCAs and mute groups act on any strip; 16 FX slots serve the
//! strips' insert points.
//!
//! Per audio block:
//!  1. channels + aux channels read their inputs and run their chains (parallel;
//!     in waves when keys come from other channels: `keytap.rs`)
//!  2. automix (gain sharing between channels in automix groups X/Y), and a
//!     dLive's AMM (`amm.rs`)
//!  3. (and 4.) buses sum the input sends and the other buses' sends: in
//!     waves, a bus after the buses feeding it (`routing.rs`)           (parallel)
//!  5. mains sum channel/aux/bus main sends                            (parallel)
//!  6. matrices sum matrix sends from all strips + their DIRECT IN     (parallel)
//!     then the user signals (USR 1-24) copy their strips
//!  7. solo (PFL/AFL per strip type, routed by the SOLO MODE) builds the solo
//!     buses of the two monitor paths (SPEAKERS and PHONES), each with
//!     its SOURCE (Main 1 by default) and DIRECT IN; then each path's EQ,
//!     PAN / WIDTH, INVERT, DELAY, level, DIM, mute, MONO, SWAP and LIMITER
//!     (`monitor.rs`)
//!  8. outputs are written (strips, monitor paths, the sources patched to
//!     outputs), then safety-clipped
//!
//! Signals from later in the chain are read one block late through return
//! rings (`routing.rs`): a bus, main or matrix as a channel's source, a user
//! signal patched to a channel, a bus send that closes a loop, a matrix's
//! DIRECT IN from the monitor paths. So the processing order never has a
//! cycle.
//!
//! Plugin delay compensation (`pdc.rs`): each summing stage reads its sources
//! delayed so that every path arrives in time; input channels write their
//! compensation rings after stage 2, the others right after they ran.
//!
//! Before stage 1, the sources (`sources.rs`) process the input ports the
//! channels read, and the console works out the block's solos and mutes
//! (`solo.rs`: SIP, MUTEGROUP/SIP OVERRIDE, talk tags) and its talkback
//! (`talk.rs`), which the bus, main and matrix stages add to their sums.
//!
//! The recorder (`livedaw-recorder`) runs around every chunk: it may shorten
//! the chunk (loop end, playback ring switch) so each chunk is contiguous on
//! the timeline, fills the 64 playback channels (input sources
//! `PLAYBACK_SOURCE_BASE + n`, like hardware inputs) before stage 1, records
//! hardware inputs at the start of the chunk and strip taps after stage 6.
//! The USB recorder / player (`livedaw_recorder::usb`) runs inside the same
//! chunks: its four player channels are sources (USB PLAYER) like the
//! playback channels, and its four RECORDER ports tap strips or sources
//! after stage 6 (the "USB deck hooks" below).
//!
//! Structure changes happen on the message thread by publishing a new
//! immutable `Topology`; the old one is retired to the garbage collector, which
//! drops it (and any removed strips) once the audio thread is done with it.

use crate::amm::{AmmParams, AmmState};
use crate::analyser::{rta_tap, Analyser, Probes};
use crate::atomic::AtomicF32;
use crate::blocks::{Block, BlockKind, Step, MAX_EXTRA_BLOCKS};
use crate::chain::Node;
use crate::config::{ConsoleConfig, Counts, MAX_AUX};
use crate::denormals::ScopedNoDenormals;
use crate::fx::{FxInputs, FxRack, InsertCallbacks};
use crate::garbage::Garbage;
use crate::groups::{AtomicGroupSet, GroupSet};
use crate::iosafes::IoSafes;
use crate::keytap::{key_sources, key_tap, KeySchedule, KeySource};
use crate::monitor::{direct_source, MonitorPathDsp, SoloPatch, BAND_SOLO_MAX_DB};
use crate::oscillator::ConsoleOscillator;
use crate::params::*;
use crate::pdc::{pdc_tap, PdcState, MAX_PDC};
use crate::pool::WorkerPool;
use crate::profile::{MainMode, Profile};
use crate::routing::{bus_feeds_bus, matrix_direct_pair, return_tap, strip_source, BusSchedule, OutputPatch, ReturnClock};
use crate::routing::{DirectSource, ReturnRing, DIRECT_SOURCE_BASE, NUM_PATCH_OUTPUTS, NUM_STRIP_SOURCES, STRIP_SOURCE_BASE};
use crate::sends::{send_mode, MAX_BUSES};
use crate::solo::{self, solo_mode, SoloBlock};
use crate::sources::{user_side, SourceDsp, SourceTable, UserSignalParams, NUM_SEND_FX};
use crate::strip::{source_ptr, KeyStrips, ProcessContext, Strip};
use crate::talk::{TalkBlock, TalkGains, TalkRamps};
use crate::taps::{send_tap, FIRST_USER_TAP, NUM_TAP_IDS};
use livedaw_dsp::model::ModelSpec;
use livedaw_dsp::util::*;
use livedaw_dsp::wing::fx::{allowed_in, FxContext, FxDevice};
use livedaw_recorder::usb::{RecTap, UsbDeck, USB_CHANNELS};
use livedaw_recorder::{playback_channel, usb_player_channel, RecordStream, RecordTrackSpec, Recorder, SourceKind};
use std::cell::UnsafeCell;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicPtr, AtomicU32, AtomicUsize, Ordering::*};
use std::sync::{Arc, Mutex, MutexGuard};
use std::time::Instant;

#[derive(Default)]
struct Topology {
    channels: Vec<Arc<Strip>>,
    auxes: Vec<Arc<Strip>>,
    /// Audio thread scratch: the non-idle inputs of the current block (capacity
    /// reserved at publish, so pushing never allocates).
    active_inputs: UnsafeCell<Vec<Arc<Strip>>>,
    /// Audio thread scratch: stage 1's waves by key dependency (`keytap.rs`).
    key_schedule: UnsafeCell<KeySchedule>,
    /// The buses by slot.
    bus_list: Vec<Arc<Strip>>,
    /// Audio thread scratch: the bus stage's waves by bus-to-bus sends (`routing.rs`).
    bus_schedule: UnsafeCell<BusSchedule>,
    mains: Vec<Arc<Strip>>,
    matrices: Vec<Arc<Strip>>,
}

// SAFETY: `active_inputs`, `key_schedule` and `bus_schedule` are only touched
// by the audio thread (between stages, never while tasks write them);
// everything else is immutable after publishing.
unsafe impl Sync for Topology {}
unsafe impl Send for Topology {}

impl Topology {
    fn inputs(&self) -> impl Iterator<Item = &Arc<Strip>> + Clone {
        self.channels.iter().chain(&self.auxes)
    }

    /// Inputs that produced signal this block (valid after stage 1).
    unsafe fn active(&self) -> &[Arc<Strip>] {
        &*self.active_inputs.get()
    }
    fn buses(&self) -> impl Iterator<Item = &Arc<Strip>> + Clone {
        self.bus_list.iter()
    }
    fn all(&self) -> impl Iterator<Item = &Arc<Strip>> + Clone {
        self.inputs().chain(self.buses()).chain(&self.mains).chain(&self.matrices)
    }

    /// A strip by kind and number as the console shows it ("CH 3", "BUS 12").
    fn by_number(&self, kind: StripKind, number: i32) -> Option<&Arc<Strip>> {
        let i = usize::try_from(number.checked_sub(1)?).ok()?;
        match kind {
            StripKind::Channel => self.channels.get(i),
            StripKind::Aux => self.auxes.get(i),
            StripKind::Bus => self.buses().find(|b| b.id() as usize == i),
            StripKind::Main => self.mains.get(i),
            StripKind::Matrix => self.matrices.get(i),
        }
    }
}

#[derive(Clone, Copy, Debug)]
struct DeviceConfig {
    sample_rate: f64,
    block_size: usize,
    num_hw_inputs: usize,
}

struct Structure {
    channels: Vec<Arc<Strip>>,
    auxes: Vec<Arc<Strip>>,
    buses: Vec<Arc<Strip>>,
    mains: Vec<Arc<Strip>>,
    matrices: Vec<Arc<Strip>>,
    next_channel_id: i32,
    device: DeviceConfig,
    /// The configured counts (`Engine::configure`); a reset builds them.
    counts: Counts,
    /// The console emulated (`ConsoleConfig::profile`): the Universal once
    /// the counts leave the profile's (`set_num_strips`).
    profile: Profile,
}

impl Structure {
    fn all(&self) -> impl Iterator<Item = &Arc<Strip>> {
        self.channels.iter().chain(&self.auxes).chain(&self.buses).chain(&self.mains).chain(&self.matrices)
    }

    fn list(&self, kind: StripKind) -> &Vec<Arc<Strip>> {
        match kind {
            StripKind::Channel => &self.channels,
            StripKind::Aux => &self.auxes,
            StripKind::Bus => &self.buses,
            StripKind::Main => &self.mains,
            StripKind::Matrix => &self.matrices,
        }
    }
}

/// Names of console-wide groups (message thread only).
struct GroupNames {
    dcas: Vec<String>,
    mute_groups: Vec<String>,
}

impl Default for GroupNames {
    fn default() -> Self {
        Self {
            // The WING's factory names: DCAs unnamed, mute groups MGRP.n.
            dcas: vec![String::new(); MAX_DCAS],
            mute_groups: (0..MAX_MUTE_GROUPS).map(|i| format!("MGRP.{}", i + 1)).collect(),
        }
    }
}

/// Audio-thread state. Only modified by `prepare` while the device is stopped.
struct AudioState {
    sample_rate: f64,
    block_size: usize,
    hw_in_to_active: Vec<i32>,
    hw_out_to_active: Vec<i32>,
    hw_in_ptrs: Vec<*const f32>,
    hw_out_ptrs: Vec<*mut f32>,
    /// The monitor paths SPEAKERS (0) and PHONES (1), left and right.
    monitor: [[Vec<f32>; 2]; 2],
    monitor_ramps: [MonitorRamps; 2],
    /// Each path's EQ, image, delay and limiter, DIRECT IN and BAND SOLO ramps.
    monitor_dsp: [MonitorPathDsp; 2],
    /// The sources' processing and the input pointers the channels read.
    sources: SourceDsp,
    block_counter: u64,
    /// The AMMs' state from block to block (`amm.rs`).
    amm: AmmState,
    /// Where every return ring writes this block (`routing.rs`).
    returns: ReturnClock,
    /// A dLive's Direct Outs: their ramps, by output, and the buffer they scale in.
    direct_gain: Vec<f32>,
    direct_scratch: Vec<f32>,
    /// The matrices' DIRECT IN from the monitor paths, one block late:
    /// PHONES L/R, SPEAKERS L/R, the solo bus L/R; which pairs are written.
    monitor_returns: ReturnRing,
    monitor_returns_live: [bool; 3],
}

/// A monitor path's ramps from the last block.
#[derive(Clone, Copy)]
struct MonitorRamps {
    level: f32,
    /// MONO / SWAP: 2x2 matrix (ll, rl, lr, rr).
    matrix: [f32; 4],
    /// The SOURCE's gain, and which source played.
    source: f32,
    source_index: i32,
}

impl Default for MonitorRamps {
    fn default() -> Self {
        Self { level: 0.0, matrix: MONITOR_STEREO, source: 1.0, source_index: 0 }
    }
}

/// Monitor matrices (out_l = m[0]*L + m[1]*R, out_r = m[2]*L + m[3]*R).
const MONITOR_STEREO: [f32; 4] = [1.0, 0.0, 0.0, 1.0];
const MONITOR_MONO: [f32; 4] = [0.5, 0.5, 0.5, 0.5];
const MONITOR_SWAP: [f32; 4] = [0.0, 1.0, 1.0, 0.0];

/// Console-wide group settings read once per block.
struct GroupState<'a> {
    mute_groups: GroupSet,
    dca_mute_groups: bool,
    main_link_last: usize,
    main_link_db: f32,
    /// SOURCE SOLO: the host strip and the soloed source's ports.
    source_solo: Option<(&'a Strip, SoloPatch)>,
}

/// Shares raw pointers with the parallel tasks (they are only read).
struct SharedPtrs<'a>(&'a [*const f32]);
// SAFETY: the pointed-to buffers are owned by the host for the whole callback
// and only read by the tasks.
unsafe impl Sync for SharedPtrs<'_> {}

impl SharedPtrs<'_> {
    // A method (not field access) so closures capture the Sync wrapper as a whole.
    fn get(&self) -> &[*const f32] {
        self.0
    }
}

/// What the host runs on the console text whenever it changes
/// (`Engine::set_console_hook`).
pub type ConsoleHook = fn(&Engine, &str);

pub struct Engine {
    /// A console's own state beyond its strips (a ProMix 01's pairs, groups,
    /// dynamics processors...: its profile's text), and how often it was set.
    /// Shows and scenes keep it (`<ConsoleState>`); a reset clears it.
    console_text: Mutex<(String, u64)>,
    /// Called with the console text whenever it changes (the host's: the
    /// console crates set the engine from their state, the oscillator's).
    console_hook: Mutex<Option<ConsoleHook>>,
    /// A console's own oscillator (`oscillator.rs`: the ProMix 01's, the 02R's).
    pub oscillator: ConsoleOscillator,
    garbage: Garbage,
    pool: WorkerPool,
    topology: AtomicPtr<Topology>,
    structure: Mutex<Structure>,
    names: Mutex<GroupNames>,
    audio: UnsafeCell<AudioState>,
    pub controls: EngineControls,
    /// A dLive's Direct Out settings and Source Selectors (`dlive.rs`).
    pub dlive: crate::dlive::DLiveControls,
    /// A dLive's Automatic Mic Mixers, console-wide (`amm.rs`; a channel's are `Strip::amm`).
    pub amm: AmmParams,
    /// The monitor paths' output peaks (SPEAKERS, PHONES; L/R) for the WING
    /// remote's meter stream, which takes them (the UI takes `controls`').
    pub monitor_remote_peaks: [[AtomicF32; 2]; 2],
    fx: Box<[FxRack]>,
    /// The FX slots in use (`set_num_fx`): the first `num_fx` of `fx`.
    num_fx: AtomicUsize,
    recorder: Recorder,
    /// The USB recorder / player (USB PLAYER sources, RECORDER outputs).
    usb: UsbDeck,
    /// RTA, LR scope and LISTEN on one strip at a time (`analyser.rs`).
    analysis: Analyser,
    /// ROUTING > SOURCES.
    sources: SourceTable,
    /// Talkback's ramps per destination (owned by the destination tasks).
    talk_ramps: TalkRamps,
    /// DCAs named MON A / MON B / MON LVL: the DCAs that control the
    /// SPEAKERS path, and the PHONES path.
    dca_monitor: [AtomicGroupSet; 2],
    /// The DCAs and mute groups in use (`set_num_dcas`, `set_num_mute_groups`):
    /// the first ones of `controls.dcas` and of the mute group indices.
    num_dcas: AtomicUsize,
    num_mute_groups: AtomicUsize,
    /// Plugin delay compensation: the setting and the plan's status.
    pdc: PdcState,
    /// The audio interface's reported input + output latency in samples (the
    /// host's), for an outboard insert's round trip.
    io_latency: AtomicU32,
    /// Recall safes of sources and outputs (GLOBAL SAFE, `iosafes.rs`).
    io_safes: IoSafes,
    /// ROUTING > OUTPUTS: the sources patched straight to outputs.
    output_patch: OutputPatch,
}

// SAFETY: `audio` is only touched by the audio thread (process) or by prepare
// while the device is stopped; `structure`/`names` are behind mutexes the audio
// thread never takes; everything else is atomic or internally synchronised.
unsafe impl Sync for Engine {}
unsafe impl Send for Engine {}

const DEFAULT_SAMPLE_RATE: f64 = 48000.0;
const DEFAULT_BLOCK: usize = 512;

/// The monitor paths a DCA name controls (bit 0 = SPEAKERS, bit 1 = PHONES):
/// "MON A" (PHONES, path 1 on the WING), "MON B" (SPEAKERS), "MON LVL" (both),
/// ignoring case, spaces and dots.
pub fn monitor_dca_paths(name: &str) -> u32 {
    let key: String = name.chars().filter(|c| !matches!(c, ' ' | '.' | '_' | '-')).collect::<String>().to_ascii_uppercase();
    match key.as_str() {
        "MONA" => 2,
        "MONB" => 1,
        "MONLVL" => 3,
        _ => 0,
    }
}

impl Engine {
    pub fn new(num_workers: usize) -> Self {
        // Read LIVEDAW_TRACE_SLOW here: reading it allocates, and the audio
        // thread asks for it on its first block.
        crate::trace::enabled();
        let device = DeviceConfig { sample_rate: DEFAULT_SAMPLE_RATE, block_size: DEFAULT_BLOCK, num_hw_inputs: 0 };
        let engine = Self {
            console_text: Mutex::new((String::new(), 0)),
            console_hook: Mutex::new(None),
            oscillator: ConsoleOscillator::default(),
            dlive: Default::default(),
            garbage: Garbage::default(),
            pool: WorkerPool::new(num_workers),
            topology: AtomicPtr::new(std::ptr::null_mut()),
            structure: Mutex::new(Structure {
                channels: Vec::new(),
                auxes: Vec::new(),
                buses: Vec::new(),
                mains: Vec::new(),
                matrices: Vec::new(),
                next_channel_id: 1,
                device,
                counts: Profile::Wing.spec().counts,
                profile: Profile::Wing,
            }),
            names: Mutex::new(GroupNames::default()),
            audio: UnsafeCell::new(AudioState {
                sample_rate: DEFAULT_SAMPLE_RATE,
                block_size: DEFAULT_BLOCK,
                hw_in_to_active: Vec::new(),
                hw_out_to_active: Vec::new(),
                hw_in_ptrs: Vec::new(),
                hw_out_ptrs: Vec::new(),
                monitor: std::array::from_fn(|_| [vec![0.0; DEFAULT_BLOCK], vec![0.0; DEFAULT_BLOCK]]),
                monitor_ramps: Default::default(),
                monitor_dsp: Default::default(),
                sources: SourceDsp::default(),
                block_counter: 0,
                amm: AmmState::default(),
                returns: ReturnClock::new(DEFAULT_BLOCK),
                monitor_returns: ReturnRing::new(6, DEFAULT_BLOCK),
                monitor_returns_live: [false; 3],
                direct_gain: vec![0.0; NUM_PATCH_OUTPUTS],
                direct_scratch: Vec::new(),
            }),
            controls: EngineControls::default(),
            amm: AmmParams::default(),
            monitor_remote_peaks: Default::default(),
            fx: (0..MAX_FX).map(|_| FxRack::default()).collect(),
            num_fx: AtomicUsize::new(WING_FX),
            recorder: Recorder::new(),
            usb: UsbDeck::new(),
            analysis: Analyser::new(),
            sources: SourceTable::default(),
            talk_ramps: TalkRamps::default(),
            dca_monitor: Default::default(),
            num_dcas: AtomicUsize::new(WING_DCAS),
            num_mute_groups: AtomicUsize::new(WING_MUTE_GROUPS),
            pdc: PdcState::default(),
            io_latency: AtomicU32::new(0),
            io_safes: IoSafes::default(),
            output_patch: OutputPatch::default(),
        };
        // SAFETY: not published yet, no audio callback.
        unsafe {
            for f in engine.fx.iter() {
                f.prepare(DEFAULT_BLOCK);
                f.set_round_trip(DEFAULT_BLOCK as u32);
            }
            (*engine.audio.get()).sources.prepare(0, DEFAULT_BLOCK, DEFAULT_SAMPLE_RATE);
        }
        let mut st = engine.lock();
        engine.build_fixed(&mut st);
        engine.publish(&st);
        drop(st);
        engine
    }

    /// Recommended worker count: leave a core for the audio thread and one for the UI/OS.
    pub fn default_num_workers() -> usize {
        if let Some(n) = std::env::var("LIVEDAW_WORKERS").ok().and_then(|v| v.parse().ok()) {
            return n; // diagnostics: a worker count of one's own
        }
        std::thread::available_parallelism().map_or(0, |n| n.get().saturating_sub(2))
    }

    fn lock(&self) -> MutexGuard<'_, Structure> {
        self.structure.lock().unwrap_or_else(|e| e.into_inner())
    }

    pub fn pool(&self) -> &WorkerPool {
        &self.pool
    }

    pub fn garbage(&self) -> &Garbage {
        &self.garbage
    }

    // =========================================================================
    // Structure (message thread)

    fn new_strip(&self, st: &Structure, kind: StripKind, id: i32) -> Arc<Strip> {
        self.new_strip_in(st, kind, id, usize::try_from(id).unwrap_or(0))
    }

    /// A new strip in `slot` (0-based) of its kind, whose id may differ (a channel's).
    fn new_strip_in(&self, st: &Structure, kind: StripKind, id: i32, slot: usize) -> Arc<Strip> {
        // Unnamed, as on the WING (the screens show "CH 3" and so on).
        let s = Arc::new(Strip::of_profile_in(st.profile.spec(), kind, id, slot));
        // Its sends to the console's mains and matrices (a matrix has none).
        let (mains, matrices) = match kind {
            StripKind::Matrix => (0, 0),
            StripKind::Main => (0, st.counts.matrices),
            _ => (st.counts.mains, st.counts.matrices),
        };
        s.create_sends(mains, matrices);
        // SAFETY: not published yet.
        unsafe { s.prepare(st.device.sample_rate, st.device.block_size) };
        s
    }

    /// The configured aux channels, buses, mains and matrices (the WING's 8,
    /// 16, 4 and 8).
    fn build_fixed(&self, st: &mut Structure) {
        let c = st.counts;
        st.auxes = (0..c.aux).map(|i| self.new_strip(st, StripKind::Aux, i as i32)).collect();
        st.buses = (0..c.buses).map(|i| self.new_strip(st, StripKind::Bus, i as i32)).collect();
        st.mains = (0..c.mains).map(|i| self.new_strip(st, StripKind::Main, i as i32)).collect();
        st.matrices = (0..c.matrices).map(|i| self.new_strip(st, StripKind::Matrix, i as i32)).collect();
        // Main 1 plays on the first two outputs.
        st.mains[0].params.output_left.store(0, Relaxed);
        st.mains[0].params.output_right.store(1, Relaxed);
    }

    fn publish(&self, st: &Structure) {
        let mut buses = st.buses.clone();
        buses.sort_by_key(|b| b.id());
        let next = Box::new(Topology {
            channels: st.channels.clone(),
            auxes: st.auxes.clone(),
            active_inputs: UnsafeCell::new(Vec::with_capacity(st.channels.len() + st.auxes.len())),
            key_schedule: UnsafeCell::new(KeySchedule::new(st.channels.len() + st.auxes.len())),
            bus_schedule: UnsafeCell::new(BusSchedule::new(buses.len())),
            bus_list: buses,
            mains: st.mains.clone(),
            matrices: st.matrices.clone(),
        });
        let old = self.topology.swap(Box::into_raw(next), AcqRel);
        if !old.is_null() {
            // SAFETY: created by Box::into_raw; now unpublished.
            self.garbage.retire(unsafe { Box::from_raw(old) });
        }
    }

    fn create_channel(&self, st: &mut Structure, requested_id: i32) -> Arc<Strip> {
        let index = st.channels.len();
        let id_free = requested_id > 0 && st.channels.iter().all(|s| s.id() != requested_id);
        let id = if id_free { requested_id } else { st.next_channel_id };
        st.next_channel_id = st.next_channel_id.max(id + 1);

        let strip = self.new_strip_in(st, StripKind::Channel, id, index);
        // Its own input, unless its console's factory patched it (an 02R's
        // tape returns on the card slots).
        let patched = st.device.num_hw_inputs == 0 || index < st.device.num_hw_inputs;
        if strip.params.input_left.load(Relaxed) < 0 {
            strip.params.input_left.store(if patched { index as i32 } else { -1 }, Relaxed);
        }
        strip
    }

    /// Adds an input channel. `requested_id` restores a saved id (ignored if taken).
    pub fn add_channel(&self, requested_id: i32) -> Arc<Strip> {
        let mut st = self.lock();
        let strip = self.create_channel(&mut st, requested_id);
        st.channels.push(strip.clone());
        self.publish(&st);
        strip
    }

    /// Grows or shrinks the channel count in one topology change.
    pub fn set_num_channels(&self, count: usize) {
        {
            let mut st = self.lock();
            if count == st.channels.len() {
                return;
            }
            while st.channels.len() < count {
                let s = self.create_channel(&mut st, -1);
                st.channels.push(s);
            }
            st.channels.truncate(count);
            self.publish(&st);
        }
        self.add_factory_preamp_blocks();
    }

    /// Adds a bus in the first free slot (or `requested_slot` if free). The
    /// console has 16; more can be added up to `MAX_BUSES`.
    pub fn add_bus(&self, requested_slot: i32) -> Option<Arc<Strip>> {
        let mut st = self.lock();
        let mut used = vec![false; MAX_BUSES];
        for b in &st.buses {
            used[b.id() as usize] = true;
        }
        let slot = if requested_slot >= 0 && (requested_slot as usize) < MAX_BUSES && !used[requested_slot as usize] {
            requested_slot as usize
        } else {
            used.iter().position(|u| !u)?
        };

        // No strip may carry a send from an old bus in this slot. (The ramp state
        // is left alone: a callback may still be finishing with a removed bus.)
        for s in st.channels.iter().chain(&st.auxes).chain(&st.buses) {
            if let Some(send) = s.sends.find(slot) {
                send.reset(s.sends.default_mode(slot));
            }
        }

        let strip = self.new_strip(&st, StripKind::Bus, slot as i32);
        st.buses.push(strip.clone());
        st.buses.sort_by_key(|b| b.id());
        self.publish(&st);
        Some(strip)
    }

    /// Message thread: changes how many aux channels, buses, mains or matrices
    /// the console has, without a reset (the Universal Mixer's configuration,
    /// docs/UNIVERSAL_MIXER.md). New strips come at the end with factory
    /// settings, and every strip gets its (factory) sends to new mains and
    /// matrices. Strips go from the end, but a bus, main or matrix that
    /// something feeds (a send that is on) stays, with those before it: the
    /// count never goes below the last one fed. Aux channels are inputs:
    /// they go whatever they do (the interface warns first). Channels:
    /// `set_num_channels`. Returns the number set; the configuration (shows
    /// save it) follows, and so does the profile: counts beyond its make the
    /// console the Universal.
    pub fn set_num_strips(&self, kind: StripKind, count: usize) -> usize {
        let mut st = self.lock();
        let inputs_and_buses = |st: &Structure| st.channels.iter().chain(&st.auxes).chain(&st.buses).cloned().collect::<Vec<_>>();
        let n = match kind {
            StripKind::Channel => return st.channels.len(),
            StripKind::Aux => {
                let n = count.min(MAX_AUX);
                st.counts.aux = n;
                while st.auxes.len() < n {
                    let s = self.new_strip(&st, StripKind::Aux, st.auxes.len() as i32);
                    st.auxes.push(s);
                }
                st.auxes.truncate(n);
                n
            }
            StripKind::Bus => {
                let old = st.counts.buses;
                let fed = |slot: usize| {
                    inputs_and_buses(&st).iter().any(|s| s.sends.find(slot).is_some_and(|send| send.enabled.load(Relaxed)))
                };
                let n = count.min(MAX_BUSES);
                let n = if n < old { (n..old).rev().find(|&b| fed(b)).map_or(n, |b| b + 1) } else { n };
                st.counts.buses = n;
                for slot in old..n {
                    if st.buses.iter().any(|b| b.id() as usize == slot) {
                        continue; // an added bus in that slot is now one of the console's
                    }
                    for s in inputs_and_buses(&st) {
                        if let Some(send) = s.sends.find(slot) {
                            send.reset(s.sends.default_mode(slot));
                        }
                    }
                    let b = self.new_strip(&st, StripKind::Bus, slot as i32);
                    st.buses.push(b);
                }
                st.buses.retain(|b| !(n..old).contains(&(b.id() as usize)));
                st.buses.sort_by_key(|b| b.id());
                n
            }
            StripKind::Main => {
                let old = st.mains.len();
                let fed =
                    |i: usize| inputs_and_buses(&st).iter().any(|s| s.main_send(i).is_some_and(|send| send.on.load(Relaxed)));
                let n = count.clamp(1, MAX_MAINS);
                let n = if n < old { (n..old).rev().find(|&i| fed(i)).map_or(n, |i| i + 1) } else { n };
                st.counts.mains = n;
                for i in old..n {
                    for s in inputs_and_buses(&st) {
                        if let Some(send) = s.main_send(i) {
                            send.reset(s.spec(), s.kind(), s.id().max(0) as usize, i);
                        }
                    }
                    let m = self.new_strip(&st, StripKind::Main, i as i32);
                    st.mains.push(m);
                }
                st.mains.truncate(n);
                let matrices = st.matrices.len();
                for s in inputs_and_buses(&st) {
                    s.create_sends(n, matrices);
                }
                n
            }
            StripKind::Matrix => {
                let old = st.matrices.len();
                let sources =
                    |st: &Structure| inputs_and_buses(st).into_iter().chain(st.mains.iter().cloned()).collect::<Vec<_>>();
                let fed = |i: usize| sources(&st).iter().any(|s| s.matrix_send(i).is_some_and(|send| send.on.load(Relaxed)));
                let n = count.min(MAX_MATRICES);
                let n = if n < old { (n..old).rev().find(|&i| fed(i)).map_or(n, |i| i + 1) } else { n };
                st.counts.matrices = n;
                for i in old..n {
                    for s in sources(&st) {
                        if let Some(send) = s.matrix_send(i) {
                            send.reset(s.kind());
                        }
                    }
                    let m = self.new_strip(&st, StripKind::Matrix, i as i32);
                    st.matrices.push(m);
                }
                st.matrices.truncate(n);
                let mains = st.mains.len();
                for s in inputs_and_buses(&st) {
                    s.create_sends(mains, n);
                }
                for s in st.mains.iter() {
                    s.create_sends(0, n);
                }
                n
            }
        };
        // Beyond the console emulated: the Universal Mixer's own.
        st.profile = st.profile.or_universal(&st.counts);
        self.publish(&st);
        n
    }

    /// Removes a channel, or a bus beyond the console's configured ones
    /// (the WING's 16). The strip is dropped later, once unreachable.
    pub fn remove_strip(&self, strip: *const Strip) -> bool {
        let mut st = self.lock();
        let fixed_buses = st.counts.buses;
        let removed = match st.channels.iter().position(|s| Arc::as_ptr(s) == strip) {
            Some(i) => Some(st.channels.remove(i)),
            None => st
                .buses
                .iter()
                .position(|s| Arc::as_ptr(s) == strip && s.id() as usize >= fixed_buses)
                .map(|i| st.buses.remove(i)),
        };
        let Some(removed) = removed else { return false };
        self.publish(&st);
        if removed.kind().is_bus() {
            for s in st.channels.iter().chain(&st.auxes).chain(&st.buses) {
                if let Some(send) = s.sends.find(removed.id() as usize) {
                    send.enabled.store(false, Relaxed);
                }
            }
        }
        drop(removed); // the retired topology keeps it alive until collected
        true
    }

    /// Back to a fresh console: no channels, default aux/buses/mains/matrices,
    /// DCAs, mute groups and FX settings.
    pub fn reset(&self) {
        self.oscillator.clear();
        self.set_console_text("");
        let mut st = self.lock();
        st.channels.clear();
        st.next_channel_id = 1;
        self.build_fixed(&mut st);
        self.publish(&st);
        self.num_dcas.store(st.counts.dcas, Relaxed);
        self.num_mute_groups.store(st.counts.mute_groups, Relaxed);
        self.controls.reset();
        self.amm.reset();
        self.dlive.reset();
        // The console's new DCAs take its palette's colour (an X32's WH).
        for d in &self.controls.dcas {
            d.colour.store(st.profile.spec().palette.default, Relaxed);
        }
        self.analysis.reset();
        self.sources.reset();
        self.io_safes.clear();
        self.output_patch.reset();
        // A console's own factory outputs (an X32's buses and mains), in place
        // of main 1 on outputs 1-2.
        let spec = st.profile.spec();
        if let Some(outputs) = spec.factory_outputs {
            for m in &st.mains {
                m.params.output_left.store(-1, Relaxed);
                m.params.output_right.store(-1, Relaxed);
            }
            outputs(spec, &self.output_patch);
        }
        let mut names = self.names.lock().unwrap_or_else(|e| e.into_inner());
        *names = GroupNames::default();
        // A console's own factory DCAs (an X32's "DCA 1"... at 0 dB).
        let spec = st.profile.spec();
        if let Some(prefix) = spec.dca_names {
            for (i, n) in names.dcas.iter_mut().enumerate().take(st.counts.dcas) {
                *n = format!("{prefix} {}", i + 1);
            }
            for d in self.controls.dcas.iter().take(st.counts.dcas) {
                d.fader_db.store(spec.dca_fader_db);
            }
        }
        self.update_monitor_dcas(&names);
    }

    /// Message thread: makes this a fresh console of `config` (a reset with
    /// its counts: channels, aux, buses, mains, matrices, DCAs, mute groups
    /// and FX slots). The WING is `ConsoleConfig::wing()`.
    pub fn configure(&self, config: &ConsoleConfig) {
        let counts = self.set_config(config);
        self.set_num_channels(counts.channels);
        self.set_num_fx(counts.fx);
        // The console's factory FX rack (an X32's), each device on its defaults.
        let spec = config.profile.spec();
        let fx = self.fx_models();
        for (i, token) in spec.fx_factory.iter().enumerate() {
            if let Some(model) = livedaw_dsp::model::model_index(fx, token) {
                self.set_fx_model(i, model, true);
            }
        }
        for (p, &bus) in self.controls.fx.iter().zip(spec.fx_send_buses) {
            let input = crate::routing::strip_source_input(StripKind::Bus, bus, 0);
            p.source_left.store(input, Relaxed);
            p.source_right.store(input, Relaxed);
        }
        // A PRO2's matrices 1-4 are its FX sends, in place of buses.
        for (p, &matrix) in self.controls.fx.iter().zip(spec.fx_send_matrices()) {
            let input = crate::routing::strip_source_input(StripKind::Matrix, matrix, 0);
            p.source_left.store(input, Relaxed);
            p.source_right.store(input, Relaxed);
        }
        self.add_factory_eq_blocks(&[StripKind::Bus, StripKind::Main, StripKind::Matrix]);
    }

    /// A console's own preamp block, off, on each of its input channels that lacks
    /// one, right after the channel's INPUT (a dLive's: `ProfileSpec::factory_preamp_block`):
    /// new channels (`set_num_channels`), a show from before it had one, a channel back on its defaults.
    pub fn add_factory_preamp_blocks(&self) {
        let Some(token) = self.profile().spec().factory_preamp_block else { return };
        for strip in self.strips(StripKind::Channel) {
            if strip.preamp_block().is_some() {
                continue;
            }
            let Some(input) = strip.steps().iter().position(|s| matches!(s, Step::Node(Node::Input))) else { continue };
            let Some(block) = self.insert_block(&strip, BlockKind::Dynamics, input + 1) else { continue };
            if let crate::blocks::BlockParams::Dynamics(d) = &block.params {
                if let Some(model) = livedaw_dsp::model::model_index(d.table(), token) {
                    d.set_model(model);
                }
            }
        }
    }

    /// A console's own EQ block on its mixes of these kinds, before their EQ (the
    /// SQ's GEQ) or after it (the dLive's).
    fn add_factory_eq_blocks(&self, kinds: &[StripKind]) {
        let spec = self.profile().spec();
        let Some(token) = spec.factory_eq_block else { return };
        for &kind in kinds {
            for strip in self.strips(kind) {
                let Some(at) = strip.steps().iter().position(|s| matches!(s, Step::Node(Node::Eq))) else { continue };
                let at = at + usize::from(spec.factory_eq_after);
                let Some(block) = self.insert_block(&strip, BlockKind::Eq, at) else { continue };
                if let crate::blocks::BlockParams::Eq(eq) = &block.params {
                    if let Some(model) = livedaw_dsp::model::model_index(eq.table(), token) {
                        eq.set_model(model);
                    }
                }
            }
        }
    }

    /// Audio thread: each Source Selector's fades, one block on; what they leave of the
    /// sends goes to the sources' `selector_gain`. The sources are mutually exclusive
    /// on the output; anything else routed to it is left alone. The fade is linear in
    /// gain (Sidecar's reading; the guide says only "automatic crossfades", up to 10 s).
    unsafe fn tick_selectors(&self, topo: &Topology, sample_rate: f64, n: usize) {
        if topo.mains.first().is_none_or(|m| m.spec().dlive_layout.is_none()) {
            return;
        }
        let per_second = n as f32 / sample_rate as f32;
        let step = |fade: f32| if fade > 0.0 { per_second / fade } else { f32::MAX };
        for sel in &self.dlive.selectors {
            let Some(dest) = sel.destination() else { continue };
            let active = sel.active.load(Relaxed);
            let (up, down) = (step(sel.fade_in_s.load()), step(sel.fade_out_s.load()));
            for (i, m) in sel.sources.iter().enumerate() {
                let Some((kind, number)) = m.strip() else { continue };
                let on = db_to_gain(m.on_db.load());
                let target = if i as i32 == active { on } else { 0.0 };
                let gain = m.step(target, up * on.max(0.001), down * on.max(0.001));
                if let Some(s) = topo.by_number(kind, number) {
                    s.selector_gain[dest].store(gain);
                }
            }
        }
    }

    /// Message thread: a selector's source `member` takes no more part: its send plays as
    /// if there were no selector.
    fn free_selector_source(&self, selector: usize, member: usize) {
        let Some(sel) = self.dlive.selectors.get(selector) else { return };
        let (Some(dest), Some(m)) = (sel.destination(), sel.sources.get(member)) else { return };
        if let Some(s) = m.strip().and_then(|(kind, number)| self.strip_by_number(kind, number)) {
            s.selector_gain[dest].store(1.0);
        }
    }

    /// Message thread: selector `selector`'s output, an Aux (a bus slot) or a Matrix. None:
    /// unassigned.
    pub fn set_selector_output(&self, selector: usize, output: Option<(StripKind, usize)>) {
        let Some(sel) = self.dlive.selectors.get(selector) else { return };
        for member in 0..sel.sources.len() {
            self.free_selector_source(selector, member);
        }
        match output {
            Some((kind, index)) => {
                sel.output.store(index as i32, Relaxed);
                let kind = StripKind::ALL.iter().position(|k| *k == kind).map_or(-1, |k| k as i32);
                sel.output_kind.store(kind, Relaxed);
            }
            None => sel.output_kind.store(-1, Relaxed),
        }
    }

    /// Message thread: a selector's source `member`, by the strip's kind and number from
    /// 1. None: empty.
    pub fn set_selector_source(&self, selector: usize, member: usize, source: Option<(StripKind, i32)>) {
        self.free_selector_source(selector, member);
        if let Some(m) = self.dlive.selectors.get(selector).and_then(|s| s.sources.get(member)) {
            m.set_strip(source);
        }
    }

    /// Message thread: the channel plays ABCD input `which` (0-3), if it has them on.
    pub fn abcd_select(&self, s: &Strip, which: usize) {
        let d = &s.dlive;
        if which >= d.abcd_left.len() {
            return;
        }
        d.abcd_active.store(which as i32, Relaxed);
        if d.abcd_on.load(Relaxed) {
            s.params.input_left.store(d.abcd_left[which].load(Relaxed), Relaxed);
            s.params.input_right.store(d.abcd_right[which].load(Relaxed), Relaxed);
        }
    }

    /// Message thread: ABCD inputs on or off for the channel. Turned on with A still
    /// unpatched, A takes the channel's patch (Sidecar's reading); turned off, it keeps
    /// the input it's on.
    pub fn abcd_enable(&self, s: &Strip, on: bool) {
        let d = &s.dlive;
        d.abcd_on.store(on, Relaxed);
        if on {
            if d.abcd_left[0].load(Relaxed) < 0 {
                d.abcd_left[0].store(s.params.input_left.load(Relaxed), Relaxed);
                d.abcd_right[0].store(s.params.input_right.load(Relaxed), Relaxed);
            }
            self.abcd_select(s, d.abcd_active.load(Relaxed).clamp(0, 3) as usize);
        }
    }

    /// Message thread: the Actions' ABCD Set All, Set For DCA and Set Range (first
    /// and last 0-based, inclusive).
    pub fn abcd_set_all(&self, which: usize) {
        for s in self.strips(StripKind::Channel) {
            self.abcd_select(&s, which);
        }
    }

    pub fn abcd_set_for_dca(&self, dca: usize, which: usize) {
        for s in self.strips(StripKind::Channel).iter().filter(|s| s.params.dca_mask.load().contains(dca)) {
            self.abcd_select(s, which);
        }
    }

    pub fn abcd_set_range(&self, first: usize, last: usize, which: usize) {
        for s in self.strips(StripKind::Channel).iter().take(last + 1).skip(first) {
            self.abcd_select(s, which);
        }
    }

    /// Message thread: a stereo mix's Follow Main Pan and Invert. Invert alone copies the
    /// channels' pans to the sends, mirrored, once (the guide's Invert without Follow;
    /// Sidecar's reading); with Follow they stay tied, at audio time.
    pub fn set_follow_main_pan(&self, mix: &Strip, follow: bool, invert: bool) {
        mix.dlive.follow_main_pan.store(follow, Relaxed);
        mix.dlive.follow_invert.store(invert, Relaxed);
        if !invert || follow {
            return;
        }
        let index = mix.id().max(0) as usize;
        for kind in [StripKind::Channel, StripKind::Aux] {
            for s in self.strips(kind) {
                let pan = -s.params.pan.load();
                match mix.kind() {
                    StripKind::Bus => {
                        if let Some(send) = s.sends.get_or_create(index) {
                            send.pan_link.store(false, Relaxed);
                            send.pan.store(pan);
                        }
                    }
                    StripKind::Matrix => {
                        if let Some(send) = s.matrix_send(index) {
                            send.pan_link.store(false, Relaxed);
                            send.pan.store(pan);
                        }
                    }
                    _ => {}
                }
            }
        }
    }

    /// A dLive's mixes have just been laid out anew (`console::apply_dlive_layout`),
    /// all on their defaults: their GEQs, the outputs that fed the old mixes (now
    /// the new ones' defaults, where nothing else is patched) and the FX sends' FX.
    pub fn settle_mix_layout(&self, layout: crate::profile::DLiveLayout) {
        use crate::routing::{strip_source, strip_source_input};
        self.add_factory_eq_blocks(&[StripKind::Bus, StripKind::Matrix]);
        let was_mix = |source: i32| {
            matches!(strip_source(source), Some((StripKind::Bus | StripKind::Matrix, ..)))
                || matches!(strip_source(source), Some((StripKind::Main, slot, _)) if slot > 0)
        };
        let patched: Vec<(usize, i32)> = self.output_patch.patched().collect();
        for (port, source) in patched {
            if was_mix(source) {
                self.output_patch.set(port as i32, -1);
            }
        }
        layout.patch_mix_outputs(&self.output_patch);
        let sends = self.profile().spec().fx_send_buses;
        for (i, p) in self.controls.fx.iter().enumerate() {
            if let Some(&bus) = sends.get(i) {
                let input = strip_source_input(StripKind::Bus, bus, 0);
                p.source_left.store(input, Relaxed);
                p.source_right.store(input, Relaxed);
            } else {
                for source in [&p.source_left, &p.source_right] {
                    if was_mix(source.load(Relaxed)) {
                        source.store(-1, Relaxed);
                    }
                }
            }
        }
    }

    /// Message thread: makes this a fresh console of `config`'s structure
    /// without its channels (a show load adds them by id) or FX slot count;
    /// returns the counts in use.
    pub fn set_config(&self, config: &ConsoleConfig) -> Counts {
        let counts = config.counts.sanitised();
        {
            let mut st = self.lock();
            st.counts = counts;
            st.profile = config.profile.or_universal(&counts);
        }
        self.reset();
        counts
    }

    /// The console's own state beyond its strips, as its profile writes it
    /// (empty: none), and its version (it changes with every set).
    pub fn console_text(&self) -> (String, u64) {
        self.console_text.lock().unwrap_or_else(|e| e.into_inner()).clone()
    }

    /// The console's own state's version alone (cheap: no copy).
    pub fn console_text_version(&self) -> u64 {
        self.console_text.lock().unwrap_or_else(|e| e.into_inner()).1
    }

    /// Sets the console's own state (a show load, a scene recall, the
    /// profile's own code); returns the new version.
    pub fn set_console_text(&self, text: &str) -> u64 {
        let (version, changed) = {
            let mut t = self.console_text.lock().unwrap_or_else(|e| e.into_inner());
            let changed = t.0 != text;
            if changed {
                t.0 = text.to_string();
                t.1 += 1;
            }
            (t.1, changed)
        };
        if changed {
            let hook = *self.console_hook.lock().unwrap_or_else(|e| e.into_inner());
            if let Some(hook) = hook {
                hook(self, text);
            }
        }
        version
    }

    /// Sets what is called with the console text whenever it changes (and
    /// calls it with the current one).
    pub fn set_console_hook(&self, hook: Option<ConsoleHook>) {
        *self.console_hook.lock().unwrap_or_else(|e| e.into_inner()) = hook;
        if let Some(hook) = hook {
            hook(self, &self.console_text().0);
        }
    }

    /// The configuration of the console (`configure`; its profile is the
    /// Universal if the counts don't fit the one asked for).
    pub fn config(&self) -> ConsoleConfig {
        let st = self.lock();
        ConsoleConfig { profile: st.profile, counts: st.counts }
    }

    /// The console emulated.
    pub fn profile(&self) -> Profile {
        self.lock().profile
    }

    /// What the console has now: its strips by kind (channels and buses
    /// can be added beyond the configuration), and the DCAs, mute groups and
    /// FX slots in use.
    pub fn counts(&self) -> Counts {
        let st = self.lock();
        Counts {
            channels: st.channels.len(),
            aux: st.auxes.len(),
            buses: st.buses.len(),
            mains: st.mains.len(),
            matrices: st.matrices.len(),
            dcas: self.num_dcas(),
            mute_groups: self.num_mute_groups(),
            fx: self.num_fx(),
        }
    }

    /// The DCAs in use: the first `num_dcas` of `controls.dcas`.
    pub fn num_dcas(&self) -> usize {
        self.num_dcas.load(Relaxed)
    }

    /// The mute groups in use (indices `0..num_mute_groups`).
    pub fn num_mute_groups(&self) -> usize {
        self.num_mute_groups.load(Relaxed)
    }

    /// Message thread: sets the number of DCAs in use (up to `MAX_DCAS`),
    /// never below a DCA a strip is a member of, so a hidden DCA never acts
    /// on a strip. Returns the number set.
    pub fn set_num_dcas(&self, count: usize) -> usize {
        let st = self.lock();
        let used = st.all().fold(GroupSet::EMPTY, |used, s| used | s.params.dca_mask.load());
        let n = count.min(MAX_DCAS).max(used.iter().last().map_or(0, |i| i + 1));
        self.num_dcas.store(n, Relaxed);
        n
    }

    /// Message thread: sets the number of mute groups in use (up to
    /// `MAX_MUTE_GROUPS`), never below a group that is engaged or has a strip
    /// or a source as a member. Returns the number set.
    pub fn set_num_mute_groups(&self, count: usize) -> usize {
        let st = self.lock();
        let used = st.all().fold(self.controls.mute_groups.load(), |used, s| used | s.params.mute_group_mask.load());
        let used = self.sources.params().iter().fold(used, |used, p| used | p.mute_groups.load());
        let n = count.min(MAX_MUTE_GROUPS).max(used.iter().last().map_or(0, |i| i + 1));
        self.num_mute_groups.store(n, Relaxed);
        n
    }

    // ---- extra blocks in a strip's chain (`blocks.rs`) -------------------------

    /// Message thread: adds a block of `kind` at step `at` of `strip`'s chain
    /// (`Strip::steps`; past the end = last). None when the strip has its
    /// `MAX_EXTRA_BLOCKS` already.
    pub fn insert_block(&self, strip: &Strip, kind: BlockKind, at: usize) -> Option<Arc<Block>> {
        self.place_block(strip, strip.new_block_uid(), kind, at)
    }

    /// Message thread: a block with the id `uid` (a show's), at step `at`.
    pub fn restore_block(&self, strip: &Strip, uid: u32, kind: BlockKind, at: usize) -> Option<Arc<Block>> {
        if strip.blocks().iter().any(|b| b.uid() == uid) {
            return None;
        }
        strip.ensure_block_uid(uid);
        self.place_block(strip, uid, kind, at)
    }

    fn place_block(&self, strip: &Strip, uid: u32, kind: BlockKind, at: usize) -> Option<Arc<Block>> {
        let st = self.lock();
        let mut steps = strip.steps();
        if steps.iter().filter(|s| matches!(s, Step::Block(_))).count() >= MAX_EXTRA_BLOCKS {
            return None;
        }
        let block = Arc::new(Block::new(uid, kind, strip.kind().is_output(), st.device.sample_rate, strip.spec().models));
        steps.insert(at.min(steps.len()), Step::Block(block.clone()));
        strip.set_layout(steps, &self.garbage);
        Some(block)
    }

    /// Message thread: takes block `uid` out of `strip`'s chain.
    pub fn remove_block(&self, strip: &Strip, uid: u32) -> bool {
        let _st = self.lock();
        let mut steps = strip.steps();
        let Some(i) = steps.iter().position(|s| matches!(s, Step::Block(b) if b.uid() == uid)) else { return false };
        steps.remove(i);
        strip.set_layout(steps, &self.garbage);
        true
    }

    /// Message thread: moves block `uid` to step `at` (of the chain without
    /// it), keeping its settings and state.
    pub fn move_block(&self, strip: &Strip, uid: u32, at: usize) -> bool {
        let _st = self.lock();
        let mut steps = strip.steps();
        let Some(i) = steps.iter().position(|s| matches!(s, Step::Block(b) if b.uid() == uid)) else { return false };
        let step = steps.remove(i);
        steps.insert(at.min(steps.len()), step);
        strip.set_layout(steps, &self.garbage);
        true
    }

    /// Message thread: places a user tap (`taps.rs`) at step `at` of
    /// `strip`'s chain; returns its id (None when all are placed).
    pub fn insert_tap(&self, strip: &Strip, at: usize) -> Option<u8> {
        let _st = self.lock();
        let mut steps = strip.steps();
        let used = |id: u8| steps.iter().any(|s| matches!(s, Step::Node(Node::Tap(t)) if *t == id));
        let id = (FIRST_USER_TAP..NUM_TAP_IDS as u8).find(|&id| !used(id))?;
        steps.insert(at.min(steps.len()), Step::Node(Node::Tap(id)));
        strip.set_layout(steps, &self.garbage);
        Some(id)
    }

    /// Message thread: a user tap with the id `id` (a show's) at step `at`.
    pub fn restore_tap(&self, strip: &Strip, id: u8, at: usize) -> bool {
        let _st = self.lock();
        let mut steps = strip.steps();
        if !(FIRST_USER_TAP..NUM_TAP_IDS as u8).contains(&id)
            || steps.iter().any(|s| matches!(s, Step::Node(Node::Tap(t)) if *t == id))
        {
            return false;
        }
        steps.insert(at.min(steps.len()), Step::Node(Node::Tap(id)));
        strip.set_layout(steps, &self.garbage);
        true
    }

    /// Message thread: moves user tap `id` to step `at` (of the chain without it).
    pub fn move_tap(&self, strip: &Strip, id: u8, at: usize) -> bool {
        let _st = self.lock();
        let mut steps = strip.steps();
        let Some(i) = steps.iter().position(|s| matches!(s, Step::Node(Node::Tap(t)) if *t == id && id >= FIRST_USER_TAP)) else {
            return false;
        };
        let step = steps.remove(i);
        steps.insert(at.min(steps.len()), step);
        strip.set_layout(steps, &self.garbage);
        true
    }

    /// Message thread: takes user tap `id` out of `strip`'s chain (sends
    /// taking it go silent).
    pub fn remove_tap(&self, strip: &Strip, id: u8) -> bool {
        let _st = self.lock();
        let mut steps = strip.steps();
        let Some(i) = steps.iter().position(|s| matches!(s, Step::Node(Node::Tap(t)) if *t == id && id >= FIRST_USER_TAP)) else {
            return false;
        };
        steps.remove(i);
        strip.set_layout(steps, &self.garbage);
        true
    }

    /// Message thread: the user tap buffers every strip writes: the taps its
    /// own sends (to buses, mains, matrices) take. Called by `collect`.
    pub fn update_taps(&self) {
        let st = self.lock();
        let bus_slots: Vec<usize> = st.buses.iter().map(|b| b.id() as usize).collect();
        for s in st.all() {
            let mut wanted = [false; NUM_TAP_IDS];
            let mut want = |tap: i32| {
                if let Some(id) = send_tap(tap) {
                    wanted[id] = true;
                }
            };
            for &slot in &bus_slots {
                if let Some(send) = s.sends.find(slot).filter(|send| send.enabled.load(Relaxed)) {
                    want(send.tap.load(Relaxed));
                }
            }
            for i in 0..st.mains.len() {
                if let Some(send) = s.main_send(i).filter(|send| send.on.load(Relaxed)) {
                    want(send.tap.load(Relaxed));
                }
            }
            for i in 0..st.matrices.len() {
                if let Some(send) = s.matrix_send(i).filter(|send| send.on.load(Relaxed)) {
                    want(send.tap.load(Relaxed));
                }
            }
            // A dLive's Mix Source, Direct Out source and Group to Group can move to any
            // of these at any moment.
            if let Profile::AhDLive(layout) = st.profile {
                use crate::dlive::{TAP_POST_GATE, TAP_POST_INSERT_B};
                use crate::strip::tap;
                let ids: &[usize] = match s.kind() {
                    StripKind::Channel => &[tap::IN, tap::FILT, 2, 3, 4, TAP_POST_GATE, TAP_POST_INSERT_B, tap::PFL],
                    StripKind::Bus if (s.id().max(0) as usize) < layout.aux_at() => &[3, tap::PFL],
                    _ => &[],
                };
                for &id in ids {
                    wanted[id] = true;
                }
            }
            s.taps.plan(&wanted, st.device.block_size, &self.garbage);
        }
    }

    /// Message thread: back to the strip's template (no extra blocks, no user taps).
    pub fn clear_blocks(&self, strip: &Strip) {
        let _st = self.lock();
        strip.set_layout(strip.chain().iter().map(|&n| Step::Node(n)).collect(), &self.garbage);
    }

    pub fn strips(&self, kind: StripKind) -> Vec<Arc<Strip>> {
        self.lock().list(kind).clone()
    }

    pub fn num_strips(&self, kind: StripKind) -> usize {
        self.lock().list(kind).len()
    }

    pub fn strip_at(&self, kind: StripKind, i: usize) -> Option<Arc<Strip>> {
        self.lock().list(kind).get(i).cloned()
    }

    pub fn channels(&self) -> Vec<Arc<Strip>> {
        self.strips(StripKind::Channel)
    }

    pub fn buses(&self) -> Vec<Arc<Strip>> {
        self.strips(StripKind::Bus)
    }

    pub fn num_channels(&self) -> usize {
        self.num_strips(StripKind::Channel)
    }

    pub fn channel_at(&self, i: usize) -> Option<Arc<Strip>> {
        self.strip_at(StripKind::Channel, i)
    }

    pub fn bus_in_slot(&self, slot: i32) -> Option<Arc<Strip>> {
        self.lock().buses.iter().find(|b| b.id() == slot).cloned()
    }

    pub fn find_channel(&self, id: i32) -> Option<Arc<Strip>> {
        self.lock().channels.iter().find(|s| s.id() == id).cloned()
    }

    /// A strip by kind and number as the console shows it: channels, aux,
    /// mains and matrices by position ("CH 3" = 3), buses by slot ("BUS 12" = 12).
    pub fn strip_by_number(&self, kind: StripKind, number: i32) -> Option<Arc<Strip>> {
        let i = usize::try_from(number.checked_sub(1)?).ok()?;
        let st = self.lock();
        match kind {
            StripKind::Bus => st.buses.iter().find(|b| b.id() as usize == i).cloned(),
            kind => st.list(kind).get(i).cloned(),
        }
    }

    /// The multitrack recorder / player.
    pub fn recorder(&self) -> &Recorder {
        &self.recorder
    }

    /// The USB recorder / player.
    pub fn usb(&self) -> &UsbDeck {
        &self.usb
    }

    /// The RTA, the LR vector scope and LISTEN (their controls are shared with the UI).
    pub fn analysis(&self) -> &Analyser {
        &self.analysis
    }

    /// Message thread: starts recording `tracks` from the current position
    /// (see [`Recorder::record`]) after checking that their sources exist.
    pub fn record(&self, tracks: &[RecordTrackSpec]) -> Result<(), String> {
        let num_hw_inputs = self.lock().device.num_hw_inputs;
        for t in tracks {
            let src = &t.source;
            if src.kind == SourceKind::HardwareInput {
                for port in src.port..src.port + t.num_channels.clamp(1, 2) as i32 {
                    // Playback channels, the USB PLAYER and the oscillators always exist.
                    let internal = playback_channel(port).is_some()
                        || usb_player_channel(port).is_some()
                        || crate::sources::osc_channel(port).is_some();
                    if !internal && usize::try_from(port).map_or(true, |p| p >= num_hw_inputs) {
                        return Err(format!("Input {} doesn't exist", port + 1));
                    }
                }
            } else {
                let kind = StripKind::from_i32(src.strip_kind).ok_or_else(|| "Unknown strip kind".to_owned())?;
                if self.strip_by_number(kind, src.strip_number).is_none() {
                    return Err(format!("{} {} doesn't exist", kind.label(), src.strip_number));
                }
            }
        }
        self.recorder.record(tracks)
    }

    /// Main 1 (the default monitor source).
    pub fn main(&self, i: usize) -> Option<Arc<Strip>> {
        self.strip_at(StripKind::Main, i)
    }

    /// STARTUP MAIN MUTE: the application calls this once at start-up (after
    /// loading its show). Mutes every main if the preference is on; returns
    /// whether it did.
    pub fn apply_startup_main_mute(&self) -> bool {
        if !self.controls.startup_main_mute.load(Relaxed) {
            return false;
        }
        for m in self.strips(StripKind::Main) {
            m.params.mute.store(true, Relaxed);
        }
        true
    }

    pub fn dca_name(&self, i: usize) -> String {
        self.names.lock().unwrap_or_else(|e| e.into_inner()).dcas.get(i).cloned().unwrap_or_default()
    }

    pub fn set_dca_name(&self, i: usize, name: &str) {
        let mut names = self.names.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(n) = names.dcas.get_mut(i) {
            *n = name.to_owned();
        }
        self.update_monitor_dcas(&names);
    }

    /// DCAs named MON A, MON B or MON LVL control the monitor level of the
    /// PHONES, the SPEAKERS or both, as on the WING (firmware 1.09).
    fn update_monitor_dcas(&self, names: &GroupNames) {
        for (path, roles) in self.dca_monitor.iter().enumerate() {
            let mut dcas = GroupSet::EMPTY;
            for (i, name) in names.dcas.iter().enumerate() {
                dcas.set(i, monitor_dca_paths(name) & (1 << path) != 0);
            }
            roles.store(dcas);
        }
    }

    /// The monitor paths (bit 0 = SPEAKERS, bit 1 = PHONES) whose level DCA `i` controls.
    pub fn dca_monitor_paths(&self, i: usize) -> u32 {
        (0..2).filter(|&path| self.dca_monitor[path].contains(i)).fold(0, |paths, path| paths | 1 << path)
    }

    /// The level offset (dB) and mute the MON DCAs give monitor path `path`.
    fn monitor_dca(&self, path: usize) -> (f32, bool) {
        let Some(roles) = self.dca_monitor.get(path) else { return (0.0, false) };
        if roles.is_empty() {
            return (0.0, false);
        }
        roles
            .load()
            .iter()
            .filter_map(|i| self.controls.dcas.get(i))
            .fold((0.0, false), |(db, muted), d| (db + d.fader_db.load(), muted || d.mute.load(Relaxed)))
    }

    /// ROUTING > SOURCES: every input port's source.
    pub fn sources(&self) -> &SourceTable {
        &self.sources
    }

    /// ROUTING > OUTPUTS: the sources patched straight to hardware outputs.
    pub fn output_patch(&self) -> &OutputPatch {
        &self.output_patch
    }

    /// Message thread: nothing feeds hardware output `port` any more: no
    /// strip, monitor path or source (an output has one source, as on the
    /// console).
    pub fn clear_output(&self, port: i32) {
        if port < 0 {
            return;
        }
        for s in self.lock().all() {
            for o in [&s.params.output_left, &s.params.output_right] {
                if o.load(Relaxed) == port {
                    o.store(-1, Relaxed);
                }
            }
        }
        let c = &self.controls;
        for o in [&c.monitor_output_left, &c.monitor_output_right, &c.phones.output_left, &c.phones.output_right] {
            if o.load(Relaxed) == port {
                o.store(-1, Relaxed);
            }
        }
        // An outboard insert's FX SEND.
        for f in &c.fx {
            for o in [&f.send_left, &f.send_right] {
                if o.load(Relaxed) == port {
                    o.store(-1, Relaxed);
                }
            }
        }
        self.output_patch.set(port, -1);
    }

    /// Message thread: patches source `source` (an input index as channels
    /// take it; -1 none) straight to hardware output `port`, replacing what
    /// fed it (manual 5.4 OUTPUTS). A bus, main or matrix signal goes to the
    /// strip's own output instead. A direct out ([`DirectSource`]: a channel,
    /// aux or bus PRE or POST FADER) is patched like a source. False when
    /// `port` can't be patched or `source` is no direct out it knows.
    pub fn route_output_source(&self, port: i32, source: i32) -> bool {
        if usize::try_from(port).map_or(true, |p| p >= NUM_PATCH_OUTPUTS) {
            return false;
        }
        if source >= DIRECT_SOURCE_BASE && DirectSource::from_code(source).is_none() {
            return false;
        }
        self.clear_output(port);
        if let Some((kind, number, side)) = crate::routing::strip_source(source) {
            if let Some(s) = self.strip_by_number(kind, number as i32 + 1) {
                if side == 0 { &s.params.output_left } else { &s.params.output_right }.store(port, Relaxed);
            }
            return true;
        }
        self.output_patch.set(port, source);
        true
    }

    /// Message thread: MONO / STEREO / MID-SIDE on ROUTING > SOURCES. The
    /// channels and aux patched to the pair's first source follow it, as on the
    /// console: stereo on the pair for STEREO and MID/SIDE, mono on the first
    /// source for MONO (MAIN and ALT inputs alike).
    pub fn set_source_mode(&self, index: usize, mode: i32) {
        self.sources.set_mode(index, mode);
        let first = crate::sources::pair_first(index);
        let Some(port) = crate::sources::source_port(first) else { return };
        let stereo = self.sources.params().get(first).is_some_and(|p| p.mode.load(Relaxed) != crate::sources::source_mode::MONO);
        for s in self.strips(StripKind::Channel).iter().chain(&self.strips(StripKind::Aux)) {
            let p = &s.params;
            if p.input_left.load(Relaxed) == port {
                p.stereo.store(stereo, Relaxed);
                p.input_right.store(if stereo { port + 1 } else { -1 }, Relaxed);
            }
            if p.alt_input_left.load(Relaxed) == port {
                p.alt_input_right.store(if stereo { port + 1 } else { -1 }, Relaxed);
            }
        }
    }

    /// Message thread: the MUTE key of a strip, with MUTEGROUP/SIP OVERRIDE
    /// (see [`solo::press_mute`]).
    pub fn press_mute(&self, s: &Strip) {
        solo::press_mute(&self.controls, s);
    }

    /// Message thread: the global MAIN / ALT switch (SETUP > AUDIO > INPUT
    /// SELECT, WING `/io/altsw`): stores it and switches every input and aux
    /// channel in GLOBAL INPUT SELECT ENABLE (`src_auto`) to MAIN or ALT. The
    /// other channels keep their own switch.
    pub fn set_global_alt(&self, alt: bool) {
        self.controls.global_alt.store(alt, Relaxed);
        for kind in [StripKind::Channel, StripKind::Aux] {
            for s in self.strips(kind) {
                if s.params.src_auto.load(Relaxed) {
                    s.params.alt_selected.store(alt, Relaxed);
                }
            }
        }
    }

    pub fn mute_group_name(&self, i: usize) -> String {
        self.names.lock().unwrap_or_else(|e| e.into_inner()).mute_groups.get(i).cloned().unwrap_or_default()
    }

    pub fn set_mute_group_name(&self, i: usize, name: &str) {
        if let Some(n) = self.names.lock().unwrap_or_else(|e| e.into_inner()).mute_groups.get_mut(i) {
            *n = name.to_owned();
        }
    }

    /// FX slot `i` (0..MAX_FX; the console uses the first `num_fx()`).
    pub fn fx(&self, i: usize) -> Option<&FxRack> {
        self.fx.get(i)
    }

    /// The FX slots in use: WING_FX to MAX_FX.
    pub fn num_fx(&self) -> usize {
        self.num_fx.load(Relaxed)
    }

    /// Message thread: sets the number of FX slots in use (SETUP > GENERAL >
    /// FX SLOTS), within WING_FX..=MAX_FX and never below a slot in use
    /// (`fx_in_use`), so fewer slots never drop a device, a plugin or an
    /// insert's assignment. Returns the number set.
    pub fn set_num_fx(&self, count: usize) -> usize {
        let st = self.lock();
        let needed = (0..MAX_FX).rev().find(|&i| self.slot_in_use(&st, i)).map_or(0, |i| i + 1);
        let n = count.clamp(WING_FX, MAX_FX).max(needed);
        self.num_fx.store(n, Relaxed);
        n
    }

    /// FX slot `i` holds something (any model but NONE: a device, a plugin
    /// or an outboard insert), or a strip's insert is assigned to it (on or off).
    pub fn fx_in_use(&self, i: usize) -> bool {
        self.slot_in_use(&self.lock(), i)
    }

    fn slot_in_use(&self, st: &Structure, i: usize) -> bool {
        let assigned = |s: &Arc<Strip>| s.assigns_fx(i);
        self.controls.fx.get(i).is_some_and(|p| p.model_index() != FX_NONE) || st.all().any(assigned)
    }

    /// Strips whose inserts currently use FX slot `i`.
    pub fn fx_users(&self, i: usize) -> Vec<Arc<Strip>> {
        self.lock().all().filter(|s| s.uses_fx(i)).cloned().collect()
    }

    pub fn total_dsp_faults(&self) -> i32 {
        self.lock().all().map(|s| s.meters.dsp_faults.load(Relaxed)).sum()
    }

    pub fn num_faulted_fx(&self) -> usize {
        self.fx.iter().filter(|f| f.is_faulted()).count()
    }

    /// Message thread: frees what the audio thread can no longer see, creates
    /// the devices of FX slots whose model was changed directly through the
    /// shared parameters (e.g. by a recall), and takes more FX slots in use when
    /// one past them got used that way (an insert from a channel preset, a copy,
    /// a remote): the console always shows every slot that runs.
    pub fn collect(&self) {
        self.set_num_fx(self.num_fx());
        self.sync_fx_devices();
        self.update_pdc();
        self.update_keys();
        self.update_taps();
        self.garbage.collect();
    }

    // =========================================================================
    // Plugin delay compensation (message thread)

    /// The compensation setting (DELAY COMPENSATION) and the plan's status.
    pub fn pdc(&self) -> &PdcState {
        &self.pdc
    }

    /// Message thread: works out the compensation plan from the plugins'
    /// latencies, the inserts and the sends (`pdc.rs`), publishes it and
    /// allocates the history rings it reads. Stage by stage as the audio
    /// thread sums: every summing strip aligns its sources to the latest one,
    /// and its own taps carry that latency on. Called by `collect`.
    pub fn update_pdc(&self) {
        let st = self.lock();
        let enabled = self.pdc.enabled.load(Relaxed);
        let fx: [u32; MAX_FX] =
            std::array::from_fn(|i| if enabled { self.fx[i].effective_latency(&self.controls.fx[i]) } else { 0 });
        let add = |own: [u32; pdc_tap::COUNT], base: u32| own.map(|l| l.saturating_add(base));

        for s in st.channels.iter().chain(&st.auxes) {
            s.pdc.set(s.own_latency(&fx), 0);
        }
        // The buses in the order the audio thread runs them (the buses
        // feeding a bus first; a send closing a loop is a block late and
        // not compensated), then mains, matrices.
        let mut buses = st.buses.clone();
        buses.sort_by_key(|b| b.id());
        let mut schedule = BusSchedule::new(buses.len());
        schedule.compute(|a, b| bus_send_enabled(&buses[a], &buses[b]));
        let bus_order: Vec<usize> =
            (0..schedule.num_waves()).flat_map(|w| schedule.wave(w).iter().map(|&b| b as usize)).collect();
        let summing: Vec<&Arc<Strip>> = bus_order.iter().map(|&b| &buses[b]).chain(&st.mains).chain(&st.matrices).collect();
        let feeds_of = |dst: &Strip| pdc_feeds(&st, &buses, &schedule, dst);
        for dst in &summing {
            let target = feeds_of(dst).iter().map(|(s, tap)| s.pdc.latency(*tap)).max().unwrap_or(0);
            dst.pdc.set(add(dst.own_latency(&fx), target), target);
        }

        // The rings the plan reads from, and what it can't make up.
        let mut needs_delay = false;
        let mut limited = false;
        for dst in &summing {
            let target = dst.pdc.target();
            for (s, tap) in feeds_of(dst) {
                let delay = target.saturating_sub(s.pdc.latency(tap)) as usize;
                if delay > 0 {
                    needs_delay = true;
                    limited |= delay > MAX_PDC;
                    s.pdc.ensure_ring(tap, st.device.block_size);
                }
            }
        }
        if !needs_delay {
            for s in st.all() {
                s.pdc.release_rings(&self.garbage);
            }
        }
        let total = st.all().map(|s| s.pdc.latency(pdc_tap::OUT)).max().unwrap_or(0);
        self.pdc.total.store(total, Relaxed);
        self.pdc.limited.store(limited, Relaxed);
    }

    /// Message thread: gives every strip a buffer for each tap point some
    /// GATE or DYNAMICS key takes from it (KEY SOURCE + KEY TAP, whether the
    /// slot is on or not), and retires the others (`keytap.rs`). Called by
    /// `collect`.
    pub fn update_keys(&self) {
        let st = self.lock();
        let mut masks: Vec<(&Arc<Strip>, u32)> = st.all().map(|s| (s, 0)).collect();
        for s in st.all() {
            let p = &s.params;
            // The gate runs on input channels only.
            let slots: &[&DynSlotParams] =
                if s.kind() == StripKind::Channel { &[p.gate(), p.dynamics()] } else { &[p.dynamics()] };
            for slot in slots {
                let (source, buffer) = key_tap(slot);
                let src = match source {
                    KeySource::SelfKey => None,
                    KeySource::Channel(i) => st.channels.get(i),
                    KeySource::Strip(kind, i) => st.list(kind).iter().find(|t| t.id() as usize == i),
                };
                if let Some(m) = src.and_then(|src| masks.iter_mut().find(|(t, _)| Arc::ptr_eq(t, src))) {
                    m.1 |= 1 << buffer;
                }
            }
        }
        for (s, mask) in masks {
            s.keys.plan(mask, st.device.block_size, &self.garbage);
        }
    }

    /// The console's FX devices (its profile's `ModelTables::fx`): what FX
    /// slots' model indexes mean.
    pub fn fx_models(&self) -> &'static [ModelSpec] {
        self.profile().spec().models.fx
    }

    /// Message thread: selects the device of FX slot `i` (an index into
    /// [`Engine::fx_models`]), creating it now. With `defaults`, its settings
    /// are reset to the device's defaults. Premium devices are refused beyond
    /// the console's premium slots (the WING's 1-8, the X32's 1-4).
    pub fn set_fx_model(&self, i: usize, model: usize, defaults: bool) -> bool {
        let (Some(rack), Some(params)) = (self.fx.get(i), self.controls.fx.get(i)) else { return false };
        let models = self.profile().spec().models;
        if model >= models.fx.len() || !allowed_in(models, model, i) {
            return false;
        }
        params.set_model_in(models.fx, model, defaults);
        self.install_fx_device(i, rack, model);
        true
    }

    /// Message thread: installs (or removes, with None) the plugin of FX slot
    /// `i`. Installing a plugin makes the slot an EXT slot with SOURCE PLUGIN
    /// (an outboard insert's settings stay); removing it empties an EXT slot.
    pub fn set_fx_plugin(&self, i: usize, cb: Option<InsertCallbacks>) {
        let (Some(rack), Some(params)) = (self.fx.get(i), self.controls.fx.get(i)) else { return };
        rack.set(cb, &self.garbage);
        if cb.is_some() {
            self.set_fx_model(i, FX_EXT, params.model_index() != FX_EXT);
            params.values[ext::SOURCE].store(ext::SOURCE_PLUGIN);
        } else if params.model_index() == FX_EXT {
            self.set_fx_model(i, FX_NONE, true);
        }
    }

    /// Message thread: as `set_fx_plugin` with a processor holding the same
    /// plugins as the slot's current one (a plugin chain rearranged): an
    /// auto-bypassed slot stays faulted.
    pub fn replace_fx_plugin(&self, i: usize, cb: InsertCallbacks) {
        let (Some(rack), Some(params)) = (self.fx.get(i), self.controls.fx.get(i)) else { return };
        rack.replace(cb, &self.garbage);
        if params.model_index() != FX_EXT {
            self.set_fx_model(i, FX_EXT, true); // (selecting a model clears the fault)
        }
    }

    fn fx_context(&self) -> FxContext {
        let st = self.lock();
        FxContext { sample_rate: st.device.sample_rate as f32, max_block: st.device.block_size }
    }

    /// Creates (or removes) the built-in device of slot `i` for `model`.
    fn install_fx_device(&self, i: usize, rack: &FxRack, model: usize) {
        let ctx = self.fx_context();
        let models = self.profile().spec().models;
        if rack.device_info() == Some((model, ctx)) && rack.device_table().is_none_or(|t| std::ptr::eq(t, models.fx)) {
            return;
        }
        let device = if model == FX_NONE || model == FX_EXT || !allowed_in(models, model, i) {
            None
        } else {
            FxDevice::new_in(models.fx, model, ctx).map(Box::new)
        };
        rack.set_device(device, &self.garbage);
    }

    fn sync_fx_devices(&self) {
        let fx = self.fx_models();
        for (i, (rack, params)) in self.fx.iter().zip(self.controls.fx.iter()).enumerate() {
            let model = params.model_index_in(fx);
            let wanted = !(model == FX_NONE || model == FX_EXT);
            let other_table = rack.device_table().is_some_and(|t| !std::ptr::eq(t, fx));
            if other_table || rack.device_info().map(|(m, _)| m) != wanted.then_some(model) {
                self.install_fx_device(i, rack, model);
            }
        }
    }

    pub fn set_device_running(&self, running: bool) {
        self.garbage.set_device_running(running);
    }

    // =========================================================================
    // Device

    /// Prepares for a device configuration. `in_map[hw]` / `out_map[hw]` give the
    /// index of hardware channel `hw` in the buffers passed to `process` (-1 = inactive).
    ///
    /// # Safety
    /// The audio callback must not be running (call it before the device starts).
    pub unsafe fn prepare(&self, sample_rate: f64, block_size: usize, in_map: &[i32], out_map: &[i32]) {
        let block_size = block_size.max(1);
        let mut st = self.lock();
        st.device = DeviceConfig { sample_rate, block_size, num_hw_inputs: in_map.len() };

        let a = &mut *self.audio.get();
        a.sample_rate = sample_rate;
        a.block_size = block_size;
        a.direct_scratch = vec![0.0; block_size];
        a.hw_in_to_active = in_map.to_vec();
        a.hw_out_to_active = out_map.to_vec();
        a.hw_in_ptrs = vec![std::ptr::null(); in_map.len()];
        a.hw_out_ptrs = vec![std::ptr::null_mut(); out_map.len()];
        a.monitor = std::array::from_fn(|_| [vec![0.0; block_size], vec![0.0; block_size]]);
        for d in &mut a.monitor_dsp {
            d.prepare(sample_rate, block_size);
        }
        a.sources.prepare(in_map.len(), block_size, sample_rate);
        self.oscillator.prepare(sample_rate, block_size);
        a.returns = ReturnClock::new(block_size);
        a.monitor_returns = ReturnRing::new(6, block_size);
        a.monitor_returns_live = [false; 3];

        for s in st.all() {
            s.prepare(sample_rate, block_size);
        }
        for f in self.fx.iter() {
            f.prepare(block_size);
        }
        drop(st);
        self.update_keys();
        self.recorder.prepare(sample_rate, block_size);
        self.usb.prepare(sample_rate, block_size);
        self.analysis.prepare(sample_rate);
        // Devices are built for a sample rate and block size: rebuild them.
        let fx = self.fx_models();
        for (i, (rack, params)) in self.fx.iter().zip(self.controls.fx.iter()).enumerate() {
            self.install_fx_device(i, rack, params.model_index_in(fx));
        }
        self.pool.set_block_duration_hint(sample_rate, block_size);
        self.update_round_trip();
    }

    /// Any thread: the interface's reported input and output latency in
    /// samples (`AudioIODevice::getInputLatencyInSamples` + output), part of
    /// an outboard insert's round trip.
    pub fn set_io_latency(&self, samples: u32) {
        self.io_latency.store(samples, Relaxed);
        self.update_round_trip();
    }

    /// An outboard insert's send leaves in one block and comes back at the
    /// earliest in the next: the round trip is a block plus the interface's latency.
    fn update_round_trip(&self) {
        let trip = (self.block_size() as u32).saturating_add(self.io_latency.load(Relaxed));
        for f in self.fx.iter() {
            f.set_round_trip(trip);
        }
    }

    /// Recall safes of sources and outputs.
    pub fn io_safes(&self) -> &IoSafes {
        &self.io_safes
    }

    pub fn sample_rate(&self) -> f64 {
        self.lock().device.sample_rate
    }

    pub fn block_size(&self) -> usize {
        self.lock().device.block_size
    }

    // =========================================================================
    // Audio thread

    /// Processes one callback. `ins`/`outs` are the *active* device channels.
    ///
    /// # Safety
    /// Audio thread only (never concurrently with itself or `prepare`). Every
    /// non-null pointer must be valid for `num_samples` samples.
    pub unsafe fn process(&self, ins: &[*const f32], outs: &[*mut f32], num_samples: usize) {
        let _ftz = ScopedNoDenormals::new();
        self.garbage.callback_started();
        let start = Instant::now();
        crate::trace::begin();

        for &o in outs {
            if !o.is_null() {
                std::slice::from_raw_parts_mut(o, num_samples).fill(0.0);
            }
        }

        let result = catch_unwind(AssertUnwindSafe(|| self.process_inner(ins, outs, num_samples)));
        if result.is_err() {
            // A bug must never take the show down: silence this block and report it.
            for &o in outs {
                if !o.is_null() {
                    std::slice::from_raw_parts_mut(o, num_samples).fill(0.0);
                }
            }
            self.controls.engine_faults.fetch_add(1, Relaxed);
        }

        self.garbage.callback_finished();

        let a = &*self.audio.get();
        crate::trace::mark("finish");
        crate::trace::end((1.0e9 * num_samples as f64 / a.sample_rate.max(1.0)) as u64);
        let load = (start.elapsed().as_secs_f64() * a.sample_rate / num_samples.max(1) as f64) as f32;
        self.controls.dsp_load.store(0.9 * self.controls.dsp_load.load() + 0.1 * load);
    }

    unsafe fn process_inner(&self, ins: &[*const f32], outs: &[*mut f32], num_samples: usize) {
        let topo = &*self.topology.load(Acquire);
        let a = &mut *self.audio.get();
        let block = a.block_size;

        // Devices may deliver more samples than announced: process in chunks.
        let mut start = 0;
        while start < num_samples {
            let n = block.min(num_samples - start);

            for (hw, &active) in a.hw_in_to_active.iter().enumerate() {
                a.hw_in_ptrs[hw] = if active >= 0 && (active as usize) < ins.len() && !ins[active as usize].is_null() {
                    ins[active as usize].add(start)
                } else {
                    std::ptr::null()
                };
            }
            for (hw, &active) in a.hw_out_to_active.iter().enumerate() {
                a.hw_out_ptrs[hw] = if active >= 0 && (active as usize) < outs.len() && !outs[active as usize].is_null() {
                    outs[active as usize].add(start)
                } else {
                    std::ptr::null_mut()
                };
            }

            // The recorder may shorten the chunk (loop end, ring switch).
            let n = self.recorder.begin_chunk(n);
            self.usb.begin_chunk(n); // USB deck hook: the player buffers, the recorder's commands
            a.block_counter += 1;
            self.process_chunk(topo, a, n);
            self.recorder.end_chunk(n);
            self.usb.end_chunk(n); // USB deck hook
            start += n;
        }
    }

    unsafe fn process_chunk(&self, topo: &Topology, a: &mut AudioState, n: usize) {
        let rec = &self.recorder;
        let pos = a.returns.pos;
        // Sources first: the channels and the recorder's input streams read
        // the processed ports (gain, mute, polarity, stereo / mid-side pairs).
        a.sources.process(
            &self.sources,
            &a.hw_in_ptrs,
            rec.playback_ptrs(),
            self.usb.player_ptrs(),
            n,
            &self.controls.mute_groups.load(),
            pos,
        );
        let streams = rec.record_streams();
        // Record hardware inputs (and playback channels) as they arrive.
        self.record_inputs(streams, a.sources.hw_ptrs(), a.sources.playback_ptrs(), n);
        crate::trace::mark("sources");

        // Talkback and the console's solos and mutes for this block.
        let talk = TalkBlock::new(&self.controls.talk, |kind, i| topo.by_number(kind, i as i32 + 1).map(|s| s.as_ref()));
        // SOURCE SOLO: the host plays the soloed source, and solos (monitor.rs).
        let m = &self.controls.monitor;
        let source_solo = m.host().and_then(|(kind, i)| topo.by_number(kind, i as i32 + 1)).and_then(|host| {
            m.source_solo_patch(&self.sources, host.params.solo.load(Relaxed)).map(|patch| (host.as_ref(), patch))
        });
        let solo = solo::console_pass(&self.controls, topo.all(), talk.active, source_solo.map(|(s, _)| s));
        let talk = &talk;
        // What is read a block late this block, and the bus stage's order (routing.rs).
        self.plan_returns(topo, a, n, source_solo.map(|(_, p)| p));
        self.tick_selectors(topo, a.sample_rate, n);
        // The send effects, on the buses' last block, for the FX return channels.
        self.send_effects(a, n);
        crate::trace::mark("plan+sendfx");

        let hw = SharedPtrs(a.sources.hw_ptrs());
        let hw = &hw;
        let playback = SharedPtrs(a.sources.playback_ptrs());
        let playback = &playback;
        let groups = GroupState {
            mute_groups: self.controls.mute_groups.load(),
            dca_mute_groups: self.controls.dca_mute_groups.load(Relaxed),
            main_link_last: self.controls.main_link_last(),
            main_link_db: topo.mains.first().map_or(0.0, |m| m.params.fader_db.load()),
            source_solo,
        };
        let block = a.block_counter;
        // The analysed and listened strips of this chunk (analyser.rs).
        let probes = self.analysis.begin_block(topo.all());
        let probes = &probes;
        crate::trace::mark("probes");

        // 1. Channels and aux channels (parallel), in waves: a strip keyed
        // from another channel runs after it (keytap.rs).
        let num_ch = topo.channels.len();
        let stage1 = |i: usize| if i < num_ch { &topo.channels[i] } else { &topo.auxes[i - num_ch] };
        (*topo.key_schedule.get()).compute(num_ch, |i| {
            let s = stage1(i);
            key_sources(&s.params, s.kind() == StripKind::Channel)
        });
        let schedule = &*topo.key_schedule.get();
        let ctx_of = |hw, wave| self.context(topo, hw, playback.get(), &groups, block, probes, schedule.waves(), wave);
        let ctx_of = &ctx_of;
        for w in 0..schedule.num_waves() {
            let strips = schedule.wave(w);
            let strips = &strips;
            self.pool.parallel_for(strips.len(), &|j| {
                stage1(strips.get(j)).process(&ctx_of(hw.get(), Some(w as u8)), n);
            });
            crate::trace::mark("channels");
        }

        // Only inputs with signal feed the buses (unused channels cost nothing below).
        {
            let active = &mut *topo.active_inputs.get();
            active.clear();
            for s in topo.inputs() {
                if !s.dsp().idle() && active.len() < active.capacity() {
                    active.push(s.clone());
                }
            }
        }

        // 2. Automix
        self.automix(topo, n);
        crate::amm::process(&self.amm, &mut a.amm, &topo.channels, a.sample_rate, n);
        // The inputs' compensation rings, now that automix and the AMM have scaled them.
        for s in topo.inputs() {
            s.write_pdc(n);
        }
        crate::trace::mark("automix+pdc");

        // A console's own oscillator, for the buses and mains it plays on.
        self.oscillator.generate(n);
        let osc = &self.oscillator;

        // 3./4. Buses, in waves: a bus runs after the buses sending to it
        // (a send that closes a loop reads its source's previous block).
        let ramps = &self.talk_ramps;
        let schedule = &*topo.bus_schedule.get();
        for w in 0..schedule.num_waves() {
            let wave = schedule.wave(w);
            self.pool.parallel_for(wave.len(), &|j| {
                let index = wave[j] as usize;
                let bus = &topo.bus_list[index];
                sum_bus(bus, index, topo, schedule, pos, block, n);
                add_talk(bus, talk.bus(bus.id() as usize), ramps.get(StripKind::Bus, bus.id() as usize), talk, n);
                osc.add_to(bus, StripKind::Bus, bus.id() as usize, n);
                bus.process(&ctx_of(hw.get(), None), n);
                bus.write_pdc(n);
                bus.write_returns(pos, n);
            });
            crate::trace::mark("buses");
        }

        // 5. Mains
        self.pool.parallel_for(topo.mains.len(), &|i| {
            let main = &topo.mains[i];
            sum_main(main, i, topo.active().iter().chain(topo.buses()), block, n);
            add_talk(main, talk.main(i), ramps.get(StripKind::Main, i), talk, n);
            osc.add_to(main, StripKind::Main, i, n);
            main.process(&ctx_of(hw.get(), None), n);
            main.write_pdc(n);
            main.write_returns(pos, n);
        });
        crate::trace::mark("mains");

        // 6. Matrices (DIRECT IN: the monitor paths of the previous block)
        let monitor_direct = MonitorReturns(std::array::from_fn(|ch| {
            if a.monitor_returns_live[ch / 2] {
                a.monitor_returns.read(ch, pos, n).map_or(std::ptr::null(), <[f32]>::as_ptr)
            } else {
                std::ptr::null()
            }
        }));
        let monitor_direct = &monitor_direct;
        self.pool.parallel_for(topo.matrices.len(), &|i| {
            let mtx = &topo.matrices[i];
            sum_matrix(mtx, i, topo, monitor_direct.get(), block, n);
            add_talk(mtx, talk.matrix(i), ramps.get(StripKind::Matrix, i), talk, n);
            osc.add_to(mtx, StripKind::Matrix, i, n);
            mtx.process(&ctx_of(hw.get(), None), n);
            mtx.write_pdc(n);
            mtx.write_returns(pos, n);
        });
        crate::trace::mark("matrices");

        // The user signals, now that every strip has run (outputs take them now).
        self.user_signals(topo, a, n);

        // Record strip taps, now that every strip has run.
        self.record_strips(topo, streams, n);
        self.record_usb(topo, a, n);
        crate::trace::mark("user+record");

        // 7. Solo / monitor
        self.build_monitor(topo, a, n, solo, talk, probes);
        crate::trace::mark("monitor");

        // 8. Outputs, then the safety clip
        self.write_outputs(topo, a, n);
        self.protect_outputs(a, n);
        a.returns.advance(n);
        crate::trace::mark("outputs");
    }

    /// Before the stages: which bus, main and matrix signals are read a
    /// block late this block (a channel patched to one, a bus send closing a
    /// loop), the bus stage's waves, the channels' pointers to the strip
    /// sources, and which monitor paths the matrices' DIRECT IN reads.
    unsafe fn plan_returns(&self, topo: &Topology, a: &mut AudioState, n: usize, solo: Option<SoloPatch>) {
        let pos = a.returns.pos;
        let want = |input: i32| {
            if let Some((kind, number, _)) = strip_source(input) {
                if let Some(s) = topo.by_number(kind, number as i32 + 1) {
                    s.want_return(return_tap::OUT);
                }
            }
        };
        for s in topo.inputs() {
            let (l, r) = s.active_inputs();
            want(l);
            want(if r < 0 && l >= 0 && s.params.stereo.load(Relaxed) { l + 1 } else { r });
        }
        for p in self.controls.fx.iter().take(self.num_fx().min(NUM_SEND_FX)).filter(|p| p.is_send()) {
            want(p.source_left.load(Relaxed));
            want(p.source_right.load(Relaxed));
        }
        // A mix's Ext In from another mix, or from PAFL (a channel's patch can name PAFL too).
        let mut pafl = topo.inputs().any(|s| {
            let (l, r) = s.active_inputs();
            crate::routing::pafl_channel(l).is_some() || crate::routing::pafl_channel(r).is_some()
        });
        for s in topo.buses().chain(&topo.mains).chain(&topo.matrices) {
            let first = s.mix_input.ext_source.load(Relaxed);
            if first >= 0 {
                want(first);
                if s.params.stereo.load(Relaxed) {
                    want(first + 1);
                }
                pafl |= crate::routing::pafl_channel(first).is_some();
            }
        }
        if let Some(p) = solo {
            want(p.left);
            want(if p.right >= 0 { p.right } else { p.left + 1 });
        }

        let schedule = &mut *topo.bus_schedule.get();
        let buses = &topo.bus_list;
        schedule.compute(|from, to| bus_send_enabled(&buses[from], &buses[to]));
        if schedule.num_waves() > 1 {
            for (to, dst) in buses.iter().enumerate() {
                for (from, src) in buses.iter().enumerate() {
                    if from == to || !schedule.is_late(from, to) || !bus_send_enabled(src, dst) {
                        continue;
                    }
                    let pre = src.sends.find(dst.id() as usize).is_some_and(|s| s.mode.load(Relaxed) == send_mode::PRE);
                    src.want_return(if pre { return_tap::TAP } else { return_tap::OUT });
                }
            }
        }
        for s in buses.iter().chain(&topo.mains).chain(&topo.matrices) {
            s.commit_returns();
        }
        for slot in 0..NUM_STRIP_SOURCES {
            let ptr = strip_source(STRIP_SOURCE_BASE + slot as i32)
                .and_then(|(kind, number, side)| {
                    topo.by_number(kind, number as i32 + 1)?.read_return(return_tap::OUT, side, pos, n)
                })
                .map_or(std::ptr::null(), <[f32]>::as_ptr);
            a.sources.set_strip_return(slot, ptr);
        }

        let mut wanted = [false; 3];
        for m in &topo.matrices {
            if let Some(pair) = matrix_direct_pair(m.params.direct_source.load(Relaxed)) {
                wanted[pair] |= m.params.direct_on.load(Relaxed);
            }
        }
        wanted[2] |= pafl;
        for (pair, &w) in wanted.iter().enumerate() {
            if w && !a.monitor_returns_live[pair] {
                a.monitor_returns.clear(2 * pair);
                a.monitor_returns.clear(2 * pair + 1);
            }
            a.monitor_returns_live[pair] = w;
        }
        for side in 0..2 {
            let ptr = if pafl {
                a.monitor_returns.read(4 + side, pos, n).map_or(std::ptr::null(), <[f32]>::as_ptr)
            } else {
                std::ptr::null()
            };
            a.sources.set_pafl_return(side, ptr);
        }
    }

    /// The send effects (an X32's FX 1-4): each slot with sources processes
    /// their signals (one block late, as channels read them) into its FX
    /// returns, which the channels read this block; the others' are silent.
    unsafe fn send_effects(&self, a: &mut AudioState, n: usize) {
        let count = self.num_fx().min(NUM_SEND_FX);
        let live = |slot: usize| slot < count && self.controls.fx[slot].is_send();
        for slot in 0..NUM_SEND_FX {
            a.sources.fx_return_live(slot, live(slot));
        }
        let block = a.block_counter;
        let sources = &mut a.sources;
        for slot in (0..count).filter(|&s| live(s)) {
            let Some(bufs) = sources.fx_return_ptrs(slot, n) else { continue };
            let p = &self.controls.fx[slot];
            let left = p.source_left.load(Relaxed);
            let right = Some(p.source_right.load(Relaxed)).filter(|&r| r >= 0).unwrap_or(left);
            let (hw, playback) = (sources.hw_ptrs(), sources.playback_ptrs());
            // SAFETY: the return buffers are only written here; a source is a
            // bus, main or matrix signal (never a return).
            let [l, r] = bufs.map(|b| std::slice::from_raw_parts_mut(b, n));
            for (buf, input) in [(&mut *l, left), (&mut *r, right)] {
                match source_ptr(hw, playback, input).filter(|_| strip_source(input).is_some()) {
                    Some(src) => buf.copy_from_slice(std::slice::from_raw_parts(src, n)),
                    None => buf.fill(0.0),
                }
            }
            let mut chans: [&mut [f32]; 2] = [l, r];
            self.fx[slot].process(&mut chans, a.sample_rate, p, &FxInputs { hw, playback }, block);
        }
    }

    /// The user signals (USR 1-24): each routed one copies its strip's tap
    /// (PRE) or processed signal (POST: an input or aux channel after its
    /// processing, before the pan; a bus, main or matrix its output), one
    /// side or both mixed (manual 2.6), then gets its source processing.
    unsafe fn user_signals(&self, topo: &Topology, a: &mut AudioState, n: usize) {
        for (i, u) in self.sources.user_signals().iter().enumerate() {
            let Some(buf) = a.sources.user_raw_mut(i, n) else { continue };
            match u.strip().and_then(|(kind, number)| topo.by_number(kind, number)) {
                Some(s) => copy_user_signal(s, u, buf, n),
                None => buf.fill(0.0),
            }
        }
        a.sources.process_user_signals(&self.sources, a.returns.pos, n, &self.controls.mute_groups.load());
    }

    #[allow(clippy::too_many_arguments)]
    fn context<'a>(
        &'a self,
        topo: &'a Topology,
        hw: &'a [*const f32],
        playback: &'a [*const f32],
        g: &GroupState<'a>,
        block: u64,
        probes: &'a Probes,
        key_waves: &'a [u8],
        wave: Option<u8>,
    ) -> ProcessContext<'a> {
        ProcessContext {
            fx: &self.fx,
            fx_params: &self.controls.fx,
            amm: &self.amm,
            dcas: &self.controls.dcas,
            mute_groups: g.mute_groups,
            dca_mute_groups: g.dca_mute_groups,
            main_link_last: g.main_link_last,
            main_link_db: g.main_link_db,
            hw_inputs: hw,
            playback,
            channels: &topo.channels,
            key_strips: KeyStrips {
                auxes: &topo.auxes,
                buses: &topo.bus_list,
                // SAFETY: the schedule is computed before the stages and only read while they run.
                bus_waves: unsafe { (*topo.bus_schedule.get()).waves() },
                mains: &topo.mains,
                matrices: &topo.matrices,
            },
            key_waves,
            wave,
            block,
            probes,
            source_solo: g.source_solo,
        }
    }

    /// Records the hardware-input streams of this chunk (a port can also be a
    /// playback channel).
    unsafe fn record_inputs(&self, streams: &[RecordStream], hw: &[*const f32], playback: &[*const f32], n: usize) {
        for (i, st) in streams.iter().enumerate() {
            if st.source.kind == SourceKind::HardwareInput {
                let port = st.source.port + st.channel as i32;
                let src = source_ptr(hw, playback, port).map(|p| std::slice::from_raw_parts(p, n));
                self.recorder.write_record_stream(i, src, n);
            }
        }
    }

    /// Records the strip taps of this chunk: the pre-fader (PFL) tap, or the
    /// output after fader and pan. A strip that doesn't exist records silence.
    unsafe fn record_strips(&self, topo: &Topology, streams: &[RecordStream], n: usize) {
        for (i, st) in streams.iter().enumerate() {
            let pre = match st.source.kind {
                SourceKind::HardwareInput => continue,
                SourceKind::StripPreFader => true,
                SourceKind::StripPostFader => false,
            };
            let strip = StripKind::from_i32(st.source.strip_kind).and_then(|k| topo.by_number(k, st.source.strip_number));
            let src = strip.map(|s| if pre { s.dsp().pfl(st.channel, n) } else { s.dsp().out(st.channel, n) });
            self.recorder.write_record_stream(i, src, n);
        }
    }

    // ---- USB deck hooks -------------------------------------------------------------

    /// The RECORDER output group (`/io/out/REC`): each of the USB recorder's
    /// four ports records a source or a strip's output as its hardware output
    /// carries it (input and aux channels their direct out, the PFL copy;
    /// the others after fader and pan). Metered while not recording.
    unsafe fn record_usb(&self, topo: &Topology, a: &AudioState, n: usize) {
        let (hw, playback) = (a.sources.hw_ptrs(), a.sources.playback_ptrs());
        for port in 0..USB_CHANNELS {
            let src = match self.usb.rec_tap(port) {
                RecTap::Off => None,
                RecTap::Source(p) => source_ptr(hw, playback, p).map(|p| std::slice::from_raw_parts(p, n)),
                RecTap::Strip { kind, number, channel } => StripKind::from_i32(kind)
                    .and_then(|k| topo.by_number(k, number))
                    .map(|s| if s.kind().is_input() { s.dsp().pfl(channel, n) } else { s.dsp().out(channel, n) }),
                RecTap::Direct(code) => DirectSource::from_code(code).and_then(|d| direct_signal(topo, d, n)),
            };
            self.usb.write_rec(port, src, n);
        }
    }

    /// Gain sharing between input channels whose INS 2 is set to AUTO_X /
    /// AUTO_Y, for the groups whose X / Y ENABLE is on. Channels leaving
    /// automix (or in a disabled group) glide back to unity.
    unsafe fn automix(&self, topo: &Topology, n: usize) {
        let enabled = [self.controls.automix_enable[0].load(Relaxed), self.controls.automix_enable[1].load(Relaxed)];
        let group_of = |s: &Strip| s.automix_group().filter(|&g| enabled[g]);
        let mut totals = [0.0f32; 2];
        for s in topo.active() {
            if let Some(g) = group_of(s) {
                totals[g] += s.dsp().automix_power();
            }
        }
        for s in topo.active() {
            let gain = match group_of(s) {
                // Power share -> gain; never below -40 dB so nobody vanishes completely.
                Some(g) if totals[g] > 1e-12 => (s.dsp().automix_power() / totals[g]).sqrt().max(0.01),
                // A silent group keeps its gains: the last active channel stays on.
                Some(_) => s.dsp().automix_gain(),
                None => 1.0,
            };
            if gain != 1.0 || s.dsp().automix_gain() != 1.0 {
                s.apply_automix(gain, n);
            }
        }
    }

    /// Stage 7: the two monitor paths, SPEAKERS and PHONES. Soloed
    /// strips go to the solo bus of the paths the SOLO MODE routes them to
    /// (PFL attenuated by the path's PFL DIM; a BAND SOLO boosted by its BAND
    /// SOLO); each path adds its SOURCE (at SRC MIX while a solo reaches it)
    /// and DIRECT IN, runs its EQ, PAN / WIDTH, INVERT and DELAY, then its
    /// level (with the MON DCAs), on the selected path(s) DIM, mute, the talk
    /// MON DIM, MONO and SWAP, and last its LIMITER (`monitor.rs`).
    unsafe fn build_monitor(
        &self,
        topo: &Topology,
        a: &mut AudioState,
        n: usize,
        solo: SoloBlock,
        talk: &TalkBlock,
        probes: &Probes,
    ) {
        let c = &self.controls;
        // LISTEN (band / key / xover solo) replaces the soloed strips (analyser.rs).
        let listen = self.analysis.listen_overrides_solo();
        let any_solo = solo.any_solo || listen;
        c.any_solo_active.store(any_solo, Relaxed);
        if !any_solo && c.solo_bus_override.load(Relaxed) {
            c.solo_bus_override.store(false, Relaxed);
        }
        let selected = solo::path_mask(c.solo_bus.load(Relaxed));
        let live = c.solo_mode.load(Relaxed) == solo_mode::LIVE && !c.solo_bus_override.load(Relaxed);
        let afl = StripKind::ALL.map(|k| c.solo_is_afl(k)); // indexed by kind
        let sources = [&c.speakers, &c.phones.source];
        let pfl_dim = sources.map(|s| db_to_gain(-s.pfl_dim_db.load().max(0.0)));

        for path in &mut a.monitor {
            for side in path.iter_mut() {
                side[..n].fill(0.0);
            }
        }

        // The solo buses.
        let mut solo_paths = 0;
        for s in topo.all() {
            let soloed = any_solo && !listen && s.console.is_soloed();
            let route = match (soloed, live) {
                (false, _) => 0,
                (true, true) => s.live_solo_paths(&c.dcas),
                (true, false) => selected,
            };
            solo_paths |= route;
            let is_afl = afl[s.kind() as usize];
            for (path, bus) in a.monitor.iter_mut().enumerate() {
                let target = if route & (1 << path) == 0 {
                    0.0
                } else if is_afl {
                    1.0
                } else {
                    pfl_dim[path]
                };
                let last = if path == 0 { *s.last_solo() } else { s.console.solo_ramp_b.load() };
                if target != 0.0 || last != 0.0 {
                    let [l, r] = bus;
                    add_with_gain_ramp(&mut l[..n], solo_tap(s, is_afl, 0, n), last, target);
                    add_with_gain_ramp(&mut r[..n], solo_tap(s, is_afl, 1, n), last, target);
                }
                if path == 0 {
                    *s.last_solo() = target;
                } else {
                    s.console.solo_ramp_b.store(target);
                }
            }
        }

        // LISTEN plays on the first selected path (the solo bus it replaces).
        // Called every block: it runs its own fades in and out.
        let listen_path = if selected & 1 != 0 { 0 } else { 1 };
        {
            let boost_db = c.monitor.paths[listen_path].band_solo_db.load();
            let boost = db_to_gain(if boost_db.is_finite() { boost_db.clamp(0.0, BAND_SOLO_MAX_DB) } else { 0.0 });
            let d = &mut a.monitor_dsp[listen_path];
            let [l, r] = &mut a.monitor[listen_path];
            self.analysis.add_listen(&mut l[..n], &mut r[..n], n, d.band_gain, boost);
            d.band_gain = boost;
        }
        if listen {
            solo_paths |= 1 << listen_path;
        }
        // The solo bus (the selected path's, before its SOURCE) for a
        // matrix DIRECT IN MON.BUS next block.
        let pos = a.returns.pos;
        if a.monitor_returns_live[2] {
            let [l, r] = &a.monitor[listen_path];
            a.monitor_returns.write(4, pos, &l[..n]);
            a.monitor_returns.write(5, pos, &r[..n]);
        }

        // Each path's SOURCE (Main 1 by default), crossfaded when it changes.
        for (path, src) in sources.iter().enumerate() {
            let index = src.source.load(Relaxed);
            let mut target = db_to_gain(src.source_level_db.load());
            if solo_paths & (1 << path) != 0 {
                target *= db_to_gain(src.source_mix_db.load());
            }
            let r = &mut a.monitor_ramps[path];
            let bus = &mut a.monitor[path];
            if index != r.source_index {
                add_monitor_source(topo, bus, r.source_index, r.source, 0.0, n);
                r.source = 0.0;
                r.source_index = index;
            }
            add_monitor_source(topo, bus, index, r.source, target, n);
            r.source = target;
        }

        // DIRECT IN: any input, aux, bus, main or matrix output at its level,
        // crossfaded when it changes.
        for (path, (p, d)) in c.monitor.paths.iter().zip(a.monitor_dsp.iter_mut()).enumerate() {
            let index = p.direct_source.load(Relaxed);
            let level = p.direct_level_db.load();
            let target = if direct_source(index).is_some() && level.is_finite() { db_to_gain(level) } else { 0.0 };
            let bus = &mut a.monitor[path];
            if index != d.direct_source {
                add_direct_in(topo, bus, d.direct_source, d.direct_gain, 0.0, n);
                d.direct_gain = 0.0;
                d.direct_source = index;
            }
            add_direct_in(topo, bus, index, d.direct_gain, target, n);
            d.direct_gain = target;
        }

        // Level (+ MON DCAs); DIM, mute, talk MON DIM, MONO and SWAP on the selected path(s).
        let (dim, mute) = (c.monitor_dim.load(Relaxed), c.monitor_mute.load(Relaxed));
        let (mono, swap) = (c.monitor_mono.load(Relaxed), c.monitor_swap.load(Relaxed));
        let talk_dim = talk.mon_dim();
        for path in 0..2 {
            let (level_db, dim_db, peak) = if path == 0 {
                (&c.monitor_level_db, &c.monitor_dim_db, &c.monitor_peak)
            } else {
                (&c.phones.level_db, &c.phones.dim_db, &c.phones.peak)
            };
            let (dca_db, dca_muted) = self.monitor_dca(path);
            let mut level = if dca_muted { 0.0 } else { db_to_gain(level_db.load() + dca_db) };
            let on_selected = selected & (1 << path) != 0;
            if on_selected {
                if dim {
                    level *= db_to_gain(-dim_db.load().max(0.0));
                }
                if mute {
                    level = 0.0;
                }
                level *= talk_dim;
            }
            let r = &mut a.monitor_ramps[path];
            let [l, rr] = &mut a.monitor[path];
            let (l, rr) = (&mut l[..n], &mut rr[..n]);
            // EQ, PAN / WIDTH, INVERT, DELAY; the RTA of MONITORS EQ reads
            // either side of the EQ.
            let dsp = &mut a.monitor_dsp[path];
            let mon = &c.monitor.paths[path];
            let rta = probes.monitor_rta(path);
            if rta == Some(rta_tap::MONITOR_PRE_EQ) {
                probes.push_rta([&*l, &*rr], 2, n);
            }
            dsp.process_eq(mon, l, rr);
            if rta == Some(rta_tap::MONITOR_POST_EQ) {
                probes.push_rta([&*l, &*rr], 2, n);
            }
            dsp.process_image(mon, l, rr);
            apply_gain_ramp(l, r.level, level);
            apply_gain_ramp(rr, r.level, level);
            r.level = level;

            // MONO (L+R)/2 on both sides, else SWAP L and R; crossfaded over a block.
            let matrix = match (on_selected && mono, on_selected && swap) {
                (true, _) => MONITOR_MONO,
                (false, true) => MONITOR_SWAP,
                _ => MONITOR_STEREO,
            };
            apply_monitor_matrix(l, rr, r.matrix, matrix);
            r.matrix = matrix;

            mon.limiter_gr_db.fetch_max(dsp.process_limiter(mon, l, rr));
            if !all_finite(l) || !all_finite(rr) {
                // A blown-up EQ or delay must not stay stuck: start the path over.
                dsp.reset();
                l.fill(0.0);
                rr.fill(0.0);
            }
            let (pl, pr) = (peak_level(l), peak_level(rr));
            peak[0].fetch_max(pl);
            peak[1].fetch_max(pr);
            self.monitor_remote_peaks[path][0].fetch_max(pl);
            self.monitor_remote_peaks[path][1].fetch_max(pr);
            // The path as a matrix DIRECT IN (MON.PH / MON.SPK) next block.
            let pair = if path == 1 { 0 } else { 1 };
            if a.monitor_returns_live[pair] {
                a.monitor_returns.write(2 * pair, pos, l);
                a.monitor_returns.write(2 * pair + 1, pos, rr);
            }
        }
    }
    unsafe fn write_outputs(&self, topo: &Topology, a: &mut AudioState, n: usize) {
        let outs = &a.hw_out_ptrs;
        let add_to = |hw: i32, src: &[f32]| {
            if hw >= 0 && (hw as usize) < outs.len() {
                let dst = outs[hw as usize];
                if !dst.is_null() {
                    let dst = std::slice::from_raw_parts_mut(dst, n);
                    dst.iter_mut().zip(src).for_each(|(d, s)| *d += s);
                }
            }
        };

        for s in topo.all() {
            let l = s.params.output_left.load(Relaxed);
            let r = s.params.output_right.load(Relaxed);
            if l < 0 && r < 0 {
                continue;
            }
            let d = s.dsp();
            if s.kind().is_input() {
                if d.idle() {
                    continue;
                }
                // Channel direct outs carry the processed signal before the fader.
                add_to(l, d.pfl(0, n));
                add_to(r, d.pfl(1, n));
            } else {
                add_to(l, d.out(0, n));
                add_to(r, d.out(1, n));
            }
        }

        // Sources patched straight to outputs (ROUTING > OUTPUTS): the
        // processed source, a user signal of this block.
        let (hw, internal) = (a.sources.hw_ptrs(), a.sources.playback_ptrs());
        for (port, source) in self.output_patch.ports.iter().enumerate().take(outs.len()) {
            let source = source.load(Relaxed);
            if source < 0 {
                continue;
            }
            if let Some(direct) = DirectSource::from_code(source) {
                let dlive = topo
                    .by_number(direct.kind, direct.number)
                    .filter(|s| s.kind() == StripKind::Channel && s.spec().dlive_layout.is_some());
                if let Some(s) = dlive {
                    let last = &mut a.direct_gain[port];
                    let signal = dlive_direct_out(
                        s,
                        direct.side,
                        &self.dlive,
                        &self.controls.dcas,
                        last,
                        &mut a.direct_scratch,
                        a.block_counter,
                        n,
                    );
                    add_to(port as i32, signal);
                } else if let Some(signal) = direct_signal(topo, direct, n) {
                    add_to(port as i32, signal);
                }
            } else if let Some((kind, number, side)) = strip_source(source) {
                // A bus, main or matrix (an X32's output signals): its output,
                // on as many outputs as name it.
                if let Some(s) = topo.by_number(kind, number as i32 + 1) {
                    let d = s.dsp();
                    if !d.idle() {
                        add_to(port as i32, d.out(side.min(d.channels().saturating_sub(1)), n));
                    }
                }
            } else if let Some(p) = source_ptr(hw, internal, source) {
                add_to(port as i32, std::slice::from_raw_parts(p, n));
            }
        }

        let c = &self.controls;
        let [speakers, phones] = &a.monitor;
        add_to(c.monitor_output_left.load(Relaxed), &speakers[0][..n]);
        add_to(c.monitor_output_right.load(Relaxed), &speakers[1][..n]);
        add_to(c.phones.output_left.load(Relaxed), &phones[0][..n]);
        add_to(c.phones.output_right.load(Relaxed), &phones[1][..n]);

        // FX SEND: the outboard inserts that ran this block.
        let block = a.block_counter;
        for (rack, p) in self.fx.iter().zip(c.fx.iter()) {
            for (side, port) in [&p.send_left, &p.send_right].into_iter().enumerate() {
                let port = port.load(Relaxed);
                if port >= 0 {
                    if let Some(send) = rack.send(side, n, block) {
                        add_to(port, send);
                    }
                }
            }
        }
    }

    unsafe fn protect_outputs(&self, a: &AudioState, n: usize) {
        if !self.controls.output_protection.load(Relaxed) {
            return;
        }
        let ceiling = db_to_gain(self.controls.output_ceiling_db.load()).clamp(0.0, 4.0);
        let mut engaged = false;
        for &o in &a.hw_out_ptrs {
            if o.is_null() {
                continue;
            }
            let out = std::slice::from_raw_parts_mut(o, n);
            if !all_finite(out) {
                out.fill(0.0);
                engaged = true;
            } else if peak_level(out) > ceiling {
                out.iter_mut().for_each(|x| *x = x.clamp(-ceiling, ceiling));
                engaged = true;
            }
        }
        if engaged {
            self.controls.output_clip_events.fetch_add(1, Relaxed);
        }
    }
}

/// The signal a soloed strip puts on the solo bus: its PFL copy or, AFL, its
/// output.
///
/// Solo-tap override hook: a solo that listens elsewhere in the strip (EQ
/// band, gate key or crossover solo) replaces the signal here.
#[inline]
unsafe fn solo_tap(s: &Strip, afl: bool, ch: usize, n: usize) -> &[f32] {
    let d = s.dsp();
    if afl {
        d.out(ch, n)
    } else {
        d.pfl(ch, n)
    }
}

/// Adds monitor SOURCE `index` (a main, matrix, bus or aux output) to a
/// monitor path with a ramped gain.
unsafe fn add_monitor_source(topo: &Topology, bus: &mut [Vec<f32>; 2], index: i32, last: f32, target: f32, n: usize) {
    if last == 0.0 && target == 0.0 {
        return;
    }
    let strip = match monitor_source(index) {
        Some((StripKind::Main, i)) => topo.mains.get(i),
        Some((StripKind::Matrix, i)) => topo.matrices.get(i),
        Some((StripKind::Bus, i)) => topo.buses().find(|b| b.id() as usize == i),
        Some((StripKind::Aux, i)) => topo.auxes.get(i),
        _ => None,
    };
    let Some(s) = strip else { return };
    let d = s.dsp();
    if d.idle() {
        return;
    }
    let [l, r] = bus;
    add_with_gain_ramp(&mut l[..n], d.out(0, n), last, target);
    add_with_gain_ramp(&mut r[..n], d.out(1, n), last, target);
}

/// Adds monitor DIRECT IN `index` (an input, aux, bus, main or matrix
/// output) to a monitor path with a ramped gain.
unsafe fn add_direct_in(topo: &Topology, bus: &mut [Vec<f32>; 2], index: i32, last: f32, target: f32, n: usize) {
    if last == 0.0 && target == 0.0 {
        return;
    }
    let Some(s) = direct_source(index).and_then(|(kind, i)| topo.by_number(kind, i as i32 + 1)) else { return };
    let d = s.dsp();
    if d.idle() {
        return;
    }
    let [l, r] = bus;
    add_with_gain_ramp(&mut l[..n], d.out(0, n), last, target);
    add_with_gain_ramp(&mut r[..n], d.out(1, n), last, target);
}

/// Talkback into a destination's sum (`talk.rs`): dims what the destination
/// summed (BUS DIM) and adds the talkback channel's PFL signal, both ramped.
///
/// # Safety
/// The destination's task only, after its sum and before it processes.
unsafe fn add_talk(dst: &Strip, g: TalkGains, ramp: Option<&mut TalkGains>, talk: &TalkBlock, n: usize) {
    let Some(ramp) = ramp else { return };
    if g == TalkGains::NONE && *ramp == TalkGains::NONE {
        return;
    }
    let d = dst.dsp_mut();
    if g.dim != 1.0 || ramp.dim != 1.0 {
        for ch in 0..2 {
            apply_gain_ramp(d.work_mut(ch, n), ramp.dim, g.dim);
        }
    }
    match talk.strip() {
        Some(src) if !src.dsp().idle() => {
            let s = src.dsp();
            add_with_gain_ramp(d.work_mut(0, n), s.pfl(0, n), ramp.talk, g.talk);
            add_with_gain_ramp(d.work_mut(1, n), s.pfl(1, n), ramp.talk, g.talk);
        }
        _ => {}
    }
    *ramp = g;
}

/// Applies a 2x2 matrix to the monitor pair, crossfading from `last` to `target`.
fn apply_monitor_matrix(l: &mut [f32], r: &mut [f32], last: [f32; 4], target: [f32; 4]) {
    if last == MONITOR_STEREO && target == MONITOR_STEREO {
        return;
    }
    let step = 1.0 / l.len().max(1) as f32;
    let mut t = 0.0;
    for (x, y) in l.iter_mut().zip(r.iter_mut()) {
        t += step;
        let m: [f32; 4] = std::array::from_fn(|i| last[i] + (target[i] - last[i]) * t);
        let (a, b) = (*x, *y);
        *x = m[0] * a + m[1] * b;
        *y = m[2] * a + m[3] * b;
    }
}

/// Adds a stereo pair into `dst`'s work buffers with ramped gains.
#[inline]
unsafe fn add_pair(dst: &Strip, n: usize, l: &[f32], r: &[f32], last: &mut [f32; 2], target: [f32; 2]) {
    let d = dst.dsp_mut();
    add_with_gain_ramp(d.work_mut(0, n), l, last[0], target[0]);
    add_with_gain_ramp(d.work_mut(1, n), r, last[1], target[1]);
    *last = target;
}

/// Pan gains of a strip for signals taken before its own pan (pre-fader sends).
#[inline]
unsafe fn pre_gains(src: &Strip, pan: Option<f32>) -> [f32; 2] {
    let d = src.dsp();
    match pan {
        None => d.pan_gains(),
        Some(p) => {
            let (l, r) = src.spec().pan.gains(d.mono_source(), p);
            [l, r]
        }
    }
}

/// A send taking a user tap of its source (`taps.rs`): its settings.
struct UserTapSend {
    id: usize,
    on: bool,
    level_db: f32,
    /// What a Source Selector leaves of it (1: none holds it).
    scale: f32,
    ignore_mute: bool,
    /// The send's own pan (None: the source's).
    pan: Option<f32>,
}

/// Adds source `s`'s user tap to `dst` as a pre-fader send would (cut by
/// the source's mute unless the send ignores it); silence when the tap
/// wasn't written this block. Not delay compensated.
///
/// # Safety
/// The task summing `dst`, which owns the send's ramp `last`.
unsafe fn add_user_tap(dst: &Strip, s: &Strip, send: &UserTapSend, block: u64, last: &mut [f32; 2], n: usize) {
    let level = if send.on && !tap_send_muted(s, send.ignore_mute) { db_to_gain(send.level_db) * send.scale } else { 0.0 };
    if level == 0.0 && *last == [0.0, 0.0] {
        return;
    }
    let (Some(l), Some(r)) = (s.taps.read(send.id, block, 0, n), s.taps.read(send.id, block, 1, n)) else {
        *last = [0.0, 0.0];
        return;
    };
    let g = pre_gains(s, send.pan);
    add_pair(dst, n, l, r, last, [level * g[0], level * g[1]]);
}

/// A TAP-mode (pre-fader) send is cut while its source is muted, unless it is
/// an input or aux channel's send set to IGNORE CHANNEL MUTE.
#[inline]
unsafe fn tap_send_muted(src: &Strip, ignore_mute: bool) -> bool {
    src.dsp().muted() && !(ignore_mute && src.kind().is_input())
}

/// A source's signal at `tap` (`pdc_tap`) as a destination aligned to
/// `target` sums it: delayed by the difference when the plan says so.
#[inline]
unsafe fn tap_at(src: &Strip, tap: usize, target: u32, ch: usize, n: usize) -> &[f32] {
    if let Some(delayed) = src.pdc.delayed(tap, target, ch, n) {
        return delayed;
    }
    let d = src.dsp();
    match tap {
        pdc_tap::TAP => d.tap_signal(ch, n),
        pdc_tap::PFL => d.pfl(ch, n),
        pdc_tap::POST => d.post(ch, n),
        _ => d.out(ch, n),
    }
}

/// A send's pan after a stereo mix's Follow Main Pan (a dLive's): `own` unless the mix
/// follows the channels' Main pans, as they are or mirrored.
fn follow_main_pan(mix: &Strip, s: &Strip, own: Option<f32>) -> Option<f32> {
    let d = &mix.dlive;
    if !s.kind().is_input() || !d.follow_main_pan.load(Relaxed) || !mix.params.stereo.load(Relaxed) {
        return own;
    }
    d.follow_invert.load(Relaxed).then(|| -s.params.pan.load() / 100.0)
}

/// Where a matrix send reads its source (`pdc_tap`) and the pan it applies
/// there (`None`: the strip's own). Input and aux channels: PAN LINK follows
/// the channel's pan (POST: its panned output, with the width), LINK
/// REVERSED mirrors it, else the send pans itself. Buses and mains send
/// their output (or tap) as panned, balanced by the send's pan.
fn matrix_send_route(s: &Strip, mtx: &Strip, send: &MatrixSendParams) -> (usize, Option<f32>) {
    let pre = send.pre.load(Relaxed);
    if !s.kind().is_input() {
        return (if pre { pdc_tap::TAP } else { pdc_tap::OUT }, None);
    }
    let own = match (send.pan_link.load(Relaxed), send.pan_reversed.load(Relaxed)) {
        (true, false) => None,
        (true, true) => Some(-s.params.pan.load() / 100.0),
        (false, _) => Some(send.pan.load() / 100.0),
    };
    let pan = follow_main_pan(mtx, s, own);
    let tap = match (pre, pan) {
        (true, _) => pdc_tap::TAP,
        (false, None) => pdc_tap::OUT,
        (false, Some(_)) => pdc_tap::POST,
    };
    (tap, pan)
}

/// Bus `from` sends to bus `to` (enabled; the level may be down).
fn bus_send_enabled(from: &Strip, to: &Strip) -> bool {
    let (a, b) = (from.id().max(0) as usize, to.id().max(0) as usize);
    bus_feeds_bus(a, b) && from.sends.find(b).is_some_and(|s| s.enabled.load(Relaxed))
}

/// The (source, tap) pairs summing strip `dst` adds up in the same block, as
/// its stage reads them: the enabled sends, but not a bus send that closes a
/// loop (`schedule`, over `buses` by slot), which arrives a block late.
fn pdc_feeds<'a>(
    st: &'a Structure,
    buses: &'a [Arc<Strip>],
    schedule: &BusSchedule,
    dst: &Strip,
) -> Vec<(&'a Arc<Strip>, usize)> {
    let inputs = st.channels.iter().chain(&st.auxes);
    let index = dst.id().max(0) as usize;
    let tap_or_out = |pre: bool| if pre { pdc_tap::TAP } else { pdc_tap::OUT };
    let mut feeds = Vec::new();
    match dst.kind() {
        StripKind::Bus => {
            let me = buses.iter().position(|b| std::ptr::eq(b.as_ref(), dst));
            let feeding = buses.iter().enumerate().filter(|(a, b)| {
                me.is_some_and(|me| *a != me && !schedule.is_late(*a, me)) && bus_feeds_bus(b.id() as usize, index)
            });
            for s in inputs.chain(feeding.map(|(_, b)| b)) {
                let Some(send) = s.sends.find(index).filter(|send| send.enabled.load(Relaxed)) else { continue };
                let tap = match send.mode.load(Relaxed) {
                    send_mode::PRE => pdc_tap::TAP,
                    send_mode::GROUP => pdc_tap::OUT,
                    _ => pdc_tap::POST,
                };
                feeds.push((s, tap));
            }
        }
        StripKind::Main => {
            for s in inputs.chain(&st.buses) {
                let mode = dst.spec().dlive_layout.map_or(MainMode::Lr, |l| l.main);
                let control = if mode.shares_lr_send(index) { 0 } else { index };
                let Some(send) = s.main_send(control) else { continue };
                if send.on.load(Relaxed) && mode != MainMode::None {
                    let pre = send.pre.load(Relaxed);
                    let tap = if !pre && main_panned(mode, index) { pdc_tap::POST } else { tap_or_out(pre) };
                    feeds.push((s, tap));
                }
            }
        }
        StripKind::Matrix => {
            for s in inputs.chain(&st.buses).chain(&st.mains) {
                let Some(send) = s.matrix_send(index) else { continue };
                if send.on.load(Relaxed) {
                    feeds.push((s, matrix_send_route(s, dst, send).0));
                }
            }
            // (DIRECT IN comes from the monitor paths, a block late.)
        }
        StripKind::Channel | StripKind::Aux => {}
    }
    feeds
}

/// Bus `index` (in the bus list) sums the active inputs' sends and the
/// other buses' (manual 5.10 BUS → BUS). A bus send from an earlier wave
/// takes this block's signal; one that closes a loop (`schedule`) takes its
/// source's previous block from the return ring at the clock's `pos`.
///
/// # Safety
/// Bus task only, after the stages and waves feeding it.
unsafe fn sum_bus(bus: &Strip, index: usize, topo: &Topology, schedule: &BusSchedule, pos: usize, block: u64, n: usize) {
    bus.begin_summed_block(n);
    let slot = bus.id() as usize;
    let target = bus.pdc.target();
    for s in topo.active() {
        add_bus_send(bus, s, slot, target, None, block, n);
    }
    for (from, s) in topo.bus_list.iter().enumerate() {
        if from == index || !bus_feeds_bus(s.id() as usize, slot) {
            continue;
        }
        add_bus_send(bus, s, slot, target, schedule.is_late(from, index).then_some(pos), block, n);
    }
}

/// The tap Group `s` leaves from for Group `bus` (a dLive's Group to Group, V2.11).
/// None: not two Groups, or another console. Its added delay of a few samples isn't
/// modelled.
fn group_source_tap(bus: &Strip, s: &Strip) -> Option<usize> {
    use crate::dlive::group_source;
    let groups = 0..s.spec().dlive_layout?.aux_at();
    let is_group = |x: &Strip| x.kind() == StripKind::Bus && groups.contains(&(x.id().max(0) as usize));
    if !is_group(s) || !is_group(bus) {
        return None;
    }
    Some(if s.dlive.group_source.load(Relaxed) == group_source::POST_COMP { 3 } else { crate::strip::tap::PFL })
}

/// The tap of channel `s` that `bus`'s Mix Source takes its pre-fade sends from (None:
/// their usual tap, Post Delay on a dLive, and on every other console).
fn mix_source_tap(bus: &Strip, s: &Strip, mode: i32) -> Option<usize> {
    use crate::params::mix_source;
    if mode != send_mode::PRE || s.kind() != StripKind::Channel {
        return None;
    }
    match bus.mix_input.mix_source.load(Relaxed) {
        mix_source::POST_PREAMP => Some(crate::strip::tap::IN),
        mix_source::POST_INSERT_A => Some(2),
        mix_source::POST_PEQ => Some(if s.eq_first() { 3 } else { 4 }),
        _ => None,
    }
}

/// Adds strip `s`'s send to bus slot `slot` into `bus`. `late`: the send
/// closes a loop and reads `s`'s previous block at that return clock
/// position (its output, or its tap for PRE; the send's own pan balances it,
/// and it follows `s`'s own MUTE), never `s`'s buffers, which may be running.
unsafe fn add_bus_send(bus: &Strip, s: &Strip, slot: usize, target: u32, late: Option<usize>, block: u64, n: usize) {
    let Some(send) = s.sends.find(slot) else { return };
    let scale = selector_scale(s, StripKind::Bus, slot);
    let own_pan = match (send.pan_link.load(Relaxed), send.pan_reversed.load(Relaxed)) {
        (true, false) => None,
        (true, true) => Some(-s.params.pan.load() / 100.0), // LINK REVERSED
        (false, _) => Some(send.pan.load() / 100.0),
    };
    let pan = follow_main_pan(bus, s, own_pan);
    let tap = send_tap(send.tap.load(Relaxed))
        .or_else(|| mix_source_tap(bus, s, send.mode.load(Relaxed)))
        .or_else(|| group_source_tap(bus, s));
    if let Some(id) = tap {
        let last = &mut *send.last_gain.get();
        if late.is_some() {
            *last = [0.0, 0.0]; // a send closing a loop can't take a tap of this block
            return;
        }
        let tap = UserTapSend {
            id,
            on: send.enabled.load(Relaxed),
            level_db: send.level_db.load(),
            scale,
            ignore_mute: send.ignore_mute.load(Relaxed),
            pan,
        };
        add_user_tap(bus, s, &tap, block, last, n);
        return;
    }
    let mode = send.mode.load(Relaxed);
    let muted = || match late {
        Some(_) => s.params.mute.load(Relaxed),
        None => tap_send_muted(s, send.ignore_mute.load(Relaxed)),
    };
    let level = if !send.enabled.load(Relaxed) {
        0.0
    } else if mode == send_mode::GROUP {
        1.0 // GROUP has no send level
    } else if mode == send_mode::PRE && muted() {
        0.0
    } else {
        db_to_gain(send.level_db.load())
    } * scale;
    let last = &mut *send.last_gain.get();
    if level == 0.0 && *last == [0.0, 0.0] {
        return;
    }
    if let Some(pos) = late {
        let tap = if mode == send_mode::PRE { return_tap::TAP } else { return_tap::OUT };
        let (Some(l), Some(r)) = (s.read_return(tap, 0, pos, n), s.read_return(tap, 1, pos, n)) else {
            *last = [0.0, 0.0];
            return;
        };
        let (gl, gr) = match pan {
            Some(p) if mode != send_mode::GROUP => stereo_balance(p),
            _ => (1.0, 1.0),
        };
        add_pair(bus, n, l, r, last, [level * gl, level * gr]);
        return;
    }
    if s.dsp().idle() {
        *last = [0.0, 0.0]; // silent strips have nothing to add
        return;
    }
    match mode {
        send_mode::GROUP => {
            let (l, r) = (tap_at(s, pdc_tap::OUT, target, 0, n), tap_at(s, pdc_tap::OUT, target, 1, n));
            add_pair(bus, n, l, r, last, [level, level])
        }
        mode => {
            let g = pre_gains(s, pan);
            let tap = if mode == send_mode::PRE { pdc_tap::TAP } else { pdc_tap::POST };
            let (l, r) = (tap_at(s, tap, target, 0, n), tap_at(s, tap, target, 1, n));
            add_pair(bus, n, l, r, last, [level * g[0], level * g[1]]);
        }
    }
}

/// Main strip `index` in `mode` takes the channels before their pan, by [`main_gains`].
fn main_panned(mode: MainMode, index: usize) -> bool {
    match mode {
        MainMode::Lcr | MainMode::LcrPlus | MainMode::Surround => true,
        MainMode::LrM => index == 1,
        _ => false,
    }
}

/// What `s` gives main strip `index` in a mode where the pan decides (`main_panned`),
/// left and right, taken before its pan. Stereo sources feed LR alone (and in 5.1 the
/// rear pair); the curves are in `dlive.rs`.
///
/// # Safety
/// A task after `s`'s stage.
unsafe fn main_gains(s: &Strip, mode: MainMode, index: usize) -> [f32; 2] {
    use crate::dlive::{lcr_gains, lcr_plus_gains, pan_mode};
    let stereo = s.params.stereo.load(Relaxed);
    if mode == MainMode::LrM {
        return [1.0, 1.0]; // M: unpanned, the two sides of a stereo channel folded by the main
    }
    let d = &s.dlive;
    let lr = s.dsp().pan_gains();
    let pan = s.params.pan.load() / 100.0;
    let divergence = d.divergence.load() / 100.0;
    let (l, c, r) = if stereo {
        (lr[0], 0.0, lr[1])
    } else {
        match if mode == MainMode::Surround { d.pan_mode.load(Relaxed) } else { -1 } {
            pan_mode::LR => (lr[0], 0.0, lr[1]),
            pan_mode::LCR_PLUS => lcr_plus_gains(lr, divergence),
            -1 if mode == MainMode::LcrPlus => lcr_plus_gains(lr, divergence),
            _ => lcr_gains(pan),
        }
    };
    if mode != MainMode::Surround {
        return if index == 0 { [l, r] } else { [c, c] };
    }
    let (rear, front) = (d.surround_rear.load() / 100.0 * std::f32::consts::FRAC_PI_2).sin_cos();
    let bed = db_to_gain(d.non_lfe_db.load());
    match index {
        0 => [l * front * bed, r * front * bed],
        1 => [c * front * bed, c * front * bed],
        2 => [db_to_gain(d.lfe_db.load()); 2],
        _ => [lr[0] * rear * bed, lr[1] * rear * bed],
    }
}

/// A Source Selector's say on `s`'s send to a mix (1: none holds it).
#[inline]
fn selector_scale(s: &Strip, kind: StripKind, index: usize) -> f32 {
    crate::dlive::selector_destination(kind, index).map_or(1.0, |d| s.selector_gain[d].load())
}

/// A dLive channel's Direct Out this block: the Global Direct Output Source (the tap of
/// that point), its own level and what the options make it follow. Into `scratch`, the
/// ramp in `last`.
///
/// # Safety
/// Audio thread, after every stage.
#[allow(clippy::too_many_arguments)]
unsafe fn dlive_direct_out<'a>(
    s: &Strip,
    side: usize,
    c: &crate::dlive::DLiveControls,
    dcas: &[DcaParams],
    last: &mut f32,
    scratch: &'a mut [f32],
    block: u64,
    n: usize,
) -> &'a [f32] {
    use crate::dlive::{direct_source as src, TAP_POST_GATE, TAP_POST_INSERT_B};
    use crate::strip::tap;
    let eq_first = s.eq_first();
    let id = match c.direct_source.load(Relaxed) {
        src::POST_PREAMP => tap::IN,
        src::POST_LPF => tap::FILT,
        src::POST_GATE => TAP_POST_GATE,
        src::POST_INSERT_A => 2,
        src::POST_PEQ => {
            if eq_first {
                3
            } else {
                4
            }
        }
        src::POST_COMP => {
            if eq_first {
                4
            } else {
                3
            }
        }
        src::POST_INSERT_B => TAP_POST_INSERT_B,
        _ => tap::PFL,
    };
    let all = c.direct_follow_all.load(Relaxed);
    let muted = if all { s.dsp().muted() } else { c.direct_follow_mute.load(Relaxed) && s.params.mute.load(Relaxed) };
    let fader_db = if all {
        s.params.fader_db.load() + s.dca_offset_db(dcas)
    } else if c.direct_follow_fader.load(Relaxed) {
        s.params.fader_db.load()
    } else {
        0.0
    };
    let gain = if muted { 0.0 } else { db_to_gain(s.dlive.direct_db.load() + fader_db) };
    let out = &mut scratch[..n];
    out.fill(0.0);
    if let Some(signal) = s.taps.read(id, block, side, n) {
        add_with_gain_ramp(out, signal, *last, gain);
        *last = gain;
    } else {
        *last = 0.0;
    }
    out
}

/// # Safety
/// Main task `index` only, after the bus stages.
unsafe fn sum_main<'a>(main: &Strip, index: usize, sources: impl Iterator<Item = &'a Arc<Strip>>, block: u64, n: usize) {
    main.begin_summed_block(n);
    let mode = main.spec().dlive_layout.map_or(MainMode::Lr, |l| l.main);
    if mode == MainMode::None {
        return; // the console's one main strip, unheard
    }
    let target = main.pdc.target();
    let control = if mode.shares_lr_send(index) { 0 } else { index };
    for s in sources {
        let (Some(send), Some(own)) = (s.main_send(control), s.main_send(index)) else { continue };
        let is_pre = send.pre.load(Relaxed);
        let mode_id = if is_pre { send_mode::PRE } else { send_mode::POST };
        if let Some(id) = send_tap(send.tap.load(Relaxed)).or_else(|| mix_source_tap(main, s, mode_id)) {
            let tap = UserTapSend {
                id,
                on: send.on.load(Relaxed),
                level_db: send.level_db.load(),
                scale: 1.0,
                ignore_mute: false,
                pan: None,
            };
            add_user_tap(main, s, &tap, block, own.last(), n);
            continue;
        }
        let pre = is_pre;
        // Pre-fader main sends still follow the strip's mute.
        let on = send.on.load(Relaxed) && !(pre && s.dsp().muted());
        let level = if on { db_to_gain(send.level_db.load()) } else { 0.0 };
        let last = own.last();
        let src = s.dsp();
        if (level == 0.0 && *last == [0.0, 0.0]) || src.idle() {
            *last = [0.0, 0.0];
            continue;
        }
        if main_panned(mode, index) {
            let g = main_gains(s, mode, index);
            let tap = if pre { pdc_tap::TAP } else { pdc_tap::POST };
            let (l, r) = (tap_at(s, tap, target, 0, n), tap_at(s, tap, target, 1, n));
            add_pair(main, n, l, r, last, [level * g[0], level * g[1]]);
            continue;
        }
        if pre {
            let g = pre_gains(s, None);
            let (l, r) = (tap_at(s, pdc_tap::TAP, target, 0, n), tap_at(s, pdc_tap::TAP, target, 1, n));
            add_pair(main, n, l, r, last, [level * g[0], level * g[1]]);
        } else {
            let (l, r) = (tap_at(s, pdc_tap::OUT, target, 0, n), tap_at(s, pdc_tap::OUT, target, 1, n));
            add_pair(main, n, l, r, last, [level, level]);
        }
    }
}

/// # Safety
/// Matrix task `index` only, after the main stage.
unsafe fn sum_matrix(mtx: &Strip, index: usize, topo: &Topology, monitor: &[*const f32; 6], block: u64, n: usize) {
    mtx.begin_summed_block(n);
    let target = mtx.pdc.target();
    for s in topo.active().iter().chain(topo.buses()).chain(&topo.mains) {
        let Some(send) = s.matrix_send(index) else { continue };
        let scale = selector_scale(s, StripKind::Matrix, index);
        let mode_id = if send.pre.load(Relaxed) { send_mode::PRE } else { send_mode::POST };
        if let Some(id) = send_tap(send.tap.load(Relaxed)).or_else(|| mix_source_tap(mtx, s, mode_id)) {
            let pan = (!send.pan_link.load(Relaxed)).then(|| send.pan.load() / 100.0);
            let tap = UserTapSend {
                id,
                on: send.on.load(Relaxed),
                level_db: send.level_db.load(),
                scale,
                ignore_mute: send.ignore_mute.load(Relaxed),
                pan,
            };
            add_user_tap(mtx, s, &tap, block, send.last(), n);
            continue;
        }
        let on = send.on.load(Relaxed) && !(send.pre.load(Relaxed) && tap_send_muted(s, send.ignore_mute.load(Relaxed)));
        let level = if on { db_to_gain(send.level_db.load()) * scale } else { 0.0 };
        let last = send.last();
        let src = s.dsp();
        if (level == 0.0 && *last == [0.0, 0.0]) || src.idle() {
            *last = [0.0, 0.0];
            continue;
        }
        let (tap, pan) = matrix_send_route(s, mtx, send);
        if s.kind().is_input() {
            // The pan gains, unless the channel's panned output is taken as it is.
            let g = if tap == pdc_tap::OUT { [1.0, 1.0] } else { pre_gains(s, pan) };
            let (l, r) = (tap_at(s, tap, target, 0, n), tap_at(s, tap, target, 1, n));
            add_pair(mtx, n, l, r, last, [level * g[0], level * g[1]]);
            continue;
        }
        let (bl, br) = stereo_balance(send.pan.load() / 100.0);
        if tap == pdc_tap::TAP {
            let g = pre_gains(s, None);
            let (l, r) = (tap_at(s, pdc_tap::TAP, target, 0, n), tap_at(s, pdc_tap::TAP, target, 1, n));
            add_pair(mtx, n, l, r, last, [level * g[0] * bl, level * g[1] * br]);
        } else {
            let (l, r) = (tap_at(s, pdc_tap::OUT, target, 0, n), tap_at(s, pdc_tap::OUT, target, 1, n));
            add_pair(mtx, n, l, r, last, [level * bl, level * br]);
        }
    }

    // DIRECT IN (manual 6.4): the PHONES, the SPEAKERS or the solo bus, a
    // block late (the monitor runs after the matrices); AES is silent here.
    let p = &mtx.params;
    let pair = matrix_direct_pair(p.direct_source.load(Relaxed));
    let on = p.direct_on.load(Relaxed) && pair.is_some();
    let gain =
        if on { db_to_gain(p.direct_level_db.load()) * if p.direct_invert.load(Relaxed) { -1.0 } else { 1.0 } } else { 0.0 };
    let prev = mtx.dsp().last_direct();
    let ptrs = pair.map(|pair| (monitor[2 * pair], monitor[2 * pair + 1]));
    match ptrs {
        Some((l, r)) if !l.is_null() && !r.is_null() && (gain != 0.0 || prev != 0.0) => {
            let (l, r) = (std::slice::from_raw_parts(l, n), std::slice::from_raw_parts(r, n));
            let mut ramp = [prev, prev];
            add_pair(mtx, n, l, r, &mut ramp, [gain, gain]);
        }
        _ => {}
    }
    mtx.dsp_mut().set_last_direct(gain);
}

/// Copies user signal `u`'s side of strip `s` into `buf` (manual 2.6): PRE
/// takes the tap point, POST an input or aux channel after its processing
/// (before the pan) and a bus, main or matrix its output; L+R mixes a stereo
/// strip's sides at half each; a mono channel's one side whatever the
/// setting.
///
/// # Safety
/// Audio thread, after every stage.
/// A direct out's signal this block (the strip ran): its PFL copy (pre fader)
/// or its post-fader copy (before pan); a mono channel's on either side.
/// None when the strip doesn't exist or is idle.
unsafe fn direct_signal(topo: &Topology, direct: DirectSource, n: usize) -> Option<&[f32]> {
    let d = topo.by_number(direct.kind, direct.number)?.dsp();
    if d.idle() {
        return None;
    }
    Some(if direct.post { d.post(direct.side, n) } else { d.pfl(direct.side, n) })
}

unsafe fn copy_user_signal(s: &Strip, u: &UserSignalParams, buf: &mut [f32], n: usize) {
    let d = s.dsp();
    if d.idle() {
        buf.fill(0.0);
        return;
    }
    let post = u.post.load(Relaxed);
    let side = |ch: usize| match (post, s.kind().is_input()) {
        (false, _) => d.tap_signal(ch, n),
        (true, true) => d.post(ch, n),
        (true, false) => d.out(ch, n),
    };
    let stereo = !s.kind().is_input() || d.channels() > 1;
    match u.side.load(Relaxed) {
        _ if !stereo => buf.copy_from_slice(side(0)),
        user_side::LEFT => buf.copy_from_slice(side(0)),
        user_side::RIGHT => buf.copy_from_slice(side(1)),
        _ => {
            for ((b, l), r) in buf.iter_mut().zip(side(0)).zip(side(1)) {
                *b = 0.5 * (l + r);
            }
        }
    }
}

/// The matrices' DIRECT IN signals for the parallel tasks (the monitor
/// return ring's channels, null where not written).
struct MonitorReturns([*const f32; 6]);
// SAFETY: the ring's read window, which nothing writes during the matrix stage.
unsafe impl Sync for MonitorReturns {}

impl MonitorReturns {
    fn get(&self) -> &[*const f32; 6] {
        &self.0
    }
}

impl Drop for Engine {
    fn drop(&mut self) {
        self.pool.shutdown();
        let topo = self.topology.swap(std::ptr::null_mut(), AcqRel);
        if !topo.is_null() {
            // SAFETY: created by Box::into_raw in publish().
            drop(unsafe { Box::from_raw(topo) });
        }
        self.garbage.collect_all_now();
    }
}
