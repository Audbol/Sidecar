//! Level conversions, pan laws and block helpers.

/// Anything at or below this level is treated as silence (-inf dB).
pub const MINUS_INF_DB: f32 = -100.0;

#[inline]
pub fn db_to_gain(db: f32) -> f32 {
    if db <= MINUS_INF_DB {
        0.0
    } else {
        10.0f32.powf(db * 0.05)
    }
}

#[inline]
pub fn gain_to_db(gain: f32) -> f32 {
    if gain > 0.0 {
        (20.0 * gain.log10()).max(MINUS_INF_DB)
    } else {
        MINUS_INF_DB
    }
}

/// Below this many dB either way, [`db_to_gain`] is 1.0.
const NO_CHANGE_DB: f32 = 1.0e-7;

/// [`db_to_gain`] that keeps its last answer: while the level it is asked for doesn't move (a
/// compressor not compressing, a gate that is open or shut) it costs a comparison, not a
/// `powf`. The answer is always `db_to_gain`'s own, bit for bit.
#[derive(Clone, Copy, Debug)]
pub struct DbToGainMemo {
    db: u32,
    gain: f32,
}

impl Default for DbToGainMemo {
    fn default() -> Self {
        // A NaN no caller passes, with the NaN `db_to_gain` gives for it.
        Self { db: 0x7fc0_0001, gain: f32::NAN }
    }
}

impl DbToGainMemo {
    #[inline]
    pub fn get(&mut self, db: f32) -> f32 {
        // A level this close to 0 dB is a gain of exactly 1 (a reduction dying away): the true
        // value is within 1.2e-9 of 1, a hundredth of the step between f32s there.
        if db.abs() < NO_CHANGE_DB {
            return 1.0;
        }
        if db.to_bits() != self.db {
            self.db = db.to_bits();
            self.gain = db_to_gain(db);
        }
        self.gain
    }
}

/// [`gain_to_db`] that keeps its last answer, as [`DbToGainMemo`] does for the other way.
#[derive(Clone, Copy, Debug)]
pub struct GainToDbMemo {
    gain: u32,
    db: f32,
}

impl Default for GainToDbMemo {
    fn default() -> Self {
        Self { gain: 0x7fc0_0001, db: f32::NAN }
    }
}

impl GainToDbMemo {
    #[inline]
    pub fn get(&mut self, gain: f32) -> f32 {
        if gain.to_bits() != self.gain {
            self.gain = gain.to_bits();
            self.db = gain_to_db(gain);
        }
        self.db
    }
}

/// One-pole smoothing coefficient for a time constant in milliseconds.
#[inline]
pub fn time_constant_coeff(ms: f32, sample_rate: f64) -> f32 {
    if ms <= 0.0 || sample_rate <= 0.0 {
        return 0.0;
    }
    (-1.0 / (0.001 * ms as f64 * sample_rate)).exp() as f32
}

/// Multiplies a block by a gain moving linearly from `start` to `end`.
#[inline]
pub fn apply_gain_ramp(data: &mut [f32], start: f32, end: f32) {
    if start == end {
        if start != 1.0 {
            data.iter_mut().for_each(|x| *x *= start);
        }
        return;
    }
    let step = (end - start) / data.len().max(1) as f32;
    let mut g = start;
    for x in data.iter_mut() {
        g += step;
        *x *= g;
    }
}

/// `dst += src * gain`, with the gain ramping from `start` to `end`.
#[inline]
pub fn add_with_gain_ramp(dst: &mut [f32], src: &[f32], start: f32, end: f32) {
    if start == 0.0 && end == 0.0 {
        return;
    }
    let n = dst.len().min(src.len());
    if start == end {
        for i in 0..n {
            dst[i] += src[i] * start;
        }
        return;
    }
    let step = (end - start) / n.max(1) as f32;
    let mut g = start;
    for i in 0..n {
        g += step;
        dst[i] += src[i] * g;
    }
}

/// The largest absolute sample (a NaN never counts). Eight running maxima advance together:
/// one running maximum would make every sample wait for the one before, and a maximum is the
/// same in any order, so the result is exactly that of a single pass.
#[inline]
pub fn peak_level(data: &[f32]) -> f32 {
    let mut lanes = [0.0f32; 8];
    let mut chunks = data.chunks_exact(8);
    for c in &mut chunks {
        for (l, x) in lanes.iter_mut().zip(c) {
            let a = x.abs();
            if a > *l {
                *l = a;
            }
        }
    }
    let mut peak = 0.0f32;
    for a in lanes.into_iter().chain(chunks.remainder().iter().map(|x| x.abs())) {
        if a > peak {
            peak = a;
        }
    }
    peak
}

/// True if the block contains no NaN or infinity.
#[inline]
pub fn all_finite(data: &[f32]) -> bool {
    // inf * 0 and NaN * 0 are NaN, and NaN poisons the sum: one branch per block. Eight sums
    // advance together (a sum of zeros is zero in any order, and a NaN reaches the total
    // whichever sum it lands in).
    let mut lanes = [0.0f32; 8];
    let mut chunks = data.chunks_exact(8);
    for c in &mut chunks {
        for (l, x) in lanes.iter_mut().zip(c) {
            *l += x * 0.0;
        }
    }
    let acc = lanes.into_iter().chain(chunks.remainder().iter().map(|x| x * 0.0)).fold(0.0f32, |acc, x| acc + x);
    acc == 0.0
}

/// The WING's pan law for a mono source; `pan` is -1 (L) .. +1 (R). Unity on
/// both sides at centre; panning rises the near side linearly to +3 dB and
/// lowers the far side linearly to silence (measured on a WING: centre 0 / 0 dB,
/// L50 +1.63 / -6.02 dB, L100 +3.01 dB / off).
#[inline]
pub fn wing_mono_pan(pan: f32) -> (f32, f32) {
    let pan = pan.clamp(-1.0, 1.0);
    let near = 1.0 + pan.abs() * (std::f32::consts::SQRT_2 - 1.0);
    let far = 1.0 - pan.abs();
    if pan < 0.0 {
        (near, far)
    } else {
        (far, near)
    }
}

/// Balance law for a stereo source: the opposite side is attenuated, centre is unity.
#[inline]
pub fn stereo_balance(pan: f32) -> (f32, f32) {
    let pan = pan.clamp(-1.0, 1.0);
    ((1.0 - pan).min(1.0), (1.0 + pan).min(1.0))
}

/// A pan law: the left and right gains of a mono source and of a stereo one
/// (its balance) for a pan of -1 (L) .. +1 (R). A console's profile names its law.
#[derive(Clone, Copy, Debug)]
pub struct PanLaw {
    pub mono: fn(f32) -> (f32, f32),
    pub stereo: fn(f32) -> (f32, f32),
}

impl PanLaw {
    /// The WING's: [`wing_mono_pan`] and [`stereo_balance`].
    pub const WING: PanLaw = PanLaw { mono: wing_mono_pan, stereo: stereo_balance };

    /// The gains of a mono (or widened) source, else a stereo one.
    #[inline]
    pub fn gains(&self, mono_source: bool, pan: f32) -> (f32, f32) {
        if mono_source {
            (self.mono)(pan)
        } else {
            (self.stereo)(pan)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn db_conversions() {
        assert!((db_to_gain(0.0) - 1.0).abs() < 1e-6);
        assert!((db_to_gain(-6.0206) - 0.5).abs() < 1e-4);
        assert_eq!(db_to_gain(MINUS_INF_DB), 0.0);
        assert!((gain_to_db(0.5) + 6.0206).abs() < 1e-3);
    }

    #[test]
    fn memos_answer_as_the_functions_do() {
        let (mut up, mut down) = (DbToGainMemo::default(), GainToDbMemo::default());
        for x in [0.0f32, 0.0, -6.0, -6.0, -120.0, -120.0, 3.5, f32::NAN, f32::NAN, f32::INFINITY, 0.0, 1e-8, -1e-8] {
            assert_eq!(up.get(x).to_bits(), db_to_gain(x).to_bits(), "{x}");
            assert_eq!(down.get(x).to_bits(), gain_to_db(x).to_bits(), "{x}");
        }
    }

    #[test]
    fn a_level_near_zero_is_a_gain_of_one() {
        // Every `step`th f32 below the bound, either sign, and the values just inside it.
        let top = NO_CHANGE_DB.to_bits();
        for bits in (0..=top).step_by(4099).chain(top - 3..=top) {
            for sign in [0, 0x8000_0000] {
                let x = f32::from_bits(bits | sign);
                assert_eq!(db_to_gain(x), 1.0, "{x:e}");
            }
        }
    }

    #[test]
    fn pan_laws() {
        // The levels a WING gives at centre, L50 and L100.
        assert_eq!(wing_mono_pan(0.0), (1.0, 1.0));
        let (l, r) = wing_mono_pan(-0.5);
        assert!((gain_to_db(l) - 1.63).abs() < 0.01 && (gain_to_db(r) + 6.02).abs() < 0.01);
        let (l, r) = wing_mono_pan(-1.0);
        assert!((gain_to_db(l) - 3.01).abs() < 0.01 && r == 0.0);
        assert_eq!(wing_mono_pan(0.5), (wing_mono_pan(-0.5).1, wing_mono_pan(-0.5).0));
        assert_eq!(stereo_balance(0.0), (1.0, 1.0));
        let (l, r) = stereo_balance(0.5);
        assert!((l - 0.5).abs() < 1e-6 && r == 1.0);
    }

    #[test]
    fn peak_and_finite_checks_agree_with_the_plain_passes() {
        let mut rng = 7u32;
        let mut next = || {
            rng = rng.wrapping_mul(1664525).wrapping_add(1013904223);
            (rng >> 8) as f32 / (1 << 23) as f32 - 1.0
        };
        for len in 0..70 {
            let mut v: Vec<f32> = (0..len).map(|_| next() * 3.0).collect();
            for special in [None, Some(f32::NAN), Some(f32::INFINITY), Some(f32::NEG_INFINITY), Some(-0.0)] {
                if let (Some(x), true) = (special, len > 0) {
                    v[len / 2] = x;
                    v[len - 1] = x;
                }
                let plain_peak = v.iter().fold(0.0f32, |p, x| p.max(x.abs()));
                assert_eq!(peak_level(&v).to_bits(), plain_peak.to_bits(), "{len} {special:?}");
                let plain_finite = v.iter().fold(0.0f32, |acc, x| acc + x * 0.0) == 0.0;
                assert_eq!(all_finite(&v), plain_finite, "{len} {special:?}");
            }
        }
    }

    #[test]
    fn finite_check() {
        assert!(all_finite(&[0.0, 1.0, -1.0]));
        assert!(!all_finite(&[0.0, f32::NAN]));
        assert!(!all_finite(&[f32::INFINITY]));
    }

    #[test]
    fn ramps() {
        let mut d = [1.0f32; 4];
        apply_gain_ramp(&mut d, 0.0, 1.0);
        assert!((d[3] - 1.0).abs() < 1e-6 && d[0] > 0.0 && d[0] < 1.0);
        let mut dst = [0.0f32; 4];
        add_with_gain_ramp(&mut dst, &[2.0; 4], 0.5, 0.5);
        assert_eq!(dst, [1.0; 4]);
    }
}
