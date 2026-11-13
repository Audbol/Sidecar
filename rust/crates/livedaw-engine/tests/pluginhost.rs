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

// ---- Side chains: a plug-in block keyed from a channel's input or output. ----

use livedaw_engine::blocks::{BlockKind, BlockParams};
use livedaw_engine::InsertCallbacks;
use std::ffi::c_void;
use std::sync::atomic::{AtomicI32, AtomicU32};

/// A plug-in that passes its audio and records what it was given: the channel count of its last
/// call and the peak of the key (its third channel) over that call.
#[derive(Default)]
struct Recorder {
    channels: AtomicI32,
    key_peak: AtomicU32,
}

unsafe extern "C" fn record(ctx: *mut c_void, channels: *const *mut f32, num: i32, n: i32) -> i32 {
    let r = &*(ctx as *const Recorder);
    r.channels.store(num, Relaxed);
    let peak = if num >= 3 {
        std::slice::from_raw_parts(*channels.add(2), n as usize).iter().fold(0.0f32, |m, x| m.max(x.abs()))
    } else {
        0.0
    };
    r.key_peak.store(peak.to_bits(), Relaxed);
    0
}

impl Recorder {
    fn install(rig: &Rig, channel: usize) -> (std::sync::Arc<Recorder>, u32) {
        let rec = std::sync::Arc::new(Recorder::default());
        let ch = rig.channel(channel);
        // Where the RACK page puts it: after the input's trim and tap, before the level.
        let block = rig.engine.insert_block(&ch, BlockKind::Plugin, 3).unwrap();
        let BlockParams::Plugin(p) = &block.params else { panic!() };
        let cb = InsertCallbacks { process: record, release: None, ctx: std::sync::Arc::as_ptr(&rec) as *mut c_void };
        assert!(rig.engine.set_plugin_block(p.pid(), Some(cb), false));
        (rec, p.pid())
    }

    fn peak(&self) -> f32 {
        f32::from_bits(self.key_peak.load(Relaxed))
    }
}

#[test]
fn a_keyed_plugin_block_gets_the_key_as_a_third_channel() {
    let mut rig = Rig::new(HostSize::Ch16);
    let (rec, pid) = Recorder::install(&rig, 2);
    rig.run(10);
    assert_eq!(rec.channels.load(Relaxed), 2, "no key: the plug-in's two channels only");
    assert!(rig.engine.set_plugin_block_key(pid, 5, false));
    assert_eq!(rig.engine.plugin_block_key(pid), Some((5, false)));
    rig.run(10);
    assert_eq!(rec.channels.load(Relaxed), 3);
    assert!(near(rec.peak(), level(5)), "the key is channel 6's input: {} vs {}", rec.peak(), level(5));
    // The audio itself is untouched by the key.
    let peaks = rig.run(10);
    assert!(near(peaks[2], level(2)), "{}", peaks[2]);
    // No key again.
    assert!(rig.engine.set_plugin_block_key(pid, -1, false));
    rig.run(10);
    assert_eq!(rec.channels.load(Relaxed), 2);
}

#[test]
fn the_key_is_taken_before_or_after_the_keying_channels_chain() {
    let mut rig = Rig::new(HostSize::Ch16);
    // Channel 6 is 20 dB down at its output; its input is not.
    rig.channel(5).params.fader_db.store(-20.0);
    let (rec, pid) = Recorder::install(&rig, 2);
    assert!(rig.engine.set_plugin_block_key(pid, 5, false));
    rig.run(40);
    let pre = rec.peak();
    assert!(near(pre, level(5)), "pre-chain: {pre}");
    assert!(rig.engine.set_plugin_block_key(pid, 5, true));
    rig.run(40);
    let post = rec.peak();
    assert!(near(post, level(5) * 0.1), "post-chain: {post} vs {}", level(5) * 0.1);
}

#[test]
fn the_key_is_this_blocks_whichever_channel_runs_first() {
    // The keying channel is processed before the keyed one in a wave of its own: no block of delay.
    for (keyed, source) in [(2usize, 9usize), (9, 2)] {
        let mut rig = Rig::new(HostSize::Ch16);
        let (rec, pid) = Recorder::install(&rig, keyed);
        assert!(rig.engine.set_plugin_block_key(pid, source as i32, true));
        rig.run(1);
        assert!(near(rec.peak(), level(source)), "{keyed} keyed from {source}, first block: {}", rec.peak());
    }
    // Two channels keying each other form a cycle: one of them gets the previous block's key, and
    // from the second block on both hear it.
    let mut rig = Rig::new(HostSize::Ch16);
    let (a, pa) = Recorder::install(&rig, 1);
    let (b, pb) = Recorder::install(&rig, 4);
    assert!(rig.engine.set_plugin_block_key(pa, 4, true));
    assert!(rig.engine.set_plugin_block_key(pb, 1, true));
    rig.run(1);
    assert!(a.peak() == 0.0 || b.peak() == 0.0, "one of the two is a block late");
    rig.run(3);
    assert!(near(a.peak(), level(4)) && near(b.peak(), level(1)), "{} {}", a.peak(), b.peak());
}

#[test]
fn a_channel_cannot_key_itself_after_its_chain() {
    let rig = Rig::new(HostSize::Ch16);
    let (_rec, pid) = Recorder::install(&rig, 3);
    assert!(!rig.engine.set_plugin_block_key(pid, 3, true), "its own output feeding itself is refused");
    assert_eq!(rig.engine.plugin_block_key(pid), Some((-1, false)), "and nothing changed");
    assert!(rig.engine.set_plugin_block_key(pid, 3, false), "its own input is fine");
    assert_eq!(rig.engine.plugin_block_key(pid), Some((3, false)));
    assert!(!rig.engine.set_plugin_block_key(pid, 16, false), "no such channel");
    assert!(!rig.engine.set_plugin_block_key(pid + 100, 0, false), "no such block");
    assert_eq!(rig.engine.plugin_block_key(pid + 100), None);
    assert_eq!(rig.engine.plugin_block_key(pid), Some((3, false)));
}

#[test]
fn a_self_keyed_plugin_hears_its_own_input_this_block() {
    let mut rig = Rig::new(HostSize::Ch16);
    let (rec, pid) = Recorder::install(&rig, 4);
    assert!(rig.engine.set_plugin_block_key(pid, 4, false));
    rig.run(1);
    assert!(near(rec.peak(), level(4)), "{}", rec.peak());
}

// ---- The channel's delay: the latency alignment's mechanism. ----

#[test]
fn a_channels_delay_is_in_samples_from_zero() {
    let mut rig = Rig::new(HostSize::Ch16);
    let ch = rig.channel(1);
    assert!(ch.chain().contains(&livedaw_engine::chain::Node::Delay), "the host's channels have a delay stage");
    ch.params.input.delay.store(37.0);
    ch.params.input.delay_on.store(true, Relaxed);
    rig.run(40);
    let at = |t: usize, delay: usize| level(1) * (TAU * 1000.0 * (t - delay) as f32 / 48000.0).sin();
    let last = rig.t - 1;
    assert!((rig.outputs[1][BLOCK - 1] - at(last, 37)).abs() < 1e-3, "37 samples late");
    // A short delay is allowed (the strip delay's own minimum is 16), and 0 is the input as it came.
    ch.params.input.delay.store(5.0);
    rig.run(40);
    let last = rig.t - 1;
    assert!((rig.outputs[1][BLOCK - 1] - at(last, 5)).abs() < 1e-3, "5 samples late");
    ch.params.input.delay_on.store(false, Relaxed);
    rig.run(40);
    let last = rig.t - 1;
    assert!((rig.outputs[1][BLOCK - 1] - at(last, 0)).abs() < 1e-3, "no delay");
    // The neighbour never had one.
    assert!((rig.outputs[2][BLOCK - 1] - level(2) * (TAU * 1000.0 * last as f32 / 48000.0).sin()).abs() < 1e-3);
}

// ---- A host of another size, keeping what it can. ----

#[test]
fn a_host_changes_size_and_keeps_its_remaining_channels() {
    let mut rig = Rig::new(HostSize::Ch16);
    let block = rig.engine.insert_block(&rig.channel(2), BlockKind::Plugin, 3).unwrap();
    let uid = block.uid();
    rig.channel(2).params.fader_db.store(-6.0);
    rig.channel(2).params.input_left.store(9, Relaxed);
    rig.channel(15).params.fader_db.store(-3.0);
    assert!(rig.engine.route_output_source(0, DirectSource { kind: StripKind::Channel, number: 6, side: 0, post: true }.code()));

    // 16 -> 32: everything stays, the new channels are factory fresh and play their own outputs.
    assert!(rig.engine.resize_plugin_host(HostSize::Ch32));
    assert_eq!(rig.engine.profile(), Profile::PluginHost(HostSize::Ch32));
    assert_eq!(rig.engine.strips(StripKind::Channel).len(), 32);
    assert!(rig.engine.config().counts == Counts::plugin_host(32));
    let kept = rig.channel(2);
    assert!(kept.blocks().iter().any(|b| b.uid() == uid), "the rack of channel 3 is still there");
    assert_eq!(kept.params.fader_db.load(), -6.0);
    assert_eq!(kept.params.input_left.load(Relaxed), 9);
    assert_eq!(rig.channel(15).params.fader_db.load(), -3.0);
    assert_eq!(rig.channel(20).params.input_left.load(Relaxed), 20);
    assert!(rig.channel(20).blocks().is_empty());
    let patch = rig.engine.output_patch();
    assert_eq!(DirectSource::from_code(patch.get(0)).map(|d| d.number), Some(6), "a changed patch stays");
    assert_eq!(DirectSource::from_code(patch.get(20)).map(|d| d.number), Some(21), "new outputs play their channels");

    // 32 -> 16 again: channel 3's rack survives, what played a channel that went is left unpatched.
    assert!(rig.engine.route_output_source(1, DirectSource { kind: StripKind::Channel, number: 25, side: 0, post: true }.code()));
    assert!(rig.engine.resize_plugin_host(HostSize::Ch16));
    assert_eq!(rig.engine.strips(StripKind::Channel).len(), 16);
    assert!(rig.channel(2).blocks().iter().any(|b| b.uid() == uid));
    let patch = rig.engine.output_patch();
    assert_eq!(patch.get(1), -1, "output 2 played channel 25, which is gone");
    assert_eq!(DirectSource::from_code(patch.get(0)).map(|d| d.number), Some(6));
    assert_eq!(patch.get(20), -1);
    // The same size again, and a console that is not a host, change nothing.
    assert!(rig.engine.resize_plugin_host(HostSize::Ch16));
    let wing = Engine::new(0);
    wing.configure(&ConsoleConfig::default());
    assert!(!wing.resize_plugin_host(HostSize::Ch32));
    assert_eq!(wing.strips(StripKind::Channel).len(), Counts::WING.channels);
    rig.run(10); // and it still runs
}
