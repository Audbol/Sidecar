//! The DM2000's MIDI (V2 Owner's Manual Appendix C; Yamaha's "DM2000 V2 MIDI
//! Specification", the parameter change list and its tables, in `tables.rs`
//! through `tools/dm2000/gen_midi_tables.py`).
//!
//! * Parameter changes and requests (`F0 43 1n/3n 3E 7F 01 ...` for the edit
//!   buffer, `3E 06 02`/`03` for the patches and the setup): every parameter the
//!   engine has (`Field`), by the list's element, parameter and channel numbers;
//!   a request is answered as a change on the Rx channel; console changes go out.
//! * The function calls for scenes (`10 00` recall, `10 20` store) ask the app.
//! * Control changes: the manual's 16 tables, kept; the formula that scales a
//!   value (p.370) on the way in and out. NRPN mode is the setup's only: Yamaha
//!   doesn't publish the NRPN numbers, so it does nothing.
//! * Program changes: the 128-entry table to scenes 0-99 (p.352).
//! * Key remote (`06 20`): LAYER, ON and SOLO keys act here; every key goes to the
//!   app as well (Sidecar's surface decides the rest).
//! * Remote meter (`06 21`): requests start 50 ms meter data for ten seconds.
//! * MMC: the DM2000 only transmits commands and receives responses (DM2000MMC.xls),
//!   so Sidecar transmits stop, play, FF, REW and record when asked and notes the
//!   responses it gets.
//!
//! **Sidecar's reading**, not the document's: which engine signal answers each
//! meter point (the post-fader meter for CHANNEL, the pre-fader one for EQ OUT);
//! the gain reduction meters as the level of the reduction on the same scale,
//! 0 dB of reduction its full value (the document only gives the scale); the
//! `ALL` meter order (by channel, the types in the sheet's order); a matrix's L
//! and R sharing one engine strip and send pair. FADER LOW's 256 steps are the
//! 10-bit value snapped to fours.

use crate::tables::{CcAssign, Def, BUS_FADER, CC_INITIAL, DEFS, FADER, METER};
use livedaw_dsp::fader::FADER_MIN;
use livedaw_dsp::model::model_index;
use livedaw_engine::params::DynSlotParams;
use livedaw_engine::ports::dm2000_ports;
use livedaw_engine::routing::strip_source_input;
use livedaw_engine::sends::send_mode;
use livedaw_engine::sources::FX_RETURN_SOURCE_BASE;
use livedaw_engine::{Engine, Profile, Strip, StripKind};
use std::collections::{HashMap, VecDeque};
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

const GROUP: u8 = 0x3E;
const MODEL: u8 = 0x06;
const UNIVERSAL: u8 = 0x7F;

const CHANNELS: usize = 96;
const BUSES: usize = 8;
const AUXES: usize = 12;
const MATRICES: usize = 4;
const LAYER_MASTER: u8 = 4;

/// The compressor's types, as `kCompType` numbers them.
const COMP_TYPES: [&str; 4] = ["DMCMP", "DMEXP", "DMCPH", "DMCPS"];
/// The gate's types, as `kGateType` numbers them.
const GATE_TYPES: [&str; 2] = ["DMGAT", "DMDUK"];

/// The first dm2000 effect model in `FX_MODELS` (after the WING's empty and EXT
/// slots); `kEffectType` is the manual's type number minus one (Sidecar's reading:
/// the list's value 0 pairs with "Reverb Hall", number 1).
const FX_FIRST: usize = 2;
const FX_STEREO_TYPES: usize = 43; // types 1-43 are in FX_MODELS in the manual's order
const FX_MBAND_MODEL: usize = 45;
const FX_MBAND_VALUE: i32 = 49; // the manual's type 50

/// The six kinds of message MIDI SETUP switches: program change, control change,
/// parameter change, bulk, MTC, others.
pub const KINDS: usize = 6;
pub const PGM: usize = 0;
pub const CTL: usize = 1;
pub const PRM: usize = 2;
pub const OTHER: usize = 5;
/// `Setup::to_bytes`' length.
pub const SETUP_BYTES: usize = 2 + 4 * KINDS + 2;

/// MIDI SETUP (Owner's Manual p.217), its initial state the list's.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Setup {
    pub rx_channel: u8,
    pub tx_channel: u8,
    pub tx: [bool; KINDS],
    pub rx: [bool; KINDS],
    pub omni: [bool; KINDS],
    pub echo: [bool; KINDS],
    /// Control changes by NRPN instead of the table.
    pub nrpn: bool,
    /// Faders at 256 steps.
    pub fader_low: bool,
}

impl Default for Setup {
    fn default() -> Self {
        Self {
            rx_channel: 0,
            tx_channel: 0,
            tx: [false; KINDS],
            rx: [true, false, true, true, false, false],
            omni: [false; KINDS],
            echo: [false; KINDS],
            nrpn: false,
            fader_low: false,
        }
    }
}

impl Setup {
    pub fn to_bytes(&self) -> [u8; SETUP_BYTES] {
        let mut b = [0; SETUP_BYTES];
        b[0] = self.rx_channel;
        b[1] = self.tx_channel;
        for k in 0..KINDS {
            b[2 + k] = self.tx[k] as u8;
            b[2 + KINDS + k] = self.rx[k] as u8;
            b[2 + 2 * KINDS + k] = self.omni[k] as u8;
            b[2 + 3 * KINDS + k] = self.echo[k] as u8;
        }
        b[SETUP_BYTES - 2] = self.nrpn as u8;
        b[SETUP_BYTES - 1] = self.fader_low as u8;
        b
    }

    pub fn from_bytes(b: &[u8; SETUP_BYTES]) -> Self {
        let mut s = Self { rx_channel: b[0].min(15), tx_channel: b[1].min(15), ..Self::default() };
        for k in 0..KINDS {
            s.tx[k] = b[2 + k] != 0;
            s.rx[k] = b[2 + KINDS + k] != 0;
            s.omni[k] = b[2 + 2 * KINDS + k] != 0;
            s.echo[k] = b[2 + 3 * KINDS + k] != 0;
        }
        s.nrpn = b[SETUP_BYTES - 2] != 0;
        s.fader_low = b[SETUP_BYTES - 1] != 0;
        s
    }
}

/// What the MIDI asks of the app.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Event {
    /// Recall scene `n` (0-99).
    RecallScene(u16),
    /// Store into scene `n` (1-99).
    StoreScene(u16),
    /// A key of the Key Remote list went down (or up): for the surface.
    Key { h: u8, m: u8, l: u8, press: bool },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Group {
    Input,
    Bus,
    Aux,
    Matrix,
    Stereo,
    Other,
}

/// What a parameter of the list holds in the engine.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Field {
    Phase,
    InsertOn,
    InsertFirst,
    InsertLoc,
    On,
    Pan,
    Fader,
    Att,
    GateOn,
    GateType,
    GateAttack,
    GateRange,
    GateHold,
    GateDecay,
    GateThreshold,
    CompLoc,
    CompOn,
    CompType,
    CompAttack,
    CompRelease,
    CompRatio,
    CompGain,
    CompKnee,
    CompThreshold,
    EqMode,
    EqOn,
    EqLowQ,
    EqHighQ,
    EqMidQ(u8),
    EqFreq(u8),
    EqGain(u8),
    EqSwitch(u8),
    DelayOn,
    DelayTime,
    RouteStereo,
    RouteBus(u8),
    AuxOn(u8),
    AuxPre(u8),
    AuxLevel(u8),
    AuxPan(u8),
    ToMatrixOn(u8),
    ToMatrixLevel(u8),
    ToMatrixPan(u8),
    ToStereoOn,
    ToStereoLevel,
    ToStereoPan,
    FaderGroup(u8),
    MuteGroup(u8),
    GroupMasterFader(u8),
    GroupMasterOn(u8),
    MuteMaster(u8),
    FxMix,
    FxType,
    FxBypass,
    ChannelIn,
    SlotOut(u8),
    OmniOut,
    StereoOut,
    Digital2tr,
    Name(u8),
    RxChannel,
    TxChannel,
    TxEnable(u8),
    RxEnable(u8),
    OmniEnable(u8),
    EchoEnable(u8),
    ControlMode,
    FaderResolution,
    PcTable,
}

fn group_of(element: &str) -> Group {
    if element.starts_with("kInput") || element.starts_with("kChannel") {
        Group::Input
    } else if element.starts_with("kBus") {
        Group::Bus
    } else if element.starts_with("kAUX") {
        Group::Aux
    } else if element.starts_with("kMatrix") {
        Group::Matrix
    } else if element.starts_with("kStereo") {
        Group::Stereo
    } else {
        Group::Other
    }
}

/// The number at the end of a name, or after `prefix`.
fn suffix(name: &str, prefix: &str) -> Option<u8> {
    name.strip_prefix(prefix)?.parse().ok()
}

fn field_of(d: &Def) -> Option<(Group, Field)> {
    use Field::*;
    let g = group_of(d.element);
    let n = d.name;
    let f = match d.element {
        "kInFaderGrpMaster" | "kOutFaderGrpMaster" => {
            let base = if d.element == "kInFaderGrpMaster" { 0 } else { 8 };
            match n {
                "kFader" => GroupMasterFader(base),
                "kMasterOn" => GroupMasterOn(base),
                _ => return None,
            }
        }
        "kInMuteGrpMaster" => MuteMaster(0),
        "kOutMuteGrpMaster" => MuteMaster(8),
        "kEffect" => match n {
            "kEffectMix" => FxMix,
            "kEffectType" => FxType,
            "kEffectBypass" => FxBypass,
            _ => return None,
        },
        "kSetupMIDIRx" => match n {
            "kMIDIRxMidiCh" => RxChannel,
            _ => return None,
        },
        "kSetupMIDITx" => match n {
            "kMIDITxMidiCh" => TxChannel,
            _ => return None,
        },
        "kSetupMIDITxEnable" | "kSetupMIDIRxEnable" | "kSetupMIDIOmniEnable" | "kSetupMIDIEchoEnable" => {
            let k = d.pp;
            if (k as usize) >= KINDS {
                return None;
            }
            match d.element {
                "kSetupMIDITxEnable" => TxEnable(k),
                "kSetupMIDIRxEnable" => RxEnable(k),
                "kSetupMIDIOmniEnable" => OmniEnable(k),
                _ => EchoEnable(k),
            }
        }
        "kSetupMIDIControl" => ControlMode,
        "kSetupMIDISetup" => FaderResolution,
        "kSetupPGMChangeTable" => PcTable,
        "kChannelInput" => ChannelIn,
        "kSlotOutput" => SlotOut(d.pp),
        "kOmniOutput" => OmniOut,
        "kStereoOut" => StereoOut,
        "kDigital2trOut" => Digital2tr,
        "kInputChannelName" | "kBusModuleName" | "kAUXModuleName" | "kMatrixModuleName" | "kStereoOutModuleName" => {
            let i = n.rfind("Long")?;
            Name(n[i + 4..].parse::<u8>().ok()?.checked_sub(1)?)
        }
        _ => match n {
            "kPhase" => Phase,
            "kInsertOn" => InsertOn,
            "kInsertLocInsertFirst" => InsertFirst,
            "kInsertLocInsert" => InsertLoc,
            "kChannelOn" => On,
            "kChannelPan" | "kBalance" => Pan,
            "kFader" => Fader,
            "kAtt" => Att,
            "kGateOn" => GateOn,
            "kGateType" => GateType,
            "kGateAttack" => GateAttack,
            "kGateRange" => GateRange,
            "kGateHold" => GateHold,
            "kGateDecay" => GateDecay,
            "kGateThreshold" => GateThreshold,
            "kCompLocComp" => CompLoc,
            "kCompOn" => CompOn,
            "kCompType" => CompType,
            "kCompAttack" => CompAttack,
            "kCompRelease" => CompRelease,
            "kCompRatio" => CompRatio,
            "kCompGain" => CompGain,
            "kCompKnee" => CompKnee,
            "kCompThreshold" => CompThreshold,
            "kEQMode" => EqMode,
            "kEQOn" => EqOn,
            "kEQLowQ" => EqLowQ,
            "kEQHiQ" => EqHighQ,
            "kEQLowMidQ" => EqMidQ(4),
            "kEQHiMidQ" => EqMidQ(7),
            "kEQLowF" => EqFreq(2),
            "kEQLowMidF" => EqFreq(5),
            "kEQHiMidF" => EqFreq(8),
            "kEQHiF" => EqFreq(11),
            "kEQLowG" => EqGain(3),
            "kEQLowMidG" => EqGain(6),
            "kEQHiMidG" => EqGain(9),
            "kEQHiG" => EqGain(12),
            "kEQHPFOn" => EqSwitch(13),
            "kEQLPFOn" => EqSwitch(14),
            "kInDelayOn" | "kOutDelayOn" => DelayOn,
            "kInDelayTime" | "kOutDelayTime" => DelayTime,
            "kRoutingStereo" => RouteStereo,
            "kBusToStereoOn" => ToStereoOn,
            "kBusToStereoLevel" => ToStereoLevel,
            "kBusToStereoPan" => ToStereoPan,
            _ => {
                if let Some(rest) = n.strip_prefix("kAUX") {
                    let digits: String = rest.chars().take_while(char::is_ascii_digit).collect();
                    let tail = &rest[digits.len()..];
                    let num: u16 = digits.parse().ok()?;
                    match tail {
                        "On" => AuxOn((num.checked_sub(1)?) as u8),
                        "Pre" => AuxPre((num.checked_sub(1)?) as u8),
                        "Level" => AuxLevel((num.checked_sub(1)?) as u8),
                        "Pan" if digits.len() == 4 => AuxPan((num / 100 / 2) as u8),
                        _ => return None,
                    }
                } else if let Some(rest) = n.strip_prefix("kBusToMatrixPan") {
                    let num: u16 = rest.parse().ok()?;
                    ToMatrixPan((num / 100 / 2) as u8)
                } else if let Some(rest) = n.strip_prefix("kBusToMatrix") {
                    let digits: String = rest.chars().take_while(char::is_ascii_digit).collect();
                    let num: u8 = digits.parse().ok()?;
                    match &rest[digits.len()..] {
                        "On" => ToMatrixOn((num.checked_sub(1)?) / 2),
                        "Level" => ToMatrixLevel((num.checked_sub(1)?) / 2),
                        _ => return None,
                    }
                } else if let Some(x) = suffix(n, "kRoutingBus") {
                    RouteBus(x.checked_sub(1)?)
                } else if let Some(x) = n.find("GroupFader").and_then(|i| n[i + 10..].parse::<u8>().ok()) {
                    FaderGroup(x.checked_sub(1)?)
                } else {
                    let x = n.find("GroupMute").and_then(|i| n[i + 9..].parse::<u8>().ok())?;
                    MuteGroup(x.checked_sub(1)?)
                }
            }
        },
    };
    Some((g, f))
}

/// The console's strips, looked up once.
struct Console<'a> {
    engine: &'a Engine,
    inputs: Vec<Arc<Strip>>,
    buses: Vec<Arc<Strip>>,
    matrices: Vec<Arc<Strip>>,
    mains: Vec<Arc<Strip>>,
}

impl<'a> Console<'a> {
    fn new(engine: &'a Engine) -> Option<Self> {
        (engine.profile() == Profile::Dm2000).then(|| Self {
            engine,
            inputs: engine.strips(StripKind::Channel),
            buses: engine.strips(StripKind::Bus),
            matrices: engine.strips(StripKind::Matrix),
            mains: engine.strips(StripKind::Main),
        })
    }

    fn strip(&self, g: Group, ch: usize) -> Option<&Arc<Strip>> {
        match g {
            Group::Input => self.inputs.get(ch),
            Group::Bus => self.buses.get(ch).filter(|_| ch < BUSES),
            Group::Aux => self.buses.get(BUSES + ch).filter(|_| ch < AUXES),
            Group::Matrix => self.matrices.get(ch / 2),
            Group::Stereo => self.mains.first(),
            Group::Other => None,
        }
    }

    fn bus_slot(&self, i: usize) -> Option<usize> {
        usize::try_from(self.buses.get(i)?.id()).ok()
    }

    fn aux_slot(&self, i: usize) -> Option<usize> {
        self.bus_slot(BUSES + i)
    }
}

/// A fader's 10-bit value (table `t`) nearest level `db`.
pub fn fader_value(t: &[f32; 1024], db: f32) -> i32 {
    if db <= t[1] - 0.001 && db < t[0] + 3.0 {
        return 0;
    }
    let p = t.partition_point(|&x| x < db);
    let p = p.clamp(1, 1023);
    let i = if (t[p] - db).abs() < (t[p - 1] - db).abs() { p } else { p - 1 };
    i as i32
}

fn fader_table(g: Group) -> &'static [f32; 1024] {
    match g {
        Group::Input => &FADER,
        _ => &BUS_FADER,
    }
}

/// A pan's list value (-63..63) from the engine's -100..100.
fn pan_value(pan: f32) -> i32 {
    (pan / 100.0 * 63.0).round() as i32
}

fn pan_of(v: i32) -> f32 {
    v as f32 / 63.0 * 100.0
}

/// PROCESS ORDER (`chain::DM2000_ORDERS`) as the list's three parameters.
fn order_parts(order: i32) -> (i32, i32, bool) {
    if order >= 9 {
        (order - 9, order - 9, true) // Ins->Comp
    } else {
        (order / 3, order % 3, false)
    }
}

fn order_of(comp: i32, ins: i32, ins_first: bool) -> i32 {
    if comp == ins && ins_first {
        9 + comp
    } else {
        3 * comp + ins
    }
}

fn ports_from_patch(v: i32) -> Option<i32> {
    use dm2000_ports_values::*;
    Some(match v {
        1..=24 => v - 1,
        SLOT_FIRST..=SLOT_LAST => SLOT_PORT + (v - SLOT_FIRST),
        FX1..=FX1_LAST => FX_RETURN_SOURCE_BASE + fx_value_to_return(v)?,
        TD_FIRST..=TD_LAST => TD_PORT + (v - TD_FIRST),
        TA_FIRST..=TA_LAST => TA_PORT + (v - TA_FIRST),
        BUS_FIRST..=BUS_LAST => strip_source_input(StripKind::Bus, (v - BUS_FIRST) as usize, 0),
        _ => return None,
    })
}

/// The PRM TABLE #19 value of an input source (-1 and anything else: 0, NONE).
fn patch_of_source(source: i32) -> i32 {
    use dm2000_ports_values::*;
    if (0..24).contains(&source) {
        return source + 1;
    }
    if (SLOT_PORT..SLOT_PORT + 96).contains(&source) {
        return SLOT_FIRST + (source - SLOT_PORT);
    }
    if (TD_PORT..TD_PORT + 6).contains(&source) {
        return TD_FIRST + (source - TD_PORT);
    }
    if (TA_PORT..TA_PORT + 4).contains(&source) {
        return TA_FIRST + (source - TA_PORT);
    }
    if (FX_RETURN_SOURCE_BASE..FX_RETURN_SOURCE_BASE + 16).contains(&source) {
        return fx_return_to_value(source - FX_RETURN_SOURCE_BASE);
    }
    for b in 0..20 {
        if strip_source_input(StripKind::Bus, b, 0) == source {
            return BUS_FIRST + b as i32;
        }
    }
    0
}

/// The input patch's numbering (PRM TABLE #19) and the engine's ports.
mod dm2000_ports_values {
    pub const SLOT_FIRST: i32 = 25;
    pub const SLOT_LAST: i32 = 120;
    pub const SLOT_PORT: i32 = 24;
    pub const FX1: i32 = 121;
    pub const FX1_LAST: i32 = 148;
    pub const TD_FIRST: i32 = 149;
    pub const TD_LAST: i32 = 154;
    pub const TD_PORT: i32 = 120;
    pub const TA_FIRST: i32 = 155;
    pub const TA_LAST: i32 = 158;
    pub const TA_PORT: i32 = 126;
    pub const BUS_FIRST: i32 = 159;
    pub const BUS_LAST: i32 = 178;

    /// #19: FX1-1..8 (121-128), FX2-1..8 (129-136), FX3-1,2 ... FX8-1,2 (137-148).
    /// The engine's return group: FX1-1,2 ... FX8-1,2.
    pub fn fx_value_to_return(v: i32) -> Option<i32> {
        match v {
            121..=122 => Some(v - 121),
            129..=130 => Some(v - 129 + 2),
            137..=148 => Some(v - 137 + 4),
            _ => None, // FX1-3..8, FX2-3..8: the multichannel types, not here
        }
    }

    pub fn fx_return_to_value(r: i32) -> i32 {
        match r {
            0..=1 => 121 + r,
            2..=3 => 129 + r - 2,
            _ => 137 + r - 4,
        }
    }
}

/// Output patch values (#22, #23, #25; #26 from STEREO-only).
fn output_source(v: i32, stereo_out: bool) -> Option<i32> {
    if stereo_out {
        return match v {
            0 => Some(-1),
            1..=8 => Some(strip_source_input(StripKind::Matrix, ((v - 1) / 2) as usize, ((v - 1) % 2) as usize)),
            9..=10 => Some(strip_source_input(StripKind::Main, 0, (v - 9) as usize)),
            _ => None,
        };
    }
    match v {
        0 => Some(-1),
        1..=8 => Some(strip_source_input(StripKind::Bus, (v - 1) as usize, 0)),
        9..=20 => Some(strip_source_input(StripKind::Bus, BUSES + (v - 9) as usize, 0)),
        21..=28 => Some(strip_source_input(StripKind::Matrix, ((v - 21) / 2) as usize, ((v - 21) % 2) as usize)),
        29..=30 => Some(strip_source_input(StripKind::Main, 0, (v - 29) as usize)),
        _ => None,
    }
}

fn output_value(source: i32, stereo_out: bool) -> i32 {
    for v in 1..=(if stereo_out { 10 } else { 30 }) {
        if output_source(v, stereo_out) == Some(source) {
            return v;
        }
    }
    0
}

/// A meter reading (linear) on the document's METER DATA scale.
pub fn meter_value(level: f32) -> u16 {
    if level.is_nan() || level <= 0.0 {
        return 0;
    }
    let db = 20.0 * level.log10();
    if db >= 0.0 {
        return if db > 0.0 { 0x1FFF } else { 0x0FFF };
    }
    meter_value_db(db)
}

fn meter_value_db(db: f32) -> u16 {
    if db <= -96.0 {
        return METER[0];
    }
    let i = (db + 96.0).floor() as usize;
    if i >= 96 {
        return 0x0FFF;
    }
    let f = db + 96.0 - i as f32;
    (METER[i] as f32 + f * (METER[i + 1] as f32 - METER[i] as f32)).round() as u16
}

/// The meter of a gain reduction: 0 dB (none) is full scale, `db` of reduction
/// the level -`db` (Sidecar's reading).
pub fn meter_of_reduction(db: f32) -> u16 {
    meter_value_db(-db.abs().min(96.0))
}

/// A request's running meter data (Remote meter, ten seconds at 50 ms).
#[derive(Clone, Copy, Debug)]
struct MeterJob {
    group: u8,
    kind: u8,
    channel: u8,
    count: u16,
    ticks: u32,
}

const METER_TICKS: u32 = 200;
const METER_JOBS: usize = 32;

/// The control change table: per MIDI channel and number, (parameter, its channel, role).
type CcIn = Vec<Vec<Option<(u16, u16, u8)>>>;
/// The other way: a parameter and channel's control changes, (table, number, role).
type CcOut = HashMap<(u16, u16), Vec<(u8, u8, u8)>>;

/// The protocol's state: what it has received, and sent.
pub struct Midi {
    pub setup: Setup,
    /// Scene (0-99, 100 no assign) of program changes 0-127.
    pub pc_table: [u8; 128],
    /// The key remote's layer: 0-3 the input layers, 4 MASTER, 5-8 the remote layers.
    pub layer: u8,
    /// The last MMC responses seen (SIGNATURE, TALLY, RECORD STATUS, TRACK READY): their sub-ID.
    pub mmc_last: Option<u8>,
    /// MMC device id of the commands sent (`7F` all-call).
    pub mmc_device: u8,
    fields: Vec<Option<(Group, Field)>>,
    /// (tt, ee, pp) to the list.
    index: HashMap<(u8, u8, u8), u16>,
    /// The poll's slots: a parameter and channel transmitted.
    slots: Vec<(u16, u16)>,
    base: Vec<Option<usize>>,
    last: Vec<Option<i32>>,
    /// Control change table: a parameter and its role (0 single, 1 high, 2 low) for each.
    cc_in: CcIn,
    /// Back the other way: the control changes of a parameter and channel.
    cc_out: CcOut,
    out: VecDeque<Vec<u8>>,
    events: VecDeque<Event>,
    meters: Vec<MeterJob>,
    primed: bool,
}

impl Default for Midi {
    fn default() -> Self {
        let fields: Vec<_> = DEFS.iter().map(field_of).collect();
        let mut index = HashMap::new();
        let mut base = vec![None; DEFS.len()];
        let mut slots = Vec::new();
        for (i, d) in DEFS.iter().enumerate() {
            index.insert((d.tt, d.ee, d.pp), i as u16);
            if d.tx && fields[i].is_some() {
                base[i] = Some(slots.len());
                for ch in 0..=d.max_ch {
                    slots.push((i as u16, ch));
                }
            }
        }
        let (cc_in, cc_out) = cc_tables(&fields);
        let last = vec![None; slots.len()];
        // Program changes 1-99 scenes 1-99, 100 scene 0, the rest no assign (p.352).
        let mut pc_table = [100u8; 128];
        for (i, v) in pc_table.iter_mut().enumerate().take(99) {
            *v = (i + 1) as u8;
        }
        pc_table[99] = 0;
        Self {
            setup: Setup::default(),
            pc_table,
            layer: 0,
            mmc_last: None,
            mmc_device: 0x7F,
            fields,
            index,
            slots,
            base,
            last,
            cc_in,
            cc_out,
            out: VecDeque::new(),
            events: VecDeque::new(),
            meters: Vec::new(),
            primed: false,
        }
    }
}

/// The def (and channel) a table entry names: faders, ON, PAN, EQ ATT, EQ ON.
fn cc_tables(fields: &[Option<(Group, Field)>]) -> (CcIn, CcOut) {
    use Field::*;
    let find = |g: Group, f: Field| {
        DEFS.iter().enumerate().position(|(i, d)| group_of(d.element) == g && fields[i] == Some((g, f))).map(|i| i as u16)
    };
    let mut cc_in = vec![vec![None; 120]; 16];
    let mut cc_out: CcOut = HashMap::new();
    for (table, row) in CC_INITIAL.iter().enumerate() {
        for (n, &CcAssign { kind, target }) in row.iter().enumerate() {
            if kind == 0 {
                continue;
            }
            let t = target as usize;
            let (g, ch) = match t {
                0..=95 => (Group::Input, t),
                96..=103 => (Group::Bus, t - 96),
                104..=115 => (Group::Aux, t - 104),
                116..=119 => (Group::Matrix, (t - 116) * 2),
                _ => (Group::Stereo, 0),
            };
            let (field, role) = match kind {
                1 => (Fader, 1),
                2 => (Fader, 2),
                3 => (On, 0),
                4 => (Pan, 0),
                5 => (Att, 1),
                6 => (Att, 2),
                _ => (EqOn, 0),
            };
            let Some(def) = find(g, field) else { continue };
            cc_in[table][n] = Some((def, ch as u16, role));
            cc_out.entry((def, ch as u16)).or_default().push((table as u8, n as u8, role));
        }
    }
    (cc_in, cc_out)
}

/// The manual's value scaling (p.370) of parameter `def`: (add, mod) and its width.
fn scale(d: &Def) -> (i32, i32, i32) {
    let steps = d.max - d.min + 1;
    let width = if steps <= 128 { 127 } else { 16383 };
    let add = width / steps;
    (add, width - add * steps, width)
}

/// The list's parameter value as a 4-byte data field (a 28-bit two's complement).
fn data_bytes(v: i32) -> [u8; 4] {
    let u = (v as u32) & 0x0FFF_FFFF;
    [(u >> 21) as u8 & 0x7F, (u >> 14) as u8 & 0x7F, (u >> 7) as u8 & 0x7F, u as u8 & 0x7F]
}

fn data_value(b: &[u8]) -> i32 {
    let u = (b[0] as u32) << 21 | (b[1] as u32) << 14 | (b[2] as u32) << 7 | b[3] as u32;
    (u << 4) as i32 >> 4
}

impl Midi {
    pub fn take_out(&mut self) -> Option<Vec<u8>> {
        self.out.pop_front()
    }

    /// A message the caller couldn't take yet (its buffer was too small).
    pub fn put_back(&mut self, msg: Vec<u8>) {
        self.out.push_front(msg);
    }

    pub fn take_event(&mut self) -> Option<Event> {
        self.events.pop_front()
    }

    /// Queues an MMC command (`F0 7F id 06 cc F7`): DM2000MMC.xls's transmit list.
    pub fn mmc(&mut self, command: u8) {
        self.out.push_back(vec![0xF0, 0x7F, self.mmc_device, 0x06, command, 0xF7]);
    }

    /// Scene `n` (0-99) was recalled on the console: its program change goes out
    /// (the lowest assigned), else the function call if the table doesn't have it (p.379).
    pub fn scene_recalled(&mut self, n: u16) {
        if self.setup.tx[PGM] {
            if let Some(pc) = self.pc_table.iter().position(|&s| s as u16 == n) {
                self.out.push_back(vec![0xC0 | self.setup.tx_channel, pc as u8]);
                return;
            }
        }
        if self.setup.tx[PRM] && n <= 99 {
            let msg = vec![0xF0, 0x43, 0x10 | self.setup.tx_channel, GROUP, UNIVERSAL, 0x10, 0x00, 0, n as u8, 0x02, 0x00, 0xF7];
            self.out.push_back(msg);
        }
    }

    /// Handles one received MIDI message on the console.
    pub fn receive(&mut self, engine: &Engine, msg: &[u8]) {
        let Some(c) = Console::new(engine) else { return };
        match msg.first().copied() {
            Some(0xF0) => self.sysex(&c, msg),
            Some(s) if s & 0xF0 == 0xB0 && msg.len() >= 3 => self.control_change(&c, s & 0x0F, msg[1], msg[2], msg),
            Some(s) if s & 0xF0 == 0xC0 && msg.len() >= 2 => self.program_change(s & 0x0F, msg[1], msg),
            _ => {}
        }
    }

    fn program_change(&mut self, ch: u8, program: u8, msg: &[u8]) {
        if !self.setup.rx[PGM] || (ch != self.setup.rx_channel && !self.setup.omni[PGM]) {
            return;
        }
        if self.setup.echo[PGM] {
            self.out.push_back(msg.to_vec());
        }
        let scene = self.pc_table.get(program as usize).copied().unwrap_or(100);
        if scene <= 99 {
            self.events.push_back(Event::RecallScene(scene as u16));
        }
    }

    fn control_change(&mut self, c: &Console, ch: u8, cc: u8, value: u8, msg: &[u8]) {
        if !self.setup.rx[CTL] {
            return;
        }
        if self.setup.echo[CTL] {
            self.out.push_back(msg.to_vec());
        }
        if self.setup.nrpn {
            return; // Yamaha doesn't publish the NRPN numbers
        }
        let Some(&Some((def, ch_num, role))) = self.cc_in.get(ch as usize).and_then(|t| t.get(cc as usize)) else {
            return;
        };
        let (add, modv, width) = scale(&DEFS[def as usize]);
        let d = &DEFS[def as usize];
        let current = self.read(c, def, ch_num).unwrap_or(d.min);
        let cur = (current - d.min) * add + modv / 2;
        let rx = match role {
            1 => (value as i32) * 128 + (cur & 127),
            2 => (cur & 16256) + value as i32,
            _ => value as i32,
        };
        let rx = rx.min(width);
        let param = ((rx - modv / 2) / add).clamp(0, d.max - d.min);
        self.apply(c, def, ch_num, d.min + param);
    }

    fn sysex(&mut self, c: &Console, msg: &[u8]) {
        // MMC responses: DM2000MMC.xls "MMC Command DM2000 Recognize".
        if msg.len() >= 6 && msg[1] == 0x7F && msg[3] == 0x07 {
            self.mmc_last = Some(msg[4]);
            if self.setup.echo[OTHER] {
                self.out.push_back(msg.to_vec());
            }
            return;
        }
        if msg.len() < 8 || msg[1] != 0x43 || msg[3] != GROUP || !(msg[4] == MODEL || msg[4] == UNIVERSAL) {
            return;
        }
        let sub = msg[2] & 0xF0;
        if sub != 0x10 && sub != 0x30 {
            return;
        }
        if !self.setup.rx[PRM] || (msg[2] & 0x0F != self.setup.rx_channel && !self.setup.omni[PRM]) {
            return;
        }
        if self.setup.echo[PRM] {
            self.out.push_back(msg.to_vec());
        }
        let request = sub == 0x30;
        match (msg[5], msg.len()) {
            (0x01..=0x04, _) => self.parameter(c, msg, request),
            (0x10, 12) if !request => self.function(msg),
            (0x20, 11) if !request => self.key(c, msg[6], msg[7], msg[8], msg[9] != 0),
            (0x21, 12) if request => self.meter_request(msg),
            _ => {}
        }
    }

    fn parameter(&mut self, c: &Console, msg: &[u8], request: bool) {
        let (tt, ee, pp, ch) = (msg[5], msg[6], msg[7], msg[8] as u16);
        let Some(&i) = self.index.get(&(tt, ee, pp)) else { return };
        let d = &DEFS[i as usize];
        if ch > d.max_ch {
            return;
        }
        if request {
            if msg.len() != 10 {
                return;
            }
            if let Some(v) = self.read(c, i, ch) {
                let device = self.setup.rx_channel;
                self.send_change(i, ch, v, device);
            }
        } else {
            if msg.len() != 14 || !d.rx {
                return;
            }
            self.apply(c, i, ch, data_value(&msg[9..13]).clamp(d.min, d.max));
        }
    }

    /// `F0 43 1n 3E 7F 10 ff mh ml ch cl F7`: scene recall and store.
    fn function(&mut self, msg: &[u8]) {
        let number = (msg[7] as u16) << 7 | msg[8] as u16;
        match msg[6] {
            0x00 if number <= 99 => self.events.push_back(Event::RecallScene(number)),
            0x20 if (1..=99).contains(&number) => self.events.push_back(Event::StoreScene(number)),
            _ => {}
        }
    }

    fn key(&mut self, c: &Console, h: u8, m: u8, l: u8, press: bool) {
        self.events.push_back(Event::Key { h, m, l, press });
        if !press || h != 0 {
            return;
        }
        match (m, l) {
            // LAYER 1-24 ... REMOTE4.
            (0x10, 0..=8) => self.layer = l,
            // ON 1-24, ON STEREO: the fader's channel ON on the layer.
            (0x00, _) => {
                if let Some(s) = self.fader_strip(c, l) {
                    let mute = s.params.mute.load(Relaxed);
                    s.params.mute.store(!mute, Relaxed);
                }
            }
            (0x01, _) => {
                if let Some(s) = self.fader_strip(c, l) {
                    let solo = s.params.solo.load(Relaxed);
                    s.params.solo.store(!solo, Relaxed);
                }
            }
            _ => {}
        }
    }

    /// The strip under channel strip `l` (0-23; 24 the STEREO key).
    fn fader_strip<'c>(&self, c: &'c Console, l: u8) -> Option<&'c Arc<Strip>> {
        if l == 0x18 {
            return c.mains.first();
        }
        if l >= 24 {
            return None;
        }
        match self.layer {
            0..=3 => c.inputs.get(self.layer as usize * 24 + l as usize),
            LAYER_MASTER => match l as usize {
                n @ 0..=7 => c.buses.get(n),
                n @ 8..=19 => c.buses.get(BUSES + n - 8),
                n => c.matrices.get(n - 20),
            },
            _ => None,
        }
    }

    /// `F0 43 3n 3E 06 21 g t c cH cL F7`: starts (or with g 7F, stops) meter data.
    fn meter_request(&mut self, msg: &[u8]) {
        let (group, kind, channel) = (msg[6], msg[7], msg[8]);
        if group == 0x7F {
            self.meters.clear();
            return;
        }
        let count = (msg[9] as u16) << 7 | msg[10] as u16;
        if let Some(j) = self.meters.iter_mut().find(|j| j.group == group && j.kind == kind && j.channel == channel) {
            j.count = count;
            j.ticks = METER_TICKS;
        } else if self.meters.len() < METER_JOBS {
            self.meters.push(MeterJob { group, kind, channel, count, ticks: METER_TICKS });
        }
    }

    fn apply(&mut self, c: &Console, def: u16, ch: u16, value: i32) {
        self.write(c, def, ch, value);
        // What it holds now, so the poll doesn't echo it.
        if let Some(b) = self.base[def as usize] {
            let slot = b + ch as usize;
            self.last[slot] = self.read(c, def, ch);
        }
    }

    fn read(&self, c: &Console, def: u16, ch: u16) -> Option<i32> {
        let (g, f) = self.fields[def as usize]?;
        use Field::*;
        let ch = ch as usize;
        let on = |b: bool| b as i32;
        // The setup's and the groups' fields first: no strip.
        match f {
            RxChannel => return Some(self.setup.rx_channel as i32),
            TxChannel => return Some(self.setup.tx_channel as i32),
            TxEnable(k) => return Some(on(self.setup.tx[k as usize])),
            RxEnable(k) => return Some(on(self.setup.rx[k as usize])),
            OmniEnable(k) => return Some(on(self.setup.omni[k as usize])),
            EchoEnable(k) => return Some(on(self.setup.echo[k as usize])),
            ControlMode => return Some(on(self.setup.nrpn)),
            FaderResolution => return Some(on(self.setup.fader_low)),
            PcTable => return self.pc_table.get(ch).map(|&s| s as i32),
            GroupMasterFader(b) => {
                return Some(fader_value(&FADER, c.engine.controls.dcas.get(b as usize + ch)?.fader_db.load()))
            }
            GroupMasterOn(b) => return Some(on(!c.engine.controls.dcas.get(b as usize + ch)?.mute.load(Relaxed))),
            MuteMaster(b) => return Some(on(c.engine.controls.mute_groups.contains(b as usize + ch))),
            FxMix => return Some(c.engine.controls.fx.get(ch)?.mix.load().round() as i32),
            FxBypass => return Some(on(c.engine.controls.fx.get(ch)?.bypass.load(Relaxed))),
            FxType => return fx_type_value(c.engine.controls.fx.get(ch)?.model.load(Relaxed)),
            OmniOut => return Some(output_value(c.engine.output_patch().get(dm2000_port_base(f, ch)), false)),
            SlotOut(_) | Digital2tr => return Some(output_value(c.engine.output_patch().get(dm2000_port_base(f, ch)), false)),
            StereoOut => return Some(output_value(c.engine.output_patch().get(dm2000_port_base(f, ch)), true)),
            _ => {}
        }
        let s = c.strip(g, ch)?;
        let p = &s.params;
        Some(match f {
            Phase => on(p.input.invert.load(Relaxed)),
            InsertOn => on(p.pre_insert().on.load(Relaxed)),
            InsertFirst => on(!order_parts(p.process_order.load(Relaxed)).2),
            InsertLoc => order_parts(p.process_order.load(Relaxed)).1,
            CompLoc => order_parts(p.process_order.load(Relaxed)).0,
            On => on(!p.mute.load(Relaxed)),
            Pan => pan_value(p.pan.load()),
            Fader => fader_value(fader_table(g), p.fader_db.load()),
            Att => (p.input.trim_db.load() * 10.0).round() as i32,
            GateOn => on(p.gate().on.load(Relaxed)),
            GateType => {
                let t = p.gate().model_spec()?.token;
                GATE_TYPES.iter().position(|&x| x == t)? as i32
            }
            GateAttack => p.gate().values[2].load().round() as i32,
            GateRange => p.gate().values[1].load().round() as i32,
            GateHold => p.gate().values[3].load().round() as i32,
            GateDecay => p.gate().values[4].load().round() as i32,
            GateThreshold => (p.gate().values[0].load() * 10.0).round() as i32,
            CompOn => on(p.dynamics().on.load(Relaxed)),
            CompType => {
                let t = p.dynamics().model_spec()?.token;
                COMP_TYPES.iter().position(|&x| x == t)? as i32
            }
            CompAttack => p.dynamics().values[2].load().round() as i32,
            CompRelease => p.dynamics().values[5].load().round() as i32,
            CompRatio => p.dynamics().values[1].load().round() as i32,
            CompGain => (comp_gain_value(p.dynamics().model_spec()?.token, p.dynamics().values[3].load()) * 10.0).round() as i32,
            CompKnee => comp_knee_value(p.dynamics().model_spec()?.token, p.dynamics().values[4].load()),
            CompThreshold => (p.dynamics().values[0].load() * 10.0).round() as i32,
            EqMode => p.eq().values[0].load().round() as i32,
            EqOn => on(p.eq().on.load(Relaxed)),
            EqLowQ => eq_low_q(p.eq().values[1].load().round() as i32),
            EqHighQ => eq_high_q(p.eq().values[10].load().round() as i32),
            EqMidQ(i) => p.eq().values[i as usize].load().round() as i32,
            EqFreq(i) => p.eq().values[i as usize].load().round() as i32 + 5,
            EqGain(i) => (p.eq().values[i as usize].load() * 10.0).round() as i32,
            EqSwitch(i) => p.eq().values[i as usize].load().round() as i32,
            DelayOn => on(p.input.delay_on.load(Relaxed)),
            DelayTime => p.input.delay.load().round() as i32,
            RouteStereo => on(s.main_send(0)?.on.load(Relaxed)),
            RouteBus(i) => on(s.sends.find(c.bus_slot(i as usize)?).is_some_and(|x| x.enabled.load(Relaxed))),
            AuxOn(i) => on(s.sends.find(c.aux_slot(i as usize)?).is_none_or(|x| x.enabled.load(Relaxed))),
            AuxPre(i) => on(s.sends.find(c.aux_slot(i as usize)?).is_some_and(|x| x.mode.load(Relaxed) == send_mode::PRE)),
            AuxLevel(i) => fader_value(&FADER, s.sends.find(c.aux_slot(i as usize)?).map_or(FADER_MIN, |x| x.level_db.load())),
            AuxPan(i) => pan_value(s.sends.find(c.aux_slot(i as usize * 2)?).map_or(0.0, |x| x.pan.load())),
            ToMatrixOn(i) => on(s.matrix_send(i as usize)?.on.load(Relaxed)),
            ToMatrixLevel(i) => fader_value(&FADER, s.matrix_send(i as usize)?.level_db.load()),
            ToMatrixPan(i) => pan_value(s.matrix_send(i as usize)?.pan.load()),
            ToStereoOn => on(s.main_send(0)?.on.load(Relaxed)),
            ToStereoLevel => fader_value(&BUS_FADER, s.main_send(0)?.level_db.load()),
            ToStereoPan => pan_value(p.pan.load()),
            FaderGroup(i) => on(p.dca_mask.contains(i as usize + group_base(g))),
            MuteGroup(i) => on(p.mute_group_mask.contains(i as usize + group_base(g))),
            ChannelIn => patch_of_source(p.input_left.load(Relaxed)),
            Name(i) => {
                let n = s.name();
                n.as_bytes().get(i as usize).map_or(32, |&b| b as i32)
            }
            _ => return None,
        })
    }

    fn write(&mut self, c: &Console, def: u16, ch: u16, v: i32) {
        let Some((g, f)) = self.fields[def as usize] else { return };
        use Field::*;
        let ch = ch as usize;
        match f {
            RxChannel => return self.setup.rx_channel = v.clamp(0, 15) as u8,
            TxChannel => return self.setup.tx_channel = v.clamp(0, 15) as u8,
            TxEnable(k) => return self.setup.tx[k as usize] = v != 0,
            RxEnable(k) => return self.setup.rx[k as usize] = v != 0,
            OmniEnable(k) => return self.setup.omni[k as usize] = v != 0,
            EchoEnable(k) => return self.setup.echo[k as usize] = v != 0,
            ControlMode => return self.setup.nrpn = v != 0,
            FaderResolution => return self.setup.fader_low = v != 0,
            PcTable => {
                if let Some(p) = self.pc_table.get_mut(ch) {
                    *p = v.clamp(0, 100) as u8;
                }
                return;
            }
            GroupMasterFader(b) => {
                if let Some(d) = c.engine.controls.dcas.get(b as usize + ch) {
                    d.fader_db.store(FADER[v.clamp(0, 1023) as usize]);
                }
                return;
            }
            GroupMasterOn(b) => {
                if let Some(d) = c.engine.controls.dcas.get(b as usize + ch) {
                    d.mute.store(v == 0, Relaxed);
                }
                return;
            }
            MuteMaster(b) => return c.engine.controls.mute_groups.set(b as usize + ch, v != 0),
            FxMix => {
                if let Some(x) = c.engine.controls.fx.get(ch) {
                    x.mix.store(v as f32);
                }
                return;
            }
            FxBypass => {
                if let Some(x) = c.engine.controls.fx.get(ch) {
                    x.bypass.store(v != 0, Relaxed);
                }
                return;
            }
            FxType => {
                if let Some(m) = fx_model_of(v) {
                    c.engine.set_fx_model(ch, m, true);
                }
                return;
            }
            OmniOut | SlotOut(_) | Digital2tr => {
                if let Some(s) = output_source(v, false) {
                    c.engine.output_patch().set(dm2000_port_base(f, ch), s);
                }
                return;
            }
            StereoOut => {
                if let Some(s) = output_source(v, true) {
                    c.engine.output_patch().set(dm2000_port_base(f, ch), s);
                }
                return;
            }
            _ => {}
        }
        let Some(s) = c.strip(g, ch) else { return };
        let p = &s.params;
        match f {
            Phase => p.input.invert.store(v != 0, Relaxed),
            InsertOn => p.pre_insert().on.store(v != 0, Relaxed),
            InsertFirst => {
                let (comp, ins, _) = order_parts(p.process_order.load(Relaxed));
                p.process_order.store(order_of(comp, ins, v == 0), Relaxed);
            }
            InsertLoc => {
                let (comp, _, first) = order_parts(p.process_order.load(Relaxed));
                p.process_order.store(order_of(comp, v.clamp(0, 2), first), Relaxed);
            }
            CompLoc => {
                let (_, ins, first) = order_parts(p.process_order.load(Relaxed));
                p.process_order.store(order_of(v.clamp(0, 2), ins, first), Relaxed);
            }
            On => p.mute.store(v == 0, Relaxed),
            Pan => p.pan.store(pan_of(v)),
            Fader => p.fader_db.store(fader_table(g)[v.clamp(0, 1023) as usize]),
            Att => p.input.trim_db.store(v as f32 / 10.0),
            GateOn => p.gate().on.store(v != 0, Relaxed),
            GateType => set_dyn_model(p.gate(), GATE_TYPES.get(v as usize)),
            GateAttack => p.gate().values[2].store(v as f32),
            GateRange => p.gate().values[1].store(v as f32),
            GateHold => p.gate().values[3].store(v as f32),
            GateDecay => p.gate().values[4].store(v as f32),
            GateThreshold => p.gate().values[0].store(v as f32 / 10.0),
            CompOn => p.dynamics().on.store(v != 0, Relaxed),
            CompType => set_dyn_model(p.dynamics(), COMP_TYPES.get(v as usize)),
            CompAttack => p.dynamics().values[2].store(v as f32),
            CompRelease => p.dynamics().values[5].store(v as f32),
            CompRatio => p.dynamics().values[1].store(v as f32),
            CompGain => {
                let Some(spec) = p.dynamics().model_spec() else { return };
                p.dynamics().values[3].store(comp_gain_of(spec.token, v));
            }
            CompKnee => {
                let Some(spec) = p.dynamics().model_spec() else { return };
                p.dynamics().values[4].store(comp_knee_of(spec.token, v));
            }
            CompThreshold => p.dynamics().values[0].store(v as f32 / 10.0),
            EqMode => p.eq().values[0].store(v as f32),
            EqOn => p.eq().on.store(v != 0, Relaxed),
            EqLowQ => p.eq().values[1].store(eq_low_q_of(v) as f32),
            EqHighQ => p.eq().values[10].store(eq_high_q_of(v) as f32),
            EqMidQ(i) => p.eq().values[i as usize].store(v as f32),
            EqFreq(i) => p.eq().values[i as usize].store((v - 5).clamp(0, 119) as f32),
            EqGain(i) => p.eq().values[i as usize].store(v as f32 / 10.0),
            EqSwitch(i) => p.eq().values[i as usize].store(v as f32),
            DelayOn => p.input.delay_on.store(v != 0, Relaxed),
            DelayTime => p.input.delay.store(v as f32),
            RouteStereo | ToStereoOn => {
                if let Some(m) = s.main_send(0) {
                    m.on.store(v != 0, Relaxed);
                }
            }
            RouteBus(i) => {
                if let Some(x) = c.bus_slot(i as usize).and_then(|b| s.sends.get_or_create(b)) {
                    x.enabled.store(v != 0, Relaxed);
                    x.level_db.store(0.0);
                }
            }
            AuxOn(i) => {
                if let Some(x) = c.aux_slot(i as usize).and_then(|b| s.sends.get_or_create(b)) {
                    x.enabled.store(v != 0, Relaxed);
                }
            }
            AuxPre(i) => {
                if let Some(x) = c.aux_slot(i as usize).and_then(|b| s.sends.get_or_create(b)) {
                    x.mode.store(if v != 0 { send_mode::PRE } else { send_mode::POST }, Relaxed);
                }
            }
            AuxLevel(i) => {
                if let Some(x) = c.aux_slot(i as usize).and_then(|b| s.sends.get_or_create(b)) {
                    x.level_db.store(FADER[v.clamp(0, 1023) as usize]);
                }
            }
            AuxPan(i) => {
                for side in 0..2 {
                    if let Some(x) = c.aux_slot(i as usize * 2 + side).and_then(|b| s.sends.get_or_create(b)) {
                        x.pan.store(pan_of(v));
                    }
                }
            }
            ToMatrixOn(i) => {
                if let Some(m) = s.matrix_send(i as usize) {
                    m.on.store(v != 0, Relaxed);
                }
            }
            ToMatrixLevel(i) => {
                if let Some(m) = s.matrix_send(i as usize) {
                    m.level_db.store(FADER[v.clamp(0, 1023) as usize]);
                }
            }
            ToMatrixPan(i) => {
                if let Some(m) = s.matrix_send(i as usize) {
                    m.pan.store(pan_of(v));
                }
            }
            ToStereoLevel => {
                if let Some(m) = s.main_send(0) {
                    m.level_db.store(BUS_FADER[v.clamp(0, 1023) as usize]);
                }
            }
            ToStereoPan => p.pan.store(pan_of(v)),
            FaderGroup(i) => p.dca_mask.set(i as usize + group_base(g), v != 0),
            MuteGroup(i) => p.mute_group_mask.set(i as usize + group_base(g), v != 0),
            ChannelIn => {
                if v == 0 {
                    p.input_left.store(-1, Relaxed);
                } else if let Some(source) = ports_from_patch(v) {
                    p.input_left.store(source, Relaxed);
                }
            }
            Name(i) => {
                let mut name: Vec<u8> = s.name().into_bytes();
                let i = i as usize;
                if name.len() <= i {
                    name.resize(i + 1, b' ');
                }
                name[i] = v.clamp(32, 122) as u8;
                let text = String::from_utf8_lossy(&name).trim_end().to_string();
                s.set_name(&text);
            }
            _ => {}
        }
    }

    /// Queues the console's changes since the last poll (the first takes the
    /// console's values and sends nothing), and the running meter data.
    pub fn poll(&mut self, engine: &Engine) {
        let Some(c) = Console::new(engine) else { return };
        for i in 0..self.slots.len() {
            let (def, ch) = self.slots[i];
            let now = self.read(&c, def, ch);
            if now != self.last[i] {
                self.last[i] = now;
                if let (true, Some(v)) = (self.primed, now) {
                    self.transmit(def, ch, v);
                }
            }
        }
        self.primed = true;
        self.send_meters(&c);
    }

    /// Sends every value again (the next poll sends what differs from nothing).
    pub fn resend_all(&mut self) {
        self.last.fill(None);
    }

    /// A console change: a control change where the table assigns one (and
    /// control change TX is on), else a parameter change (p.380).
    fn transmit(&mut self, def: u16, ch: u16, value: i32) {
        let d = &DEFS[def as usize];
        let value = self.resolution(d, value);
        if !self.setup.nrpn && self.setup.tx[CTL] {
            if let Some(routes) = self.cc_out.get(&(def, ch)).cloned() {
                let (add, modv, _) = scale(d);
                let cur = (value - d.min) * add + modv / 2;
                for (table, cc, role) in routes {
                    let v = match role {
                        1 => (cur >> 7) as u8 & 0x7F,
                        2 => cur as u8 & 0x7F,
                        _ => cur as u8 & 0x7F,
                    };
                    self.out.push_back(vec![0xB0 | table, cc, v]);
                }
                return;
            }
        }
        if self.setup.tx[PRM] {
            let device = self.setup.tx_channel;
            self.send_change(def, ch, value, device);
        }
    }

    /// FADER LOW (Sidecar's reading of the resolution switch): the 10-bit value
    /// snapped to fours, the top kept.
    fn resolution(&self, d: &Def, value: i32) -> i32 {
        if self.setup.fader_low && d.max == 1023 && value < 1023 {
            value & !3
        } else {
            value
        }
    }

    fn send_change(&mut self, def: u16, ch: u16, value: i32, device: u8) {
        let d = &DEFS[def as usize];
        let model = if d.universal { UNIVERSAL } else { MODEL };
        let v = data_bytes(value);
        let msg = vec![0xF0, 0x43, 0x10 | device, GROUP, model, d.tt, d.ee, d.pp, ch as u8, v[0], v[1], v[2], v[3], 0xF7];
        self.out.push_back(msg);
    }

    fn send_meters(&mut self, c: &Console) {
        if self.meters.is_empty() {
            return;
        }
        let jobs = std::mem::take(&mut self.meters);
        for j in &jobs {
            if let Some(data) = meter_data(c, j) {
                let mut msg = vec![0xF0, 0x43, 0x10 | self.setup.rx_channel, GROUP, MODEL, 0x21, j.group, j.kind, j.channel];
                for v in data {
                    msg.push((v >> 7) as u8 & 0x7F);
                    msg.push(v as u8 & 0x7F);
                }
                msg.push(0xF7);
                self.out.push_back(msg);
            }
        }
        self.meters = jobs
            .into_iter()
            .filter_map(|mut j| {
                j.ticks = j.ticks.saturating_sub(1);
                (j.ticks > 0).then_some(j)
            })
            .collect();
    }
}

fn group_base(g: Group) -> usize {
    match g {
        Group::Input => 0,
        _ => 8,
    }
}

fn dm2000_port_base(f: Field, ch: usize) -> i32 {
    use dm2000_ports::{DIGITAL_2TR, OMNI, SLOT};
    match f {
        Field::SlotOut(i) => SLOT + 16 * ch as i32 + i as i32,
        Field::OmniOut => OMNI + ch as i32,
        Field::Digital2tr => DIGITAL_2TR + ch as i32,
        _ => ch as i32, // STEREO OUT
    }
}

fn eq_low_q(engine: i32) -> i32 {
    match engine {
        0 => 44,
        42 => 41,
        i => i - 1,
    }
}

fn eq_low_q_of(v: i32) -> i32 {
    match v {
        44 => 0,
        41 => 42,
        i => (i + 1).clamp(0, 42),
    }
}

fn eq_high_q(engine: i32) -> i32 {
    match engine {
        0 => 43,
        42 => 42,
        i => i - 1,
    }
}

fn eq_high_q_of(v: i32) -> i32 {
    match v {
        43 => 0,
        42 => 42,
        i => (i + 1).clamp(0, 42),
    }
}

fn is_compander(token: &str) -> bool {
    token == "DMCPH" || token == "DMCPS"
}

/// `kCompGain` is the output gain in dB (PRM TABLE #09: 0.0 to +18.0, or for a
/// compander -18.0 to 0.0).
fn comp_gain_value(token: &str, db: f32) -> f32 {
    if is_compander(token) {
        db + 18.0
    } else {
        db
    }
}

fn comp_gain_of(token: &str, v: i32) -> f32 {
    let db = v as f32 / 10.0;
    if is_compander(token) {
        db - 18.0
    } else {
        db
    }
}

/// `kCompKnee` (PRM TABLE #10): KNEE 0 HARD - 5, or for a compander WIDTH 1-90 dB as 0-89.
fn comp_knee_value(token: &str, x: f32) -> i32 {
    if is_compander(token) {
        x.round() as i32 - 1
    } else {
        x.round() as i32
    }
}

fn comp_knee_of(token: &str, v: i32) -> f32 {
    if is_compander(token) {
        v as f32 + 1.0
    } else {
        v.min(5) as f32
    }
}

fn set_dyn_model(slot: &DynSlotParams, token: Option<&&'static str>) {
    if let Some(&token) = token {
        if let Some(i) = model_index(slot.table(), token) {
            slot.set_model(i);
        }
    }
}

fn fx_type_value(model: i32) -> Option<i32> {
    let m = model as usize;
    if (FX_FIRST..FX_FIRST + FX_STEREO_TYPES).contains(&m) {
        Some((m - FX_FIRST) as i32)
    } else if m == FX_MBAND_MODEL {
        Some(FX_MBAND_VALUE)
    } else {
        None
    }
}

fn fx_model_of(v: i32) -> Option<usize> {
    if (0..FX_STEREO_TYPES as i32).contains(&v) {
        Some(FX_FIRST + v as usize)
    } else if v == FX_MBAND_VALUE {
        Some(FX_MBAND_MODEL)
    } else {
        None
    }
}

/// The meters of one job (`*1)` tables: the sheet's meter types per channel, or all).
fn meter_data(c: &Console, j: &MeterJob) -> Option<Vec<u16>> {
    let (group, channels, kinds): (Group, usize, u8) = match j.group {
        0 => (Group::Input, CHANNELS, 6),
        1 => (Group::Bus, BUSES, 4),
        2 => (Group::Aux, AUXES, 4),
        3 => (Group::Matrix, MATRICES * 2, 4),
        4 => (Group::Stereo, 2, 4),
        _ => return None,
    };
    let (first, last) = if j.channel == 0x7F {
        (0, channels)
    } else if (j.channel as usize) < channels {
        let n = (j.count as usize).max(1);
        (j.channel as usize, (j.channel as usize + n).min(channels))
    } else {
        return None;
    };
    let mut out = Vec::new();
    for ch in first..last {
        if j.kind == 0x7F {
            for k in 0..kinds {
                out.push(meter_point(c, group, ch, k)?);
            }
        } else if j.kind < kinds {
            out.push(meter_point(c, group, ch, j.kind)?);
        } else {
            return None;
        }
    }
    Some(out)
}

fn meter_point(c: &Console, g: Group, ch: usize, kind: u8) -> Option<u16> {
    let s = c.strip(g, ch)?;
    let m = &s.meters;
    let side = match g {
        Group::Matrix | Group::Stereo => ch % 2,
        _ => 0,
    };
    Some(match kind {
        0 => meter_value(m.output_peak[side].load()),
        1 => meter_value(m.pfl_peak[side].load()),
        2 => meter_value(m.dyn_out_peak.load()),
        3 => meter_of_reduction(m.dyn_reduction_db.load()),
        4 => meter_value(m.gate_out_peak.load()),
        5 => meter_of_reduction(m.gate_reduction_db.load()),
        _ => return None,
    })
}

#[cfg(test)]
mod tests;
