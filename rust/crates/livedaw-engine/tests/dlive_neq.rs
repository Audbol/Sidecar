//! The dLive's NEQ-12 on a mix's NEQ block, through the real strip: its bell
//! boosts a tone, its Ins take it out, and the audio path allocates nothing
//! even while every one of its controls moves.

use livedaw_dsp::dlive::{neq_band, NEQ_BANDS, NEQ_CUT, NEQ_HF_SHAPE, NEQ_IN, NEQ_LF_SHAPE, NEQ_SHELF};
use livedaw_dsp::model::model_index;
use livedaw_engine::blocks::{BlockKind, BlockParams};
use livedaw_engine::config::ConsoleConfig;
use livedaw_engine::params::{EqBlockParams, StripKind};
use livedaw_engine::profile::{dlive, DLiveLayout, Profile};
use livedaw_engine::Engine;
use std::cell::Cell;
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

thread_local! {
    static IN_AUDIO: Cell<bool> = const { Cell::new(false) };
    /// Per thread: the tests run side by side, and another engine's first
    /// blocks allocate (its one-time set-up).
    static AUDIO_ALLOCS: Cell<usize> = const { Cell::new(0) };
}

struct Counting;

fn note() {
    if IN_AUDIO.try_with(|c| c.get()).unwrap_or(false) {
        let _ = AUDIO_ALLOCS.try_with(|n| n.set(n.get() + 1));
    }
}

unsafe impl std::alloc::GlobalAlloc for Counting {
    unsafe fn alloc(&self, l: std::alloc::Layout) -> *mut u8 {
        note();
        unsafe { std::alloc::System.alloc(l) }
    }
    unsafe fn dealloc(&self, p: *mut u8, l: std::alloc::Layout) {
        note();
        unsafe { std::alloc::System.dealloc(p, l) }
    }
    unsafe fn alloc_zeroed(&self, l: std::alloc::Layout) -> *mut u8 {
        note();
        unsafe { std::alloc::System.alloc_zeroed(l) }
    }
    unsafe fn realloc(&self, p: *mut u8, l: std::alloc::Layout, n: usize) -> *mut u8 {
        note();
        unsafe { std::alloc::System.realloc(p, l, n) }
    }
}

#[global_allocator]
static ALLOC: Counting = Counting;

const BLOCK: usize = 128;
const SR: f32 = 48000.0;
/// Aux 1's output, the MixRack's socket 3.
const AUX_OUT: usize = 2;

struct Rig {
    engine: Arc<Engine>,
    inputs: Vec<Vec<f32>>,
    outputs: Vec<Vec<f32>>,
    t: usize,
}

impl Rig {
    fn new() -> Self {
        let layout = DLiveLayout::FOH;
        let engine = Arc::new(Engine::new(0));
        engine.configure(&ConsoleConfig { profile: Profile::AhDLive(layout), counts: layout.counts() });
        let (num_ins, num_outs) = (64, 32);
        let in_map: Vec<i32> = (0..num_ins as i32).collect();
        let out_map: Vec<i32> = (0..num_outs as i32).collect();
        unsafe { engine.prepare(f64::from(SR), BLOCK, &in_map, &out_map) };
        Self { engine, inputs: vec![vec![0.0; BLOCK]; num_ins], outputs: vec![vec![0.0; BLOCK]; num_outs], t: 0 }
    }

    /// Runs `blocks` blocks of a 1 kHz tone on input 1; Aux 1's peak over the last.
    fn run(&mut self, blocks: usize) -> f32 {
        let ins: Vec<*const f32> = self.inputs.iter().map(|v| v.as_ptr()).collect();
        let outs: Vec<*mut f32> = self.outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
        for _ in 0..blocks {
            for (i, x) in self.inputs[0].iter_mut().enumerate() {
                *x = 0.25 * (2.0 * std::f32::consts::PI * 1000.0 * (self.t + i) as f32 / SR).sin();
            }
            self.t += BLOCK;
            IN_AUDIO.with(|c| c.set(true));
            unsafe { self.engine.process(&ins, &outs, BLOCK) };
            IN_AUDIO.with(|c| c.set(false));
        }
        self.outputs[AUX_OUT].iter().fold(0.0f32, |m, x| m.max(x.abs()))
    }

    /// Ip 1 up and unmuted, off LR, sent to Aux 1.
    fn tone_to_aux(&self) -> Arc<livedaw_engine::Strip> {
        let channel = &self.engine.strips(StripKind::Channel)[0];
        channel.params.mute.store(false, Relaxed);
        channel.params.fader_db.store(0.0);
        channel.main_send(0).unwrap().on.store(false, Relaxed);
        let send = channel.sends.get_or_create(dlive::AUX).unwrap();
        send.enabled.store(true, Relaxed);
        send.level_db.store(0.0);
        self.engine.strips(StripKind::Bus)[dlive::AUX].clone()
    }
}

fn neq_block(strip: &livedaw_engine::Strip) -> Arc<livedaw_engine::blocks::Block> {
    strip.blocks().into_iter().find(|b| b.kind() == BlockKind::Eq).expect("the mix's NEQ block")
}

fn eq_of(block: &livedaw_engine::blocks::Block) -> &EqBlockParams {
    let BlockParams::Eq(eq) = &block.params else { panic!("an EQ block") };
    eq
}

#[test]
fn a_mix_neq_block_can_hold_the_neq12_and_it_works() {
    let mut rig = Rig::new();
    let aux = rig.tone_to_aux();
    let block = neq_block(&aux);
    let neq = eq_of(&block);
    assert_eq!(neq.model_spec().unwrap().token, "DLVGEQC", "a new mix starts on the Constant-Q GEQ");

    let model = model_index(neq.table(), "DLVNEQ12").expect("the NEQ-12 is one of the block's models");
    neq.set_model(model);
    neq.on.store(true, Relaxed);
    let dry = rig.run(60);
    assert!(dry > 0.2, "the tone reaches Aux 1: {dry}");

    // A bell in the middle band, +12 dB at the tone: 4 times the level.
    let band = neq_band(6);
    neq.values[band + 1].store(1000.0);
    neq.values[band + 2].store(12.0);
    neq.values[band + 3].store(1.0);
    rig.run(20);
    let boosted = rig.run(40);
    assert!((boosted / dry - 3.98).abs() < 0.1, "{}", boosted / dry);

    // The band's In out: back to dry. Then the overall In out, with a cut in the way.
    neq.values[band].store(0.0);
    rig.run(20);
    assert!((rig.run(40) / dry - 1.0).abs() < 0.02);
    neq.values[band].store(1.0);
    neq.values[NEQ_LF_SHAPE].store((NEQ_CUT + 2) as f32);
    neq.values[neq_band(0) + 1].store(5000.0); // well above the tone
    rig.run(40);
    assert!(rig.run(40) < dry * 0.01, "the 48 dB/oct low cut in the way of the tone");
    neq.values[NEQ_IN].store(0.0);
    rig.run(20);
    assert!((rig.run(40) / dry - 1.0).abs() < 0.02, "the overall In out");
    neq.on.store(false, Relaxed);
    rig.run(20);
    assert!((rig.run(40) / dry - 1.0).abs() < 0.02, "the block off");
}

#[test]
fn the_neq12_allocates_nothing_while_its_controls_move() {
    let mut rig = Rig::new();
    let aux = rig.tone_to_aux();
    let block = neq_block(&aux);
    let neq = eq_of(&block);
    neq.set_model(model_index(neq.table(), "DLVNEQ12").unwrap());
    neq.on.store(true, Relaxed);
    rig.run(8); // the first blocks' one-time set-up, before counting

    AUDIO_ALLOCS.with(|n| n.set(0));
    for n in 0..500usize {
        let x = n as f32 * 0.17;
        for b in 0..NEQ_BANDS {
            let i = neq_band(b);
            neq.values[i].store(if n % 7 == 0 && b % 2 == 0 { 0.0 } else { 1.0 });
            neq.values[i + 1].store(20.0 * 1000f32.powf(0.5 + 0.5 * (x + b as f32).sin()));
            neq.values[i + 2].store(15.0 * (x * 1.3 + b as f32).sin());
            neq.values[i + 3].store(1.0 / 9.0 + (1.5 - 1.0 / 9.0) * (0.5 + 0.5 * (x * 0.7 + b as f32).sin()));
        }
        neq.values[NEQ_LF_SHAPE].store((n % 5) as f32);
        neq.values[NEQ_HF_SHAPE].store(((n + 2) % 5) as f32);
        neq.values[NEQ_IN].store(if n % 11 == 0 { 0.0 } else { 1.0 });
        rig.run(1);
    }
    // Back to shelves, in the block, all still running: nothing surprising at the end.
    neq.values[NEQ_LF_SHAPE].store(NEQ_SHELF as f32);
    neq.values[NEQ_HF_SHAPE].store(NEQ_SHELF as f32);
    rig.run(8);
    assert_eq!(AUDIO_ALLOCS.with(Cell::get), 0, "the audio thread allocated");
    assert!(rig.outputs[AUX_OUT].iter().all(|x| x.is_finite()));
}
