//! What only an Allen & Heath dLive has, past its Mixer Config (`profile::DLiveLayout`):
//! a channel's stereo image, ABCD inputs and surround position, a stereo mix's Follow
//! Main Pan, the Direct Outs' global settings and the Source Selectors.
//!
//! From the dLive Firmware Reference Guide (V1.9: Preamp, Routing, MixRack Setup, Audio /
//! Source Select). Where it says less than the code needs (the pan and divergence
//! curves, how 5.0 positions spread, a selector's fade shape), the comments say so.

use crate::atomic::AtomicF32;
use crate::params::StripKind;
use livedaw_dsp::fader::FADER_MIN;
use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering::Relaxed};

/// The tap after a channel's gate, and after its Insert B (Direct Out source points;
/// free ids of `taps.rs`'s fixed ones).
pub const TAP_POST_GATE: usize = 10;
pub const TAP_POST_INSERT_B: usize = 11;

/// A stereo channel's Stereo Image mode (Preamp page).
pub mod stereo_image {
    /// Normal stereo.
    pub const LR: i32 = 0;
    /// Reversed.
    pub const RL: i32 = 1;
    /// Left polarity inverted.
    pub const L_POL_R: i32 = 2;
    /// Reversed, the side fed by the right input inverted.
    pub const R_POL_L: i32 = 3;
    /// Mono sum.
    pub const MONO: i32 = 4;
    /// The left input on both sides.
    pub const LL: i32 = 5;
    /// The right input on both sides.
    pub const RR: i32 = 6;
    /// The first input is Mid, the second Side.
    pub const MS: i32 = 7;
}

/// A channel's front panning in 5.1 (Routing, Surround panning).
pub mod pan_mode {
    pub const LR: i32 = 0;
    pub const LCR: i32 = 1;
    pub const LCR_PLUS: i32 = 2;
}

/// The Global Direct Output Source (Routing).
pub mod direct_source {
    pub const POST_PREAMP: i32 = 0;
    pub const POST_LPF: i32 = 1;
    pub const POST_GATE: i32 = 2;
    pub const POST_INSERT_A: i32 = 3;
    pub const POST_PEQ: i32 = 4;
    pub const POST_COMP: i32 = 5;
    pub const POST_INSERT_B: i32 = 6;
    pub const POST_DELAY: i32 = 7;
}

/// Where a Group's signal leaves it for other Groups (Group to Group, V2.11).
pub mod group_source {
    pub const POST_COMP: i32 = 0;
    pub const POST_DELAY: i32 = 1;
}

/// A strip's dLive settings. `#[repr(C)]`, mirrored by `ld::DLiveStripParams`.
#[repr(C)]
#[derive(Debug)]
pub struct DLiveStripParams {
    /// Stereo channels: [`stereo_image`].
    pub stereo_image: AtomicI32,
    /// Stereo channels: 0 (mono) to 100 (stereo) %.
    pub image_width: AtomicF32,
    /// Stereo mixes: the channels' sends follow their Main pans.
    pub follow_main_pan: AtomicBool,
    /// Stereo mixes: with Follow, mirrored. Alone, it copies the mirrored pans once
    /// (`Engine::set_follow_main_pan`: Sidecar's reading).
    pub follow_invert: AtomicBool,
    /// Channels, LCR+ and 5.1: 0 (all to C) to 100 (all to LR) %.
    pub divergence: AtomicF32,
    /// Channels in 5.1: [`pan_mode`].
    pub pan_mode: AtomicI32,
    /// Channels in 5.1: 0 (front) to 100 (rear) % of the 5.0 image.
    pub surround_rear: AtomicF32,
    /// Channels in 5.1: to the LFE, dB.
    pub lfe_db: AtomicF32,
    /// Channels in 5.1: to the 5.0 mix, dB.
    pub non_lfe_db: AtomicF32,
    /// Channels: the Direct Out's level, dB.
    pub direct_db: AtomicF32,
    /// Channels: ABCD inputs in use.
    pub abcd_on: AtomicBool,
    /// Channels: which of A, B, C, D (0-3) feeds it.
    pub abcd_active: AtomicI32,
    /// Channels: the patch of A to D (an input number, -1 none), first and second input.
    pub abcd_left: [AtomicI32; 4],
    pub abcd_right: [AtomicI32; 4],
    /// Groups: [`group_source`].
    pub group_source: AtomicI32,
}

impl Default for DLiveStripParams {
    fn default() -> Self {
        Self {
            stereo_image: AtomicI32::new(stereo_image::LR),
            image_width: AtomicF32::new(100.0),
            follow_main_pan: AtomicBool::new(false),
            follow_invert: AtomicBool::new(false),
            divergence: AtomicF32::new(100.0),
            pan_mode: AtomicI32::new(pan_mode::LR),
            surround_rear: AtomicF32::new(0.0),
            lfe_db: AtomicF32::new(FADER_MIN),
            non_lfe_db: AtomicF32::new(0.0),
            direct_db: AtomicF32::new(0.0),
            abcd_on: AtomicBool::new(false),
            abcd_active: AtomicI32::new(0),
            abcd_left: std::array::from_fn(|_| AtomicI32::new(-1)),
            abcd_right: std::array::from_fn(|_| AtomicI32::new(-1)),
            group_source: AtomicI32::new(group_source::POST_DELAY),
        }
    }
}

impl DLiveStripParams {
    /// Everything is as a new strip's.
    pub fn is_default(&self) -> bool {
        let d = Self::default();
        let same_i = |a: &AtomicI32, b: &AtomicI32| a.load(Relaxed) == b.load(Relaxed);
        let same_f = |a: &AtomicF32, b: &AtomicF32| a.load() == b.load();
        same_i(&self.stereo_image, &d.stereo_image)
            && same_f(&self.image_width, &d.image_width)
            && !self.follow_main_pan.load(Relaxed)
            && !self.follow_invert.load(Relaxed)
            && same_f(&self.divergence, &d.divergence)
            && same_i(&self.pan_mode, &d.pan_mode)
            && same_f(&self.surround_rear, &d.surround_rear)
            && same_f(&self.lfe_db, &d.lfe_db)
            && same_f(&self.non_lfe_db, &d.non_lfe_db)
            && same_f(&self.direct_db, &d.direct_db)
            && !self.abcd_on.load(Relaxed)
            && same_i(&self.abcd_active, &d.abcd_active)
            && self.abcd_left.iter().chain(&self.abcd_right).all(|p| p.load(Relaxed) < 0)
            && same_i(&self.group_source, &d.group_source)
    }

    /// Back to a new strip's.
    pub fn reset(&self) {
        let d = Self::default();
        self.stereo_image.store(d.stereo_image.load(Relaxed), Relaxed);
        self.image_width.store(d.image_width.load());
        self.follow_main_pan.store(false, Relaxed);
        self.follow_invert.store(false, Relaxed);
        self.divergence.store(d.divergence.load());
        self.pan_mode.store(d.pan_mode.load(Relaxed), Relaxed);
        self.surround_rear.store(d.surround_rear.load());
        self.lfe_db.store(d.lfe_db.load());
        self.non_lfe_db.store(d.non_lfe_db.load());
        self.direct_db.store(d.direct_db.load());
        self.abcd_on.store(false, Relaxed);
        self.abcd_active.store(0, Relaxed);
        for p in self.abcd_left.iter().chain(&self.abcd_right) {
            p.store(-1, Relaxed);
        }
        self.group_source.store(d.group_source.load(Relaxed), Relaxed);
    }
}

/// Source Selectors there are, and sources in each (Audio / Source Select).
pub const NUM_SOURCE_SELECTORS: usize = 20;
pub const MAX_SELECTOR_SOURCES: usize = 20;
/// The longest fade, seconds.
pub const MAX_SELECTOR_FADE_S: f32 = 10.0;

/// One source of a selector: an input or a mix (Group, Aux, Main).
#[derive(Debug)]
pub struct SelectorSource {
    /// `StripKind::ALL`'s index, -1: empty.
    pub kind: AtomicI32,
    /// From 1, as `Engine::strip_by_number`.
    pub number: AtomicI32,
    pub on_db: AtomicF32,
    /// The gain now, owned by the audio thread.
    gain: UnsafeCell<f32>,
}

impl Default for SelectorSource {
    fn default() -> Self {
        Self { kind: AtomicI32::new(-1), number: AtomicI32::new(0), on_db: AtomicF32::new(0.0), gain: UnsafeCell::new(0.0) }
    }
}

// SAFETY: `gain` is touched only by the audio thread (`Engine::tick_selectors`).
unsafe impl Sync for SelectorSource {}

impl SelectorSource {
    /// The strip it names, if any.
    pub fn strip(&self) -> Option<(StripKind, i32)> {
        let kind = *StripKind::ALL.get(usize::try_from(self.kind.load(Relaxed)).ok()?)?;
        Some((kind, self.number.load(Relaxed)))
    }

    pub fn set_strip(&self, strip: Option<(StripKind, i32)>) {
        let index = strip.and_then(|(kind, _)| StripKind::ALL.iter().position(|k| *k == kind));
        self.number.store(strip.map_or(0, |(_, n)| n), Relaxed);
        self.kind.store(index.map_or(-1, |i| i as i32), Relaxed);
    }

    /// Audio thread: a step of the fade; the new gain.
    pub(crate) unsafe fn step(&self, target: f32, up_per_block: f32, down_per_block: f32) -> f32 {
        let g = &mut *self.gain.get();
        *g = if target > *g { (*g + up_per_block).min(target) } else { (*g - down_per_block).max(target) };
        *g
    }
}

/// One Source Selector.
#[derive(Debug)]
pub struct SourceSelector {
    /// `(index in StripKind::ALL)`, -1: unassigned. An Aux (a Bus) or a Matrix.
    pub output_kind: AtomicI32,
    /// The output's bus slot, or matrix index.
    pub output: AtomicI32,
    /// The source playing, -1: Off (V1.90).
    pub active: AtomicI32,
    pub fade_in_s: AtomicF32,
    pub fade_out_s: AtomicF32,
    pub sources: [SelectorSource; MAX_SELECTOR_SOURCES],
}

impl Default for SourceSelector {
    fn default() -> Self {
        Self {
            output_kind: AtomicI32::new(-1),
            output: AtomicI32::new(0),
            active: AtomicI32::new(-1),
            fade_in_s: AtomicF32::new(1.0),
            fade_out_s: AtomicF32::new(1.0),
            sources: Default::default(),
        }
    }
}

impl SourceSelector {
    /// The strip's destination index among `Strip::selector_gain`, if it has an output.
    pub fn destination(&self) -> Option<usize> {
        let kind = *StripKind::ALL.get(usize::try_from(self.output_kind.load(Relaxed)).ok()?)?;
        selector_destination(kind, usize::try_from(self.output.load(Relaxed)).ok()?)
    }

    /// Set up at all.
    pub fn is_default(&self) -> bool {
        self.output_kind.load(Relaxed) < 0 && self.sources.iter().all(|s| s.kind.load(Relaxed) < 0)
    }
}

/// `Strip::selector_gain`'s index for a send to `kind`'s `index`: bus slots, then matrices.
pub fn selector_destination(kind: StripKind, index: usize) -> Option<usize> {
    match kind {
        StripKind::Bus => (index < crate::routing::MAX_SOURCE_BUSES).then_some(index),
        StripKind::Matrix => Some(crate::routing::MAX_SOURCE_BUSES + index).filter(|&d| d < NUM_SELECTOR_DESTINATIONS),
        _ => None,
    }
}

/// How many destinations a strip's selector gains cover.
pub const NUM_SELECTOR_DESTINATIONS: usize = crate::routing::MAX_SOURCE_BUSES + crate::params::MAX_MATRICES;

/// The console-wide settings of a dLive. Direct Out's are `#[repr(C)]`-free atomics read
/// through their own FFI calls.
#[derive(Debug)]
pub struct DLiveControls {
    /// [`direct_source`], all channels.
    pub direct_source: AtomicI32,
    pub direct_follow_fader: AtomicBool,
    pub direct_follow_mute: AtomicBool,
    /// DCA and mutes, channel fader and mute.
    pub direct_follow_all: AtomicBool,
    pub selectors: [SourceSelector; NUM_SOURCE_SELECTORS],
}

impl Default for DLiveControls {
    fn default() -> Self {
        Self {
            direct_source: AtomicI32::new(direct_source::POST_DELAY),
            direct_follow_fader: AtomicBool::new(false),
            direct_follow_mute: AtomicBool::new(false),
            direct_follow_all: AtomicBool::new(false),
            selectors: Default::default(),
        }
    }
}

impl DLiveControls {
    pub fn reset(&self) {
        self.direct_source.store(direct_source::POST_DELAY, Relaxed);
        self.direct_follow_fader.store(false, Relaxed);
        self.direct_follow_mute.store(false, Relaxed);
        self.direct_follow_all.store(false, Relaxed);
        for s in &self.selectors {
            s.output_kind.store(-1, Relaxed);
            s.active.store(-1, Relaxed);
            s.fade_in_s.store(1.0);
            s.fade_out_s.store(1.0);
            for m in &s.sources {
                m.set_strip(None);
                m.on_db.store(0.0);
            }
        }
    }
}

/// A stereo channel's image, in place: Mode, then Width (block diagram: Width and Mode
/// come ahead of Trim). `from_width` to `to_width` are 0..1, ramped over the block.
pub fn apply_stereo_image(l: &mut [f32], r: &mut [f32], mode: i32, from_width: f32, to_width: f32) {
    for (x, y) in l.iter_mut().zip(r.iter_mut()) {
        let (a, b) = (*x, *y);
        (*x, *y) = match mode {
            stereo_image::RL => (b, a),
            stereo_image::L_POL_R => (-a, b),
            stereo_image::R_POL_L => (-b, a),
            stereo_image::MONO => (0.5 * (a + b), 0.5 * (a + b)),
            stereo_image::LL => (a, a),
            stereo_image::RR => (b, b),
            stereo_image::MS => (a + b, a - b),
            _ => (a, b),
        };
    }
    if from_width >= 1.0 && to_width >= 1.0 {
        return;
    }
    let n = l.len().max(1) as f32;
    let step = (to_width - from_width) / n;
    let mut w = from_width;
    for (x, y) in l.iter_mut().zip(r.iter_mut()) {
        w += step;
        let mid = 0.5 * (*x + *y);
        let side = 0.5 * (*x - *y) * w;
        *x = mid + side;
        *y = mid - side;
    }
}

/// The front triplet's gains for a mono source at `pan` (-1 hard left to 1 hard right):
/// (L, C, R). At mid pan all of it goes to C, none to L and R (Routing, MixRack Setup).
/// The curve between is Sidecar's reading: equal power.
pub fn lcr_gains(pan: f32) -> (f32, f32, f32) {
    let a = pan.clamp(-1.0, 1.0);
    let (side, centre) = (a.abs() * std::f32::consts::FRAC_PI_2).sin_cos();
    (if a < 0.0 { side } else { 0.0 }, centre, if a > 0.0 { side } else { 0.0 })
}

/// LCR+: `divergence` 0..1 blends between C alone and the stereo image `lr` (full
/// divergence: no C, a phantom centre; none, at mid pan: C alone). The law between is
/// Sidecar's reading: equal power, C not following the pan.
pub fn lcr_plus_gains(lr: [f32; 2], divergence: f32) -> (f32, f32, f32) {
    let (to_lr, to_c) = (divergence.clamp(0.0, 1.0) * std::f32::consts::FRAC_PI_2).sin_cos();
    (lr[0] * to_lr, to_c, lr[1] * to_lr)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stereo_image_modes() {
        let run = |mode: i32| {
            let (mut l, mut r) = ([1.0f32], [0.5f32]);
            apply_stereo_image(&mut l, &mut r, mode, 1.0, 1.0);
            (l[0], r[0])
        };
        assert_eq!(run(stereo_image::LR), (1.0, 0.5));
        assert_eq!(run(stereo_image::RL), (0.5, 1.0));
        assert_eq!(run(stereo_image::L_POL_R), (-1.0, 0.5));
        assert_eq!(run(stereo_image::R_POL_L), (-0.5, 1.0));
        assert_eq!(run(stereo_image::MONO), (0.75, 0.75));
        assert_eq!(run(stereo_image::LL), (1.0, 1.0));
        assert_eq!(run(stereo_image::RR), (0.5, 0.5));
        assert_eq!(run(stereo_image::MS), (1.5, 0.5));
    }

    #[test]
    fn the_c_struct_keeps_its_size() {
        // `ld::DLiveStripParams` mirrors it field for field (not in the ABI size table).
        assert_eq!(std::mem::size_of::<DLiveStripParams>(), 80);
    }

    #[test]
    fn width_zero_is_mono() {
        let (mut l, mut r) = ([1.0f32; 4], [0.0f32; 4]);
        apply_stereo_image(&mut l, &mut r, stereo_image::LR, 0.0, 0.0);
        assert_eq!((l[3], r[3]), (0.5, 0.5));
    }

    #[test]
    fn lcr_centre_is_centre_alone() {
        let (l, c, r) = lcr_gains(0.0);
        assert_eq!((l, r), (0.0, 0.0));
        assert!((c - 1.0).abs() < 1e-6);
        let (l, c, r) = lcr_gains(-1.0);
        assert!((l - 1.0).abs() < 1e-6 && c.abs() < 1e-6 && r == 0.0);
    }

    #[test]
    fn divergence_moves_between_c_and_lr() {
        let (l, c, r) = lcr_plus_gains([0.7, 0.7], 0.0);
        assert!(l.abs() < 1e-6 && r.abs() < 1e-6 && (c - 1.0).abs() < 1e-6, "none: C alone");
        let (l, c, r) = lcr_plus_gains([0.7, 0.7], 1.0);
        assert!((l - 0.7).abs() < 1e-6 && (r - 0.7).abs() < 1e-6 && c.abs() < 1e-6, "full: a phantom centre");
    }
}
