//! Runs an X32 FX device on a raw f32 mono file, for comparing with recordings of
//! the console. The input feeds both sides, the output is interleaved f32
//! (left, right).
//!
//!     x32_fx_render TOKEN in.f32 out.f32 [INDEX=OSC ...]
//!
//! INDEX is the 0-based parameter, OSC its X32 value 0..1, or #N a list index.
use livedaw_dsp::model::{ParamKind, ParamSpec};
use livedaw_dsp::wing::fx::{create, FxContext};
use livedaw_dsp::x32::FX_MODELS;

fn from_osc(p: &ParamSpec, n: f32) -> f32 {
    match p.kind {
        ParamKind::Linear => p.min + n * (p.max - p.min),
        ParamKind::Log => p.min * (p.max / p.min).powf(n),
        _ => (n * (p.max - p.min)).round() + p.min,
    }
}

fn main() {
    let a: Vec<String> = std::env::args().collect();
    let spec = FX_MODELS.iter().find(|m| m.token == a[1]).expect("token");
    let input: Vec<f32> =
        std::fs::read(&a[2]).unwrap().chunks_exact(4).map(|b| f32::from_le_bytes([b[0], b[1], b[2], b[3]])).collect();
    let mut values: Vec<f32> = spec.params.iter().map(|p| p.default).collect();
    for s in &a[4..] {
        let (i, v) = s.split_once('=').unwrap();
        let i: usize = i.parse().unwrap();
        values[i] = match v.strip_prefix('#') {
            Some(n) => n.parse().unwrap(),
            None => from_osc(&spec.params[i], v.parse().unwrap()),
        };
    }
    let block = 64;
    let mut dev = create(spec.token, FxContext { sample_rate: 48000.0, max_block: block }).unwrap();
    dev.set_params(&livedaw_dsp::model::Params::new(spec, &values));
    let mut out = Vec::with_capacity(input.len() * 8);
    for c in input.chunks(block) {
        let (mut l, mut r) = (c.to_vec(), c.to_vec());
        dev.process(&mut l, &mut r);
        for (x, y) in l.iter().zip(&r) {
            out.extend_from_slice(&x.to_le_bytes());
            out.extend_from_slice(&y.to_le_bytes());
        }
    }
    std::fs::write(&a[3], out).unwrap();
}
