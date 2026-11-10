//! A XENYX 1204USB console, end to end: its counts, names and patches, and audio
//! through its blocks (the curves are Sidecar's reading: `livedaw_dsp::xenyx1204`).

use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::params::StripKind;
use livedaw_engine::profile::{DynSlot, Profile};
use livedaw_engine::{Engine, Strip};
use std::f32::consts::TAU;
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

const BLOCK: usize = 128;

struct Rig {
    engine: Arc<Engine>,
    inputs: Vec<Vec<f32>>,
    outputs: Vec<Vec<f32>>,
    t: usize,
}

impl Rig {
    fn new() -> Self {
        let engine = Arc::new(Engine::new(0));
        engine.configure(&ConsoleConfig { profile: Profile::Xenyx1204, counts: Counts::XENYX1204 });
        let (num_ins, num_outs) = (16, 12);
        let in_map: Vec<i32> = (0..num_ins as i32).collect();
        let out_map: Vec<i32> = (0..num_outs as i32).collect();
        unsafe { engine.prepare(48000.0, BLOCK, &in_map, &out_map) };
        engine.strips(StripKind::Main)[0].params.fader_db.store(0.0);
        Self { engine, inputs: vec![vec![0.0; BLOCK]; num_ins], outputs: vec![vec![0.0; BLOCK]; num_outs], t: 0 }
    }

    /// `blocks` blocks of 1 kHz at `amp` on input 1; each output's peak over the last.
    fn run(&mut self, blocks: usize, amp: f32) -> Vec<f32> {
        let ins: Vec<*const f32> = self.inputs.iter().map(|v| v.as_ptr()).collect();
        let outs: Vec<*mut f32> = self.outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
        for _ in 0..blocks {
            for (i, x) in self.inputs[0].iter_mut().enumerate() {
                *x = amp * (TAU * 1000.0 * (self.t + i) as f32 / 48000.0).sin();
            }
            self.t += BLOCK;
            unsafe { self.engine.process(&ins, &outs, BLOCK) };
        }
        self.outputs.iter().map(|o| o.iter().fold(0.0f32, |m, x| m.max(x.abs()))).collect()
    }

    fn channel(&self, n: usize) -> Arc<Strip> {
        let ch = self.engine.strips(StripKind::Channel)[n].clone();
        ch.main_send(0).unwrap().on.store(true, Relaxed);
        ch.main_send(0).unwrap().level_db.store(0.0);
        ch
    }
}

fn db(x: f32) -> f32 {
    20.0 * x.log10()
}

#[test]
fn a_new_console_has_the_panels_strips_and_names() {
    let spec = Profile::Xenyx1204.spec();
    assert_eq!(spec.name, "XENYX 1204USB");
    assert_eq!(Profile::from_id("xenyx1204"), Some(Profile::Xenyx1204));
    assert_eq!(spec.counts, Counts { channels: 6, aux: 3, buses: 3, mains: 1, matrices: 0, dcas: 0, mute_groups: 0, fx: 0 });
    let rig = Rig::new();
    let name = |k, n: usize| rig.engine.strips(k)[n].name();
    assert_eq!((name(StripKind::Channel, 0).as_str(), name(StripKind::Channel, 4).as_str()), ("1", "5/6"));
    assert_eq!(name(StripKind::Aux, 2), "2-TR/USB");
    assert_eq!((name(StripKind::Bus, 0).as_str(), name(StripKind::Bus, 2).as_str()), ("AUX 1", "ALT 3-4"));
    assert_eq!(name(StripKind::Main, 0), "MAIN MIX");
    assert!(!rig.channel(0).params.stereo.load(Relaxed) && rig.channel(4).params.stereo.load(Relaxed));
    assert_eq!(rig.channel(5).params.input_left.load(Relaxed), 6, "7/8 on LINE 7/8");
    let ch = rig.channel(0);
    let f = ch.params.filter();
    assert_eq!((f.low_cut_freq.load(), f.low_cut_slope.load(Relaxed)), (75.0, 2));
    assert!(!f.low_cut.load(Relaxed), "the low cut starts out");
}

#[test]
fn it_may_hold_only_its_own_models_and_the_universal_all() {
    let spec = Profile::Xenyx1204.spec();
    assert!(spec.may_hold(DynSlot::Gate, "XYCLIP") && spec.may_hold(DynSlot::Dynamics, "XYCMP"));
    assert!(!spec.may_hold(DynSlot::Dynamics, "XYCLIP") && !spec.may_hold(DynSlot::Gate, "XYCMP"));
    assert!(spec.models.table(0).iter().all(|m| m.token.starts_with("XY")));
    let u = Profile::Universal.spec();
    assert!(["XYCLIP", "XYCMP"].iter().all(|t| u.models.table(0).iter().any(|m| m.token == *t)));
    assert!(u.models.table(1).iter().any(|m| m.token == "XYEQ"));
}

#[test]
fn the_fader_law_prints_the_panels_scale() {
    let fader = Profile::Xenyx1204.spec().fader_for(StripKind::Channel);
    assert_eq!(fader.max_db(), 10.0);
    assert!(fader.to_db(0.0) < -80.0, "the bottom is -oo");
    assert!((fader.to_pos(0.0) - 0.786).abs() < 1e-3);
    assert!((fader.to_db(0.572) + 10.0).abs() < 0.1);
}

#[test]
fn a_channel_reaches_the_main_outputs_and_the_gain_knob_raises_it() {
    let mut rig = Rig::new();
    let ch = rig.channel(0);
    ch.params.fader_db.store(0.0);
    let flat = rig.run(60, 0.05)[0];
    assert!(flat > 0.0, "main out 1 hears channel 1");
    ch.params.input.trim_db.store(12.0);
    let louder = rig.run(60, 0.05)[0];
    assert!(db(louder / flat) > 8.0 && db(louder / flat) < 13.0, "{}", db(louder / flat));
}

#[test]
fn the_low_cut_takes_the_lows_and_the_comp_knob_works_on_loud_signals() {
    let mut rig = Rig::new();
    let ch = rig.channel(0);
    ch.params.fader_db.store(0.0);
    let loud = rig.run(80, 0.7)[0];
    // COMP at 10 (one value of the compressor's model) takes the loud tone down.
    let d = ch.params.dynamics();
    assert!(d.on.load(Relaxed));
    d.values[0].store(10.0);
    let squashed = rig.run(80, 0.7)[0];
    assert!(squashed < loud, "{squashed} < {loud}");
    d.values[0].store(0.0);
    // The low cut at 75 Hz: off passes the tone, on leaves a 1 kHz tone alone.
    rig.run(300, 0.1);
    let off = rig.run(60, 0.1)[0];
    ch.params.filter().low_cut.store(true, Relaxed);
    rig.run(100, 0.1);
    let on = rig.run(60, 0.1)[0];
    assert!((db(on / off)).abs() < 0.5);
}
