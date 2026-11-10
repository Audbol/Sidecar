//! What an FX engine has besides its device and the settings of `FxParams`: a
//! dLive's RackUltra (UFX) channel strips and its Dyn8 engines, and for every FX
//! slot of every console a key source, the console-wide Global Key and MIDI notes.
//!
//! - **UFX**: on a dLive, FX 17-24 (`UFX_FIRST`..) are the RackUltra card's 8
//!   engines. Each runs its device inside a channel strip
//!   (`livedaw_dsp::dlive_fx::ultra::strip::UltraStrip`: PEQ, dynamics, the FX,
//!   dynamics, PEQ, Width), whose values are [`UfxEngine::values`]. Like the
//!   RackExtra's 16 they are inserted or fed from a mix (`FxParams::source_left`)
//!   and come back on the FX return sources (`sources::NUM_SEND_FX` covers all 24).
//! - **Dyn8**: a dLive's 64 Dyn8 engines are not FX slots. `MAX_FX` (64) stays as it
//!   is: every scope and safe in a show is a 64-bit mask of FX slots, and the FX
//!   slots' settings sit in the middle of the controls the host shares
//!   (`EngineControls::fx`), so 88 slots would have changed both. They are an array
//!   of their own ([`UfxState::dyn8_params`] and their racks) outside the shared
//!   controls, each an `FxRack` with the Dyn8 model. An insert names Dyn8 `n`
//!   (from 0) as slot [`DYN8_FX_BASE`]` + n`; on any other console such an insert
//!   passes its signal through. `Engine::fx` and `Engine::fx_params` take both ranges.
//! - **Key source** ([`FxKey`]): the strip tap a slot's device takes its external key
//!   from (`FxAlgo::process_keyed`: Dyn8's External key, the Dual AutoKey
//!   Harmoniser's side chain, a UFX strip's EXTERNAL key), as a GATE's KEY SOURCE and
//!   KEY TAP (`keytap.rs`). An insert reads the tap of this block when its source ran
//!   before it, else the previous block's; a send effect, whose own input is a block
//!   late, the previous block's.
//! - **Global Key** ([`GlobalKey`]) and **MIDI notes** ([`MidiNotes`]): handed to
//!   every FX slot's device by the audio thread at the start of a block
//!   (`FxAlgo::global_key`, `FxAlgo::midi_note`); the devices that follow them
//!   listen, the others ignore them. A device made later gets the key with its first
//!   block, and the notes from then on (a note held across the change isn't replayed).
//!
//! Sidecar's reading: which MIDI channel a UFX engine listens on isn't modelled (every
//! engine hears every note given to `MidiNotes::push`), and one key source serves a
//! UFX engine's strip and its device both (the guide: one external source per FX).

use crate::atomic::AtomicF32;
use crate::fx::FxRack;
use crate::garbage::Garbage;
use crate::keytap::{key_buffer, KeySource};
use crate::params::{FxParams, FX_NONE, MAX_FX};
use livedaw_dsp::dlive_fx::ultra::strip::{UltraStrip, STRIP, STRIP_LEN};
use livedaw_dsp::dlive_fx::{DYN8_SLOTS, ULTRA_SLOTS};
use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicPtr, AtomicU32, AtomicUsize, Ordering::*};

/// A dLive's RackUltra engines, and the FX slot of the first (FX 17).
pub const NUM_UFX: usize = 8;
pub const UFX_FIRST: usize = ULTRA_SLOTS.start;
/// A dLive's Dyn8 engines, and the slot number an insert names the first by.
pub const NUM_DYN8: usize = 64;
pub const DYN8_FX_BASE: usize = MAX_FX;
/// Every engine an insert can name: the FX slots, then the Dyn8 engines.
pub const NUM_FX_ENGINES: usize = MAX_FX + NUM_DYN8;
/// The values of a UFX engine's strip (`ultra::strip::STRIP`'s parameters).
pub const UFX_STRIP_PARAMS: usize = STRIP_LEN;
/// A UFX strip's gain-reduction meters: the pre and the post dynamics.
pub const UFX_STRIP_METERS: usize = 2;
/// The token Dyn8 has in a dLive's FX table.
pub const DYN8_TOKEN: &str = "DLVDYN8";

const _: () = assert!(DYN8_SLOTS.start == DYN8_FX_BASE && DYN8_SLOTS.end == NUM_FX_ENGINES);
const _: () = assert!(ULTRA_SLOTS.end - ULTRA_SLOTS.start == NUM_UFX && ULTRA_SLOTS.end <= crate::sources::NUM_SEND_FX);

/// The Dyn8 engine (from 0) an insert's slot number names, if any.
pub fn dyn8_of(slot: i32) -> Option<usize> {
    usize::try_from(slot).ok()?.checked_sub(DYN8_FX_BASE).filter(|&n| n < NUM_DYN8)
}

/// The UFX engine (from 0) FX slot `slot` is on a dLive, if any.
pub fn ufx_of(slot: usize) -> Option<usize> {
    slot.checked_sub(UFX_FIRST).filter(|&n| n < NUM_UFX)
}

/// An FX engine's key source: a strip (`KeySource::code`; -1: none, the device keys
/// itself) and the tap it is taken at (`sc_tap`, as a dynamics slot's: `TAP_POINTS`
/// on an input channel, `OUTPUT_KEY_TAPS` on an aux, bus, main or matrix).
#[derive(Debug)]
pub struct FxKey {
    pub source: AtomicI32,
    pub tap: AtomicI32,
}

impl Default for FxKey {
    fn default() -> Self {
        Self { source: AtomicI32::new(-1), tap: AtomicI32::new(0) }
    }
}

impl FxKey {
    pub fn set(&self, source: KeySource, tap: i32) {
        self.source.store(source.code(), Relaxed);
        self.tap.store(tap.max(0), Relaxed);
    }

    pub fn reset(&self) {
        self.set(KeySource::SelfKey, 0);
    }

    /// The source and the tap buffer the key takes (`keytap::key_buffer`).
    pub fn get(&self) -> (KeySource, usize) {
        let source = KeySource::from_code(self.source.load(Relaxed));
        (source, key_buffer(source, self.tap.load(Relaxed)))
    }

    /// The input channel the key comes from, for stage 1's order (-1: none or
    /// another kind of strip).
    pub(crate) fn channel(&self) -> i32 {
        match KeySource::from_code(self.source.load(Relaxed)) {
            KeySource::Channel(n) => n as i32,
            _ => -1,
        }
    }
}

/// Wrapper so a replaced strip can go to the garbage collector.
struct RetiredStrip(#[allow(dead_code)] Box<UltraStrip>);

/// One RackUltra engine's channel strip: its values and, once the engine is
/// prepared for a device, its DSP (run by the task that runs the engine's FX slot).
pub struct UfxEngine {
    /// The strip's values, in `ultra::strip::STRIP`'s order.
    pub values: [AtomicF32; UFX_STRIP_PARAMS],
    strip: AtomicPtr<UltraStrip>,
    reductions: [AtomicF32; UFX_STRIP_METERS],
}

impl Default for UfxEngine {
    fn default() -> Self {
        Self {
            values: std::array::from_fn(|i| AtomicF32::new(STRIP.params[i].default)),
            strip: AtomicPtr::new(std::ptr::null_mut()),
            reductions: Default::default(),
        }
    }
}

impl UfxEngine {
    /// Every value back on its default (both PEQs and dynamics out, Width 100 %).
    pub fn reset(&self) {
        for (v, p) in self.values.iter().zip(STRIP.params) {
            v.store(p.default);
        }
    }

    /// Sets value `i` (kept within its parameter's range). False for no such value.
    pub fn set_value(&self, i: usize, v: f32) -> bool {
        match (self.values.get(i), STRIP.params.get(i)) {
            (Some(a), Some(p)) => {
                a.store(p.sanitise(v));
                true
            }
            _ => false,
        }
    }

    /// A value by its id in `STRIP` ("a_in", "b_th", "width").
    pub fn param(&self, id: &str) -> Option<&AtomicF32> {
        self.values.get(STRIP.param_index(id)?)
    }

    /// The gain reduction of the pre (0) or post (1) dynamics, dB (<= 0).
    pub fn reduction(&self, i: usize) -> f32 {
        self.reductions.get(i).map_or(0.0, AtomicF32::load)
    }

    /// Message thread: installs the strip's DSP (or removes it, with None).
    pub(crate) fn set_strip(&self, strip: Option<Box<UltraStrip>>, garbage: &Garbage) {
        let old = self.strip.swap(strip.map_or(std::ptr::null_mut(), Box::into_raw), AcqRel);
        if !old.is_null() {
            // SAFETY: created by Box::into_raw above; now unreachable for new blocks.
            garbage.retire(RetiredStrip(unsafe { Box::from_raw(old) }));
        }
    }

    /// The strip's DSP, for the task running the engine's FX slot.
    ///
    /// # Safety
    /// Only the task that claimed the slot (`FxRack::process`) may call this.
    #[allow(clippy::mut_from_ref)]
    pub(crate) unsafe fn strip_mut(&self) -> Option<&mut UltraStrip> {
        self.strip.load(Acquire).as_mut()
    }

    pub(crate) fn snapshot(&self, out: &mut [f32; UFX_STRIP_PARAMS]) {
        for (o, v) in out.iter_mut().zip(&self.values) {
            *o = v.load();
        }
    }

    pub(crate) fn store_reductions(&self, r: [f32; UFX_STRIP_METERS]) {
        for (a, v) in self.reductions.iter().zip(r) {
            a.store(if v.is_finite() { v } else { 0.0 });
        }
    }
}

impl Drop for UfxEngine {
    fn drop(&mut self) {
        let p = self.strip.swap(std::ptr::null_mut(), AcqRel);
        if !p.is_null() {
            // SAFETY: created by Box::into_raw in set_strip; the engine is being destroyed.
            drop(unsafe { Box::from_raw(p) });
        }
    }
}

/// The Global Key's scales, as `FxAlgo::global_key` numbers them.
pub const GLOBAL_KEY_SCALES: [&str; 3] = ["MAJOR", "MINOR", "CHROMATIC"];

/// The console-wide Global Key: the tuners' and harmonisers' key when theirs is
/// set to follow it. Key 0 (C) to 11 (B), a scale of [`GLOBAL_KEY_SCALES`].
#[derive(Debug, Default)]
pub struct GlobalKey {
    code: AtomicU32,
}

impl GlobalKey {
    pub fn set(&self, key: i32, scale: i32) {
        let (key, scale) = (key.clamp(0, 11) as u32, scale.clamp(0, GLOBAL_KEY_SCALES.len() as i32 - 1) as u32);
        self.code.store(key | scale << 8, Relaxed);
    }

    pub fn key(&self) -> i32 {
        (self.code() & 0xff) as i32
    }

    pub fn scale(&self) -> i32 {
        (self.code() >> 8) as i32
    }

    /// Key and scale in one word (what an `FxRack` remembers it gave its device).
    pub(crate) fn code(&self) -> u32 {
        self.code.load(Relaxed)
    }
}

/// Notes waiting for the audio thread.
const NOTE_QUEUE: usize = 256;
/// The most notes one block hands to the devices; the rest wait for the next.
pub(crate) const NOTES_PER_BLOCK: usize = 32;
const NOTE_SET: u32 = 1 << 31;

/// MIDI notes on their way to the FX devices: any thread pushes (the host's MIDI
/// input, a remote), the audio thread takes them at the start of a block. Lock-free;
/// when more than 256 wait, the oldest are lost.
pub struct MidiNotes {
    ring: [AtomicU32; NOTE_QUEUE],
    write: AtomicUsize,
    /// Audio thread only.
    read: UnsafeCell<usize>,
}

// SAFETY: `read` is the audio thread's alone; the rest is atomic.
unsafe impl Sync for MidiNotes {}

impl Default for MidiNotes {
    fn default() -> Self {
        Self { ring: std::array::from_fn(|_| AtomicU32::new(0)), write: AtomicUsize::new(0), read: UnsafeCell::new(0) }
    }
}

impl MidiNotes {
    /// Any thread: a note on (`velocity` 1-127) or off (`velocity` 0).
    pub fn push(&self, note: u8, velocity: u8) {
        let at = self.write.fetch_add(1, AcqRel);
        let event = NOTE_SET | u32::from(note & 0x7f) << 8 | u32::from(velocity & 0x7f);
        self.ring[at % NOTE_QUEUE].store(event, Release);
    }

    /// Audio thread: the notes that arrived, in order, into `out`; returns how many.
    ///
    /// # Safety
    /// Audio thread only (never concurrently with itself).
    pub(crate) unsafe fn take(&self, out: &mut [(u8, u8); NOTES_PER_BLOCK]) -> usize {
        let read = &mut *self.read.get();
        let write = self.write.load(Acquire);
        // Lapped by the writers: the oldest notes are gone.
        if write.wrapping_sub(*read) > NOTE_QUEUE {
            *read = write.wrapping_sub(NOTE_QUEUE);
        }
        let mut n = 0;
        while *read != write && n < out.len() {
            let event = self.ring[*read % NOTE_QUEUE].swap(0, AcqRel);
            if event == 0 {
                // Its writer has the place and hasn't stored yet: next block.
                break;
            }
            out[n] = (((event >> 8) & 0x7f) as u8, (event & 0x7f) as u8);
            n += 1;
            *read = read.wrapping_add(1);
        }
        n
    }
}

/// Everything of this module the engine holds (`Engine::ufx`).
pub struct UfxState {
    /// Each engine's key source: the FX slots (`MAX_FX`), then the Dyn8 engines.
    pub keys: Box<[FxKey]>,
    /// The RackUltra engines' strips (a dLive's FX 17-24).
    pub strips: Box<[UfxEngine]>,
    /// The Dyn8 engines and their settings (`values` in Dyn8's parameter order,
    /// `bypass`, `mix`; the model is Dyn8 on a dLive and none on any other console).
    pub(crate) dyn8: Box<[FxRack]>,
    pub dyn8_params: Box<[FxParams]>,
    pub global_key: GlobalKey,
    pub notes: MidiNotes,
    /// The console is a dLive: the UFX strips and the Dyn8 engines run.
    live: AtomicBool,
}

impl Default for UfxState {
    fn default() -> Self {
        Self {
            keys: (0..NUM_FX_ENGINES).map(|_| FxKey::default()).collect(),
            strips: (0..NUM_UFX).map(|_| UfxEngine::default()).collect(),
            dyn8: (0..NUM_DYN8).map(|_| FxRack::default()).collect(),
            dyn8_params: (0..NUM_DYN8).map(|_| FxParams::default()).collect(),
            global_key: GlobalKey::default(),
            notes: MidiNotes::default(),
            live: AtomicBool::new(false),
        }
    }
}

impl UfxState {
    /// The console has RackUltra and Dyn8 engines (it is a dLive).
    pub fn live(&self) -> bool {
        self.live.load(Relaxed)
    }

    /// The console stopped being a dLive (its counts grew past one's): an insert
    /// naming a Dyn8 engine passes its signal, FX 17-24 are plain slots.
    pub(crate) fn stop(&self) {
        self.live.store(false, Relaxed);
    }

    /// A fresh console: no key sources, the strips and the Dyn8 engines on their
    /// defaults, the Global Key C Major. `dyn8` is Dyn8's place in the console's FX
    /// table (a dLive's), None on a console without Dyn8 engines.
    pub(crate) fn reset(&self, table: &[livedaw_dsp::model::ModelSpec], dyn8: Option<usize>) {
        self.live.store(dyn8.is_some(), Relaxed);
        self.keys.iter().for_each(FxKey::reset);
        self.strips.iter().for_each(UfxEngine::reset);
        for p in self.dyn8_params.iter() {
            p.bypass.store(false, Relaxed);
            p.mix.store(100.0);
            match dyn8 {
                Some(model) => p.set_model_in(table, model, true),
                None => p.model.store(FX_NONE as i32, Relaxed),
            }
        }
        self.global_key.set(0, 0);
    }

    /// The strip FX slot `slot` runs its device in: a dLive's FX 17-24.
    pub fn strip_of(&self, slot: usize) -> Option<&UfxEngine> {
        self.strips.get(ufx_of(slot)?).filter(|_| self.live())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn notes_arrive_in_order_and_the_oldest_are_lost_when_too_many_wait() {
        let q = MidiNotes::default();
        let mut out = [(0, 0); NOTES_PER_BLOCK];
        assert_eq!(unsafe { q.take(&mut out) }, 0);
        q.push(60, 100);
        q.push(64, 90);
        q.push(60, 0);
        assert_eq!(unsafe { q.take(&mut out) }, 3);
        assert_eq!(&out[..3], &[(60, 100), (64, 90), (60, 0)]);
        assert_eq!(unsafe { q.take(&mut out) }, 0);
        // More than a block's worth: the rest the next block.
        for n in 0..40u8 {
            q.push(n, 1);
        }
        assert_eq!(unsafe { q.take(&mut out) }, NOTES_PER_BLOCK);
        assert_eq!(out[0], (0, 1));
        assert_eq!(unsafe { q.take(&mut out) }, 8);
        assert_eq!(out[7], (39, 1));
        // Far too many: the newest 256 stay.
        for n in 0..(NOTE_QUEUE + 10) {
            q.push((n % 128) as u8, 1);
        }
        assert_eq!(unsafe { q.take(&mut out) }, NOTES_PER_BLOCK);
        assert_eq!(out[0], (10, 1));
    }

    #[test]
    fn slot_numbers_and_the_global_key() {
        assert_eq!((ufx_of(15), ufx_of(16), ufx_of(23), ufx_of(24)), (None, Some(0), Some(7), None));
        assert_eq!((dyn8_of(63), dyn8_of(64), dyn8_of(127), dyn8_of(128), dyn8_of(-1)), (None, Some(0), Some(63), None, None));
        let g = GlobalKey::default();
        g.set(9, 1);
        assert_eq!((g.key(), g.scale()), (9, 1));
        g.set(40, 7);
        assert_eq!((g.key(), g.scale()), (11, 2));
    }
}
