//! A dLive's Automatic Mic Mixer (Firmware Reference Guide, chapters MixRack Setup
//! and DEEP Processing; research notes in `docs/DLIVE_RESEARCH.md`).
//!
//! 1, 2 or 4 AMMs of input channels (up to 64 members in one, 32 each in two, 16
//! each in four), one mode for them all:
//!
//! * **NOM:** a gate with an adaptive threshold that follows the room's background
//!   noise. Each open mic comes down by NOM Attenuation per doubling of the Number
//!   Of open Mics, closed mics by Off Attenuation; the last open mic stays open
//!   until another opens. Chair, Best Mic, Solo.
//! * **D-Classic:** constant gain sharing, each mic's gain proportional to its
//!   level, offset by its Priority.
//!
//! The key is taken before Insert A, so the PEQ, compressor and fader don't affect
//! it. The gain sits after the fader, as the guide's text says (the datasheet's block
//! diagram draws it before the fader): it acts on the fader path and on post-fade
//! sends, not on pre-fade sends.
//!
//! Each channel measures its key in stage 1, in parallel ([`AmmChannel::measure`]);
//! then the engine's AMM pass ([`process`]), serial and just after automix, works out
//! the gains and applies them. Nothing here allocates or locks.
//!
//! The guide gives the controls and their ranges, not the algorithms: the detector's
//! constants below, and the rest marked so, are Sidecar's reading.

use crate::atomic::AtomicF32;
use crate::strip::Strip;
use livedaw_dsp::biquad::{Biquad, BiquadCoeffs};
use livedaw_dsp::util::{db_to_gain, gain_to_db};
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering::Relaxed};
use std::sync::Arc;

/// The most AMMs a console has.
pub const MAX_AMMS: usize = 4;
/// Members in all: 64 in one AMM, 32 each in two, 16 each in four.
pub const MAX_MEMBERS: usize = 64;

/// `AmmParams::mode`.
pub mod amm_mode {
    pub const NOM: i32 = 0;
    pub const D_CLASSIC: i32 = 1;
}

/// The guide's ranges, as (lowest, highest).
pub const OFF_ATTEN_DB: (f32, f32) = (-90.0, -10.0);
pub const ON_HOLD_S: (f32, f32) = (1.0, 5.0);
pub const NOM_ATTEN_DB: (f32, f32) = (3.0, 6.0);
pub const CHAIR_SENSITIVITY: (f32, f32) = (1.0, 10.0);
pub const CHAIR_DUCK_DB: (f32, f32) = (-40.0, -3.0);
pub const PRIORITY_DB: (f32, f32) = (-15.0, 15.0);
/// Sidecar's reading: the guide doesn't give the side chain filters' ranges.
pub const KEY_HPF_HZ: (f32, f32) = (20.0, 2000.0);
pub const KEY_LPF_HZ: (f32, f32) = (1000.0, 20000.0);

/// A second-order Butterworth side chain filter (Sidecar's reading).
const KEY_Q: f64 = std::f64::consts::FRAC_1_SQRT_2;

// ---- the detector (Sidecar's reading: the guide gives no numbers) -------------------

/// How fast the key's level follows the signal up and down.
const ATTACK_S: f32 = 0.005;
const RELEASE_S: f32 = 0.03;
/// Where a channel's noise floor starts, and how far down it can go (dBFS).
const INITIAL_FLOOR_DB: f32 = -60.0;
const LOWEST_FLOOR_DB: f32 = -85.0;
/// Below this there's no signal to learn from: the floor waits (a muted preamp,
/// a dropout), or it would have to climb all the way back afterwards.
const NO_SIGNAL_DB: f32 = -100.0;
/// The floor falls quickly to a quieter room and climbs slowly: a talker's words
/// don't teach it, the gaps between them pull it back down. A steady sound does
/// teach it, and stops counting as a voice after a while.
const FLOOR_FALL_DB_PER_S: f32 = 60.0;
const FLOOR_RISE_DB_PER_S: f32 = 1.5;
/// A mic opens this far above its noise floor, and closes this far above it.
const OPEN_MARGIN_DB: f32 = 10.0;
const CLOSE_MARGIN_DB: f32 = 6.0;
/// Nothing quieter than this opens a mic, whatever the floor (dBFS).
const QUIETEST_OPENING_DB: f32 = -70.0;
/// The Chair's On Hold is none, but a fixed short hang (Sidecar's reading) keeps
/// it, and the duck on the others, from fluttering between words.
const CHAIR_HANG_S: f32 = 0.25;

/// Chair Sensitivity 1 to 10 as (open, close) margins above the noise floor:
/// 12 dB at 1, 3 dB at 10, so 3 is an ordinary mic's (Sidecar's reading).
fn chair_margins(sensitivity: f32) -> (f32, f32) {
    let open = 13.0 - sensitivity;
    (open, (open - 4.0).max(1.0))
}

/// Best Mic: mics that triggered within this time of each other (or this far
/// below the strongest) count as hearing the same voice (Sidecar's reading:
/// a second talker starts on his own, at a level close to the first's).
const SAME_VOICE_S: f32 = 0.1;
const SAME_VOICE_BELOW_DB: f32 = 10.0;
/// The Best Mic keeps its place until another beats it by this much.
const BEST_MIC_HYSTERESIS_DB: f32 = 3.0;

/// How fast the gain itself may move (Sidecar's reading): opening at once so a word
/// isn't clipped, closing gently, D-Classic following its levels. Each block's
/// change is then ramped across the block, so it can't click.
const OPEN_DB_PER_S: f32 = 4000.0;
const CLOSE_DB_PER_S: f32 = 300.0;
const CLASSIC_DB_PER_S: f32 = 1000.0;
/// D-Classic's least gain, -40 dB: the WING's automix floor (Sidecar's reading).
const CLASSIC_LEAST_GAIN: f32 = 0.01;
/// A D-Classic AMM this quiet keeps its gains: the last talker's mic stays on,
/// as on the WING's automix.
const SILENT_POWER: f32 = 1e-12;

fn power_to_db(power: f32) -> f32 {
    if power > 1e-12 {
        10.0 * power.log10()
    } else {
        -120.0
    }
}

// ---- parameters (shared with the C++ side) ------------------------------------------

/// A channel's AMM settings and read-back (`Strip::amm`). The engine ignores them
/// on anything but a dLive's input channels.
#[repr(C)]
#[derive(Debug)]
pub struct AmmStripParams {
    /// The AMM the channel is in, from 0; -1: none.
    pub amm: AtomicI32,
    /// A member with On off is out of its AMM: it passes at 0 dB and doesn't count.
    pub on: AtomicBool,
    /// NOM: the AMM's Chair. One per AMM: the engine clears the others.
    pub chair: AtomicBool,
    /// NOM: opens this mic and turns the others down. One per AMM, as Chair.
    pub solo: AtomicBool,
    /// D-Classic: -15 to +15 dB on this channel's level in the calculation.
    pub priority_db: AtomicF32,
    /// Read-back: the gain the AMM applies now, in dB (0 for a channel outside one).
    pub gain_db: AtomicF32,
    /// Read-back, NOM: the mic is open.
    pub open: AtomicBool,
}

impl Default for AmmStripParams {
    fn default() -> Self {
        Self {
            amm: AtomicI32::new(-1),
            // Sidecar's reading: joining an AMM is the choice that counts; On is
            // what takes a member out of it again.
            on: AtomicBool::new(true),
            chair: AtomicBool::new(false),
            solo: AtomicBool::new(false),
            priority_db: AtomicF32::new(0.0),
            gain_db: AtomicF32::new(0.0),
            open: AtomicBool::new(false),
        }
    }
}

/// The console's AMM settings (`Engine::amm`). Out-of-range values are clamped
/// where they're used. Defaults: Sidecar's reading (the guide gives none).
#[repr(C)]
#[derive(Debug)]
pub struct AmmParams {
    /// `amm_mode`, for every AMM.
    pub mode: AtomicI32,
    /// 1, 2 or 4 AMMs.
    pub count: AtomicI32,
    /// -90 to -10 dB on closed mics.
    pub off_atten_db: AtomicF32,
    /// 1 to 5 seconds. Not for the Chair.
    pub on_hold_s: AtomicF32,
    /// 3 to 6 dB on each open mic per doubling of the open mics.
    pub nom_atten_db: AtomicF32,
    /// 1 to 10.
    pub chair_sensitivity: AtomicF32,
    /// -40 to -3 dB on the others while the Chair is open.
    pub chair_duck_db: AtomicF32,
    /// The key's filters, for both modes.
    pub key_hpf_hz: AtomicF32,
    pub key_lpf_hz: AtomicF32,
    /// Each AMM's Best Mic On (NOM).
    pub best_mic: [AtomicBool; MAX_AMMS],
    /// Each AMM's Solo, kept by the engine: on while one of its channels is soloed.
    /// Clearing it releases that channel.
    pub solo: [AtomicBool; MAX_AMMS],
}

impl Default for AmmParams {
    fn default() -> Self {
        Self {
            mode: AtomicI32::new(amm_mode::NOM),
            count: AtomicI32::new(1),
            off_atten_db: AtomicF32::new(-40.0),
            on_hold_s: AtomicF32::new(2.0),
            nom_atten_db: AtomicF32::new(3.0),
            chair_sensitivity: AtomicF32::new(5.0),
            chair_duck_db: AtomicF32::new(-20.0),
            key_hpf_hz: AtomicF32::new(100.0),
            key_lpf_hz: AtomicF32::new(10000.0),
            best_mic: Default::default(),
            solo: Default::default(),
        }
    }
}

impl AmmParams {
    /// Back to the defaults (a new console).
    pub fn reset(&self) {
        let d = Self::default();
        self.mode.store(d.mode.load(Relaxed), Relaxed);
        self.count.store(d.count.load(Relaxed), Relaxed);
        self.off_atten_db.store(d.off_atten_db.load());
        self.on_hold_s.store(d.on_hold_s.load());
        self.nom_atten_db.store(d.nom_atten_db.load());
        self.chair_sensitivity.store(d.chair_sensitivity.load());
        self.chair_duck_db.store(d.chair_duck_db.load());
        self.key_hpf_hz.store(d.key_hpf_hz.load());
        self.key_lpf_hz.store(d.key_lpf_hz.load());
        for flag in self.best_mic.iter().chain(&self.solo) {
            flag.store(false, Relaxed);
        }
    }

    /// The number of AMMs: 1, 2 or 4.
    pub fn amms(&self) -> usize {
        match self.count.load(Relaxed) {
            2 => 2,
            4 => 4,
            _ => 1,
        }
    }

    fn key_filters_hz(&self) -> (f32, f32) {
        (self.key_hpf_hz.load().clamp(KEY_HPF_HZ.0, KEY_HPF_HZ.1), self.key_lpf_hz.load().clamp(KEY_LPF_HZ.0, KEY_LPF_HZ.1))
    }

    fn settings(&self) -> Settings {
        Settings {
            off_db: self.off_atten_db.load().clamp(OFF_ATTEN_DB.0, OFF_ATTEN_DB.1),
            hold_s: self.on_hold_s.load().clamp(ON_HOLD_S.0, ON_HOLD_S.1),
            nom_db: self.nom_atten_db.load().clamp(NOM_ATTEN_DB.0, NOM_ATTEN_DB.1),
            sensitivity: self.chair_sensitivity.load().clamp(CHAIR_SENSITIVITY.0, CHAIR_SENSITIVITY.1),
            duck_db: self.chair_duck_db.load().clamp(CHAIR_DUCK_DB.0, CHAIR_DUCK_DB.1),
        }
    }
}

/// This block's NOM settings, in range.
struct Settings {
    off_db: f32,
    hold_s: f32,
    nom_db: f32,
    sensitivity: f32,
    duck_db: f32,
}

// ---- audio thread -------------------------------------------------------------------

/// A channel's AMM state: its task owns it in stage 1, the AMM pass after that.
#[derive(Clone)]
pub(crate) struct AmmChannel {
    hpf: Biquad,
    lpf: Biquad,
    /// The frequencies the filters are set to (0: not set yet).
    filter_hz: (f32, f32),
    /// The block's key power (mean square), from stage 1; 0 unless the channel
    /// is an On member.
    power: f32,
    envelope: f32,
    level_db: f32,
    floor_db: f32,
    /// The AMM it is in this block, -1: none.
    zone: i8,
    /// Muted or not running: it can't open.
    absent: bool,
    /// Above its threshold now.
    triggered: bool,
    /// Its trigger counts this block (Best Mic may refuse it).
    fired: bool,
    /// Seconds since it triggered.
    triggered_for_s: f32,
    hold_s: f32,
    open: bool,
    /// D-Classic: its level after Priority, as a power.
    weight: f32,
    target_db: f32,
    gain_db: f32,
    /// The linear gain last applied (where the next block's ramp starts).
    pub gain: f32,
    chair_was: bool,
    solo_was: bool,
}

impl Default for AmmChannel {
    fn default() -> Self {
        Self {
            hpf: Biquad::default(),
            lpf: Biquad::default(),
            filter_hz: (0.0, 0.0),
            power: 0.0,
            envelope: 0.0,
            level_db: -120.0,
            floor_db: INITIAL_FLOOR_DB,
            zone: -1,
            absent: true,
            triggered: false,
            fired: false,
            triggered_for_s: 0.0,
            hold_s: 0.0,
            open: false,
            weight: 0.0,
            target_db: 0.0,
            gain_db: 0.0,
            gain: 1.0,
            chair_was: false,
            solo_was: false,
        }
    }
}

impl AmmChannel {
    pub(crate) fn clear_key(&mut self) {
        self.power = 0.0;
    }

    pub(crate) fn reset_filters(&mut self) {
        self.hpf.reset();
        self.lpf.reset();
    }

    /// Stage 1: the block's power of `key`, the channel's signal before Insert A
    /// (mono; a scratch buffer, filtered in place).
    pub(crate) fn measure(&mut self, key: &mut [f32], params: &AmmParams, sample_rate: f64) {
        let hz = params.key_filters_hz();
        if hz != self.filter_hz {
            let top = (sample_rate * 0.45) as f32;
            self.hpf.set_coefficients(BiquadCoeffs::high_pass(sample_rate, f64::from(hz.0.min(top)), KEY_Q));
            self.lpf.set_coefficients(BiquadCoeffs::low_pass(sample_rate, f64::from(hz.1.min(top)), KEY_Q));
            self.filter_hz = hz;
        }
        self.hpf.process(key, 0);
        self.lpf.process(key, 0);
        self.power = key.iter().map(|x| x * x).sum::<f32>() / key.len().max(1) as f32;
        if !self.power.is_finite() {
            // Not a number on the channel (its strip silences itself after the chain).
            self.power = 0.0;
            self.reset_filters();
        }
    }

    /// Follows the key: its level, the noise floor and the trigger, with the
    /// `margins` (open, close) above the floor.
    fn follow(&mut self, margins: (f32, f32), dt: f32) {
        let power = if self.absent { 0.0 } else { self.power };
        let tau = if power > self.envelope { ATTACK_S } else { RELEASE_S };
        self.envelope = power + (self.envelope - power) * (-dt / tau).exp();
        let level = power_to_db(self.envelope);
        self.level_db = level;
        if level > NO_SIGNAL_DB {
            self.floor_db = if level < self.floor_db {
                (self.floor_db - FLOOR_FALL_DB_PER_S * dt).max(level).max(LOWEST_FLOOR_DB)
            } else {
                (self.floor_db + FLOOR_RISE_DB_PER_S * dt).min(level)
            };
        }
        let margin = if self.triggered { margins.1 } else { margins.0 };
        let triggered = level > (self.floor_db + margin).max(QUIETEST_OPENING_DB);
        self.triggered_for_s = if triggered && self.triggered { self.triggered_for_s + dt } else { 0.0 };
        self.triggered = triggered;
    }

    /// Moves the gain towards the target, `up` or `down` dB per second, over `dt`.
    fn slew(&mut self, up: f32, down: f32, dt: f32) {
        let difference = self.target_db - self.gain_db;
        let step = (if difference > 0.0 { up } else { down }) * dt;
        self.gain_db += difference.clamp(-step, step);
    }
}

/// An AMM's state from block to block. The channels are named by their strip ids
/// (a channel keeps its id when the console's channels change), -1: none.
#[derive(Clone, Copy)]
struct Zone {
    last_open: i32,
    chair: i32,
    soloist: i32,
    best: i32,
    solo_engaged: bool,
}

impl Default for Zone {
    fn default() -> Self {
        Self { last_open: -1, chair: -1, soloist: -1, best: -1, solo_engaged: false }
    }
}

/// The AMMs' state (audio thread only).
#[derive(Default)]
pub(crate) struct AmmState {
    zones: [Zone; MAX_AMMS],
}

/// # Safety
/// The AMM pass: audio thread, after the channels ran, before anything reads them.
#[allow(clippy::mut_from_ref)]
unsafe fn channel(s: &Strip) -> &mut AmmChannel {
    &mut s.dsp_mut().amm
}

/// The members of AMM `zone`.
fn members(channels: &[Arc<Strip>], zone: usize) -> impl Iterator<Item = &Arc<Strip>> + Clone {
    // SAFETY: the AMM pass, for all its callers.
    channels.iter().filter(move |s| unsafe { channel(s) }.zone == zone as i8)
}

/// One flag per AMM (Chair, Solo): the channel holding it now (-1: none). The
/// newest press wins (the lowest-numbered of several in the same block) and the
/// engine clears the others, so the screens need only set a channel's flag.
///
/// # Safety
/// The AMM pass.
unsafe fn exclusive<'a>(
    of: impl Iterator<Item = &'a Arc<Strip>> + Clone,
    holder: i32,
    flag: impl Fn(&Strip) -> &AtomicBool,
    was: impl Fn(&mut AmmChannel) -> &mut bool,
) -> i32 {
    let (mut pressed, mut first, mut keeps) = (-1, -1, false);
    for s in of.clone() {
        let on = flag(s).load(Relaxed);
        let before = was(channel(s));
        if on {
            if !*before && pressed < 0 {
                pressed = s.id();
            }
            if first < 0 {
                first = s.id();
            }
            keeps |= s.id() == holder;
        }
        *before = on;
    }
    let winner = if pressed >= 0 {
        pressed
    } else if keeps {
        holder
    } else {
        first
    };
    for s in of {
        if s.id() != winner && flag(s).swap(false, Relaxed) {
            *was(channel(s)) = false;
        }
    }
    winner
}

/// Best Mic On: of the mics triggered by the same voice only the strongest
/// counts, Chair apart.
///
/// # Safety
/// The AMM pass.
unsafe fn best_mic(channels: &[Arc<Strip>], z: usize, st: &mut Zone, chair: i32) {
    let score = |s: &Arc<Strip>| channel(s).level_db + if s.id() == st.best { BEST_MIC_HYSTERESIS_DB } else { 0.0 };
    let best = members(channels, z)
        .filter(|s| channel(s).triggered && s.id() != chair)
        .max_by(|a, b| score(a).total_cmp(&score(b)))
        .cloned();
    st.best = best.as_ref().map_or(-1, |s| s.id());
    let Some(best) = best else { return };
    let (level, age) = (channel(&best).level_db, channel(&best).triggered_for_s);
    for s in members(channels, z) {
        if s.id() == st.best || s.id() == chair {
            continue;
        }
        let a = channel(s);
        if a.triggered && ((a.triggered_for_s - age).abs() < SAME_VOICE_S || a.level_db < level - SAME_VOICE_BELOW_DB) {
            a.fired = false;
        }
    }
}

/// The NOM mode of one AMM: each member's `target_db`.
///
/// # Safety
/// The AMM pass.
unsafe fn nom(params: &AmmParams, set: &Settings, st: &mut Zone, channels: &[Arc<Strip>], z: usize, dt: f32) {
    let chair = exclusive(members(channels, z), st.chair, |s| &s.amm.chair, |a| &mut a.chair_was);
    let mut soloist = exclusive(members(channels, z), st.soloist, |s| &s.amm.solo, |a| &mut a.solo_was);
    // Clearing the AMM's Solo releases its soloed channel.
    if soloist >= 0 && st.solo_engaged && !params.solo[z].load(Relaxed) {
        for s in members(channels, z) {
            s.amm.solo.store(false, Relaxed);
            channel(s).solo_was = false;
        }
        soloist = -1;
    }
    st.chair = chair;
    st.soloist = soloist;
    st.solo_engaged = soloist >= 0;
    params.solo[z].store(st.solo_engaged, Relaxed);

    for s in members(channels, z) {
        let a = channel(s);
        a.follow(if s.id() == chair { chair_margins(set.sensitivity) } else { (OPEN_MARGIN_DB, CLOSE_MARGIN_DB) }, dt);
        a.fired = a.triggered;
    }
    if params.best_mic[z].load(Relaxed) {
        best_mic(channels, z, st, chair);
    } else {
        st.best = -1;
    }
    for s in members(channels, z) {
        let a = channel(s);
        a.hold_s = if a.absent {
            0.0
        } else if a.fired {
            if s.id() == chair {
                CHAIR_HANG_S
            } else {
                set.hold_s
            }
        } else {
            (a.hold_s - dt).max(0.0)
        };
        a.open = !a.absent && (a.fired || a.hold_s > 0.0);
    }
    if soloist >= 0 {
        for s in members(channels, z) {
            channel(s).open = s.id() == soloist;
        }
    } else if members(channels, z).any(|s| channel(s).open) {
        // The last open mic: the one left when the others have closed. Until
        // then it's the one already marked, or the strongest that's open.
        if !members(channels, z).any(|s| s.id() == st.last_open && channel(s).open) {
            st.last_open = members(channels, z)
                .filter(|s| channel(s).open)
                .max_by(|a, b| channel(a).level_db.total_cmp(&channel(b).level_db))
                .map_or(-1, |s| s.id());
        }
    } else if let Some(s) = members(channels, z).find(|s| s.id() == st.last_open && !channel(s).absent) {
        // The ambience: nothing else is open, it stays until another opens.
        // Sidecar's reading: nothing is open until a mic first triggers.
        channel(s).open = true;
    }

    let open_mics = members(channels, z).filter(|s| channel(s).open).count().max(1);
    let open_db = -set.nom_db * (open_mics as f32).log2();
    let duck = soloist < 0 && members(channels, z).any(|s| s.id() == chair && channel(s).open);
    for s in members(channels, z) {
        let a = channel(s);
        a.target_db = if a.open { open_db } else { set.off_db };
        // Sidecar's reading: on every other member, open or closed.
        if duck && s.id() != chair {
            a.target_db += set.duck_db;
        }
    }
}

/// The D-Classic mode of one AMM: each member's `target_db`. The gains share a
/// constant total power, each proportional to its member's level after Priority.
///
/// # Safety
/// The AMM pass.
unsafe fn d_classic(channels: &[Arc<Strip>], z: usize, dt: f32) {
    let mut total = 0.0f32;
    for s in members(channels, z) {
        let a = channel(s);
        a.follow((OPEN_MARGIN_DB, CLOSE_MARGIN_DB), dt);
        a.open = false;
        let priority = s.amm.priority_db.load().clamp(PRIORITY_DB.0, PRIORITY_DB.1);
        a.weight = if a.absent { 0.0 } else { a.envelope * db_to_gain(priority * 2.0) };
        total += a.weight;
    }
    for s in members(channels, z) {
        let a = channel(s);
        if total > SILENT_POWER {
            a.target_db = gain_to_db((a.weight / total).sqrt().max(CLASSIC_LEAST_GAIN));
        } else {
            a.target_db = a.gain_db;
        }
    }
}

/// The AMM pass, for one block: every member's gain worked out and applied to
/// what follows its fader. Channels outside an AMM glide back to 0 dB. Does nothing
/// on a console that isn't a dLive.
///
/// # Safety
/// Audio thread, after the input stage, before anything reads the channels.
pub(crate) unsafe fn process(params: &AmmParams, state: &mut AmmState, channels: &[Arc<Strip>], sample_rate: f64, n: usize) {
    if !channels.first().is_some_and(|s| s.has_amm()) {
        return;
    }
    let dt = n as f32 / sample_rate as f32;
    let amms = params.amms();
    let capacity = MAX_MEMBERS / amms;
    let mut members_of = [0usize; MAX_AMMS];
    for s in channels {
        let d = s.dsp_mut();
        // A channel past its AMM's capacity stays out of it (Sidecar's reading:
        // the first ones in channel order are the members).
        let zone = s.amm_zone(amms).filter(|&z| {
            members_of[z] += 1;
            members_of[z] <= capacity
        });
        d.amm.zone = zone.map_or(-1, |z| z as i8);
        // Sidecar's reading: a muted mic is out of the calculation, as a mic that's
        // off (it mustn't turn the others down), though its key is taken before the mute.
        d.amm.absent = d.idle() || d.muted();
    }
    let classic = params.mode.load(Relaxed) == amm_mode::D_CLASSIC;
    let set = params.settings();
    for z in 0..MAX_AMMS {
        if classic || z >= amms {
            state.zones[z] = Zone::default();
            params.solo[z].store(false, Relaxed);
        }
        if z < amms {
            if classic {
                d_classic(channels, z, dt);
            } else {
                nom(params, &set, &mut state.zones[z], channels, z, dt);
            }
        }
    }
    let (up, down) = if classic { (CLASSIC_DB_PER_S, CLASSIC_DB_PER_S) } else { (OPEN_DB_PER_S, CLOSE_DB_PER_S) };
    for s in channels {
        let a = channel(s);
        if a.zone < 0 {
            a.target_db = 0.0;
            a.open = false;
        }
        a.slew(up, down, dt);
        let gain = db_to_gain(a.gain_db);
        if gain != 1.0 || a.gain != 1.0 {
            s.apply_amm(gain, n);
        }
        s.amm.gain_db.store(a.gain_db);
        s.amm.open.store(a.open, Relaxed);
    }
}
