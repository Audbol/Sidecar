//! The Universal core end to end (Phase 1): a console that isn't a WING,
//! with audio through every part Phase 1 generalized, saved as a show and
//! loaded into a fresh engine, which must become the same console and sound
//! the same.

use livedaw_engine::blocks::{BlockKind, BlockParams, Step};
use livedaw_engine::chain::Node;
use livedaw_engine::sends::send_mode;
use livedaw_engine::{ConsoleConfig, Counts, Engine, StripKind};
use livedaw_session::console::NoPlugins;
use livedaw_session::Show;
use std::sync::atomic::Ordering::Relaxed;

const BLOCK: usize = 128;
/// -6.0206 dB: half the level.
const HALF_DB: f32 = -6.0206;

/// Drives an engine with constant (DC) inputs and keeps its outputs.
struct Rig {
    engine: Engine,
    inputs: Vec<Vec<f32>>,
    outputs: Vec<Vec<f32>>,
}

impl Rig {
    fn new(engine: Engine, num_ins: usize, num_outs: usize) -> Self {
        let in_map: Vec<i32> = (0..num_ins as i32).collect();
        let out_map: Vec<i32> = (0..num_outs as i32).collect();
        // SAFETY: no audio callback runs; the rig is the only caller of `process`.
        unsafe { engine.prepare(48000.0, BLOCK, &in_map, &out_map) };
        // Preparing drops the block-sized tap buffers; the app's next collect
        // (10 Hz) plans them again, as here.
        engine.collect();
        Self { engine, inputs: vec![vec![0.0; BLOCK]; num_ins], outputs: vec![vec![0.0; BLOCK]; num_outs] }
    }

    fn set_input(&mut self, ch: usize, v: f32) {
        self.inputs[ch].fill(v);
    }

    fn run(&mut self, blocks: usize) {
        let ins: Vec<*const f32> = self.inputs.iter().map(|v| v.as_ptr()).collect();
        let outs: Vec<*mut f32> = self.outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
        for _ in 0..blocks {
            // SAFETY: the buffers hold BLOCK samples and outlive the call.
            unsafe { self.engine.process(&ins, &outs, BLOCK) };
        }
    }

    fn out(&self, ch: usize) -> f32 {
        *self.outputs[ch].last().unwrap()
    }
}

fn close(a: f32, b: f32, tol: f32) -> bool {
    (a - b).abs() <= tol
}

/// 48 channels, 4 aux, 24 buses, 2 mains, 6 matrices, 32 DCAs, 12 mute groups.
const COUNTS: Counts = Counts { channels: 48, aux: 4, buses: 24, mains: 2, matrices: 6, dcas: 32, mute_groups: 12, fx: 16 };

/// The console the test runs (`COUNTS`):
/// * CH 40 (input 1, 0.5) -> BUS 21 (post-fader) -> MAIN 2 (+6 dB low shelf in an
///   extra EQ block) -> MTX 6 at -6 dB; CH 40 is in DCA 30 at -6 dB, and a user tap
///   after its INPUT feeds MTX 6 too, at -6 dB (before the fader and the DCA);
/// * CH 2 (input 2, 0.25) -> MAIN 1, in mute group 12;
/// * MAIN 2 on outputs 1/2, MTX 6 on 3/4, MAIN 1 on 5/6.
fn build(e: &Engine) {
    e.configure(&ConsoleConfig { counts: COUNTS });
    e.set_num_dcas(COUNTS.dcas);
    let ch40 = e.channel_at(39).unwrap();
    let ch2 = e.channel_at(1).unwrap();
    let bus21 = e.strips(StripKind::Bus).into_iter().find(|b| b.id() == 20).unwrap();
    let mains = e.strips(StripKind::Main);
    let mtx6 = e.strips(StripKind::Matrix)[5].clone();

    for s in [&ch40, &ch2, &bus21, &mains[0], &mains[1], &mtx6] {
        s.params.fader_db.store(0.0);
    }
    ch40.params.input_left.store(0, Relaxed);
    ch2.params.input_left.store(1, Relaxed);
    ch40.main_send(0).unwrap().on.store(false, Relaxed);
    let send = ch40.sends.get_or_create(20).unwrap();
    send.enabled.store(true, Relaxed);
    send.mode.store(send_mode::POST, Relaxed);
    send.level_db.store(0.0);
    bus21.main_send(1).unwrap().on.store(true, Relaxed);
    let to_mtx = mains[1].matrix_send(5).unwrap();
    to_mtx.on.store(true, Relaxed);
    to_mtx.level_db.store(HALF_DB);

    // DCA 30 and mute group 12, beyond the WING's 16 and 8.
    ch40.params.dca_mask.set(29, true);
    e.controls.dcas[29].fader_db.store(HALF_DB);
    ch2.params.mute_group_mask.set(11, true);

    // An extra EQ block on MAIN 2, before its POST copy.
    let post = mains[1].steps().iter().position(|s| matches!(s, Step::Node(Node::Post))).unwrap();
    let eq = e.insert_block(&mains[1], BlockKind::Eq, post).unwrap();
    let BlockParams::Eq(p) = &eq.params else { unreachable!() };
    p.param("lg", true).unwrap().store(6.0);
    p.on.store(true, Relaxed);

    // A user tap after CH 40's INPUT, sent to MTX 6.
    let input = ch40.steps().iter().position(|s| matches!(s, Step::Node(Node::Input))).unwrap();
    let tap = e.insert_tap(&ch40, input + 1).unwrap();
    let from_tap = ch40.matrix_send(5).unwrap();
    from_tap.tap.store(tap as i32, Relaxed);
    from_tap.on.store(true, Relaxed);
    from_tap.level_db.store(HALF_DB);

    for (m, l) in [(&mains[1], 0), (&mtx6, 2), (&mains[0], 4)] {
        m.params.output_left.store(l, Relaxed);
        m.params.output_right.store(l + 1, Relaxed);
    }
}

#[test]
fn a_console_that_isnt_a_wing_runs_saves_and_loads_the_same() {
    let engine = Engine::new(0);
    build(&engine);
    let mut rig = Rig::new(engine, 2, 6);
    rig.set_input(0, 0.5);
    rig.set_input(1, 0.25);
    rig.run(400); // the EQ block's shelf settles

    // MAIN 2: CH 40 at 0.5, halved by DCA 30, through BUS 21, +6 dB by the EQ block.
    let main2 = rig.out(0);
    assert!(main2 > 0.25 * 1.9 && main2 < 0.25 * 2.05, "MAIN 2: {main2}");
    // MTX 6: MAIN 2 at -6 dB, plus CH 40's tap (before the fader and the DCA) at -6 dB.
    let mtx6 = rig.out(2);
    assert!(close(mtx6, main2 * 0.5 + 0.25, 1e-3), "MTX 6: {mtx6} (MAIN 2 {main2})");
    // MAIN 1: CH 2 alone.
    assert!(close(rig.out(4), 0.25, 1e-4), "MAIN 1: {}", rig.out(4));

    // Saved and loaded into a fresh engine (a WING until the show says otherwise).
    let text = Show::new().to_xml(&rig.engine, &mut NoPlugins).to_document();
    let restored = Engine::new(0);
    let warnings = Show::new().load_from_text(&restored, &mut NoPlugins, &text, true).unwrap();
    assert!(warnings.is_empty(), "{warnings:?}");
    assert_eq!(restored.config().counts, COUNTS);
    assert_eq!(restored.counts(), Counts { dcas: COUNTS.dcas, ..COUNTS });
    let mut again = Rig::new(restored, 2, 6);
    again.set_input(0, 0.5);
    again.set_input(1, 0.25);
    again.run(400);
    for ch in 0..6 {
        assert!(close(again.out(ch), rig.out(ch), 1e-5), "output {ch}: {} after loading, {} before", again.out(ch), rig.out(ch));
    }

    // Mute group 12 mutes CH 2 on both consoles.
    for r in [&mut rig, &mut again] {
        r.engine.controls.mute_groups.set(11, true);
        r.run(20);
        assert!(r.out(4).abs() < 1e-6, "MAIN 1 with mute group 12: {}", r.out(4));
    }
}
