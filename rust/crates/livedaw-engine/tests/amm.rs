//! A dLive's Automatic Mic Mixer, end to end: real audio blocks through a dLive's
//! channels, in both modes.

mod common;

use common::{set_channels, unity_console};
use livedaw_engine::amm::{amm_mode, AmmParams};
use livedaw_engine::config::ConsoleConfig;
use livedaw_engine::params::StripKind;
use livedaw_engine::profile::{dlive, DLiveLayout, Profile};
use livedaw_engine::sends::send_mode;
use livedaw_engine::{Engine, Strip};
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

const BLOCK: usize = 128;
const RATE: f32 = 48000.0;
/// Peak of a talking mic: -15 dBFS RMS, with the room far below it.
const VOICE: f32 = 0.25;
const OFF: f32 = -40.0;

struct Rig {
    engine: Arc<Engine>,
    /// Each input's tone level.
    levels: Vec<f32>,
    inputs: Vec<Vec<f32>>,
    outputs: Vec<Vec<f32>>,
    t: usize,
}

impl Rig {
    fn dlive() -> Self {
        let engine = Arc::new(Engine::new(0));
        engine.configure(&ConsoleConfig { profile: Profile::AhDLive(DLiveLayout::FOH), counts: DLiveLayout::FOH.counts() });
        Self::on(engine)
    }

    fn on(engine: Arc<Engine>) -> Self {
        let (num_ins, num_outs) = (64, 32);
        let in_map: Vec<i32> = (0..num_ins as i32).collect();
        let out_map: Vec<i32> = (0..num_outs as i32).collect();
        unsafe { engine.prepare(48000.0, BLOCK, &in_map, &out_map) };
        Self {
            engine,
            levels: vec![0.0; num_ins],
            inputs: vec![vec![0.0; BLOCK]; num_ins],
            outputs: vec![vec![0.0; BLOCK]; num_outs],
            t: 0,
        }
    }

    fn ch(&self, i: usize) -> Arc<Strip> {
        self.engine.strips(StripKind::Channel)[i].clone()
    }

    fn amm(&self) -> &AmmParams {
        &self.engine.amm
    }

    /// Channels up, unmuted, in AMM `amm`.
    fn join(&self, channels: &[usize], amm: i32) {
        for &i in channels {
            let ch = self.ch(i);
            ch.params.mute.store(false, Relaxed);
            ch.params.fader_db.store(0.0);
            ch.amm.amm.store(amm, Relaxed);
        }
    }

    fn talk(&mut self, i: usize, level: f32) {
        self.levels[i] = level;
    }

    fn quiet(&mut self, i: usize) {
        self.levels[i] = 0.0;
    }

    fn fill_block(&mut self) {
        for (input, &level) in self.inputs.iter_mut().zip(&self.levels) {
            for (i, x) in input.iter_mut().enumerate() {
                *x = level * (2.0 * std::f32::consts::PI * 1000.0 * (self.t + i) as f32 / RATE).sin();
            }
        }
        self.t += BLOCK;
    }

    /// Runs `seconds` of audio; each output's peak over the last block.
    fn run(&mut self, seconds: f32) -> Vec<f32> {
        self.run_collecting(seconds, &mut |_| {});
        self.outputs.iter().map(|o| o.iter().fold(0.0f32, |m, x| m.max(x.abs()))).collect()
    }

    /// As [`run`](Self::run), and hands LR's left output to `collect` after each block.
    fn run_collecting(&mut self, seconds: f32, collect: &mut dyn FnMut(&[f32])) {
        let blocks = (seconds * RATE / BLOCK as f32).round() as usize;
        for _ in 0..blocks {
            self.fill_block();
            let ins: Vec<*const f32> = self.inputs.iter().map(|v| v.as_ptr()).collect();
            let outs: Vec<*mut f32> = self.outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
            unsafe { self.engine.process(&ins, &outs, BLOCK) };
            collect(&self.outputs[0]);
        }
    }

    fn gain(&self, i: usize) -> f32 {
        self.ch(i).amm.gain_db.load()
    }

    fn open(&self, i: usize) -> bool {
        self.ch(i).amm.open.load(Relaxed)
    }
}

#[track_caller]
fn near(actual: f32, wanted: f32, tolerance: f32, what: &str) {
    assert!((actual - wanted).abs() < tolerance, "{what}: {actual} dB, wanted {wanted} dB");
}

fn nom_rig(channels: &[usize]) -> Rig {
    let rig = Rig::dlive();
    rig.join(channels, 0);
    rig
}

#[test]
fn one_mic_talking_passes_at_0_db_and_the_others_close_to_off_attenuation() {
    let mut rig = nom_rig(&[0, 1, 2, 3]);
    // The same mic without the AMM: what 0 dB sounds like on the LR.
    let bypass = {
        let mut other = Rig::dlive();
        other.join(&[0], -1);
        other.talk(0, VOICE);
        other.run(0.5)[0]
    };
    rig.talk(0, VOICE);
    let peaks = rig.run(0.5);
    near(rig.gain(0), 0.0, 0.01, "the mic talking");
    assert!((peaks[0] / bypass - 1.0).abs() < 0.01, "at 0 dB: {} of {bypass}", peaks[0]);
    assert!(rig.open(0));
    for i in 1..4 {
        near(rig.gain(i), OFF, 0.01, "the closed mics");
        assert!(!rig.open(i));
    }
    // Off Attenuation is a control.
    rig.amm().off_atten_db.store(-60.0);
    rig.run(0.5);
    near(rig.gain(1), -60.0, 0.01, "a lower Off Attenuation");
    // Out of range, it's clamped.
    rig.amm().off_atten_db.store(-200.0);
    rig.run(0.5);
    near(rig.gain(1), -90.0, 0.01, "clamped to -90");
}

#[test]
fn nothing_opens_until_a_mic_first_triggers() {
    let mut rig = nom_rig(&[0, 1]);
    rig.run(0.5);
    near(rig.gain(0), OFF, 0.01, "no voice yet");
    near(rig.gain(1), OFF, 0.01, "no voice yet");
}

#[test]
fn open_mics_come_down_equally_by_the_nom_attenuation_per_doubling() {
    let mut rig = nom_rig(&[0, 1, 2, 3]);
    rig.talk(0, VOICE);
    rig.talk(1, VOICE);
    rig.run(0.5);
    near(rig.gain(0), -3.0, 0.01, "two open at 3 dB");
    near(rig.gain(1), -3.0, 0.01, "two open at 3 dB");
    rig.amm().nom_atten_db.store(6.0);
    rig.run(0.5);
    near(rig.gain(0), -6.0, 0.01, "two open at 6 dB");
    rig.amm().nom_atten_db.store(3.0);
    rig.talk(2, VOICE);
    rig.talk(3, VOICE);
    rig.run(0.5);
    near(rig.gain(0), -6.0, 0.01, "four open at 3 dB: two doublings");
    rig.quiet(3);
    rig.run(2.5 + 0.5); // past the hold
    near(rig.gain(0), -3.0 * 3f32.log2(), 0.05, "three open");
    near(rig.gain(3), OFF, 0.01, "closed again");
}

#[test]
fn the_last_open_mic_stays_open_until_another_opens() {
    let mut rig = nom_rig(&[0, 1]);
    rig.talk(0, VOICE);
    rig.run(0.5);
    rig.quiet(0);
    rig.run(4.0); // long past the hold
    near(rig.gain(0), 0.0, 0.01, "the room stays open");
    assert!(rig.open(0));
    near(rig.gain(1), OFF, 0.01, "the other stays closed");
    rig.talk(1, VOICE);
    rig.run(0.5);
    near(rig.gain(1), 0.0, 0.01, "another opens");
    near(rig.gain(0), OFF, 0.01, "and the last one closes");
    assert!(!rig.open(0));
}

#[test]
fn on_hold_keeps_a_mic_open_after_its_voice_stops() {
    let mut rig = nom_rig(&[0, 1]);
    rig.amm().on_hold_s.store(2.0);
    rig.talk(0, VOICE);
    rig.run(0.5);
    // The mic stops; another keeps talking, so the last-open rule can't be what holds it.
    rig.quiet(0);
    rig.talk(1, VOICE);
    rig.run(1.3);
    near(rig.gain(0), -3.0, 0.01, "held open: two mics open");
    near(rig.gain(1), -3.0, 0.01, "two mics open");
    rig.run(2.0);
    near(rig.gain(0), OFF, 0.01, "the hold has run out");
    near(rig.gain(1), 0.0, 0.01, "one mic open");
}

#[test]
fn the_chair_ducks_the_others_and_has_no_on_hold() {
    let mut rig = nom_rig(&[0, 1]);
    rig.amm().on_hold_s.store(5.0);
    rig.ch(0).amm.chair.store(true, Relaxed);
    rig.talk(0, VOICE);
    rig.talk(1, VOICE);
    rig.run(0.5);
    near(rig.gain(0), -3.0, 0.01, "the Chair is an open mic");
    near(rig.gain(1), -23.0, 0.01, "NOM and Chair Duck");
    rig.amm().chair_duck_db.store(-30.0);
    rig.run(0.5);
    near(rig.gain(1), -33.0, 0.01, "another Chair Duck");
    rig.quiet(0);
    rig.run(1.0); // The 5 s On Hold doesn't apply.
    near(rig.gain(0), OFF, 0.01, "the Chair has closed");
    near(rig.gain(1), 0.0, 0.01, "no more ducking");
}

#[test]
fn only_one_chair_per_amm_and_it_opens_by_its_sensitivity() {
    // About -52 dBFS RMS: too quiet to open an ordinary mic (10 dB above the -60 dBFS
    // starting floor), loud enough for a Chair at sensitivity 10 (3 dB above it).
    let level = 10f32.powf(-52.0 / 20.0) * std::f32::consts::SQRT_2;

    let mut rig = nom_rig(&[0, 1]);
    rig.talk(0, level);
    rig.run(0.3);
    assert!(!rig.open(0), "an ordinary mic doesn't open on that");

    let mut rig = nom_rig(&[0, 1]);
    rig.ch(0).amm.chair.store(true, Relaxed);
    rig.amm().chair_sensitivity.store(10.0);
    rig.talk(0, level);
    rig.run(0.3);
    assert!(rig.open(0), "a Chair at sensitivity 10 does");

    let mut rig = nom_rig(&[0, 1]);
    rig.ch(0).amm.chair.store(true, Relaxed);
    rig.amm().chair_sensitivity.store(1.0);
    rig.talk(0, level);
    rig.run(0.3);
    assert!(!rig.open(0), "a Chair at sensitivity 1 doesn't");
    // The newest Chair takes over.
    rig.ch(1).amm.chair.store(true, Relaxed);
    rig.run(0.05);
    assert!(rig.ch(1).amm.chair.load(Relaxed));
    assert!(!rig.ch(0).amm.chair.load(Relaxed), "the engine released the old one");
}

#[test]
fn solo_opens_one_and_turns_all_the_others_down() {
    let mut rig = nom_rig(&[0, 1, 2, 3]);
    rig.talk(0, VOICE);
    rig.talk(1, VOICE);
    rig.ch(2).amm.solo.store(true, Relaxed);
    rig.run(0.5);
    near(rig.gain(2), 0.0, 0.01, "the soloed mic, silent or not");
    near(rig.gain(0), OFF, 0.01, "talking but not soloed");
    near(rig.gain(1), OFF, 0.01, "talking but not soloed");
    assert!(rig.amm().solo[0].load(Relaxed), "the AMM shows its Solo");
    // Exclusive: the newest takes over, and the engine releases the old one.
    rig.ch(3).amm.solo.store(true, Relaxed);
    rig.run(0.3);
    assert!(!rig.ch(2).amm.solo.load(Relaxed));
    near(rig.gain(3), 0.0, 0.01, "the new soloed mic");
    near(rig.gain(2), OFF, 0.01, "the old one");
    // Clearing the AMM's Solo releases it.
    rig.amm().solo[0].store(false, Relaxed);
    rig.run(0.5);
    assert!(!rig.ch(3).amm.solo.load(Relaxed));
    assert!(!rig.amm().solo[0].load(Relaxed));
    near(rig.gain(0), -3.0, 0.01, "back to NOM");
    near(rig.gain(1), -3.0, 0.01, "back to NOM");
}

#[test]
fn best_mic_keeps_only_the_strongest_of_one_voice() {
    // One voice on two mics, starting together, 6 dB apart.
    let mut rig = nom_rig(&[0, 1, 2]);
    rig.amm().best_mic[0].store(true, Relaxed);
    rig.talk(0, VOICE);
    rig.talk(1, VOICE * 0.5);
    rig.run(0.5);
    near(rig.gain(0), 0.0, 0.01, "the strongest stays");
    near(rig.gain(1), OFF, 0.01, "the same voice on another mic");

    // A second talker, starting apart from the first at a like level, opens.
    rig.talk(2, VOICE);
    rig.run(0.5);
    near(rig.gain(0), -3.0, 0.01, "two talkers");
    near(rig.gain(2), -3.0, 0.01, "two talkers");

    // Best Mic off: the second mic of that one voice opens too.
    let mut rig = nom_rig(&[0, 1]);
    rig.talk(0, VOICE);
    rig.talk(1, VOICE * 0.5);
    rig.run(0.5);
    near(rig.gain(0), -3.0, 0.01, "without Best Mic");
    near(rig.gain(1), -3.0, 0.01, "without Best Mic");

    // The Chair isn't part of it.
    let mut rig = nom_rig(&[0, 1]);
    rig.amm().best_mic[0].store(true, Relaxed);
    rig.ch(1).amm.chair.store(true, Relaxed);
    rig.talk(0, VOICE);
    rig.talk(1, VOICE * 0.5);
    rig.run(0.5);
    assert!(rig.open(0) && rig.open(1), "the Chair opens beside the Best Mic");
}

#[test]
fn the_noise_floor_follows_the_room() {
    let mut rig = nom_rig(&[0, 1]);
    rig.amm().on_hold_s.store(1.0);
    // A steady -40 dBFS on the second mic: it opens at first, then the floor catches up with
    // it and it stops counting as a voice.
    rig.talk(1, 0.01414);
    rig.run(14.0);
    // Now it's only open as the last open mic: a talker closes it.
    rig.talk(0, VOICE);
    rig.run(0.5);
    near(rig.gain(0), 0.0, 0.01, "the talker, alone");
    near(rig.gain(1), OFF, 0.01, "the noise isn't an open mic");
}

#[test]
fn a_muted_or_off_member_does_not_count() {
    let mut rig = nom_rig(&[0, 1, 2]);
    rig.ch(1).params.mute.store(true, Relaxed);
    rig.ch(2).amm.on.store(false, Relaxed);
    rig.talk(0, VOICE);
    rig.talk(1, VOICE);
    rig.talk(2, VOICE);
    rig.run(0.5);
    near(rig.gain(0), 0.0, 0.01, "alone: the others don't count");
    assert!(!rig.open(1));
    near(rig.gain(2), 0.0, 0.01, "off: out of the AMM, at 0 dB");
}

#[test]
fn d_classic_shares_the_gain() {
    let mut rig = Rig::dlive();
    rig.join(&[0, 1, 2], 0);
    rig.amm().mode.store(amm_mode::D_CLASSIC, Relaxed);
    rig.talk(0, VOICE);
    rig.run(0.5);
    near(rig.gain(0), 0.0, 0.05, "alone");
    near(rig.gain(1), -40.0, 0.05, "silent mics at the floor");

    rig.talk(1, VOICE);
    rig.run(0.5);
    near(rig.gain(0), -3.01, 0.05, "two equal mics");
    near(rig.gain(1), -3.01, 0.05, "two equal mics");

    // One 6 dB under the other: gains in proportion, the power constant.
    rig.talk(1, VOICE * 0.5);
    rig.run(0.5);
    let (g0, g1) = (10f32.powf(rig.gain(0) / 20.0), 10f32.powf(rig.gain(1) / 20.0));
    assert!((g0 / g1 - 2.0).abs() < 0.05, "proportional to level: {g0} {g1}");
    assert!((g0 * g0 + g1 * g1 - 1.0).abs() < 0.02, "constant power: {g0} {g1}");
}

#[test]
fn d_classic_priority_offsets_the_level() {
    let mut rig = Rig::dlive();
    rig.join(&[0, 1], 0);
    rig.amm().mode.store(amm_mode::D_CLASSIC, Relaxed);
    rig.ch(0).amm.priority_db.store(6.02);
    rig.talk(0, VOICE);
    rig.talk(1, VOICE);
    rig.run(0.5);
    let want0 = 20.0 * (2.0f32 / 5.0f32.sqrt()).log10();
    let want1 = 20.0 * (1.0f32 / 5.0f32.sqrt()).log10();
    near(rig.gain(0), want0, 0.1, "+6 dB of priority");
    near(rig.gain(1), want1, 0.1, "the other");
}

#[test]
fn d_classic_has_no_chair_best_mic_or_solo() {
    let mut rig = Rig::dlive();
    rig.join(&[0, 1], 0);
    rig.amm().mode.store(amm_mode::D_CLASSIC, Relaxed);
    rig.ch(0).amm.chair.store(true, Relaxed);
    rig.ch(1).amm.solo.store(true, Relaxed);
    rig.amm().best_mic[0].store(true, Relaxed);
    rig.talk(0, VOICE);
    rig.talk(1, VOICE);
    rig.run(0.5);
    near(rig.gain(0), -3.01, 0.05, "plain gain sharing");
    near(rig.gain(1), -3.01, 0.05, "plain gain sharing");
    assert!(!rig.amm().solo[0].load(Relaxed));
}

#[test]
fn the_amms_are_independent() {
    let mut rig = Rig::dlive();
    rig.amm().count.store(2, Relaxed);
    rig.join(&[0, 1], 0);
    rig.join(&[2, 3], 1);
    rig.talk(0, VOICE);
    rig.talk(2, VOICE);
    rig.ch(1).amm.solo.store(true, Relaxed); // Solo is per AMM.
    rig.run(0.5);
    near(rig.gain(2), 0.0, 0.01, "AMM 2: alone in its AMM, whatever AMM 1 does");
    near(rig.gain(3), OFF, 0.01, "AMM 2's closed mic");
    near(rig.gain(1), 0.0, 0.01, "AMM 1's soloed mic");
    near(rig.gain(0), OFF, 0.01, "AMM 1's other mic");
    assert!(rig.amm().solo[0].load(Relaxed) && !rig.amm().solo[1].load(Relaxed));
}

#[test]
fn an_amm_holds_as_many_members_as_there_are_amms_for() {
    let mut rig = Rig::dlive();
    rig.amm().count.store(4, Relaxed);
    let many: Vec<usize> = (0..20).collect();
    rig.join(&many, 0);
    rig.talk(0, VOICE);
    rig.run(0.5);
    near(rig.gain(15), OFF, 0.01, "the 16th member");
    near(rig.gain(16), 0.0, 0.01, "the 17th isn't a member");
    assert!(!rig.open(16));
    // With fewer AMMs each has room for more.
    rig.amm().count.store(2, Relaxed);
    rig.run(0.5);
    near(rig.gain(16), OFF, 0.01, "a member now");
}

#[test]
fn an_amm_past_the_number_of_amms_is_none() {
    let mut rig = Rig::dlive();
    rig.join(&[0, 1], 3); // AMM 4, on a console with one
    rig.talk(0, VOICE);
    rig.run(0.5);
    near(rig.gain(0), 0.0, 0.01, "not a member");
    near(rig.gain(1), 0.0, 0.01, "not a member");
}

#[test]
fn the_gain_sits_after_the_fader_for_post_fade_sends_not_pre_fade_sends() {
    let mut rig = nom_rig(&[0, 1]);
    let ch = rig.ch(1);
    // Mic 2 reaches only Aux 1 (pre-fade) and Aux 2 (post-fade), the MixRack's 3 and 4.
    ch.main_send(0).unwrap().on.store(false, Relaxed);
    let send = |bus: usize, mode: i32| {
        let s = ch.sends.get_or_create(bus).unwrap();
        s.enabled.store(true, Relaxed);
        s.level_db.store(0.0);
        s.mode.store(mode, Relaxed);
    };
    send(dlive::AUX, send_mode::PRE);
    send(dlive::AUX + 1, send_mode::POST);
    rig.talk(1, VOICE);
    rig.run(0.5);
    let open = rig.run(0.1);
    assert!(open[2] > 0.1, "pre-fade");
    assert!((open[3] / open[2] - 1.0).abs() < 0.02, "open: post-fade as pre-fade, {} {}", open[3], open[2]);

    // Soloing the other mic closes this one: the post-fade send follows, the pre-fade doesn't.
    rig.ch(0).amm.solo.store(true, Relaxed);
    rig.run(0.5);
    let closed = rig.run(0.1);
    near(rig.gain(1), OFF, 0.01, "the closed mic");
    assert!((closed[2] / open[2] - 1.0).abs() < 0.02, "pre-fade unaffected: {} then {}", open[2], closed[2]);
    assert!((closed[3] / open[3] - 0.01).abs() < 0.002, "post-fade down by Off Attenuation: {} then {}", open[3], closed[3]);
}

#[test]
fn the_key_is_taken_before_insert_a() {
    let boost = |rig: &Rig, gain_db: f32| {
        let ch = rig.ch(0);
        let eq = ch.params.eq();
        let at = |id: &str| livedaw_dsp::dlive::EQ[0].params.iter().position(|p| p.id == id).unwrap();
        eq.on.store(true, Relaxed);
        eq.values[at("1f")].store(1000.0);
        eq.values[at("1g")].store(gain_db);
    };

    // The fader far down: taken after it, this mic would be too quiet to open.
    // It gets there in silence: a fader still moving would let the first block through.
    let mut rig = nom_rig(&[0, 1]);
    rig.ch(0).params.fader_db.store(-60.0);
    rig.run(0.2);
    rig.talk(0, VOICE);
    rig.run(0.5);
    near(rig.gain(0), 0.0, 0.01, "heard despite the fader");
    assert!(rig.open(0));

    // A talker just too quiet to open a mic, about -52 dBFS RMS, and 15 dB of PEQ at
    // his frequency: taken after the PEQ, the boost would open it.
    let mut rig = nom_rig(&[0, 1]);
    boost(&rig, 15.0);
    rig.run(0.2);
    rig.talk(0, 10f32.powf(-52.0 / 20.0) * std::f32::consts::SQRT_2);
    rig.run(0.3);
    assert!(!rig.open(0), "the PEQ's boost doesn't reach the key");
}

#[test]
fn a_gain_change_does_not_click() {
    let mut rig = nom_rig(&[0, 1]);
    rig.talk(0, VOICE);
    rig.run(0.5);
    let step_of = |rig: &mut Rig, seconds: f32| {
        let (mut last, mut most) = (None::<f32>, 0.0f32);
        rig.run_collecting(seconds, &mut |block| {
            for &x in block {
                if let Some(l) = last {
                    most = most.max((x - l).abs());
                }
                last = Some(x);
            }
        });
        most
    };
    let steady = step_of(&mut rig, 0.1);
    // Closing, by Solo on the other mic.
    rig.ch(1).amm.solo.store(true, Relaxed);
    let closing = step_of(&mut rig, 0.3);
    assert!(closing < steady * 1.1, "closing: {closing} against {steady}");
    near(rig.gain(0), OFF, 0.5, "closed");
    // Opening again.
    rig.ch(1).amm.solo.store(false, Relaxed);
    let opening = step_of(&mut rig, 0.3);
    assert!(opening < steady * 1.1, "opening: {opening} against {steady}");
    near(rig.gain(0), 0.0, 0.01, "open");
}

#[test]
fn leaving_the_amm_glides_back_to_0_db() {
    let mut rig = nom_rig(&[0, 1]);
    rig.talk(0, VOICE);
    rig.run(0.5);
    near(rig.gain(1), OFF, 0.01, "closed");
    rig.ch(1).amm.amm.store(-1, Relaxed);
    rig.run(0.5);
    near(rig.gain(1), 0.0, 0.001, "out of it");
}

#[test]
fn a_console_that_is_not_a_dlive_is_unaffected() {
    let wing = |amm: bool| {
        let engine = Arc::new(Engine::new(0));
        unity_console(&engine);
        set_channels(&engine, 2);
        let mut rig = Rig::on(engine);
        for i in 0..2 {
            let ch = rig.ch(i);
            ch.params.input_left.store(i as i32, Relaxed);
            ch.params.mute.store(false, Relaxed);
            ch.params.fader_db.store(0.0);
            if amm {
                ch.amm.amm.store(0, Relaxed);
                ch.amm.chair.store(i == 1, Relaxed);
            }
        }
        rig.talk(0, VOICE);
        rig.talk(1, VOICE * 0.5);
        let mut samples = Vec::new();
        rig.run_collecting(0.5, &mut |block| samples.extend_from_slice(block));
        (rig, samples)
    };
    let (_, plain) = wing(false);
    let (rig, with_amm) = wing(true);
    assert!(plain.iter().any(|x| x.abs() > 0.1), "there's signal on the LR");
    assert_eq!(plain, with_amm, "sample for sample");
    assert_eq!(rig.gain(0), 0.0);
    assert!(!rig.open(0));
}
