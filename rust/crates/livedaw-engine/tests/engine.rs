//! Engine behaviour tests: the WING signal flow end to end.

use livedaw_dsp::model::model_index;
use livedaw_dsp::wing::{DYN_MODELS, EQ_MODELS_CH, PROCESS_ORDERS};
mod common;
use common::{set_channels, unity_console};
use livedaw_engine::params::{StripKind, FX_EXT, FX_NONE, MAX_FX, WING_AUX, WING_BUSES, WING_FX, WING_MAINS, WING_MATRICES};
use livedaw_engine::params::{MAIN_SEND_PAGE, MAX_DCAS, MAX_MUTE_GROUPS};
use livedaw_engine::{ConsoleConfig, Counts, Engine, GroupSet, InsertCallbacks, Strip};
use std::ffi::c_void;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering::Relaxed};
use std::sync::Arc;

const BLOCK: usize = 128;
/// A mono channel's gain on each side at pan centre (the WING's pan law: unity).
const CENTRE: f32 = 1.0;
/// The near side's gain of a mono channel panned hard (the WING's pan law: +3 dB).
const EDGE: f32 = std::f32::consts::SQRT_2;
/// Each of two equal automix talkers' gain (half the power, -3 dB).
const SHARE: f32 = std::f32::consts::FRAC_1_SQRT_2;

/// Drives an engine with constant (DC) inputs and captures its outputs.
struct Rig {
    engine: Arc<Engine>,
    workers: Vec<std::thread::JoinHandle<()>>,
    inputs: Vec<Vec<f32>>,
    outputs: Vec<Vec<f32>>,
}

impl Rig {
    fn new(num_workers: usize, num_ins: usize, num_outs: usize) -> Self {
        let engine = Arc::new(Engine::new(num_workers));
        unity_console(&engine);
        let workers = (0..num_workers)
            .map(|i| {
                let e = engine.clone();
                std::thread::spawn(move || e.pool().run_worker(i))
            })
            .collect();
        while engine.pool().num_running_workers() < num_workers {
            std::thread::yield_now();
        }
        let in_map: Vec<i32> = (0..num_ins as i32).collect();
        let out_map: Vec<i32> = (0..num_outs as i32).collect();
        unsafe { engine.prepare(48000.0, BLOCK, &in_map, &out_map) };
        Self { engine, workers, inputs: vec![vec![0.0; BLOCK]; num_ins], outputs: vec![vec![0.0; BLOCK]; num_outs] }
    }

    fn set_input(&mut self, ch: usize, v: f32) {
        self.inputs[ch].fill(v);
    }

    fn run(&mut self, blocks: usize) {
        let ins: Vec<*const f32> = self.inputs.iter().map(|v| v.as_ptr()).collect();
        let outs: Vec<*mut f32> = self.outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
        for _ in 0..blocks {
            unsafe { self.engine.process(&ins, &outs, BLOCK) };
        }
    }

    fn out(&self, ch: usize) -> f32 {
        *self.outputs[ch].last().unwrap()
    }

    fn ch(&self, i: usize) -> Arc<Strip> {
        self.engine.channel_at(i).unwrap()
    }

    fn strip(&self, kind: StripKind, i: usize) -> Arc<Strip> {
        self.engine.strip_at(kind, i).unwrap()
    }
}

impl Drop for Rig {
    fn drop(&mut self) {
        self.engine.pool().shutdown();
        for w in self.workers.drain(..) {
            w.join().unwrap();
        }
    }
}

fn close(a: f32, b: f32, tol: f32) -> bool {
    (a - b).abs() <= tol
}

#[test]
fn console_has_the_wing_layout() {
    let e = Engine::new(0);
    assert_eq!(e.num_strips(StripKind::Channel), 0);
    assert_eq!(e.num_strips(StripKind::Aux), WING_AUX);
    assert_eq!(e.num_strips(StripKind::Bus), WING_BUSES);
    assert_eq!(e.num_strips(StripKind::Main), WING_MAINS);
    assert_eq!(e.num_strips(StripKind::Matrix), WING_MATRICES);
    assert_eq!(e.num_fx(), WING_FX);
    set_channels(&e, 40);
    // Unnamed, as on the WING.
    assert_eq!(e.channel_at(39).unwrap().name(), "");
    assert_eq!(e.strip_at(StripKind::Bus, 15).unwrap().name(), "");
    // Wing buses can't be removed; extra ones can.
    let bus = e.strip_at(StripKind::Bus, 0).unwrap();
    assert!(!e.remove_strip(Arc::as_ptr(&bus)));
    let extra = e.add_bus(-1).unwrap();
    assert_eq!(extra.id(), 16);
    assert!(e.remove_strip(Arc::as_ptr(&extra)));
}

#[test]
fn fx_slots_grow_beyond_the_wings_but_never_drop_one_in_use() {
    let e = Engine::new(0);
    assert_eq!(e.num_fx(), WING_FX);
    assert_eq!(e.set_num_fx(32), 32);
    assert_eq!(e.set_num_fx(1000), MAX_FX);
    assert_eq!(e.set_num_fx(0), WING_FX);

    // A device in FX 40 keeps 40 slots in use.
    e.set_num_fx(48);
    assert!(e.set_fx_model(39, FX_EXT, true));
    assert!(e.fx_in_use(39));
    assert_eq!(e.set_num_fx(16), 40);
    assert!(e.set_fx_model(39, FX_NONE, true));
    assert!(!e.fx_in_use(39));

    // So does an insert assigned to FX 24, even switched off.
    set_channels(&e, 1);
    let ch = e.channel_at(0).unwrap();
    ch.params.pre_insert().fx.store(23, Relaxed);
    assert!(e.fx_in_use(23));
    assert_eq!(e.set_num_fx(16), 24);
    ch.params.pre_insert().fx.store(-1, Relaxed);
    assert_eq!(e.set_num_fx(16), 16);

    // An insert pointed past the slots in use some other way (a channel preset,
    // a copy, a remote): the next collect takes them in use, never fewer.
    ch.params.post_insert().fx.store(50, Relaxed);
    assert_eq!(e.num_fx(), 16);
    e.collect();
    assert_eq!(e.num_fx(), 51);
    ch.params.post_insert().fx.store(-1, Relaxed);
    e.collect();
    assert_eq!(e.num_fx(), 51, "collect never takes slots away");
}

#[test]
fn channel_reaches_main_1_through_pan_law() {
    let mut rig = Rig::new(2, 4, 8);
    set_channels(&rig.engine, 2);
    rig.set_input(0, 0.5);
    rig.run(20);
    assert!(close(rig.out(0), 0.5 * CENTRE, 1e-5));
    assert!(close(rig.out(1), 0.5 * CENTRE, 1e-5));

    rig.ch(0).params.pan.store(-100.0);
    rig.run(20);
    assert!(close(rig.out(0), 0.5 * EDGE, 1e-5) && close(rig.out(1), 0.0, 1e-5));
}

#[test]
fn mute_fader_dcas_and_mute_groups() {
    let mut rig = Rig::new(2, 2, 2);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    rig.set_input(0, 0.5);
    ch.params.fader_db.store(-6.0206);
    rig.run(20);
    assert!(close(rig.out(0), 0.25 * CENTRE, 1e-4));

    // DCA 3 adds its fader to the channel's.
    ch.params.dca_mask.store(GroupSet::from_mask(1 << 2));
    rig.engine.controls.dcas[2].fader_db.store(-6.0206);
    rig.run(20);
    assert!(close(rig.out(0), 0.125 * CENTRE, 1e-4));
    rig.engine.controls.dcas[2].mute.store(true, Relaxed);
    rig.run(20);
    assert!(close(rig.out(0), 0.0, 1e-6));
    rig.engine.controls.dcas[2].mute.store(false, Relaxed);

    // Mute group 2
    ch.params.mute_group_mask.store(GroupSet::from_mask(1 << 1));
    rig.engine.controls.mute_groups.store(GroupSet::from_mask(1 << 1));
    rig.run(20);
    assert!(close(rig.out(0), 0.0, 1e-6));
    rig.engine.controls.mute_groups.store(GroupSet::from_mask(0));

    ch.params.mute.store(true, Relaxed);
    rig.run(20);
    assert!(close(rig.out(0), 0.0, 1e-6));
    ch.params.mute.store(false, Relaxed);
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    rig.run(20);
    assert!(close(rig.out(0), 0.0, 1e-6));
}

#[test]
fn a_console_of_any_counts_and_groups_beyond_the_wings() {
    // A console that isn't a WING: more channels, buses and groups, fewer mains.
    let rig = Rig::new(2, 2, 2);
    let e = &rig.engine;
    // A new engine is a WING without channels (the application adds them).
    assert_eq!(e.counts(), Counts { channels: 0, ..Counts::WING });
    let counts = Counts { channels: 64, aux: 4, buses: 24, mains: 2, matrices: 3, dcas: 24, mute_groups: 200, fx: 32 };
    e.configure(&ConsoleConfig::of_counts(counts));
    assert_eq!(e.counts(), counts);
    assert_eq!(e.config().counts, counts);
    assert_eq!(e.num_strips(StripKind::Bus), 24);
    // A reset keeps the configuration (but drops the channels, as before).
    e.reset();
    assert_eq!(Counts { channels: 64, ..e.counts() }, counts);
    e.configure(&ConsoleConfig::wing());
    assert_eq!(e.counts(), Counts::WING);

    // Never fewer DCAs or mute groups than the highest one in use.
    let ch = e.channel_at(0).unwrap();
    ch.params.dca_mask.set(20, true);
    assert_eq!(e.set_num_dcas(4), 21);
    ch.params.dca_mask.clear();
    assert_eq!(e.set_num_dcas(4), 4);
    e.controls.mute_groups.set(9, true);
    e.sources().get(3).unwrap().mute_groups.set(30, true);
    assert_eq!(e.set_num_mute_groups(2), 31);
    assert_eq!(e.set_num_mute_groups(9999), MAX_MUTE_GROUPS);
}

#[test]
fn any_number_of_mains_and_matrices_carry_audio() {
    // Six mains and twelve matrices: CH 1 into MAIN 6, MAIN 6 into MTX 12.
    let mut rig = Rig::new(2, 2, 4);
    let counts = Counts { channels: 1, mains: 6, matrices: 12, ..Counts::WING };
    rig.engine.configure(&ConsoleConfig::of_counts(counts));
    unity_console(&rig.engine);
    assert_eq!((rig.engine.num_strips(StripKind::Main), rig.engine.num_strips(StripKind::Matrix)), (6, 12));
    let ch = rig.ch(0);
    ch.params.fader_db.store(0.0);
    assert!(ch.main_send(0).unwrap().on.load(Relaxed), "Main 1 by default");
    assert!(!ch.main_send(5).unwrap().on.load(Relaxed));
    assert!(ch.main_send(6).is_some() && ch.main_send(MAIN_SEND_PAGE).is_none(), "a page of sends, no more");
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    ch.main_send(5).unwrap().on.store(true, Relaxed);
    let mains = rig.engine.strips(StripKind::Main);
    for (m, outs) in [(&mains[0], -1), (&mains[5], 0)] {
        m.params.output_left.store(outs, Relaxed);
        m.params.output_right.store(if outs < 0 { -1 } else { outs + 1 }, Relaxed);
    }
    let mtx = rig.engine.strips(StripKind::Matrix)[11].clone();
    mtx.params.output_left.store(2, Relaxed);
    mtx.params.output_right.store(3, Relaxed);
    let send = mains[5].matrix_send(11).unwrap();
    send.on.store(true, Relaxed);
    send.level_db.store(-6.0206);
    assert!(mains[5].main_send(0).is_none() && mtx.matrix_send(0).is_none(), "mains feed matrices only");

    rig.set_input(0, 0.5);
    rig.run(20);
    assert!(close(rig.out(0), 0.5 * CENTRE, 1e-4), "MAIN 6: {}", rig.out(0));
    assert!(close(rig.out(2), 0.25 * CENTRE, 1e-4), "MTX 12: {}", rig.out(2));
    // Back to the WING: four mains, eight matrices.
    rig.engine.configure(&ConsoleConfig::wing());
    assert_eq!((rig.engine.num_strips(StripKind::Main), rig.engine.num_strips(StripKind::Matrix)), (4, 8));
}

#[test]
fn dcas_and_mute_groups_beyond_64_act_on_the_audio() {
    let mut rig = Rig::new(2, 2, 2);
    set_channels(&rig.engine, 1);
    rig.engine.set_num_dcas(MAX_DCAS);
    rig.engine.set_num_mute_groups(MAX_MUTE_GROUPS);
    let ch = rig.ch(0);
    rig.set_input(0, 0.5);
    ch.params.fader_db.store(0.0);
    rig.run(20);
    assert!(close(rig.out(0), 0.5 * CENTRE, 1e-4));

    // DCA 201 adds its fader; with DCA MUTEGROUPS its mute mutes.
    ch.params.dca_mask.set(200, true);
    rig.engine.controls.dcas[200].fader_db.store(-6.0206);
    rig.run(20);
    assert!(close(rig.out(0), 0.25 * CENTRE, 1e-4));
    rig.engine.controls.dcas[200].mute.store(true, Relaxed);
    rig.run(20);
    assert!(close(rig.out(0), 0.0, 1e-6));
    assert_eq!(ch.console.mute_state(), livedaw_engine::solo::mute_state::GROUP);
    rig.engine.controls.dcas[200].mute.store(false, Relaxed);
    rig.engine.controls.dcas[200].fader_db.store(0.0);

    // Mute group 100.
    ch.params.mute_group_mask.set(99, true);
    rig.engine.controls.mute_groups.set(99, true);
    rig.run(20);
    assert!(close(rig.out(0), 0.0, 1e-6));
    rig.engine.controls.mute_groups.set(99, false);
    rig.run(20);
    assert!(close(rig.out(0), 0.5 * CENTRE, 1e-4));
    assert_eq!(ch.console.mute_state(), livedaw_engine::solo::mute_state::OFF);
}

#[test]
fn stereo_channel_width_and_inputs() {
    let mut rig = Rig::new(1, 2, 2);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    rig.set_input(0, 0.25);
    rig.set_input(1, -0.5);
    rig.run(20);
    assert_eq!(ch.num_channels(), 1);

    ch.params.stereo.store(true, Relaxed); // right input defaults to the next channel
    rig.run(20);
    assert_eq!(ch.num_channels(), 2);
    assert!(close(rig.out(0), 0.25, 1e-5) && close(rig.out(1), -0.5, 1e-5));

    ch.params.width.store(0.0); // mono
    rig.run(20);
    assert!(close(rig.out(0), -0.125, 1e-5) && close(rig.out(1), -0.125, 1e-5));
}

#[test]
fn bus_sends_pre_post_and_subgroup() {
    let mut rig = Rig::new(2, 2, 6);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    rig.set_input(0, 0.5);
    let bus = rig.strip(StripKind::Bus, 2);
    bus.params.output_left.store(2, Relaxed);
    bus.params.output_right.store(3, Relaxed);
    let send = ch.sends.get_or_create(2).unwrap();
    send.enabled.store(true, Relaxed);

    // PRE (tap PFL): the channel fader doesn't matter.
    ch.params.fader_db.store(-100.0);
    rig.run(20);
    assert!(close(rig.out(2), 0.5 * CENTRE, 1e-5), "{}", rig.out(2));

    // POST follows the fader.
    send.mode.store(1, Relaxed);
    rig.run(20);
    assert!(rig.out(2).abs() < 1e-4);
    ch.params.fader_db.store(0.0);
    rig.run(20);
    assert!(close(rig.out(2), 0.5 * CENTRE, 1e-5));

    // Send pan not linked to the channel pan.
    send.pan_link.store(false, Relaxed);
    send.pan.store(100.0);
    rig.run(20);
    assert!(close(rig.out(2), 0.0, 1e-5) && close(rig.out(3), 0.5 * EDGE, 1e-5));

    // GRP: post-fader, post-pan; the bus feeds Main 1.
    send.mode.store(2, Relaxed);
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    bus.main_send(0).unwrap().on.store(true, Relaxed);
    ch.params.pan.store(-100.0);
    rig.run(20);
    assert!(close(rig.out(0), 0.5 * EDGE, 1e-5) && close(rig.out(1), 0.0, 1e-5));
}

#[test]
fn upper_buses_feed_buses_1_to_8() {
    let mut rig = Rig::new(2, 1, 4);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    rig.set_input(0, 0.5);
    let fx_bus = rig.strip(StripKind::Bus, 12);
    let bus1 = rig.strip(StripKind::Bus, 0);
    ch.sends.get_or_create(12).unwrap().enabled.store(true, Relaxed);
    fx_bus.sends.get_or_create(0).unwrap().enabled.store(true, Relaxed);
    fx_bus.sends.get_or_create(0).unwrap().mode.store(1, Relaxed);
    bus1.params.output_left.store(2, Relaxed);
    rig.run(20);
    // Mono channel -> stereo bus 13 at unity per side -> bus 1 (post, centred).
    assert!(close(rig.out(2), 0.5 * CENTRE, 1e-4), "{}", rig.out(2));
}

#[test]
fn matrix_takes_mains_and_a_monitor_path_as_direct_input() {
    let mut rig = Rig::new(2, 2, 6);
    set_channels(&rig.engine, 2);
    rig.ch(1).params.input_left.store(1, Relaxed);
    rig.set_input(0, 0.5);
    rig.set_input(1, 0.25);
    let main = rig.strip(StripKind::Main, 0);
    let mtx = rig.strip(StripKind::Matrix, 0);
    main.matrix_send(0).unwrap().on.store(true, Relaxed);
    mtx.params.output_left.store(4, Relaxed);
    rig.run(20);
    assert!(close(rig.out(4), 0.75 * CENTRE, 1e-4));

    // DIRECT IN MON.SPK, inverted: the SPEAKERS (Main 1 at 0 dB) cancel Main 1.
    let c = &rig.engine.controls;
    c.monitor_level_db.store(0.0);
    mtx.params.direct_on.store(true, Relaxed);
    mtx.params.direct_level_db.store(0.0);
    mtx.params.direct_source.store(livedaw_engine::routing::matrix_direct::SPEAKERS, Relaxed);
    mtx.params.direct_invert.store(true, Relaxed);
    rig.run(20);
    assert!(close(rig.out(4), 0.0, 1e-4), "{}", rig.out(4));
}

#[test]
fn solo_pfl_goes_to_the_monitor() {
    let mut rig = Rig::new(1, 2, 4);
    set_channels(&rig.engine, 2);
    rig.ch(1).params.input_left.store(1, Relaxed);
    rig.set_input(0, 0.5);
    rig.set_input(1, 0.25);
    rig.engine.controls.monitor_output_left.store(2, Relaxed);
    rig.engine.controls.monitor_output_right.store(3, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), 0.75 * CENTRE, 1e-4)); // follows Main 1

    let ch = rig.ch(1);
    ch.params.fader_db.store(-100.0);
    ch.params.solo.store(true, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), 0.25, 1e-4), "{}", rig.out(2)); // PFL: pre fader, mono both sides
    assert!(rig.engine.controls.any_solo_active.load(Relaxed));
}

#[test]
fn an_x32_runs_its_own_chains_and_models() {
    use livedaw_engine::Profile;
    let mut rig = Rig::new(1, 1, 2);
    rig.engine.configure(&ConsoleConfig { profile: Profile::X32, counts: Counts::X32 });
    // Its mains: LR stereo, M/C mono; channels feed LR, not M/C; no bus feeds
    // LR at first (a WING's buses 9-16 do).
    let main = |i| rig.strip(StripKind::Main, i);
    assert!(main(0).params.stereo.load(Relaxed) && !main(1).params.stereo.load(Relaxed));
    let ch1 = rig.ch(0);
    assert!(ch1.main_send(0).unwrap().on.load(Relaxed) && !ch1.main_send(1).unwrap().on.load(Relaxed));
    // Its channels show their own colour (YE), not their source's.
    assert!(!ch1.params.cust_link.load(Relaxed) && ch1.params.colour.load(Relaxed) == 4);
    // Its sends start on at -oo, its M/C level at -oo, its DCAs WH.
    let send = ch1.sends.get_or_create(0).unwrap();
    assert!(send.enabled.load(Relaxed) && send.level_db.load() == livedaw_dsp::fader::FADER_MIN);
    assert_eq!(ch1.main_send(1).unwrap().level_db.load(), livedaw_dsp::fader::FADER_MIN);
    assert_eq!(rig.engine.controls.dcas[0].colour.load(Relaxed), 8);
    // An X32's buses send to matrices only: their bus sends stay off.
    let bus2 = rig.strip(StripKind::Bus, 1);
    assert!(!bus2.sends.get_or_create(0).unwrap().enabled.load(Relaxed));
    for b in rig.engine.strips(StripKind::Bus) {
        assert!(!b.main_send(0).unwrap().on.load(Relaxed), "bus {}", b.id() + 1);
        // Buses 13-16 feed FX 1-4 at 0 dB; the others start down.
        let up = b.id() >= 12;
        assert_eq!(b.params.fader_db.load(), if up { 0.0 } else { livedaw_dsp::fader::FADER_MIN });
    }
    unity_console(&rig.engine);
    for ch in rig.engine.strips(StripKind::Channel) {
        ch.params.fader_db.store(0.0);
    }
    let ch = rig.ch(0);
    ch.params.input_left.store(0, Relaxed);
    assert_eq!(ch.chain()[1], livedaw_engine::chain::Node::Input);
    rig.set_input(0, 0.25);
    rig.run(50);
    let unity = rig.out(0);
    assert!(close(unity, 0.25 * CENTRE, 1e-4), "an X32 channel reaches the main: {unity}");

    // Its 4-band EQ: a low shelf of +6 dB on band 1 lifts DC by 6 dB.
    let eq = ch.params.eq();
    eq.on.store(true, Relaxed);
    eq.param("1t").unwrap().store(1.0); // LShv
    eq.param("1g").unwrap().store(6.0);
    rig.run(200);
    assert!(close(rig.out(0) / unity, 1.995, 0.02), "EQ: {}", rig.out(0) / unity);
    eq.on.store(false, Relaxed);

    // Its compressor: 0.25 (-12 dB) over a -30 dB threshold at 100:1 comes down
    // to about the threshold, 18 dB lower.
    let d = ch.params.dynamics();
    assert_eq!(d.model_spec().unwrap().token, "X32COMP");
    d.param("thr").unwrap().store(-30.0);
    d.param("ratio").unwrap().store(11.0);
    d.on.store(true, Relaxed);
    rig.run(400);
    let gr = 20.0 * (rig.out(0) / unity).log10();
    assert!(close(gr, -17.8, 0.5), "compressed by {gr} dB");

    // A send takes the signal at one of the channel's own tap points: before
    // the EQ (tap 2, after the gate) or after it (tap 3), the X32's <-EQ and
    // EQ-> (GEDI). Bus 1 to LR, the channel to bus 1 only.
    d.on.store(false, Relaxed);
    eq.on.store(true, Relaxed);
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    let bus = rig.strip(StripKind::Bus, 0);
    bus.params.fader_db.store(0.0);
    bus.main_send(0).unwrap().on.store(true, Relaxed);
    let send = ch.sends.get_or_create(0).unwrap();
    send.level_db.store(0.0);
    send.enabled.store(true, Relaxed);
    let at = |rig: &mut Rig, tap: i32| {
        send.tap.store(tap, Relaxed);
        rig.engine.update_taps();
        rig.run(200);
        rig.out(0)
    };
    let (pre_eq, post_eq) = (at(&mut rig, 2), at(&mut rig, 3));
    assert!(pre_eq > 0.0 && close(post_eq / pre_eq, 1.995, 0.03), "EQ-> / <-EQ: {}", post_eq / pre_eq);
    eq.on.store(false, Relaxed);
    ch.main_send(0).unwrap().on.store(true, Relaxed);
    send.enabled.store(false, Relaxed);

    // Every one of its process orders runs.
    for i in 0..4 {
        ch.params.process_order.store(i, Relaxed);
        rig.run(2);
        assert!(rig.out(0).is_finite());
    }
}

#[test]
fn gate_model_and_process_order() {
    let mut rig = Rig::new(1, 1, 2);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    rig.set_input(0, 0.001); // -60 dB, below the -40 dB default threshold
    ch.params.gate().on.store(true, Relaxed);
    rig.run(200);
    // Closed down towards its 40 dB default range (1e-5 here).
    assert!(rig.out(0).abs() < 2e-5, "gate didn't close: {}", rig.out(0));
    assert!(ch.meters.gate_active.load(Relaxed));

    // The gate slot can hold a compressor model too.
    ch.params.gate().set_model(model_index(DYN_MODELS, "COMP").unwrap());
    rig.run(50);
    assert!(close(rig.out(0), 0.001 * CENTRE, 1e-5));

    // Any of the 24 orders runs.
    for i in 0..PROCESS_ORDERS.len() {
        ch.params.process_order.store(i as i32, Relaxed);
        rig.run(2);
        assert!(rig.out(0).is_finite());
    }
}

#[test]
fn input_delay_shifts_the_signal() {
    let mut rig = Rig::new(0, 1, 2);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    ch.params.input.delay_on.store(true, Relaxed);
    ch.params.input.delay_mode.store(3, Relaxed); // samples
    ch.params.input.delay.store(64.0);
    rig.inputs[0][0] = 1.0;
    rig.run(1);
    // (Gains ramp in from silence during the first block, so the impulse is small.)
    let first = rig.outputs[0].iter().position(|x| x.abs() > 0.01).unwrap();
    assert_eq!(first, 64);
}

/// The delay in meters, as the WING: 0.3 m per ms (3 m = 10 ms = 480
/// samples at 48 kHz) and never shorter than its 0.1 m = 16 samples.
#[test]
fn input_delay_in_meters_converts_as_the_wing() {
    for (meters, samples) in [(0.1, 16), (3.0, 480)] {
        let mut rig = Rig::new(0, 1, 2);
        set_channels(&rig.engine, 1);
        let ch = rig.ch(0);
        ch.params.input.delay_on.store(true, Relaxed);
        ch.params.input.delay_mode.store(0, Relaxed); // meters
        ch.params.input.delay.store(meters);
        rig.run(10); // ramps settle in silence
        rig.inputs[0].fill(0.0);
        rig.inputs[0][0] = 1.0;
        let mut first = None;
        for block in 0..4 {
            rig.run(1);
            rig.inputs[0][0] = 0.0;
            if let Some(i) = rig.outputs[0].iter().position(|x| x.abs() > 0.1) {
                first = Some(block * rig.outputs[0].len() + i);
                break;
            }
        }
        assert_eq!(first, Some(samples), "{meters} m");
    }
}

#[test]
fn unpatched_channels_idle_and_resume_cleanly() {
    let mut rig = Rig::new(Engine::default_num_workers(), 8, 2);
    set_channels(&rig.engine, 4096);
    for i in 0..8 {
        rig.set_input(i, 0.01);
    }
    rig.run(10);
    assert!(rig.ch(100).is_idle());
    assert!(!rig.ch(3).is_idle());

    let start = std::time::Instant::now();
    rig.run(200);
    let ms = start.elapsed().as_secs_f64() * 1000.0 / 200.0;
    println!("4096 channels, 8 patched: {ms:.3} ms per {:.2} ms block", 1000.0 * BLOCK as f64 / 48000.0);

    for i in 0..8 {
        rig.ch(i).params.mute.store(true, Relaxed);
    }
    rig.ch(100).params.input_left.store(0, Relaxed);
    rig.run(20);
    assert!(close(rig.out(0), 0.01 * CENTRE, 1e-5));
}

#[test]
fn automix_shares_gain() {
    let mut rig = Rig::new(1, 2, 2);
    set_channels(&rig.engine, 2);
    rig.ch(1).params.input_left.store(1, Relaxed);
    rig.set_input(0, 0.5);
    rig.set_input(1, 0.5);
    for i in 0..2 {
        let ch = rig.ch(i);
        let ins = ch.params.post_insert();
        ins.on.store(true, Relaxed);
        ins.mode.store(1, Relaxed); // AUTO_X
    }
    rig.run(50);
    // Two equal talkers: each gets half the power (-3 dB), the sum stays at one talker's level.
    assert!(close(rig.out(0), 0.5 * CENTRE * 2.0 * SHARE, 1e-3), "{}", rig.out(0));
    assert_eq!(rig.ch(0).automix_group(), Some(0));

    // AUTO_X with INS 2 off is not a member: it isn't shared and takes no
    // share from the others (channel 1 alone in the group keeps unity).
    rig.ch(1).params.post_insert().on.store(false, Relaxed);
    assert_eq!(rig.ch(1).automix_group(), None);
    rig.run(200);
    assert!(close(rig.ch(0).meters.automix_db.load(), 0.0, 0.05), "{}", rig.ch(0).meters.automix_db.load());
    assert!(close(rig.ch(1).meters.automix_db.load(), 0.0, 0.05), "{}", rig.ch(1).meters.automix_db.load());
    assert!(close(rig.out(0), 2.0 * 0.5 * CENTRE, 1e-3), "{}", rig.out(0));
    // ON again: it joins.
    rig.ch(1).params.post_insert().on.store(true, Relaxed);
    rig.ch(1).params.post_insert().mode.store(2, Relaxed); // AUTO_Y: its own group
    assert_eq!(rig.ch(1).automix_group(), Some(1));
    rig.run(50);
    assert!(close(rig.out(0), 2.0 * 0.5 * CENTRE, 1e-3), "{}", rig.out(0));
    rig.ch(1).params.post_insert().mode.store(1, Relaxed);
    rig.run(50);
    assert!(close(rig.out(0), 0.5 * CENTRE * 2.0 * SHARE, 1e-3), "{}", rig.out(0));
}

// ---- FX slots through the C ABI --------------------------------------------

struct FakePlugin {
    gain: f32,
    mode: u8, // 0 = gain, 1 = emit NaN, 2 = report failure (as a C++ exception would)
    released: Arc<AtomicBool>,
}

unsafe extern "C" fn fake_process(ctx: *mut c_void, ch: *const *mut f32, nc: i32, n: i32) -> i32 {
    let p = &*(ctx as *const FakePlugin);
    match p.mode {
        1 => {
            *(*ch) = f32::NAN;
            0
        }
        2 => 1,
        _ => {
            for c in 0..nc as usize {
                std::slice::from_raw_parts_mut(*ch.add(c), n as usize).iter_mut().for_each(|x| *x *= p.gain);
            }
            0
        }
    }
}

unsafe extern "C" fn fake_release(ctx: *mut c_void) {
    let p = Box::from_raw(ctx as *mut FakePlugin);
    p.released.store(true, Relaxed);
}

fn fake(gain: f32, mode: u8) -> (InsertCallbacks, Arc<AtomicBool>) {
    let released = Arc::new(AtomicBool::new(false));
    let ctx = Box::into_raw(Box::new(FakePlugin { gain, mode, released: released.clone() }));
    (InsertCallbacks { process: fake_process, release: Some(fake_release), ctx: ctx as *mut c_void }, released)
}

#[test]
fn fx_slots_as_inserts_and_faulty_ones_are_auto_bypassed() {
    let mut rig = Rig::new(1, 2, 2);
    set_channels(&rig.engine, 2);
    let (a, b) = (rig.ch(0), rig.ch(1));
    b.params.input_left.store(1, Relaxed);
    let engine = rig.engine.clone();

    let (half, released) = fake(0.5, 0);
    engine.set_fx_plugin(0, Some(half));
    engine.set_fx_plugin(1, Some(fake(1.0, 1).0)); // NaN
    engine.set_fx_plugin(2, Some(fake(1.0, 2).0)); // failing
    a.params.pre_insert().on.store(true, Relaxed);
    a.params.pre_insert().fx.store(0, Relaxed);
    rig.set_input(0, 0.5);
    rig.run(20);
    assert!(close(rig.out(0), 0.25 * CENTRE, 1e-5));

    // Post insert with a NaN plugin: auto-bypassed, signal kept.
    a.params.post_insert().on.store(true, Relaxed);
    a.params.post_insert().fx.store(1, Relaxed);
    rig.run(20);
    assert!(engine.fx(1).unwrap().is_faulted());
    assert!(close(rig.out(0), 0.25 * CENTRE, 1e-5));

    // Failing plugin on another strip.
    b.params.pre_insert().on.store(true, Relaxed);
    b.params.pre_insert().fx.store(2, Relaxed);
    rig.run(5);
    assert!(engine.fx(2).unwrap().is_faulted());
    assert_eq!(engine.num_faulted_fx(), 2);

    // The host rearranging the slot's plugins (a chain edited) keeps it
    // auto-bypassed; a new processor starts unfaulted.
    engine.replace_fx_plugin(1, fake(1.0, 0).0);
    rig.run(5);
    assert!(engine.fx(1).unwrap().is_faulted());
    engine.set_fx_plugin(1, Some(fake(1.0, 0).0));
    assert!(!engine.fx(1).unwrap().is_faulted());
    rig.run(5);
    assert!(!engine.fx(1).unwrap().is_faulted());

    // Two strips on one slot: never concurrent, reported.
    b.params.pre_insert().fx.store(0, Relaxed);
    rig.set_input(1, 0.5);
    rig.run(5);
    assert!(engine.fx(0).unwrap().is_double_assigned());
    assert_eq!(engine.fx_users(0).len(), 2);

    // Replacing a processor releases the old one once the audio thread is done with it.
    engine.set_fx_plugin(0, None);
    rig.run(2);
    engine.set_device_running(true);
    engine.collect();
    assert!(released.load(Relaxed));
}

#[test]
fn nan_input_never_reaches_buses_or_outputs() {
    let mut rig = Rig::new(1, 2, 2);
    set_channels(&rig.engine, 2);
    rig.ch(1).params.input_left.store(1, Relaxed);
    rig.set_input(0, f32::NAN);
    rig.set_input(1, 0.25);
    rig.run(4);
    assert!(rig.ch(0).meters.dsp_faults.load(Relaxed) > 0);
    assert!(close(rig.out(0), 0.25 * CENTRE, 1e-5));

    rig.set_input(0, 0.25);
    rig.run(40);
    assert!(rig.out(0).is_finite() && rig.out(0) > 0.4 * CENTRE);
}

#[test]
fn output_protection_clips_at_the_ceiling() {
    let mut rig = Rig::new(0, 1, 2);
    set_channels(&rig.engine, 1);
    rig.ch(0).params.fader_db.store(10.0);
    rig.set_input(0, 0.9);
    rig.run(20);
    assert!(close(rig.out(0), 1.0, 1e-6));
    assert!(rig.engine.controls.output_clip_events.load(Relaxed) > 0);
    rig.engine.controls.output_ceiling_db.store(-6.0206);
    rig.run(20);
    assert!(close(rig.out(0), 0.5, 1e-4));
    rig.engine.controls.output_protection.store(false, Relaxed);
    rig.run(20);
    assert!(rig.out(0) > 2.0);
}

#[test]
fn stress_structural_changes_while_audio_runs() {
    let mut rig = Rig::new(2, 8, 8);
    for i in 0..8 {
        rig.set_input(i, 0.01 * (i + 1) as f32);
    }
    set_channels(&rig.engine, 8);
    rig.engine.set_device_running(true); // garbage must now wait for callbacks

    let engine = rig.engine.clone();
    let running = Arc::new(AtomicBool::new(true));
    let blocks = Arc::new(AtomicUsize::new(0));
    let bad = Arc::new(AtomicBool::new(false));
    let inputs = rig.inputs.clone();

    let audio = {
        let (running, blocks, bad) = (running.clone(), blocks.clone(), bad.clone());
        std::thread::spawn(move || {
            let mut outputs = vec![vec![0.0f32; BLOCK]; 8];
            let ins: Vec<*const f32> = inputs.iter().map(|v| v.as_ptr()).collect();
            while running.load(Relaxed) {
                let outs: Vec<*mut f32> = outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
                unsafe { engine.process(&ins, &outs, BLOCK) };
                if outputs.iter().any(|o| !o[0].is_finite()) {
                    bad.store(true, Relaxed);
                }
                blocks.fetch_add(1, Relaxed);
            }
        })
    };

    let mut seed = 42u32;
    let mut rand = move |n: u32| {
        seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
        (seed >> 8) % n.max(1)
    };
    // 1.5 s of changes, longer if the audio hasn't run 100 blocks by then (a
    // loaded machine, as when the checks run beside a build), up to 15 s.
    let start = std::time::Instant::now();
    let mut ops = 0;
    while start.elapsed() < std::time::Duration::from_millis(1500)
        || (blocks.load(Relaxed) <= 100 && start.elapsed() < std::time::Duration::from_secs(15))
    {
        let e = &rig.engine;
        match rand(6) {
            0 => set_channels(e, rand(64) as usize),
            1 => {
                e.add_bus(-1);
            }
            2 => {
                let buses = e.buses();
                e.remove_strip(Arc::as_ptr(&buses[rand(buses.len() as u32) as usize]));
            }
            3 => {
                let slot = rand(16) as usize;
                if rand(2) == 0 {
                    e.set_fx_plugin(slot, Some(fake(0.5, 0).0));
                } else {
                    e.set_fx_plugin(slot, None);
                }
            }
            4 => {
                if let Some(s) = e.channel_at(rand(e.num_channels().max(1) as u32) as usize) {
                    let p = &s.params;
                    p.pre_insert().on.store(rand(2) == 0, Relaxed);
                    p.pre_insert().fx.store(rand(17) as i32 - 1, Relaxed);
                    p.stereo.store(rand(2) == 0, Relaxed);
                    p.gate().on.store(rand(2) == 0, Relaxed);
                    p.dynamics().set_model(rand(DYN_MODELS.len() as u32) as usize);
                    p.dynamics().on.store(true, Relaxed);
                    // Keys from other channels (cycles and itself included) at any tap.
                    for slot in [p.gate(), p.dynamics()] {
                        slot.sc_source.store(rand(66) as i32 - 1, Relaxed);
                        slot.sc_tap.store(rand(8) as i32, Relaxed);
                    }
                    p.eq().set_model(rand(7) as usize);
                    p.process_order.store(rand(24) as i32, Relaxed);
                    if let Some(send) = s.sends.get_or_create(rand(24) as usize) {
                        send.enabled.store(rand(2) == 0, Relaxed);
                        send.mode.store(rand(3) as i32, Relaxed);
                    }
                    s.matrix_send(rand(8) as usize).unwrap().on.store(rand(2) == 0, Relaxed);
                }
            }
            _ => e.reset(),
        }
        e.collect();
        ops += 1;
    }

    running.store(false, Relaxed);
    audio.join().unwrap();
    rig.engine.set_device_running(false);
    rig.engine.collect();

    println!("{ops} structural operations during {} audio blocks", blocks.load(Relaxed));
    assert!(!bad.load(Relaxed));
    assert!(blocks.load(Relaxed) > 100);
    assert_eq!(rig.engine.garbage().num_pending(), 0);
}

#[test]
fn builtin_fx_devices_run_in_slots() {
    use livedaw_dsp::wing::fx::model_index as fx;
    use livedaw_engine::params::FX_NONE;
    let mut rig = Rig::new(1, 1, 2);
    set_channels(&rig.engine, 1);
    let a = rig.ch(0);
    let engine = rig.engine.clone();

    // A flat graphic EQ with +6 dB trim on a pre insert doubles the signal.
    let geq = fx("GEQ").unwrap();
    assert!(engine.set_fx_model(9, geq, true));
    assert!(engine.fx(9).unwrap().has_device());
    a.params.pre_insert().on.store(true, Relaxed);
    a.params.pre_insert().fx.store(9, Relaxed);
    rig.set_input(0, 0.25);
    rig.run(40);
    let flat = rig.out(0);
    assert!(close(flat, 0.25 * CENTRE, 1e-3), "{flat}");
    engine.controls.fx[9].param("TRIM").unwrap().store(6.0206);
    rig.run(40);
    assert!(close(rig.out(0), 2.0 * flat, 2e-3), "{}", rig.out(0));

    // FX MIX 0 is dry; bypass is dry.
    engine.controls.fx[9].mix.store(0.0);
    rig.run(10);
    assert!(close(rig.out(0), flat, 1e-3));
    engine.controls.fx[9].mix.store(100.0);
    engine.controls.fx[9].bypass.store(true, Relaxed);
    rig.run(10);
    assert!(close(rig.out(0), flat, 1e-3));
    engine.controls.fx[9].bypass.store(false, Relaxed);

    // A hall reverb rings on after the input stops (premium: slots 1-8 only).
    let hall = fx("HALL").unwrap();
    assert!(!engine.set_fx_model(9, hall, true));
    assert!(engine.set_fx_model(0, hall, true));
    a.params.pre_insert().fx.store(0, Relaxed);
    rig.run(20);
    rig.set_input(0, 0.0);
    rig.run(30);
    let tail: f32 = (0..20)
        .map(|_| {
            rig.run(1);
            rig.out(0).abs()
        })
        .fold(0.0, f32::max);
    assert!(tail > 1e-5, "no reverb tail: {tail}");

    // A model set through the shared parameters gets its device on collect.
    engine.controls.fx[1].set_model(fx("CHORUS").unwrap(), true);
    assert!(!engine.fx(1).unwrap().has_device());
    engine.collect();
    assert!(engine.fx(1).unwrap().has_device());

    // NONE removes the device.
    assert!(engine.set_fx_model(0, FX_NONE, true));
    assert!(!engine.fx(0).unwrap().has_device());
}

// ---- WING processing order, taps and console settings ------------------------

/// DC gain of a strip's EQ as the engine runs it.
fn eq_dc_gain(s: &Strip) -> f32 {
    use livedaw_dsp::wing::{eq::design, EQ_MODELS_BUS, EQ_MODELS_CH, MAX_EQ_PARAMS};
    let table = if s.kind().is_output() { EQ_MODELS_BUS } else { EQ_MODELS_CH };
    let spec = &table[s.params.eq().model_index()];
    let mut v = [0.0; MAX_EQ_PARAMS];
    s.params.eq().values_snapshot(&mut v);
    let d = design(spec, &v[..spec.params.len()], 48000.0, s.params.eq().mix.load() / 100.0);
    10f32.powf(d.response_db(1.0, 48000.0) as f32 / 20.0)
}

#[test]
fn extra_blocks_run_where_they_sit_in_the_chain() {
    use livedaw_engine::blocks::{BlockKind, BlockParams, Step};
    use livedaw_engine::chain::Node;
    let mut rig = Rig::new(1, 1, 2);
    set_channels(&rig.engine, 1);
    let engine = rig.engine.clone();
    let ch = rig.ch(0);
    ch.params.fader_db.store(0.0);
    rig.set_input(0, 0.5);
    rig.run(20);
    assert!(close(rig.out(0), 0.5 * CENTRE, 1e-4));
    assert!(ch.layout().is_none(), "the WING template");
    let at = |node: Node| ch.steps().iter().position(|s| matches!(s, Step::Node(n) if *n == node)).unwrap();

    // An insert block (FX 3, a plugin at half gain) before PFL: the PFL meter
    // and the output both take it.
    engine.set_fx_plugin(2, Some(fake(0.5, 0).0));
    let ins = engine.insert_block(&ch, BlockKind::Insert, at(Node::Pfl)).unwrap();
    let BlockParams::Insert(p) = &ins.params else { panic!() };
    p.fx.store(2, Relaxed);
    p.on.store(true, Relaxed);
    assert!(ch.uses_fx(2) && engine.fx_in_use(2));
    ch.meters.pfl_peak[0].store(0.0);
    rig.run(20);
    assert!(close(rig.out(0), 0.25 * CENTRE, 1e-4), "{}", rig.out(0));
    assert!(close(ch.meters.pfl_peak[0].load(), 0.25, 1e-4));

    // Moved after the fader: the PFL is whole again, the output still halved.
    assert!(engine.move_block(&ch, ins.uid(), at(Node::Fader) + 1));
    assert!(at(Node::Fader) < ch.steps().iter().position(|s| matches!(s, Step::Block(_))).unwrap());
    rig.run(20);
    ch.meters.pfl_peak[0].store(0.0);
    rig.run(4);
    assert!(close(rig.out(0), 0.25 * CENTRE, 1e-4));
    assert!(close(ch.meters.pfl_peak[0].load(), 0.5, 1e-4));

    // Its latency counts for the taps after it (delay compensation).
    engine.fx(2).unwrap().set_latency(64);
    engine.update_pdc();
    assert_eq!(ch.pdc.latency(livedaw_engine::pdc_tap::PFL), 0);
    assert_eq!(ch.pdc.latency(livedaw_engine::pdc_tap::POST), 64);

    // An EQ block (+6 dB low shelf) and the plugin taken out: the EQ alone.
    let eq = engine.insert_block(&ch, BlockKind::Eq, at(Node::Post)).unwrap();
    let BlockParams::Eq(e) = &eq.params else { panic!() };
    assert!(!e.on.load(Relaxed), "a new EQ block is off");
    e.param("lg").unwrap().store(6.0);
    e.on.store(true, Relaxed);
    assert!(engine.remove_block(&ch, ins.uid()));
    assert!(!ch.uses_fx(2));
    rig.run(200);
    assert!(rig.out(0) > 0.5 * CENTRE * 1.9, "{}", rig.out(0));
    assert_eq!(ch.blocks().len(), 1);

    // No blocks left: back to the template.
    assert!(engine.remove_block(&ch, eq.uid()));
    assert!(ch.layout().is_none());
    rig.run(20);
    assert!(close(rig.out(0), 0.5 * CENTRE, 1e-4));
    // At most MAX_EXTRA_BLOCKS.
    for _ in 0..livedaw_engine::blocks::MAX_EXTRA_BLOCKS {
        assert!(engine.insert_block(&ch, BlockKind::Delay, 0).is_some());
    }
    assert!(engine.insert_block(&ch, BlockKind::Delay, 0).is_none());
    engine.clear_blocks(&ch);
    assert!(ch.layout().is_none());
}

#[test]
fn sends_take_user_taps_anywhere_in_the_chain() {
    use livedaw_engine::blocks::Step;
    use livedaw_engine::chain::Node;
    let mut rig = Rig::new(1, 1, 4);
    set_channels(&rig.engine, 1);
    let engine = rig.engine.clone();
    let ch = rig.ch(0);
    let bus = rig.strip(StripKind::Bus, 0);
    bus.params.output_left.store(2, Relaxed);
    bus.params.output_right.store(3, Relaxed);
    ch.params.fader_db.store(livedaw_dsp::fader::FADER_MIN); // fader down: post sends silent
    let send = ch.sends.get_or_create(0).unwrap();
    send.enabled.store(true, Relaxed);
    send.mode.store(livedaw_engine::sends::send_mode::POST, Relaxed);
    send.level_db.store(0.0);
    rig.set_input(0, 0.5);
    rig.run(20);
    assert!(rig.out(2).abs() < 1e-6, "post-fader: nothing");

    // A user tap right after INPUT, before the fader: the send takes it.
    let input = ch.steps().iter().position(|s| matches!(s, Step::Node(Node::Input))).unwrap();
    let id = engine.insert_tap(&ch, input + 1).unwrap();
    assert!(id >= livedaw_engine::taps::FIRST_USER_TAP);
    send.tap.store(id as i32, Relaxed);
    engine.update_taps();
    rig.run(20);
    assert!(close(rig.out(2), 0.5 * CENTRE, 1e-4), "{}", rig.out(2));
    // Like a pre-fader send: the channel's mute cuts it.
    ch.params.mute.store(true, Relaxed);
    rig.run(20);
    assert!(rig.out(2).abs() < 1e-6);
    ch.params.mute.store(false, Relaxed);
    // Moved after the fader (down): silence; back before it: the signal again.
    let fader = ch.steps().iter().position(|s| matches!(s, Step::Node(Node::Fader))).unwrap();
    assert!(engine.move_tap(&ch, id, fader + 1));
    rig.run(20);
    assert!(rig.out(2).abs() < 1e-6, "{}", rig.out(2));
    assert!(engine.move_tap(&ch, id, input + 1));
    rig.run(20);
    assert!(close(rig.out(2), 0.5 * CENTRE, 1e-4), "{}", rig.out(2));
    assert!(!engine.move_tap(&ch, livedaw_engine::strip::tap::PFL as u8, 0), "the WING's taps stay");
    // A second tap gets another id; the tap taken away: silence.
    assert_ne!(engine.insert_tap(&ch, 0), Some(id));
    assert!(engine.remove_tap(&ch, id));
    rig.run(20);
    assert!(rig.out(2).abs() < 1e-6);
    assert!(!engine.remove_tap(&ch, livedaw_engine::strip::tap::PFL as u8), "the WING's taps stay");
}

#[test]
fn bus_runs_dyn_ins1_fader_eq_ins2_with_the_tap_after_ins1() {
    let mut rig = Rig::new(1, 1, 6);
    set_channels(&rig.engine, 1);
    let engine = rig.engine.clone();
    let ch = rig.ch(0);
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    rig.set_input(0, 0.5);
    engine.set_fx_plugin(0, Some(fake(0.5, 0).0));
    engine.set_fx_plugin(1, Some(fake(0.25, 0).0));

    let fx_bus = rig.strip(StripKind::Bus, 12);
    let bus1 = rig.strip(StripKind::Bus, 0);
    ch.sends.get_or_create(12).unwrap().enabled.store(true, Relaxed);
    let p = &fx_bus.params;
    p.pre_insert().on.store(true, Relaxed);
    p.pre_insert().fx.store(0, Relaxed);
    p.post_insert().on.store(true, Relaxed);
    p.post_insert().fx.store(1, Relaxed);
    p.eq().param("lg").unwrap().store(6.0);
    p.tap.store(0, Relaxed); // ignored: a bus taps after INS 1
    p.fader_db.store(-100.0);
    p.output_left.store(4, Relaxed);
    fx_bus.sends.get_or_create(0).unwrap().enabled.store(true, Relaxed); // PRE
    bus1.params.output_left.store(2, Relaxed);
    rig.run(40);

    // The tap carries INS 1 but neither the fader, the EQ nor INS 2.
    assert!(close(rig.out(2), 0.5 * CENTRE * 0.5, 1e-4), "{}", rig.out(2));
    assert!(rig.out(4).abs() < 1e-6);
    assert!(close(fx_bus.meters.pfl_peak[0].load(), 0.25 * CENTRE, 1e-4));

    // The output runs the EQ and INS 2 after the fader.
    p.fader_db.store(0.0);
    rig.run(40);
    let eq = eq_dc_gain(&fx_bus);
    assert!(eq > 1.9, "{eq}");
    assert!(close(rig.out(4), 0.25 * CENTRE * eq * 0.25, 1e-3), "{} vs {}", rig.out(4), 0.25 * CENTRE * eq * 0.25);
}

#[test]
fn aux_has_no_post_insert_and_taps_after_ins1() {
    let mut rig = Rig::new(1, 2, 4);
    let engine = rig.engine.clone();
    let aux = rig.strip(StripKind::Aux, 0);
    aux.params.input_left.store(0, Relaxed);
    rig.set_input(0, 0.5);
    engine.set_fx_plugin(0, Some(fake(0.5, 0).0));
    engine.set_fx_plugin(1, Some(fake(0.25, 0).0));
    let p = &aux.params;
    p.pre_insert().on.store(true, Relaxed);
    p.pre_insert().fx.store(0, Relaxed);
    p.post_insert().on.store(true, Relaxed); // the aux has no INS 2: ignored
    p.post_insert().fx.store(1, Relaxed);
    p.tap.store(0, Relaxed); // ignored: aux taps after INS 1, pre-fader
    p.fader_db.store(-6.0206);
    aux.sends.get_or_create(2).unwrap().enabled.store(true, Relaxed);
    rig.strip(StripKind::Bus, 2).params.output_left.store(2, Relaxed);
    rig.run(20);
    assert!(close(rig.out(0), 0.5 * 0.5 * 0.5 * CENTRE, 1e-4), "{}", rig.out(0));
    assert!(close(rig.out(2), 0.5 * 0.5 * CENTRE, 1e-4), "{}", rig.out(2));
    assert!(!aux.uses_fx(1) && aux.uses_fx(0));
    assert_eq!(engine.fx_users(1).len(), 0);

    // Aux can't join automix either.
    p.post_insert().mode.store(1, Relaxed);
    rig.run(20);
    assert!(close(rig.out(0), 0.5 * 0.5 * 0.5 * CENTRE, 1e-4));
}

#[test]
fn channel_delay_is_post_fader() {
    let mut rig = Rig::new(0, 1, 4);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    ch.params.input.delay_on.store(true, Relaxed);
    ch.params.input.delay_mode.store(3, Relaxed); // samples
    ch.params.input.delay.store(64.0);
    ch.sends.get_or_create(1).unwrap().enabled.store(true, Relaxed); // TAP at PRE FDR
    rig.strip(StripKind::Bus, 1).params.output_left.store(2, Relaxed);
    rig.run(10); // ramps settle in silence
    rig.inputs[0][10] = 1.0;
    rig.run(1);
    let first = |ch: usize| rig.outputs[ch].iter().position(|x| x.abs() > 0.1).unwrap();
    assert_eq!(first(0), 74, "the main output is delayed");
    assert_eq!(first(2), 10, "the pre-fader tap is not");
}

#[test]
fn tap_sends_follow_the_channel_mute_unless_ignored() {
    let mut rig = Rig::new(1, 1, 8);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    rig.set_input(0, 0.5);
    let send = ch.sends.get_or_create(2).unwrap();
    send.enabled.store(true, Relaxed);
    rig.strip(StripKind::Bus, 2).params.output_left.store(2, Relaxed);
    ch.matrix_send(0).unwrap().on.store(true, Relaxed);
    ch.matrix_send(0).unwrap().pre.store(true, Relaxed);
    rig.strip(StripKind::Matrix, 0).params.output_left.store(4, Relaxed);
    ch.main_send(1).unwrap().on.store(true, Relaxed);
    ch.main_send(1).unwrap().pre.store(true, Relaxed);
    rig.strip(StripKind::Main, 1).params.output_left.store(6, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), 0.5 * CENTRE, 1e-5));
    assert!(close(rig.out(4), 0.5 * CENTRE, 1e-5));
    assert!(close(rig.out(6), 0.5 * CENTRE, 1e-5));

    // Muted (own mute, DCA mute or mute group): TAP sends are cut too.
    for how in 0..3 {
        match how {
            0 => ch.params.mute.store(true, Relaxed),
            1 => {
                ch.params.dca_mask.store(GroupSet::from_mask(1));
                rig.engine.controls.dcas[0].mute.store(true, Relaxed);
            }
            _ => {
                ch.params.mute_group_mask.store(GroupSet::from_mask(1));
                rig.engine.controls.mute_groups.store(GroupSet::from_mask(1));
            }
        }
        rig.run(20);
        assert!(rig.out(2).abs() < 1e-6 && rig.out(4).abs() < 1e-6 && rig.out(6).abs() < 1e-6, "mute {how}");
        ch.params.mute.store(false, Relaxed);
        rig.engine.controls.dcas[0].mute.store(false, Relaxed);
        rig.engine.controls.mute_groups.store(GroupSet::from_mask(0));
    }

    // IGNORE CHANNEL MUTE keeps bus and matrix TAP sends (not the main send).
    ch.params.mute.store(true, Relaxed);
    send.ignore_mute.store(true, Relaxed);
    ch.matrix_send(0).unwrap().ignore_mute.store(true, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), 0.5 * CENTRE, 1e-5));
    assert!(close(rig.out(4), 0.5 * CENTRE, 1e-5));
    assert!(rig.out(6).abs() < 1e-6);

    // POST sends follow the fader and mute regardless.
    send.mode.store(1, Relaxed);
    rig.run(20);
    assert!(rig.out(2).abs() < 1e-6);
}

#[test]
fn bus_pre_sends_follow_the_bus_mute() {
    let mut rig = Rig::new(1, 1, 4);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    rig.set_input(0, 0.5);
    let fx_bus = rig.strip(StripKind::Bus, 12);
    ch.sends.get_or_create(12).unwrap().enabled.store(true, Relaxed);
    let send = fx_bus.sends.get_or_create(0).unwrap();
    send.enabled.store(true, Relaxed);
    send.ignore_mute.store(true, Relaxed); // only input and aux channels have it
    rig.strip(StripKind::Bus, 0).params.output_left.store(2, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), 0.5 * CENTRE, 1e-5));
    fx_bus.params.mute.store(true, Relaxed);
    rig.run(20);
    assert!(rig.out(2).abs() < 1e-6);
}

#[test]
fn group_sends_ignore_the_send_level_and_reversed_pan_link() {
    let mut rig = Rig::new(1, 1, 4);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    rig.set_input(0, 0.5);
    let send = ch.sends.get_or_create(2).unwrap();
    send.enabled.store(true, Relaxed);
    let bus = rig.strip(StripKind::Bus, 2);
    bus.params.output_left.store(2, Relaxed);
    bus.params.output_right.store(3, Relaxed);

    // LINK REVERSED: the send pan mirrors the channel pan.
    ch.params.pan.store(-100.0);
    send.pan_reversed.store(true, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), 0.0, 1e-5) && close(rig.out(3), 0.5 * EDGE, 1e-5), "{} {}", rig.out(2), rig.out(3));
    send.pan_reversed.store(false, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), 0.5 * EDGE, 1e-5) && close(rig.out(3), 0.0, 1e-5));

    // GROUP: post-fader, post-pan, no send level.
    send.mode.store(2, Relaxed);
    send.level_db.store(-100.0);
    ch.params.fader_db.store(-6.0206);
    rig.run(20);
    assert!(close(rig.out(2), 0.25 * EDGE, 1e-4), "{}", rig.out(2));
}

#[test]
fn matrix_send_pan_link() {
    let mut rig = Rig::new(1, 1, 6);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    rig.set_input(0, 0.5);
    let send = &ch.matrix_send(1).unwrap();
    send.on.store(true, Relaxed);
    let mtx = rig.strip(StripKind::Matrix, 1);
    mtx.params.output_left.store(2, Relaxed);
    mtx.params.output_right.store(3, Relaxed);
    assert!(send.pan_link.load(Relaxed), "linked by default");

    // Linked: the send follows the channel's pan, POST and TAP alike; its own pan doesn't count.
    ch.params.pan.store(-100.0);
    send.pan.store(100.0);
    for pre in [false, true] {
        send.pre.store(pre, Relaxed);
        rig.run(20);
        assert!(close(rig.out(2), 0.5 * EDGE, 1e-5) && close(rig.out(3), 0.0, 1e-5), "pre {pre}: {} {}", rig.out(2), rig.out(3));
    }

    // LINK REVERSED mirrors the channel's pan.
    send.pan_reversed.store(true, Relaxed);
    for pre in [false, true] {
        send.pre.store(pre, Relaxed);
        rig.run(20);
        assert!(close(rig.out(2), 0.0, 1e-5) && close(rig.out(3), 0.5 * EDGE, 1e-5), "reversed, pre {pre}");
    }

    // Unlinked: the send's own pan, whatever the channel's.
    send.pan_link.store(false, Relaxed);
    send.pan.store(0.0);
    for pre in [false, true] {
        send.pre.store(pre, Relaxed);
        rig.run(20);
        assert!(close(rig.out(2), 0.5 * CENTRE, 1e-5) && close(rig.out(3), 0.5 * CENTRE, 1e-5), "own pan, pre {pre}");
    }
    send.pan.store(100.0);
    rig.run(20);
    assert!(close(rig.out(2), 0.0, 1e-5) && close(rig.out(3), 0.5 * EDGE, 1e-5));

    // A bus has no PAN LINK: its panned output, balanced by the send's pan.
    send.on.store(false, Relaxed);
    ch.sends.get_or_create(4).unwrap().enabled.store(true, Relaxed);
    let bus = rig.strip(StripKind::Bus, 4);
    let bus_send = &bus.matrix_send(1).unwrap();
    bus_send.on.store(true, Relaxed);
    bus_send.pan_link.store(false, Relaxed);
    bus.params.pan.store(-100.0);
    rig.run(20);
    assert!(close(rig.out(2), 0.5 * EDGE, 1e-4) && close(rig.out(3), 0.0, 1e-5), "{} {}", rig.out(2), rig.out(3));
}

#[test]
fn automix_enables_input_channels_only_and_follows_the_fader() {
    let mut rig = Rig::new(1, 2, 4);
    set_channels(&rig.engine, 2);
    rig.ch(1).params.input_left.store(1, Relaxed);
    rig.set_input(0, 0.5);
    rig.set_input(1, 0.5);
    for i in 0..2 {
        let ch = rig.ch(i);
        let ins = ch.params.post_insert();
        ins.on.store(true, Relaxed);
        ins.mode.store(1, Relaxed); // AUTO_X
    }
    rig.run(50);
    let shared = 0.5 * CENTRE * 2.0 * SHARE;
    assert!(close(rig.out(0), shared, 1e-3), "{}", rig.out(0));
    assert!(close(rig.ch(0).meters.automix_db.load(), -3.0103, 0.05));

    // Automix sits in INS 2: the POST FDR tap is before it, POST PROC after it.
    let a = rig.ch(0);
    a.sends.get_or_create(5).unwrap().enabled.store(true, Relaxed);
    rig.strip(StripKind::Bus, 5).params.output_left.store(2, Relaxed);
    a.params.tap.store(6, Relaxed); // POST FDR
    rig.run(50);
    assert!(close(rig.out(2), 0.5 * CENTRE, 1e-3), "{}", rig.out(2));
    a.params.tap.store(7, Relaxed); // POST PROC
    rig.run(50);
    assert!(close(rig.out(2), 0.5 * CENTRE * SHARE, 1e-3), "{}", rig.out(2));
    a.params.tap.store(5, Relaxed);

    // X ENABLE off: both glide back to unity.
    rig.engine.controls.automix_enable[0].store(false, Relaxed);
    rig.run(50);
    assert!(close(rig.out(0), 2.0 * 0.5 * CENTRE, 1e-4), "{}", rig.out(0));
    rig.engine.controls.automix_enable[0].store(true, Relaxed);
    rig.run(50);
    assert!(close(rig.out(0), shared, 1e-3));

    // Pulling one fader down hands its share to the other channel.
    rig.ch(1).params.fader_db.store(-100.0);
    rig.run(200);
    assert!(close(rig.out(0), 0.5 * CENTRE, 1e-3), "{}", rig.out(0));
}

#[test]
fn trim_and_pre_fader_meters() {
    let mut rig = Rig::new(0, 1, 2);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    rig.set_input(0, 0.25);
    ch.params.input.trim_db.store(6.0206);
    ch.params.fader_db.store(-100.0);
    rig.run(20);
    let m = &ch.meters;
    for v in [&m.input_peak[0], &m.trim_peak[0], &m.pfl_peak[0], &m.output_peak[0]] {
        v.store(0.0);
    }
    rig.run(2);
    assert!(close(m.input_peak[0].load(), 0.25, 1e-5));
    assert!(close(m.trim_peak[0].load(), 0.5, 1e-4));
    assert!(close(m.pfl_peak[0].load(), 0.5, 1e-4));
    assert!(m.output_peak[0].load() < 1e-6);
}

/// The WING remote's meter stream has its own copy of the input, PRE (after
/// trim) and output peaks: taking the UI's leaves them, and taking them leaves the UI's.
#[test]
fn remote_meter_peaks_are_a_second_reader() {
    let mut rig = Rig::new(0, 1, 2);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    rig.set_input(0, 0.25);
    ch.params.input.trim_db.store(6.0206);
    ch.params.fader_db.store(0.0);
    rig.run(20);
    let [input, pre, output] = ch.remote_peaks.take();
    assert!(close(input[0], 0.25, 1e-5), "{input:?}");
    assert!(close(pre[0], 0.5, 1e-4), "{pre:?}");
    assert!(output[0] > 0.1, "{output:?}");
    // The UI's peaks are still there, and the remote's were taken.
    assert!(close(ch.meters.input_peak[0].exchange(0.0), 0.25, 1e-5));
    assert_eq!(ch.remote_peaks.take()[0][0], 0.0);
    rig.run(2);
    assert!(close(ch.remote_peaks.take()[0][0], 0.25, 1e-5));
}

/// The meters' PRE tap (the trim peak, and the remote stream's PRE) is after
/// TRIM and before the EQ: on a WING, TRIM moves a PRE strip meter and the EQ
/// does not. The pre-fader (PFL) peak hears the EQ.
#[test]
fn the_pre_meter_tap_follows_trim_not_eq() {
    let mut rig = Rig::new(0, 1, 2);
    set_channels(&rig.engine, 1);
    let ch = rig.ch(0);
    ch.params.fader_db.store(0.0);
    // A 750 Hz tone at -12 dB (two periods a block).
    for (i, v) in rig.inputs[0].iter_mut().enumerate() {
        *v = 0.25 * (std::f32::consts::TAU * 2.0 * i as f32 / BLOCK as f32).sin();
    }
    let measure = |rig: &mut Rig| {
        rig.run(200);
        let m = &ch.meters;
        for v in [&m.trim_peak[0], &m.pfl_peak[0]] {
            v.store(0.0);
        }
        ch.remote_peaks.take();
        rig.run(4);
        let db = |v: f32| 20.0 * v.log10();
        (db(m.trim_peak[0].load()), db(m.pfl_peak[0].load()), db(ch.remote_peaks.take()[1][0]))
    };
    let (trim0, pfl0, pre0) = measure(&mut rig);
    assert!(close(trim0, -12.04, 0.1) && close(pfl0, -12.04, 0.3) && close(pre0, trim0, 0.01), "{trim0} {pfl0} {pre0}");
    // +12 dB of EQ on the tone: PRE stays, the pre-fader level rises.
    let spec = &EQ_MODELS_CH[model_index(EQ_MODELS_CH, "STD").unwrap()];
    let eq = ch.params.eq();
    eq.on.store(true, Relaxed);
    eq.set_model(model_index(EQ_MODELS_CH, "STD").unwrap());
    eq.values[spec.param_index("2f").unwrap()].store(750.0);
    eq.values[spec.param_index("2g").unwrap()].store(12.0);
    let (trim1, pfl1, pre1) = measure(&mut rig);
    assert!(close(trim1, trim0, 0.05) && close(pre1, trim0, 0.05), "EQ moved PRE: {trim1} {pre1}");
    assert!(pfl1 > pfl0 + 9.0, "EQ: {pfl0} -> {pfl1}");
    // +6 dB of TRIM: PRE rises 6 dB.
    ch.params.input.trim_db.store(6.0206);
    let (trim2, _, pre2) = measure(&mut rig);
    assert!(close(trim2, trim0 + 6.02, 0.1) && close(pre2, trim2, 0.01), "TRIM: {trim0} -> {trim2} ({pre2})");
}

#[test]
fn monitor_dim_mute_mono_and_swap() {
    let mut rig = Rig::new(0, 1, 4);
    set_channels(&rig.engine, 1);
    let engine = rig.engine.clone();
    let c = &engine.controls;
    c.monitor_output_left.store(2, Relaxed);
    c.monitor_output_right.store(3, Relaxed);
    rig.ch(0).params.pan.store(-100.0);
    rig.set_input(0, 0.5);
    rig.run(20);
    let hard = 0.5 * EDGE;
    assert!(close(rig.out(2), hard, 1e-5) && close(rig.out(3), 0.0, 1e-5));

    c.monitor_dim.store(true, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), hard * 0.1, 1e-5), "{}", rig.out(2));
    c.monitor_dim_db.store(6.0206);
    rig.run(20);
    assert!(close(rig.out(2), hard * 0.5, 1e-4));
    c.monitor_mute.store(true, Relaxed);
    rig.run(20);
    assert!(rig.out(2).abs() < 1e-7);
    c.monitor_mute.store(false, Relaxed);
    c.monitor_dim.store(false, Relaxed);
    assert_eq!(c.monitor_level_db.load(), 0.0, "DIM never touches the level");

    c.monitor_mono.store(true, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), hard * 0.5, 1e-5) && close(rig.out(3), hard * 0.5, 1e-5));
    c.monitor_mono.store(false, Relaxed);
    c.monitor_swap.store(true, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), 0.0, 1e-5) && close(rig.out(3), hard, 1e-5));
    // Main 1 itself is unaffected.
    assert!(close(rig.out(0), hard, 1e-5));
}

#[test]
fn solo_pfl_or_afl_per_strip_type() {
    let mut rig = Rig::new(0, 1, 4);
    set_channels(&rig.engine, 1);
    let engine = rig.engine.clone();
    let c = &engine.controls;
    c.monitor_output_left.store(2, Relaxed);
    let ch = rig.ch(0);
    rig.set_input(0, 0.5);
    ch.sends.get_or_create(3).unwrap().enabled.store(true, Relaxed);
    let bus = rig.strip(StripKind::Bus, 3);
    bus.params.fader_db.store(-100.0);
    bus.params.solo.store(true, Relaxed);

    // BUS SOLO defaults to AFL: the bus fader is down, nothing to hear.
    rig.run(20);
    assert!(rig.out(2).abs() < 1e-6);
    c.bus_solo_afl.store(false, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), 0.5 * CENTRE, 1e-5), "{}", rig.out(2));

    // CHANNEL SOLO covers aux channels too.
    bus.params.solo.store(false, Relaxed);
    let aux = rig.strip(StripKind::Aux, 0);
    aux.params.input_left.store(0, Relaxed);
    aux.params.fader_db.store(-100.0);
    aux.params.solo.store(true, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), 0.5, 1e-5), "PFL: {}", rig.out(2));
    c.solo_afl.store(true, Relaxed);
    rig.run(20);
    assert!(rig.out(2).abs() < 1e-6);
}

#[test]
fn dca_mutegroups_main_link_and_startup_main_mute() {
    let mut rig = Rig::new(1, 1, 6);
    set_channels(&rig.engine, 1);
    let engine = rig.engine.clone();
    let c = &engine.controls;
    let ch = rig.ch(0);
    rig.set_input(0, 0.5);
    ch.params.dca_mask.store(GroupSet::from_mask(1));
    c.dcas[0].mute.store(true, Relaxed);
    rig.run(20);
    assert!(rig.out(0).abs() < 1e-6);
    c.dca_mute_groups.store(false, Relaxed);
    rig.run(20);
    assert!(close(rig.out(0), 0.5 * CENTRE, 1e-5), "the DCA mute no longer mutes its strips");
    c.dcas[0].mute.store(false, Relaxed);

    // MAIN LINK 1>2: Main 1's fader also acts on Main 2, not on Main 3.
    for i in 1..3 {
        ch.main_send(i).unwrap().on.store(true, Relaxed);
        rig.strip(StripKind::Main, i).params.output_left.store(2 * i as i32, Relaxed);
    }
    let main1 = rig.strip(StripKind::Main, 0);
    main1.params.fader_db.store(-6.0206);
    rig.run(20);
    assert!(close(rig.out(2), 0.5 * CENTRE, 1e-5));
    c.main_link.store(1, Relaxed);
    rig.run(20);
    assert!(close(rig.out(0), 0.25 * CENTRE, 1e-4));
    assert!(close(rig.out(2), 0.25 * CENTRE, 1e-4), "{}", rig.out(2));
    assert!(close(rig.out(4), 0.5 * CENTRE, 1e-5));
    c.main_link.store(2, Relaxed);
    rig.run(20);
    assert!(close(rig.out(4), 0.25 * CENTRE, 1e-4));

    // STARTUP MAIN MUTE
    assert!(!engine.apply_startup_main_mute());
    assert!(!main1.params.mute.load(Relaxed));
    c.startup_main_mute.store(true, Relaxed);
    assert!(engine.apply_startup_main_mute());
    assert!((0..WING_MAINS).all(|i| rig.strip(StripKind::Main, i).params.mute.load(Relaxed)));
}

/// WING 3.0.6: a POST (AFL) solo of an input, aux or DCA follows the pan.
#[test]
fn post_solo_follows_the_pan() {
    let mut rig = Rig::new(0, 1, 4);
    set_channels(&rig.engine, 1);
    let engine = rig.engine.clone();
    let c = &engine.controls;
    c.monitor_output_left.store(2, Relaxed);
    c.monitor_output_right.store(3, Relaxed);
    c.solo_afl.store(true, Relaxed);
    rig.set_input(0, 0.5);
    let ch = rig.ch(0);
    let aux = rig.strip(StripKind::Aux, 0);
    aux.params.input_left.store(0, Relaxed);
    for (strip, pan) in [(&ch, -100.0), (&aux, 100.0)] {
        strip.params.pan.store(pan);
        strip.params.solo.store(true, Relaxed);
        rig.run(20);
        let (near, far) = if pan < 0.0 { (rig.out(2), rig.out(3)) } else { (rig.out(3), rig.out(2)) };
        assert!(close(near, 0.5 * EDGE, 1e-4) && far.abs() < 1e-5, "{near} {far}");
        strip.params.solo.store(false, Relaxed);
    }

    // A DCA solo solos its members after their pan too.
    ch.params.dca_mask.store(GroupSet::from_mask(1));
    c.dcas[0].solo.store(true, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), 0.5 * EDGE, 1e-4) && rig.out(3).abs() < 1e-5, "{} {}", rig.out(2), rig.out(3));
    // PFL ignores it.
    c.solo_afl.store(false, Relaxed);
    rig.run(20);
    assert!(close(rig.out(2), rig.out(3), 1e-5));
}

#[test]
fn the_console_grows_and_shrinks_without_a_reset() {
    let mut rig = Rig::new(1, 1, 4);
    set_channels(&rig.engine, 2);
    let engine = rig.engine.clone();
    let ch = rig.ch(0);
    let main1 = rig.strip(StripKind::Main, 0);
    main1.params.fader_db.store(-6.0);

    // Two more mains: every strip sends to them (off, as the factory has it),
    // and the rest of the console stays as it was.
    assert_eq!(engine.set_num_strips(StripKind::Main, 6), 6);
    assert_eq!(engine.num_strips(StripKind::Main), 6);
    assert_eq!(engine.config().counts.mains, 6, "the configuration follows");
    assert!(Arc::ptr_eq(&main1, &rig.strip(StripKind::Main, 0)), "MAIN 1 is the same strip");
    assert_eq!(main1.params.fader_db.load(), -6.0);
    assert!(Arc::ptr_eq(&ch, &rig.ch(0)));
    let to_main6 = ch.main_send(5).unwrap();
    assert!(!to_main6.on.load(Relaxed));
    assert!(rig.strip(StripKind::Bus, 3).main_send(5).is_some());

    // Audio through MAIN 6.
    let main6 = rig.strip(StripKind::Main, 5);
    main6.params.fader_db.store(0.0);
    main6.params.output_left.store(2, Relaxed);
    ch.params.fader_db.store(0.0);
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    to_main6.on.store(true, Relaxed);
    to_main6.level_db.store(0.0);
    rig.set_input(0, 0.5);
    rig.run(20);
    assert!(close(rig.out(2), 0.5 * CENTRE, 1e-4), "{}", rig.out(2));

    // A main something feeds stays (and those before it); unfed, they go.
    assert_eq!(engine.set_num_strips(StripKind::Main, 2), 6, "MAIN 6 is fed");
    to_main6.on.store(false, Relaxed);
    assert_eq!(engine.set_num_strips(StripKind::Main, 2), 2);
    assert_eq!(engine.config().counts.mains, 2);
    assert_eq!(engine.set_num_strips(StripKind::Main, 0), 1, "a console has a main");
    // Added again, a main starts clean: the old send's settings don't come back.
    to_main6.on.store(true, Relaxed);
    assert_eq!(engine.set_num_strips(StripKind::Main, 6), 6);
    assert!(!ch.main_send(5).unwrap().on.load(Relaxed));
    rig.run(4);

    // Matrices: likewise, fed from a main too.
    assert_eq!(engine.set_num_strips(StripKind::Matrix, 10), 10);
    assert!(main1.matrix_send(9).is_some() && ch.matrix_send(9).is_some());
    main1.matrix_send(9).unwrap().on.store(true, Relaxed);
    assert_eq!(engine.set_num_strips(StripKind::Matrix, 4), 10);
    main1.matrix_send(9).unwrap().on.store(false, Relaxed);
    assert_eq!(engine.set_num_strips(StripKind::Matrix, 4), 4);

    // Buses: a fed bus stays.
    assert_eq!(engine.set_num_strips(StripKind::Bus, 24), 24);
    assert_eq!(engine.num_strips(StripKind::Bus), 24);
    ch.sends.get_or_create(22).unwrap().enabled.store(true, Relaxed);
    assert_eq!(engine.set_num_strips(StripKind::Bus, 8), 23);
    ch.sends.find(22).unwrap().enabled.store(false, Relaxed);
    assert_eq!(engine.set_num_strips(StripKind::Bus, 8), 8);
    assert_eq!(engine.num_strips(StripKind::Bus), 8);

    // Aux: inputs, they come and go (the interface warns first).
    assert_eq!(engine.set_num_strips(StripKind::Aux, 12), 12);
    assert_eq!(engine.set_num_strips(StripKind::Aux, 2), 2);
    assert_eq!(engine.num_strips(StripKind::Aux), 2);
    // Channels have set_num_channels.
    assert_eq!(engine.set_num_strips(StripKind::Channel, 9), 2);
    rig.run(4);
    engine.collect();
}
