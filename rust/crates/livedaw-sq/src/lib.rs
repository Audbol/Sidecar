//! The Allen & Heath SQ-5's MIDI protocol over the engine (`docs/UNIVERSAL_MIXER.md`),
//! from the SQ MIDI Protocol for firmware V1.6: NRPN mutes, levels, pans and
//! assignments in either fader law, and scene recall by bank and program change.
//! Clean-room: built from that public document (its parameter tables are
//! pictures; [`decode`] holds their formulas), not from the console's firmware.
//!
//! The twelve mixes are Auxes, as a new SQ's are, so the protocol's Group
//! addresses answer nothing yet. A stereo matrix is Mtx1-3. Not here yet: the
//! Soft Keys' notes, the DAW strips on the next MIDI channel, MIDI Show Control
//! and MIDI over TCP/IP.

use livedaw_dsp::fader::{FaderLaw, FADER_MIN};
use livedaw_engine::{Engine, Profile, Strip, StripKind};
use std::collections::VecDeque;
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

const INPUTS: usize = 48;
const MIXES: usize = 12;
const FX_RETURNS: usize = 8;
const FX_SENDS: usize = 4;
const MATRICES: usize = 3;

/// Something with a mute or a master level.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Channel {
    Input(usize),
    FxReturn(usize),
    Lr,
    Aux(usize),
    FxSend(usize),
    Matrix(usize),
    Dca(usize),
    MuteGroup(usize),
}

/// Where a send starts.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Source {
    Input(usize),
    FxReturn(usize),
    Lr,
    Aux(usize),
}

/// Where a send arrives.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Dest {
    Lr,
    Aux(usize),
    FxSend(usize),
    Matrix(usize),
}

/// What a parameter number addresses.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Param {
    Mute(Channel),
    Master(Channel),
    Level(Source, Dest),
    Pan(Source, Dest),
    Assign(Source, Dest),
}

/// The send a level address names (its pan is 0x800 on, its assignment 0x1000 on).
fn send(a: u16) -> Option<(Source, Dest)> {
    let a = a as usize;
    let pair = |base: usize, width: usize, count: usize| {
        let v = a - base;
        (v / width < count).then_some((v / width, v % width))
    };
    Some(match a {
        0x2000..=0x202F => (Source::Input(a - 0x2000), Dest::Lr),
        0x203C..=0x2043 => (Source::FxReturn(a - 0x203C), Dest::Lr),
        0x2044..=0x2283 => {
            let (s, d) = pair(0x2044, MIXES, INPUTS)?;
            (Source::Input(s), Dest::Aux(d))
        }
        0x2314..=0x2373 => {
            let (s, d) = pair(0x2314, MIXES, FX_RETURNS)?;
            (Source::FxReturn(s), Dest::Aux(d))
        }
        0x2614..=0x26D3 => {
            let (s, d) = pair(0x2614, FX_SENDS, INPUTS)?;
            (Source::Input(s), Dest::FxSend(d))
        }
        0x2704..=0x2723 => {
            let (s, d) = pair(0x2704, FX_SENDS, FX_RETURNS)?;
            (Source::FxReturn(s), Dest::FxSend(d))
        }
        0x2724..=0x2726 => (Source::Lr, Dest::Matrix(a - 0x2724)),
        0x2727..=0x274A => {
            let (s, d) = pair(0x2727, MATRICES, MIXES)?;
            (Source::Aux(s), Dest::Matrix(d))
        }
        _ => return None,
    })
}

/// What the 14-bit parameter number `a` (MSB * 128 + LSB) addresses, if Sidecar has it.
pub fn decode(a: u16) -> Option<Param> {
    let n = a as usize;
    Some(match n {
        0x00..=0x2F => Param::Mute(Channel::Input(n)),
        0x3C..=0x43 => Param::Mute(Channel::FxReturn(n - 0x3C)),
        0x44 => Param::Mute(Channel::Lr),
        0x45..=0x50 => Param::Mute(Channel::Aux(n - 0x45)),
        0x51..=0x54 => Param::Mute(Channel::FxSend(n - 0x51)),
        0x55..=0x57 => Param::Mute(Channel::Matrix(n - 0x55)),
        0x100..=0x107 => Param::Mute(Channel::Dca(n - 0x100)),
        0x200..=0x207 => Param::Mute(Channel::MuteGroup(n - 0x200)),
        0x2780 => Param::Master(Channel::Lr),
        0x2781..=0x278C => Param::Master(Channel::Aux(n - 0x2781)),
        0x278D..=0x2790 => Param::Master(Channel::FxSend(n - 0x278D)),
        0x2791..=0x2793 => Param::Master(Channel::Matrix(n - 0x2791)),
        0x27A0..=0x27A7 => Param::Master(Channel::Dca(n - 0x27A0)),
        0x2000..=0x277F => {
            let (s, d) = send(a)?;
            Param::Level(s, d)
        }
        0x2800..=0x2F7F => {
            let (s, d) = send(a - 0x800)?;
            Param::Pan(s, d)
        }
        0x3000..=0x377F => {
            let (s, d) = send(a - 0x1000)?;
            Param::Assign(s, d)
        }
        _ => return None,
    })
}

/// The NRPN fader law (Utility > General > MIDI).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Taper {
    /// 16384 values, straight in dB: the standard mode.
    #[default]
    Linear,
    /// 255 positions, as the SQ's faders.
    Audio,
}

impl Taper {
    /// The 14-bit value of a level.
    pub fn value(self, db: f32) -> u16 {
        if db <= -89.5 {
            return 0;
        }
        match self {
            // The table's "Example Linear Taper Level Values": 0 dB is 76 5C, 118.7 a dB.
            // (The document's worked examples differ from its table by up to 0.65 dB,
            // -20 dB as 63 49 where the table has 64 16: Sidecar follows the table.)
            Taper::Linear => (15196.0 + 118.7 * db).round().clamp(1.0, 16383.0) as u16,
            Taper::Audio => ((FaderLaw::SQ.to_pos(db) * 255.0).round() as u16) << 6,
        }
    }

    /// The level of a 14-bit value.
    pub fn db(self, value: u16) -> f32 {
        if value == 0 {
            return FADER_MIN;
        }
        match self {
            Taper::Linear => ((value as f32 - 15196.0) / 118.7).min(10.0),
            Taper::Audio => FaderLaw::SQ.to_db((value >> 6) as f32 / 255.0),
        }
    }
}

/// A pan (-100..100) as the protocol's value: 00 00 full left, 3F 7F the centre, 7F 7F full right.
fn pan_value(pan: f32) -> u16 {
    if pan.abs() < 0.05 {
        return 0x1FFF;
    }
    (((pan + 100.0) / 200.0 * 16383.0).round() as u16).min(16383)
}

fn pan_of(value: u16) -> f32 {
    if value == 0x1FFF {
        return 0.0;
    }
    (value as f32 / 16383.0 * 200.0 - 100.0).clamp(-100.0, 100.0)
}

/// The engine's strips, looked up once.
struct Console<'a> {
    engine: &'a Engine,
    inputs: Vec<Arc<Strip>>,
    returns: Vec<Arc<Strip>>,
    buses: Vec<Arc<Strip>>,
    mains: Vec<Arc<Strip>>,
    matrices: Vec<Arc<Strip>>,
}

impl<'a> Console<'a> {
    fn new(engine: &'a Engine) -> Option<Self> {
        (engine.profile() == Profile::AhSq5).then(|| Self {
            engine,
            inputs: engine.strips(StripKind::Channel),
            returns: engine.strips(StripKind::Aux),
            buses: engine.strips(StripKind::Bus),
            mains: engine.strips(StripKind::Main),
            matrices: engine.strips(StripKind::Matrix),
        })
    }

    fn channel(&self, c: Channel) -> Option<&Arc<Strip>> {
        match c {
            Channel::Input(i) => self.inputs.get(i),
            Channel::FxReturn(i) => self.returns.get(i),
            Channel::Lr => self.mains.first(),
            Channel::Aux(i) => self.buses.get(i),
            Channel::FxSend(i) => self.buses.get(MIXES + i),
            Channel::Matrix(i) => self.matrices.get(i),
            Channel::Dca(_) | Channel::MuteGroup(_) => None,
        }
    }

    fn source(&self, s: Source) -> Option<&Arc<Strip>> {
        match s {
            Source::Input(i) => self.inputs.get(i),
            Source::FxReturn(i) => self.returns.get(i),
            Source::Lr => self.mains.first(),
            Source::Aux(i) => self.buses.get(i),
        }
    }

    /// The bus slot a destination is the send to, if it's a bus.
    fn bus_slot(&self, d: Dest) -> Option<usize> {
        let bus = match d {
            Dest::Aux(i) => self.buses.get(i),
            Dest::FxSend(i) => self.buses.get(MIXES + i),
            Dest::Lr | Dest::Matrix(_) => None,
        }?;
        usize::try_from(bus.id()).ok()
    }

    fn read(&self, p: Param, taper: Taper) -> Option<u16> {
        let dcas = &self.engine.controls.dcas;
        Some(match p {
            Param::Mute(Channel::Dca(i)) => dcas.get(i)?.mute.load(Relaxed) as u16,
            Param::Mute(Channel::MuteGroup(i)) => self.engine.controls.mute_groups.contains(i) as u16,
            Param::Mute(c) => self.channel(c)?.params.mute.load(Relaxed) as u16,
            Param::Master(Channel::Dca(i)) => taper.value(dcas.get(i)?.fader_db.load()),
            Param::Master(c) => taper.value(self.channel(c)?.params.fader_db.load()),
            Param::Level(s, Dest::Lr) => taper.value(self.source(s)?.params.fader_db.load()),
            Param::Level(s, Dest::Matrix(m)) => taper.value(self.source(s)?.matrix_send(m)?.level_db.load()),
            Param::Level(s, d) => {
                let slot = self.bus_slot(d)?;
                taper.value(self.source(s)?.sends.find(slot).map_or(FADER_MIN, |p| p.level_db.load()))
            }
            Param::Pan(s, Dest::Lr) => pan_value(self.source(s)?.params.pan.load()),
            Param::Pan(s, Dest::Matrix(m)) => pan_value(self.source(s)?.matrix_send(m)?.pan.load()),
            Param::Pan(s, d) => {
                let slot = self.bus_slot(d)?;
                pan_value(self.source(s)?.sends.find(slot).map_or(0.0, |p| p.pan.load()))
            }
            Param::Assign(s, Dest::Lr) => self.source(s)?.main_send(0)?.on.load(Relaxed) as u16,
            Param::Assign(s, Dest::Matrix(m)) => self.source(s)?.matrix_send(m)?.on.load(Relaxed) as u16,
            Param::Assign(s, d) => {
                let slot = self.bus_slot(d)?;
                self.source(s)?.sends.find(slot).is_some_and(|p| p.enabled.load(Relaxed)) as u16
            }
        })
    }

    fn write(&self, p: Param, taper: Taper, v: u16) -> Option<()> {
        let dcas = &self.engine.controls.dcas;
        let on = v != 0;
        match p {
            Param::Mute(Channel::Dca(i)) => dcas.get(i)?.mute.store(on, Relaxed),
            Param::Mute(Channel::MuteGroup(i)) => self.engine.controls.mute_groups.set(i, on),
            Param::Mute(c) => self.channel(c)?.params.mute.store(on, Relaxed),
            Param::Master(Channel::Dca(i)) => dcas.get(i)?.fader_db.store(taper.db(v)),
            Param::Master(c) => self.channel(c)?.params.fader_db.store(taper.db(v)),
            Param::Level(s, Dest::Lr) => self.source(s)?.params.fader_db.store(taper.db(v)),
            Param::Level(s, Dest::Matrix(m)) => self.source(s)?.matrix_send(m)?.level_db.store(taper.db(v)),
            Param::Level(s, d) => self.source(s)?.sends.get_or_create(self.bus_slot(d)?)?.level_db.store(taper.db(v)),
            Param::Pan(s, Dest::Lr) => self.source(s)?.params.pan.store(pan_of(v)),
            Param::Pan(s, Dest::Matrix(m)) => self.source(s)?.matrix_send(m)?.pan.store(pan_of(v)),
            Param::Pan(s, d) => self.source(s)?.sends.get_or_create(self.bus_slot(d)?)?.pan.store(pan_of(v)),
            Param::Assign(s, Dest::Lr) => self.source(s)?.main_send(0)?.on.store(on, Relaxed),
            Param::Assign(s, Dest::Matrix(m)) => self.source(s)?.matrix_send(m)?.on.store(on, Relaxed),
            Param::Assign(s, d) => self.source(s)?.sends.get_or_create(self.bus_slot(d)?)?.enabled.store(on, Relaxed),
        }
        Some(())
    }
}

/// What the MIDI asks of the app.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Event {
    /// Recall scene `n` (1-300).
    RecallScene(u16),
}

/// The protocol's state: what it has received, and sent.
pub struct Midi {
    /// The SQ's MIDI channel (0-15).
    pub channel: u8,
    pub taper: Taper,
    msb: u8,
    lsb: u8,
    coarse: u8,
    bank: u8,
    /// Every address Sidecar has, and the value last sent or received.
    params: Vec<(u16, Param)>,
    last: Vec<Option<u16>>,
    out: VecDeque<Vec<u8>>,
    events: VecDeque<Event>,
    primed: bool,
}

impl Default for Midi {
    fn default() -> Self {
        let params: Vec<(u16, Param)> = (0..0x3800).filter_map(|a| decode(a).map(|p| (a, p))).collect();
        let last = vec![None; params.len()];
        Self {
            channel: 0,
            taper: Taper::Linear,
            msb: 0,
            lsb: 0,
            coarse: 0,
            bank: 0,
            params,
            last,
            out: VecDeque::new(),
            events: VecDeque::new(),
            primed: false,
        }
    }
}

impl Midi {
    pub fn take_out(&mut self) -> Option<Vec<u8>> {
        self.out.pop_front()
    }

    pub fn take_event(&mut self) -> Option<Event> {
        self.events.pop_front()
    }

    fn address(&self) -> u16 {
        (self.msb as u16) << 7 | self.lsb as u16
    }

    fn slot(&self, a: u16) -> Option<usize> {
        self.params.binary_search_by_key(&a, |p| p.0).ok()
    }

    /// Handles one received MIDI message on the console.
    pub fn receive(&mut self, engine: &Engine, msg: &[u8]) {
        let Some(c) = Console::new(engine) else { return };
        let (Some(&status), true) = (msg.first(), msg.len() >= 2) else { return };
        if status & 0x0F != self.channel {
            return;
        }
        match (status & 0xF0, msg.get(1).copied(), msg.get(2).copied()) {
            (0xB0, Some(0x00), Some(bank)) => self.bank = bank,
            (0xB0, Some(0x63), Some(v)) => self.msb = v,
            (0xB0, Some(0x62), Some(v)) => self.lsb = v,
            (0xB0, Some(0x06), Some(v)) => self.coarse = v,
            (0xB0, Some(0x26), Some(fine)) => self.apply(&c, (self.coarse as u16) << 7 | fine as u16),
            (0xB0, Some(0x60), Some(_)) => self.step(&c, true),
            (0xB0, Some(0x61), Some(_)) => self.step(&c, false),
            (0xC0, Some(program), _) => {
                // Banks 0-2 scenes 1-300 by number; 0A-0C the Cue List's positions.
                let bank = if (0x0A..=0x0C).contains(&self.bank) { self.bank - 0x0A } else { self.bank };
                if bank <= 2 {
                    let n = bank as u16 * 128 + program as u16 + 1;
                    if n <= 300 {
                        self.events.push_back(Event::RecallScene(n));
                    }
                }
            }
            _ => {}
        }
    }

    /// An absolute value for the selected parameter.
    fn apply(&mut self, c: &Console, value: u16) {
        let a = self.address();
        let Some(i) = self.slot(a) else { return };
        let p = self.params[i].1;
        if c.write(p, self.taper, value).is_some() {
            // What it holds now, so the poll doesn't echo it.
            self.last[i] = c.read(p, self.taper);
        }
    }

    /// A relative message: a level by 1 dB, a pan by 1 %, a mute or assignment toggled.
    fn step(&mut self, c: &Console, up: bool) {
        let a = self.address();
        let Some(i) = self.slot(a) else { return };
        let p = self.params[i].1;
        let Some(now) = c.read(p, self.taper) else { return };
        let next = match p {
            Param::Mute(_) | Param::Assign(..) => (now == 0) as u16,
            Param::Pan(..) => pan_value((pan_of(now) + if up { 1.0 } else { -1.0 }).clamp(-100.0, 100.0)),
            Param::Master(_) | Param::Level(..) => {
                let db = self.taper.db(now);
                let db = if db <= FADER_MIN {
                    if up {
                        -89.0
                    } else {
                        return;
                    }
                } else {
                    db + if up { 1.0 } else { -1.0 }
                };
                self.taper.value(db.min(10.0))
            }
        };
        if c.write(p, self.taper, next).is_some() {
            self.last[i] = c.read(p, self.taper);
        }
    }

    /// Queues the console's changes since the last poll (the first takes the
    /// console's values and sends nothing).
    pub fn poll(&mut self, engine: &Engine) {
        let Some(c) = Console::new(engine) else { return };
        for i in 0..self.params.len() {
            let (a, p) = self.params[i];
            let now = c.read(p, self.taper);
            if now != self.last[i] {
                self.last[i] = now;
                if let (true, Some(v)) = (self.primed, now) {
                    self.send(a, v);
                }
            }
        }
        self.primed = true;
    }

    /// Sends every value again (the next poll sends what differs from nothing).
    pub fn resend_all(&mut self) {
        self.last.fill(None);
    }

    fn send(&mut self, a: u16, v: u16) {
        let cc = 0xB0 | self.channel;
        for (n, b) in [(0x63, a >> 7), (0x62, a & 0x7F), (0x06, v >> 7), (0x26, v & 0x7F)] {
            self.out.push_back(vec![cc, n, b as u8]);
        }
    }

    /// Scene `n` (1-300) was recalled on the console: its bank and program change go out.
    pub fn scene_recalled(&mut self, n: u16) {
        if !(1..=300).contains(&n) {
            return;
        }
        let i = n - 1;
        self.out.push_back(vec![0xB0 | self.channel, 0x00, (i / 128) as u8]);
        self.out.push_back(vec![0xC0 | self.channel, (i % 128) as u8]);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_engine::{ConsoleConfig, Counts};

    fn sq() -> Engine {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::AhSq5, counts: Counts::SQ5 });
        e
    }

    fn nrpn(m: &mut Midi, e: &Engine, bytes: &[u8]) {
        for msg in bytes.chunks(3) {
            m.receive(e, msg);
        }
    }

    #[test]
    fn the_tables_formulas_land_on_the_tables_addresses() {
        let at = |msb: u16, lsb: u16| decode(msb << 7 | lsb);
        // The mute table.
        assert_eq!(at(0x00, 0x2F), Some(Param::Mute(Channel::Input(47))));
        assert_eq!(at(0x00, 0x3C), Some(Param::Mute(Channel::FxReturn(0))));
        assert_eq!(at(0x00, 0x44), Some(Param::Mute(Channel::Lr)));
        assert_eq!(at(0x00, 0x50), Some(Param::Mute(Channel::Aux(11))));
        assert_eq!(at(0x00, 0x54), Some(Param::Mute(Channel::FxSend(3))));
        assert_eq!(at(0x02, 0x07), Some(Param::Mute(Channel::Dca(7))));
        assert_eq!(at(0x04, 0x07), Some(Param::Mute(Channel::MuteGroup(7))));
        assert_eq!(at(0x00, 0x30), None, "Grp1: a new SQ's mixes are Auxes");
        // The level tables' corners.
        assert_eq!(at(0x40, 0x2F), Some(Param::Level(Source::Input(47), Dest::Lr)));
        assert_eq!(at(0x40, 0x44), Some(Param::Level(Source::Input(0), Dest::Aux(0))));
        assert_eq!(at(0x41, 0x00), Some(Param::Level(Source::Input(5), Dest::Aux(0))));
        assert_eq!(at(0x45, 0x03), Some(Param::Level(Source::Input(47), Dest::Aux(11))));
        assert_eq!(at(0x46, 0x14), Some(Param::Level(Source::FxReturn(0), Dest::Aux(0))));
        assert_eq!(at(0x46, 0x73), Some(Param::Level(Source::FxReturn(7), Dest::Aux(11))));
        assert_eq!(at(0x4C, 0x14), Some(Param::Level(Source::Input(0), Dest::FxSend(0))));
        assert_eq!(at(0x4D, 0x53), Some(Param::Level(Source::Input(47), Dest::FxSend(3))));
        assert_eq!(at(0x4E, 0x23), Some(Param::Level(Source::FxReturn(7), Dest::FxSend(3))));
        assert_eq!(at(0x4E, 0x24), Some(Param::Level(Source::Lr, Dest::Matrix(0))));
        assert_eq!(at(0x4E, 0x4A), Some(Param::Level(Source::Aux(11), Dest::Matrix(2))));
        assert_eq!(at(0x4F, 0x00), Some(Param::Master(Channel::Lr)));
        assert_eq!(at(0x4F, 0x0C), Some(Param::Master(Channel::Aux(11))));
        assert_eq!(at(0x4F, 0x10), Some(Param::Master(Channel::FxSend(3))));
        assert_eq!(at(0x4F, 0x11), Some(Param::Master(Channel::Matrix(0))));
        assert_eq!(at(0x4F, 0x27), Some(Param::Master(Channel::Dca(7))));
        // Pan and assignment: the same map 0x800 and 0x1000 on.
        assert_eq!(at(0x55, 0x03), Some(Param::Pan(Source::Input(47), Dest::Aux(11))));
        assert_eq!(at(0x65, 0x03), Some(Param::Assign(Source::Input(47), Dest::Aux(11))));
        assert_eq!(at(0x60, 0x00), Some(Param::Assign(Source::Input(0), Dest::Lr)));
    }

    #[test]
    fn both_fader_laws_match_the_tables() {
        // (dB, VC, VF) of "Example Linear Taper Level Values".
        for (db, vc, vf) in [(0.0, 0x76, 0x5C), (-20.0, 0x64, 0x16), (-40.0, 0x51, 0x4F), (10.0, 0x7F, 0x7F), (-89.0, 0x24, 0x16)]
        {
            let v = Taper::Linear.value(db);
            assert!((v as i32 - (vc << 7 | vf)).abs() <= 2, "{db}: {v}");
            assert!((Taper::Linear.db(v) - db).abs() < 0.03, "{db}");
        }
        assert_eq!(Taper::Linear.value(FADER_MIN), 0);
        assert_eq!(Taper::Linear.db(0), FADER_MIN);
        // "Approximate Audio Taper Level Values".
        for (db, vc, vf) in [(0.0, 0x62, 0x00), (-20.0, 0x2E, 0x40), (10.0, 0x7F, 0x40)] {
            assert_eq!(Taper::Audio.value(db), (vc << 7 | vf) as u16, "{db}");
        }
        // "Example Pan/Balance Values".
        assert_eq!(pan_value(-100.0), 0);
        assert_eq!(pan_value(0.0), 0x3F << 7 | 0x7F);
        assert_eq!(pan_value(100.0), 0x7F << 7 | 0x7F);
        assert!((pan_of(0x06 << 7 | 0x33) + 90.0).abs() < 0.1);
    }

    #[test]
    fn the_protocols_examples_do_what_it_says() {
        let e = sq();
        let mut m = Midi::default();
        let ch = |i: usize| e.strips(StripKind::Channel)[i].clone();
        // Ip1, Mute On / Off / Toggle.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x00, 0xB0, 0x62, 0x00, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x01]);
        assert!(ch(0).params.mute.load(Relaxed));
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x00, 0xB0, 0x62, 0x00, 0xB0, 0x60, 0x00]);
        assert!(!ch(0).params.mute.load(Relaxed), "an increment toggles it");
        // Ip1 to LR, -20 dB.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x40, 0xB0, 0x62, 0x00, 0xB0, 0x06, 0x64, 0xB0, 0x26, 0x16]);
        assert!((ch(0).params.fader_db.load() + 20.0).abs() < 0.02);
        // Ip40 to Aux5, -20 dB (the level table's value).
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x44, 0xB0, 0x62, 0x1C, 0xB0, 0x06, 0x64, 0xB0, 0x26, 0x16]);
        let aux5 = e.bus_in_slot(4).unwrap().id() as usize;
        assert!((ch(39).sends.find(aux5).unwrap().level_db.load() + 20.0).abs() < 0.02);
        // Relative: +1 dB, the same parameter.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x44, 0xB0, 0x62, 0x1C, 0xB0, 0x60, 0x00]);
        assert!((ch(39).sends.find(aux5).unwrap().level_db.load() + 19.0).abs() < 0.03);
        // Ip1 to LR, L100%.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x50, 0xB0, 0x62, 0x00, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x00]);
        assert_eq!(ch(0).params.pan.load(), -100.0);
        // Another MIDI channel: ignored.
        nrpn(&mut m, &e, &[0xB3, 0x63, 0x00, 0xB3, 0x62, 0x00, 0xB3, 0x06, 0x00, 0xB3, 0x26, 0x01]);
        assert!(!ch(0).params.mute.load(Relaxed));
        // Mute Grp 4, DCA 1.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x04, 0xB0, 0x62, 0x03, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x01]);
        assert!(e.controls.mute_groups.contains(3));
    }

    #[test]
    fn scenes_by_bank_and_program_as_the_examples() {
        let e = sq();
        let mut m = Midi::default();
        for (bytes, n) in [
            ([0xB0, 0x00, 0x00, 0xC0, 0x06], 7),
            ([0xB0, 0x00, 0x00, 0xC0, 0x77], 120),
            ([0xB0, 0x00, 0x01, 0xC0, 0x1B], 156),
            ([0xB0, 0x00, 0x0B, 0xC0, 0x1B], 156), // cue 156
        ] {
            m.receive(&e, &bytes[..3]);
            m.receive(&e, &bytes[3..]);
            assert_eq!(m.take_event(), Some(Event::RecallScene(n)));
        }
        m.scene_recalled(156);
        assert_eq!(m.take_out(), Some(vec![0xB0, 0x00, 0x01]));
        assert_eq!(m.take_out(), Some(vec![0xC0, 0x1B]));
    }

    #[test]
    fn console_changes_go_out_received_values_dont_echo() {
        let e = sq();
        let mut m = Midi::default();
        m.poll(&e);
        assert_eq!(m.take_out(), None, "nothing at start");
        let ch = e.strips(StripKind::Channel)[0].clone();
        ch.params.fader_db.store(0.0);
        m.poll(&e);
        let sent: Vec<_> = std::iter::from_fn(|| m.take_out()).collect();
        // Ip1 to LR at 0 dB: 40 00, then 76 5C.
        assert_eq!(sent, vec![vec![0xB0, 0x63, 0x40], vec![0xB0, 0x62, 0x00], vec![0xB0, 0x06, 0x76], vec![0xB0, 0x26, 0x5C]]);
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x40, 0xB0, 0x62, 0x00, 0xB0, 0x06, 0x64, 0xB0, 0x26, 0x16]);
        m.poll(&e);
        assert_eq!(m.take_out(), None, "what came in doesn't go back");
    }

    #[test]
    fn another_console_is_left_alone() {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig::wing());
        let mut m = Midi::default();
        let before = e.strips(StripKind::Channel)[0].params.mute.load(Relaxed);
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x00, 0xB0, 0x62, 0x00, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x01]);
        assert_eq!(e.strips(StripKind::Channel)[0].params.mute.load(Relaxed), before);
        m.poll(&e);
        assert_eq!(m.take_out(), None);
    }
}
