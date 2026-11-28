//! An SQ-5's own engine behaviour, end to end (Reference Guide V1.6.0): Direct Out's
//! settings (p.46), the gate's and compressor's side chain (pp.35, 41), the Input to PAFL
//! Source Point and the PAFL delay (p.94).

use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::dlive::{direct_source, pafl_source};
use livedaw_engine::params::StripKind;
use livedaw_engine::profile::Profile;
use livedaw_engine::routing::DirectSource;
use livedaw_engine::strip::tap;
use livedaw_engine::{Engine, Strip};
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

const BLOCK: usize = 128;
const SR: f64 = 48000.0;
/// The output channel 1's Direct Out is patched to, and the PAFL (monitor) pairs'.
const DIRECT: usize = 19;
const SPK: usize = 24;
const PH: usize = 26;

struct Rig {
    engine: Arc<Engine>,
    inputs: Vec<Vec<f32>>,
    outputs: Vec<Vec<f32>>,
    t: usize,
}

impl Rig {
    fn new() -> Self {
        let engine = Arc::new(Engine::new(0));
        engine.configure(&ConsoleConfig { profile: Profile::AhSq5, counts: Counts::SQ5 });
        let (num_ins, num_outs) = (64, 32);
        let in_map: Vec<i32> = (0..num_ins as i32).collect();
        let out_map: Vec<i32> = (0..num_outs as i32).collect();
        unsafe { engine.prepare(SR, BLOCK, &in_map, &out_map) };
        let c = &engine.controls;
        c.monitor_output_left.store(SPK as i32, Relaxed);
        c.monitor_output_right.store(SPK as i32 + 1, Relaxed);
        c.phones.output_left.store(PH as i32, Relaxed);
        c.phones.output_right.store(PH as i32 + 1, Relaxed);
        c.output_protection.store(false, Relaxed);
        engine.collect();
        Self { engine, inputs: vec![vec![0.0; BLOCK]; num_ins], outputs: vec![vec![0.0; BLOCK]; num_outs], t: 0 }
    }

    /// A 1 kHz tone on input 1 (0.25 peak) and at half that on input 2; the others are
    /// silent. The peak each output reached over the last block.
    fn run(&mut self, blocks: usize) -> Vec<f32> {
        let ins: Vec<*const f32> = self.inputs.iter().map(|v| v.as_ptr()).collect();
        let outs: Vec<*mut f32> = self.outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
        for _ in 0..blocks {
            for i in 0..BLOCK {
                let x = 0.25 * (2.0 * std::f32::consts::PI * 1000.0 * (self.t + i) as f32 / SR as f32).sin();
                self.inputs[0][i] = x;
                self.inputs[1][i] = 0.5 * x;
            }
            self.t += BLOCK;
            unsafe { self.engine.process(&ins, &outs, BLOCK) };
        }
        self.outputs.iter().map(|o| o.iter().fold(0.0f32, |m, x| m.max(x.abs()))).collect()
    }

    fn settle(&mut self) -> Vec<f32> {
        self.run(60)
    }

    /// Long enough for a gate's hold and release, or a compressor's release.
    fn settle_dynamics(&mut self) -> Vec<f32> {
        self.run(400)
    }

    fn ch(&self, i: usize) -> Arc<Strip> {
        self.engine.strips(StripKind::Channel)[i].clone()
    }

    fn up(&self, i: usize) -> Arc<Strip> {
        let ch = self.ch(i);
        ch.params.mute.store(false, Relaxed);
        ch.params.fader_db.store(0.0);
        ch
    }

    /// Channel 1 up, its Direct Out on output DIRECT.
    fn direct() -> (Self, Arc<Strip>) {
        let rig = Self::new();
        let ch = rig.up(0);
        let code = DirectSource { kind: StripKind::Channel, number: 1, side: 0, post: false }.code();
        rig.engine.output_patch().set(DIRECT as i32, code);
        rig.engine.collect();
        (rig, ch)
    }
}

fn param(table: &[livedaw_dsp::model::ModelSpec], model: usize, id: &str) -> usize {
    table[model].params.iter().position(|p| p.id == id).unwrap()
}

// ---- Direct Out ----------------------------------------------------------------------------

#[test]
fn a_new_sq_takes_its_direct_outs_post_compressor_following_the_mute() {
    let (mut rig, ch) = Rig::direct();
    let e = rig.engine.clone();
    assert_eq!(e.dlive.direct_source.load(Relaxed), direct_source::POST_COMP);
    assert!(e.dlive.direct_follow_mute.load(Relaxed) && !e.dlive.direct_follow_fader.load(Relaxed));
    ch.params.fader_db.store(-20.0);
    let peak = rig.settle()[DIRECT];
    assert!((peak - 0.25).abs() < 0.02, "pre fader, at its own 0 dB: {peak}");
    ch.params.mute.store(true, Relaxed);
    assert!(rig.settle()[DIRECT] < 1e-5, "Follow Mute");
}

#[test]
fn direct_out_source_points_are_in_the_channels_order() {
    let (mut rig, ch) = Rig::direct();
    let e = rig.engine.clone();
    let source = |s: i32| e.dlive.direct_source.store(s, Relaxed);
    // A closed gate: everything from Post Gate on is silent.
    let gate = ch.params.gate();
    gate.on.store(true, Relaxed);
    let m = gate.model.load(Relaxed) as usize;
    gate.values[param(gate.table(), m, "thr")].store(0.0);
    source(direct_source::POST_PREAMP);
    assert!((rig.settle()[DIRECT] - 0.25).abs() < 0.02, "Post Preamp");
    source(direct_source::POST_GATE);
    assert!(rig.settle()[DIRECT] < 0.01, "Post Gate");
    gate.on.store(false, Relaxed);
    // A 12 dB boost at 1 kHz: after the insert return, not yet; after the PEQ, there.
    let eq = ch.params.eq();
    let at = |id: &str| livedaw_dsp::sq::EQ[0].params.iter().position(|p| p.id == id).unwrap();
    eq.on.store(true, Relaxed);
    eq.values[at("1f")].store(1000.0);
    eq.values[at("1g")].store(12.0);
    source(direct_source::POST_INSERT_A);
    let insert = rig.settle()[DIRECT];
    source(direct_source::POST_PEQ);
    let peq = rig.settle()[DIRECT];
    source(direct_source::POST_DELAY);
    let delay = rig.settle()[DIRECT];
    assert!((insert - 0.25).abs() < 0.02, "Post Insert Return: {insert}");
    assert!(peq / insert > 3.0 && (delay - peq).abs() < 0.02, "Post PEQ {peq}, Post Delay {delay}");
}

#[test]
fn direct_out_follows_the_dcas_and_mute_groups_as_set() {
    let (mut rig, ch) = Rig::direct();
    let e = rig.engine.clone();
    e.dlive.direct_follow_mute.store(false, Relaxed);
    ch.params.dca_mask.set(0, true);
    ch.params.mute_group_mask.set(2, true);
    e.controls.dcas[0].fader_db.store(-20.0);
    assert!(rig.settle()[DIRECT] > 0.2, "not following the DCA");
    e.dlive.direct_follow_dca_fader.store(true, Relaxed);
    let down = rig.settle()[DIRECT];
    assert!((down - 0.025).abs() < 0.005, "Follow DCA Fader: {down}");
    e.dlive.direct_follow_dca_fader.store(false, Relaxed);
    e.controls.dcas[0].mute.store(true, Relaxed);
    assert!(rig.settle()[DIRECT] > 0.2, "a DCA mute, not followed");
    e.dlive.direct_follow_dca_mute.store(true, Relaxed);
    assert!(rig.settle()[DIRECT] < 1e-5, "Follow DCA Mute");
    e.dlive.direct_follow_dca_mute.store(false, Relaxed);
    e.controls.dcas[0].mute.store(false, Relaxed);
    e.controls.mute_groups.set(2, true);
    assert!(rig.settle()[DIRECT] > 0.2, "a mute group, not followed");
    e.dlive.direct_follow_mute_group.store(true, Relaxed);
    assert!(rig.settle()[DIRECT] < 1e-5, "Follow Mute Group");
}

// ---- Side chains ---------------------------------------------------------------------------

#[test]
fn the_gate_keys_from_another_channel_after_its_processing() {
    let (mut rig, ch) = Rig::direct();
    let e = rig.engine.clone();
    e.dlive.direct_source.store(direct_source::POST_DELAY, Relaxed);
    let gate = ch.params.gate();
    gate.on.store(true, Relaxed);
    let m = gate.model.load(Relaxed) as usize;
    gate.values[param(gate.table(), m, "thr")].store(-30.0);
    assert!(rig.settle_dynamics()[DIRECT] > 0.2, "Self Keyed: open");
    // Channel 3 hears nothing: keyed from it, the gate closes.
    gate.sc_source.store(2, Relaxed);
    gate.sc_tap.store(tap::PFL as i32, Relaxed);
    e.collect();
    assert!(rig.settle_dynamics()[DIRECT] < 0.01, "keyed from a silent channel");
    // Channel 2 has its tone at -18 dBFS, over the threshold, but only while it isn't
    // muted: the key comes before its mute and fader.
    let key = rig.ch(1);
    key.params.mute.store(true, Relaxed);
    gate.sc_source.store(1, Relaxed);
    e.collect();
    assert!(rig.settle_dynamics()[DIRECT] > 0.2, "keyed from channel 2");
}

#[test]
fn the_compressors_key_filter_keeps_the_tone_out_of_the_side_chain() {
    let (mut rig, ch) = Rig::direct();
    let e = rig.engine.clone();
    e.dlive.direct_source.store(direct_source::POST_DELAY, Relaxed);
    let comp = ch.params.dynamics();
    comp.on.store(true, Relaxed);
    let m = comp.model.load(Relaxed) as usize;
    comp.values[param(comp.table(), m, "thr")].store(-30.0);
    let squeezed = rig.settle_dynamics()[DIRECT];
    assert!(squeezed < 0.1, "keyed by the tone: {squeezed}");
    // HPF at 5 kHz: the 1 kHz tone barely reaches the detector.
    comp.sc_filter.store(2, Relaxed);
    comp.sc_freq.store(5000.0);
    let free = rig.settle_dynamics()[DIRECT];
    assert!(free > 0.2 && free > squeezed * 2.0, "with the key filter: {squeezed} then {free}");
}

// ---- PAFL ------------------------------------------------------------------------------------

#[test]
fn input_pfl_leaves_the_channel_at_its_source_point() {
    let mut rig = Rig::new();
    let e = rig.engine.clone();
    e.controls.speakers.pfl_dim_db.store(0.0);
    e.controls.phones.source.pfl_dim_db.store(0.0);
    let ch = rig.up(0);
    let eq = ch.params.eq();
    let at = |id: &str| livedaw_dsp::sq::EQ[0].params.iter().position(|p| p.id == id).unwrap();
    eq.on.store(true, Relaxed);
    eq.values[at("1f")].store(1000.0);
    eq.values[at("1g")].store(12.0);
    ch.params.solo.store(true, Relaxed);
    let heard = |rig: &mut Rig| {
        let o = rig.settle();
        o[SPK].max(o[PH])
    };
    assert_eq!(e.dlive.pafl_source.load(Relaxed), pafl_source::POST_DELAY, "the engine's own PFL point");
    let post_delay = heard(&mut rig);
    e.dlive.pafl_source.store(pafl_source::POST_PREAMP, Relaxed);
    let preamp = heard(&mut rig);
    e.dlive.pafl_source.store(pafl_source::POST_PEQ, Relaxed);
    let peq = heard(&mut rig);
    assert!((preamp - 0.25).abs() < 0.03, "Post Preamp: {preamp}");
    assert!(peq / preamp > 3.0 && (post_delay - peq).abs() < 0.05, "Post PEQ {peq}, Post Delay {post_delay}");
}

#[test]
fn the_pafl_delay_runs_past_the_wings_hundred_metres() {
    let mut rig = Rig::new();
    let e = rig.engine.clone();
    rig.up(0).params.solo.store(true, Relaxed);
    for path in &e.controls.monitor.paths {
        path.delay_on.store(true, Relaxed);
        path.delay_m.store(0.6 * livedaw_dsp::delay::SPEED_OF_SOUND); // 600 ms
    }
    let blocks = |ms: f64| (ms * 0.001 * SR / BLOCK as f64) as usize;
    let heard = |o: Vec<f32>| o[SPK].max(o[PH]);
    assert!(heard(rig.run(blocks(550.0))) < 1e-6, "still delayed at 550 ms");
    assert!(heard(rig.run(blocks(100.0))) > 0.02, "heard by 650 ms");
}
