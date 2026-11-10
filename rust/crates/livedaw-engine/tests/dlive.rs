//! A dLive console, end to end: audio through its channels, and through mixes
//! and FX sends past the WING's 16 buses.

use livedaw_engine::blocks::{BlockKind, BlockParams, Step};
use livedaw_engine::chain::Node;
use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::params::{DynSlotParams, StripKind};
use livedaw_engine::profile::{dlive, DLiveLayout, DynSlot, Profile};
use livedaw_engine::{Engine, Strip};
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
        Self::of(DLiveLayout::FOH)
    }

    fn of(layout: DLiveLayout) -> Self {
        let engine = Arc::new(Engine::new(0));
        engine.configure(&ConsoleConfig { profile: Profile::AhDLive(layout), counts: layout.counts() });
        let (num_ins, num_outs) = (64, 32);
        let in_map: Vec<i32> = (0..num_ins as i32).collect();
        let out_map: Vec<i32> = (0..num_outs as i32).collect();
        unsafe { engine.prepare(48000.0, BLOCK, &in_map, &out_map) };
        Self { engine, inputs: vec![vec![0.0; BLOCK]; num_ins], outputs: vec![vec![0.0; BLOCK]; num_outs], t: 0 }
    }

    /// Runs `blocks` blocks of a 1 kHz tone on input 1; the peak each output
    /// reached over the last of them.
    fn run(&mut self, blocks: usize) -> Vec<f32> {
        let ins: Vec<*const f32> = self.inputs.iter().map(|v| v.as_ptr()).collect();
        let outs: Vec<*mut f32> = self.outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
        for _ in 0..blocks {
            for (i, x) in self.inputs[0].iter_mut().enumerate() {
                *x = 0.25 * (2.0 * std::f32::consts::PI * 1000.0 * (self.t + i) as f32 / 48000.0).sin();
            }
            self.t += BLOCK;
            unsafe { self.engine.process(&ins, &outs, BLOCK) };
        }
        self.outputs.iter().map(|o| o.iter().fold(0.0f32, |m, x| m.max(x.abs()))).collect()
    }

    /// Ip 1 up and unmuted, off LR: only what's sent reaches an output.
    fn first_channel_off_the_main(&self) {
        let ch = &self.engine.strips(StripKind::Channel)[0];
        ch.params.mute.store(false, Relaxed);
        ch.params.fader_db.store(0.0);
        ch.main_send(0).unwrap().on.store(false, Relaxed);
    }

    fn send_to(&self, bus: usize) {
        let channels = self.engine.strips(StripKind::Channel);
        let send = channels[0].sends.get_or_create(bus).unwrap();
        send.enabled.store(true, Relaxed);
        send.level_db.store(0.0);
    }
}

#[test]
fn every_input_starts_muted_and_a_channel_reaches_lr_when_unmuted() {
    let mut rig = Rig::new();
    let ch = rig.engine.strips(StripKind::Channel)[0].clone();
    ch.params.fader_db.store(0.0);
    assert!(rig.run(50)[0] < 1e-6, "all channels muted, as the template starts");
    ch.params.mute.store(false, Relaxed);
    assert!(rig.run(50)[0] > 0.05, "Ip 1 on LR, on the MixRack's outputs 1-2");
}

#[test]
fn a_stereo_aux_past_bus_16_reaches_its_outputs() {
    let mut rig = Rig::new();
    rig.first_channel_off_the_main();
    rig.send_to(dlive::ST_AUX + 7);
    let peaks = rig.run(50);
    assert!(peaks[24] > 0.05 && peaks[25] > 0.05, "StAux 8 on the MixRack's 25-26: {} {}", peaks[24], peaks[25]);
    assert!(peaks[0] < 1e-6, "not on LR");
    assert!(peaks[2] < 1e-6, "not on Aux 1's output");
}

#[test]
fn an_fx_send_past_bus_16_feeds_its_fx_and_comes_back_on_its_return() {
    let mut rig = Rig::new();
    rig.first_channel_off_the_main();
    rig.send_to(dlive::FX + 2); // FX 3: the Stereo Tap Delay
    let ret = rig.engine.strips(StripKind::Aux)[2].clone();
    ret.params.mute.store(false, Relaxed);
    ret.params.fader_db.store(0.0);
    assert!(rig.run(800)[0] > 1e-3, "through FX 3 and back on its return, to LR");
}

#[test]
fn another_mixer_config_has_its_mixes_on_its_outputs() {
    // No mono Aux: Stereo Aux 1 comes first after the LR, on outputs 3-4.
    let layout = DLiveLayout { grp: 2, st_grp: 2, aux: 0, st_aux: 12, fx: 4, st_fx: 2, mtx: 2, st_mtx: 2, ..DLiveLayout::FOH };
    assert!(layout.is_valid());
    let mut rig = Rig::of(layout);
    assert_eq!(rig.engine.profile(), Profile::AhDLive(layout));
    assert_eq!(rig.engine.counts(), layout.counts());
    rig.first_channel_off_the_main();
    rig.send_to(layout.st_aux_at());
    let peaks = rig.run(50);
    assert!(peaks[2] > 0.05 && peaks[3] > 0.05, "Stereo Aux 1 on the MixRack's 3-4: {} {}", peaks[2], peaks[3]);
    assert!(peaks[0] < 1e-6, "not on LR");
}

#[test]
fn ext_in_sums_into_a_mix_with_its_own_trim_and_polarity() {
    let mut rig = Rig::new();
    let aux = rig.engine.strips(StripKind::Bus)[dlive::AUX].clone();
    // Every channel is muted: only the Ext In can reach Aux 1's output, the MixRack's 3.
    assert!(rig.run(20)[2] < 1e-6);
    aux.mix_input.ext_source.store(0, Relaxed);
    rig.run(10);
    let full = rig.run(50)[2];
    assert!(full > 0.2, "Input 1 on Aux 1: {full}");
    aux.mix_input.ext_trim_db.store(-6.02);
    rig.run(10);
    let half = rig.run(50)[2];
    assert!((half / full - 0.5).abs() < 0.02, "trim: {half} of {full}");
    aux.params.fader_db.store(-60.0);
    rig.run(10);
    assert!(rig.run(50)[2] < half * 0.01, "the mix's fader follows it");
}

#[test]
fn mix_source_sets_where_pre_fade_sends_leave_the_channel() {
    use livedaw_engine::params::mix_source;
    let mut rig = Rig::new();
    rig.first_channel_off_the_main();
    let ch = rig.engine.strips(StripKind::Channel)[0].clone();
    // 12 dB of PEQ at the tone, the fader well down: a pre-fade send doesn't care about that.
    let eq = ch.params.eq();
    let at = |id: &str| livedaw_dsp::dlive::EQ[0].params.iter().position(|p| p.id == id).unwrap();
    eq.on.store(true, Relaxed);
    eq.values[at("1f")].store(1000.0);
    eq.values[at("1g")].store(12.0);
    ch.params.fader_db.store(-40.0);
    rig.send_to(dlive::AUX);
    let aux = rig.engine.strips(StripKind::Bus)[dlive::AUX].clone();
    rig.engine.collect(); // as the app does: the taps' buffers

    aux.mix_input.mix_source.store(mix_source::POST_PREAMP, Relaxed);
    rig.run(50);
    let before_eq = rig.run(50)[2];
    aux.mix_input.mix_source.store(mix_source::POST_PEQ, Relaxed);
    rig.run(50);
    let after_eq = rig.run(50)[2];
    assert!(before_eq > 0.2, "pre-fade, despite the fader: {before_eq}");
    assert!(after_eq / before_eq > 3.0, "the PEQ's boost: {before_eq} then {after_eq}");
}

#[test]
fn post_preamp_sends_carry_the_preamp() {
    use livedaw_engine::params::mix_source;
    let mut rig = Rig::new();
    rig.first_channel_off_the_main();
    let ch = rig.engine.strips(StripKind::Channel)[0].clone();
    ch.params.fader_db.store(-40.0); // a pre-fade send doesn't care
    rig.send_to(dlive::AUX);
    let aux = rig.engine.strips(StripKind::Bus)[dlive::AUX].clone();
    aux.mix_input.mix_source.store(mix_source::POST_PREAMP, Relaxed);
    rig.engine.collect(); // as the app does: the taps' buffers
    rig.run(50);
    let without = rig.run(50)[2];
    let block = ch.preamp_block().unwrap();
    let d = params(&block);
    d.param("out").unwrap().store(12.0);
    d.on.store(true, Relaxed);
    rig.run(50);
    let with = rig.run(50)[2];
    assert!(with > without * 3.0, "the preamp's OUTPUT is in the send: {without} then {with}");
}

#[test]
fn new_sends_are_pre_to_aux_and_post_to_groups_and_fx() {
    use livedaw_engine::sends::send_mode;
    let rig = Rig::new();
    let ch = rig.engine.strips(StripKind::Channel)[0].clone();
    let mode = |bus: usize| ch.sends.get_or_create(bus).unwrap().mode.load(Relaxed);
    assert_eq!(mode(0), send_mode::POST, "a Group");
    assert_eq!(mode(dlive::AUX), send_mode::PRE, "an Aux");
    assert_eq!(mode(dlive::ST_AUX + 3), send_mode::PRE, "a stereo Aux");
    assert_eq!(mode(dlive::FX), send_mode::POST, "an FX send");
}

#[test]
fn a_dlive_runs_with_every_block_off_and_its_mixes_patched() {
    // Every strip of a full console, once, with signal on each input.
    let mut rig = Rig::new();
    for ch in rig.engine.strips(StripKind::Channel) {
        ch.params.mute.store(false, Relaxed);
    }
    for slot in 0..64 {
        rig.inputs[slot].fill(0.01);
    }
    let peaks = rig.run(100);
    assert!(peaks.iter().all(|p| p.is_finite()));
}

/// The strip's preamp block's parameters.
fn preamp_of(strip: &Strip) -> (std::sync::Arc<livedaw_engine::blocks::Block>, usize) {
    let block = strip.preamp_block().expect("a preamp block");
    let index = strip.blocks().iter().position(|b| b.uid() == block.uid()).unwrap();
    (block, index)
}

fn params(block: &livedaw_engine::blocks::Block) -> &DynSlotParams {
    let BlockParams::Dynamics(d) = &block.params else { panic!("a dynamics block") };
    d
}

#[test]
fn every_input_channel_starts_with_its_preamp_off_ahead_of_its_filter() {
    let rig = Rig::new();
    let channels = rig.engine.strips(StripKind::Channel);
    assert_eq!(channels.len(), Counts::DLIVE.channels);
    for ch in &channels {
        let (block, _) = preamp_of(ch);
        assert_eq!(block.kind(), BlockKind::Dynamics);
        let d = params(&block);
        assert!(!d.on.load(Relaxed), "off");
        assert_eq!(d.model_spec().unwrap().token, "DLVVALVE");
        assert_eq!(ch.blocks().len(), 1, "one extra block");
        let steps = ch.steps();
        let at = |node: Node| steps.iter().position(|s| matches!(s, Step::Node(n) if *n == node)).unwrap();
        let (input, filter) = (at(Node::Input), at(Node::Filter));
        assert!(matches!(&steps[input + 1], Step::Block(b) if b.uid() == block.uid()), "right after INPUT");
        assert!(input + 1 < filter);
    }
    // Only the input channels: not the FX returns, mixes, LR or matrices.
    for kind in [StripKind::Aux, StripKind::Bus, StripKind::Main, StripKind::Matrix] {
        for strip in rig.engine.strips(kind) {
            assert!(strip.preamp_block().is_none(), "{kind:?}");
            assert!(strip.blocks().iter().all(|b| b.kind() != BlockKind::Dynamics), "{kind:?}");
        }
    }
}

#[test]
fn no_other_console_has_a_preamp_block() {
    for profile in Profile::ALL {
        if matches!(profile, Profile::AhDLive(_) | Profile::AhSq5) {
            // the SQ-5 has the DEEP preamp block
            continue;
        }
        let engine = Engine::new(0);
        engine.configure(&ConsoleConfig { profile, counts: profile.spec().counts });
        for kind in StripKind::ALL {
            for strip in engine.strips(kind) {
                assert!(strip.preamp_block().is_none(), "{profile:?} {kind:?}");
                assert!(strip.blocks().iter().all(|b| b.kind() != BlockKind::Dynamics), "{profile:?} {kind:?}");
            }
        }
    }
}

#[test]
fn a_dlive_offers_its_preamp_models_for_its_preamp_block_alone() {
    let spec = Profile::AhDLive(DLiveLayout::FOH).spec();
    for token in ["DLVVALVE", "DLVTUBE"] {
        assert!(!spec.may_hold(DynSlot::Gate, token), "{token}");
        assert!(!spec.may_hold(DynSlot::Dynamics, token), "{token}");
        assert!(spec.may_hold(DynSlot::Preamp, token), "{token}");
    }
    for token in ["DLVGATE", "DLVPEAK"] {
        assert!(spec.may_hold(DynSlot::Gate, token), "{token}");
        assert!(spec.may_hold(DynSlot::Dynamics, token), "{token}");
        assert!(!spec.may_hold(DynSlot::Preamp, token), "{token}");
    }
    // The Universal offers every console's models anywhere; so does the WING's table its own.
    let universal = Profile::Universal.spec();
    assert!(universal.may_hold(DynSlot::Gate, "DLVVALVE") && universal.may_hold(DynSlot::Dynamics, "DLVTUBE"));
    assert!(universal.may_hold(DynSlot::Gate, "GATE"));
    assert!(Profile::Wing.spec().may_hold(DynSlot::Dynamics, "COMP"));
}

#[test]
fn the_preamp_is_inaudible_off_and_colours_the_channel_on() {
    let run = |set: &dyn Fn(&Rig)| {
        let mut rig = Rig::new();
        let ch = rig.engine.strips(StripKind::Channel)[0].clone();
        ch.params.mute.store(false, Relaxed);
        ch.params.fader_db.store(0.0);
        set(&rig);
        rig.run(400);
        rig.run(1);
        rig.outputs[0].clone()
    };
    let same = run(&|_| {});
    let off = run(&|rig: &Rig| {
        let ch = rig.engine.strips(StripKind::Channel)[0].clone();
        let (block, _) = preamp_of(&ch);
        rig.engine.remove_block(&ch, block.uid());
    });
    assert_eq!(same, off, "the block off is as if it weren't there");

    let on = run(&|rig: &Rig| {
        let ch = rig.engine.strips(StripKind::Channel)[0].clone();
        let (block, _) = preamp_of(&ch);
        let d = params(&block);
        d.param("harm").unwrap().store(100.0);
        d.on.store(true, Relaxed);
    });
    assert!(on.iter().zip(&same).any(|(a, b)| (a - b).abs() > 0.01), "with it on, the signal has changed");
}
