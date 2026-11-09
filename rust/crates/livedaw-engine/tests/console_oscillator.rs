//! A console's own oscillator, as a 01V uses it (Owner's Manual p.204): it takes
//! over what it's assigned to, and burst noise is 200 ms every four seconds.

use livedaw_dsp::generator::generator_mode;
use livedaw_engine::config::ConsoleConfig;
use livedaw_engine::params::StripKind;
use livedaw_engine::profile::{y01v, Profile};
use livedaw_engine::{Counts, Engine};
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

const BLOCK: usize = 128;
const BLOCKS_PER_SECOND: usize = 375; // at 48 kHz
const AUX1_OUTPUT: usize = 2;

struct Rig {
    engine: Arc<Engine>,
    inputs: Vec<Vec<f32>>,
    outputs: Vec<Vec<f32>>,
    t: usize,
}

impl Rig {
    fn new() -> Self {
        let engine = Arc::new(Engine::new(0));
        engine.configure(&ConsoleConfig { profile: Profile::Yamaha01V, counts: Counts::Y01V });
        let (num_ins, num_outs) = (24, 24);
        let in_map: Vec<i32> = (0..num_ins as i32).collect();
        let out_map: Vec<i32> = (0..num_outs as i32).collect();
        unsafe { engine.prepare(48000.0, BLOCK, &in_map, &out_map) };
        let aux = engine.strips(StripKind::Bus)[y01v::AUX].clone();
        aux.params.mute.store(false, Relaxed);
        aux.params.fader_db.store(0.0);
        Self { engine, inputs: vec![vec![0.0; BLOCK]; num_ins], outputs: vec![vec![0.0; BLOCK]; num_outs], t: 0 }
    }

    /// CH 1 sending a tone of peak 0.25 to AUX 1.
    fn tone_to_aux1(&self) {
        let ch = self.engine.strips(StripKind::Channel)[0].clone();
        ch.params.mute.store(false, Relaxed);
        ch.params.fader_db.store(0.0);
        let send = ch.sends.get_or_create(y01v::AUX).unwrap();
        send.enabled.store(true, Relaxed);
        send.level_db.store(0.0);
    }

    /// The peak of AUX 1's output over `blocks` blocks of a 1 kHz tone on input 1.
    fn peak(&mut self, blocks: usize) -> f32 {
        let ins: Vec<*const f32> = self.inputs.iter().map(|v| v.as_ptr()).collect();
        let outs: Vec<*mut f32> = self.outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
        let mut peak = 0.0f32;
        for _ in 0..blocks {
            for (i, x) in self.inputs[0].iter_mut().enumerate() {
                *x = 0.25 * (2.0 * std::f32::consts::PI * 1000.0 * (self.t + i) as f32 / 48000.0).sin();
            }
            self.t += BLOCK;
            unsafe { self.engine.process(&ins, &outs, BLOCK) };
            peak = self.outputs[AUX1_OUTPUT].iter().fold(peak, |m, x| m.max(x.abs()));
        }
        peak
    }
}

#[test]
fn it_takes_over_what_its_assigned_to() {
    let mut rig = Rig::new();
    rig.tone_to_aux1();
    rig.peak(40);
    let tone = rig.peak(40);
    assert!(tone > 0.05, "the tone reaches AUX 1: {tone}");

    let osc = &rig.engine.oscillator;
    osc.exclusive.store(true, Relaxed);
    osc.assign(StripKind::Bus, y01v::AUX, [1.0, 1.0]);
    osc.set(true, generator_mode::SINE, 100.0, -20.0);
    rig.peak(40);
    let alone = rig.peak(375);
    assert!((alone - 0.1).abs() < 0.01, "only the oscillator, at -20 dB: {alone}");

    rig.engine.oscillator.set(false, generator_mode::SINE, 100.0, -20.0);
    rig.peak(40);
    let back = rig.peak(40);
    assert!((back - tone).abs() < 0.01, "the tone comes back: {back} (was {tone})");
}

#[test]
fn elsewhere_it_leaves_the_bus_alone() {
    let mut rig = Rig::new();
    rig.tone_to_aux1();
    rig.peak(40);
    let tone = rig.peak(40);
    let osc = &rig.engine.oscillator;
    osc.exclusive.store(true, Relaxed);
    osc.assign(StripKind::Bus, y01v::AUX + 1, [1.0, 1.0]);
    osc.set(true, generator_mode::SINE, 100.0, -20.0);
    rig.peak(40);
    assert!((rig.peak(40) - tone).abs() < 0.01, "AUX 2 has it, AUX 1 doesn't");
}

#[test]
fn burst_noise_is_200_ms_every_four_seconds() {
    let mut rig = Rig::new();
    let osc = &rig.engine.oscillator;
    osc.burst.store(true, Relaxed);
    osc.assign(StripKind::Bus, y01v::AUX, [1.0, 1.0]);
    osc.set(true, generator_mode::PINK, 1000.0, -6.0);
    assert!(rig.peak(BLOCKS_PER_SECOND / 10) > 0.01, "the burst's start");
    rig.peak(BLOCKS_PER_SECOND * 15 / 100); // past its end
    assert!(rig.peak(BLOCKS_PER_SECOND * 36 / 10) < 1e-5, "silence until the next");
    assert!(rig.peak(BLOCKS_PER_SECOND * 3 / 10) > 0.01, "the next burst");
}
