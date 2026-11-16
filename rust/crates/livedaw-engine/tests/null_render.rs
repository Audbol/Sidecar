//! A null test for the engine's CPU work: a fixed multi-channel signal through the WING, the
//! X32, a dLive and the 64-channel plug-in host, each set up with processing (`common/rigs.rs`),
//! must come out bit for bit as it did before the engine was optimised. The reference is a
//! checksum of every output sample's bits (`null_reference.txt`) with each output's peak and
//! energy, so a failure says where it differs.
//!
//! To look at a difference in detail, set `NULL_DUMP=<dir>` to write each console's output
//! as raw f32 (`<dir>/<console>.f32`), and `NULL_COMPARE=<dir>` to report how far this
//! build's output is from such a dump. To make a new reference after a change meant to alter
//! the sound, run with `NULL_UPDATE=1`.

#[path = "common/rigs.rs"]
mod rigs;

use rigs::{Console, Rig};
use std::fmt::Write as _;

const BLOCKS: usize = 250;

struct Render {
    ports: usize,
    hash: u64,
    peak: Vec<f32>,
    energy: Vec<f64>,
    samples: Vec<f32>,
}

fn render(console: Console, workers: usize) -> Render {
    let mut rig = Rig::new(console, workers);
    let ports = rig.outputs.len();
    let mut r = Render { ports, hash: 0xcbf29ce484222325, peak: vec![0.0; ports], energy: vec![0.0; ports], samples: Vec::new() };
    for _ in 0..BLOCKS {
        rig.step();
        for (p, out) in rig.outputs.iter().enumerate() {
            for &x in out {
                for b in x.to_bits().to_le_bytes() {
                    r.hash = (r.hash ^ b as u64).wrapping_mul(0x100000001b3);
                }
                assert!(x.is_finite(), "{}: output {p} is not finite", console.name());
                r.peak[p] = r.peak[p].max(x.abs());
                r.energy[p] += (x as f64) * (x as f64);
            }
        }
        if std::env::var_os("NULL_DUMP").is_some() || std::env::var_os("NULL_COMPARE").is_some() {
            for out in &rig.outputs {
                r.samples.extend_from_slice(out);
            }
        }
    }
    r
}

fn line(name: &str, r: &Render) -> String {
    let mut s = format!("{name} ports={} hash={:016x}", r.ports, r.hash);
    for p in 0..r.ports {
        write!(s, " {:.9e}/{:.9e}", r.peak[p], r.energy[p]).unwrap();
    }
    s
}

fn file_name(c: Console) -> String {
    c.name().replace(' ', "_")
}

#[test]
fn consoles_render_what_they_always_did() {
    let reference = std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/tests/null_reference.txt")).unwrap_or_default();
    let mut fresh = String::new();
    let mut failures = Vec::new();
    for console in Console::ALL {
        let r = render(console, 0);
        let now = line(&file_name(console), &r);
        if let Some(dir) = std::env::var_os("NULL_DUMP") {
            let bytes: Vec<u8> = r.samples.iter().flat_map(|x| x.to_le_bytes()).collect();
            std::fs::write(std::path::Path::new(&dir).join(format!("{}.f32", file_name(console))), bytes).unwrap();
        }
        if let Some(dir) = std::env::var_os("NULL_COMPARE") {
            let bytes = std::fs::read(std::path::Path::new(&dir).join(format!("{}.f32", file_name(console)))).unwrap();
            let before: Vec<f32> = bytes.chunks_exact(4).map(|b| f32::from_le_bytes([b[0], b[1], b[2], b[3]])).collect();
            assert_eq!(before.len(), r.samples.len());
            let (mut worst, mut at, mut differ, mut peak) = (0.0f32, 0usize, 0usize, 0.0f32);
            for (i, (a, b)) in before.iter().zip(&r.samples).enumerate() {
                peak = peak.max(a.abs());
                if a.to_bits() != b.to_bits() {
                    differ += 1;
                    if (a - b).abs() > worst {
                        worst = (a - b).abs();
                        at = i;
                    }
                }
            }
            println!(
                "{}: {} of {} samples differ, largest difference {:e} ({:.1} dB below the peak) at sample {}",
                console.name(),
                differ,
                before.len(),
                worst,
                if worst > 0.0 { 20.0 * (worst / peak).log10() } else { f32::NEG_INFINITY },
                at
            );
        }
        let want = reference.lines().find(|l| l.starts_with(&format!("{} ", file_name(console))));
        if want != Some(now.as_str()) {
            failures.push(console.name());
        }
        fresh.push_str(&now);
        fresh.push('\n');
        // The same console on worker threads: what the tasks add up to doesn't depend on who ran them.
        let threaded = render(console, 3);
        assert_eq!(threaded.hash, r.hash, "{} with worker threads", console.name());
    }
    if std::env::var_os("NULL_UPDATE").is_some() {
        std::fs::write(concat!(env!("CARGO_MANIFEST_DIR"), "/tests/null_reference.txt"), &fresh).unwrap();
        return;
    }
    assert!(failures.is_empty(), "the output of {failures:?} is not what the reference says (see NULL_COMPARE in this file)");
}
