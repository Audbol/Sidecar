//! The X32's meter replies (`/meters`): linear levels (1.0 is full scale) and
//! gain reductions as gains (1.0 is none), in a blob of a little-endian count
//! and little-endian floats.
//!
//! Requests served: 0 (the METERS page: 32 channels, 8 aux in, 8 FX returns,
//! 16 buses, 6 matrices), 1 (32 channels, their gate and dynamics gain
//! reductions), 2 (16 buses, 6 matrices, L, R, M/C, then their dynamics gain
//! reductions), 6 (one strip: post trim, gate, dynamics, post fader) and 13
//! (32 channels, 8 aux in, 8 FX returns).

use crate::map::{Console, Target};

/// Strips by meter id (0-71: channels, aux in, FX returns, buses, matrices, LR, M/C).
pub const STRIPS: usize = 72;

/// The levels of every strip at one moment.
#[derive(Clone, Debug)]
pub struct Levels {
    /// After trim and polarity (before the processing).
    pub pre: [f32; STRIPS],
    /// The output, left and right (mono strips: both the same).
    pub out: [[f32; 2]; STRIPS],
    /// Gate and dynamics gain reductions as gains.
    pub gate: [f32; STRIPS],
    pub dyn_: [f32; STRIPS],
}

impl Default for Levels {
    fn default() -> Self {
        Self { pre: [0.0; STRIPS], out: [[0.0; 2]; STRIPS], gate: [1.0; STRIPS], dyn_: [1.0; STRIPS] }
    }
}

fn gain(reduction_db: f32) -> f32 {
    if reduction_db.is_finite() {
        10f32.powf(-reduction_db.abs() / 20.0)
    } else {
        1.0
    }
}

impl Levels {
    /// Takes the strips' peaks since the last take (the engine's remote
    /// peaks: one reader).
    pub fn take(c: &Console) -> Levels {
        let mut l = Levels::default();
        for id in 0..STRIPS {
            let Some(s) = Target::of_meter_id(id).and_then(|t| c.strip(t)) else { continue };
            let [_, pre, out] = s.remote_peaks.take();
            l.pre[id] = pre[0].max(pre[1]);
            l.out[id] = out;
            l.gate[id] = gain(s.meters.gate_reduction_db.load());
            l.dyn_[id] = gain(s.meters.dyn_reduction_db.load());
        }
        l
    }

    fn out(&self, id: usize) -> f32 {
        self.out[id][0].max(self.out[id][1])
    }

    /// The values of meter request `id` with its parameters, if served.
    pub fn frame(&self, id: u32, params: &[i32]) -> Option<Vec<f32>> {
        let inputs = |l: &Levels| (0..48).map(|i| l.pre[i]).collect::<Vec<f32>>();
        Some(match id {
            0 => {
                let mut v = inputs(self);
                v.extend((48..70).map(|i| self.out(i)));
                v
            }
            1 => {
                let mut v: Vec<f32> = self.pre[..32].to_vec();
                v.extend_from_slice(&self.gate[..32]);
                v.extend_from_slice(&self.dyn_[..32]);
                v
            }
            2 => {
                let mut v: Vec<f32> = (48..70).map(|i| self.out(i)).collect();
                v.extend_from_slice(&self.out[70]);
                v.push(self.out(71));
                v.extend_from_slice(&self.dyn_[48..72]);
                v
            }
            6 => {
                let id = usize::try_from(*params.first()?).ok().filter(|&i| i < STRIPS)?;
                vec![self.pre[id], self.gate[id], self.dyn_[id], self.out(id)]
            }
            13 => inputs(self),
            _ => return None,
        })
    }
}

/// The parameters a request takes before its time factor.
pub fn param_count(id: u32) -> usize {
    match id {
        5 => 2,
        6 => 1,
        _ => 0,
    }
}

/// The reply blob: the count, then the values (little-endian).
pub fn blob(values: &[f32]) -> Vec<u8> {
    let mut b = Vec::with_capacity(4 + 4 * values.len());
    b.extend_from_slice(&(values.len() as i32).to_le_bytes());
    for v in values {
        b.extend_from_slice(&v.to_le_bytes());
    }
    b
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn meter_sizes() {
        let l = Levels::default();
        for (id, n) in [(0, 70), (1, 96), (2, 49), (13, 48)] {
            assert_eq!(l.frame(id, &[]).unwrap().len(), n, "/meters/{id}");
        }
        assert_eq!(l.frame(6, &[71]).unwrap(), [0.0, 1.0, 1.0, 0.0], "no reduction is a gain of 1");
        assert!(l.frame(6, &[72]).is_none() && l.frame(6, &[]).is_none() && l.frame(3, &[]).is_none());
        assert_eq!(blob(&[1.0]), [1, 0, 0, 0, 0, 0, 0x80, 0x3f]);
        assert!((gain(-6.0) - 0.501).abs() < 1e-3);
    }
}
