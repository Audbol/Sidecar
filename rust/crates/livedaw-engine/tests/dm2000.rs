//! A DM2000 console, end to end: its counts, patches and routing, and audio
//! through its order of processing.

use livedaw_engine::chain::Node;
use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::params::StripKind;
use livedaw_engine::profile::{dm2000, Profile};
use livedaw_engine::routing::strip_source_input;
use livedaw_engine::sources::FX_RETURN_SOURCE_BASE;
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
        engine.configure(&ConsoleConfig { profile: Profile::Dm2000, counts: Counts::DM2000 });
        let (num_ins, num_outs) = (130, 130);
        let in_map: Vec<i32> = (0..num_ins as i32).collect();
        let out_map: Vec<i32> = (0..num_outs as i32).collect();
        unsafe { engine.prepare(48000.0, BLOCK, &in_map, &out_map) };
        Self { engine, inputs: vec![vec![0.0; BLOCK]; num_ins], outputs: vec![vec![0.0; BLOCK]; num_outs], t: 0 }
    }

    /// Runs `blocks` blocks of a 1 kHz tone at -12 dBFS on input 1; the peak each
    /// output reached over the last of them.
    fn run(&mut self, blocks: usize) -> Vec<f32> {
        let ins: Vec<*const f32> = self.inputs.iter().map(|v| v.as_ptr()).collect();
        let outs: Vec<*mut f32> = self.outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
        for _ in 0..blocks {
            for (i, x) in self.inputs[0].iter_mut().enumerate() {
                *x = 0.25 * (TAU * 1000.0 * (self.t + i) as f32 / 48000.0).sin();
            }
            self.t += BLOCK;
            unsafe { self.engine.process(&ins, &outs, BLOCK) };
        }
        self.outputs.iter().map(|o| o.iter().fold(0.0f32, |m, x| m.max(x.abs()))).collect()
    }

    fn channel(&self, n: usize) -> Arc<Strip> {
        self.engine.strips(StripKind::Channel)[n].clone()
    }
}

#[test]
fn a_new_console_has_the_manuals_counts_and_chains() {
    let spec = Profile::Dm2000.spec();
    assert_eq!(spec.name, "DM2000");
    assert_eq!(spec.counts, Counts { channels: 96, aux: 0, buses: 20, mains: 1, matrices: 4, dcas: 12, mute_groups: 12, fx: 8 });
    assert_eq!(Profile::from_id("dm2000"), Some(Profile::Dm2000));
    assert_eq!(spec.label_at(StripKind::Bus, 7), ("BUS", 8));
    assert_eq!(spec.label_at(StripKind::Bus, dm2000::AUX), ("AUX", 1));
    assert_eq!(spec.label_at(StripKind::Bus, dm2000::AUX + 11), ("AUX", 12));
    assert_eq!(spec.label_at(StripKind::Main, 0), ("ST", 1));

    let ch = spec.template_at(StripKind::Channel, 0);
    let at = |n: Node| ch.iter().position(|&x| x == n).unwrap_or_else(|| panic!("no {n:?}"));
    // The block diagram's order, p.399.
    assert!(at(Node::Input) < at(Node::Gate));
    assert!(at(Node::Gate) < at(Node::Attenuator));
    assert!(at(Node::Attenuator) < at(Node::Eq));
    assert!(at(Node::Eq) < at(Node::Delay));
    assert!(at(Node::Delay) < at(Node::Fader));
    assert_eq!(ch.iter().filter(|n| matches!(n, Node::Slot(_))).count(), 6, "three places for two blocks");
    assert!(!ch.contains(&Node::Filter), "no filter block: the EQ's outer bands are it");
    let out = spec.template_at(StripKind::Main, 0);
    let out_at = |n: Node| out.iter().position(|&x| x == n).unwrap();
    assert!(
        out_at(Node::Attenuator) < out_at(Node::Eq) && out_at(Node::Fader) < out_at(Node::Delay),
        "the output delay after the fader"
    );
    assert!(!out.contains(&Node::Gate), "gates on Input Channels only");
}

#[test]
fn the_factory_patch_is_the_manuals() {
    let rig = Rig::new();
    let ch = rig.engine.strips(StripKind::Channel);
    let source = |n: usize| ch[n].params.input_left.load(Relaxed);
    assert_eq!(source(0), 0, "CH1 AD1");
    assert_eq!(source(23), 23, "CH24 AD24");
    assert_eq!(source(24), 24, "CH25 S1-01");
    assert_eq!(source(31), 31, "CH32 S1-08");
    assert_eq!(source(32), 24 + 16, "CH33 S2-01");
    assert_eq!(source(40), 24 + 32, "CH41 S3-01");
    assert_eq!(source(48), 24 + 48, "CH49 S4-01");
    assert_eq!(source(71), 24 + 80 + 7, "CH72 S6-08");
    assert_eq!(source(72), FX_RETURN_SOURCE_BASE, "CH73 FX1-1");
    assert_eq!(source(87), FX_RETURN_SOURCE_BASE + 15, "CH88 FX8-2");
    assert_eq!(source(88), 120, "CH89 2TD1L");
    assert_eq!(source(92), 126, "CH93 2TA1L");
    assert_eq!(source(95), 129, "CH96 2TA2R");
    assert!(ch.iter().all(|c| !c.params.stereo.load(Relaxed)));
    assert!(ch[0].params.fader_db.load() <= -130.0, "faders at -oo");
    assert_eq!(ch[0].params.process_order.load(Relaxed), 3, "insert PRE EQ, compressor PRE FADER");
    let eq = ch[0].params.eq();
    assert_eq!(eq.table()[eq.model_index()].token, "DMEQ");
    assert_eq!(rig.engine.strips(StripKind::Bus)[0].name(), "BUS1");
    assert_eq!(rig.engine.strips(StripKind::Bus)[dm2000::AUX + 11].name(), "AUX12");
    assert_eq!(rig.engine.strips(StripKind::Matrix)[3].name(), "MATRIX4");
    assert_eq!(rig.engine.strips(StripKind::Main)[0].name(), "ST");
    assert_eq!(rig.engine.strips(StripKind::Channel)[95].name(), "CH96");
    // Effect 1 fed by AUX 1, Effect 8 by AUX 8 (p.289).
    let (fx1, fx8) = (&rig.engine.controls.fx[0], &rig.engine.controls.fx[7]);
    assert_eq!(fx1.source_left.load(Relaxed), strip_source_input(StripKind::Bus, dm2000::AUX, 0));
    assert_eq!(fx8.source_left.load(Relaxed), strip_source_input(StripKind::Bus, dm2000::AUX + 7, 0));
}

#[test]
fn the_output_patch_reaches_omni_and_the_slots() {
    let mut rig = Rig::new();
    let ch = rig.channel(0);
    ch.params.fader_db.store(0.0);
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    // CH1 to AUX 3 at 0 dB: OMNI OUT 3 (engine output 4).
    let s = ch.sends.get_or_create(dm2000::AUX + 2).unwrap();
    s.enabled.store(true, Relaxed);
    s.level_db.store(0.0);
    let peaks = rig.run(60);
    assert!(peaks[2 + 2] > 0.2, "AUX 3 on OMNI OUT 3: {}", peaks[4]);
    assert!(peaks[0] < 1e-6, "not on STEREO");
    // CH1 to BUS 2: SLOT1-02 and SLOT1-10.
    let b = ch.sends.get_or_create(1).unwrap();
    b.enabled.store(true, Relaxed);
    b.level_db.store(0.0);
    let peaks = rig.run(60);
    assert!(peaks[25] > 0.2 && peaks[33] > 0.2, "BUS 2 on slot outs: {} {}", peaks[25], peaks[33]);
}

#[test]
fn the_compressor_and_insert_take_every_position() {
    let spec = Profile::Dm2000.spec();
    let rig = Rig::new();
    let ch = rig.channel(0);
    assert_eq!(spec.process_orders.len(), 12);
    let place = |ch: &Strip, o: i32| {
        ch.params.process_order.store(o, Relaxed);
        let slots: Vec<Option<Node>> = (0..6).map(|k| ch.slot_block(k)).collect();
        (
            slots.iter().position(|s| *s == Some(Node::Dynamics)).unwrap(),
            slots.iter().position(|s| *s == Some(Node::PreInsert)).unwrap(),
        )
    };
    for o in 0..9 {
        let (d, i) = place(&ch, o);
        assert_eq!(d / 2, o as usize / 3, "order {o}: compressor place");
        assert_eq!(i / 2, o as usize % 3, "order {o}: insert place");
    }
    // The shared places: Comp->Ins, then Ins->Comp.
    assert_eq!(place(&ch, 4), (2, 3));
    assert_eq!(place(&ch, 10), (3, 2));
    let (d, i) = place(&ch, 9);
    assert!(i < d, "Ins->Comp at PRE EQ");
}

#[test]
fn the_attenuator_sits_after_the_gate_and_before_the_eq() {
    let mut rig = Rig::new();
    let ch = rig.channel(0);
    ch.params.fader_db.store(0.0);
    // The gate on and just opened by the signal at -12 dBFS...
    let gate = ch.params.gate();
    gate.on.store(true, Relaxed);
    gate.values[gate.table()[gate.model_index()].param_index("thr").unwrap()].store(-20.0);
    gate.values[gate.table()[gate.model_index()].param_index("range").unwrap()].store(-70.0);
    let open = rig.run(120)[0];
    assert!(open > 0.15, "open: {open}");
    // ...stays open with the attenuator well down: the gate sees the signal before it.
    ch.params.input.trim_db.store(-30.0);
    let attenuated = rig.run(200)[0];
    let expected = open * 10f32.powf(-30.0 / 20.0);
    assert!((attenuated - expected).abs() < expected * 0.2, "open and 30 dB down: {attenuated} of {open}");
}

#[test]
fn the_delay_runs_in_samples_from_zero_to_43400() {
    let mut rig = Rig::new();
    let ch = rig.channel(0);
    ch.params.fader_db.store(0.0);
    // A click is simplest through the delay alone: measure with the tone's first sample.
    ch.params.input.delay_on.store(true, Relaxed);
    ch.params.input.delay.store(0.0);
    assert!(rig.run(10)[0] > 0.2, "0 samples with the delay on: no 16-sample floor to worry the tone");
    let spec = Profile::Dm2000.spec();
    assert_eq!(spec.delay_samples, Some((0, 43400)));
    assert_eq!(ch.params.input.delay_mode.load(Relaxed), 3, "samples");
    // 43400 samples is delayed by that many: nothing at the outputs yet after a third of them.
    let mut rig = Rig::new();
    let ch = rig.channel(0);
    ch.params.fader_db.store(0.0);
    ch.params.input.delay_on.store(true, Relaxed);
    ch.params.input.delay.store(43400.0);
    let before = rig.run(14400 / BLOCK)[0];
    assert!(before < 1e-5, "still in the delay line: {before}");
    let after = rig.run((43400 - 14400) / BLOCK + 40)[0];
    assert!(after > 0.2, "out after 43400 samples: {after}");
}

#[test]
fn the_fader_laws_are_yamahas_tables() {
    let spec = Profile::Dm2000.spec();
    let (fader, main, bus) =
        (spec.fader_for(StripKind::Channel), spec.fader_for(StripKind::Main), spec.fader_for(StripKind::Bus));
    assert_eq!(fader.steps, 1024);
    assert_eq!((fader.max_db(), main.max_db(), bus.max_db()), (10.0, 0.0, 0.0));
    assert_eq!(spec.fader_for(StripKind::Matrix).max_db(), 0.0, "the matrices' too");
    // -oo at the bottom; 0 dB on value 823 (the MIDI specification's PRM TABLE #05-2).
    assert!(fader.to_db(0.0) < -130.0);
    assert!((fader.to_pos(0.0) - 823.0 / 1023.0).abs() < 1e-4);
    assert!((fader.to_db(1.0 / 1023.0) + 138.0).abs() < 0.5, "-138 on the bottom step");
    // Between the breakpoints, as the table lists them.
    for (v, db) in [(128.0f32, -59.0f32), (643.0, -9.0), (903.0, 4.0)] {
        assert!((fader.to_db(v / 1023.0) - db).abs() < 0.01, "{v}");
    }
    assert!((bus.to_db(903.0 / 1023.0) + 6.0).abs() < 0.01, "#15-2");
}

#[test]
fn a_geq_is_an_output_blocks_not_a_channels() {
    let spec = Profile::Dm2000.spec();
    assert!(spec.models.table(2).iter().any(|m| m.token == "DMGEQ"), "on the outputs");
    assert!(!spec.models.table(1).iter().any(|m| m.token == "DMGEQ"), "not on the inputs");
}

#[test]
fn it_may_hold_only_its_own_models_and_the_universal_all() {
    use livedaw_engine::profile::DynSlot;
    let dm = Profile::Dm2000.spec();
    assert!(dm.may_hold(DynSlot::Gate, "DMGAT"));
    assert!(dm.may_hold(DynSlot::Dynamics, "DMCMP"));
    assert!(!dm.may_hold(DynSlot::Gate, "DMCMP"), "a compressor isn't a gate");
    assert!(!dm.may_hold(DynSlot::Dynamics, "DMDUK"));
    assert!(!dm.models.table(0).iter().any(|m| m.token == "GATE"), "its table holds only its models");
    let u = Profile::Universal.spec();
    for token in ["DMGAT", "DMCMP"] {
        assert!(u.models.table(0).iter().any(|m| m.token == token), "{token}");
        assert!(u.may_hold(DynSlot::Dynamics, token));
    }
    assert!(u.models.table(1).iter().any(|m| m.token == "DMEQ"));
    assert!(u.models.table(4).iter().any(|m| m.token == "DMHALL"));
}
