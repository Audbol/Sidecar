//! The X32's 3-Tap and 4-Tap Delays (X323TAP, X324TAP; Phase 4 step 9e).
//!
//! A delay line per side, read at TIME (the base tap) and at TIME x each
//! tap's FACTOR (1/4 .. 3). Each tap has its GAIN (0-100 %); 3-Tap pans each
//! tap (PAN, a balance of the two sides), 4-Tap spreads its four taps across
//! the image by SPREAD (0 = all centred, 6 = alternating hard left / right).
//! FEEDBACK returns the base tap into the line (square law, as the stereo
//! delay's), CROSS FEED into the other side's (the repeats ping-pong); every
//! pass goes through LO CUT / HI CUT. MONO feeds both lines the input's sum;
//! DRY adds the input under the taps. Time changes crossfade.
//!
//! The design is Sidecar's reading of the controls, to be measured on an X32.

use super::XfadeTap;
use crate::wing::fx::blocks::DelayLine;
use crate::wing::fx::modulation::shared::{loop_limit, ms, CutFilter};
use crate::wing::fx::{FxAlgo, FxContext};
use crate::wing::Params;

/// FACTOR choices: 1/4, 3/8, 1/2, 2/3, 1, 4/3, 3/2, 2, 3.
const FACTORS: [f32; 9] = [0.25, 0.375, 0.5, 2.0 / 3.0, 1.0, 4.0 / 3.0, 1.5, 2.0, 3.0];
/// TIME x FACTOR at its largest.
const MAX_MS: f32 = 3000.0 * 3.0 + 10.0;
const FADE_MS: f32 = 50.0;
/// The base tap and up to three more.
const TAPS: usize = 4;

pub(in crate::wing::fx) struct TapDelay {
    sr: f32,
    /// 3 (3-Tap: base, A, B) or 4 (4-Tap: base, A, B, C).
    count: usize,
    lines: [DelayLine; 2],
    taps: [[XfadeTap; 2]; TAPS],
    /// Per tap: the gain to the left and to the right output.
    gains: [(f32, f32); TAPS],
    last_gains: [(f32, f32); TAPS],
    feedback: f32,
    cross: bool,
    mono: bool,
    dry: f32,
    cut: CutFilter,
    primed: bool,
}

impl TapDelay {
    pub(in crate::wing::fx) fn new(count: usize, ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let len = ms(MAX_MS, sr) as usize + 4;
        Self {
            sr,
            count: count.min(TAPS),
            lines: [DelayLine::new(len), DelayLine::new(len)],
            taps: [[XfadeTap::new(FADE_MS, sr); 2]; TAPS],
            gains: [(0.0, 0.0); TAPS],
            last_gains: [(0.0, 0.0); TAPS],
            feedback: 0.0,
            cross: false,
            mono: false,
            dry: 0.0,
            cut: CutFilter::new(sr),
            primed: false,
        }
    }
}

/// A balance (-1..1) as the two sides' levels: each at full until the other side.
fn pan(p: f32) -> (f32, f32) {
    let p = p.clamp(-1.0, 1.0);
    ((1.0 - p).min(1.0), (1.0 + p).min(1.0))
}

impl FxAlgo for TapDelay {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        let max = self.lines[0].max_delay() - 1;
        // Taps are read before the new sample is written: index = delay - 1.
        let tap = |t_ms: f32| (ms(t_ms.max(0.0), sr).round() as usize).saturating_sub(1).max(1).min(max);
        let time = p.f("time");
        let factor = |id: &str| FACTORS.get(p.choice(id)).copied().unwrap_or(1.0);
        let times = [time, time * factor("factor_a"), time * factor("factor_b"), time * factor("factor_c")];
        let level = |id: &str| (p.f(id) * 0.01).clamp(0.0, 1.0);
        let levels = [level("gain_base"), level("gain_a"), level("gain_b"), level("gain_c")];
        let pans = if self.count == 3 {
            [p.f("pan_base"), p.f("pan_a"), p.f("pan_b"), 0.0].map(|x| x * 0.01)
        } else {
            let s = p.f("spread") / 6.0;
            [-s, s, -0.5 * s, 0.5 * s]
        };
        for k in 0..self.count {
            for side in &mut self.taps[k] {
                side.set(tap(times[k]));
            }
            let (l, r) = pan(pans[k]);
            self.gains[k] = (levels[k] * l, levels[k] * r);
        }
        self.feedback = (p.f("feedback") * 0.01).powi(2);
        self.cross = p.on("cross_feed");
        self.mono = p.on("mono");
        self.dry = if p.on("dry") { 1.0 } else { 0.0 };
        self.cut.set(p.f("lo_cut"), p.f("hi_cut"));
        if !self.primed {
            self.primed = true;
            self.taps.iter_mut().flatten().for_each(XfadeTap::snap);
            self.last_gains = self.gains;
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let n = left.len().max(1) as f32;
        let (count, fb) = (self.count, self.feedback);
        for (i, (l, r)) in left.iter_mut().zip(right.iter_mut()).enumerate() {
            let t = (i + 1) as f32 / n;
            let (dl, dr) = (*l, *r);
            let mut out = [0.0f32; 2];
            let mut base = [0.0f32; 2];
            for k in 0..count {
                let (gl, gr) = self.gains[k];
                let (ll, lr) = self.last_gains[k];
                let g = [ll + (gl - ll) * t, lr + (gr - lr) * t];
                for side in 0..2 {
                    let y = self.taps[k][side].read(&self.lines[side]);
                    if k == 0 {
                        base[side] = y;
                    }
                    out[side] += g[side] * y;
                }
            }
            let (il, ir) = if self.mono { (0.5 * (dl + dr), 0.5 * (dl + dr)) } else { (dl, dr) };
            let (fl, fr) = if self.cross { (base[1], base[0]) } else { (base[0], base[1]) };
            let xl = self.cut.tick(0, il + fb * fl);
            let xr = self.cut.tick(1, ir + fb * fr);
            self.lines[0].push(loop_limit(xl));
            self.lines[1].push(loop_limit(xr));
            *l = out[0] + self.dry * dl;
            *r = out[1] + self.dry * dr;
        }
        self.last_gains = self.gains;
    }

    fn reset(&mut self) {
        for l in &mut self.lines {
            l.clear();
        }
        self.taps.iter_mut().flatten().for_each(XfadeTap::snap);
        self.cut.reset();
        self.last_gains = self.gains;
    }
}

#[cfg(test)]
mod tests {
    use super::super::tests::{device, impulse_lr, peak, SR};

    #[test]
    fn taps_land_at_their_factors_and_pan() {
        // 3-Tap: base at 200 ms hard left, A at x1/2 hard right, B at x2 centred; no feedback.
        let open = [("lo_cut", 10.0), ("hi_cut", 20_000.0), ("feedback", 0.0), ("time", 200.0)];
        let taps = [("pan_base", -100.0), ("factor_a", 2.0), ("pan_a", 100.0), ("factor_b", 7.0), ("pan_b", 0.0)];
        let gains = [("gain_base", 100.0), ("gain_a", 100.0), ("gain_b", 100.0)];
        let (mut dev, v) = device("X323TAP", &[&open[..], &taps[..], &gains[..]].concat());
        let (l, r) = impulse_lr(&mut dev, &v, 1.0, 1.0, 1.0);
        let at = |x: &[f32], t_ms: f32| peak(x, (t_ms * 0.001 * SR) as usize - 20, (t_ms * 0.001 * SR) as usize + 20).1;
        assert!(at(&l, 200.0) > 0.5 && at(&r, 200.0) < 1e-3, "base hard left");
        assert!(at(&r, 100.0) > 0.5 && at(&l, 100.0) < 1e-3, "A hard right at 1/2");
        assert!(at(&l, 400.0) > 0.5 && at(&r, 400.0) > 0.5, "B centred at x2");
        // DRY off: nothing at once; DRY on: the input under the taps.
        assert!(l[0].abs() < 1e-6);
        let (mut dev, v) = device("X323TAP", &[&open[..], &[("dry", 1.0)]].concat());
        let (l, _) = impulse_lr(&mut dev, &v, 0.1, 1.0, 1.0);
        assert!((l[0] - 1.0).abs() < 1e-6);
    }

    #[test]
    fn feedback_repeats_the_base_tap_and_cross_feed_swaps_sides() {
        let open = [("lo_cut", 10.0), ("hi_cut", 20_000.0), ("time", 100.0), ("gain_a", 0.0), ("gain_b", 0.0), ("gain_c", 0.0)];
        let fb = [("feedback", 71.0), ("gain_base", 100.0), ("spread", 0.0)];
        let (mut dev, v) = device("X324TAP", &[&open[..], &fb[..]].concat());
        let (l, r) = impulse_lr(&mut dev, &v, 0.5, 1.0, 0.0);
        let t = (0.1 * SR) as usize;
        let at = |x: &[f32], k: usize| peak(x, k * t - 30, k * t + 30).1;
        let (l1, l2, r2) = (at(&l, 1), at(&l, 2), at(&r, 2));
        assert!(l1 > 0.5 && l2 > 0.2 && r2 < 1e-3, "repeats stay left: {l1} {l2} {r2}");
        let (mut dev, v) = device("X324TAP", &[&open[..], &fb[..], &[("cross_feed", 1.0)]].concat());
        let (l, r) = impulse_lr(&mut dev, &v, 0.5, 1.0, 0.0);
        assert!(at(&l, 1) > 0.5 && at(&r, 2) > 0.2 && at(&l, 2) < 1e-3, "the second repeat on the right");
    }
}
