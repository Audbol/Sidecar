//! The PRO2's processing blocks: their tables against the Owner's Manual, and what
//! the manual says each curve and envelope does.

use livedaw_dsp::midas_pro::{self, fx, DYN_MODELS, EQ4_MODEL, EQ6_MODEL, GEQ_HZ, GEQ_MODEL, KNEES_DB, SC_WIDTH_Q};
use livedaw_dsp::model::{model_index, ModelSpec, ParamKind, Params};
use livedaw_dsp::universal;
use livedaw_dsp::wing::dynamics::{SidechainSettings, WingDynamics, XoverSettings};
use livedaw_dsp::wing::eq::{design, MAX_SECTIONS};
use livedaw_dsp::wing::fx::{create, FxContext};
use livedaw_dsp::wing::{MAX_DYN_PARAMS, MAX_EQ_PARAMS, MAX_FX_PARAMS};
use std::f32::consts::TAU;

const FS: f64 = 48000.0;

fn values(spec: &ModelSpec, set: &[(&str, f32)]) -> Vec<f32> {
    let mut v = vec![0.0; spec.params.len()];
    spec.defaults_into(&mut v);
    for (id, x) in set {
        v[spec.param_index(id).unwrap_or_else(|| panic!("{id}"))] = *x;
    }
    v
}

fn param<'a>(spec: &'a ModelSpec, id: &str) -> &'a livedaw_dsp::model::ParamSpec {
    spec.params.iter().find(|p| p.id == id).unwrap_or_else(|| panic!("{id}"))
}

fn eq(spec: &ModelSpec, set: &[(&str, f32)], f: f64) -> f64 {
    design(spec, &values(spec, set), FS, 1.0).response_db(f, FS)
}

fn eq4(set: &[(&str, f32)], f: f64) -> f64 {
    eq(&EQ4_MODEL, set, f)
}

fn eq6(set: &[(&str, f32)], f: f64) -> f64 {
    eq(&EQ6_MODEL, set, f)
}

// ---- the EQs -------------------------------------------------------------------------

#[test]
fn the_eqs_tables_are_the_manuals() {
    let ends = |spec: &ModelSpec, id: &str| (param(spec, id).lo(), param(spec, id).hi());
    assert_eq!(ends(&EQ4_MODEL, "tf"), (1000.0, 25000.0), "treble");
    assert_eq!(ends(&EQ4_MODEL, "hf"), (320.0, 8000.0), "hi mid");
    assert_eq!(ends(&EQ4_MODEL, "lf"), (80.0, 2000.0), "lo mid");
    assert_eq!(ends(&EQ4_MODEL, "bf"), (16.0, 400.0), "bass");
    for b in ["t", "h", "l", "b"] {
        assert_eq!(ends(&EQ4_MODEL, &format!("{b}g")), (-16.0, 16.0));
        assert_eq!(ends(&EQ4_MODEL, &format!("{b}w")), (0.1, 3.0));
    }
    for b in 1..=6 {
        assert_eq!(ends(&EQ6_MODEL, &format!("{b}f")), (16.0, 25000.0));
        assert_eq!(ends(&EQ6_MODEL, &format!("{b}g")), (-16.0, 16.0));
        assert_eq!(ends(&EQ6_MODEL, &format!("{b}w")), (0.1, 3.0));
    }
    let names = |spec: &ModelSpec, id: &str| match param(spec, id).kind {
        ParamKind::Choice(c) => c.to_vec(),
        _ => panic!("{id}"),
    };
    assert_eq!(names(&EQ4_MODEL, "ts"), ["Bell", "Soft", "Classic", "Bright"]);
    assert_eq!(names(&EQ4_MODEL, "bs"), ["Bell", "Warm", "Classic", "Deep"]);
    assert_eq!(names(&EQ6_MODEL, "1s"), ["Bell", "Warm", "HP 6", "HP 12"]);
    assert_eq!(names(&EQ6_MODEL, "2s"), ["Bell", "HP 24"]);
    assert_eq!(names(&EQ6_MODEL, "6s"), ["Bell", "Soft", "LP 6", "LP 12"]);
    assert_eq!(GEQ_HZ.len(), 31);
    assert_eq!((GEQ_HZ[0], GEQ_HZ[30]), (20.0, 20000.0), "with the DN370's extra 20 Hz band");
    assert_eq!(ends(&GEQ_MODEL, "hf"), (20.0, 500.0));
    assert_eq!(ends(&GEQ_MODEL, "lf"), (2000.0, 20000.0));
    assert_eq!(ends(&GEQ_MODEL, "n1f"), (20.0, 2000.0));
    assert_eq!(ends(&GEQ_MODEL, "n2f"), (200.0, 20000.0));
    assert_eq!(ends(&GEQ_MODEL, "1"), (-12.0, 12.0));
    assert!(GEQ_MODEL.params.len() <= MAX_EQ_PARAMS && EQ6_MODEL.params.len() <= MAX_EQ_PARAMS);
}

#[test]
fn a_bell_reaches_its_gain_and_is_as_wide_as_its_width() {
    // Lo mid at 1 kHz, +12, 1 octave.
    let bell = |w: f32| eq4(&[("lf", 1000.0), ("lg", 12.0), ("lw", w)], 1000.0);
    assert!((bell(1.0) - 12.0).abs() < 0.05);
    assert!((bell(3.0) - 12.0).abs() < 0.05);
    // Half the gain an octave wide: at the band's edges, about half (a bell's 3 dB-points).
    let at_edge = eq4(&[("lf", 1000.0), ("lg", 12.0), ("lw", 1.0)], 1414.0);
    assert!((6.0..9.0).contains(&at_edge), "{at_edge}");
    // Narrower: less at the same place.
    assert!(eq4(&[("lf", 1000.0), ("lg", 12.0), ("lw", 0.3)], 1414.0) < 2.0);
    // Flat, and all the bands at once.
    assert!(eq4(&[], 1000.0).abs() < 1e-6);
    let all = [("tg", 3.0), ("hg", 3.0), ("lg", 3.0), ("bg", 3.0)];
    assert!(eq4(&all, 1000.0).abs() > 0.5);
}

fn treble(shape: f32, g: f32, f: f64) -> f64 {
    eq4(&[("ts", shape), ("tf", 4000.0), ("tg", g)], f)
}

fn bass(shape: f32, g: f32, f: f64) -> f64 {
    eq4(&[("bs", shape), ("bf", 100.0), ("bg", g)], f)
}

/// The span in octaves over which a shelf moves from 10 % to 90 % of its gain.
fn transition_octaves(curve: impl Fn(f64) -> f64, g: f64) -> f64 {
    let mut cross = [0.0f64; 2];
    let mut f = 50.0f64;
    let mut prev = curve(f) / g;
    while f < 20000.0 {
        f *= 1.003;
        let now = curve(f) / g;
        for (i, level) in [0.1, 0.9].into_iter().enumerate() {
            if (prev - level) * (now - level) <= 0.0 && cross[i] == 0.0 {
                cross[i] = f;
            }
        }
        prev = now;
    }
    (cross[1] / cross[0]).log2().abs()
}

#[test]
fn the_shelves_do_what_the_manual_says() {
    // Bell: zero far away.
    assert!(treble(0.0, 12.0, 100.0).abs() < 0.1);
    let (g, far) = (12.0, 16000.0);
    // Each reaches its gain at the top.
    for shape in [1.0, 2.0, 3.0] {
        let r = treble(shape, g as f32, far);
        assert!((r - g).abs() < 1.0, "{shape}: {r}");
    }
    let soft = |f| treble(1.0, 12.0, f);
    let classic = |f| treble(2.0, 12.0, f);
    let bright = |f| treble(3.0, 12.0, f);
    let (ts, tc, tb) = (transition_octaves(soft, g), transition_octaves(classic, g), transition_octaves(bright, g));
    // "A very gentle gradient"; "much steeper"; "slightly steeper than the classic".
    assert!(ts > 1.7 * tc, "soft {ts}, classic {tc}");
    assert!(tb < tc, "bright {tb}, classic {tc}");
    assert!(tb > 0.6 * tc, "only slightly steeper: {tb}, {tc}");
    // Classic undershoots: boosting the treble slightly cuts the mids, and vice versa.
    let sweep = |c: &dyn Fn(f64) -> f64| (0..400).map(|i| c(200.0 * 1.012f64.powi(i))).fold(f64::INFINITY, f64::min);
    let dip = sweep(&classic);
    assert!((-1.6..=-0.3).contains(&dip), "boost's undershoot {dip}");
    let cut_peak = (0..400).map(|i| treble(2.0, -12.0, 200.0 * 1.012f64.powi(i))).fold(f64::NEG_INFINITY, f64::max);
    assert!((0.3..=1.6).contains(&cut_peak), "cut's overshoot {cut_peak}");
    // Bright has none; Soft, the absolute minimum.
    assert!(sweep(&bright) > -0.01, "no undershoot: {}", sweep(&bright));
    assert!(sweep(&soft) > -0.01);
    // The bass mirrors it.
    assert!((bass(2.0, 12.0, 20.0) - 12.0).abs() < 1.2);
    let dip = (0..400).map(|i| bass(2.0, 12.0, 300.0 * 1.012f64.powi(i))).fold(f64::INFINITY, f64::min);
    assert!(dip < -0.3, "bass undershoot {dip}");
    let deep = (0..400).map(|i| bass(3.0, 12.0, 300.0 * 1.012f64.powi(i))).fold(f64::INFINITY, f64::min);
    assert!(deep > -0.01, "deep: {deep}");
}

#[test]
fn the_output_eq_has_its_filters_and_shelves() {
    // Band 1's 6 and 12 dB/oct high pass, an octave and two under the corner.
    let (hp6, hp12) = (eq6(&[("1s", 2.0), ("1f", 400.0)], 100.0), eq6(&[("1s", 3.0), ("1f", 400.0)], 100.0));
    assert!((hp6 + 12.3).abs() < 1.0, "{hp6}");
    assert!((hp12 + 24.0).abs() < 1.0, "{hp12}");
    // Band 2's 24 dB/oct, and band 1 "inaccessible" with it.
    let hp24 = eq6(&[("2s", 1.0), ("2f", 400.0)], 200.0);
    assert!((hp24 + 24.0).abs() < 2.0, "{hp24}");
    let inaccessible = eq6(&[("2s", 1.0), ("2f", 100.0), ("1f", 1000.0), ("1g", 12.0)], 1000.0);
    assert!(inaccessible.abs() < 0.5, "band 1 taken out: {inaccessible}");
    assert!((eq6(&[("2s", 0.0), ("1f", 1000.0), ("1g", 12.0)], 1000.0) - 12.0).abs() < 0.5, "else there");
    // Warm and Soft shelves; band 6's low pass.
    assert!((eq6(&[("1s", 1.0), ("1f", 100.0), ("1g", 12.0)], 20.0) - 12.0).abs() < 1.5);
    assert!((eq6(&[("6s", 1.0), ("6f", 4000.0), ("6g", -12.0)], 20000.0) + 12.0).abs() < 2.0);
    let lp = eq6(&[("6s", 3.0), ("6f", 1000.0)], 4000.0);
    assert!((lp + 24.0).abs() < 2.0, "{lp}");
    // The default is six flat bells.
    assert!(eq6(&[], 1000.0).abs() < 1e-6);
}

#[test]
fn the_geq_covers_its_bands_filters_and_q() {
    let band = |i: usize, g: f32, q: f32, f: f64| {
        let id = (i + 1).to_string();
        eq(&GEQ_MODEL, &[(id.as_str(), g), ("q", q)], f)
    };
    assert!((band(17, 12.0, 0.0, 1000.0) - 12.0).abs() < 0.1, "1 kHz");
    assert!((band(0, -12.0, 1.0, 20.0) + 12.0).abs() < 0.2, "20 Hz");
    // Constant Q keeps its width; proportional gets narrower as the gain falls.
    let width = |q: f32, g: f32| band(17, g, q, 1122.0).abs() / g.abs() as f64;
    assert!(width(0.0, 3.0) < 0.75 * width(1.0, 3.0), "proportional narrower at +3");
    assert!((width(0.0, 12.0) - width(1.0, 12.0)).abs() < 0.03, "the same at full gain");
    assert!(midas_pro::geq_q(true, 0.0) > midas_pro::geq_q(true, 12.0));
    assert_eq!(midas_pro::geq_q(false, 0.0), midas_pro::geq_q(false, 12.0));
    // The filters and the notches.
    assert!(eq(&GEQ_MODEL, &[("hon", 1.0), ("hf", 400.0), ("hs", 0.0)], 100.0) < -10.0);
    assert!(eq(&GEQ_MODEL, &[("hon", 1.0), ("hf", 400.0), ("hs", 1.0)], 100.0) < -20.0);
    assert!(eq(&GEQ_MODEL, &[("n1on", 1.0), ("n1f", 500.0)], 500.0) < -30.0);
    assert!(eq(&GEQ_MODEL, &[("n2on", 1.0), ("n2f", 5000.0)], 5000.0) < -30.0);
    // Everything in at once fits.
    let all: Vec<String> = (1..=31).map(|i| i.to_string()).collect();
    let mut set: Vec<(&str, f32)> = all.iter().map(|s| (s.as_str(), 4.0)).collect();
    set.extend([("hon", 1.0), ("lon", 1.0), ("n1on", 1.0), ("n2on", 1.0)]);
    let d = design(&GEQ_MODEL, &values(&GEQ_MODEL, &set), FS, 1.0);
    assert_eq!(d.len, 31 + 4);
    assert!(d.len <= MAX_SECTIONS);
}

// ---- the dynamics --------------------------------------------------------------------

fn model(t: &str) -> (usize, &'static ModelSpec) {
    let m = model_index(DYN_MODELS, t).unwrap();
    (m, &DYN_MODELS[m])
}

/// The output of `x` through a model.
fn dyn_run_sc(t: &str, set: &[(&str, f32)], x: &mut [f32], sc: SidechainSettings) {
    let (m, spec) = model(t);
    let v = values(spec, set);
    let mut d = WingDynamics::default();
    d.set_table(DYN_MODELS);
    d.prepare(FS);
    d.process(&mut [x], None, m, &v, &sc, &XoverSettings::default(), 1.0, 0.0);
}

fn dyn_run(t: &str, set: &[(&str, f32)], x: &mut [f32]) {
    dyn_run_sc(t, set, x, SidechainSettings::default());
}

/// The gain in dB, sample by sample, of DC `from` until `at` then `to`.
fn dc_gain_trace(t: &str, set: &[(&str, f32)], from: f32, at: usize, to: f32, n: usize) -> Vec<f32> {
    let mut x: Vec<f32> = (0..n).map(|i| if i < at { from } else { to }).collect();
    let input = x.clone();
    dyn_run(t, set, &mut x);
    x.iter().zip(&input).map(|(y, i)| 20.0 * (y / i).abs().log10()).collect()
}

fn gain_at(tail: &[f32]) -> f32 {
    tail.iter().sum::<f32>() / tail.len() as f32
}

/// A hard-kneed, fast compressor on a fixed threshold.
const THR: f32 = -20.0; // dBu: -41 dBFS

#[test]
fn the_dynamics_have_the_manuals_ranges() {
    let (_, cmp) = model("MPCMP");
    let (_, cmpo) = model("MPCMPO");
    let (_, gate) = model("MPGAT");
    let ends = |s: &ModelSpec, id: &str| (param(s, id).lo(), param(s, id).hi());
    for s in [cmp, cmpo] {
        assert_eq!(ends(s, "thr"), (-50.0, 25.0));
        assert_eq!(ends(s, "ratio"), (1.0, 25.0));
        assert_eq!(ends(s, "att"), (0.2, 20.0));
        assert_eq!(ends(s, "rel"), (50.0, 3000.0));
        assert_eq!(ends(s, "gain"), (0.0, 24.0));
        assert!(s.params.len() <= MAX_DYN_PARAMS);
    }
    assert_eq!(KNEES_DB, [4.0, 12.0, 40.0]);
    assert_eq!(ends(gate, "thr"), (-50.0, 25.0));
    assert_eq!(ends(gate, "att"), (0.02, 20.0));
    assert_eq!(ends(gate, "hld"), (5.0, 2000.0));
    assert_eq!(ends(gate, "rel"), (2.0, 2000.0));
    assert_eq!(ends(gate, "range"), (-100.0, 0.0));
    let modes = |s: &ModelSpec| match param(s, "mode").kind {
        ParamKind::Choice(c) => c.to_vec(),
        _ => panic!(),
    };
    assert_eq!(modes(cmp), ["Corrective", "Adaptive", "Creative", "Vintage"], "four on the inputs");
    assert_eq!(modes(cmpo), ["Corrective", "Adaptive", "Creative", "Vintage", "Shimmer"], "five on the outputs");
    assert!(cmp.param_index("clip").is_none() && cmpo.param_index("clip").is_some(), "the outputs' soft clip");
    assert!(midas_pro::is_gate("MPGAT") && !midas_pro::is_gate("MPCMP"));
    assert_eq!(midas_pro::dbfs(21.0), 0.0);
}

#[test]
fn every_mode_compresses_at_the_set_ratio() {
    for mode in 0..4 {
        let set = [("mode", mode as f32), ("thr", THR), ("ratio", 4.0), ("att", 1.0), ("rel", 100.0), ("knee", 0.0)];
        let trace = dc_gain_trace("MPCMP", &set, 0.316, 0, 0.316, 96000);
        // -10 dBFS, 31 dB over -41 dBFS, 4:1: down 23.25 dB (RMS-sensing DC is the same).
        let g = gain_at(&trace[86000..]);
        assert!((g + 23.25).abs() < 0.6, "mode {mode}: {g}");
    }
    let set = [("mode", 4.0), ("thr", THR), ("ratio", 4.0), ("att", 1.0), ("rel", 100.0), ("knee", 0.0)];
    let trace = dc_gain_trace("MPCMPO", &set, 0.316, 0, 0.316, 96000);
    assert!((gain_at(&trace[86000..]) + 23.25).abs() < 0.6, "Shimmer");
    // 1:1 does nothing, and make-up gain comes after.
    let trace = dc_gain_trace("MPCMP", &[("thr", THR), ("ratio", 1.0)], 0.3, 0, 0.3, 4800);
    assert!(gain_at(&trace[4000..]).abs() < 0.05);
    let trace = dc_gain_trace("MPCMP", &[("thr", 25.0), ("gain", 6.0), ("att", 0.2)], 0.1, 0, 0.1, 9600);
    assert!((gain_at(&trace[9000..]) - 6.0).abs() < 0.05);
}

/// Samples from `at` until the trace has moved `frac` of the way to its final value.
fn time_to(trace: &[f32], at: usize, frac: f32) -> usize {
    let (from, to) = (trace[at - 1], trace[trace.len() - 1]);
    let target = from + (to - from) * frac;
    trace[at..].iter().position(|g| if to < from { *g <= target } else { *g >= target }).unwrap_or(trace.len())
}

#[test]
fn corrective_is_exponential_at_the_set_times() {
    let set = [("mode", 0.0), ("thr", THR), ("ratio", 8.0), ("att", 5.0), ("rel", 100.0), ("knee", 0.0)];
    // Level from under the threshold to 30 dB above.
    let trace = dc_gain_trace("MPCMP", &set, 0.0005, 2000, 0.25, 96000);
    let t63 = time_to(&trace, 2000, 0.632) as f32 / 48.0;
    assert!((3.5..7.5).contains(&t63), "attack {t63} ms");
    // The manual's attack and release are the envelope's: release.
    let trace = dc_gain_trace("MPCMP", &set, 0.25, 48000, 0.0005, 96000);
    let t63 = time_to(&trace, 48000, 0.632) as f32 / 48.0;
    assert!((70.0..130.0).contains(&t63), "release {t63} ms");
}

#[test]
fn creative_attacks_in_a_straight_line_and_releases_second_order() {
    // Linear attack: large changes take longer than small ones.
    let base = [("mode", 2.0), ("thr", THR), ("ratio", 25.0), ("att", 5.0), ("rel", 100.0), ("knee", 0.0)];
    let small = dc_gain_trace("MPCMP", &base, 0.0005, 2000, 0.03, 48000); // about 10 dB of reduction
    let large = dc_gain_trace("MPCMP", &base, 0.0005, 2000, 0.3, 48000); // about 30 dB
    let (t_small, t_large) = (time_to(&small, 2000, 0.97) as f32, time_to(&large, 2000, 0.97) as f32);
    assert!((t_large / t_small - 2.0) > 0.3, "the larger step takes longer: {t_small} {t_large}");
    // Straight: halfway there at about half the time.
    let (half, whole) = (time_to(&large, 2000, 0.5) as f32, time_to(&large, 2000, 0.97) as f32);
    assert!((half / whole - 0.5).abs() < 0.1, "{half} {whole}");
    // Second order release: it leaves slower than an exponential at the start.
    let trace = dc_gain_trace("MPCMP", &base, 0.3, 48000, 0.0005, 96000);
    let exp = dc_gain_trace(
        "MPCMP",
        &[("mode", 0.0), ("thr", THR), ("ratio", 25.0), ("att", 5.0), ("rel", 100.0), ("knee", 0.0)],
        0.3,
        48000,
        0.0005,
        96000,
    );
    let (early_2nd, early_exp) = (trace[48480] - trace[47999], exp[48480] - exp[47999]);
    assert!(early_2nd < 0.5 * early_exp, "{early_2nd} {early_exp}");
}

#[test]
fn adaptive_senses_rms_and_vintage_waits_at_the_onset() {
    // A tone: RMS 3 dB under its peak.
    let tone = |a: f32| -> Vec<f32> { (0..96000).map(|i| a * (TAU * 1000.0 * i as f32 / 48000.0).sin()).collect() };
    let run = |mode: f32, a: f32| {
        let mut x = tone(a);
        dyn_run("MPCMP", &[("mode", mode), ("thr", THR), ("ratio", 4.0), ("att", 1.0), ("rel", 100.0), ("knee", 0.0)], &mut x);
        x[90000..].iter().map(|v| v.abs()).fold(0.0, f32::max)
    };
    let (peak_mode, rms_mode) = (run(0.0, 0.3), run(1.0, 0.3));
    assert!(rms_mode > peak_mode * 1.15, "RMS reduction less: {peak_mode} {rms_mode}");
    // Vintage: at a shallow reduction the attack is its own, whatever ATTACK says ...
    let v = |att: f32, level: f32, t: f32| {
        let trace = dc_gain_trace(
            "MPCMP",
            &[("mode", 3.0), ("thr", THR), ("ratio", 4.0), ("att", att), ("rel", 100.0), ("knee", 0.0)],
            0.0005,
            2000,
            level,
            96000,
        );
        time_to(&trace, 2000, t) as f32
    };
    // ...about 3 dB of reduction: 4 dB over at 4:1 (-37 dBFS).
    let shallow = 10f32.powf(-37.0 / 20.0);
    let (fast, slow) = (v(0.2, shallow, 0.632), v(20.0, shallow, 0.632));
    assert!((fast - slow).abs() / slow.max(1.0) < 0.5, "onset independent of ATTACK: {fast} {slow}");
    // ... and driven harder it gets back to the setting.
    let (fast, slow) = (v(0.2, 0.5, 0.632), v(20.0, 0.5, 0.632));
    assert!(fast < 0.5 * slow, "driven: {fast} {slow}");
}

#[test]
fn vintage_adds_harmonics_and_a_soft_knee_slows_the_attack() {
    // Third harmonic of a compressed tone, Vintage against Corrective.
    let third = |mode: f32| {
        let mut x: Vec<f32> = (0..96000).map(|i| 0.5 * (TAU * 1000.0 * i as f32 / 48000.0).sin()).collect();
        dyn_run(
            "MPCMP",
            &[("mode", mode), ("thr", THR), ("ratio", 8.0), ("att", 1.0), ("rel", 200.0), ("knee", 0.0), ("gain", 12.0)],
            &mut x,
        );
        let n = 4800;
        let (mut a, mut b) = (0.0f32, 0.0f32);
        for (i, v) in x[96000 - n..].iter().enumerate() {
            let t = TAU * 3000.0 * i as f32 / 48000.0;
            a += v * t.cos();
            b += v * t.sin();
        }
        a.hypot(b) / n as f32
    };
    assert!(third(3.0) > 3.0 * third(0.0), "{} {}", third(3.0), third(0.0));
    // The knee: at the threshold, soft reduces more than hard (the rounded curve).
    let reduction = |knee: f32| {
        let t = 10f32.powf(-41.0 / 20.0);
        let tr =
            dc_gain_trace("MPCMP", &[("mode", 0.0), ("thr", THR), ("ratio", 8.0), ("att", 1.0), ("knee", knee)], t, 0, t, 96000);
        -gain_at(&tr[90000..])
    };
    let (hard, med, soft) = (reduction(0.0), reduction(1.0), reduction(2.0));
    assert!(hard < med && med < soft, "{hard} {med} {soft}");
    // "Soft knee typically slows down attack speed on signals in the knee area."
    let attack = |knee: f32| {
        // 6 dB over the threshold: past the hard knee, still in the soft one.
        let level = 10f32.powf(-35.0 / 20.0);
        let tr = dc_gain_trace(
            "MPCMP",
            &[("mode", 0.0), ("thr", THR), ("ratio", 8.0), ("att", 5.0), ("knee", knee)],
            0.0001,
            2000,
            level,
            96000,
        );
        time_to(&tr, 2000, 0.632)
    };
    assert!(attack(2.0) as f32 > 1.3 * attack(0.0) as f32, "{} {}", attack(2.0), attack(0.0));
}

#[test]
fn shimmer_overshoots_and_settles() {
    let set = [("mode", 4.0), ("thr", THR), ("ratio", 8.0), ("att", 10.0), ("rel", 100.0), ("knee", 0.0)];
    let trace = dc_gain_trace("MPCMPO", &set, 0.0005, 2000, 0.25, 96000);
    let settled = gain_at(&trace[90000..]);
    let low = trace[2000..48000].iter().copied().fold(0.0f32, f32::min);
    assert!(low < settled - 1.0, "overshoot: {low} past {settled}");
    // And for the same settings a Corrective doesn't.
    let c = dc_gain_trace(
        "MPCMPO",
        &[("mode", 0.0), ("thr", THR), ("ratio", 8.0), ("att", 10.0), ("rel", 100.0), ("knee", 0.0)],
        0.0005,
        2000,
        0.25,
        96000,
    );
    let low = c[2000..48000].iter().copied().fold(0.0f32, f32::min);
    assert!(low > gain_at(&c[90000..]) - 0.3, "{low}");
}

#[test]
fn the_soft_clip_comes_after_the_make_up_gain() {
    let (_, cmpo) = model("MPCMPO");
    // Off: nothing. On at 0 dBu (0.089 FS): under it, nothing; far over, at most +6 dB.
    let (m, _) = (model("MPCMPO").0, ());
    let run = |set: &[(&str, f32)], x: f32| {
        let v = values(cmpo, set);
        let mut buf = vec![x; 96000];
        let mut d = WingDynamics::default();
        d.set_table(DYN_MODELS);
        d.prepare(FS);
        d.process(&mut [&mut buf[..]], None, m, &v, &SidechainSettings::default(), &XoverSettings::default(), 1.0, 0.0);
        buf[90000]
    };
    let free = [("thr", 25.0), ("ratio", 1.0)];
    assert!((run(&free, 0.5) - 0.5).abs() < 1e-3, "off");
    let on = [("thr", 25.0), ("ratio", 1.0), ("clip", 1.0), ("clipl", 0.0)];
    let t = 10f32.powf(-21.0 / 20.0);
    assert!((run(&on, 0.5 * t) - 0.5 * t).abs() < 1e-3, "under the clip level");
    let y = run(&on, 8.0);
    assert!(y > t && y <= 2.1 * t, "over it: {y}");
    assert!((run(&on, 2.0 * t) - t * 1.76).abs() < 0.2 * t, "progressive");
}

#[test]
fn the_gate_keeps_to_its_range_hold_and_hysteresis() {
    let open_db = -30.0 + 0.0; // dBu
    let fs_db = |dbu: f32| 10f32.powf((dbu - 21.0) / 20.0);
    let tail = |x: &mut Vec<f32>| x[x.len() - 1000..].iter().map(|v| v.abs()).fold(0.0, f32::max);
    let set = [("thr", open_db), ("range", -40.0), ("hld", 5.0), ("rel", 2.0)];
    // Well under the threshold: down by RANGE.
    let quiet = fs_db(-70.0);
    let mut x: Vec<f32> = vec![quiet; 48000];
    dyn_run("MPGAT", &set, &mut x);
    assert!((tail(&mut x) / quiet - 0.01).abs() < 0.003, "{}", tail(&mut x) / quiet);
    // Over it: open.
    let mut x: Vec<f32> = vec![fs_db(-20.0); 48000];
    dyn_run("MPGAT", &set, &mut x);
    assert!((tail(&mut x) / fs_db(-20.0) - 1.0).abs() < 0.01);
    // Hysteresis: once open, it stays open a little under the threshold.
    let mut x: Vec<f32> = (0..96000).map(|i| if i < 24000 { fs_db(-25.0) } else { fs_db(-32.0) }).collect();
    dyn_run("MPGAT", &set, &mut x);
    assert!((tail(&mut x) / fs_db(-32.0) - 1.0).abs() < 0.02, "held open by the hysteresis");
    // Hold: open for its time before closing.
    let mut x: Vec<f32> = (0..96000).map(|i| if i < 24000 { 0.1 } else { 0.0001 }).collect();
    dyn_run("MPGAT", &[("thr", open_db), ("range", -60.0), ("hld", 200.0), ("rel", 2.0)], &mut x);
    assert!(x[24000 + 4800].abs() > 0.00009, "holding");
    assert!(x[95000].abs() < 0.00001, "closed after");
}

#[test]
fn the_side_chain_filter_is_a_band_pass_of_its_three_widths() {
    let bw = |q: f32| 1.0 / q;
    assert_eq!(SC_WIDTH_Q.len(), 3);
    assert!(bw(SC_WIDTH_Q[0]) < bw(SC_WIDTH_Q[1]) && bw(SC_WIDTH_Q[1]) < bw(SC_WIDTH_Q[2]));
    // With it in, a tone outside the band hardly compresses.
    let run = |freq: f32, q: f32| {
        let mut x: Vec<f32> = (0..96000).map(|i| 0.5 * (TAU * freq * i as f32 / 48000.0).sin()).collect();
        let sc = SidechainSettings { filter: 3, freq: 1000.0, q };
        dyn_run_sc(
            "MPCMP",
            &[("mode", 0.0), ("thr", THR), ("ratio", 8.0), ("att", 1.0), ("rel", 100.0), ("knee", 0.0)],
            &mut x,
            sc,
        );
        x[90000..].iter().map(|v| v.abs()).fold(0.0, f32::max)
    };
    assert!(run(1000.0, SC_WIDTH_Q[0]) < 0.05, "in band: {}", run(1000.0, SC_WIDTH_Q[0]));
    let outside = run(100.0, SC_WIDTH_Q[0]);
    assert!(outside > 3.0 * run(1000.0, SC_WIDTH_Q[0]), "outside: {outside}");
}

// ---- the effects ---------------------------------------------------------------------

fn device(t: &str, set: &[(&str, f32)]) -> (Box<dyn livedaw_dsp::wing::fx::FxAlgo>, Vec<f32>, &'static ModelSpec) {
    let m = fx::FX_MODELS.iter().position(|m| m.token == t).unwrap();
    let spec = &fx::FX_MODELS[m];
    let dev = create(t, FxContext { sample_rate: FS as f32, max_block: 256 }).unwrap();
    (dev, values(spec, set), spec)
}

/// Runs an impulse (and silence) through a device, in blocks; the two outputs.
fn fx_run(t: &str, set: &[(&str, f32)], mut l: Vec<f32>, mut r: Vec<f32>) -> (Vec<f32>, Vec<f32>) {
    let (mut dev, v, spec) = device(t, set);
    let p = Params::new(spec, &v);
    dev.set_params(&p);
    let mut at = 0;
    while at < l.len() {
        let n = (l.len() - at).min(256);
        dev.process(&mut l[at..at + n], &mut r[at..at + n]);
        at += n;
    }
    (l, r)
}

fn impulse_fx(t: &str, set: &[(&str, f32)], n: usize) -> (Vec<f32>, Vec<f32>) {
    let (mut l, mut r) = (vec![0.0; n], vec![0.0; n]);
    l[0] = 1.0;
    r[0] = 1.0;
    fx_run(t, set, l, r)
}

fn first_over(x: &[f32], level: f32) -> usize {
    x.iter().position(|v| v.abs() > level).unwrap_or(x.len())
}

#[test]
fn the_effect_tables_are_the_manuals() {
    assert_eq!(&fx::FX_MODELS[0].token[..2], "NO", "NONE first");
    assert_eq!(fx::FX_MODELS.len(), 9);
    assert!(fx::FX_MODELS.iter().all(|m| m.params.len() <= MAX_FX_PARAMS));
    let ends = |t: &str, id: &str| {
        let s = fx::FX_MODELS.iter().find(|m| m.token == t).unwrap();
        (param(s, id).lo(), param(s, id).hi())
    };
    // Delay.
    assert_eq!(ends("MPDLY", "fl"), (-100.0, 100.0));
    assert_eq!(ends("MPDLY", "ll"), (-60.0, 10.0));
    assert_eq!(ends("MPDLY", "rt"), (0.001, 10.0));
    assert_eq!(ends("MPDLY", "tmp"), (60.0, 240.0));
    assert_eq!(ends("MPDLY", "hi"), (-12.0, 12.0));
    // DN780: from 8 to 90 metres, 0.1 to 18 s, keys +/-7.
    assert_eq!(ends("MPDN780", "size"), (8.0, 90.0));
    assert_eq!(ends("MPDN780", "dec"), (0.1, 18.0));
    assert_eq!(ends("MPDN780", "lf"), (-7.0, 7.0));
    assert_eq!(ends("MPDN780", "in"), (-4.0, 6.0));
    assert_eq!(fx::DN780_PROGRAMS.len(), 12);
    // Flanger.
    assert_eq!(ends("MPFLG", "dly"), (0.1, 10.0));
    assert_eq!(ends("MPFLG", "rt"), (0.01, 50.0));
    assert_eq!(ends("MPFLG", "hf"), (1000.0, 20000.0));
    assert_eq!(ends("MPFLG", "lf"), (20.0, 1000.0));
    assert_eq!(ends("MPFLG", "spr"), (0.0, 180.0));
    // Phaser.
    assert_eq!(ends("MPPHS", "r1"), (0.0, 40.0));
    assert_eq!(ends("MPPHS", "m1"), (500.0, 24000.0));
    assert_eq!(ends("MPPHS", "gain"), (-20.0, 20.0));
    // Pitch.
    assert_eq!(ends("MPPIT", "c1"), (-12.0, 12.0));
    assert_eq!(ends("MPPIT", "f1"), (-100.0, 100.0));
    assert_eq!(ends("MPPIT", "p1"), (1.0, 500.0));
    assert_eq!(ends("MPPIT", "l1"), (-40.0, -10.0));
    assert_eq!(ends("MPPIT", "hf"), (2000.0, 20000.0));
    assert_eq!(ends("MPPIT", "lf"), (20.0, 400.0));
    // 3-band.
    assert_eq!(ends("MP3BC", "LT"), (-50.0, 25.0));
    assert_eq!(ends("MP3BC", "LR"), (1.0, 25.0));
    assert_eq!(ends("MP3BC", "LG"), (0.0, 24.0));
    assert_eq!(ends("MP3BC", "LL"), (50.0, 3000.0));
    assert_eq!(ends("MP3BC", "LA"), (0.2, 50.0));
    assert_eq!(ends("MP3BC", "kn"), (4.0, 40.0));
    assert_eq!(ends("MP3BC", "x1"), (40.0, 1000.0));
    assert_eq!(ends("MP3BC", "x2"), (640.0, 16000.0));
    // Dynamic EQ: ratio in both directions.
    assert_eq!(ends("MPDEQ", "r1"), (-100.0, 100.0));
}

#[test]
fn the_delay_runs_its_modes() {
    let (l, r) = impulse_fx("MPDLY", &[("dl", 100.0), ("dr", 100.0), ("fl", 0.0), ("fr", 0.0), ("mix", 100.0)], 48000);
    let want = 4800;
    assert!((first_over(&l, 0.1) as i32 - want).abs() < 4, "{}", first_over(&l, 0.1));
    assert!((first_over(&r, 0.1) as i32 - want).abs() < 4);
    // Its range clamps: 10-200 ms holds 1600 ms at 200.
    let (l, _) = impulse_fx("MPDLY", &[("rng", 1.0), ("dl", 1600.0), ("mix", 100.0), ("fl", 0.0), ("fr", 0.0)], 96000);
    assert!((first_over(&l, 0.1) as i32 - 9600).abs() < 8, "{}", first_over(&l, 0.1));
    // Ping-pong crosses: the first repeat left, the next right.
    let (l, r) = impulse_fx(
        "MPDLY",
        &[("mode", 2.0), ("rng", 1.0), ("dl", 50.0), ("dr", 50.0), ("fl", 80.0), ("fr", 80.0), ("mix", 100.0)],
        24000,
    );
    assert!(l[2400 - 3..2400 + 3].iter().any(|v| v.abs() > 0.1), "L first");
    assert!(r[2400 - 3..2400 + 3].iter().all(|v| v.abs() < 0.05), "R not yet");
    assert!(r[4800 - 4..4800 + 4].iter().any(|v| v.abs() > 0.1), "R second");
    // BPM Sync: a beat at 150 bpm is 400 ms.
    let (l, _) =
        impulse_fx("MPDLY", &[("bpm", 1.0), ("tmp", 150.0), ("nl", 5.0), ("fl", 0.0), ("fr", 0.0), ("mix", 100.0)], 48000);
    assert!((first_over(&l, 0.1) as i32 - 19200).abs() < 20, "{}", first_over(&l, 0.1));
}

fn energy(x: &[f32]) -> f32 {
    x.iter().map(|v| v * v).sum()
}

#[test]
fn the_dn780_reverbs_and_its_programs() {
    let n = 48000 * 6;
    // A Hall of decay T: 60 dB down by about T.
    let decay = 1.6;
    let (l, _) = impulse_fx("MPDN780", &[("pre", 10.0), ("dec", decay), ("hf", 0.0), ("size", 30.0)], n);
    assert!(energy(&l) > 0.001);
    let burst_end = 24000;
    let mut tone: Vec<f32> = (0..n).map(|i| 0.5 * (TAU * 1000.0 * i as f32 / 48000.0).sin()).collect();
    tone[burst_end..].iter_mut().for_each(|v| *v = 0.0);
    let (l, _) = fx_run("MPDN780", &[("pre", 10.0), ("dec", decay), ("hf", 0.0), ("size", 30.0)], tone.clone(), tone);
    let window = 4800;
    let (start, t_end) = (burst_end + 480, burst_end + (decay * 48000.0) as usize);
    let drop = 10.0 * (energy(&l[t_end..t_end + window]) / energy(&l[start..start + window])).log10();
    assert!((-75.0..-45.0).contains(&drop), "{drop} dB after the decay time");
    // Program by program.
    let hold = |alg: usize, set: &[(&str, f32)], tail_from: usize| {
        let mut s = vec![("alg", alg as f32)];
        s.extend_from_slice(set);
        impulse_fx("MPDN780", &s, 48000 * 3).0[tail_from..].iter().map(|v| v.abs()).fold(0.0, f32::max)
    };
    // Delay: a single repeat at PRE DELAY.
    let (l, _) = impulse_fx("MPDN780", &[("alg", 7.0), ("pre", 200.0)], 48000);
    assert!((first_over(&l, 0.5) as i32 - 9600).abs() < 2, "{}", first_over(&l, 0.5));
    // ADT.
    let (l, r) = impulse_fx("MPDN780", &[("alg", 8.0), ("pre", 40.0), ("pat", 1.0)], 48000);
    assert!(energy(&l) > 0.1 && energy(&r) > 0.1, "two voices, one each side");
    // Multi-Tap: repeats on regeneration.
    assert!(hold(9, &[("pre", 100.0), ("pat", 4.0), ("dec", 10.0)], 2400) > 0.05);
    // Sound-On-Sound: quiet with LEVEL 0, then a loop that holds.
    assert!(hold(10, &[("pre", 100.0), ("lvl", 0.0), ("dec", 18.0)], 2400) < 1e-6, "LEVEL 0");
    assert!(hold(10, &[("pre", 100.0), ("lvl", 9.0), ("dec", 18.0)], 96000) > 0.3, "the loop holds");
    // Infinite Room holds after LEVEL takes it in.
    assert!(hold(11, &[("lvl", 9.0)], 96000) > 1e-3);
    assert!(hold(3, &[("dec", 0.2), ("lvl", 9.0)], 96000) < 1e-5, "a room dies");
    // Alive and friends come and go.
    assert!(hold(4, &[("dec", 0.5)], 48000) < 1e-5, "Alive gone");
    let (l, _) = impulse_fx("MPDN780", &[("alg", 6.0), ("pat", 9.0), ("dec", 0.5)], 48000);
    let (rise, fall) = (energy(&l[..4000]), energy(&l[4000..9600]));
    assert!(fall > rise, "Reverse rises: {rise} {fall}");
    // Muted input, muted reverb.
    let silence = impulse_fx("MPDN780", &[("imt", 1.0)], 24000).0;
    assert!(energy(&silence[100..]) < 1e-9, "input mute");
}

#[test]
fn the_pitch_shifter_shifts() {
    let n = 48000;
    let tone: Vec<f32> = (0..n).map(|i| 0.3 * (TAU * 440.0 * i as f32 / 48000.0).sin()).collect();
    let (l, _) = fx_run(
        "MPPIT",
        &[
            ("c1", 12.0),
            ("f1", 0.0),
            ("c2", 12.0),
            ("f2", 0.0),
            ("mix", 100.0),
            ("l1", -10.0),
            ("l2", -10.0),
            ("b1", 0.0),
            ("b2", 0.0),
            ("n1", -100.0),
            ("n2", -100.0),
            ("p1", 1.0),
            ("p2", 1.0),
        ],
        tone.clone(),
        tone,
    );
    // Zero crossings in the tail: twice the frequency.
    let tail = &l[24000..];
    let crossings = tail.windows(2).filter(|w| w[0] <= 0.0 && w[1] > 0.0).count();
    let hz = crossings as f32 / (tail.len() as f32 / 48000.0);
    assert!((hz - 880.0).abs() < 60.0, "{hz}");
    assert!(energy(tail) > 0.01);
}

#[test]
fn the_flanger_and_phaser_pass_dry_at_no_mix_and_comb_at_half() {
    let tone: Vec<f32> = (0..24000).map(|i| 0.5 * (TAU * 1000.0 * i as f32 / 48000.0).sin()).collect();
    let (l, _) = fx_run("MPFLG", &[("mix", 0.0)], tone.clone(), tone.clone());
    assert!(l.iter().zip(&tone).all(|(a, b)| (a - b).abs() < 1e-5), "flanger dry");
    let (l, _) = fx_run("MPPHS", &[("bld", 0.0)], tone.clone(), tone.clone());
    assert!(l.iter().zip(&tone).all(|(a, b)| (a - b).abs() < 1e-5), "phaser dry");
    // Static phaser (no sweep, four stages): the notch sits where the all-passes give
    // 180 degrees, between the tones.
    let response = |f: f32| {
        let t: Vec<f32> = (0..24000).map(|i| 0.5 * (TAU * f * i as f32 / 48000.0).sin()).collect();
        let (l, _) = fx_run("MPPHS", &[("dep", 0.0), ("r1", 0.0), ("m1", 2000.0), ("bld", 50.0)], t.clone(), t);
        l[12000..].iter().map(|v| v.abs()).fold(0.0, f32::max)
    };
    let (notch, away) = (response(828.0), response(100.0));
    assert!(notch < 0.1 * away, "notch {notch} against {away}");
}

#[test]
fn the_three_band_sums_to_its_input_and_works_each_band() {
    let (x_l, x_r): (Vec<f32>, Vec<f32>) = (0..48000)
        .map(|i| {
            let t = i as f32 / 48000.0;
            (0.2 * (TAU * 110.0 * t).sin() + 0.2 * (TAU * 5000.0 * t).sin(), 0.3 * (TAU * 700.0 * t).sin())
        })
        .unzip();
    // Nothing set (1:1): the three bands sum to the input (OM p.80: "coherent band summing").
    let flat = [("LR", 1.0), ("MR", 1.0), ("HR", 1.0)];
    let (l, r) = fx_run("MP3BC", &flat, x_l.clone(), x_r.clone());
    let rms = |x: &[f32]| (energy(&x[4800..]) / (x.len() - 4800) as f32).sqrt();
    assert!((rms(&l) / rms(&x_l) - 1.0).abs() < 0.02, "{} {}", rms(&l), rms(&x_l));
    assert!((rms(&r) / rms(&x_r) - 1.0).abs() < 0.02);
    // The low band's compressor leaves the high band alone.
    let tone = |f: f32| -> Vec<f32> { (0..48000).map(|i| 0.5 * (TAU * f * i as f32 / 48000.0).sin()).collect() };
    let low_on = [("LR", 10.0), ("LT", -30.0), ("MR", 1.0), ("HR", 1.0)];
    let level = |x: Vec<f32>| x[40000..].iter().map(|v| v.abs()).fold(0.0, f32::max);
    assert!(level(fx_run("MP3BC", &low_on, tone(60.0), tone(60.0)).0) < 0.2, "low comes down");
    assert!(rms(&fx_run("MP3BC", &low_on, tone(7900.0), tone(7900.0)).0) > 0.33, "high untouched");
}

#[test]
fn the_dynamic_eq_cuts_boosts_and_stays_static_when_fixed() {
    let tone = |f: f32, a: f32| -> Vec<f32> { (0..48000).map(|i| a * (TAU * f * i as f32 / 48000.0).sin()).collect() };
    let level = |x: Vec<f32>| x[40000..].iter().map(|v| v.abs()).fold(0.0, f32::max);
    let on = [("in1", 1.0), ("f1", 1000.0), ("w1", 1.0), ("t1", -40.0), ("r1", -100.0), ("g1", 12.0)];
    // Compressing above the threshold: a tone in the band comes down by the limit.
    let cut = level(fx_run("MPDEQ", &on, tone(1000.0, 0.5), tone(1000.0, 0.5)).0);
    assert!((cut / 0.5 - 0.25).abs() < 0.06, "{}", cut / 0.5);
    // One outside the band is left alone.
    assert!((level(fx_run("MPDEQ", &on, tone(200.0, 0.5), tone(200.0, 0.5)).0) - 0.5).abs() < 0.05);
    // Off: dry.
    assert!((level(fx_run("MPDEQ", &[("f1", 1000.0)], tone(1000.0, 0.5), tone(1000.0, 0.5)).0) - 0.5).abs() < 0.01);
    // Expansion above the threshold boosts (ratio to the right), to the limit.
    let up = [("in1", 1.0), ("f1", 1000.0), ("t1", -40.0), ("r1", 100.0), ("g1", 6.0)];
    assert!((level(fx_run("MPDEQ", &up, tone(1000.0, 0.25), tone(1000.0, 0.25)).0) / 0.25 - 2.0).abs() < 0.3);
    // BELOW with compression lifts a quiet signal, and FIXED makes it a static EQ at the limit.
    let lift = [("in1", 1.0), ("f1", 1000.0), ("t1", -10.0), ("r1", -100.0), ("be1", 1.0), ("g1", 6.0)];
    assert!(level(fx_run("MPDEQ", &lift, tone(1000.0, 0.05), tone(1000.0, 0.05)).0) > 0.08);
    let fixed = [("in1", 1.0), ("f1", 1000.0), ("t1", 0.0), ("r1", -100.0), ("fx1", 1.0), ("g1", 6.0)];
    assert!((level(fx_run("MPDEQ", &fixed, tone(1000.0, 0.5), tone(1000.0, 0.5)).0) / 0.5 - 0.5).abs() < 0.1);
}

#[test]
fn everything_is_offered_on_universal_strips() {
    for m in DYN_MODELS {
        assert!(universal::DYN_MODELS.iter().any(|u| u.token == m.token), "{}", m.token);
    }
    for t in ["MPEQ4", "MPEQ6", "MPGEQ"] {
        assert!(universal::EQ_MODELS_CH.iter().any(|m| m.token == t), "{t}");
        assert!(universal::EQ_MODELS_BUS.iter().any(|m| m.token == t), "{t}");
    }
    for m in &fx::FX_MODELS[2..] {
        assert!(universal::FX_MODELS.iter().any(|u| u.token == m.token), "{}", m.token);
    }
    assert_eq!(universal::console_of("MPPHS"), Some("PRO2"));
}
