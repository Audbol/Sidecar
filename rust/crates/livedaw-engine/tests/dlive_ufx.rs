//! A dLive's RackUltra (UFX) engines and Dyn8 engines in the signal path, and what
//! every FX engine hears besides its audio: a key source, the Global Key, MIDI notes
//! (`ufx.rs`).

use livedaw_dsp::dlive_fx::ultra::strip::STRIP;
use livedaw_dsp::model::model_index;
use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::keytap::KeySource;
use livedaw_engine::params::{StripKind, MAX_FX};
use livedaw_engine::profile::{dlive, DLiveLayout, Profile};
use livedaw_engine::routing::strip_source_input;
use livedaw_engine::sources::{FX_RETURN_SOURCE_BASE, NUM_SEND_FX};
use livedaw_engine::ufx::{DYN8_FX_BASE, NUM_DYN8, NUM_UFX, UFX_FIRST, UFX_STRIP_PARAMS};
use livedaw_engine::{Engine, Strip};
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

const BLOCK: usize = 128;
const SR: f32 = 48000.0;

/// What plays on an input: nothing, a 1 kHz sine, or a voice-like tone of a pitch.
#[derive(Clone, Copy)]
enum Signal {
    Off,
    Sine(f32),
    Voice(f32),
}

struct Rig {
    engine: Arc<Engine>,
    signals: Vec<Signal>,
    inputs: Vec<Vec<f32>>,
    outputs: Vec<Vec<f32>>,
    /// Output 1 (LR's left) of the last run.
    left: Vec<f32>,
    t: usize,
}

impl Rig {
    fn dlive() -> Self {
        let layout = DLiveLayout::FOH;
        Self::of(&ConsoleConfig { profile: Profile::AhDLive(layout), counts: layout.counts() })
    }

    fn of(config: &ConsoleConfig) -> Self {
        let engine = Arc::new(Engine::new(0));
        engine.configure(config);
        let (num_ins, num_outs) = (8, 8);
        let in_map: Vec<i32> = (0..num_ins as i32).collect();
        let out_map: Vec<i32> = (0..num_outs as i32).collect();
        unsafe { engine.prepare(f64::from(SR), BLOCK, &in_map, &out_map) };
        Self {
            engine,
            signals: vec![Signal::Off; num_ins],
            inputs: vec![vec![0.0; BLOCK]; num_ins],
            outputs: vec![vec![0.0; BLOCK]; num_outs],
            left: Vec::new(),
            t: 0,
        }
    }

    /// Runs `blocks` blocks; the peak of LR's left over the last quarter of them.
    fn run(&mut self, blocks: usize) -> f32 {
        let ins: Vec<*const f32> = self.inputs.iter().map(|v| v.as_ptr()).collect();
        let outs: Vec<*mut f32> = self.outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
        self.left.clear();
        for _ in 0..blocks {
            for (buf, signal) in self.inputs.iter_mut().zip(&self.signals) {
                for (i, x) in buf.iter_mut().enumerate() {
                    let t = (self.t + i) as f32 / SR;
                    let w = |f: f32| (std::f32::consts::TAU * f * t).sin();
                    *x = match *signal {
                        Signal::Off => 0.0,
                        Signal::Sine(level) => level * w(1000.0),
                        Signal::Voice(f) => 0.2 * (w(f) + 0.5 * w(2.0 * f) + 0.33 * w(3.0 * f) + 0.25 * w(4.0 * f)),
                    };
                }
            }
            self.t += BLOCK;
            unsafe { self.engine.process(&ins, &outs, BLOCK) };
            self.left.extend_from_slice(&self.outputs[0]);
        }
        let tail = &self.left[self.left.len() - self.left.len() / 4..];
        tail.iter().fold(0.0f32, |m, x| m.max(x.abs()))
    }

    fn channel(&self, i: usize) -> Arc<Strip> {
        self.engine.strips(StripKind::Channel)[i].clone()
    }

    /// Channel `i` on input `i`, up and unmuted, on LR (the MixRack's outputs 1-2).
    fn open(&self, i: usize) -> Arc<Strip> {
        let ch = self.channel(i);
        ch.params.input_left.store(i as i32, Relaxed);
        ch.params.mute.store(false, Relaxed);
        ch.params.fader_db.store(0.0);
        ch
    }

    fn fx_model(&self, slot: usize, token: &str) -> bool {
        let model = model_index(self.engine.fx_models(), token).unwrap();
        self.engine.set_fx_model(slot, model, true)
    }

    fn set_fx(&self, slot: usize, id: &str, v: f32) {
        let params = self.engine.fx_params(slot).unwrap();
        params.param_in(self.engine.fx_models(), id).unwrap_or_else(|| panic!("no parameter {id}")).store(v);
    }

    /// The pitch of the last run's tail, by autocorrelation (150 to 400 Hz).
    fn pitch(&self) -> f32 {
        let x = &self.left[self.left.len() - 16384..];
        let at = |lag: usize| x.iter().zip(&x[lag..]).map(|(a, b)| a * b).sum::<f32>();
        let (lo, hi) = ((SR / 400.0) as usize, (SR / 150.0) as usize);
        let best = (lo..=hi).max_by(|&a, &b| at(a).total_cmp(&at(b))).unwrap();
        let (y0, y1, y2) = (at(best - 1), at(best), at(best + 1));
        let fine = 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2);
        SR / (best as f32 + fine)
    }
}

fn insert(ch: &Strip, slot: usize) {
    let ins = ch.params.pre_insert();
    ins.fx.store(slot as i32, Relaxed);
    ins.on.store(true, Relaxed);
}

#[test]
fn a_dlive_has_24_fx_and_17_to_24_take_only_the_rackultra_models() {
    let rig = Rig::dlive();
    let e = &rig.engine;
    assert_eq!((e.num_fx(), UFX_FIRST, NUM_UFX), (24, 16, 8));
    assert!(rig.fx_model(UFX_FIRST, "DLVSAT") && rig.fx_model(UFX_FIRST + 7, "DLVSPACE"));
    assert!(!rig.fx_model(UFX_FIRST, "DLVTAP"), "a RackExtra model on a RackUltra engine");
    assert!(!rig.fx_model(0, "DLVSAT") && !rig.fx_model(24, "DLVSAT"), "a RackUltra model elsewhere");
    assert!(!rig.fx_model(24, "DLVDYN8") && !rig.fx_model(DYN8_FX_BASE, "DLVTAP"), "Dyn8 engines aren't FX slots");
    assert!(e.ufx.strip_of(UFX_FIRST).is_some() && e.ufx.strip_of(UFX_FIRST + 7).is_some());
    assert!(e.ufx.strip_of(15).is_none() && e.ufx.strip_of(24).is_none());
    assert_eq!(STRIP.params.len(), UFX_STRIP_PARAMS);
    // The Dyn8 engines hold Dyn8 and nothing else.
    let dyn8 = e.fx_params(DYN8_FX_BASE + NUM_DYN8 - 1).unwrap();
    assert_eq!(e.fx_models()[dyn8.model_index_in(e.fx_models())].token, "DLVDYN8");
    assert!(e.fx_params(DYN8_FX_BASE + NUM_DYN8).is_none() && e.fx(DYN8_FX_BASE + NUM_DYN8).is_none());
    assert_eq!(DYN8_FX_BASE, MAX_FX);

    // No other console has them: its FX 17-24 are plain slots, its Dyn8 engines empty.
    let wing = Rig::of(&ConsoleConfig::wing());
    assert!(wing.engine.ufx.strip_of(UFX_FIRST).is_none() && !wing.engine.ufx.live());
    assert!(wing.engine.fx_params(DYN8_FX_BASE).is_none() && wing.engine.fx(DYN8_FX_BASE).is_none());
    assert!(wing.engine.fx_key(DYN8_FX_BASE).is_none() && wing.engine.fx_key(0).is_some());
    assert_eq!(wing.engine.num_fx(), 16);
}

#[test]
fn a_ufx_engine_runs_its_strip_around_its_device() {
    let mut rig = Rig::dlive();
    rig.signals[0] = Signal::Sine(0.05);
    let ch = rig.open(0);
    assert!(rig.fx_model(UFX_FIRST, "DLVSAT"));
    insert(&ch, UFX_FIRST);
    let flat = rig.run(100);
    assert!(flat > 0.01, "through the Saturator: {flat}");

    // The post PEQ, after the device: a 12 dB bell at 1 kHz.
    let engine = rig.engine.clone();
    let strip = engine.ufx.strip_of(UFX_FIRST).unwrap();
    let set = |id: &str, v: f32| strip.param(id).unwrap_or_else(|| panic!("no value {id}")).store(v);
    set("d_in", 1.0);
    set("d_1f", 1000.0);
    set("d_1g", 12.0);
    let boosted = rig.run(100);
    assert!(boosted > 3.0 * flat && boosted < 5.0 * flat, "+12 dB: {flat} -> {boosted}");
    assert_eq!(strip.reduction(0), 0.0, "the dynamics are out");

    // The post dynamics as a compressor: it reduces, and its meter says so.
    set("c_on", 1.0);
    set("c_ty", 2.0);
    set("c_th", -40.0);
    set("c_ra", 20.0);
    let squeezed = rig.run(200);
    assert!(squeezed < 0.5 * boosted, "{boosted} -> {squeezed}");
    assert!(strip.reduction(1) < -6.0 && strip.reduction(0) == 0.0, "{} {}", strip.reduction(1), strip.reduction(0));

    // Bypassed, the strip is out with its device and its meter rests.
    rig.engine.controls.fx[UFX_FIRST].bypass.store(true, Relaxed);
    let bypassed = rig.run(100);
    assert!((bypassed - 0.05).abs() < 0.005, "the dry channel: {bypassed}");
    assert_eq!(strip.reduction(1), 0.0);

    // A value is kept within its parameter's range.
    assert!(!strip.set_value(UFX_STRIP_PARAMS, 1.0) && strip.set_value(0, 7.0));
    assert_eq!(strip.values[0].load(), 1.0);
}

#[test]
fn a_ufx_engine_fed_from_a_mix_comes_back_on_its_fx_return() {
    let mut rig = Rig::dlive();
    const _: () = assert!(NUM_SEND_FX >= UFX_FIRST + NUM_UFX);
    rig.signals[0] = Signal::Sine(0.25);
    let ch = rig.open(0);
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    // Ip 1 to FX send 8, which feeds FX 24 (and still FX 8, empty).
    let send = ch.sends.get_or_create(dlive::FX + 7).unwrap();
    send.enabled.store(true, Relaxed);
    send.level_db.store(0.0);
    let slot = UFX_FIRST + 7;
    assert!(rig.fx_model(slot, "DLVSAT"));
    let bus = strip_source_input(StripKind::Bus, dlive::FX + 7, 0);
    let engine = rig.engine.clone();
    let p = &engine.controls.fx[slot];
    p.source_left.store(bus, Relaxed);
    p.source_right.store(bus, Relaxed);
    // FX return 1 takes FX 24's return.
    let ret = rig.engine.strips(StripKind::Aux)[0].clone();
    ret.params.input_left.store(FX_RETURN_SOURCE_BASE + 2 * slot as i32, Relaxed);
    ret.params.input_right.store(FX_RETURN_SOURCE_BASE + 2 * slot as i32 + 1, Relaxed);
    ret.params.mute.store(false, Relaxed);
    ret.params.fader_db.store(0.0);
    let wet = rig.run(100);
    assert!(wet > 0.05, "FX 24's return on LR: {wet}");
    // Its strip runs there too: Width and PEQ belong to the return.
    let engine = rig.engine.clone();
    let strip = engine.ufx.strip_of(slot).unwrap();
    strip.param("a_in").unwrap().store(1.0);
    strip.param("a_1f").unwrap().store(1000.0);
    strip.param("a_1g").unwrap().store(-12.0);
    let cut = rig.run(100);
    assert!(cut < 0.5 * wet, "the pre PEQ's cut: {wet} -> {cut}");
    // An insert again: the return goes silent.
    p.source_left.store(-1, Relaxed);
    assert!(rig.run(100) < 1e-6);
}

/// Every band of a Dyn8 squeezing hard.
fn dyn8_squeeze(rig: &Rig, slot: usize, key: f32) {
    for n in 1..=4 {
        rig.set_fx(slot, &format!("mt_{n}"), -50.0);
        rig.set_fx(slot, &format!("mr_{n}"), 20.0);
        rig.set_fx(slot, &format!("mk_{n}"), key);
    }
}

#[test]
fn a_dyn8_engine_inserts_on_a_channel_and_on_a_mix() {
    let mut rig = Rig::dlive();
    rig.signals[0] = Signal::Sine(0.25);
    let ch = rig.open(0);
    let open = rig.run(100);
    assert!((open - 0.25).abs() < 0.02, "{open}");

    let slot = DYN8_FX_BASE + 63;
    insert(&ch, slot);
    assert!(!rig.engine.fx(slot).unwrap().has_device(), "made by the message thread's next pass");
    assert!((rig.run(20) - open).abs() < 0.02, "it passes the signal until then");
    rig.engine.collect();
    assert!(rig.engine.fx(slot).unwrap().has_device());
    assert!(rig.engine.fx_in_use(slot) && rig.engine.fx_users(slot).len() == 1);
    assert_eq!(rig.engine.num_fx(), 24, "a Dyn8 in use takes no FX slot into use");
    let defaults = rig.run(100);
    assert!(defaults > 0.5 * open && defaults <= open, "Dyn8 at its defaults, 2.4:1 over 0 dBu: {defaults}");
    dyn8_squeeze(&rig, slot, 0.0);
    let squeezed = rig.run(200);
    assert!(squeezed < 0.3 * open, "{open} -> {squeezed}");
    assert!(rig.engine.fx(slot).unwrap().meter() < -6.0, "its meter: {}", rig.engine.fx(slot).unwrap().meter());
    // Bypassed, and unassigned: its device goes with the insert.
    rig.engine.fx_params(slot).unwrap().bypass.store(true, Relaxed);
    assert!((rig.run(100) - open).abs() < 0.02);
    rig.engine.fx_params(slot).unwrap().bypass.store(false, Relaxed);
    ch.params.pre_insert().fx.store(-1, Relaxed);
    rig.engine.collect();
    assert!(!rig.engine.fx(slot).unwrap().has_device());

    // On LR's insert.
    let lr = rig.engine.strips(StripKind::Main)[0].clone();
    insert(&lr, slot);
    rig.engine.collect();
    let squeezed = rig.run(200);
    assert!(squeezed < 0.3 * open, "on the mix: {open} -> {squeezed}");
}

#[test]
fn no_other_console_has_dyn8_engines() {
    let mut rig = Rig::of(&ConsoleConfig { profile: Profile::Universal, counts: Counts::WING });
    rig.signals[0] = Signal::Sine(0.25);
    let ch = rig.engine.strips(StripKind::Channel)[0].clone();
    ch.params.input_left.store(0, Relaxed);
    ch.params.fader_db.store(0.0);
    rig.engine.strips(StripKind::Main)[0].params.fader_db.store(0.0);
    let open = rig.run(50);
    assert!(open > 0.1, "{open}");
    insert(&ch, DYN8_FX_BASE);
    rig.engine.collect();
    assert!(rig.engine.fx(DYN8_FX_BASE).is_none());
    assert!((rig.run(50) - open).abs() < 1e-4, "the insert passes its signal");
    assert_eq!(rig.engine.num_fx(), 16);
}

#[test]
fn a_dlive_grown_past_its_counts_stops_its_ufx_and_dyn8_engines() {
    let mut rig = Rig::dlive();
    rig.signals[0] = Signal::Sine(0.25);
    let ch = rig.open(0);
    let open = rig.run(50);
    let slot = DYN8_FX_BASE + 1;
    insert(&ch, slot);
    dyn8_squeeze(&rig, slot, 0.0);
    rig.engine.collect();
    assert!(rig.run(200) < 0.3 * open, "the Dyn8 runs on the dLive");
    assert!(rig.engine.ufx.strip_of(UFX_FIRST).is_some());

    let matrices = rig.engine.num_strips(StripKind::Matrix);
    rig.engine.set_num_strips(StripKind::Matrix, matrices + 1);
    assert_eq!(rig.engine.profile(), Profile::Universal);
    assert!(rig.engine.ufx.strip_of(UFX_FIRST).is_none() && rig.engine.fx(slot).is_none());
    assert!((rig.run(50) - open).abs() < 0.02, "the insert passes its signal");
    rig.engine.collect();
}

#[test]
fn a_dyn8_bands_external_key_comes_from_the_engines_key_source() {
    let mut rig = Rig::dlive();
    rig.signals[0] = Signal::Sine(0.02);
    let ch = rig.open(0);
    let slot = DYN8_FX_BASE;
    insert(&ch, slot);
    // Every band keyed externally, from Ip 2 at its input.
    dyn8_squeeze(&rig, slot, 2.0);
    let engine = rig.engine.clone();
    let key = engine.fx_key(slot).unwrap();
    key.set(KeySource::Channel(1), 0);
    rig.channel(1).params.input_left.store(1, Relaxed);
    rig.engine.collect();
    assert!(rig.channel(1).keys.has(0), "Ip 2 keeps its input for the key");
    let quiet_key = rig.run(200);
    assert!((quiet_key - 0.02).abs() < 0.004, "nothing on the key: {quiet_key}");
    rig.signals[1] = Signal::Sine(0.5);
    let loud_key = rig.run(200);
    assert!(loud_key < 0.5 * quiet_key, "Ip 2 squeezes Ip 1: {quiet_key} -> {loud_key}");
    // Without a key source there is no key: Dyn8's bands then take their own signal.
    key.reset();
    rig.engine.collect();
    assert!(!rig.channel(1).keys.has(0));
    let none = rig.run(200);
    assert!(none < 0.5 * quiet_key, "{none}");

    // A mix's key: Aux 1's, from a bus.
    key.set(KeySource::Strip(StripKind::Bus, dlive::AUX), 0);
    let ip2 = rig.channel(1);
    let send = ip2.sends.get_or_create(dlive::AUX).unwrap();
    send.enabled.store(true, Relaxed);
    send.level_db.store(0.0);
    rig.channel(1).params.mute.store(false, Relaxed);
    rig.channel(1).params.fader_db.store(0.0);
    rig.channel(1).main_send(0).unwrap().on.store(false, Relaxed);
    rig.engine.collect();
    let from_bus = rig.run(200);
    assert!(from_bus < 0.5 * quiet_key, "Aux 1 squeezes Ip 1 (a block late): {quiet_key} -> {from_bus}");
}

#[test]
fn a_ufx_strips_dynamics_duck_to_the_engines_key() {
    let mut rig = Rig::dlive();
    rig.signals[0] = Signal::Sine(0.1);
    let ch = rig.open(0);
    assert!(rig.fx_model(UFX_FIRST + 1, "DLVSAT"));
    insert(&ch, UFX_FIRST + 1);
    let engine = rig.engine.clone();
    let strip = engine.ufx.strip_of(UFX_FIRST + 1).unwrap();
    let set = |id: &str, v: f32| strip.param(id).unwrap().store(v);
    // The pre dynamics as a ducker on the EXTERNAL key.
    set("b_on", 1.0);
    set("b_ty", 3.0);
    set("b_ky", 1.0);
    set("b_th", -30.0);
    set("b_de", 40.0);
    rig.engine.fx_key(UFX_FIRST + 1).unwrap().set(KeySource::Channel(1), 0);
    rig.channel(1).params.input_left.store(1, Relaxed);
    rig.engine.collect();
    let open = rig.run(200);
    assert!(open > 0.03, "{open}");
    rig.signals[1] = Signal::Sine(0.5);
    let ducked = rig.run(300);
    assert!(ducked < 0.1 * open, "{open} -> {ducked}");
    assert!(strip.reduction(0) < -20.0, "{}", strip.reduction(0));
}

#[test]
fn midi_notes_reach_the_midi_harmoniser() {
    let mut rig = Rig::dlive();
    rig.signals[0] = Signal::Voice(220.0);
    let ch = rig.open(0);
    assert!(rig.fx_model(UFX_FIRST + 2, "DLVHMIDI"));
    rig.set_fx(UFX_FIRST + 2, "chor", 0.0);
    rig.set_fx(UFX_FIRST + 2, "gain", 0.0);
    insert(&ch, UFX_FIRST + 2);
    assert!(rig.run(400) < 2e-3, "no notes, no voices");
    rig.engine.ufx.notes.push(64, 100); // E4
    let held = rig.run(400);
    assert!(held > 0.02, "{held}");
    assert!((rig.pitch() / 329.63 - 1.0).abs() < 0.02, "the voice at E4: {}", rig.pitch());
    rig.engine.ufx.notes.push(64, 0);
    assert!(rig.run(400) < 2e-3, "released");
}

#[test]
fn the_global_key_reaches_a_tuner_that_follows_it() {
    let mut rig = Rig::dlive();
    // An A# between the notes of C major.
    rig.signals[0] = Signal::Voice(233.08);
    let ch = rig.open(0);
    let slot = UFX_FIRST + 3;
    assert!(rig.fx_model(slot, "DLVTUNER"));
    for (id, v) in [("glob", 1.0), ("scale", 0.0), ("retune", 10.0), ("trans", 0.0)] {
        rig.set_fx(slot, id, v);
    }
    insert(&ch, slot);
    // The Global Key starts as C Major: A# goes to A or B.
    assert_eq!((rig.engine.ufx.global_key.key(), rig.engine.ufx.global_key.scale()), (0, 0));
    rig.run(600);
    let c = rig.pitch();
    assert!((c / 220.0 - 1.0).abs() < 0.012 || (c / 246.94 - 1.0).abs() < 0.012, "in C major: {c}");
    // F Major holds A#.
    rig.engine.ufx.global_key.set(5, 0);
    rig.run(600);
    let f = rig.pitch();
    assert!((f / 233.08 - 1.0).abs() < 0.008, "in F major: {f}");
    // A device made later hears the key with its first block.
    assert!(rig.fx_model(slot, "DLVSAT") && rig.fx_model(slot, "DLVTUNER"));
    for (id, v) in [("glob", 1.0), ("scale", 0.0), ("retune", 10.0), ("trans", 0.0)] {
        rig.set_fx(slot, id, v);
    }
    rig.run(600);
    let again = rig.pitch();
    assert!((again / 233.08 - 1.0).abs() < 0.008, "a new Tuner in F major: {again}");
}
