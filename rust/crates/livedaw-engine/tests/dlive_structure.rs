//! A dLive's structure, end to end: Main modes, PAFL busses, Ext In from mixes, Mix
//! Source on the Matrices and Main, Source Selectors, ABCD, Direct Out, Group to Group,
//! Follow Main Pan and the stereo image.

use livedaw_engine::config::ConsoleConfig;
use livedaw_engine::dlive::{direct_source, group_source, stereo_image};
use livedaw_engine::params::{mix_source, StripKind};
use livedaw_engine::profile::{dlive, DLiveLayout, MainMode, Profile};
use livedaw_engine::routing::{strip_source_input, DirectSource, PAFL_SOURCE};
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
    fn of(layout: DLiveLayout) -> Self {
        let engine = Arc::new(Engine::new(0));
        engine.configure(&ConsoleConfig { profile: Profile::AhDLive(layout), counts: layout.counts() });
        let (num_ins, num_outs) = (64, 32);
        let in_map: Vec<i32> = (0..num_ins as i32).collect();
        let out_map: Vec<i32> = (0..num_outs as i32).collect();
        unsafe { engine.prepare(48000.0, BLOCK, &in_map, &out_map) };
        Self { engine, inputs: vec![vec![0.0; BLOCK]; num_ins], outputs: vec![vec![0.0; BLOCK]; num_outs], t: 0 }
    }

    fn main(mode: MainMode) -> Self {
        Self::of(DLiveLayout { main: mode, ..DLiveLayout::FOH })
    }

    /// A 1 kHz tone on inputs 1 and 2 (input 2 at half level); the peak each output
    /// reached over the last block.
    fn run(&mut self, blocks: usize) -> Vec<f32> {
        let ins: Vec<*const f32> = self.inputs.iter().map(|v| v.as_ptr()).collect();
        let outs: Vec<*mut f32> = self.outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
        for _ in 0..blocks {
            for i in 0..BLOCK {
                let x = 0.25 * (2.0 * std::f32::consts::PI * 1000.0 * (self.t + i) as f32 / 48000.0).sin();
                self.inputs[0][i] = x;
                self.inputs[1][i] = 0.5 * x;
            }
            self.t += BLOCK;
            unsafe { self.engine.process(&ins, &outs, BLOCK) };
        }
        self.outputs.iter().map(|o| o.iter().fold(0.0f32, |m, x| m.max(x.abs()))).collect()
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

    /// Input 1 up, off LR: only what's sent reaches an output.
    fn first_channel_off_the_main(&self) -> Arc<Strip> {
        let ch = self.up(0);
        ch.main_send(0).unwrap().on.store(false, Relaxed);
        ch
    }

    fn send_to(&self, channel: usize, bus: usize) {
        let ch = self.ch(channel);
        let send = ch.sends.get_or_create(bus).unwrap();
        send.enabled.store(true, Relaxed);
        send.level_db.store(0.0);
    }

    fn settle(&mut self) -> Vec<f32> {
        self.run(60)
    }
}

// ---- the Mixer Config ----------------------------------------------------------------

#[test]
fn one_stereo_pafl_takes_two_busses() {
    assert_eq!(DLiveLayout::FOH.pafl, 1);
    assert_eq!(DLiveLayout::FOH.busses_used(), 60);
    assert_eq!(DLiveLayout::FOH.busses_remaining(), 4, "the owner's config reads 4");
    let two = DLiveLayout { pafl: 2, ..DLiveLayout::FOH };
    assert_eq!(two.busses_remaining(), 2);
    assert!(two.is_valid());
    assert!(!DLiveLayout { pafl: DLiveLayout::MAX_PAFL + 1, ..DLiveLayout::FOH }.is_valid());
}

#[test]
fn each_main_mode_takes_its_busses() {
    let used = |m| DLiveLayout { main: m, ..DLiveLayout::FOH }.busses_used();
    assert_eq!(used(MainMode::None), 58);
    assert_eq!(used(MainMode::Lr), 60);
    assert_eq!(used(MainMode::LrMsum), 61);
    assert_eq!(used(MainMode::LrM), 61);
    assert_eq!(used(MainMode::Lcr), 61);
    assert_eq!(used(MainMode::LcrPlus), 61);
    assert_eq!(used(MainMode::Surround), 64);
    assert_eq!(DLiveLayout { main: MainMode::Surround, ..DLiveLayout::FOH }.counts().mains, 4);
}

#[test]
fn a_show_keeps_pafl_and_main_mode_and_old_shows_still_load() {
    let layout = DLiveLayout { pafl: 2, main: MainMode::LcrPlus, ..DLiveLayout::FOH };
    assert_eq!(DLiveLayout::from_text(&layout.to_text()), Some(layout));
    let old = format!("{},{}", DLiveLayout::FOH.values().map(|n| n.to_string()).join(","), DLiveLayout::FOH.stereo_inputs);
    assert_eq!(DLiveLayout::from_text(&old), Some(DLiveLayout::FOH), "from before PAFL counted: one bus");
    // A show that filled all 64 under the old count, whose PAFL bus now can't fit.
    let full = format!("0,0,62,0,0,0,0,0,{}", DLiveLayout::FOH.stereo_inputs);
    let loaded = DLiveLayout::from_text(&full).unwrap();
    assert_eq!(loaded.pafl, 0);
    assert!(loaded.is_valid());
}

// ---- Main modes ----------------------------------------------------------------------

const C_L: usize = 2; // the second main on outputs 3 and 4

#[test]
fn lr_plus_m_assigns_each_independently() {
    let mut rig = Rig::main(MainMode::LrM);
    let ch = rig.up(0);
    let peaks = rig.settle();
    assert!(peaks[0] > 0.05, "on LR");
    assert!(peaks[C_L] < 1e-6, "not assigned to M yet");
    ch.main_send(1).unwrap().on.store(true, Relaxed);
    let peaks = rig.settle();
    assert!((peaks[C_L] - 0.25).abs() < 0.02, "M takes it unpanned at unity: {}", peaks[C_L]);
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    let peaks = rig.settle();
    assert!(peaks[0] < 1e-6, "off LR");
    assert!(peaks[C_L] > 0.2, "still on M");
}

#[test]
fn lr_plus_msum_is_the_mono_sum_before_the_lr_master() {
    let mut rig = Rig::main(MainMode::LrMsum);
    rig.up(0);
    let lr = rig.settle()[0];
    let main = rig.engine.strips(StripKind::Main)[0].clone();
    main.params.fader_db.store(-60.0);
    let peaks = rig.settle();
    assert!(peaks[0] < lr * 0.01, "the LR master came down");
    assert!((peaks[C_L] - lr).abs() < 0.02, "Msum is pre-fade: {} against {lr}", peaks[C_L]);
}

#[test]
fn lcr_at_mid_pan_is_all_in_the_centre() {
    let mut rig = Rig::main(MainMode::Lcr);
    let ch = rig.up(0);
    let peaks = rig.settle();
    assert!(peaks[0] < 1e-5 && peaks[1] < 1e-5, "none to L and R: {} {}", peaks[0], peaks[1]);
    assert!((peaks[C_L] - 0.25).abs() < 0.02, "all to C: {}", peaks[C_L]);
    ch.params.pan.store(-100.0);
    let peaks = rig.settle();
    assert!((peaks[0] - 0.25).abs() < 0.02 && peaks[1] < 1e-5, "hard left: {} {}", peaks[0], peaks[1]);
    assert!(peaks[C_L] < 1e-3, "nothing left in C: {}", peaks[C_L]);
}

#[test]
fn lcr_keeps_stereo_channels_on_lr() {
    let mut rig = Rig::main(MainMode::Lcr);
    let last = rig.engine.strips(StripKind::Channel).len() - 1;
    let st = rig.up(last);
    st.params.input_left.store(0, Relaxed);
    st.params.input_right.store(1, Relaxed);
    let peaks = rig.settle();
    assert!(peaks[0] > 0.1 && peaks[1] > 0.05, "LR carries it: {} {}", peaks[0], peaks[1]);
    assert!(peaks[C_L] < 1e-6, "not C: {}", peaks[C_L]);
}

#[test]
fn lcr_plus_divergence_blends_c_against_lr() {
    let mut rig = Rig::main(MainMode::LcrPlus);
    let ch = rig.up(0);
    ch.dlive.divergence.store(0.0);
    let peaks = rig.settle();
    assert!(peaks[0] < 1e-5, "no divergence: no LR");
    assert!((peaks[C_L] - 0.25).abs() < 0.02, "C alone: {}", peaks[C_L]);
    ch.dlive.divergence.store(100.0);
    let peaks = rig.settle();
    assert!(peaks[C_L] < 1e-5, "full divergence: no C");
    assert!(peaks[0] > 0.1, "a phantom centre on LR: {}", peaks[0]);
}

#[test]
fn none_has_no_main_mix() {
    let mut rig = Rig::main(MainMode::None);
    rig.up(0);
    let peaks = rig.settle();
    assert!(peaks[0] < 1e-6 && peaks[1] < 1e-6, "no Main: {} {}", peaks[0], peaks[1]);
}

#[test]
fn surround_has_lfe_and_rear_and_their_own_levels() {
    let mut rig = Rig::main(MainMode::Surround);
    assert_eq!(rig.engine.num_strips(StripKind::Main), 4);
    let names: Vec<String> = rig.engine.strips(StripKind::Main).iter().map(|m| m.name()).collect();
    assert_eq!(names, ["LR", "C", "LFE", "Ls/Rs"]);
    let ch = rig.up(0);
    // Outputs: LR 1-2, C 3, LFE 4, Ls/Rs 5-6.
    ch.dlive.lfe_db.store(0.0);
    let peaks = rig.settle();
    assert!(peaks[0] > 0.1, "LR panning by default: {}", peaks[0]);
    assert!(peaks[2] < 1e-5, "no C in LR mode");
    assert!((peaks[3] - 0.25).abs() < 0.02, "the LFE level: {}", peaks[3]);
    assert!(peaks[4] < 1e-3, "at the front, nothing in the rear");
    ch.dlive.surround_rear.store(100.0);
    let peaks = rig.settle();
    assert!(peaks[0] < 1e-3, "at the rear, nothing in the front");
    assert!(peaks[4] > 0.1 && peaks[5] > 0.1, "Ls/Rs: {} {}", peaks[4], peaks[5]);
    ch.dlive.non_lfe_db.store(-120.0);
    let peaks = rig.settle();
    assert!(peaks[4] < 1e-5, "the 5.0 mix turned down");
    assert!(peaks[3] > 0.2, "the LFE still there");
}

// ---- Ext In, Mix Source ----------------------------------------------------------------

#[test]
fn ext_in_takes_another_mix_a_block_late() {
    let mut rig = Rig::of(DLiveLayout::FOH);
    rig.first_channel_off_the_main();
    rig.send_to(0, dlive::AUX); // Aux 1, output 3
    let aux2 = rig.engine.strips(StripKind::Bus)[dlive::AUX + 1].clone();
    aux2.mix_input.ext_source.store(strip_source_input(StripKind::Bus, dlive::AUX, 0), Relaxed);
    let peaks = rig.settle();
    assert!(peaks[2] > 0.2, "Aux 1 itself: {}", peaks[2]);
    assert!((peaks[3] - peaks[2]).abs() < 0.02, "Aux 2 carries Aux 1: {} {}", peaks[2], peaks[3]);
}

#[test]
fn a_mix_cant_take_itself_as_its_ext_in() {
    let mut rig = Rig::of(DLiveLayout::FOH);
    rig.first_channel_off_the_main();
    rig.send_to(0, dlive::AUX);
    let aux = rig.engine.strips(StripKind::Bus)[dlive::AUX].clone();
    aux.mix_input.ext_source.store(strip_source_input(StripKind::Bus, dlive::AUX, 0), Relaxed);
    let peak = rig.settle()[2];
    assert!((peak - 0.25).abs() < 0.02, "no feedback: {peak}");
}

#[test]
fn ext_in_takes_the_pafl_bus() {
    let mut rig = Rig::of(DLiveLayout::FOH);
    let ch = rig.first_channel_off_the_main();
    let aux = rig.engine.strips(StripKind::Bus)[dlive::AUX].clone();
    aux.mix_input.ext_source.store(PAFL_SOURCE, Relaxed);
    assert!(rig.settle()[2] < 1e-6, "nothing on PAFL");
    ch.params.solo.store(true, Relaxed);
    assert!(rig.settle()[2] > 0.05, "the soloed channel reaches the mix");
}

#[test]
fn mix_source_works_on_the_matrices_and_main() {
    let mut rig = Rig::of(DLiveLayout::FOH);
    let ch = rig.first_channel_off_the_main();
    ch.params.fader_db.store(-40.0);
    // A pre-fade send to Matrix 1.
    let send = ch.matrix_send(0).unwrap();
    send.on.store(true, Relaxed);
    send.pre.store(true, Relaxed);
    send.level_db.store(0.0);
    let mtx = rig.engine.strips(StripKind::Matrix)[0].clone();
    let mtx_output = 2 + usize::from(DLiveLayout::FOH.aux) + 2 * usize::from(DLiveLayout::FOH.st_aux);
    mtx.mix_input.mix_source.store(mix_source::POST_PREAMP, Relaxed);
    rig.engine.collect();
    let eq = ch.params.eq();
    let at = |id: &str| livedaw_dsp::dlive::EQ[0].params.iter().position(|p| p.id == id).unwrap();
    eq.on.store(true, Relaxed);
    eq.values[at("1f")].store(1000.0);
    eq.values[at("1g")].store(12.0);
    let before = rig.settle()[mtx_output];
    mtx.mix_input.mix_source.store(mix_source::POST_PEQ, Relaxed);
    let after = rig.settle()[mtx_output];
    assert!(before > 0.1, "Post Preamp, despite the fader: {before}");
    assert!(after / before > 3.0, "Post PEQ has the boost: {before} then {after}");

    // And the Main, on a pre-fade send to LR.
    let lr = ch.main_send(0).unwrap();
    lr.on.store(true, Relaxed);
    lr.pre.store(true, Relaxed);
    let main = rig.engine.strips(StripKind::Main)[0].clone();
    main.mix_input.mix_source.store(mix_source::POST_PREAMP, Relaxed);
    let before = rig.settle()[0];
    main.mix_input.mix_source.store(mix_source::POST_PEQ, Relaxed);
    let after = rig.settle()[0];
    assert!(after / before > 3.0, "the Main's Mix Source: {before} then {after}");
}

// ---- Source Selectors ------------------------------------------------------------------

#[test]
fn a_source_selector_plays_one_source_and_crossfades() {
    let mut rig = Rig::of(DLiveLayout::FOH);
    rig.first_channel_off_the_main();
    rig.up(1).main_send(0).unwrap().on.store(false, Relaxed);
    rig.send_to(0, dlive::AUX);
    rig.send_to(1, dlive::AUX);
    let e = rig.engine.clone();
    e.set_selector_output(0, Some((StripKind::Bus, dlive::AUX)));
    e.set_selector_source(0, 0, Some((StripKind::Channel, 1)));
    e.set_selector_source(0, 1, Some((StripKind::Channel, 2)));
    e.dlive.selectors[0].fade_in_s.store(0.01);
    e.dlive.selectors[0].fade_out_s.store(0.01);

    e.dlive.selectors[0].active.store(0, Relaxed);
    assert!((rig.settle()[2] - 0.25).abs() < 0.02, "source 1 alone");
    e.dlive.selectors[0].active.store(1, Relaxed);
    assert!((rig.settle()[2] - 0.125).abs() < 0.02, "source 2 alone");
    e.dlive.selectors[0].active.store(-1, Relaxed);
    assert!(rig.settle()[2] < 1e-6, "Off");
}

#[test]
fn what_a_selector_doesnt_hold_stays_on_its_output() {
    let mut rig = Rig::of(DLiveLayout::FOH);
    rig.first_channel_off_the_main();
    rig.up(1).main_send(0).unwrap().on.store(false, Relaxed);
    rig.send_to(0, dlive::AUX);
    rig.send_to(1, dlive::AUX); // a paging mic, not a member
    let e = rig.engine.clone();
    e.set_selector_output(0, Some((StripKind::Bus, dlive::AUX)));
    e.set_selector_source(0, 0, Some((StripKind::Channel, 1)));
    e.dlive.selectors[0].active.store(-1, Relaxed);
    let page = rig.settle()[2];
    assert!((page - 0.125).abs() < 0.02, "the priority source: {page}");
    // Freed, the member plays again.
    e.set_selector_source(0, 0, None);
    assert!(rig.settle()[2] > 0.3, "both play with no selector");
}

#[test]
fn a_fade_takes_its_time() {
    let mut rig = Rig::of(DLiveLayout::FOH);
    rig.first_channel_off_the_main();
    rig.send_to(0, dlive::AUX);
    let e = rig.engine.clone();
    e.set_selector_output(0, Some((StripKind::Bus, dlive::AUX)));
    e.set_selector_source(0, 0, Some((StripKind::Channel, 1)));
    e.dlive.selectors[0].fade_in_s.store(1.0);
    e.dlive.selectors[0].active.store(0, Relaxed);
    // 1 s is 375 blocks: at 94 blocks it's a quarter of the way.
    let early = rig.run(94)[2];
    assert!((early - 0.0625).abs() < 0.015, "a quarter in: {early}");
    let later = rig.run(281)[2];
    assert!((later - 0.25).abs() < 0.01, "all in: {later}");
}

// ---- ABCD ----------------------------------------------------------------------------

#[test]
fn abcd_switches_a_channels_source() {
    let mut rig = Rig::of(DLiveLayout::FOH);
    let ch = rig.up(0);
    let e = rig.engine.clone();
    ch.dlive.abcd_left[1].store(1, Relaxed);
    e.abcd_enable(&ch, true);
    assert_eq!(ch.dlive.abcd_left[0].load(Relaxed), 0, "A took the channel's own patch");
    let a = rig.settle()[0];
    e.abcd_select(&ch, 1);
    assert_eq!(ch.params.input_left.load(Relaxed), 1);
    let b = rig.settle()[0];
    assert!((a / b - 2.0).abs() < 0.2, "input 2 is half level: {a} {b}");
    e.abcd_set_all(0);
    assert_eq!(ch.params.input_left.load(Relaxed), 0, "Set All");
}

#[test]
fn abcd_off_ignores_a_selection() {
    let rig = Rig::of(DLiveLayout::FOH);
    let ch = rig.up(0);
    ch.dlive.abcd_left[1].store(9, Relaxed);
    rig.engine.abcd_select(&ch, 1);
    assert_eq!(ch.params.input_left.load(Relaxed), 0, "only remembered");
    rig.engine.abcd_set_range(0, 0, 1);
    assert_eq!(ch.params.input_left.load(Relaxed), 0);
}

#[test]
fn abcd_for_a_dca_is_its_members() {
    let rig = Rig::of(DLiveLayout::FOH);
    let (a, b) = (rig.up(0), rig.up(1));
    for s in [&a, &b] {
        s.dlive.abcd_left[1].store(20, Relaxed);
        rig.engine.abcd_enable(s, true);
    }
    let mut set = livedaw_engine::GroupSet::EMPTY;
    set.insert(3);
    a.params.dca_mask.store(set);
    rig.engine.abcd_set_for_dca(3, 1);
    assert_eq!(a.params.input_left.load(Relaxed), 20);
    assert_eq!(b.params.input_left.load(Relaxed), 1, "its own patch: not a member");
}

// ---- Direct Out ------------------------------------------------------------------------

/// Channel 1's Direct Out on output 20, a dLive's global source and level.
fn direct_rig() -> (Rig, Arc<Strip>) {
    let rig = Rig::of(DLiveLayout::FOH);
    let ch = rig.first_channel_off_the_main();
    let code = DirectSource { kind: StripKind::Channel, number: 1, side: 0, post: false }.code();
    rig.engine.output_patch().set(19, code);
    rig.engine.collect();
    (rig, ch)
}

#[test]
fn direct_out_takes_the_global_source() {
    let (mut rig, ch) = direct_rig();
    let eq = ch.params.eq();
    let at = |id: &str| livedaw_dsp::dlive::EQ[0].params.iter().position(|p| p.id == id).unwrap();
    eq.on.store(true, Relaxed);
    eq.values[at("1f")].store(1000.0);
    eq.values[at("1g")].store(12.0);
    rig.engine.dlive.direct_source.store(direct_source::POST_PREAMP, Relaxed);
    let before = rig.settle()[19];
    rig.engine.dlive.direct_source.store(direct_source::POST_PEQ, Relaxed);
    let after = rig.settle()[19];
    assert!((before - 0.25).abs() < 0.02, "Post Preamp: {before}");
    assert!(after / before > 3.0, "Post PEQ: {before} then {after}");
}

#[test]
fn direct_out_has_its_own_level() {
    let (mut rig, ch) = direct_rig();
    ch.dlive.direct_db.store(-6.0);
    let peak = rig.settle()[19];
    assert!((peak - 0.125).abs() < 0.01, "6 dB down: {peak}");
}

#[test]
fn direct_out_follows_as_set() {
    let (mut rig, ch) = direct_rig();
    let e = rig.engine.clone();
    ch.params.fader_db.store(-20.0);
    ch.params.mute.store(true, Relaxed);
    assert!(rig.settle()[19] > 0.2, "follows nothing by default");
    e.dlive.direct_follow_mute.store(true, Relaxed);
    assert!(rig.settle()[19] < 1e-5, "Follow Mute");
    e.dlive.direct_follow_mute.store(false, Relaxed);
    e.dlive.direct_follow_fader.store(true, Relaxed);
    let down = rig.settle()[19];
    assert!((down - 0.025).abs() < 0.005, "Follow Fader: {down}");
    e.dlive.direct_follow_fader.store(false, Relaxed);
    e.dlive.direct_follow_all.store(true, Relaxed);
    assert!(rig.settle()[19] < 1e-5, "Follow All: the mute");
}

// ---- Group to Group, Follow Main Pan, stereo image ----------------------------------------

#[test]
fn group_to_group_leaves_at_its_source_point() {
    let layout = DLiveLayout::FOH;
    let mut rig = Rig::of(layout);
    rig.first_channel_off_the_main();
    let g1 = rig.engine.strips(StripKind::Bus)[0].clone();
    // Group 1 feeds the channel's signal to Group 2, which goes to LR.
    rig.send_to(0, 0);
    let send = g1.sends.get_or_create(1).unwrap();
    send.enabled.store(true, Relaxed);
    send.level_db.store(0.0);
    g1.main_send(0).unwrap().on.store(false, Relaxed);
    // Post Comp and Post Delay both come before the fader: down 40 dB, it still feeds.
    g1.params.fader_db.store(-40.0);
    g1.dlive.group_source.store(group_source::POST_COMP, Relaxed);
    rig.engine.collect();
    let post_comp = rig.settle()[0];
    g1.dlive.group_source.store(group_source::POST_DELAY, Relaxed);
    let post_delay = rig.settle()[0];
    assert!(post_comp > 0.05, "Post Comp is pre-fader: {post_comp}");
    assert!(post_delay > 0.05, "Post Delay is pre-fader: {post_delay}");
}

#[test]
fn follow_main_pan_ties_a_stereo_mixs_sends_to_the_main_pans() {
    let mut rig = Rig::of(DLiveLayout::FOH);
    let ch = rig.first_channel_off_the_main();
    let st_aux = dlive::ST_AUX;
    rig.send_to(0, st_aux);
    let mix = rig.engine.strips(StripKind::Bus)[st_aux].clone();
    ch.params.pan.store(-100.0);
    let (l, r) = (10usize, 11usize); // Stereo Aux 1's outputs
    rig.engine.set_follow_main_pan(&mix, false, false);
    let peaks = rig.settle();
    assert!(peaks[r] > 0.05, "the send's own centre pan: {} {}", peaks[l], peaks[r]);
    rig.engine.set_follow_main_pan(&mix, true, false);
    let peaks = rig.settle();
    assert!(peaks[l] > 0.1 && peaks[r] < 1e-3, "follows left: {} {}", peaks[l], peaks[r]);
    rig.engine.set_follow_main_pan(&mix, true, true);
    let peaks = rig.settle();
    assert!(peaks[r] > 0.1 && peaks[l] < 1e-3, "mirrored: {} {}", peaks[l], peaks[r]);
}

#[test]
fn invert_alone_copies_the_pans_once() {
    let rig = Rig::of(DLiveLayout::FOH);
    let ch = rig.ch(0);
    let mix = rig.engine.strips(StripKind::Bus)[dlive::ST_AUX].clone();
    ch.params.pan.store(-30.0);
    rig.engine.set_follow_main_pan(&mix, false, true);
    let send = ch.sends.get_or_create(dlive::ST_AUX).unwrap();
    assert_eq!(send.pan.load(), 30.0);
    assert!(!send.pan_link.load(Relaxed));
    ch.params.pan.store(-70.0);
    assert_eq!(send.pan.load(), 30.0, "not tied");
}

#[test]
fn the_stereo_image_comes_ahead_of_a_stereo_channel() {
    let mut rig = Rig::of(DLiveLayout::FOH);
    let last = rig.engine.strips(StripKind::Channel).len() - 1;
    let st = rig.up(last);
    st.params.input_left.store(0, Relaxed);
    st.params.input_right.store(1, Relaxed);
    let peaks = rig.settle();
    assert!(peaks[0] > peaks[1] * 1.5, "input 1 is the louder, on the left: {} {}", peaks[0], peaks[1]);
    st.dlive.stereo_image.store(stereo_image::RL, Relaxed);
    let peaks = rig.settle();
    assert!(peaks[1] > peaks[0] * 1.5, "reversed: {} {}", peaks[0], peaks[1]);
    st.dlive.stereo_image.store(stereo_image::LR, Relaxed);
    st.dlive.image_width.store(0.0);
    let peaks = rig.settle();
    assert!((peaks[0] - peaks[1]).abs() < 1e-3, "width 0 is mono: {} {}", peaks[0], peaks[1]);
}
