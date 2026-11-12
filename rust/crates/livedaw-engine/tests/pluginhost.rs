//! The plug-in host, end to end: each channel takes one interface input and returns to one
//! interface output, and mixes with nothing.

use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::params::StripKind;
use livedaw_engine::profile::{HostSize, Profile};
use livedaw_engine::routing::DirectSource;
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

/// The tone on input `n` (0-based): its own level, so an output shows which input it hears.
fn level(n: usize) -> f32 {
    0.005 * (n + 1) as f32
}

impl Rig {
    fn new(size: HostSize) -> Self {
        let engine = Arc::new(Engine::new(0));
        let profile = Profile::PluginHost(size);
        engine.configure(&ConsoleConfig { profile, counts: profile.spec().counts });
        let ports = size.channels() + 2;
        let in_map: Vec<i32> = (0..ports as i32).collect();
        let out_map: Vec<i32> = (0..ports as i32).collect();
        unsafe { engine.prepare(48000.0, BLOCK, &in_map, &out_map) };
        Self { engine, inputs: vec![vec![0.0; BLOCK]; ports], outputs: vec![vec![0.0; BLOCK]; ports], t: 0 }
    }

    /// `blocks` blocks of a 1 kHz tone on every input; each output's peak over the last.
    fn run(&mut self, blocks: usize) -> Vec<f32> {
        let ins: Vec<*const f32> = self.inputs.iter().map(|v| v.as_ptr()).collect();
        let outs: Vec<*mut f32> = self.outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
        for _ in 0..blocks {
            for (n, input) in self.inputs.iter_mut().enumerate() {
                for (i, x) in input.iter_mut().enumerate() {
                    *x = level(n) * (TAU * 1000.0 * (self.t + i) as f32 / 48000.0).sin();
                }
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

fn near(a: f32, b: f32) -> bool {
    (a - b).abs() <= 1e-4 + 1e-3 * b.abs()
}

#[test]
fn each_size_has_its_channels_and_nothing_to_mix() {
    for (size, channels) in [(HostSize::Ch16, 16), (HostSize::Ch32, 32), (HostSize::Ch64, 64)] {
        let profile = Profile::PluginHost(size);
        assert_eq!(profile.spec().name, "PLUGIN HOST");
        assert_eq!(profile.name(), "PLUGIN HOST");
        assert_eq!(size.channels(), channels);
        assert_eq!(HostSize::from_channels(channels), Some(size));
        let counts = Counts { channels, aux: 0, buses: 0, mains: 1, matrices: 0, dcas: 0, mute_groups: 0, fx: 0 };
        assert_eq!(profile.spec().counts, counts);
        assert_eq!(profile.spec().counts, Counts::plugin_host(channels));
        assert!(profile.fits(&counts));
        assert!(!profile.fits(&Counts::WING));
        let rig = Rig::new(size);
        assert_eq!(rig.engine.profile(), profile);
        assert_eq!(rig.engine.strips(StripKind::Channel).len(), channels);
        for kind in [StripKind::Aux, StripKind::Bus, StripKind::Matrix] {
            assert!(rig.engine.strips(kind).is_empty(), "{kind:?}");
        }
        assert_eq!(rig.engine.strips(StripKind::Main).len(), 1, "the engine's one main, fed by nothing");
        for n in [0, channels - 1] {
            let ch = rig.channel(n);
            assert!(ch.main_send(0).map_or(true, |m| !m.on.load(Relaxed)), "channel {} feeds no main", n + 1);
            assert!(ch.blocks().is_empty(), "no blocks by default");
        }
    }
}

#[test]
fn the_profile_round_trips_through_its_id() {
    assert_eq!(Profile::PluginHost(HostSize::Ch32).id(), "pluginhost");
    assert_eq!(Profile::from_id("pluginhost"), Some(Profile::PluginHost(HostSize::Ch16)));
    for size in HostSize::ALL {
        let profile = Profile::PluginHost(size);
        assert_eq!(Profile::from_id(profile.id()).map(|p| p.id()), Some("pluginhost"));
        assert_eq!(profile.or_universal(&profile.spec().counts), profile);
    }
    assert_eq!(Profile::PluginHost(HostSize::Ch16).or_universal(&Counts::plugin_host(32)), Profile::Universal);
}

#[test]
fn an_empty_chain_takes_input_n_to_output_n_and_nothing_else() {
    for size in HostSize::ALL {
        let mut rig = Rig::new(size);
        let peaks = rig.run(40);
        for n in 0..size.channels() {
            assert!(near(peaks[n], level(n)), "size {size:?}: output {} hears {} not {}", n + 1, peaks[n], level(n));
        }
        // The interface's spare ports hear nothing.
        for (n, peak) in peaks.iter().enumerate().skip(size.channels()) {
            assert_eq!(*peak, 0.0, "output {}", n + 1);
        }
    }
}

#[test]
fn bypass_passes_the_input_straight_through() {
    let mut rig = Rig::new(HostSize::Ch16);
    let ch = rig.channel(2);
    // Trim and polarity are the channel's own; bypass leaves the input as it came.
    ch.params.input.trim_db.store(12.0);
    ch.params.input.invert.store(true, Relaxed);
    let peaks = rig.run(40);
    assert!(peaks[2] > 3.0 * level(2), "trimmed up: {}", peaks[2]);
    ch.params.input.bypass.store(true, Relaxed);
    let peaks = rig.run(60);
    assert!(near(peaks[2], level(2)), "{} vs {}", peaks[2], level(2));
    assert!(near(peaks[1], level(1)) && near(peaks[3], level(3)), "the neighbours are untouched");
    // The sign too: a block later the output is the input, not its inverse.
    let input_now = level(2) * (TAU * 1000.0 * (rig.t - 1) as f32 / 48000.0).sin();
    assert!((rig.outputs[2][BLOCK - 1] - input_now).abs() < 1e-3);
}

#[test]
fn repatching_an_output_takes_another_channel() {
    let mut rig = Rig::new(HostSize::Ch16);
    rig.run(20);
    let source = DirectSource { kind: StripKind::Channel, number: 6, side: 0, post: true }.code();
    assert!(rig.engine.route_output_source(0, source));
    let peaks = rig.run(40);
    assert!(near(peaks[0], level(5)), "output 1 now plays channel 6: {}", peaks[0]);
    assert!(near(peaks[5], level(5)), "and channel 6 still plays its own");
    assert!(near(peaks[1], level(1)));
    // Silence an output by patching nothing.
    assert!(rig.engine.route_output_source(3, -1));
    let peaks = rig.run(40);
    assert_eq!(peaks[3], 0.0);
    assert!(near(peaks[4], level(4)));
}

#[test]
fn an_input_is_patchable_too() {
    let mut rig = Rig::new(HostSize::Ch16);
    // Channel 1 listens to interface input 9.
    rig.channel(0).params.input_left.store(8, Relaxed);
    let peaks = rig.run(60);
    assert!(near(peaks[0], level(8)), "{}", peaks[0]);
    assert!(near(peaks[8], level(8)), "channel 9 still hears its own");
}
