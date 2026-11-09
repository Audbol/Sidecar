//! The RackExtra models: the tables' ranges, the devices' common checks, and one
//! behavioural check (at least) per model that it does its job.

use super::*;
use crate::biquad::{Biquad, BiquadCoeffs};
use crate::model::model_index;
use crate::wing::fx::{FxContext, FxDevice};
use crate::wing::MAX_FX_PARAMS;
use std::f32::consts::TAU;

const SR: f32 = 48_000.0;

type Values = [f32; MAX_FX_PARAMS];

/// A device at 48 kHz on its defaults, `set` applied.
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

/// Runs stereo buffers through in 256-sample blocks.
fn run(dev: &mut FxDevice, v: &Values, l: &mut [f32], r: &mut [f32]) {
    for (cl, cr) in l.chunks_mut(256).zip(r.chunks_mut(256)) {
        dev.process(v, cl, cr);
    }
}

fn samples(s: f32) -> usize {
    (s * SR) as usize
}

/// The response to a unit impulse on both inputs.
fn impulse(token: &str, set: &[(&str, f32)], secs: f32) -> (Vec<f32>, Vec<f32>) {
    let (mut dev, v) = device(token, set);
    let n = samples(secs);
    let (mut l, mut r) = (vec![0.0; n], vec![0.0; n]);
    l[0] = 1.0;
    r[0] = 1.0;
    run(&mut dev, &v, &mut l, &mut r);
    (l, r)
}

fn sine(hz: f32, n: usize, amp: f32) -> Vec<f32> {
    (0..n).map(|i| amp * (TAU * hz * i as f32 / SR).sin()).collect()
}

/// `input` on both channels through the model.
fn render(token: &str, set: &[(&str, f32)], input: &[f32]) -> (Vec<f32>, Vec<f32>) {
    let (mut dev, v) = device(token, set);
    let (mut l, mut r) = (input.to_vec(), input.to_vec());
    run(&mut dev, &v, &mut l, &mut r);
    (l, r)
}

fn rms(x: &[f32]) -> f32 {
    (x.iter().map(|v| v * v).sum::<f32>() / x.len().max(1) as f32).sqrt()
}

fn energy(x: &[f32]) -> f32 {
    x.iter().map(|v| v * v).sum()
}

/// Largest magnitude in `x[from..to]` and its index.
fn peak(x: &[f32], from: usize, to: usize) -> (usize, f32) {
    x[from..to.min(x.len())].iter().enumerate().fold((from, 0.0), |a, (i, v)| if v.abs() > a.1 { (from + i, v.abs()) } else { a })
}

/// Level of `hz` in `x` (Goertzel), linear.
fn tone(x: &[f32], hz: f32) -> f32 {
    let w = TAU * hz / SR;
    let (mut s1, mut s2) = (0.0f32, 0.0f32);
    for &v in x {
        let s = v + 2.0 * w.cos() * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    (s1 * s1 + s2 * s2 - 2.0 * w.cos() * s1 * s2).sqrt() / x.len() as f32
}

/// Frequency of a tone by counting rising zero crossings over `x`.
fn pitch_hz(x: &[f32]) -> f32 {
    let mut crossings = 0usize;
    for w in x.windows(2) {
        if w[0] < 0.0 && w[1] >= 0.0 {
            crossings += 1;
        }
    }
    crossings as f32 * SR / x.len() as f32
}

/// RT60 from the energy decay curve (-5 to -35 dB).
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

/// The band of `x` around `hz`.
fn band(x: &[f32], hz: f32) -> Vec<f32> {
    let mut f = Biquad::default();
    f.set_coefficients(BiquadCoeffs::band_pass(f64::from(SR), f64::from(hz), 1.0));
    x.iter().map(|&v| f.tick(v, 0)).collect()
}

/// RMS over successive `window`-sample chunks of `x`.
fn envelope(x: &[f32], window: usize) -> Vec<f32> {
    x.chunks(window).map(rms).collect()
}

fn spread(levels: &[f32]) -> f32 {
    let (lo, hi) = levels.iter().fold((f32::MAX, 0.0f32), |(a, b), &v| (a.min(v), b.max(v)));
    hi / lo.max(1e-9)
}

// ---- the tables -----------------------------------------------------------------------

#[test]
fn every_model_is_in_the_table_once_and_starts_with_none_and_ext() {
    assert_eq!(FX_MODELS.len(), 26);
    assert_eq!((FX_MODELS[0].token, FX_MODELS[1].token), ("NONE", "EXT"));
    for (i, m) in FX_MODELS.iter().enumerate().skip(2) {
        assert!(is_dlive(m.token), "{}", m.token);
        assert_eq!(model_index(&FX_MODELS, m.token), Some(i), "{} twice", m.token);
        assert_eq!(m.category, ModelCategory::FxPremium, "{}", m.token);
    }
    assert!(!is_dlive("SQSMRH") && !is_dlive("HALL"));
}

#[test]
fn every_models_parameters_are_valid() {
    for m in &FX_MODELS[2..] {
        assert!(!m.params.is_empty() && m.params.len() <= MAX_FX_PARAMS, "{}", m.token);
        let mut ids: Vec<_> = m.params.iter().map(|p| p.id).collect();
        ids.sort_unstable();
        ids.dedup();
        assert_eq!(ids.len(), m.params.len(), "{} repeats an id", m.token);
        for p in m.params {
            let what = format!("{} {}", m.token, p.id);
            assert!(p.min < p.max, "{what}: range");
            assert!((p.lo()..=p.hi()).contains(&p.default), "{what}: default {}", p.default);
            assert_eq!(p.sanitise(p.default), p.default, "{what}: sanitised default");
            assert!(!p.label.is_empty(), "{what}: label");
            match p.kind {
                ParamKind::Log => assert!(p.min > 0.0 && p.steps >= 2, "{what}: log"),
                ParamKind::Linear => assert!(p.steps >= 2, "{what}: steps"),
                ParamKind::Choice(items) => {
                    assert_eq!(p.steps as usize, items.len(), "{what}: steps");
                    assert_eq!(p.max as usize, items.len() - 1, "{what}: max");
                }
                ParamKind::Toggle => assert_eq!((p.min, p.max), (0.0, 1.0), "{what}: toggle"),
                ParamKind::Int | ParamKind::Values(_) => {}
            }
        }
    }
}

#[test]
fn the_ranges_the_guide_states_are_the_ranges() {
    let at = |token: &str, id: &str| {
        let m = &FX_MODELS[model_index(&FX_MODELS, token).unwrap()];
        let s = &m.params[m.param_index(id).unwrap_or_else(|| panic!("{token} {id}"))];
        (s.min, s.max)
    };
    for token in ["DLVSMRC", "DLVSMRH", "DLVSMRR", "DLVSMRE"] {
        assert_eq!(at(token, "lc"), (0.0, 400.0));
        assert_eq!(at(token, "hc"), (2000.0, 20000.0));
    }
    assert_eq!(at("DLVTAP", "time_l"), (1.0, 2700.0));
    assert_eq!(at("DLVBBD", "time"), (1.0, 2700.0));
    assert_eq!(at("DLV2TAP", "time_l"), (5.0, 1360.0));
    assert_eq!(at("DLV2TAP", "lc"), (20.0, 8000.0));
    assert_eq!(at("DLV2TAP", "hc"), (400.0, 20000.0));
    assert_eq!(at("DLVECHO", "in"), (-15.0, 15.0));
    assert_eq!(at("DLVECHO", "vol"), (-15.0, 15.0));
    assert_eq!(at("DLVGVRB", "lc"), (20.0, 6000.0));
    assert_eq!(at("DLVGVRB", "hc"), (400.0, 20000.0));
    assert_eq!(at("DLVGVRB", "pre"), (0.0, 170.0));
    assert_eq!(at("DLVMOO", "rate"), (0.1, 10.0));
    assert_eq!(at("DLVMOO", "mres"), (0.0, 10.0));
    assert_eq!(at("DLVDEES", "f_1"), (3000.0, 8000.0));
    assert_eq!(at("DLVDEES", "r_1"), (0.0, 24.0));
    assert_eq!(at("DLVROT", "drv"), (1.0, 11.0));
    assert_eq!(at("DLVROT", "spd").0, 0.1);
    assert_eq!(at("DLVVS1", "semi_a"), (-12.0, 12.0));
    assert_eq!(at("DLVPDBL", "dly_a"), (0.0, 25.0));
    assert_eq!(at("DLVPDBL", "pit_a"), (-100.0, 100.0));
    assert_eq!(at("DLVPDBL", "rate_a"), (0.1, 10.0));
    assert_eq!(at("DLVPDBL", "dep_a"), (0.0, 100.0));
    assert_eq!(at("DLVDYNEQ", "f_2"), (40.0, 18000.0));
    assert_eq!(at("DLVDYNEQ", "w_2"), (1.0 / 9.0, 2.25));
    assert_eq!(at("DLVDYNEQ", "w_1"), (1.0 / 9.0, 2.1));
    assert_eq!(at("DLVDYNEQ", "t_1"), (-50.0, 18.0));
    assert_eq!(at("DLVDYNEQ", "trim"), (-12.0, 12.0));
    for token in ["DLVMB3", "DLVMB4"] {
        assert_eq!(at(token, "thr_1"), (-50.0, 18.0));
        assert_eq!(at(token, "gain_1"), (-6.0, 18.0));
        assert_eq!(at(token, "ratio_1"), (1.0, 20.0));
    }
    assert_eq!(at("DLVTRAN", "att_1"), (-15.0, 15.0));
    assert_eq!(at("DLVTRAN", "sus_1"), (-24.0, 24.0));
    assert_eq!(at("DLVTRAN", "pre_1"), (-15.0, 15.0));
    assert_eq!(at("DLVTRAN", "pst_1"), (-15.0, 15.0));
}

#[test]
fn every_model_passes_the_device_checks() {
    // Silence stays silent, noise gets through and dies away, random and extreme
    // settings stay finite, reset clears, at 44.1 and 96 kHz.
    for m in &FX_MODELS[2..] {
        crate::wing::fx::tests::check_device(m.token);
    }
}

#[test]
fn a_dlive_runs_its_models_in_any_of_its_16_engines() {
    use crate::dlive::MODELS;
    use crate::wing::fx::allowed_in;
    let model = model_index(MODELS.fx, "DLVSMRH").unwrap();
    assert!(allowed_in(&MODELS, model, 0) && allowed_in(&MODELS, model, 15));
    assert!(MODELS.fx.iter().any(|m| m.token == "DLVCHOR"));
}

// ---- SMR ------------------------------------------------------------------------------

const SMR_OPEN: [(&str, f32); 5] = [("lc", 0.0), ("hc", 20_000.0), ("hfd", 20_000.0), ("hfs", 0.0), ("pre", 0.0)];

#[test]
fn smr_decay_follows_its_control_in_every_room() {
    for token in ["DLVSMRC", "DLVSMRH", "DLVSMRR", "DLVSMRE"] {
        for decay in [1.0, 3.0] {
            let set = [&SMR_OPEN[..], &[("decay", decay), ("rlev", -60.0)]].concat();
            let (l, r) = impulse(token, &set, 3.0 * decay + 3.0);
            let m: Vec<f32> = l.iter().zip(&r).map(|(a, b)| 0.5 * (a + b)).collect();
            let t = rt60(&m);
            assert!((t / decay - 1.0).abs() < 0.3, "{token}: DECAY {decay} measured {t}");
        }
    }
}

#[test]
fn smr_input_filters_are_steep_and_on_the_input() {
    let level = |set: &[(&str, f32)], hz: f32| {
        let n = samples(1.0);
        let (l, _) = render("DLVSMRH", set, &sine(hz, n, 0.3));
        rms(&l[n / 2..])
    };
    let lc = [("lc", 400.0), ("hc", 20_000.0), ("rlev", 0.0)];
    assert!(level(&lc, 100.0) < 0.05 * level(&lc, 1000.0), "LF CUT at 400 Hz");
    let hc = [("lc", 0.0), ("hc", 2000.0), ("rlev", 0.0)];
    assert!(level(&hc, 8000.0) < 0.02 * level(&hc, 1000.0), "HF CUT at 2 kHz");
}

#[test]
fn smr_hf_decay_and_slope_shorten_the_highs() {
    let high = |set: &[(&str, f32)]| {
        let (l, _) = impulse("DLVSMRH", &[&SMR_OPEN[..], set].concat(), 6.0);
        rt60(&band(&l, 8000.0))
    };
    let flat = high(&[("hfd", 20_000.0), ("hfs", 0.0)]);
    let cut = high(&[("hfd", 2000.0), ("hfs", 100.0)]);
    assert!(cut < 0.5 * flat, "HF DECAY 2 kHz, slope 100: {flat} -> {cut}");
}

#[test]
fn smr_levels_split_reflections_from_the_tail_and_echoes_land() {
    let (early, tail) = (samples(0.05), samples(0.3));
    let e = |set: &[(&str, f32)]| {
        let (l, _) = impulse("DLVSMRH", &[&SMR_OPEN[..], set].concat(), 1.2);
        (energy(&l[..early]), energy(&l[tail..]))
    };
    let (reflect, _) = e(&[("dlev", -60.0)]);
    let (_, decay) = e(&[("rlev", -60.0)]);
    assert!(reflect > 10.0 * e(&[("rlev", -60.0)]).0, "REFLECTION LEVEL makes the start");
    assert!(decay > 100.0 * e(&[("dlev", -60.0)]).1, "DECAY LEVEL makes the tail");

    // Echo 1 and 2 are discrete taps at their times.
    let set =
        [&SMR_OPEN[..], &[("rlev", -60.0), ("dlev", -60.0), ("e1", 100.0), ("e1l", 0.0), ("e2", 250.0), ("e2l", -6.0)]].concat();
    let (l, _) = impulse("DLVSMRH", &set, 0.5);
    let (a, pa) = peak(&l, 10, samples(0.2));
    let (b, pb) = peak(&l, samples(0.2), samples(0.5));
    assert!(a.abs_diff(samples(0.1)) < 8 && b.abs_diff(samples(0.25)) < 8, "{a} {b}");
    assert!(pa > 1.5 * pb, "{pa} {pb}");
}

#[test]
fn smr_rooms_sound_different_and_the_emt_has_a_shape() {
    let m = |token: &str| {
        let (l, _) = impulse(token, &SMR_OPEN, 0.8);
        l
    };
    let (hall, room) = (m("DLVSMRH"), m("DLVSMRR"));
    let diff: f32 = hall.iter().zip(&room).map(|(a, b)| (a - b) * (a - b)).sum();
    assert!(diff > 0.1 * energy(&hall), "Hall and Room are different spaces");
    // The plate's Shape moves energy from the start of the build-up to later.
    let first = |shape: f32| {
        let set = [&SMR_OPEN[..], &[("shape", shape), ("det", 0.0), ("dlev", -60.0)]].concat();
        let (l, _) = impulse("DLVSMRE", &set, 0.4);
        energy(&l[samples(0.012)..samples(0.05)]) / energy(&l[samples(0.05)..samples(0.2)]).max(1e-12)
    };
    assert!(first(100.0) < first(0.0), "{} {}", first(0.0), first(100.0));
}

#[test]
fn smr_size_tail_diffusion_and_ref_detail_all_act() {
    let run_with = |set: &[(&str, f32)]| {
        let (l, _) = impulse("DLVSMRH", &[&SMR_OPEN[..], set].concat(), 0.6);
        l
    };
    let differs = |a: &[f32], b: &[f32]| a.iter().zip(b).map(|(x, y)| (x - y) * (x - y)).sum::<f32>() > 0.02 * energy(a);
    assert!(differs(&run_with(&[("size", 10.0)]), &run_with(&[("size", 100.0)])), "SIZE");
    assert!(differs(&run_with(&[("sdiff", 0.0)]), &run_with(&[("sdiff", 100.0)])), "SOURCE DIFFUSION");
    assert!(differs(&run_with(&[("bdiff", 0.0)]), &run_with(&[("bdiff", 100.0)])), "BODY DIFFUSION");
    assert!(differs(&run_with(&[("tdiff", 0.0)]), &run_with(&[("tdiff", 100.0)])), "TAIL DIFFUSION");
    assert!(differs(&run_with(&[("det", 0.0), ("dlev", -60.0)]), &run_with(&[("det", 100.0), ("dlev", -60.0)])), "REF DETAIL");
    assert!(differs(&run_with(&[("col", -12.0)]), &run_with(&[("col", 12.0)])), "COLOUR");
    assert!(differs(&run_with(&[("colf", 300.0), ("col", 12.0)]), &run_with(&[("colf", 6000.0), ("col", 12.0)])), "COLOUR FREQ");
    assert!(differs(&run_with(&[("mdep", 0.0), ("mspd", 5.0)]), &run_with(&[("mdep", 100.0), ("mspd", 5.0)])), "MOD DEPTH");
    assert!(differs(&run_with(&[("mdep", 100.0), ("mspd", 0.05)]), &run_with(&[("mdep", 100.0), ("mspd", 5.0)])), "MOD SPEED");
    assert!(differs(&run_with(&[("lfd", 0.25)]), &run_with(&[("lfd", 2.5)])), "LF DECAY");
    assert!(differs(&run_with(&[("lfd", 2.5), ("lfx", 150.0)]), &run_with(&[("lfd", 2.5), ("lfx", 1500.0)])), "LF XOVER");
}

// ---- delays -----------------------------------------------------------------------------

const DELAY_OPEN: [(&str, f32); 4] = [("fb", 0.0), ("lc", 20.0), ("hc", 20_000.0), ("fbf", 20_000.0)];

#[test]
fn stereo_tap_times_land_in_ms_and_at_a_tempo() {
    let (l, r) = impulse("DLVTAP", &[&DELAY_OPEN[..], &[("time_l", 300.0), ("time_r", 500.0)]].concat(), 1.0);
    assert!(peak(&l, 10, l.len()).0.abs_diff(samples(0.3)) <= 3, "left");
    assert!(peak(&r, 10, r.len()).0.abs_diff(samples(0.5)) <= 3, "right");

    // 120 BPM: a 1/4 note is 500 ms, a dotted 1/8 is 375 ms.
    let tempo = [("unit", 1.0), ("bpm", 120.0), ("note_l", 6.0), ("note_r", 10.0)];
    let (l, r) = impulse("DLVTAP", &[&DELAY_OPEN[..], &tempo].concat(), 1.0);
    assert!(peak(&l, 10, l.len()).0.abs_diff(samples(0.5)) <= 3, "1/4");
    assert!(peak(&r, 10, r.len()).0.abs_diff(samples(0.375)) <= 3, "1/8.");

    // GLOBAL takes the global tempo.
    let (l, _) = impulse("DLVTAP", &[&DELAY_OPEN[..], &tempo, &[("tap", 1.0), ("gbpm", 60.0)]].concat(), 1.2);
    assert!(peak(&l, 10, l.len()).0.abs_diff(samples(1.0)) <= 3, "global 60 BPM");

    // LINK: the right takes the left's time.
    let (_, r) = impulse("DLVTAP", &[&DELAY_OPEN[..], &[("time_l", 300.0), ("time_r", 500.0), ("link", 1.0)]].concat(), 1.0);
    assert!(peak(&r, 10, r.len()).0.abs_diff(samples(0.3)) <= 3, "linked");

    // The longest note at the slowest tempo stops at the stated 2.7 s.
    let slow = [("unit", 1.0), ("bpm", 20.0), ("note_l", 0.0), ("link", 1.0)];
    let (l, _) = impulse("DLVTAP", &[&DELAY_OPEN[..], &slow].concat(), 3.5);
    assert!(peak(&l, 10, l.len()).0.abs_diff(samples(2.7)) <= 3, "held at 2.7 s");
}

#[test]
fn stereo_tap_scatter_ping_pong_and_width() {
    let open = [("fb", 70.0), ("lc", 20.0), ("hc", 20_000.0), ("fbf", 20_000.0), ("time_l", 100.0), ("time_r", 100.0)];
    let at = |set: &[(&str, f32)], left: f32, right: f32, k: usize| {
        let (mut dev, v) = device("DLVTAP", &[&open[..], set].concat());
        let n = samples(0.6);
        let (mut l, mut r) = (vec![0.0; n], vec![0.0; n]);
        l[0] = left;
        r[0] = right;
        run(&mut dev, &v, &mut l, &mut r);
        let t = samples(0.1) * k;
        (peak(&l, t - 50, t + 50).1, peak(&r, t - 50, t + 50).1)
    };
    let (l, r) = at(&[("scat", 0.0)], 1.0, 0.0, 2);
    assert!(l > 0.3 && r < 1e-3, "no scatter: {l} {r}");
    let (l, r) = at(&[("scat", 100.0)], 1.0, 0.0, 2);
    assert!(l < 1e-3 && r > 0.3, "full scatter: {l} {r}");
    // Ping-pong bounces a centred input: first left, then right.
    let (l1, r1) = at(&[("pp", 1.0)], 1.0, 1.0, 1);
    let (l2, r2) = at(&[("pp", 1.0)], 1.0, 1.0, 2);
    assert!(l1 > 0.3 && r1 < 1e-3 && l2 < 1e-3 && r2 > 0.3, "{l1} {r1} {l2} {r2}");
    // Width 0 folds the sides into the middle.
    let (mut dev, v) = device("DLVTAP", &[&open[..], &[("width", 0.0), ("fb", 0.0)]].concat());
    let n = samples(0.3);
    let (mut l, mut r) = (vec![0.0; n], vec![0.0; n]);
    l[0] = 1.0;
    run(&mut dev, &v, &mut l, &mut r);
    let t = samples(0.1);
    assert!((peak(&l, t - 50, t + 50).1 - peak(&r, t - 50, t + 50).1).abs() < 1e-3, "width 0 is mono");
}

#[test]
fn stereo_tap_feedback_and_its_filter() {
    let repeat = |set: &[(&str, f32)], k: usize| {
        let set = [&[("lc", 20.0), ("hc", 20_000.0), ("time_l", 100.0), ("link", 1.0)], set].concat();
        let (l, _) = impulse("DLVTAP", &set, 0.6);
        let t = samples(0.1) * k;
        peak(&l, t - 80, t + 80).1
    };
    let (one, two) = (repeat(&[("fb", 50.0), ("fbf", 20_000.0)], 1), repeat(&[("fb", 50.0), ("fbf", 20_000.0)], 2));
    assert!((two / one - 0.5).abs() < 0.08, "FEEDBACK 50 %: {one} {two}");
    assert!(
        repeat(&[("fb", 90.0), ("fbf", 300.0), ("fbs", 3.0)], 3) < 0.2 * repeat(&[("fb", 90.0), ("fbf", 20_000.0)], 3),
        "FB FILTER"
    );
    // Slope: a steeper filter takes more of the repeats' highs out.
    let bright = |slope: f32| {
        let set =
            [("lc", 20.0), ("hc", 20_000.0), ("time_l", 100.0), ("link", 1.0), ("fb", 90.0), ("fbf", 1000.0), ("fbs", slope)];
        let (l, _) = impulse("DLVTAP", &set, 0.5);
        energy(&band(&l, 5000.0)[samples(0.19)..samples(0.32)])
    };
    assert!(bright(3.0) < 0.5 * bright(0.0), "FB SLOPE {} {}", bright(0.0), bright(3.0));
}

#[test]
fn stereo_tap_input_filters_cut() {
    let level = |set: &[(&str, f32)], hz: f32| {
        let n = samples(1.0);
        let (l, _) = render(
            "DLVTAP",
            &[&[("fb", 0.0), ("fbf", 20_000.0), ("time_l", 50.0), ("time_r", 50.0)], set].concat(),
            &sine(hz, n, 0.3),
        );
        rms(&l[n / 2..])
    };
    assert!(level(&[("lc", 2000.0), ("hc", 20_000.0)], 200.0) < 0.02 * level(&[("lc", 20.0), ("hc", 20_000.0)], 200.0));
    assert!(level(&[("lc", 20.0), ("hc", 1000.0)], 8000.0) < 0.02 * level(&[("lc", 20.0), ("hc", 20_000.0)], 8000.0));
}

#[test]
fn two_tap_times_and_range() {
    let (l, r) = impulse("DLV2TAP", &[&DELAY_OPEN[..], &[("time_l", 123.0), ("time_r", 777.0)]].concat(), 1.0);
    assert!(peak(&l, 5, l.len()).0.abs_diff(samples(0.123)) <= 3);
    assert!(peak(&r, 5, r.len()).0.abs_diff(samples(0.777)) <= 3);
    let (l, _) = impulse("DLV2TAP", &[&DELAY_OPEN[..], &[("time_l", 1360.0), ("time_r", 1360.0)]].concat(), 2.0);
    assert!(peak(&l, 5, l.len()).0.abs_diff(samples(1.36)) <= 3, "the longest");
    let (l, r) = impulse("DLV2TAP", &[&DELAY_OPEN[..], &[("time_l", 200.0), ("time_r", 400.0), ("link", 1.0)]].concat(), 1.0);
    assert_eq!(peak(&l, 5, l.len()).0, peak(&r, 5, r.len()).0, "LINK");
}

const BBD_OPEN: [(&str, f32); 5] = [("fb", 0.0), ("hp", 20.0), ("ldamp", 20.0), ("hdamp", 20_000.0), ("dist", 0.0)];

#[test]
fn bucket_brigade_lands_on_time_and_darkens_with_a_slower_clock() {
    let (l, _) = impulse("DLVBBD", &[&BBD_OPEN[..], &[("time", 250.0)]].concat(), 0.5);
    let (i, p) = peak(&l, 10, l.len());
    assert!(i.abs_diff(samples(0.25)) < 40 && p > 0.02, "echo at {i} ({p})");
    // The bandwidth falls as the delay grows.
    let hf = |time: f32| {
        let n = samples(4.0);
        let (l, _) = render("DLVBBD", &[&BBD_OPEN[..], &[("time", time)]].concat(), &sine(4000.0, n, 0.3));
        rms(&l[samples(3.0)..])
    };
    assert!(hf(2000.0) < 0.5 * hf(30.0), "{} {}", hf(30.0), hf(2000.0));
    // BPM: 1/4 at 120 is 500 ms.
    let (l, _) = impulse("DLVBBD", &[&BBD_OPEN[..], &[("unit", 1.0), ("bpm", 120.0), ("note", 6.0)]].concat(), 0.8);
    assert!(peak(&l, 10, l.len()).0.abs_diff(samples(0.5)) < 60);
}

#[test]
fn bucket_brigade_damping_and_dist() {
    let repeat_level = |set: &[(&str, f32)], hz: f32| {
        let set = [&[("fb", 90.0), ("time", 120.0), ("hp", 20.0), ("dist", 0.0)], set].concat();
        let (l, _) = impulse("DLVBBD", &set, 0.6);
        // The third repeat, in the band.
        energy(&band(&l, hz)[samples(0.33)..samples(0.42)])
    };
    let high = [("ldamp", 20.0), ("hdamp", 800.0), ("fbs", 3.0)];
    let open = [("ldamp", 20.0), ("hdamp", 20_000.0), ("fbs", 3.0)];
    assert!(repeat_level(&high, 3000.0) < 0.7 * repeat_level(&open, 3000.0), "HIGH DAMP");
    let low = [("ldamp", 1500.0), ("hdamp", 20_000.0), ("fbs", 3.0)];
    assert!(repeat_level(&low, 200.0) < 0.7 * repeat_level(&open, 200.0), "LOW DAMP");
    // Dist: OFF stays clean, HARD adds odd harmonics.
    let harmonics = |dist: f32| {
        let n = samples(1.0);
        let set = [("time", 30.0), ("fb", 40.0), ("hp", 20.0), ("ldamp", 20.0), ("hdamp", 20_000.0), ("dist", dist)];
        let (l, _) = render("DLVBBD", &set, &sine(220.0, n, 0.95));
        let (t, h3) = (tone(&l[n / 2..], 220.0), tone(&l[n / 2..], 660.0));
        h3 / t
    };
    assert!(harmonics(3.0) > 3.0 * harmonics(0.0), "{} {}", harmonics(0.0), harmonics(3.0));
}

const ECHO_DRY: [(&str, f32); 2] = [("int", 0.0), ("noise", 0.0)];

#[test]
fn echo_heads_sit_at_one_two_and_three_times_and_modes_combine_them() {
    // Rate 100 is the fastest tape: the first head at 40 ms.
    let head = |mode: f32, k: usize| {
        let (l, r) = impulse("DLVECHO", &[&ECHO_DRY[..], &[("mode", mode), ("rate", 100.0)]].concat(), 0.3);
        assert_eq!(l, r, "mono out");
        let t = samples(0.04) * k;
        peak(&l, t - 100, t + 100).1
    };
    for (mode, heads) in [
        (0.0, [1, 0, 0]),
        (1.0, [0, 1, 0]),
        (2.0, [0, 0, 1]),
        (3.0, [1, 1, 0]),
        (4.0, [0, 1, 1]),
        (5.0, [1, 0, 1]),
        (6.0, [1, 1, 1]),
    ] {
        for (k, on) in heads.iter().enumerate() {
            let level = head(mode, k + 1);
            assert_eq!(level > 0.02, *on == 1, "mode {mode} head {k}: {level}");
        }
    }
}

#[test]
fn echo_oscillates_past_75_percent_and_stays_bounded() {
    let tail = |intensity: f32| {
        let n = samples(5.0);
        let mut x = sine(220.0, n, 0.3);
        x[samples(0.3)..].fill(0.0);
        let (l, _) = render("DLVECHO", &[("mode", 0.0), ("int", intensity), ("rate", 100.0)], &x);
        rms(&l[samples(4.0)..])
    };
    assert!(tail(50.0) < 1e-3, "50 % dies: {}", tail(50.0));
    let held = tail(100.0);
    assert!(held > 0.02, "100 % oscillates: {held}");
    assert!(held < 8.0, "bounded: {held}");
}

#[test]
fn echo_input_volume_noise_and_tone() {
    let level = |set: &[(&str, f32)], hz: f32| {
        let n = samples(1.0);
        let (l, _) = render("DLVECHO", &[&ECHO_DRY[..], &[("rate", 50.0)], set].concat(), &sine(hz, n, 0.1));
        rms(&l[n / 2..])
    };
    assert!(level(&[("in", 12.0)], 500.0) > 2.5 * level(&[], 500.0), "INPUT");
    assert!(level(&[("vol", 12.0)], 500.0) > 3.0 * level(&[], 500.0), "ECHO VOLUME");
    assert!(level(&[("bass", 12.0)], 100.0) > 2.0 * level(&[("bass", -12.0)], 100.0), "BASS");
    assert!(level(&[("treble", 12.0)], 3000.0) > 2.0 * level(&[("treble", -12.0)], 3000.0), "TREBLE");
    assert!(level(&[("rate", 0.0)], 500.0) > 0.0, "RATE");
    // NOISE: hiss with no input at all.
    let (l, _) = render("DLVECHO", &[("noise", 100.0)], &vec![0.0; samples(0.5)]);
    assert!(rms(&l) > 1e-3 && rms(&l) < 0.5, "NOISE {}", rms(&l));
}

// ---- Gated Verb ---------------------------------------------------------------------------

#[test]
fn gated_verb_arrives_on_time_and_stops_at_its_window() {
    let set = [("lc", 20.0), ("hc", 20_000.0), ("pre", 30.0), ("att", 5.0), ("hold", 100.0), ("rel", 95.0)];
    let (l, r) = impulse("DLVGVRB", &set, 1.0);
    let m: Vec<f32> = l.iter().zip(&r).map(|(a, b)| a + b).collect();
    assert!(energy(&m[..samples(0.03)]) < 1e-8, "nothing before the predelay");
    assert!(energy(&m[samples(0.03)..samples(0.23)]) > 1e-3, "the burst");
    assert!(energy(&m[samples(0.28)..]) < 1e-4 * energy(&m[..samples(0.25)]), "it stops");
    // Together at most 500 ms: 100 + 400 + 500 is cut down to 500.
    let (l, _) =
        impulse("DLVGVRB", &[("lc", 20.0), ("hc", 20_000.0), ("pre", 0.0), ("att", 100.0), ("hold", 400.0), ("rel", 500.0)], 1.0);
    assert!(energy(&l[samples(0.56)..]) < 1e-4 * energy(&l[..samples(0.5)]), "the 500 ms limit");
}

#[test]
fn gated_verb_panner_moves_across_the_field_and_widths_narrow() {
    let set = [
        ("type", 1.0),
        ("lc", 20.0),
        ("hc", 20_000.0),
        ("pre", 0.0),
        ("att", 1.0),
        ("hold", 150.0),
        ("rel", 150.0),
        ("width", 2.0),
    ];
    let (l, r) = impulse("DLVGVRB", &set, 0.5);
    let share = |x: &[f32], a: f32, b: f32| energy(&x[samples(a)..samples(b)]);
    assert!(share(&l, 0.01, 0.06) > 3.0 * share(&r, 0.01, 0.06), "starts on the left");
    assert!(
        share(&r, 0.24, 0.29) > 2.5 * share(&l, 0.24, 0.29),
        "ends on the right: {} {}",
        share(&l, 0.24, 0.29),
        share(&r, 0.24, 0.29)
    );
    let mono = [("type", 0.0), ("width", 0.0), ("lc", 20.0), ("hc", 20_000.0)];
    let (l, r) = impulse("DLVGVRB", &mono, 0.5);
    assert!(l.iter().zip(&r).all(|(a, b)| (a - b).abs() < 1e-4), "MONO");
    let (mut dev, v) = device("DLVGVRB", &[("type", 0.0), ("width", 2.0), ("lc", 20.0)]);
    let (mut l, mut r) = (vec![0.0; 4800], vec![0.0; 4800]);
    l[0] = 1.0;
    r[0] = 1.0;
    run(&mut dev, &v, &mut l, &mut r);
    assert!(l.iter().zip(&r).any(|(a, b)| (a - b).abs() > 1e-3), "WIDE differs between the sides");
}

#[test]
fn gated_verb_filters_and_types_differ() {
    let level = |set: &[(&str, f32)], hz: f32| {
        let n = samples(0.8);
        let (l, _) = render("DLVGVRB", set, &sine(hz, n, 0.3));
        rms(&l[n / 2..])
    };
    assert!(
        level(&[("lc", 4000.0), ("hc", 20_000.0)], 300.0) < 0.05 * level(&[("lc", 20.0), ("hc", 20_000.0)], 300.0),
        "24 dB/oct lo cut"
    );
    assert!(
        level(&[("lc", 20.0), ("hc", 400.0)], 4000.0) < 0.15 * level(&[("lc", 20.0), ("hc", 20_000.0)], 4000.0),
        "12 dB/oct hi cut"
    );
    // Powerbox is denser than Classic: more samples in the window are alive.
    let dense = |t: f32| {
        let (l, _) = impulse("DLVGVRB", &[("type", t), ("lc", 20.0), ("hc", 20_000.0), ("pre", 0.0), ("diff", 0.0)], 0.4);
        l.iter().take(samples(0.3)).filter(|x| x.abs() > 1e-6).count()
    };
    assert!(dense(2.0) > dense(0.0), "{} {}", dense(0.0), dense(2.0));
    let d = |diff: f32| impulse("DLVGVRB", &[("diff", diff), ("lc", 20.0), ("hc", 20_000.0)], 0.3).0;
    let (a, b) = (d(0.0), d(100.0));
    assert!(a.iter().zip(&b).map(|(x, y)| (x - y).abs()).sum::<f32>() > 1e-3, "DIFFUSION");
}

// ---- modulation ---------------------------------------------------------------------------

#[test]
fn adt_places_its_voices_at_the_separation() {
    let (l, r) = impulse("DLVADT", &[("sep", 30.0), ("thick", 0.0), ("mode", 0.0), ("wide", 1.0)], 0.2);
    let (i, p) = peak(&l, 5, l.len());
    assert!(i.abs_diff(samples(0.03)) <= 6 && p > 0.3, "left at {i} ({p})");
    // The second voice arrives later and on the right.
    assert!(peak(&r, 5, r.len()).0 > i + samples(0.005), "right after left");
    // Quad: a voice more on each side.
    let peaks = |mode: f32| {
        let (l, _) = impulse("DLVADT", &[("sep", 30.0), ("thick", 0.0), ("mode", mode), ("wide", 0.0)], 0.2);
        (0..l.len())
            .filter(|&i| {
                l[i].abs() > 0.05 && l[i].abs() >= l[i.saturating_sub(1)].abs() && l[i].abs() >= l[(i + 1).min(l.len() - 1)].abs()
            })
            .count()
    };
    assert!(peaks(1.0) > peaks(0.0), "{} {}", peaks(0.0), peaks(1.0));
}

#[test]
fn adt_thickness_moves_the_voices_and_autopan_moves_them_across() {
    let wobble = |thick: f32| {
        let n = samples(2.0);
        let (l, _) = render("DLVADT", &[("sep", 20.0), ("thick", thick), ("wide", 0.0)], &sine(1000.0, n, 0.4));
        spread(&envelope(&l[n / 4..], 480))
    };
    assert!(wobble(100.0) > wobble(0.0) + 0.02, "THICKNESS {} {}", wobble(0.0), wobble(100.0));
    let side = |auto: f32| {
        let n = samples(3.0);
        let set = [("auto", auto), ("adep", 100.0), ("aspd", 2.0), ("thick", 0.0), ("wide", 0.0)];
        let (l, _) = render("DLVADT", &set, &sine(1000.0, n, 0.4));
        spread(&envelope(&l[n / 4..], 960))
    };
    assert!(side(1.0) > 2.0 * side(0.0), "AUTOPAN {} {}", side(0.0), side(1.0));
    // WIDE pans the voices further apart.
    let image = |wide: f32| {
        let (mut dev, v) = device("DLVADT", &[("sep", 20.0), ("thick", 0.0), ("wide", wide)]);
        let (mut l, mut r) = (vec![0.0; samples(0.2)], vec![0.0; samples(0.2)]);
        l[0] = 1.0;
        run(&mut dev, &v, &mut l, &mut r);
        energy(&l) / energy(&r).max(1e-12)
    };
    assert!(image(1.0) > image(0.0) * 1.5, "{} {}", image(0.0), image(1.0));
}

#[test]
fn chorus_depth_and_stereo_field_switches() {
    let n = samples(2.0);
    let spec = |set: &[(&str, f32)]| render("DLVCHOR", set, &sine(1000.0, n, 0.4));
    // Depth 0 is a clean delay; depth spreads the tone off its frequency.
    let purity = |depth: f32| {
        let (l, _) = spec(&[("depth", depth), ("rate", 1.0)]);
        let t = &l[n / 2..];
        tone(t, 1000.0) / rms(t)
    };
    assert!(purity(0.0) > 1.2 * purity(100.0), "{} {}", purity(0.0), purity(100.0));
    // LFO Split: the sides sweep apart.
    let lr_diff = |split: f32, wide: f32| {
        let (l, r) = spec(&[("split", split), ("wide", wide), ("depth", 100.0), ("rate", 1.0)]);
        l.iter().zip(&r).map(|(a, b)| (a - b).abs()).sum::<f32>() / l.len() as f32
    };
    assert!(lr_diff(0.0, 0.0) < 1e-3, "no split: the same on both sides");
    assert!(lr_diff(1.0, 1.0) > 2.0 * lr_diff(1.0, 0.0) * 0.5 && lr_diff(1.0, 1.0) > 0.01, "split and wide");
    // MultiVoice adds voices: more modulation products around the tone.
    let beat = |mv: f32| {
        let (l, _) = spec(&[("mv", mv), ("depth", 100.0), ("rate", 1.0), ("wide", 0.0), ("split", 0.0)]);
        spread(&envelope(&l[n / 2..], 480))
    };
    assert!((beat(1.0) - beat(0.0)).abs() > 0.02, "MULTIVOICE {} {}", beat(0.0), beat(1.0));
}

#[test]
fn chorus_rate_wave_and_pan_depth() {
    let n = samples(2.0);
    let (a, _) = render("DLVCHOR", &[("rate", 0.2), ("depth", 100.0), ("split", 0.0)], &sine(1000.0, n, 0.4));
    let (b, _) = render("DLVCHOR", &[("rate", 5.0), ("depth", 100.0), ("split", 0.0)], &sine(1000.0, n, 0.4));
    let diff = a.iter().zip(&b).map(|(x, y)| (x - y).abs()).sum::<f32>() / n as f32;
    assert!(diff > 0.01, "RATE {diff}");
    let (r, _) = render("DLVCHOR", &[("wave", 1.0), ("depth", 100.0), ("split", 0.0)], &sine(1000.0, n, 0.4));
    let (s, _) = render("DLVCHOR", &[("wave", 0.0), ("depth", 100.0), ("split", 0.0)], &sine(1000.0, n, 0.4));
    let diff = r.iter().zip(&s).map(|(x, y)| (x - y).abs()).sum::<f32>() / n as f32;
    assert!(diff > 0.005, "RECTIFIED {diff}");
    let swing = |pan: f32| {
        let (mut dev, v) = device("DLVCHOR", &[("pan", pan), ("depth", 0.0), ("rate", 1.0), ("split", 0.0), ("wide", 1.0)]);
        let (mut l, mut r) = (sine(1000.0, n, 0.4), sine(1000.0, n, 0.4));
        run(&mut dev, &v, &mut l, &mut r);
        spread(&envelope(&l[n / 4..], 960))
    };
    assert!(swing(100.0) > 1.3 * swing(0.0), "PAN DEPTH {} {}", swing(0.0), swing(100.0));
}

#[test]
fn symphonic_chorus_depth_and_frequency_thicken() {
    let n = samples(2.0);
    let purity = |set: &[(&str, f32)]| {
        let (l, _) = render("DLVSYMP", set, &sine(1000.0, n, 0.4));
        let t = &l[n / 2..];
        tone(t, 1000.0) / rms(t)
    };
    assert!(purity(&[("depth", 0.0)]) > 1.2 * purity(&[("depth", 100.0)]), "DEPTH");
    let (a, _) = render("DLVSYMP", &[("freq", 0.2), ("depth", 100.0)], &sine(1000.0, n, 0.4));
    let (b, _) = render("DLVSYMP", &[("freq", 5.0), ("depth", 100.0)], &sine(1000.0, n, 0.4));
    assert!(a.iter().zip(&b).map(|(x, y)| (x - y).abs()).sum::<f32>() / n as f32 > 0.01, "FREQUENCY");
    // Wide: the two sides differ.
    let (l, r) = render("DLVSYMP", &[("depth", 100.0)], &sine(1000.0, n, 0.4));
    assert!(l.iter().zip(&r).map(|(x, y)| (x - y).abs()).sum::<f32>() / n as f32 > 0.01, "stereo");
}

#[test]
fn flanger_sweeps_and_regenerates() {
    let wobble = |set: &[(&str, f32)]| {
        let n = samples(4.0);
        let (l, _) = render("DLVFLNG", set, &sine(1500.0, n, 0.4));
        // Against the dry signal: the comb moves across the tone.
        let x = sine(1500.0, n, 0.4);
        let sum: Vec<f32> = l.iter().zip(&x).map(|(a, b)| a + b).collect();
        spread(&envelope(&sum[n / 4..], 240))
    };
    let swept = wobble(&[("depth", 100.0), ("speed", 0.25), ("regen", 0.0), ("lfo", 4.0), ("spread", 100.0)]);
    assert!(swept > 3.0, "the comb moves: {swept}");
    // LFO Manual: stopped, the comb stands still at Depth's position.
    assert!(wobble(&[("depth", 40.0), ("speed", 0.25), ("regen", 0.0), ("lfo", 0.0), ("spread", 100.0)]) < 1.05, "MANUAL");
    // Regenerate: the comb sharpens.
    let hollow = |regen: f32| {
        let n = samples(1.0);
        let set = [("depth", 0.0), ("lfo", 0.0), ("regen", regen)];
        let (l, _) = render("DLVFLNG", &set, &sine(1000.0, n, 0.2));
        rms(&l[n / 2..])
    };
    assert!((hollow(95.0) - hollow(0.0)).abs() > 0.02, "REGENERATE {} {}", hollow(0.0), hollow(95.0));
}

#[test]
fn flanger_lfos_types_and_stereo_differ() {
    let n = samples(1.0);
    let render_l = |set: &[(&str, f32)]| render("DLVFLNG", set, &sine(1500.0, n, 0.4)).0;
    let base = [("depth", 100.0), ("speed", 2.0), ("regen", 0.0)];
    let d = |a: &[f32], b: &[f32]| a.iter().zip(b).map(|(x, y)| (x - y).abs()).sum::<f32>() / n as f32;
    for (i, j) in [(1.0, 2.0), (2.0, 3.0), (3.0, 4.0)] {
        let a = render_l(&[&base[..], &[("lfo", i)]].concat());
        let b = render_l(&[&base[..], &[("lfo", j)]].concat());
        assert!(d(&a, &b) > 0.005, "LFO {i} vs {j}");
    }
    let t = |ty: f32| render_l(&[&base[..], &[("type", ty), ("regen", 50.0)]].concat());
    assert!(d(&t(0.0), &t(2.0)) > 0.01, "TYPE");
    // Stereo Split offsets the sides; Spread opens them further.
    let (l, r) = render("DLVFLNG", &[("split", 0.0), ("spread", 0.0), ("depth", 100.0), ("speed", 2.0)], &sine(1500.0, n, 0.4));
    let (l2, r2) =
        render("DLVFLNG", &[("split", 180.0), ("spread", 100.0), ("depth", 100.0), ("speed", 2.0)], &sine(1500.0, n, 0.4));
    assert!(d(&l2, &r2) > 5.0 * d(&l, &r).max(1e-4), "SPLIT/SPREAD {} {}", d(&l, &r), d(&l2, &r2));
}

/// A phaser's notches show against the dry signal.
fn phased(set: &[(&str, f32)], hz: f32, n: usize) -> Vec<f32> {
    let x = sine(hz, n, 0.4);
    let (l, _) = render("DLVMOO", set, &x);
    l.iter().zip(&x).map(|(a, b)| a + b).collect()
}

#[test]
fn moo_phaser_cuts_moving_notches_and_stages_count() {
    let n = samples(4.0);
    let swing =
        spread(&envelope(&phased(&[("rate", 1.0), ("depth", 100.0), ("offset", 30.0), ("mres", 0.0)], 800.0, n)[n / 4..], 480));
    assert!(swing > 4.0, "{swing}");
    // More stages: more notches, so more dips over one sweep.
    let dips = |stages: f32| {
        let e = envelope(
            &phased(&[("rate", 0.5), ("depth", 100.0), ("offset", 10.0), ("mres", 0.0), ("stg", stages)], 800.0, n),
            240,
        );
        let mean = e.iter().sum::<f32>() / e.len() as f32;
        e.windows(3).filter(|w| w[1] < w[0] && w[1] < w[2] && w[1] < 0.7 * mean).count()
    };
    assert!(dips(5.0) > dips(0.0), "{} {}", dips(0.0), dips(5.0));
}

#[test]
fn moo_phaser_resonance_shapes_and_split() {
    let n = samples(2.0);
    let level = |set: &[(&str, f32)]| {
        let (l, _) = render("DLVMOO", set, &sine(600.0, n, 0.2));
        rms(&l[n / 2..])
    };
    let fixed = [("rate", 0.1), ("depth", 0.0), ("offset", 37.0)];
    assert!(
        (level(&[&fixed[..], &[("mres", 9.0)]].concat()) - level(&[&fixed[..], &[("mres", 0.0)]].concat())).abs() > 0.01,
        "MIX RESONANCE"
    );
    // Resonant stages: where the feedback is taken from.
    let a = level(&[&fixed[..], &[("mres", 9.0), ("res", 0.0)]].concat());
    let b = level(&[&fixed[..], &[("mres", 9.0), ("res", 5.0)]].concat());
    assert!((a - b).abs() > 0.005, "RESONANT STAGES {a} {b}");
    // The four LFO shapes aren't the same sweep.
    let (s0, _) = render("DLVMOO", &[("wave", 0.0), ("rate", 2.0), ("depth", 100.0)], &sine(800.0, n, 0.4));
    for w in [1.0, 2.0, 3.0] {
        let (s, _) = render("DLVMOO", &[("wave", w), ("rate", 2.0), ("depth", 100.0)], &sine(800.0, n, 0.4));
        assert!(s.iter().zip(&s0).map(|(x, y)| (x - y).abs()).sum::<f32>() / n as f32 > 0.005, "shape {w}");
    }
    let (l, r) = render("DLVMOO", &[("stereo", 0.0), ("rate", 2.0), ("depth", 100.0)], &sine(800.0, n, 0.4));
    assert!(l.iter().zip(&r).all(|(a, b)| (a - b).abs() < 1e-5), "MONO");
    let (l, r) = render("DLVMOO", &[("stereo", 1.0), ("rate", 2.0), ("depth", 100.0)], &sine(800.0, n, 0.4));
    assert!(l.iter().zip(&r).map(|(a, b)| (a - b).abs()).sum::<f32>() / n as f32 > 0.01, "SPLIT");
}

#[test]
fn rotator_turns_and_stops() {
    let n = samples(3.0);
    let am = |set: &[(&str, f32)]| {
        let (l, _) = render("DLVROT", set, &sine(4000.0, n, 0.4));
        spread(&envelope(&l[n / 2..], 240))
    };
    assert!(am(&[("spd", 6.7), ("defl", 0.0)]) > 1.3, "the horn turns");
    assert!(am(&[("on", 0.0)]) < 1.08, "ROTORS off: coasted to a stop");
    assert!(am(&[("spd", 6.7), ("defl", 0.0)]) > am(&[("spd", 6.7), ("defl", 1.0)]), "DEFLECTORS");
}

#[test]
fn rotator_controls_act() {
    let n = samples(1.0);
    let l_r = |set: &[(&str, f32)], hz: f32| render("DLVROT", set, &sine(hz, n, 0.3));
    let (h, _) = l_r(&[("bal", 100.0)], 4000.0);
    let (d, _) = l_r(&[("bal", -100.0)], 4000.0);
    assert!(rms(&h[n / 2..]) > 10.0 * rms(&d[n / 2..]), "BALANCE");
    let (l, r) = l_r(&[("width", 0.0)], 1000.0);
    assert!(l.iter().zip(&r).all(|(a, b)| (a - b).abs() < 1e-5), "WIDTH 0 is mono");
    let (l, r) = l_r(&[("width", 100.0)], 1000.0);
    assert!(l.iter().zip(&r).any(|(a, b)| (a - b).abs() > 1e-3), "WIDTH");
    let d3 = |tube: f32| {
        let (l, _) = l_r(&[("tube", tube), ("drv", 11.0), ("bal", -100.0), ("on", 0.0)], 150.0);
        tone(&l[n / 2..], 450.0) / tone(&l[n / 2..], 150.0)
    };
    assert!(d3(1.0) > 5.0 * d3(0.0), "TUBE DRIVE {} {}", d3(0.0), d3(1.0));
    let hum = |mains: f32, hz: f32| {
        let (l, _) = render("DLVROT", &[("hum", 100.0), ("mains", mains)], &vec![0.0; n]);
        tone(&l[n / 4..], hz)
    };
    assert!(hum(0.0, 50.0) > 10.0 * hum(0.0, 60.0), "50 Hz mains");
    assert!(hum(1.0, 60.0) > 10.0 * hum(1.0, 50.0), "60 Hz mains");
    let (a, _) = l_r(&[("mic", 0.0), ("defl", 0.0), ("spd", 6.7)], 4000.0);
    let (b, _) = l_r(&[("mic", 2.0), ("defl", 0.0), ("spd", 6.7)], 4000.0);
    assert!(a.iter().zip(&b).map(|(x, y)| (x - y).abs()).sum::<f32>() / n as f32 > 0.005, "MIC SELECT");
    let (a, _) = l_r(&[("back", 0.0), ("spd", 6.7), ("bal", 100.0)], 1000.0);
    let (b, _) = l_r(&[("back", 1.0), ("spd", 6.7), ("bal", 100.0)], 1000.0);
    assert!(a.iter().zip(&b).map(|(x, y)| (x - y).abs()).sum::<f32>() / n as f32 > 1e-3, "BACK PANEL");
    let (a, _) = l_r(&[("sep", 0.0), ("spd", 6.7)], 4000.0);
    let (b, _) = l_r(&[("sep", 100.0), ("spd", 6.7)], 4000.0);
    assert!(a.iter().zip(&b).map(|(x, y)| (x - y).abs()).sum::<f32>() / n as f32 > 1e-3, "SEPARATION");
}

// ---- pitch --------------------------------------------------------------------------------

#[test]
fn vs1_shifts_by_its_interval_and_voices_pan() {
    let n = samples(1.0);
    let hz = |set: &[(&str, f32)]| {
        let (l, r) = render("DLVVS1", &[&[("cent_a", 0.0), ("cent_b", 0.0), ("width", 0.0)], set].concat(), &sine(440.0, n, 0.4));
        let m: Vec<f32> = l.iter().zip(&r).map(|(a, b)| a + b).collect();
        pitch_hz(&m[n / 4..])
    };
    // Voice B shifted the same as A so the sum is one tone (Link too).
    assert!(
        (hz(&[("semi_a", 12.0), ("link", 1.0)]) - 880.0).abs() < 12.0,
        "an octave up: {}",
        hz(&[("semi_a", 12.0), ("link", 1.0)])
    );
    assert!((hz(&[("semi_a", -12.0), ("link", 1.0)]) - 220.0).abs() < 8.0, "an octave down");
    assert!((hz(&[("oct_a", 1.0), ("link", 1.0)]) - 220.0).abs() < 8.0, "OCT DOWN");
    let flat = 440.0 * 2f32.powf(-1.0 / 12.0);
    assert!((hz(&[("flat_a", 1.0), ("link", 1.0)]) - flat).abs() < 8.0, "FLAT");
    let cents = 440.0 * 2f32.powf(50.0 / 1200.0);
    assert!((hz(&[("cent_a", 50.0), ("cent_b", 50.0)]) - cents).abs() < 8.0, "CENTS");
    // Without Link the two voices keep their own intervals.
    let (l, r) = render("DLVVS1", &[("semi_a", 12.0), ("semi_b", -12.0), ("width", 100.0)], &sine(440.0, n, 0.4));
    assert!(pitch_hz(&l[n / 4..]) > 600.0 && pitch_hz(&r[n / 4..]) < 300.0, "panned voices");
}

#[test]
fn the_pitch_models_are_under_6_ms_at_unison() {
    for (token, set) in [
        ("DLVVS1", vec![("semi_a", 0.0), ("cent_a", 0.0), ("semi_b", 0.0), ("cent_b", 0.0)]),
        ("DLVPDBL", vec![("dly_a", 0.0), ("pit_a", 0.0), ("dly_b", 0.0), ("pit_b", 0.0)]),
    ] {
        let (l, _) = impulse(token, &set, 0.05);
        let (at, level) = peak(&l, 0, l.len());
        assert!(level > 0.1 && (at as f32) < 0.006 * SR, "{token}: at {at} samples");
    }
}

#[test]
fn pitch_doubler_detunes_delays_and_modulates() {
    let n = samples(1.0);
    let set_both =
        |pit: f32| vec![("pit_a", pit), ("pit_b", pit), ("wid_a", 0.0), ("wid_b", 0.0), ("lfo_a", 0.0), ("lfo_b", 0.0)];
    let up = 440.0 * 2f32.powf(100.0 / 1200.0);
    let (l, r) = render("DLVPDBL", &set_both(100.0), &sine(440.0, n, 0.4));
    let m: Vec<f32> = l.iter().zip(&r).map(|(a, b)| a + b).collect();
    assert!((pitch_hz(&m[n / 4..]) - up).abs() < 8.0, "+100 cents: {}", pitch_hz(&m[n / 4..]));
    // Delay: 20 ms more delay arrives 20 ms later.
    let at = |dly: f32| {
        let set = [("dly_a", dly), ("pit_a", 0.0), ("wid_a", 0.0), ("lev_b", -60.0)];
        let (l, _) = impulse("DLVPDBL", &set, 0.1);
        peak(&l, 0, l.len()).0
    };
    assert!((at(20.0) - at(0.0)).abs_diff(samples(0.02)) <= 3, "{} {}", at(0.0), at(20.0));
    // The pitch LFO wobbles the voice when Active, not otherwise.
    let wob = |on: f32| {
        let set = [("lfo_a", on), ("rate_a", 1.0), ("dep_a", 100.0), ("pit_a", 0.0), ("wid_a", 0.0), ("lev_b", -60.0)];
        let long = samples(3.0);
        let (l, _) = render("DLVPDBL", &set, &sine(1000.0, long, 0.4));
        let c: Vec<f32> = l[long / 4..].chunks(samples(0.25)).filter(|c| c.len() == samples(0.25)).map(pitch_hz).collect();
        spread(&c)
    };
    assert!(wob(1.0) > 1.02 && wob(0.0) < 1.01, "{} {}", wob(0.0), wob(1.0));
    // Level and Width.
    let (l, _) = render("DLVPDBL", &[("lev_a", -60.0), ("lev_b", -60.0)], &sine(440.0, n, 0.4));
    assert!(rms(&l[n / 2..]) < 1e-4, "LEVEL");
    let (l, r) = render("DLVPDBL", &[("wid_a", 100.0), ("lev_b", -60.0)], &sine(440.0, n, 0.4));
    assert!(rms(&l[n / 2..]) > 10.0 * rms(&r[n / 2..]), "WIDTH pans");
}

const OPEN_HYPA: [(&str, f32); 3] = [("cut", 300.0), ("boost", 0.0), ("infra", 0.0)];

#[test]
fn hypabass_generates_an_octave_down_and_lost_fundamental_its_harmonics() {
    let n = samples(2.0);
    let (l, _) = render("DLVHYPA", &[&OPEN_HYPA[..], &[("sub", 100.0), ("mode", 0.0)]].concat(), &sine(120.0, n, 0.5));
    assert!(tone(&l[n / 2..], 60.0) > 0.03, "Infra Bass: an octave below: {}", tone(&l[n / 2..], 60.0));
    // Infra: a tone an octave above the Infra band gives its octave in it.
    let (l, _) =
        render("DLVHYPA", &[("cut", 300.0), ("boost", 0.0), ("infra", 100.0), ("sub", 0.0), ("mode", 0.0)], &sine(50.0, n, 0.5));
    assert!(tone(&l[n / 2..], 25.0) > 0.03, "INFRA {}", tone(&l[n / 2..], 25.0));
    // Lost Fundamental: harmonics of the bass and no new fundamental.
    let (lost, _) =
        render("DLVHYPA", &[&OPEN_HYPA[..], &[("sub", 100.0), ("mode", 1.0), ("cut", 100.0)]].concat(), &sine(70.0, n, 0.5));
    let (dry, _) = render("DLVHYPA", &[("infra", 0.0), ("sub", 0.0), ("cut", 100.0)], &sine(70.0, n, 0.5));
    assert!(tone(&lost[n / 2..], 140.0) > 0.03, "second harmonic: {}", tone(&lost[n / 2..], 140.0));
    assert!((tone(&lost[n / 2..], 70.0) - tone(&dry[n / 2..], 70.0)).abs() < 0.05, "the fundamental stays");
    // The dry path is at unity.
    assert!((rms(&dry[n / 2..]) - 0.5 / std::f32::consts::SQRT_2).abs() < 0.01, "{}", rms(&dry[n / 2..]));
}

#[test]
fn hypabass_input_boost_and_upper_cut_act() {
    let n = samples(2.0);
    let gen = |set: &[(&str, f32)], hz: f32| {
        let dry = sine(hz, n, 0.3);
        let (l, _) = render("DLVHYPA", &[&[("infra", 0.0), ("sub", 100.0), ("mode", 0.0)], set].concat(), &dry);
        let w: Vec<f32> = l.iter().zip(&dry).map(|(a, b)| a - b).collect();
        rms(&w[n / 2..])
    };
    assert!(gen(&[("boost", 12.0)], 100.0) > 1.5 * gen(&[("boost", 0.0)], 100.0), "LF INPUT BOOST");
    assert!(gen(&[("cut", 300.0)], 200.0) > 3.0 * gen(&[("cut", 50.0)], 200.0), "UPPER FREQ CUT");
}

// ---- dynamics -----------------------------------------------------------------------------

/// A gain in dB of the model for a tone (last half second of a second).
fn gain_db(token: &str, set: &[(&str, f32)], hz: f32, amp: f32) -> f32 {
    let n = samples(1.0);
    let x = sine(hz, n, amp);
    let (l, _) = render(token, set, &x);
    20.0 * (rms(&l[n / 2..]) / rms(&x[n / 2..])).log10()
}

#[test]
fn de_esser_reduces_the_sibilant_band_only() {
    let set = [("f_1", 6000.0), ("r_1", 24.0), ("flt_1", 2.0), ("resp_1", 2.0)];
    assert!(gain_db("DLVDEES", &set, 6000.0, 0.3) < -6.0, "sibilance");
    assert!(gain_db("DLVDEES", &set, 300.0, 0.3).abs() < 0.3, "a low tone is left alone");
    // Auto-threshold: at any level the band's reduction is about the same.
    let (loud, quiet) = (gain_db("DLVDEES", &set, 6000.0, 0.5), gain_db("DLVDEES", &set, 6000.0, 0.02));
    assert!((loud - quiet).abs() < 1.5, "{loud} {quiet}");
}

#[test]
fn de_esser_reduction_listen_filter_and_response_act() {
    assert!(gain_db("DLVDEES", &[("f_1", 6000.0), ("r_1", 0.0)], 6000.0, 0.3).abs() < 0.1, "REDUCTION 0");
    assert!(
        gain_db("DLVDEES", &[("f_1", 6000.0), ("r_1", 24.0)], 6000.0, 0.3)
            < gain_db("DLVDEES", &[("f_1", 6000.0), ("r_1", 6.0)], 6000.0, 0.3)
    );
    // Listen: only the band.
    let n = samples(1.0);
    let x: Vec<f32> = sine(300.0, n, 0.3).iter().zip(sine(6000.0, n, 0.2)).map(|(a, b)| a + b).collect();
    let (l, _) = render("DLVDEES", &[("f_1", 6000.0), ("lis_1", 1.0), ("flt_1", 0.0)], &x);
    let t = &l[n / 2..];
    assert!(tone(t, 6000.0) > 10.0 * tone(t, 300.0), "LISTEN");
    // Classic ducks the low tone with the sibilance; Band Pass leaves it.
    let at = |filter: f32, hz: f32| {
        let (l, _) = render("DLVDEES", &[("f_1", 6000.0), ("r_1", 24.0), ("flt_1", filter)], &x);
        tone(&l[n / 2..], hz)
    };
    assert!(at(0.0, 300.0) < 0.8 * at(2.0, 300.0), "CLASSIC vs BAND PASS");
    // Response: Slow takes longer to pull down than Fast.
    let onset = |resp: f32| {
        let (mut dev, v) = device("DLVDEES", &[("f_1", 6000.0), ("r_1", 24.0), ("flt_1", 2.0), ("resp_1", resp)]);
        let m = samples(0.02);
        let (mut l, mut r) = (sine(6000.0, m, 0.3), sine(6000.0, m, 0.3));
        run(&mut dev, &v, &mut l, &mut r);
        rms(&l[m / 2..])
    };
    assert!(onset(0.0) > onset(2.0), "RESPONSE {} {}", onset(0.0), onset(2.0));
}

#[test]
fn de_esser_channels_are_separate_until_linked() {
    let run_lr = |set: &[(&str, f32)]| {
        let (mut dev, v) = device("DLVDEES", set);
        let n = samples(1.0);
        let (mut l, mut r) = (sine(6000.0, n, 0.3), sine(300.0, n, 0.3));
        run(&mut dev, &v, &mut l, &mut r);
        (rms(&l[n / 2..]), rms(&r[n / 2..]))
    };
    let (a, _) = run_lr(&[("link", 0.0), ("f_1", 6000.0), ("r_1", 24.0), ("flt_1", 0.0)]);
    let (_, unlinked) =
        run_lr(&[("link", 0.0), ("f_1", 6000.0), ("r_1", 24.0), ("flt_1", 0.0), ("f_2", 6000.0), ("r_2", 24.0), ("flt_2", 0.0)]);
    let (_, linked) = run_lr(&[("link", 1.0), ("f_1", 6000.0), ("r_1", 24.0), ("flt_1", 0.0)]);
    assert!(a < 0.15, "left reduced");
    assert!(linked < 0.7 * unlinked, "linked: the sibilance on the left ducks the right too: {unlinked} {linked}");
}

#[test]
fn transient_controller_shapes_onsets_and_tails() {
    // A burst that starts hard and decays: the first 5 ms against the rest.
    let burst = |n: usize| -> Vec<f32> {
        (0..n)
            .map(|i| {
                let t = i as f32 / SR;
                let env = if t < 0.15 { (-t * 20.0).exp() } else { 0.0 };
                0.5 * env * (TAU * 1000.0 * t).sin()
            })
            .collect()
    };
    let n = samples(0.4);
    let (plain, _) = render("DLVTRAN", &[], &burst(n));
    let (attack, _) = render("DLVTRAN", &[("att_1", 15.0), ("link", 1.0)], &burst(n));
    let (quiet, _) = render("DLVTRAN", &[("att_1", -15.0), ("link", 1.0)], &burst(n));
    let onset = |x: &[f32]| rms(&x[..samples(0.005)]);
    assert!(onset(&attack) > 1.3 * onset(&plain), "ATTACK +15: {} {}", onset(&plain), onset(&attack));
    assert!(onset(&quiet) < 0.8 * onset(&plain), "ATTACK -15");
    let (more, _) = render("DLVTRAN", &[("sus_1", 24.0), ("link", 1.0)], &burst(n));
    let (less, _) = render("DLVTRAN", &[("sus_1", -24.0), ("link", 1.0)], &burst(n));
    let tail = |x: &[f32]| rms(&x[samples(0.04)..samples(0.14)]);
    assert!(tail(&more) > 1.5 * tail(&plain), "SUSTAIN +24: {} {}", tail(&plain), tail(&more));
    assert!(tail(&less) < 0.67 * tail(&plain), "SUSTAIN -24");
}

#[test]
fn transient_controller_gains_in_modes_and_channels() {
    let n = samples(0.5);
    let tone_x = sine(1000.0, n, 0.25);
    let g = |set: &[(&str, f32)]| {
        let (l, _) = render("DLVTRAN", set, &tone_x);
        20.0 * (rms(&l[n / 2..]) / rms(&tone_x[n / 2..])).log10()
    };
    assert!((g(&[("pre_1", 6.0), ("link", 1.0)]) - 6.0).abs() < 0.3, "PRE GAIN");
    assert!((g(&[("pst_1", -6.0), ("link", 1.0)]) + 6.0).abs() < 0.3, "POST GAIN");
    // In off: bypassed, whatever else is set.
    assert!(g(&[("in_1", 0.0), ("pre_1", 12.0), ("att_1", 15.0), ("link", 1.0)]).abs() < 1e-3, "IN off");
    // The channels are separate until Link makes channel 2 follow 1.
    let run_lr = |set: &[(&str, f32)]| {
        let (mut dev, v) = device("DLVTRAN", set);
        let (mut l, mut r) = (sine(1000.0, n, 0.25), sine(1000.0, n, 0.25));
        run(&mut dev, &v, &mut l, &mut r);
        (rms(&l[n / 2..]), rms(&r[n / 2..]))
    };
    let (l, r) = run_lr(&[("pre_1", 6.0)]);
    assert!(l > 1.5 * r, "unlinked");
    let (l, r) = run_lr(&[("pre_1", 6.0), ("link", 1.0)]);
    assert!((l - r).abs() < 1e-4, "LINK");
    // Transient mode ignores a small slow change; Continuous follows it.
    let wobbly: Vec<f32> = (0..n)
        .map(|i| {
            let t = i as f32 / SR;
            0.25 * (1.0 + 0.2 * (TAU * 4.0 * t).sin()) * (TAU * 1000.0 * t).sin()
        })
        .collect();
    let dev_of = |mode: f32| {
        let (l, _) = render("DLVTRAN", &[("att_1", 15.0), ("sus_1", -24.0), ("mode_1", mode), ("link", 1.0)], &wobbly);
        let (d, _) = render("DLVTRAN", &[("mode_1", mode), ("link", 1.0)], &wobbly);
        let diff: Vec<f32> = l.iter().zip(&d).map(|(a, b)| a - b).collect();
        rms(&diff[n / 2..])
    };
    assert!(dev_of(1.0) > 1.3 * dev_of(0.0), "{} {}", dev_of(0.0), dev_of(1.0));
}

#[test]
fn dynamic_eq_cuts_only_its_band_above_the_threshold() {
    let set = [("f_2", 1000.0), ("w_2", 1.0), ("g_2", -12.0), ("t_2", -30.0), ("m_2", 0.0), ("b_2", 1.0)];
    assert!(gain_db("DLVDYNEQ", &set, 1000.0, 0.3) < -5.0, "over the threshold, in the band");
    assert!(gain_db("DLVDYNEQ", &set, 100.0, 0.3).abs() < 0.5, "outside the band");
    // Under the threshold nothing moves.
    let quiet = [("f_2", 1000.0), ("w_2", 1.0), ("g_2", -12.0), ("t_2", 10.0), ("m_2", 0.0)];
    assert!(gain_db("DLVDYNEQ", &quiet, 1000.0, 0.3).abs() < 0.2);
    // Below: acts when the level is under the threshold.
    let below = [("f_2", 1000.0), ("w_2", 1.0), ("g_2", 12.0), ("t_2", 10.0), ("m_2", 1.0)];
    assert!(gain_db("DLVDYNEQ", &below, 1000.0, 0.05) > 5.0, "boost under the threshold");
    assert!(
        gain_db("DLVDYNEQ", &set, 1000.0, 0.3)
            > gain_db("DLVDYNEQ", &[("f_2", 1000.0), ("w_2", 1.0), ("g_2", -15.0), ("t_2", -30.0)], 1000.0, 0.3) - 1.0
    );
    // Threshold is in dBu with 18 dBu = 0 dBFS: at 0.1 peak (-20 dBFS, -2 dBu) a
    // threshold at +2 dBu doesn't trigger, one at -8 does.
    let t = |thr: f32| gain_db("DLVDYNEQ", &[("f_2", 1000.0), ("w_2", 1.0), ("g_2", -12.0), ("t_2", thr)], 1000.0, 0.1);
    assert!(t(2.0).abs() < 0.3 && t(-8.0) < -1.0, "{} {}", t(2.0), t(-8.0));
}

#[test]
fn dynamic_eq_widest_end_bands_are_shelves() {
    let bell = [("f_1", 200.0), ("w_1", 1.0), ("g_1", -12.0), ("t_1", -40.0), ("b_1", 1.0)];
    let shelf = [("f_1", 200.0), ("w_1", 2.1), ("g_1", -12.0), ("t_1", -40.0), ("b_1", 1.0)];
    assert!(gain_db("DLVDYNEQ", &bell, 40.0, 0.3) > -2.0, "a bell leaves the far lows");
    assert!(gain_db("DLVDYNEQ", &shelf, 40.0, 0.3) < -5.0, "a shelf takes everything below");
    let hi = [("f_4", 5000.0), ("w_4", 2.1), ("g_4", -12.0), ("t_4", -40.0), ("b_4", 1.0)];
    assert!(gain_db("DLVDYNEQ", &hi, 15_000.0, 0.3) < -5.0, "the top band shelves up");
}

#[test]
fn dynamic_eq_trim_ballistics_and_sidechain_act() {
    assert!((gain_db("DLVDYNEQ", &[("trim", 6.0)], 1000.0, 0.1) - 6.0).abs() < 0.2, "TRIM");
    // Fast 9 reaches its depth quicker than Std 9.
    let onset = |b: f32| {
        let (mut dev, v) = device("DLVDYNEQ", &[("f_2", 1000.0), ("w_2", 1.0), ("g_2", -15.0), ("t_2", -40.0), ("b_2", b)]);
        let m = samples(0.012);
        let (mut l, mut r) = (sine(1000.0, m, 0.3), sine(1000.0, m, 0.3));
        run(&mut dev, &v, &mut l, &mut r);
        rms(&l[m / 2..])
    };
    assert!(onset(1.0) < onset(0.0), "BALLISTICS {} {}", onset(0.0), onset(1.0));
    // Cascade: band 2 hears band 1's cut. Parallel: the input as it came, so band 2
    // reduces more when band 1 has already taken some out.
    let chain = [
        ("f_1", 1000.0),
        ("w_1", 1.0),
        ("g_1", -15.0),
        ("t_1", -40.0),
        ("b_1", 1.0),
        ("f_2", 1000.0),
        ("w_2", 1.0),
        ("g_2", -15.0),
        ("t_2", -10.0),
        ("b_2", 1.0),
    ];
    let parallel = gain_db("DLVDYNEQ", &[&chain[..], &[("side", 1.0)]].concat(), 1000.0, 0.3);
    let cascade = gain_db("DLVDYNEQ", &[&chain[..], &[("side", 0.0)]].concat(), 1000.0, 0.3);
    assert!(parallel < cascade - 1.0, "parallel {parallel} cascade {cascade}");
    // Every band of the four is real.
    for b in 1..=3 {
        let ids = [
            format!("f_{}", b + 1),
            format!("w_{}", b + 1),
            format!("g_{}", b + 1),
            format!("t_{}", b + 1),
            format!("b_{}", b + 1),
        ];
        let set: Vec<(&str, f32)> = vec![
            (ids[0].as_str(), 4000.0),
            (ids[1].as_str(), 1.0),
            (ids[2].as_str(), -12.0),
            (ids[3].as_str(), -40.0),
            (ids[4].as_str(), 1.0),
        ];
        assert!(gain_db("DLVDYNEQ", &set, 4000.0, 0.3) < -5.0, "band {}", b + 1);
    }
}

#[test]
fn multibands_are_flat_when_idle_at_every_slope() {
    for (token, slope) in [("DLVMB3", 0.0), ("DLVMB3", 1.0), ("DLVMB3", 2.0), ("DLVMB4", 0.0), ("DLVMB4", 1.0), ("DLVMB4", 2.0)] {
        for hz in [80.0, 700.0, 3000.0, 12_000.0] {
            let mut set = vec![("slope", slope), ("thr_1", 18.0), ("thr_2", 18.0), ("thr_3", 18.0)];
            if token == "DLVMB4" {
                set.push(("thr_4", 18.0));
            }
            let db = gain_db(token, &set, hz, 0.1);
            assert!(db.abs() < 0.3, "{token} slope {slope} at {hz}: {db}");
        }
    }
}

#[test]
fn multiband_compresses_its_band_only() {
    // Band 1 of the 3-band pulled hard: only lows move.
    let set = [("thr_1", -40.0), ("ratio_1", 20.0), ("thr_2", 18.0), ("thr_3", 18.0), ("ball", 0.0), ("att_1", 1.0)];
    assert!(gain_db("DLVMB3", &set, 80.0, 0.3) < -10.0, "the low band");
    assert!(gain_db("DLVMB3", &set, 3000.0, 0.3).abs() < 0.3, "the top band stays");
    // Gain, In.
    assert!((gain_db("DLVMB3", &[("thr_1", 18.0), ("gain_1", 6.0)], 80.0, 0.1) - 6.0).abs() < 0.3, "GAIN");
    assert!(gain_db("DLVMB3", &[("thr_1", -40.0), ("ratio_1", 20.0), ("in_1", 0.0)], 80.0, 0.3).abs() < 0.3, "IN off");
    // Threshold is in dBu with 18 dBu = 0 dBFS.
    let t = |thr: f32| {
        gain_db("DLVMB3", &[("thr_1", thr), ("ratio_1", 20.0), ("thr_2", 18.0), ("thr_3", 18.0), ("att_1", 1.0)], 80.0, 0.1)
    };
    assert!(t(2.0).abs() < 0.5 && t(-14.0) < -5.0, "{} {}", t(2.0), t(-14.0));
    // The 4-band's fourth band is real.
    let four = [
        ("thr_4", -40.0),
        ("ratio_4", 20.0),
        ("att_4", 1.0),
        ("thr_1", 18.0),
        ("thr_2", 18.0),
        ("thr_3", 18.0),
        ("xo_3", 4000.0),
    ];
    assert!(gain_db("DLVMB4", &four, 12_000.0, 0.3) < -10.0, "band 4");
}

#[test]
fn multiband_key_knee_ballistics_and_gang_modes_act() {
    let band2 = [("thr_1", 18.0), ("thr_3", 18.0), ("thr_2", -30.0), ("ratio_2", 20.0), ("att_2", 1.0)];
    // Wide key: band 2 hears the whole signal, so a low tone ducks it.
    let n = samples(1.0);
    let x: Vec<f32> = sine(80.0, n, 0.5).iter().zip(sine(600.0, n, 0.02)).map(|(a, b)| a + b).collect();
    let level = |key: f32| {
        let (l, _) = render("DLVMB3", &[&band2[..], &[("key_2", key), ("xo_2", 8000.0)]].concat(), &x);
        tone(&l[n / 2..], 600.0)
    };
    assert!(level(1.0) < 0.3 * level(0.0), "KEY RANGE {} {}", level(0.0), level(1.0));
    // Knee: soft starts under the threshold.
    let k = |knee: f32| {
        gain_db("DLVMB3", &[("thr_1", 0.0), ("ratio_1", 20.0), ("knee", knee), ("thr_2", 18.0), ("thr_3", 18.0)], 80.0, 0.11)
    };
    assert!(k(1.0) < k(0.0) - 0.1, "KNEE {} {}", k(0.0), k(1.0));
    // Ballistics: Slow's reduction is still building after a Fast one has settled.
    let onset = |b: f32| {
        let (mut dev, v) =
            device("DLVMB3", &[("thr_1", -40.0), ("ratio_1", 20.0), ("thr_2", 18.0), ("thr_3", 18.0), ("ball", b)]);
        let m = samples(0.02);
        let (mut l, mut r) = (sine(80.0, m, 0.3), sine(80.0, m, 0.3));
        run(&mut dev, &v, &mut l, &mut r);
        rms(&l[m / 2..])
    };
    assert!(onset(4.0) > 1.2 * onset(5.0), "BALLISTICS {} {}", onset(4.0), onset(5.0));
    // Link: every band takes band 1's; Rel: band 1's move carries the others.
    let at2 = |link: f32| {
        let set = [
            ("link", link),
            ("thr_1", -40.0),
            ("ratio_1", 20.0),
            ("thr_2", 18.0),
            ("thr_3", 18.0),
            ("att_1", 1.0),
            ("att_2", 1.0),
        ];
        gain_db("DLVMB3", &set, 500.0, 0.3)
    };
    assert!(at2(0.0).abs() < 0.3, "OFF: band 2 has its own threshold");
    assert!(at2(1.0) < -6.0, "LINK: band 2 follows band 1: {}", at2(1.0));
    assert!(at2(2.0) < at2(0.0) - 1.0, "REL: band 1's offset moves band 2: {}", at2(2.0));
    // Crossover frequency moves what's in the band.
    let c = |xo: f32| {
        gain_db(
            "DLVMB3",
            &[("xo_1", xo), ("thr_1", -40.0), ("ratio_1", 20.0), ("thr_2", 18.0), ("thr_3", 18.0), ("att_1", 1.0)],
            500.0,
            0.3,
        )
    };
    assert!(c(2000.0) < c(100.0) - 5.0, "CROSSOVER {} {}", c(100.0), c(2000.0));
}

// ---- Dimension ----------------------------------------------------------------------------

#[test]
fn dimension_chorus_wet_only_and_dry_plus_wet() {
    let (l, _) = impulse("DLVDIMC", &[("mode", 0.0), ("out", 0.0)], 0.1);
    assert!(l[0].abs() < 1e-3, "Wet Only has no dry");
    assert!(peak(&l, 100, l.len()).1 > 0.1, "wet arrives");
    let (l, _) = impulse("DLVDIMC", &[("mode", 0.0), ("out", 1.0)], 0.1);
    assert!(l[0] > 0.3, "Dry+Wet has the dry");
    // E is the deepest: a larger sweep moves a high tone's level further.
    let n = samples(4.0);
    let purity = |mode: f32| {
        let (l, _) = render("DLVDIMC", &[("mode", mode), ("out", 0.0)], &sine(4000.0, n, 0.4));
        let t = &l[n / 4..];
        tone(t, 4000.0) / rms(t)
    };
    assert!(purity(4.0) < purity(0.0), "E vs 1: {} {}", purity(4.0), purity(0.0));
    let differs = |m: f32| {
        let (a, _) = render("DLVDIMC", &[("mode", m)], &sine(1000.0, samples(1.0), 0.4));
        let (b, _) = render("DLVDIMC", &[("mode", m + 1.0)], &sine(1000.0, samples(1.0), 0.4));
        a.iter().zip(&b).map(|(x, y)| (x - y).abs()).sum::<f32>()
    };
    for m in [0.0, 1.0, 2.0, 3.0] {
        assert!(differs(m) > 1.0, "mode {m}");
    }
}
