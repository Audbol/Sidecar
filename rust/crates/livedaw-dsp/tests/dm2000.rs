//! The DM2000's processing blocks: their parameter tables against the V2
//! Owner's Manual, their defaults and their basic response.

use livedaw_dsp::dm2000::{self, fx, EQ_CHANNEL, EQ_MODEL, FREQS, GEQ_HZ, GEQ_MODEL, HIGH_Q, LOW_Q, MID_Q};
use livedaw_dsp::model::{model_index, ModelSpec, ParamKind, Params};
use livedaw_dsp::universal;
use livedaw_dsp::wing::dynamics::{SidechainSettings, WingDynamics, XoverSettings};
use livedaw_dsp::wing::eq::design;
use livedaw_dsp::wing::fx::{allowed_in, create, FxContext};
use livedaw_dsp::wing::{MAX_DYN_PARAMS, MAX_EQ_PARAMS, MAX_FX_PARAMS};
use livedaw_dsp::y02r::Q_VALUES;
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

fn index(table: &[f32], v: f32) -> f32 {
    table.iter().enumerate().min_by(|a, b| (a.1 - v).abs().total_cmp(&(b.1 - v).abs())).unwrap().0 as f32
}

fn eq_response(set: &[(&str, f32)], f: f64) -> f64 {
    design(&EQ_MODEL, &values(&EQ_MODEL, set), FS, 1.0).response_db(f, FS)
}

fn freq(hz: f32) -> f32 {
    index(&FREQS, hz)
}

/// Every frequency the 40 EQ presets use (pp.332-333).
const PRESET_FREQS: &[f32] = &[
    35.5, 67.0, 75.0, 80.0, 85.0, 90.0, 95.0, 100.0, 106.0, 112.0, 118.0, 125.0, 132.0, 140.0, 170.0, 180.0, 190.0, 212.0, 224.0,
    236.0, 265.0, 300.0, 315.0, 335.0, 355.0, 400.0, 425.0, 450.0, 475.0, 560.0, 600.0, 670.0, 710.0, 750.0, 850.0, 950.0,
    1000.0, 1060.0, 1120.0, 1320.0, 1800.0, 1900.0, 2000.0, 2120.0, 2240.0, 2360.0, 2500.0, 2650.0, 2800.0, 3150.0, 3350.0,
    3550.0, 4000.0, 4250.0, 4500.0, 5000.0, 5300.0, 5600.0, 6000.0, 6300.0, 6700.0, 7500.0, 10000.0, 12500.0, 13200.0, 15000.0,
    16000.0, 17000.0, 18000.0, 19000.0, 20000.0,
];

/// Every Q the 40 EQ presets use.
const PRESET_QS: &[f32] = &[
    10.0, 9.0, 8.0, 7.0, 6.3, 5.6, 5.0, 4.5, 4.0, 3.5, 2.8, 2.2, 2.0, 1.8, 1.6, 1.4, 1.2, 1.0, 0.9, 0.7, 0.63, 0.56, 0.5, 0.45,
    0.4, 0.28, 0.2, 0.18, 0.16, 0.14, 0.12, 0.11, 0.1,
];

#[test]
fn the_eqs_tables_are_the_manuals() {
    assert_eq!(FREQS.len(), 120, "120 frequencies");
    assert_eq!((FREQS[0], FREQS[119]), (21.2, 20000.0));
    assert!(FREQS.windows(2).all(|w| w[1] > w[0] && w[1] / w[0] < 1.075 && w[1] / w[0] > 1.03), "1/12 octave steps");
    for &f in PRESET_FREQS {
        assert!(FREQS.contains(&f), "preset frequency {f} isn't on the table");
    }
    for &q in PRESET_QS {
        assert!(Q_VALUES.contains(&q), "preset Q {q}");
    }
    assert_eq!((MID_Q.len(), LOW_Q.len(), HIGH_Q.len()), (41, 43, 43));
    assert_eq!((LOW_Q[0], LOW_Q[41], LOW_Q[42]), ("HPF", "0.10", "L.SHELF"));
    assert_eq!((HIGH_Q[0], HIGH_Q[1], HIGH_Q[42]), ("LPF", "10.0", "H.SHELF"));
    let g = EQ_MODEL.params.iter().find(|p| p.id == "lmg").unwrap();
    assert_eq!((g.lo(), g.hi(), g.steps), (-18.0, 18.0, 361));
}

#[test]
fn the_eqs_initial_data_is_flat() {
    let v = values(&EQ_MODEL, &[]);
    let p = Params::new(&EQ_MODEL, &v);
    assert_eq!(p.text("type"), "TYPE I");
    assert_eq!(
        (p.f("lf"), p.text("lq"), p.f("lmf"), p.text("lmq"), p.f("hmf"), p.text("hmq"), p.f("hf"), p.text("hq")),
        (125.0, "L.SHELF", 1000.0, "0.70", 4000.0, "0.70", 10000.0, "H.SHELF")
    );
    for f in [50.0, 125.0, 1000.0, 4000.0, 10000.0, 18000.0] {
        assert!(eq_response(&[], f).abs() < 0.01, "{f} Hz");
    }
}

#[test]
fn the_low_and_high_bands_shelve_or_cut_on_q() {
    assert!((eq_response(&[("lg", 6.0)], 30.0) - 6.0).abs() < 0.5, "low shelf");
    assert!((eq_response(&[("hg", -6.0)], 19000.0) + 6.0).abs() < 0.5, "high shelf");
    // Peaking: index 24 is Q 0.70 in LOW_Q.
    assert!((eq_response(&[("lq", 24.0), ("lg", 6.0), ("lf", freq(200.0))], 200.0) - 6.0).abs() < 0.2, "a low bell");
    let hpf = eq_response(&[("lq", dm2000::FILTER), ("lf", freq(200.0))], 50.0);
    assert!(hpf < -20.0, "two octaves under the HPF: {hpf}");
    assert!(eq_response(&[("lq", dm2000::FILTER), ("hpf", 0.0)], 50.0).abs() < 0.01, "HPF off");
    let lpf = eq_response(&[("hq", dm2000::FILTER), ("hf", freq(2000.0))], 8000.0);
    assert!(lpf < -20.0, "the LPF: {lpf}");
}

#[test]
fn a_bell_rises_at_its_frequency_and_narrows_with_q() {
    assert!((eq_response(&[("lmg", 9.0), ("lmf", freq(500.0))], 500.0) - 9.0).abs() < 0.1);
    let narrow = eq_response(&[("lmg", 12.0), ("lmq", 0.0)], 2000.0);
    let wide = eq_response(&[("lmg", 12.0), ("lmq", 40.0)], 2000.0);
    assert!(narrow < 1.0 && wide > 6.0, "{narrow} {wide}");
}

#[test]
fn type_ii_reduces_the_interference_between_bands() {
    // Two wide bells a third apart: their overlap piles up at L-MID's frequency.
    let set = [("lmg", 9.0), ("lmq", 24.0), ("lmf", freq(800.0)), ("hmg", 9.0), ("hmq", 24.0), ("hmf", freq(1250.0))];
    let mut type2 = set.to_vec();
    type2.push(("type", 1.0));
    let i = eq_response(&set, 800.0);
    let ii = eq_response(&type2, 800.0);
    assert!(i > 11.0, "TYPE I piles up: {i}");
    assert!((ii - 9.0).abs() < 0.6, "TYPE II near the set gain: {ii}");
    // Alone, the two types agree.
    let alone = [("lmg", 6.0), ("lmq", 24.0)];
    let mut alone2 = alone.to_vec();
    alone2.push(("type", 1.0));
    assert!((eq_response(&alone, 1000.0) - eq_response(&alone2, 1000.0)).abs() < 0.3);
}

#[test]
fn the_geq_has_31_bands_and_its_limit() {
    assert_eq!(GEQ_HZ.len(), 31);
    assert_eq!((GEQ_HZ[0], GEQ_HZ[30]), (20.0, 20000.0));
    assert_eq!((GEQ_HZ[23], GEQ_HZ[7]), (4000.0, 100.0), "the fader assignment's two ranges");
    let r = |set: &[(&str, f32)], f: f64| design(&GEQ_MODEL, &values(&GEQ_MODEL, set), FS, 1.0).response_db(f, FS);
    assert!(r(&[], 1000.0).abs() < 0.01, "FLAT");
    assert!((r(&[("18", 15.0)], 1000.0) - 15.0).abs() < 0.3, "+15 at 1 kHz");
    assert!(r(&[("18", 15.0)], 4000.0).abs() < 1.5, "two octaves off");
    // LIMIT holds the band.
    assert!((r(&[("18", 15.0), ("lim", 1.0)], 1000.0) - 12.0).abs() < 0.3);
    assert!((r(&[("18", 15.0), ("lim", 2.0)], 1000.0) - 6.0).abs() < 0.3);
    assert!(r(&[("18", 15.0), ("lim", 3.0)], 1000.0).abs() < 0.01, "-24 dB: no boost");
    assert!((r(&[("18", -24.0), ("lim", 3.0)], 1000.0) + 24.0).abs() < 0.3);
}

fn dyn_tail(token: &str, set: &[(&str, f32)], level: f32, sr: f32) -> f32 {
    let m = model_index(dm2000::DYN_MODELS, token).unwrap();
    let spec = &dm2000::DYN_MODELS[m];
    let v = values(spec, set);
    let mut d = WingDynamics::default();
    d.set_table(dm2000::DYN_MODELS);
    d.prepare(sr as f64);
    let mut x: Vec<f32> = (0..(sr as usize)).map(|i| level * (i as f32 * 0.13).sin()).collect();
    let (sc, xo) = (SidechainSettings::default(), XoverSettings::default());
    d.process(&mut [&mut x[..]], None, m, &v, &sc, &xo, 1.0, 0.0);
    let tail = x[x.len() / 2..].iter().fold(0.0f32, |m, s| m.max(s.abs()));
    tail / level
}

#[test]
fn the_dynamics_have_the_manuals_ranges_and_presets() {
    let by = |t: &str| &dm2000::DYN_MODELS[model_index(dm2000::DYN_MODELS, t).unwrap()];
    let param = |t: &str, id: &str| by(t).params.iter().find(|p| p.id == id).copied().unwrap();
    for t in ["DMGAT", "DMDUK"] {
        let (thr, range, att) = (param(t, "thr"), param(t, "range"), param(t, "att"));
        assert_eq!((thr.lo(), thr.hi(), thr.steps), (-54.0, 0.0, 541), "{t} threshold 0.1 dB");
        assert_eq!((range.lo(), range.hi(), range.steps), (-70.0, 0.0, 71), "{t} range 1 dB");
        assert_eq!((att.lo(), att.hi(), att.steps), (0.0, 120.0, 121), "{t} attack 1 ms");
        assert_eq!((param(t, "hld").steps, param(t, "dcy").steps), (216, 160), "{t}");
    }
    for t in ["DMCMP", "DMEXP", "DMCPH", "DMCPS"] {
        let (thr, gain) = (param(t, "thr"), param(t, "gain"));
        assert_eq!((thr.lo(), thr.steps), (-54.0, 541), "{t}");
        assert_eq!(gain.steps, 181, "{t} 0.1 dB out gain");
        assert_eq!(param(t, "rel").steps, 160, "{t}");
        if t.starts_with("DMCP") {
            assert_eq!((gain.lo(), gain.hi()), (-18.0, 0.0), "{t}");
            assert_eq!((param(t, "width").lo(), param(t, "width").hi(), param(t, "width").steps), (1.0, 90.0, 90), "{t}");
        } else {
            assert_eq!((gain.lo(), gain.hi()), (0.0, 18.0), "{t}");
            assert_eq!(param(t, "knee").steps, 6, "{t}");
        }
    }
    assert_eq!(param("DMCMP", "ratio").steps, 16, "with infinity");
    assert_eq!(param("DMCPH", "ratio").steps, 16, "COMPAND-H has infinity");
    assert_eq!(param("DMCPS", "ratio").steps, 15, "COMPAND-S doesn't");
    // Each starts on its preset template.
    let v = values(by("DMCMP"), &[]);
    let c = Params::new(by("DMCMP"), &v);
    assert_eq!((c.f("thr"), c.f("ratio"), c.f("att"), c.text("knee")), (-8.0, 2.5, 60.0, "2"));
    let v = values(by("DMGAT"), &[]);
    let g = Params::new(by("DMGAT"), &v);
    assert_eq!((g.f("thr"), g.f("range")), (-26.0, -56.0));
    assert!(dm2000::DYN_MODELS.iter().all(|m| m.params.len() <= MAX_DYN_PARAMS));
    assert!(EQ_MODEL.params.len() <= MAX_EQ_PARAMS && GEQ_MODEL.params.len() <= MAX_EQ_PARAMS);
    assert!(dm2000::is_gate("DMGAT") && dm2000::is_gate("DMDUK") && !dm2000::is_gate("DMCMP"));
}

#[test]
fn the_preset_times_fall_on_the_tables() {
    // Decay and release times of the gate and comp presets, printed at 44.1 kHz (pp.333-334).
    for ms in [331.0, 400.0, 238.0, 250.0, 70.0, 180.0, 58.0, 192.0, 12.0, 151.0, 128.0, 749.0, 842.0, 470.0, 133.0, 174.0, 261.0]
    {
        let at48: f32 = ms * 44.1 / 48.0;
        let i = index(&livedaw_dsp::y02r::RELEASES, at48) as usize;
        let near = livedaw_dsp::y02r::RELEASES[i];
        assert!((near / at48 - 1.0).abs() < 0.04, "{ms} ms: {near}");
    }
}

#[test]
fn the_dynamics_process() {
    // Compressor: 1:1 passes, a loud signal over a low threshold comes down.
    assert!((dyn_tail("DMCMP", &[("ratio", 0.0)], 0.5, 48000.0) - 1.0).abs() < 0.02);
    let c = dyn_tail("DMCMP", &[("thr", -30.0), ("ratio", index(&livedaw_dsp::y02r::RATIOS, 10.0))], 0.8, 48000.0);
    assert!(c < 0.2, "compressed: {c}");
    // Gate: closes by its range under the threshold, open over it.
    let closed = dyn_tail("DMGAT", &[("thr", -20.0), ("range", -40.0), ("dcy", 0.0)], 0.01, 48000.0);
    assert!((closed - 0.01).abs() < 0.015, "-40 dB: {closed}");
    assert!(dyn_tail("DMGAT", &[("thr", -40.0)], 0.5, 48000.0) > 0.95);
    // Ducking: down by its range while over the threshold.
    assert!((dyn_tail("DMDUK", &[("thr", -30.0), ("range", -20.0), ("att", 0.0)], 0.5, 48000.0) - 0.1).abs() < 0.03);
}

#[test]
fn the_tables_are_sample_counts_so_time_scales_with_rate() {
    // A release from the table takes the same number of samples at 96 kHz as at 48 kHz.
    let recovery = |sr: f32| {
        let m = model_index(dm2000::DYN_MODELS, "DMCMP").unwrap();
        let spec = &dm2000::DYN_MODELS[m];
        let rel = index(&livedaw_dsp::y02r::RELEASES, 500.0);
        let v = values(spec, &[("thr", -40.0), ("ratio", 15.0), ("att", 0.0), ("rel", rel)]);
        let mut d = WingDynamics::default();
        d.set_table(dm2000::DYN_MODELS);
        d.prepare(sr as f64);
        let (sc, xo) = (SidechainSettings::default(), XoverSettings::default());
        let mut loud = vec![0.5f32; sr as usize];
        d.process(&mut [&mut loud[..]], None, m, &v, &sc, &xo, 1.0, 0.0);
        // Quiet again: where the gain has come back to within 6 dB of nothing.
        let q = 0.0001f32;
        let mut quiet = vec![q; (sr * 4.0) as usize];
        d.process(&mut [&mut quiet[..]], None, m, &v, &sc, &xo, 1.0, 0.0);
        quiet.iter().position(|x| *x >= q * 0.5).unwrap_or(quiet.len()) as f32
    };
    let (at48, at96) = (recovery(48000.0), recovery(96000.0));
    assert!((at96 / at48 - 1.0).abs() < 0.1, "the same samples at both rates: {at48} {at96}");
}

fn fx_tone(token: &str, set: &[(&str, f32)], n: usize) -> (Vec<f32>, Vec<f32>) {
    let ctx = FxContext { sample_rate: 48000.0, max_block: 256 };
    let spec = &fx::FX_MODELS[model_index(&fx::FX_MODELS, token).unwrap()];
    let mut dev = create(token, ctx).unwrap_or_else(|| panic!("{token}: no device"));
    let mut v = [0.0; MAX_FX_PARAMS];
    spec.defaults_into(&mut v);
    for (id, x) in set {
        v[spec.param_index(id).unwrap_or_else(|| panic!("{id}"))] = *x;
    }
    dev.set_params(&Params::new(spec, &v));
    let (mut l, mut r) = (vec![0.0f32; n], vec![0.0f32; n]);
    for i in 0..n {
        l[i] = 0.25 * (TAU * 440.0 * i as f32 / 48000.0).sin();
        r[i] = l[i];
    }
    for (a, b) in l.chunks_mut(256).zip(r.chunks_mut(256)) {
        dev.process(a, b);
    }
    (l, r)
}

fn rms(x: &[f32]) -> f32 {
    (x.iter().map(|v| v * v).sum::<f32>() / x.len().max(1) as f32).sqrt()
}

#[test]
fn every_effect_type_runs() {
    for m in &fx::FX_MODELS[2..] {
        let (l, _) = fx_tone(m.token, &[], 24000);
        assert!(l.iter().all(|x| x.is_finite() && x.abs() < 8.0), "{} is unbounded", m.token);
    }
    assert_eq!(fx::FX_MODELS.len() - 2, 44, "the 44 stereo types");
    assert!(fx::FX_MODELS.iter().all(|m| m.params.len() <= MAX_FX_PARAMS));
}

#[test]
fn the_effects_ranges_are_the_manuals() {
    let at = |t: &str, id: &str| {
        fx::FX_MODELS[model_index(&fx::FX_MODELS, t).unwrap()].params.iter().find(|p| p.id == id).copied().unwrap()
    };
    assert_eq!((at("DMHALL", "rev").lo(), at("DMHALL", "rev").hi()), (0.3, 99.0));
    assert_eq!(at("DMMONO", "d").hi(), 2730.0);
    assert_eq!(at("DMSTDLY", "dl").hi(), 1350.0);
    assert_eq!(at("DMMODDL", "d").hi(), 2725.0);
    assert_eq!(at("DMRING", "osc").hi(), 5000.0);
    assert_eq!(at("DMPHAS", "stg").steps, 8, "2 to 16 stages");
    match at("DMHALL", "hpf").kind {
        ParamKind::Values(t) => assert_eq!((t[0], t[1], *t.last().unwrap()), (20.0, 21.2, 8000.0)),
        _ => panic!(),
    }
    match at("DMHALL", "lpf").kind {
        ParamKind::Values(t) => assert_eq!((t[0], t[t.len() - 2], *t.last().unwrap()), (50.0, 16000.0, 20000.0)),
        _ => panic!(),
    }
    assert_eq!((at("DMMBAND", "lgn").lo(), at("DMMBAND", "lgn").hi()), (-96.0, 12.0));
    assert_eq!((at("DMMBAND", "cth").lo(), at("DMMBAND", "cth").hi()), (-24.0, 0.0));
    assert_eq!(at("DMSTREV", "idly").hi(), 100.0);
}

#[test]
fn sync_sets_a_delay_from_tempo_and_note() {
    // SYNC, 120 BPM, a quarter note (index 8): 500 ms.
    let (l, _) = fx_tone("DMMONO", &[("d", 2000.0), ("sync", 1.0), ("note", 8.0), ("tmp", 120.0), ("fbg", 0.0)], 48000);
    let first = l.iter().position(|x| x.abs() > 0.01).unwrap();
    assert!((first as f32 - 24000.0).abs() < 100.0, "the first repeat at {first} samples");
}

#[test]
fn the_parallel_balances_read_the_manuals_way() {
    // REV/CHO 0% is all reverb, 100% all chorus: a long reverb rings after the chorus's stopped.
    let wet = rms(&fx_tone("DMRPCHO", &[("bal", 0.0), ("rev", 4.0)], 24000).0[20000..]);
    let dry = rms(&fx_tone("DMRPCHO", &[("bal", 100.0), ("rev", 4.0), ("mf", 0.1), ("md", 100.0)], 24000).0[20000..]);
    assert!(wet > 0.001 && dry > 0.001, "{wet} {dry}");
}

#[test]
fn the_mband_processes_three_bands_and_can_be_transparent() {
    let bypassed = [("cby", 1.0), ("eby", 1.0), ("lby", 1.0)];
    let (l, _) = fx_tone("DMMBAND", &bypassed, 4800);
    let tone: Vec<f32> = (0..4800).map(|i| 0.25 * (TAU * 440.0 * i as f32 / 48000.0).sin()).collect();
    let e = rms(&l[2400..].iter().zip(&tone[2400..]).map(|(a, b)| a - b).collect::<Vec<_>>());
    assert!(e < 0.01, "bands add back to the input: {e}");
    let (hit, _) =
        fx_tone("DMMBAND", &[("cth", -24.0), ("crt", 13.0), ("cby", 0.0), ("eby", 1.0), ("lby", 1.0), ("lth", 0.0)], 48000);
    assert!(rms(&hit[24000..]) < rms(&tone) * 0.6, "the compressor comes down");
}

#[test]
fn the_universal_offers_every_block_and_a_dm2000_only_its_own() {
    assert!(model_index(&universal::DYN_MODELS, "DMCPH").is_some());
    assert!(model_index(&universal::EQ_MODELS_CH, "DMEQ").is_some() && model_index(&universal::EQ_MODELS_BUS, "DMGEQ").is_some());
    assert!(model_index(&universal::FX_MODELS, "DMMBAND").is_some());
    assert_eq!(universal::console_of("DMGAT"), Some("DM2000"));
    // Its own console's blocks only.
    assert!(model_index(dm2000::DYN_MODELS, "GATE").is_none() && model_index(dm2000::DYN_MODELS, "Y2GAT").is_none());
    assert_eq!(EQ_CHANNEL.len(), 1, "no GEQ on an Input Channel");
    let any_effect = model_index(&fx::FX_MODELS, "DMHQP").unwrap();
    assert!(allowed_in(&dm2000::MODELS, any_effect, 7), "pitch on Effect 8 too");
}
