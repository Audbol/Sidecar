//! Dyn8, the RackUltra strip and models: the tables and one behavioural check at
//! least per model that it does its job.

use super::super::*;
use super::strip::{UltraStrip, STRIP};
use crate::model::{model_index, ModelTables};
use crate::wing::fx::{allowed_in, FxContext, FxDevice};
use crate::wing::MAX_FX_PARAMS;
use std::f32::consts::TAU;

const SR: f32 = 48_000.0;

type Values = [f32; MAX_FX_PARAMS];

fn device(token: &str, set: &[(&str, f32)]) -> (FxDevice, Values) {
    let model = model_index(&FX_MODELS, token).unwrap_or_else(|| panic!("{token}: not in the table"));
    let spec = &FX_MODELS[model];
    let mut v = [0.0; MAX_FX_PARAMS];
    spec.defaults_into(&mut v);
    for (id, x) in set {
        let i = spec.param_index(id).unwrap_or_else(|| panic!("{token}: no {id}"));
        v[i] = *x;
    }
    (FxDevice::new_in(&FX_MODELS, model, FxContext { sample_rate: SR, max_block: 256 }).unwrap(), v)
}

fn samples(s: f32) -> usize {
    (s * SR) as usize
}

fn sine(hz: f32, secs: f32, amp: f32) -> Vec<f32> {
    (0..samples(secs)).map(|i| amp * (TAU * hz * i as f32 / SR).sin()).collect()
}

/// A voice-like tone: a fundamental and a few harmonics.
fn voice(hz: f32, secs: f32, amp: f32) -> Vec<f32> {
    (0..samples(secs))
        .map(|i| {
            let t = TAU * hz * i as f32 / SR;
            amp * (t.sin() + 0.5 * (2.0 * t).sin() + 0.3 * (3.0 * t).sin() + 0.2 * (4.0 * t).sin()) / 1.5
        })
        .collect()
}

fn render(dev: &mut FxDevice, v: &Values, l: &mut [f32], r: &mut [f32]) {
    for (cl, cr) in l.chunks_mut(256).zip(r.chunks_mut(256)) {
        dev.process(v, cl, cr);
    }
}

fn run(token: &str, set: &[(&str, f32)], input: &[f32]) -> (Vec<f32>, Vec<f32>) {
    let (mut dev, v) = device(token, set);
    let (mut l, mut r) = (input.to_vec(), input.to_vec());
    render(&mut dev, &v, &mut l, &mut r);
    (l, r)
}

fn rms(x: &[f32]) -> f32 {
    (x.iter().map(|v| v * v).sum::<f32>() / x.len().max(1) as f32).sqrt()
}

fn tail(x: &[f32]) -> &[f32] {
    &x[x.len() / 2..]
}

fn peak_at(x: &[f32], at: usize, win: usize) -> f32 {
    x[at.saturating_sub(win)..(at + win).min(x.len())].iter().fold(0.0, |a: f32, v| a.max(v.abs()))
}

/// Fundamental (Hz) from the first strong peak of the autocorrelation over the
/// last half of `x`, interpolated.
fn pitch(x: &[f32]) -> f32 {
    let x = tail(x);
    let n = x.len().min(9600);
    let x = &x[x.len() - n..];
    let hi = 800usize;
    let corr = |lag: usize| -> f32 { x[..n - hi].iter().zip(&x[lag..]).map(|(a, b)| a * b).sum() };
    let c: Vec<f32> = (0..hi + 2).map(corr).collect();
    let top = c[24..hi].iter().cloned().fold(f32::MIN, f32::max);
    let lag = (24..hi).find(|&l| c[l] >= 0.85 * top && c[l] >= c[l - 1] && c[l] >= c[l + 1]).unwrap_or(24);
    let (a, b, g) = (c[lag - 1], c[lag], c[lag + 1]);
    let d = 0.5 * (a - g) / (a - 2.0 * b + g).min(-1e-9);
    SR / (lag as f32 + d)
}

/// Level of `hz` in `x` (Goertzel, linear).
fn tone(x: &[f32], hz: f32) -> f32 {
    let w = TAU * hz / SR;
    let (mut s1, mut s2) = (0.0f32, 0.0f32);
    for &v in x {
        let s = v + 2.0 * w.cos() * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    2.0 * (s1 * s1 + s2 * s2 - 2.0 * w.cos() * s1 * s2).sqrt() / x.len() as f32
}

fn near(got: f32, want: f32, tol: f32) -> bool {
    (got / want - 1.0).abs() < tol
}

const HOT: f32 = 0.5;

// ---- tables ------------------------------------------------------------------------------

#[test]
fn every_new_model_is_offered_on_a_universal_and_named_dlive() {
    use crate::universal::{console_of, FX_MODELS as UNIVERSAL};
    for t in super::TOKENS.iter().copied().chain(["DLVDYN8"]) {
        assert!(model_index(&UNIVERSAL, t).is_some(), "{t}");
        assert_eq!(console_of(t), Some("dLive"), "{t}");
        assert!(FX_MODELS.iter().any(|m| m.token == t), "{t}");
    }
    assert_eq!(FX_MODELS.len(), 38);
    assert_eq!(STRIP.params.len(), super::strip::len());
}

fn at(models: &ModelTables, token: &str) -> usize {
    model_index(models.fx, token).unwrap()
}

#[test]
fn a_dlive_engine_holds_only_its_own_and_a_universal_any() {
    use crate::{dlive, universal};
    let d = &dlive::MODELS;
    for (token, slots, out) in [("DLVTAP", RACKEXTRA_SLOTS, 16), ("DLVSPACE", ULTRA_SLOTS, 15), ("DLVDYN8", DYN8_SLOTS, 15)] {
        assert!(allowed_in(d, at(d, token), slots.start), "{token}");
        assert!(allowed_in(d, at(d, token), slots.end - 1), "{token}");
        assert!(!allowed_in(d, at(d, token), out), "{token} not on slot {out}");
    }
    assert!(!allowed_in(d, at(d, "DLVSPACE"), 24), "no 25th RackUltra engine");
    assert!(!allowed_in(d, at(d, "DLVDYN8"), 24) && !allowed_in(d, at(d, "DLVTAP"), 64), "Dyn8s are no FX slots");
    let u = &universal::MODELS;
    assert!(allowed_in(u, at(u, "DLVSPACE"), 0) && allowed_in(u, at(u, "DLVSPACE"), 7));
    assert!(allowed_in(u, at(u, "DLVDYN8"), 0) && allowed_in(u, at(u, "DLVDYN8"), 23), "any FX slot");
    assert!(allowed_in(u, at(u, "HALL"), 7), "the WING's unaffected");
}

// ---- Dyn8 --------------------------------------------------------------------------------

/// The multiband idle (nothing near its thresholds) and the EQ flat.
const DYN8_IDLE: [(&str, f32); 8] =
    [("mt_1", 18.0), ("mt_2", 18.0), ("mt_3", 18.0), ("mt_4", 18.0), ("eg_1", 0.0), ("eg_2", 0.0), ("eg_3", 0.0), ("eg_4", 0.0)];

fn dyn8_gain(set: &[(&str, f32)], hz: f32, amp: f32) -> f32 {
    let x = sine(hz, 0.6, amp);
    let all: Vec<(&str, f32)> = DYN8_IDLE.iter().chain(set).copied().collect();
    let (l, _) = run("DLVDYN8", &all, &x);
    20.0 * (rms(tail(&l)) / rms(tail(&x))).log10()
}

#[test]
fn dyn8_is_transparent_while_idle_at_every_slope() {
    for slope in [0.0, 1.0, 2.0] {
        let mut set: Vec<(&str, f32)> = DYN8_IDLE.to_vec();
        set.push(("slope", slope));
        for hz in [60.0, 300.0, 2000.0, 9000.0] {
            let g = dyn8_gain(&set, hz, 0.1);
            assert!(g.abs() < 0.8, "slope {slope}, {hz} Hz: {g} dB");
        }
    }
}

#[test]
fn dyn8_multiband_compresses_only_its_band() {
    let set: Vec<(&str, f32)> =
        DYN8_IDLE.iter().chain(&[("mt_1", -40.0), ("mr_1", 20.0), ("ball", 0.0), ("ma_1", 1.0)]).copied().collect();
    let (mut dev, v) = device("DLVDYN8", &set);
    let (mut l, mut r) = (sine(80.0, 0.6, HOT), sine(80.0, 0.6, HOT));
    render(&mut dev, &v, &mut l, &mut r);
    assert!(dev.meter() < -3.0, "{}", dev.meter());
    let low = 20.0 * (rms(tail(&l)) / rms(tail(&sine(80.0, 0.6, HOT)))).log10();
    assert!(low < -8.0, "{low}");
    // Its band alone: the same settings leave the highs at unity.
    let high = dyn8_gain(&[("mt_1", -40.0), ("mr_1", 20.0)], 6000.0, HOT);
    assert!(high.abs() < 1.5, "{high}");
}

#[test]
fn dyn8_ballistics_and_gain_are_per_band() {
    let gain = dyn8_gain(&[("mt_1", -40.0), ("mg_1", 6.0), ("mr_1", 1.0)], 80.0, 0.1);
    assert!((gain - 6.0).abs() < 1.0, "makeup gain: {gain}");
    let off = dyn8_gain(&[("mt_1", -40.0), ("mi_1", 0.0)], 80.0, HOT);
    assert!(off.abs() < 1.0, "band out: {off}");
}

#[test]
fn dyn8_link_makes_every_band_follow_the_first() {
    let set: [(&str, f32); 3] = [("mt_1", -40.0), ("mr_1", 20.0), ("mt_2", 18.0)];
    let free = dyn8_gain(&set, 600.0, HOT);
    let mut linked = set.to_vec();
    linked.push(("link", 1.0));
    let tied = dyn8_gain(&linked, 600.0, HOT);
    assert!(free.abs() < 2.0, "band 2 idle on its own: {free}");
    assert!(tied < -6.0, "linked: {tied}");
}

#[test]
fn dyn8_dynamic_eq_moves_by_at_most_its_gain_above_or_below() {
    // Above: cuts a loud 500 Hz, not a quiet one.
    let loud = dyn8_gain(&[("eg_2", -9.0), ("et_2", -30.0), ("ew_2", 0.5)], 500.0, HOT);
    let quiet = dyn8_gain(&[("eg_2", -9.0), ("et_2", -30.0), ("ew_2", 0.5)], 500.0, 0.002);
    assert!((-9.5..-6.0).contains(&loud), "{loud}");
    assert!(quiet.abs() < 1.0, "{quiet}");
    // Below: boosts a quiet one, not a loud one.
    let boosted = dyn8_gain(&[("eg_2", 9.0), ("et_2", -20.0), ("em_2", 2.0), ("ew_2", 0.5)], 500.0, 0.002);
    let left = dyn8_gain(&[("eg_2", 9.0), ("et_2", -20.0), ("em_2", 2.0), ("ew_2", 0.5)], 500.0, HOT);
    assert!(boosted > 6.0 && boosted <= 9.5, "{boosted}");
    assert!(left.abs() < 1.0, "{left}");
    // Out, the band does nothing.
    let band_out = dyn8_gain(&[("eg_2", -12.0), ("et_2", -40.0), ("ei_2", 0.0)], 500.0, HOT);
    assert!(band_out.abs() < 1.0, "{band_out}");
}

#[test]
fn dyn8_end_bands_become_shelves_at_their_widest() {
    let at_width = |w: f32, hz: f32| dyn8_gain(&[("eg_4", -12.0), ("et_4", -40.0), ("ew_4", w), ("ef_4", 8000.0)], hz, HOT);
    // Bell at 8 kHz, narrow: the far top is back up; widest: a shelf, the top cut too.
    assert!(at_width(0.5, 18000.0) > at_width(2.1, 18000.0) + 3.0);
}

#[test]
fn dyn8_external_key_drives_a_band_and_split_or_wide_set_the_sides() {
    let keyed_run = |set: &[(&str, f32)], left: &[f32], right: &[f32], key: Option<(&[f32], &[f32])>| -> (Vec<f32>, Vec<f32>) {
        let (mut dev, v) = device("DLVDYN8", set);
        let (mut l, mut r) = (left.to_vec(), right.to_vec());
        for i in (0..l.len()).step_by(256) {
            let e = (i + 256).min(l.len());
            let (lc, rc) = (&mut l[i..e], &mut r[i..e]);
            dev.process_keyed(&v, lc, rc, key.map(|(a, b)| (&a[i..e], &b[i..e])));
        }
        (l, r)
    };
    let quiet = sine(80.0, 0.6, 0.02);
    let loud = sine(80.0, 0.6, HOT);
    let silent = vec![0.0; quiet.len()];
    let ext: [(&str, f32); 3] = [("mt_1", -30.0), ("mr_1", 20.0), ("mk_1", 2.0)];
    let on_its_own = keyed_run(&ext, &quiet, &quiet, Some((&silent, &silent)));
    let keyed = keyed_run(&ext, &quiet, &quiet, Some((&loud, &loud)));
    let no_key = keyed_run(&[("mt_1", -30.0), ("mr_1", 20.0)], &quiet, &quiet, None);
    let self_keyed = keyed_run(&[("mt_1", -30.0), ("mr_1", 20.0)], &quiet, &quiet, Some((&loud, &loud)));
    assert!(rms(tail(&keyed.0)) < rms(tail(&on_its_own.0)) * 0.5, "a loud key pulls the quiet band down");
    assert!(near(rms(tail(&self_keyed.0)), rms(tail(&no_key.0)), 0.02), "Self ignores the key");

    // Split: the loud side drives both; Wide: each side its own.
    let split = keyed_run(&[("mt_1", -30.0), ("mr_1", 20.0)], &loud, &quiet, None);
    let wide = keyed_run(&[("mt_1", -30.0), ("mr_1", 20.0), ("mk_1", 1.0)], &loud, &quiet, None);
    let alone = {
        let y = keyed_run(&[("mt_1", -30.0), ("mr_1", 20.0)], &quiet, &quiet, None).1;
        20.0 * (rms(tail(&y)) / rms(tail(&quiet))).log10()
    };
    assert!(near(10f32.powf(alone / 20.0) * rms(tail(&quiet)), rms(tail(&wide.1)), 0.05), "Wide: the quiet side is left alone");
    assert!(rms(tail(&split.1)) < rms(tail(&wide.1)) * 0.5, "Split: the loud side pulls the quiet one down");
}

// ---- the strip --------------------------------------------------------------------------

fn strip_values() -> Vec<f32> {
    let mut v = vec![0.0; STRIP.params.len()];
    STRIP.defaults_into(&mut v);
    v
}

fn set_strip(v: &mut [f32], id: &str, x: f32) {
    let i = STRIP.param_index(id).unwrap_or_else(|| panic!("no {id}"));
    v[i] = x;
}

fn strip_run(
    v: &[f32],
    input: &[f32],
    key: Option<(&[f32], &[f32])>,
    device: &mut dyn FnMut(&mut [f32], &mut [f32]),
) -> (Vec<f32>, Vec<f32>) {
    let mut s = UltraStrip::new(SR, 256);
    let (mut l, mut r) = (input.to_vec(), input.to_vec());
    for i in (0..l.len()).step_by(256) {
        let e = (i + 256).min(l.len());
        let k = key.map(|(a, b)| (&a[i..e], &b[i..e]));
        s.process(v, &mut l[i..e], &mut r[i..e], k, device);
    }
    (l, r)
}

#[test]
fn the_strip_is_transparent_when_everything_is_out() {
    let input = voice(150.0, 0.3, 0.4);
    let (l, _) = strip_run(&strip_values(), &input, None, &mut |_, _| {});
    assert!(input.iter().zip(&l).all(|(a, b)| (a - b).abs() < 1e-3), "not transparent");
}

#[test]
fn the_strip_runs_peq_then_dynamics_then_the_fx_then_dynamics_then_peq() {
    let input = sine(100.0, 0.3, 0.05);
    let mut v = strip_values();
    set_strip(&mut v, "a_in", 1.0);
    set_strip(&mut v, "a_lf", 100.0);
    set_strip(&mut v, "a_lg", 12.0);
    set_strip(&mut v, "a_lm", 1.0);
    set_strip(&mut v, "a_lq", 1.0);
    let mut seen = 0.0f32;
    let (l, _) = strip_run(&v, &input, None, &mut |a, _| {
        seen = seen.max(a.iter().fold(0.0f32, |m, x| m.max(x.abs())));
    });
    assert!(near(seen, 0.05 * 10f32.powf(12.0 / 20.0), 0.12), "the FX sees the first PEQ's boost: {seen}");
    assert!(near(rms(tail(&l)), rms(tail(&input)) * 10f32.powf(12.0 / 20.0), 0.12), "and it comes out");

    // The post dynamics act on the FX's output.
    let mut v = strip_values();
    set_strip(&mut v, "c_on", 1.0);
    set_strip(&mut v, "c_ty", 2.0);
    set_strip(&mut v, "c_th", -60.0);
    set_strip(&mut v, "c_ra", 20.0);
    let (l, _) = strip_run(&v, &sine(500.0, 0.4, HOT), None, &mut |_, _| {});
    assert!(rms(tail(&l)) < 0.12, "{}", rms(tail(&l)));
}

#[test]
fn the_strip_dynamics_key_from_self_external_or_fx() {
    let quiet = sine(400.0, 0.5, 0.02);
    let loud = sine(400.0, 0.5, HOT);
    // A Ducker on external: a loud key ducks a quiet signal.
    let mut v = strip_values();
    set_strip(&mut v, "b_on", 1.0);
    set_strip(&mut v, "b_ty", 3.0);
    set_strip(&mut v, "b_ky", 1.0);
    set_strip(&mut v, "b_th", -10.0);
    set_strip(&mut v, "b_de", 24.0);
    set_strip(&mut v, "b_at", 1.0);
    let (ducked, _) = strip_run(&v, &quiet, Some((&loud, &loud)), &mut |_, _| {});
    assert!(rms(tail(&ducked)) < rms(tail(&quiet)) * 0.12, "{}", rms(tail(&ducked)) / rms(tail(&quiet)));
    let silence = vec![0.0; quiet.len()];
    let (open, _) = strip_run(&v, &quiet, Some((&silence, &silence)), &mut |_, _| {});
    assert!(near(rms(tail(&open)), rms(tail(&quiet)), 0.05), "no key, no duck");

    // The second section's key from the FX's input: the FX swallows the signal,
    // the section keeps a gate open from what went in.
    let mut v = strip_values();
    set_strip(&mut v, "c_on", 1.0);
    set_strip(&mut v, "c_ty", 0.0);
    set_strip(&mut v, "c_ky", 2.0);
    set_strip(&mut v, "c_th", -40.0);
    set_strip(&mut v, "c_de", 60.0);
    let (gated_open, _) = strip_run(&v, &loud, None, &mut |a, b| {
        a.iter_mut().for_each(|x| *x *= 0.001);
        b.iter_mut().for_each(|x| *x *= 0.001);
    });
    assert!(rms(tail(&gated_open)) > 0.0003, "gate open on the FX's input: {}", rms(tail(&gated_open)));
    set_strip(&mut v, "c_ky", 3.0);
    let (gated_shut, _) = strip_run(&v, &loud, None, &mut |a, b| {
        a.iter_mut().for_each(|x| *x *= 0.001);
        b.iter_mut().for_each(|x| *x *= 0.001);
    });
    assert!(rms(tail(&gated_shut)) < 0.5 * rms(tail(&gated_open)), "keyed by what comes out, the gate shuts");
}

#[test]
fn the_strip_width_narrows_to_mono_and_widens() {
    let mut v = strip_values();
    set_strip(&mut v, "width", 0.0);
    let input = sine(800.0, 0.2, 0.3);
    let run_width = |v: &[f32]| {
        let mut s = UltraStrip::new(SR, 256);
        let mut l = input.clone();
        let mut r: Vec<f32> = input.iter().map(|x| -x).collect();
        for (cl, cr) in l.chunks_mut(256).zip(r.chunks_mut(256)) {
            s.process(v, cl, cr, None, &mut |_, _| {});
        }
        l
    };
    let mono = run_width(&v);
    assert!(rms(&mono) < 1e-3, "out of phase cancels to mono");
    set_strip(&mut v, "width", 200.0);
    let wide = run_width(&v);
    assert!(near(rms(&wide), rms(&input) * 2.0, 0.02), "{}", rms(&wide) / rms(&input));
}

// ---- pitch models ------------------------------------------------------------------------

#[test]
fn vocal_shifter_shifts_by_semitones_with_precision() {
    let x = voice(220.0, 0.8, 0.4);
    let (l, r) = run("DLVVSHFT", &[("semi_L", 5.0), ("semi_R", -7.0)], &x);
    assert!(near(pitch(&l), 220.0 * 2f32.powf(5.0 / 12.0), 0.02), "{}", pitch(&l));
    assert!(near(pitch(&r), 220.0 * 2f32.powf(-7.0 / 12.0), 0.02), "{}", pitch(&r));
    // Without Precision the setting rounds to the semitone; with it, it doesn't.
    let (coarse, _) = run("DLVVSHFT", &[("semi_L", 2.4)], &x);
    let (fine, _) = run("DLVVSHFT", &[("semi_L", 2.4), ("prec", 1.0)], &x);
    assert!(near(pitch(&coarse), 220.0 * 2f32.powf(2.0 / 12.0), 0.012), "{}", pitch(&coarse));
    assert!(near(pitch(&fine), 220.0 * 2f32.powf(2.4 / 12.0), 0.012), "{}", pitch(&fine));
    // Link: the right takes the left's settings.
    let (_, linked) = run("DLVVSHFT", &[("semi_L", 7.0), ("semi_R", -5.0), ("link", 1.0)], &x);
    assert!(near(pitch(&linked), 220.0 * 2f32.powf(7.0 / 12.0), 0.02), "{}", pitch(&linked));
}

#[test]
fn vocal_shifter_formant_moves_without_moving_the_pitch() {
    let x = voice(220.0, 0.8, 0.4);
    let (plain, _) = run("DLVVSHFT", &[], &x);
    let (up, _) = run("DLVVSHFT", &[("form_L", 4.0)], &x);
    assert!(near(pitch(&up), 220.0, 0.03), "the formant doesn't move the pitch: {}", pitch(&up));
    // More of the voice sits higher: the 880 Hz partial grows against the fundamental.
    let h4 = |y: &[f32]| tone(tail(y), 880.0) / tone(tail(y), 220.0).max(1e-6);
    assert!(h4(&up) > 1.15 * h4(&plain), "{} vs {}", h4(&up), h4(&plain));
}

#[test]
fn vocal_shifter_delays_and_attenuates() {
    let x = voice(220.0, 0.5, 0.4);
    let (y, _) = run("DLVVSHFT", &[("gain_L", -20.0)], &x);
    let (dry, _) = run("DLVVSHFT", &[], &x);
    assert!(near(rms(tail(&y)), 0.1 * rms(tail(&dry)), 0.05), "{}", rms(tail(&y)) / rms(tail(&dry)));
    // A burst: Delay holds it back by the setting.
    let mut burst = vec![0.0; samples(0.4)];
    let b = voice(220.0, 0.03, 0.4);
    burst[samples(0.1)..samples(0.1) + b.len()].copy_from_slice(&b);
    let (d0, _) = run("DLVVSHFT", &[], &burst);
    let (d80, _) = run("DLVVSHFT", &[("dly_L", 80.0)], &burst);
    let first = |y: &[f32]| y.iter().position(|v| v.abs() > 0.04).unwrap_or(0);
    let moved = first(&d80) as f32 - first(&d0) as f32;
    assert!((moved / SR * 1000.0 - 80.0).abs() < 12.0, "{} ms", moved / SR * 1000.0);
}

#[test]
fn vocal_tuner_pulls_a_detuned_voice_to_the_nearest_note() {
    let x = voice(226.0, 1.0, 0.4); // A3 + 47 cents
    let (y, _) = run("DLVTUNER", &[("retune", 10.0), ("trans", 0.0)], &x);
    assert!(near(pitch(&y), 220.0, 0.008), "{}", pitch(&y));
}

#[test]
fn vocal_tuner_correction_bypass_range_scale_and_note_states() {
    let x = voice(226.0, 1.0, 0.4);
    let (half, _) = run("DLVTUNER", &[("retune", 10.0), ("trans", 0.0), ("corr", 50.0)], &x);
    assert!(near(pitch(&half), 223.0, 0.005), "half correction: {}", pitch(&half));
    let (byp, _) = run("DLVTUNER", &[("byp", 1.0)], &x);
    assert!(near(pitch(&byp), 226.0, 0.006), "bypass: {}", pitch(&byp));
    // Soprano's range starts at A3: 200 Hz (below it) stays.
    let low = voice(200.0, 1.0, 0.4);
    let (out, _) = run("DLVTUNER", &[("range", 3.0), ("retune", 10.0), ("trans", 0.0)], &low);
    assert!(near(pitch(&out), 200.0, 0.006), "out of range: {}", pitch(&out));
    // C major: A# (233 Hz) isn't in it, it goes to A or B.
    let sharp = voice(233.08, 1.0, 0.4);
    let (out, _) = run("DLVTUNER", &[("scale", 0.0), ("retune", 10.0), ("trans", 0.0)], &sharp);
    assert!(near(pitch(&out), 220.0, 0.008) || near(pitch(&out), 246.94, 0.008), "{}", pitch(&out));
    // Note A's state Off: not a target; the voice goes to the note above.
    let (out, _) = run("DLVTUNER", &[("n9", 0.0), ("retune", 10.0), ("trans", 0.0)], &x);
    assert!(near(pitch(&out), 233.08, 0.01), "{}", pitch(&out));
    // Bypass on a note: a voice sung there stays where it is.
    let (out, _) = run("DLVTUNER", &[("n9", 1.0), ("retune", 10.0), ("trans", 0.0)], &x);
    assert!(near(pitch(&out), 226.0, 0.006), "{}", pitch(&out));
}

#[test]
fn vocal_tuner_midi_snap_and_global_key() {
    let (mut dev, v) = device("DLVTUNER", &[("retune", 10.0), ("trans", 0.0)]);
    dev.midi_note(60, 100);
    let x = voice(240.0, 1.0, 0.4);
    let (mut l, mut r) = (x.clone(), x.clone());
    render(&mut dev, &v, &mut l, &mut r);
    assert!(near(pitch(&l), 261.63, 0.008), "MIDI note snap: {}", pitch(&l));
    // Global key: F major holds A#, which C major doesn't.
    let sharp = voice(233.08, 1.0, 0.4);
    let (mut dev, v) = device("DLVTUNER", &[("glob", 1.0), ("scale", 0.0), ("retune", 10.0), ("trans", 0.0)]);
    dev.global_key(5, 0);
    let (mut l, mut r) = (sharp.clone(), sharp.clone());
    render(&mut dev, &v, &mut l, &mut r);
    assert!(near(pitch(&l), 233.08, 0.006), "{}", pitch(&l));
}

#[test]
fn vocal_gridder_snaps_and_waits_out_its_tolerances() {
    let x = voice(226.0, 1.0, 0.4);
    let (y, _) = run("DLVGRID", &[("ttol", 0.0), ("ptol", 0.0), ("scale", 2.0)], &x);
    assert!(near(pitch(&y), 220.0, 0.008), "{}", pitch(&y));
    // A step from A3 to B3: a long Time Tolerance holds the old note, then it moves.
    let mut step = voice(220.0, 0.2, 0.4);
    step.extend(voice(246.94, 1.0, 0.4));
    let set = [("ttol", 500.0), ("ptol", 10.0), ("scale", 2.0)];
    let (y, _) = run("DLVGRID", &set, &step);
    let held = &y[samples(0.3)..samples(0.5)];
    assert!(near(pitch(held), 220.0, 0.02), "held: {}", pitch(held));
    let moved = &y[samples(0.9)..];
    assert!(near(pitch(moved), 246.94, 0.02), "moved: {}", pitch(moved));
}

#[test]
fn harmonisers_add_scale_intervals_to_the_sung_note() {
    // In C major, a third above A3 is C4 (three semitones), an octave A4.
    let x = voice(220.0, 1.0, 0.4);
    let lone = [("gain_2", -40.0), ("rev", 0.0), ("chor", 0.0)];
    let third = [lone[0], lone[1], lone[2], ("int_1", 3.0)];
    let (y, _) = run("DLVH2", &third, &x);
    assert!(near(pitch(&y), 261.63, 0.012), "{}", pitch(&y));
    let oct = [lone[0], lone[1], lone[2], ("int_1", 8.0)];
    let (y, _) = run("DLVH2", &oct, &x);
    assert!(near(pitch(&y), 440.0, 0.012), "{}", pitch(&y));
    // In D major it's a major third, C#.
    let d_major = [lone[0], lone[1], lone[2], ("int_1", 3.0), ("key", 2.0)];
    let (y, _) = run("DLVH2", &d_major, &x);
    assert!(near(pitch(&y), 277.18, 0.012), "{}", pitch(&y));
    // Global key.
    let set = [
        ("gain_1", 0.0),
        ("int_1", 3.0),
        ("gain_2", -40.0),
        ("gain_3", -40.0),
        ("gain_4", -40.0),
        ("rev", 0.0),
        ("chor", 0.0),
        ("ksrc", 1.0),
    ];
    let (mut dev, v) = device("DLVH4", &set);
    dev.global_key(2, 0);
    let (mut l, mut r) = (x.clone(), x.clone());
    render(&mut dev, &v, &mut l, &mut r);
    assert!(near(pitch(&l), 277.18, 0.012), "{}", pitch(&l));
}

#[test]
fn harmoniser_pans_and_auto_expand() {
    let x = voice(220.0, 0.8, 0.4);
    let set = [("int_1", 3.0), ("pan_1", -100.0), ("gain_2", -40.0), ("rev", 0.0), ("chor", 0.0)];
    let (l, r) = run("DLVH2", &set, &x);
    assert!(rms(tail(&r)) < 0.2 * rms(tail(&l)), "{} {}", rms(tail(&l)), rms(tail(&r)));
    // Auto Expand: the harmony fades out under a very quiet singer.
    let quiet = voice(220.0, 0.8, 0.002);
    let (open, _) = run("DLVH2", &[("int_1", 3.0), ("rev", 0.0), ("chor", 0.0), ("exp", 0.0)], &quiet);
    let (shut, _) = run("DLVH2", &[("int_1", 3.0), ("rev", 0.0), ("chor", 0.0), ("exp", 1.0)], &quiet);
    assert!(rms(tail(&shut)) < 0.3 * rms(tail(&open)), "{} {}", rms(tail(&shut)), rms(tail(&open)));
}

#[test]
fn harmoniser_is_stable_after_a_silent_start() {
    // The room in the harmonies has to survive running idle first.
    for token in ["DLVH2", "DLVH4", "DLVTUNER", "DLVGRID", "DLVVSHFT"] {
        let (mut dev, v) = device(token, &[]);
        let silent = vec![0.0; samples(1.0)];
        let (mut a, mut b) = (silent.clone(), silent.clone());
        render(&mut dev, &v, &mut a, &mut b);
        let (mut l, mut r) = (voice(220.0, 0.6, 0.4), voice(220.0, 0.6, 0.4));
        render(&mut dev, &v, &mut l, &mut r);
        assert!(l.iter().chain(&r).all(|x| x.is_finite() && x.abs() < 4.0), "{token}");
    }
}

#[test]
fn dual_autokey_finds_the_key_from_a_side_chain() {
    let (mut dev, v) = device("DLVH2", &[("ksrc", 2.0)]);
    let mut key: Vec<f32> = Vec::new();
    for hz in [261.63, 293.66, 329.63, 349.23, 392.0, 440.0, 493.88, 261.63] {
        key.extend(voice(hz, 0.35, 0.4));
    }
    for _ in 0..3 {
        let k = key.clone();
        let (mut l, mut r) = (vec![0.0; k.len()], vec![0.0; k.len()]);
        let mut i = 0;
        while i < l.len() {
            let e = (i + 256).min(l.len());
            let (kl, kr) = (&k[i..e], &k[i..e]);
            dev.process_keyed(&v, &mut l[i..e], &mut r[i..e], Some((kl, kr)));
            i = e;
        }
    }
    let mut out = [0.0; crate::wing::fx::FX_READOUTS];
    dev.readouts(&mut out);
    // C major (or its relative, A minor) with some confidence.
    let (key, scale) = (out[1] as i32 - 1, out[2] as i32);
    assert!((key == 0 && scale == 0) || (key == 9 && scale == 1), "key {key} scale {scale}");
    assert!(out[0] > 0.0, "{}", out[0]);
}

#[test]
fn midi_harmoniser_plays_the_notes_held() {
    let x = voice(220.0, 1.0, 0.4);
    let (mut dev, v) = device("DLVHMIDI", &[("chor", 0.0), ("gain", 0.0)]);
    let (mut l, mut r) = (x.clone(), x.clone());
    render(&mut dev, &v, &mut l, &mut r);
    assert!(rms(tail(&l)) < 1e-3, "no notes, no voices");

    dev.midi_note(64, 100); // E4
    let (mut l, mut r) = (x.clone(), x.clone());
    render(&mut dev, &v, &mut l, &mut r);
    assert!(near(pitch(&l), 329.63, 0.015), "{}", pitch(&l));
    dev.midi_note(64, 0);
    let (mut l, mut r) = (x.clone(), x.clone());
    render(&mut dev, &v, &mut l, &mut r);
    assert!(rms(tail(&l)) < 1e-3, "released");

    // Polyphonic: two notes held, two pitches.
    let (mut dev, v) = device("DLVHMIDI", &[("mode", 1.0), ("chor", 0.0), ("gain", 0.0)]);
    let (mut pad_l, mut pad_r) = (vec![0.0; 256], vec![0.0; 256]);
    dev.process(&v, &mut pad_l, &mut pad_r); // the Polyphonic setting first
    dev.midi_note(60, 100);
    dev.midi_note(64, 100);
    let (mut l, mut r) = (x.clone(), x);
    render(&mut dev, &v, &mut l, &mut r);
    assert!(
        tone(tail(&l), 261.63) > 0.05 && tone(tail(&l), 329.63) > 0.05,
        "{} {}",
        tone(tail(&l), 261.63),
        tone(tail(&l), 329.63)
    );
}

// ---- reverbs -------------------------------------------------------------------------------

fn impulse(token: &str, set: &[(&str, f32)], secs: f32) -> (Vec<f32>, Vec<f32>) {
    let (mut dev, v) = device(token, set);
    let n = samples(secs);
    let (mut l, mut r) = (vec![0.0; n], vec![0.0; n]);
    l[0] = 1.0;
    r[0] = 1.0;
    render(&mut dev, &v, &mut l, &mut r);
    (l, r)
}

fn rt60(x: &[f32]) -> f32 {
    let mut edc = vec![0.0f64; x.len()];
    let mut acc = 0.0f64;
    for i in (0..x.len()).rev() {
        acc += f64::from(x[i]).powi(2);
        edc[i] = acc;
    }
    let db = |i: usize| 10.0 * (edc[i] / edc[0]).log10();
    let t5 = (0..x.len()).find(|&i| db(i) < -5.0).unwrap();
    let t35 = (0..x.len()).find(|&i| db(i) < -35.0).unwrap();
    2.0 * (t35 - t5) as f32 / SR
}

#[test]
fn spaces_decay_follows_its_control_and_echoes_land_on_their_side() {
    let (short, _) = impulse("DLVSPACE", &[("decay", 1.0), ("early", -60.0), ("sub", -60.0)], 3.0);
    let (long, _) = impulse("DLVSPACE", &[("decay", 4.0), ("early", -60.0), ("sub", -60.0)], 10.0);
    let (a, b) = (rt60(&short), rt60(&long));
    assert!(a > 0.5 && a < 2.0, "{a}");
    assert!(b > a * 2.0 && b < 7.0, "{b}");
    // Echo 1 on at 40 ms, everything else silent: a spike on the left only.
    let (l, r) = impulse(
        "DLVSPACE",
        &[("eo1", 1.0), ("et1", 40.0), ("eg1", 0.0), ("early", -60.0), ("late", -60.0), ("sub", -60.0), ("direct", -60.0)],
        0.2,
    );
    let at = (0.040 * SR) as usize;
    assert!(peak_at(&l, at, 8) > 0.3, "{}", peak_at(&l, at, 8));
    assert!(peak_at(&r, at, 8) < 0.1, "{}", peak_at(&r, at, 8));
}

#[test]
fn spaces_levels_direct_sub_and_output_eq_act() {
    let noise: Vec<f32> = (0..samples(0.2)).map(|i| ((i * 7919) % 2003) as f32 / 2003.0 - 0.5).collect();
    let energy = |set: &[(&str, f32)], x: &[f32]| rms(&run("DLVSPACE", set, x).0);
    let d = energy(&[("late", -60.0), ("early", -60.0), ("sub", -60.0), ("direct", 0.0)], &noise);
    let quiet = energy(&[("late", -60.0), ("early", -60.0), ("sub", -60.0), ("direct", -60.0)], &noise);
    assert!(d > 5.0 * quiet.max(1e-6), "{d} {quiet}");
    // The Sub level adds its low end: below 80 Hz.
    let lo = sine(50.0, 1.0, 0.3);
    let plain = tone(tail(&run("DLVSPACE", &[("sub", -60.0)], &lo).0), 50.0);
    let sub = tone(tail(&run("DLVSPACE", &[("sub", 6.0)], &lo).0), 50.0);
    assert!(sub > plain * 1.2, "{plain} {sub}");
    let hi_gone = tone(tail(&run("DLVSPACE", &[("hc", 1000.0)], &sine(6000.0, 1.0, 0.3)).0), 6000.0);
    let hi_open = tone(tail(&run("DLVSPACE", &[("hc", 20000.0)], &sine(6000.0, 1.0, 0.3)).0), 6000.0);
    assert!(hi_gone < hi_open * 0.5, "{hi_gone} {hi_open}");
}

#[test]
fn plate_types_decay_and_echoes() {
    let (short, _) = impulse("DLVPLATE", &[("decay", 0.5)], 2.0);
    let (long, _) = impulse("DLVPLATE", &[("decay", 4.0)], 8.0);
    assert!(rt60(&long) > rt60(&short) * 2.5, "{} {}", rt60(&long), rt60(&short));
    // Dark is darker than Bright.
    let hf = |ty: f32| {
        let (l, _) = impulse("DLVPLATE", &[("type", ty), ("decay", 2.0)], 2.0);
        // The energy above 5 kHz against the whole.
        let mut hp = crate::biquad::Biquad::default();
        hp.set_coefficients(crate::biquad::BiquadCoeffs::high_pass(f64::from(SR), 5000.0, 0.707));
        let high: Vec<f32> = l.iter().map(|v| hp.tick(*v, 0)).collect();
        rms(&high) / rms(&l).max(1e-6)
    };
    assert!(hf(2.0) > 1.5 * hf(3.0), "Bright {} Dark {}", hf(2.0), hf(3.0));
    let (ol, _) = impulse("DLVPLATE", &[("eo1", 1.0), ("et1", 120.0), ("eg1", 0.0), ("width", 0.0)], 0.3);
    let at = (0.120 * SR) as usize;
    assert!(peak_at(&ol, at, 16) > rms(&ol) * 2.0, "the echo stands out");
}

// ---- rhythm delay --------------------------------------------------------------------------

const RD_OPEN: [(&str, f32); 4] = [("bpm", 120.0), ("fb", 0.0), ("hc", 20000.0), ("lc", 20.0)];

fn rd(extra: &[(&str, f32)], secs: f32) -> (Vec<f32>, Vec<f32>) {
    let set: Vec<(&str, f32)> = RD_OPEN.iter().chain(extra.iter()).copied().collect();
    impulse("DLVRDLY", &set, secs)
}

#[test]
fn rhythm_delay_repeats_on_its_grid_and_feeds_back() {
    let (l, _) = rd(&[("int", 3.0), ("reps", 4.0), ("shape", 0.0)], 1.5);
    let step = samples(0.25);
    for k in 1..=4 {
        assert!(peak_at(&l, step * k, 4) > 0.6, "repeat {k}: {}", peak_at(&l, step * k, 4));
    }
    assert!(peak_at(&l, step * 5, 4) < 0.05, "no fifth without feedback");
    // Dotted: the grid stretches by half.
    let (d, _) = rd(&[("int", 3.0), ("reps", 1.0), ("nm", 1.0)], 0.6);
    assert!(peak_at(&d, samples(0.375), 4) > 0.6);
    // Feedback goes around.
    let (fb, _) = rd(&[("int", 3.0), ("reps", 2.0), ("shape", 0.0), ("fb", 70.0)], 2.5);
    assert!(peak_at(&fb, step * 4, 16) > 0.2, "{}", peak_at(&fb, step * 4, 16));
}

#[test]
fn rhythm_delay_shapes_and_advanced_page() {
    let (flat, _) = rd(&[("int", 3.0), ("reps", 4.0), ("shape", 0.0)], 1.5);
    let (fall, _) = rd(&[("int", 3.0), ("reps", 4.0), ("shape", 1.0), ("amt", 100.0)], 1.5);
    let step = samples(0.25);
    assert!(peak_at(&fall, step * 4, 4) < 0.2 * peak_at(&flat, step * 4, 4), "fades away");
    // Advanced: the third repeat at 25 % of a 4-quarter bar (0.5 s); the others down.
    let mut set: Vec<(&str, f32)> = vec![("page", 1.0), ("bar", 4.0), ("t_3", 25.0), ("n_3", -100.0)];
    for id in ["g_1", "g_2", "g_4", "g_5", "g_6", "g_7", "g_8"] {
        set.push((id, -40.0));
    }
    let (l, r) = rd(&set, 1.0);
    let at = samples(0.5);
    assert!(peak_at(&l, at, 4) > 0.5, "{}", peak_at(&l, at, 4));
    assert!(peak_at(&r, at, 4) < 0.1, "panned hard left: {}", peak_at(&r, at, 4));
}

#[test]
fn rhythm_delay_pattern_tap_places_repeats() {
    let token = "DLVRDLY";
    let spec = &FX_MODELS[model_index(&FX_MODELS, token).unwrap()];
    let mut set: Vec<(&str, f32)> = RD_OPEN.to_vec();
    set.extend([("page", 1.0), ("bar", 4.0)]);
    for id in ["g_2", "g_3", "g_4", "g_5", "g_6", "g_7", "g_8"] {
        set.push((id, -40.0));
    }
    let (mut dev, mut v) = device(token, &set);
    let tap = spec.param_index("ptap").unwrap();
    let silent = vec![0.0; 256];
    let mut advance = |v: &Values, blocks: usize| {
        for _ in 0..blocks {
            let (mut l, mut r) = (silent.clone(), silent.clone());
            dev.process(v, &mut l, &mut r);
        }
    };
    // Tap, then again a quarter of the bar (0.5 s) later, each with a block between.
    v[tap] = 1.0;
    advance(&v, 1);
    v[tap] = 0.0;
    advance(&v, 1);
    advance(&v, samples(0.5) / 256 - 2);
    v[tap] = 1.0;
    advance(&v, 1);
    v[tap] = 0.0;
    advance(&v, 1);
    let n = samples(1.0);
    let (mut l, mut r) = (vec![0.0; n], vec![0.0; n]);
    l[0] = 1.0;
    r[0] = 1.0;
    render(&mut dev, &v, &mut l, &mut r);
    // The two taps put repeat 1 a quarter of the bar along.
    assert!(peak_at(&l, samples(0.5), 512) > 0.5, "{}", peak_at(&l, samples(0.5), 512));
}

// ---- tone --------------------------------------------------------------------------------

#[test]
fn saturator_modes_differ_bias_and_pad_act() {
    let x = sine(200.0, 0.3, HOT);
    let thd = |set: &[(&str, f32)]| {
        let y = run("DLVSAT", set, &x).0;
        let t = tail(&y);
        (tone(t, 400.0) / tone(t, 200.0), tone(t, 600.0) / tone(t, 200.0))
    };
    let (a2, _) = thd(&[("mode", 0.0)]);
    let (b2, b3) = thd(&[("mode", 1.0)]);
    assert!(a2 > 0.01, "triode: even harmonics {a2}");
    assert!(b3 > 0.01, "pentode: odd {b3}");
    assert!(a2 > 2.0 * b2, "triode more even than pentode: {a2} {b2}");
    // Bias pushes it off centre: more even harmonics.
    let (biased, _) = thd(&[("mode", 1.0), ("bias", 60.0)]);
    assert!(biased > 2.0 * b2, "{biased} {b2}");
    // PAD: -30 dB in, +30 dB out: the same level, far less distortion.
    let (_, pad3) = thd(&[("mode", 1.0), ("pad", 1.0)]);
    assert!(pad3 < 0.1 * b3, "{pad3} {b3}");
    let padded = rms(tail(&run("DLVSAT", &[("mode", 1.0), ("pad", 1.0)], &x).0));
    let driven = rms(tail(&run("DLVSAT", &[("mode", 1.0)], &x).0));
    assert!(near(padded, driven, 0.3), "{padded} {driven}");
}

#[test]
fn saturator_filters_and_level() {
    let lo = sine(50.0, 0.3, 0.02);
    let (cut, _) = run("DLVSAT", &[("lc", 800.0)], &lo);
    let (open, _) = run("DLVSAT", &[("lc", 20.0)], &lo);
    assert!(rms(tail(&cut)) < 0.2 * rms(tail(&open)), "{} {}", rms(tail(&cut)), rms(tail(&open)));
    let hi = sine(8000.0, 0.3, 0.02);
    let (dull, _) = run("DLVSAT", &[("hc", 1500.0)], &hi);
    let (airy, _) = run("DLVSAT", &[("hc", 24000.0)], &hi);
    assert!(rms(tail(&dull)) < 0.3 * rms(tail(&airy)), "{} {}", rms(tail(&dull)), rms(tail(&airy)));
    let (down, _) = run("DLVSAT", &[("level", -20.0)], &hi);
    assert!(near(rms(tail(&down)), rms(tail(&airy)) * 0.1, 0.05));
    // Tilt +20 dB lifts the highs and cuts the lows around 1 kHz.
    let (tilt_hi, _) = run("DLVSAT", &[("tilt", 20.0)], &hi);
    let (tilt_lo, _) = run("DLVSAT", &[("tilt", 20.0)], &lo);
    assert!(rms(tail(&tilt_hi)) > 3.0 * rms(tail(&airy)), "{} {}", rms(tail(&tilt_hi)), rms(tail(&airy)));
    assert!(rms(tail(&tilt_lo)) < 0.5 * rms(tail(&open)));
}

#[test]
fn amp_cab_models_and_cabinet_shape_the_tone() {
    let x = sine(200.0, 0.4, 0.3);
    let t = |set: &[(&str, f32)]| run("DLVAMP", set, &x).0;
    // Each amp runs; two voicings sound different.
    let a = t(&[("amp", 0.0), ("cab", 0.0), ("gain", 7.0)]);
    let b = t(&[("amp", 2.0), ("cab", 0.0), ("gain", 7.0)]);
    let diff: f32 = a.iter().zip(&b).map(|(p, q)| (p - q).abs()).sum::<f32>() / a.len() as f32;
    assert!(diff > 0.002, "{diff}");
    for amp in 0..5 {
        let y = t(&[("amp", amp as f32), ("gain", 8.0), ("cab", 1.0)]);
        assert!(rms(tail(&y)) > 1e-3 && rms(tail(&y)) < 2.0, "amp {amp}: {}", rms(tail(&y)));
    }
    // A cabinet rolls the highs off; None leaves them.
    let hi = sine(9000.0, 0.4, 0.05);
    let none = rms(tail(&run("DLVAMP", &[("cab", 0.0)], &hi).0));
    let cab = rms(tail(&run("DLVAMP", &[("cab", 1.0)], &hi).0));
    assert!(cab < 0.5 * none, "{cab} {none}");
    let quiet = rms(tail(&t(&[("out", -12.0)])));
    let loud = rms(tail(&t(&[("out", 0.0)])));
    assert!(near(quiet / loud, 10f32.powf(-12.0 / 20.0), 0.1));
}
