//! A PRO2 console, end to end: its counts, patches and routing, and audio through
//! its order of processing.

use livedaw_engine::chain::Node;
use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::params::StripKind;
use livedaw_engine::profile::{DynSlot, Profile};
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
        engine.configure(&ConsoleConfig { profile: Profile::MidasPro2, counts: Counts::PRO2 });
        let (num_ins, num_outs) = (60, 32);
        let in_map: Vec<i32> = (0..num_ins as i32).collect();
        let out_map: Vec<i32> = (0..num_outs as i32).collect();
        unsafe { engine.prepare(48000.0, BLOCK, &in_map, &out_map) };
        Self { engine, inputs: vec![vec![0.0; BLOCK]; num_ins], outputs: vec![vec![0.0; BLOCK]; num_outs], t: 0 }
    }

    /// `blocks` blocks of 1 kHz at -12 dBFS on input 1; each output's peak over the last.
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
    let spec = Profile::MidasPro2.spec();
    assert_eq!(spec.name, "PRO2");
    assert_eq!(spec.counts, Counts { channels: 56, aux: 8, buses: 16, mains: 2, matrices: 8, dcas: 8, mute_groups: 6, fx: 6 });
    assert_eq!(Profile::from_id("pro2"), Some(Profile::MidasPro2));
    assert_eq!(spec.label_at(StripKind::Bus, 0), ("AUX", 1));
    assert_eq!(spec.label_at(StripKind::Aux, 7), ("AR", 8));
    assert_eq!(spec.label_at(StripKind::Main, 1), ("MST", 2));

    let ch = spec.template_at(StripKind::Channel, 0);
    let at = |n: Node| ch.iter().position(|&x| x == n).unwrap_or_else(|| panic!("no {n:?}"));
    // The Mono input channel signal flow, p.296.
    assert!(at(Node::Input) < at(Node::Filter));
    assert!(at(Node::Filter) < at(Node::Slot(0)));
    assert!(at(Node::Slot(3)) < at(Node::Pfl));
    assert!(at(Node::Pfl) < at(Node::Fader));
    assert_eq!(ch.iter().filter(|n| matches!(n, Node::Slot(_))).count(), 4, "gate, compressor, insert, EQ");
    assert_eq!(spec.process_orders, ["EIGD", "GDIE"], "default EQ/INS/DYN, gate before compressor");
    assert_eq!(spec.pre_fader_order(StripKind::Bus, 0), b"EID");
    assert_eq!(spec.pre_fader_order(StripKind::Main, 1), b"DIE");

    let aux = spec.template_at(StripKind::Aux, 0);
    assert!(aux.contains(&Node::Eq) && aux.contains(&Node::PreInsert), "p.264: an EQ and insert");
    assert!(!aux.contains(&Node::Gate) && !aux.contains(&Node::Dynamics), "no dynamics, p.11");
    assert!(!aux.contains(&Node::Filter));

    let out = spec.template_at(StripKind::Main, 0);
    let out_at = |n: Node| out.iter().position(|&x| x == n).unwrap();
    assert!(out_at(Node::Slot(2)) < out_at(Node::Fader) && out_at(Node::Fader) < out_at(Node::Delay));
    assert!(!out.contains(&Node::Gate));
    assert_eq!(out.iter().filter(|n| matches!(n, Node::Slot(_))).count(), 3);
}

#[test]
fn the_normal_foh_state_is_the_manuals() {
    let rig = Rig::new();
    let e = &rig.engine;
    let ch = e.strips(StripKind::Channel);
    let source = |n: usize| ch[n].params.input_left.load(Relaxed);
    assert_eq!(source(0), 0, "DL251 input 1");
    assert_eq!(source(47), 47, "DL251 input 48");
    assert_eq!(source(48), 48, "49-56 on the surface inputs");
    assert_eq!(source(55), 55);
    assert!(ch.iter().all(|c| !c.params.stereo.load(Relaxed)), "mono by default");
    assert!(ch[0].params.fader_db.load() <= -130.0);
    assert_eq!(ch[0].params.process_order.load(Relaxed), 0, "EQ/INS/DYN");
    let aux = e.strips(StripKind::Aux);
    assert_eq!(aux[0].params.input_left.load(Relaxed), FX_RETURN_SOURCE_BASE, "FX 1 L on Aux Return 1");
    assert_eq!(aux[7].params.input_left.load(Relaxed), FX_RETURN_SOURCE_BASE + 7, "FX 4 R on Aux Return 8");
    assert_eq!((aux[0].params.pan.load(), aux[1].params.pan.load()), (-100.0, 100.0));
    assert_eq!(ch[0].name(), "Input 1");
    assert_eq!(aux[3].name(), "Aux Ret 4");
    assert_eq!(e.strips(StripKind::Bus)[15].name(), "Aux Send 16");
    assert_eq!(e.strips(StripKind::Matrix)[7].name(), "Matrix 8");
    assert_eq!(e.strips(StripKind::Main)[0].name(), "Master");
    assert_eq!(e.strips(StripKind::Main)[1].name(), "Mono");
    // Aux 9-16 are groups, routed to the stereo masters; 1-8 not.
    let bus = e.strips(StripKind::Bus);
    assert!(bus[8].main_send(0).unwrap().on.load(Relaxed));
    assert!(!bus[7].main_send(0).unwrap().on.load(Relaxed));
    assert_eq!(bus[8].params.fader_db.load(), 0.0);
    assert_eq!((bus[8].params.pan.load(), bus[9].params.pan.load()), (-100.0, 100.0));
    // The rack and what feeds it (matrices 1-4).
    let fx = &e.controls.fx;
    let models = e.fx_models();
    let token = |i: usize| models[fx[i].model.load(Relaxed) as usize].token;
    assert_eq!([token(0), token(1), token(2), token(3)], ["MPDN780", "MPDN780", "MPDN780", "MPPIT"]);
    assert_eq!(fx[0].source_left.load(Relaxed), strip_source_input(StripKind::Matrix, 0, 0));
    assert_eq!(fx[3].source_right.load(Relaxed), strip_source_input(StripKind::Matrix, 3, 0));
}

#[test]
fn the_normal_foh_patch_reaches_dl251_and_the_master_outs() {
    use livedaw_engine::ports::pro2_ports::{DL251_OUT, MASTER_OUT};
    let mut rig = Rig::new();
    let ch = rig.channel(0);
    ch.params.fader_db.store(0.0);
    // CH1 to Aux Send 2.
    let s = ch.sends.get_or_create(1).unwrap();
    s.enabled.store(true, Relaxed);
    s.level_db.store(0.0);
    rig.engine.strips(StripKind::Bus)[1].params.fader_db.store(0.0);
    for m in rig.engine.strips(StripKind::Main).iter() {
        m.params.fader_db.store(0.0);
    }
    let peaks = rig.run(80);
    assert!(peaks[DL251_OUT as usize + 1] > 0.1, "Aux Send 2 on DL251 output 2: {:?}", &peaks[..18]);
    assert!(peaks[MASTER_OUT as usize] > 0.1, "the masters on their XLRs");
    assert!(peaks[DL251_OUT as usize + 13] > 0.1 && peaks[DL251_OUT as usize + 14] > 0.1, "L and R on 14-15");
    // Output 16 is the mono master, off until a channel is sent to it.
    assert!(peaks[DL251_OUT as usize + 15] < 1e-5);
    ch.main_send(1).unwrap().on.store(true, Relaxed);
    ch.main_send(1).unwrap().level_db.store(0.0);
    let peaks = rig.run(80);
    assert!(peaks[DL251_OUT as usize + 15] > 0.1, "mono master on 16");
}

#[test]
fn the_processing_order_swaps_eq_and_dynamics() {
    let spec = Profile::MidasPro2.spec();
    let rig = Rig::new();
    let ch = rig.channel(0);
    let place = |ch: &Strip, o: i32, node: Node| {
        ch.params.process_order.store(o, Relaxed);
        (0..4).find(|&k| ch.slot_block(k) == Some(node)).unwrap()
    };
    assert_eq!(spec.process_orders.len(), 2);
    assert_eq!(place(&ch, 0, Node::Eq), 0, "EQ first by default");
    assert_eq!(place(&ch, 0, Node::PreInsert), 1);
    assert_eq!(place(&ch, 0, Node::Gate), 2);
    assert_eq!(place(&ch, 0, Node::Dynamics), 3);
    assert_eq!(place(&ch, 1, Node::Gate), 0, "C/O: gate first");
    assert_eq!(place(&ch, 1, Node::Dynamics), 1);
    assert_eq!(place(&ch, 1, Node::PreInsert), 2);
    assert_eq!(place(&ch, 1, Node::Eq), 3);
}

#[test]
fn the_fader_law_prints_the_panels_scale() {
    let spec = Profile::MidasPro2.spec();
    let fader = spec.fader_for(StripKind::Channel);
    assert_eq!(fader.steps, 1024);
    assert_eq!(fader, spec.fader_for(StripKind::Main));
    assert_eq!(fader, spec.fader_for(StripKind::Bus));
    assert_eq!(fader.max_db(), 10.0);
    assert!(fader.to_db(0.0) < -130.0, "the bottom is -oo");
    assert!((fader.to_pos(0.0) - 780.0 / 1023.0).abs() < 1e-3);
    // The scale, against the positions on the press render (p = (1141 - y) / 480).
    for (db, y) in [
        (5.0f32, 718.0f32),
        (-5.0, 834.0),
        (-10.0, 893.0),
        (-20.0, 954.0),
        (-30.0, 1015.0),
        (-40.0, 1047.0),
        (-50.0, 1079.0),
        (-70.0, 1110.0),
    ] {
        let pos = (1141.0 - y) / 480.0;
        assert!((fader.to_db(pos) - db).abs() < 2.5, "{db}: {}", fader.to_db(pos));
    }
}

#[test]
fn it_may_hold_only_its_own_models_and_the_universal_all() {
    let spec = Profile::MidasPro2.spec();
    assert!(spec.may_hold(DynSlot::Gate, "MPGAT"));
    assert!(spec.may_hold(DynSlot::Dynamics, "MPCMP"));
    assert!(!spec.may_hold(DynSlot::Gate, "MPCMP"), "a compressor isn't a gate");
    assert!(!spec.may_hold(DynSlot::Dynamics, "MPGAT"));
    // Shimmer only on the outputs.
    assert!(spec.may_hold_on(StripKind::Channel, DynSlot::Dynamics, "MPCMP"));
    assert!(!spec.may_hold_on(StripKind::Channel, DynSlot::Dynamics, "MPCMPO"), "the inputs have four modes");
    assert!(spec.may_hold_on(StripKind::Bus, DynSlot::Dynamics, "MPCMPO"));
    assert!(!spec.may_hold_on(StripKind::Main, DynSlot::Dynamics, "MPCMP"));
    assert!(!spec.models.table(0).iter().any(|m| m.token == "GATE"), "only its own models");
    assert!(spec.models.table(0).iter().all(|m| m.token.starts_with("MP")));
    let u = Profile::Universal.spec();
    for token in ["MPGAT", "MPCMP", "MPCMPO"] {
        assert!(u.models.table(0).iter().any(|m| m.token == token), "{token}");
        assert!(u.may_hold(DynSlot::Dynamics, token));
    }
    assert!(u.models.table(1).iter().any(|m| m.token == "MPEQ4"));
    assert!(u.models.table(2).iter().any(|m| m.token == "MPGEQ"));
    assert!(u.models.table(4).iter().any(|m| m.token == "MPDN780"));
    // A new output's compressor is the outputs' model, an input's the inputs'.
    let rig = Rig::new();
    let token = |s: &Strip| {
        let d = s.params.dynamics();
        d.table()[d.model_index()].token
    };
    assert_eq!(token(&rig.channel(0)), "MPCMP");
    assert_eq!(token(&rig.engine.strips(StripKind::Bus)[0]), "MPCMPO");
    assert_eq!(token(&rig.engine.strips(StripKind::Matrix)[0]), "MPCMPO");
    assert_eq!(token(&rig.engine.strips(StripKind::Main)[0]), "MPCMPO");
    assert_eq!(rig.channel(0).params.filter().low_cut_slope.load(Relaxed), 1, "12 dB/oct");
    assert_eq!(rig.channel(0).params.filter().high_cut_slope.load(Relaxed), 0, "6 dB/oct");
}
