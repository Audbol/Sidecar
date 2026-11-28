//! The DM2000's Automix (V2 Owner's Manual chapter 17, pp.193-200): the Input
//! Channels' faders and [ON] keys recorded against a running clock and played
//! back. The app's AUTOMIX page and keys drive one; it is pure, so the host
//! hands it the time and the faders and ONs, and takes back what playback set.
//!
//! - REC punches every channel in for the pass; with TOUCH SENSE only the
//!   faders held (TOUCH), or touched once in the pass (LATCH: no punch out).
//! - A punched-in fader's take replaces the existing data as the pass reaches
//!   it (ABSOLUTE), or shifts it by as much as the fader has been moved
//!   (RELATIVE, in dB; Sidecar's reading of p.196's diagram).
//! - At a punch out the fader stays where it is until the existing data's next
//!   move (Edit Out OFF), glides back to the existing data over the RETURN time
//!   (RETURN), or goes on recording until the existing data meets it (TAKEOVER).
//!   A punch out is a fader let go, or REC turned off while running.
//! - AUTO-REC is REC that stays on: every run records (p.197).
//! - Events past the point a pass stops stay (UPDATE TO END off).

use livedaw_dsp::fader::{FADER_MAX, FADER_MIN};

/// The Input Channels a fader can be held on.
pub const CHANNELS: usize = 96;
/// RETURN's TIME (p.200): 0.0 to 30.0 seconds, in 0.1 second steps.
pub const RETURN_TIME_MAX: f32 = 30.0;
/// A fader moved by less than this (dB) hasn't moved.
const MOVED_DB: f32 = 0.05;

/// The page's and the surface's keys, by the host's numbers.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(i32)]
pub enum Key {
    Enable,
    Rec,
    Run,
    Clear,
    Undo,
    /// SCENE CLOCK: a scene recall starts the clock again.
    Clock,
    /// OVERWRITE FADER.
    Fader,
    /// OVERWRITE ON.
    On,
    AutoRec,
    /// The page's TOUCH: on (TOUCH SENSE by touch) or off.
    Touch,
    /// The page's LATCH: on (TOUCH SENSE by latch) or off.
    Latch,
    /// The surface's [TOUCH SENSE]: TOUCH or LATCH, whichever was chosen last, on or off.
    TouchSense,
    /// [RELATIVE] and the page's RELATIVE: the Fader Edit mode.
    Relative,
    /// The page's ABSOLUTE.
    Absolute,
    /// [RETURN] and the page's RETURN: the Edit Out mode RETURN, or OFF.
    Return,
    /// The page's TAKEOVER: the Edit Out mode TAKEOVER, or OFF.
    Takeover,
}

impl Key {
    pub const ALL: [Key; 16] = [
        Key::Enable,
        Key::Rec,
        Key::Run,
        Key::Clear,
        Key::Undo,
        Key::Clock,
        Key::Fader,
        Key::On,
        Key::AutoRec,
        Key::Touch,
        Key::Latch,
        Key::TouchSense,
        Key::Relative,
        Key::Absolute,
        Key::Return,
        Key::Takeover,
    ];

    pub fn from_i32(k: i32) -> Option<Key> {
        usize::try_from(k).ok().and_then(|k| Self::ALL.get(k).copied())
    }
}

/// TOUCH SENSE (p.196).
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum TouchSense {
    #[default]
    Off,
    /// A held fader is punched in, and out when let go.
    Touch,
    /// A touched fader is punched in for the rest of the pass.
    Latch,
}

/// EDIT OUT (p.195): what a fader does at its punch out.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum EditOut {
    #[default]
    Off,
    Takeover,
    Return,
}

/// One recorded move: Input Channel `strip`'s fader (dB) or ON (1 on, 0 off) at `t` seconds.
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct Move {
    pub t: f64,
    pub strip: usize,
    pub on: bool,
    pub value: f32,
}

#[derive(Clone, Copy, PartialEq, Debug, Default)]
enum Tail {
    #[default]
    None,
    /// Gliding from `from` back to the existing data, since `at`.
    Return { from: f32, at: f64 },
    /// Recording until the existing data, now below the fader (`above`) or over it, meets it.
    Takeover { above: bool },
}

#[derive(Clone, Copy, Debug)]
struct Channel {
    /// The fader and ON as last seen or played.
    fader: f32,
    on: bool,
    /// The existing data's fader value now.
    base: f32,
    /// RELATIVE: the fader's move from the existing data.
    offset: f32,
    punched: bool,
    latched: bool,
    tail: Tail,
}

impl Channel {
    fn new(fader: f32, on: bool) -> Self {
        Self { fader, on, base: fader, offset: 0.0, punched: false, latched: false, tail: Tail::None }
    }
}

/// One Automix and its undo buffer.
#[derive(Clone, Debug)]
pub struct Automix {
    pub enable: bool,
    pub scene_clock: bool,
    /// OVERWRITE FADER and ON: what a pass records.
    pub faders: bool,
    pub ons: bool,
    rec: bool,
    run: bool,
    auto_rec: bool,
    touch: TouchSense,
    /// What [TOUCH SENSE] turns on: the page's TOUCH or LATCH, as chosen last.
    touch_choice: TouchSense,
    relative: bool,
    edit_out: EditOut,
    return_time: f32,
    moves: Vec<Move>,
    undo: Vec<Move>,
    start: f64,
    now: f64,
    /// The first move still to play.
    next: usize,
    channels: Vec<Channel>,
    held: [bool; CHANNELS],
    /// A run started: the next tick takes the faders as they are.
    snapshot: bool,
}

impl Default for Automix {
    fn default() -> Self {
        Self {
            enable: true,
            scene_clock: false,
            faders: true,
            ons: true,
            rec: false,
            run: false,
            auto_rec: false,
            touch: TouchSense::Off,
            touch_choice: TouchSense::Touch,
            relative: false,
            edit_out: EditOut::Off,
            // Sidecar's choice: the manual gives the range, not the initial value.
            return_time: 1.0,
            moves: Vec::new(),
            undo: Vec::new(),
            start: 0.0,
            now: 0.0,
            next: 0,
            channels: Vec::new(),
            held: [false; CHANNELS],
            snapshot: false,
        }
    }
}

/// `value` moved by `offset` dB; -oo stays.
fn shifted(value: f32, offset: f32) -> f32 {
    if value <= FADER_MIN {
        value
    } else {
        (value + offset).clamp(FADER_MIN, FADER_MAX)
    }
}

impl Automix {
    pub fn touch_sense(&self) -> TouchSense {
        self.touch
    }

    pub fn edit_out(&self) -> EditOut {
        self.edit_out
    }

    pub fn is_relative(&self) -> bool {
        self.relative
    }

    pub fn return_time(&self) -> f32 {
        self.return_time
    }

    pub fn set_return_time(&mut self, seconds: f32) {
        if seconds.is_finite() {
            self.return_time = ((seconds.clamp(0.0, RETURN_TIME_MAX) * 10.0).round() / 10.0).clamp(0.0, RETURN_TIME_MAX);
        }
    }

    /// The Automix's moves, in time order.
    pub fn moves(&self) -> &[Move] {
        &self.moves
    }

    /// The clock: seconds since the run started.
    pub fn time(&self) -> f64 {
        self.now
    }

    /// Whether `key` is lit.
    pub fn is(&self, key: Key) -> bool {
        match key {
            Key::Enable => self.enable,
            Key::Rec => self.rec,
            Key::Run => self.run,
            Key::Clock => self.scene_clock,
            Key::Fader => self.faders,
            Key::On => self.ons,
            Key::AutoRec => self.auto_rec,
            Key::Touch => self.touch == TouchSense::Touch,
            Key::Latch => self.touch == TouchSense::Latch,
            Key::TouchSense => self.touch != TouchSense::Off,
            Key::Relative => self.relative,
            Key::Absolute => !self.relative,
            Key::Return => self.edit_out == EditOut::Return,
            Key::Takeover => self.edit_out == EditOut::Takeover,
            Key::Clear | Key::Undo => false,
        }
    }

    /// `key` pressed at `now` (seconds, any steady clock).
    pub fn press(&mut self, key: Key, now: f64) {
        match key {
            Key::Enable => self.enable = !self.enable,
            Key::Fader => self.faders = !self.faders,
            Key::On => self.ons = !self.ons,
            Key::Clock => self.scene_clock = !self.scene_clock,
            Key::Clear => {
                self.moves.clear();
                self.next = 0;
            }
            Key::Undo => {
                // The take before the last one back; the one undone goes to the buffer (p.197).
                std::mem::swap(&mut self.moves, &mut self.undo);
                self.next = self.moves.partition_point(|m| m.t <= self.now);
            }
            Key::AutoRec => {
                // REC that stays on: on now, records this run (and every run after it).
                self.auto_rec = !self.auto_rec;
                if self.auto_rec != self.rec {
                    self.press(Key::Rec, now);
                }
            }
            Key::Touch | Key::Latch => {
                let mode = if key == Key::Touch { TouchSense::Touch } else { TouchSense::Latch };
                self.touch = if self.touch == mode { TouchSense::Off } else { mode };
                self.touch_choice = mode;
            }
            Key::TouchSense => {
                self.touch = if self.touch == TouchSense::Off { self.touch_choice } else { TouchSense::Off };
            }
            Key::Relative => self.relative = !self.relative,
            Key::Absolute => self.relative = false,
            Key::Return | Key::Takeover => {
                let mode = if key == Key::Return { EditOut::Return } else { EditOut::Takeover };
                self.edit_out = if self.edit_out == mode { EditOut::Off } else { mode };
            }
            Key::Rec => {
                self.rec = !self.rec;
                if self.rec {
                    // UNDO brings back what this pass replaces.
                    self.undo = self.moves.clone();
                    for c in &mut self.channels {
                        c.latched = false;
                    }
                    if !self.run {
                        self.press(Key::Run, now);
                    }
                } else {
                    self.auto_rec = false;
                }
            }
            Key::Run => {
                self.run = !self.run;
                if self.run {
                    self.start = now;
                    self.now = 0.0;
                    self.next = 0;
                    self.snapshot = true;
                    // AUTO-REC: every run records.
                    if self.auto_rec && !self.rec {
                        self.press(Key::Rec, now);
                    }
                } else {
                    self.rec = false; // AUTO-REC stays on for the next run
                    for c in &mut self.channels {
                        c.punched = false;
                        c.latched = false;
                        c.tail = Tail::None;
                    }
                }
            }
        }
    }

    /// Input Channel `channel`'s (0-95) fader knob held or let go.
    pub fn hold(&mut self, channel: usize, held: bool) {
        if let Some(h) = self.held.get_mut(channel) {
            *h = held;
        }
    }

    /// A scene was recalled at `now`: with SCENE CLOCK the clock starts again.
    pub fn scene_recalled(&mut self, now: f64) {
        if self.scene_clock && self.run {
            self.start = now;
            self.now = 0.0;
            self.next = 0;
        }
    }

    /// Runs the clock to `now`: plays what is due into `faders` (dB) and `ons`
    /// (the Input Channels in order) and records what the punched-in ones do.
    /// True when it set any of them.
    pub fn tick(&mut self, now: f64, faders: &mut [f32], ons: &mut [bool]) -> bool {
        if !self.run {
            return false;
        }
        self.now = now - self.start;
        let n = faders.len().min(ons.len());
        if self.snapshot || self.channels.len() != n {
            self.channels = (0..n).map(|i| Channel::new(faders[i], ons[i])).collect();
            self.snapshot = false;
        }
        self.punch(n);
        let mut taken = Vec::new();
        let moved = self.touched(n, faders, ons, &mut taken);
        let mut wrote = self.play(n, faders, ons);
        // A punched-in fader's move, where it ends up (RELATIVE: with the existing data's).
        for i in (0..n).filter(|&i| moved[i]) {
            taken.push(Move { t: self.now, strip: i, on: false, value: faders[i] });
        }
        wrote |= self.tails(n, faders, ons, &mut taken);
        // Recorded before what is still to play.
        for m in taken {
            self.moves.insert(self.next, m);
            self.next += 1;
        }
        wrote
    }

    /// Who is punched in now; a punch out starts the Edit Out's tail.
    fn punch(&mut self, n: usize) {
        for i in 0..n {
            let held = self.held.get(i).copied().unwrap_or(false);
            let c = &mut self.channels[i];
            let punched = self.rec
                && match self.touch {
                    TouchSense::Off => true,
                    TouchSense::Touch => held,
                    TouchSense::Latch => {
                        c.latched |= held;
                        c.latched
                    }
                };
            if punched && !c.punched {
                c.offset = 0.0;
                c.tail = Tail::None;
            } else if !punched && c.punched && self.faders {
                c.tail = match self.edit_out {
                    EditOut::Off => Tail::None,
                    EditOut::Return => Tail::Return { from: c.fader, at: self.now },
                    EditOut::Takeover if (c.fader - c.base).abs() <= MOVED_DB => Tail::None,
                    EditOut::Takeover => Tail::Takeover { above: c.fader > c.base },
                };
            }
            c.punched = punched;
        }
    }

    /// The existing moves now due: played, or replaced where a fader is punched in or in its tail.
    fn play(&mut self, n: usize, faders: &mut [f32], ons: &mut [bool]) -> bool {
        let mut wrote = false;
        while let Some(&m) = self.moves.get(self.next).filter(|m| m.t <= self.now) {
            if m.strip >= n {
                self.next += 1;
                continue;
            }
            let c = &mut self.channels[m.strip];
            if m.on {
                if c.punched && self.ons {
                    self.moves.remove(self.next);
                    continue;
                }
                self.next += 1;
                if self.enable {
                    c.on = m.value >= 0.5;
                    ons[m.strip] = c.on;
                    wrote = true;
                }
                continue;
            }
            let existing = m.value;
            if c.punched && self.faders {
                c.base = existing;
                if self.relative {
                    // RELATIVE: the existing move, shifted by the fader's.
                    let v = shifted(existing, c.offset);
                    self.moves[self.next].value = v;
                    self.next += 1;
                    c.fader = v;
                    faders[m.strip] = v;
                    wrote = true;
                } else {
                    self.moves.remove(self.next);
                }
                continue;
            }
            match c.tail {
                Tail::Return { .. } => {
                    c.base = existing;
                    self.moves.remove(self.next);
                    continue;
                }
                Tail::Takeover { above } if (c.fader > existing) == above && (c.fader - existing).abs() > MOVED_DB => {
                    c.base = existing;
                    self.moves.remove(self.next);
                    continue;
                }
                // TAKEOVER: the existing data met the fader; it plays again.
                Tail::Takeover { .. } => c.tail = Tail::None,
                Tail::None => {}
            }
            c.base = existing;
            self.next += 1;
            if self.enable {
                c.fader = existing;
                faders[m.strip] = existing;
                wrote = true;
            }
        }
        wrote
    }

    /// What the punched-in channels did since the last tick: their ON moves are taken, and
    /// which faders moved is answered (RELATIVE: the move is the new offset).
    fn touched(&mut self, n: usize, faders: &[f32], ons: &[bool], taken: &mut Vec<Move>) -> Vec<bool> {
        let mut moved = vec![false; n];
        for i in 0..n {
            let c = &mut self.channels[i];
            if !c.punched {
                continue;
            }
            if (faders[i] - c.fader).abs() > MOVED_DB || !self.faders {
                moved[i] = self.faders;
                if self.faders && self.relative {
                    c.offset = faders[i] - c.base;
                }
                c.fader = faders[i];
            }
            if self.ons && ons[i] != c.on {
                taken.push(Move { t: self.now, strip: i, on: true, value: if ons[i] { 1.0 } else { 0.0 } });
            }
            c.on = ons[i];
        }
        moved
    }

    /// The faders punched out and still in their Edit Out's tail, and the channels not recorded.
    fn tails(&mut self, n: usize, faders: &mut [f32], ons: &[bool], taken: &mut Vec<Move>) -> bool {
        let mut wrote = false;
        for i in 0..n {
            let c = &mut self.channels[i];
            if c.punched {
                continue;
            }
            let moved = (faders[i] - c.fader).abs() > MOVED_DB;
            match c.tail {
                Tail::Return { from, at } => {
                    let k = if self.return_time <= 0.0 {
                        1.0
                    } else {
                        ((self.now - at) / self.return_time as f64).clamp(0.0, 1.0) as f32
                    };
                    let v = if k >= 1.0 { c.base } else { from + (c.base - from) * k };
                    if (v - c.fader).abs() > MOVED_DB || (k >= 1.0 && v != c.fader) {
                        taken.push(Move { t: self.now, strip: i, on: false, value: v });
                        c.fader = v;
                        faders[i] = v;
                        wrote = true;
                    }
                    if k >= 1.0 {
                        c.tail = Tail::None;
                    }
                }
                Tail::Takeover { above } => {
                    // Moved now, the fader still records; meeting the existing data ends it.
                    if moved {
                        taken.push(Move { t: self.now, strip: i, on: false, value: faders[i] });
                        c.fader = faders[i];
                    }
                    if (c.fader > c.base) != above || (c.fader - c.base).abs() <= MOVED_DB {
                        c.tail = Tail::None;
                    }
                }
                Tail::None => {
                    // Not recorded: what the fader does is only noted.
                    c.fader = faders[i];
                }
            }
            c.on = ons[i];
        }
        wrote
    }
}

#[cfg(test)]
mod tests;
