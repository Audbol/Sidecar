//! The X32's parameter tree mapped onto the engine.
//!
//! Every address the server knows is an [`Entry`] (its path, its type and
//! where its value lives), grouped in [`Node`]s as `/node` lists them
//! (`/ch/01/mix`: on, fader, st, pan, mono, mlevel). The strips: X32 channel
//! n is engine channel n, aux in 1-8 are engine aux slots 1-8 and FX returns
//! 1-8 aux slots 9-16, buses 1-16 the bus slots, matrices 1-6 the matrices,
//! main LR main 1 and M/C main 2.
//!
//! Gate, dynamics and EQ parameters are the X32 models' (`livedaw_dsp::x32`).
//! What the engine has no equivalent for yet (icons, sources, key sources and
//! filters, send tap types, the dynamics and insert positions, channel links,
//! automix) the server keeps itself and answers back; so does an address
//! whose strip the engine doesn't have.

use crate::value::{Fmt, Law, Ty, Val};
use livedaw_dsp::delay::{delay_to_ms, ms_to_delay};
use livedaw_dsp::fader::FADER_MIN;
use livedaw_engine::params::DynSlotParams;
use livedaw_engine::sends::send_mode;
use livedaw_engine::strip::tap;
use livedaw_engine::SendParams;
use livedaw_engine::{Engine, Strip, StripKind};
use std::collections::HashMap;
use std::sync::atomic::Ordering::Relaxed;
use std::sync::{Arc, Mutex, OnceLock};

/// What an address belongs to (indexes from 0).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Target {
    Ch(usize),
    AuxIn(usize),
    FxRtn(usize),
    Bus(usize),
    Mtx(usize),
    MainSt,
    MainM,
    Dca(usize),
    Config,
}

impl Target {
    /// The strip's id in the meter requests and `/-stat/selidx` (0-71).
    pub fn meter_id(self) -> Option<usize> {
        Some(match self {
            Target::Ch(i) => i,
            Target::AuxIn(i) => 32 + i,
            Target::FxRtn(i) => 40 + i,
            Target::Bus(i) => 48 + i,
            Target::Mtx(i) => 64 + i,
            Target::MainSt => 70,
            Target::MainM => 71,
            Target::Dca(_) | Target::Config => return None,
        })
    }

    /// The strip with meter id `id`.
    pub fn of_meter_id(id: usize) -> Option<Target> {
        Some(match id {
            0..=31 => Target::Ch(id),
            32..=39 => Target::AuxIn(id - 32),
            40..=47 => Target::FxRtn(id - 40),
            48..=63 => Target::Bus(id - 48),
            64..=69 => Target::Mtx(id - 64),
            70 => Target::MainSt,
            71 => Target::MainM,
            _ => return None,
        })
    }
}

/// Where a value lives.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Bind {
    /// The server's own state.
    State,
    Name,
    Colour,
    Fader,
    /// `mix/on`: 1 is unmuted.
    On,
    Pan,
    /// A send to main `n` (LR is main 0, M/C main 1).
    MainOn(usize),
    MainLevel(usize),
    /// A send to bus slot `n`.
    SendOn(usize),
    SendLevel(usize),
    SendPan(usize),
    SendFollow(usize),
    /// The tap point of the sends to bus pair `n`, `n + 1` (IN/LC, <-EQ, EQ->,
    /// PRE, POST, GRP).
    SendType(usize),
    /// A channel's compressor and insert PRE / POST (its process order).
    DynPos,
    InsertPos,
    /// A send to matrix `n`.
    MtxOn(usize),
    MtxLevel(usize),
    MtxPan(usize),
    MtxFollow(usize),
    Dcas,
    MuteGroups,
    Trim,
    Invert,
    LowCut,
    LowCutSlope,
    LowCutFreq,
    DelayOn,
    DelayTime,
    GateOn,
    GateMode,
    Gate(&'static str),
    DynOn,
    DynMode,
    Dyn(&'static str),
    DynMix,
    EqOn,
    Eq(&'static str),
    InsertOn,
    /// The insert's FX slot in the engine, the side and AUX loops kept.
    InsertSel,
    DcaOn,
    DcaFader,
    DcaName,
    DcaColour,
    /// `/config/mute/n`: mute group n engaged.
    MuteGroup(usize),
}

/// One address.
#[derive(Clone, Debug)]
pub struct Entry {
    pub path: String,
    pub target: Target,
    pub ty: Ty,
    pub bind: Bind,
    /// The X32's factory value (answered while the engine has no such strip).
    pub default: Val,
    /// In its node's `/node` line (a bus's `dyn/keysrc` answers a get but isn't).
    pub listed: bool,
}

/// A group of addresses, as `/node` lists them.
#[derive(Clone, Debug, Default)]
pub struct Node {
    pub path: String,
    pub leaves: Vec<usize>,
    /// Sub-nodes (their index in [`Tree::nodes`]).
    pub children: Vec<usize>,
}

/// The whole tree.
#[derive(Debug, Default)]
pub struct Tree {
    pub entries: Vec<Entry>,
    pub nodes: Vec<Node>,
    by_path: HashMap<String, usize>,
    node_by_path: HashMap<String, usize>,
}

/// What an address names.
#[derive(Clone, Copy, Debug)]
pub enum Found<'a> {
    Leaf(usize),
    Node(&'a Node),
}

// ---- types ------------------------------------------------------------------------------

const fn num(dec: u8, sign: bool, width: u8) -> Fmt {
    Fmt::Num { dec, sign, width }
}

const ONOFF: Ty = Ty::Enum(&["OFF", "ON"]);
pub const COLOURS: &[&str] =
    &["OFF", "RD", "GN", "YE", "BL", "MG", "CY", "WH", "OFFi", "RDi", "GNi", "YEi", "BLi", "MGi", "CYi", "WHi"];
const COLOUR: Ty = Ty::Enum(COLOURS);
const NAME: Ty = Ty::Str(12);
const ICON: Ty = Ty::Int(1, 74);
/// OFF, In01-32, Aux 1-6, USB L/R, Fx 1L-4R, Bus 01-16.
const SOURCE: Ty = Ty::Int(0, 64);
const FADER: Ty = Ty::Float(Law::Level(1024), Fmt::Level);
const LEVEL: Ty = Ty::Float(Law::Level(161), Fmt::Level);
const PAN: Ty = Ty::Float(Law::Lin(-100.0, 100.0, 101), num(0, true, 0));
const TRIM: Ty = Ty::Float(Law::Lin(-18.0, 18.0, 145), num(1, true, 0));
const HP_SLOPE: Ty = Ty::Enum(&["12", "18", "24"]);
const HP_FREQ: Ty = Ty::Float(Law::Log(20.0, 400.0, 101), num(0, false, 3));
/// 0.3 to 500 ms in 0.1 ms steps.
const DELAY: Ty = Ty::Float(Law::Lin(0.3, 500.0, 4998), num(1, false, 5));
const GATE_MODE: Ty = Ty::Enum(&["EXP2", "EXP3", "EXP4", "GATE", "DUCK"]);
const GATE_THR: Ty = Ty::Float(Law::Lin(-80.0, 0.0, 161), num(1, false, 0));
const RANGE: Ty = Ty::Float(Law::Lin(3.0, 60.0, 58), num(1, false, 0));
const ATTACK: Ty = Ty::Float(Law::Lin(0.0, 120.0, 121), num(0, false, 0));
const HOLD: Ty = Ty::Float(Law::Log(0.02, 2000.0, 101), Fmt::Time { width: 0 });
const RELEASE: Ty = Ty::Float(Law::Log(5.0, 4000.0, 101), Fmt::Time { width: 4 });
const KEY_FILTER: Ty = Ty::Enum(&["LC6", "LC12", "HC6", "HC12", "1.0", "2.0", "3.0", "5.0", "10.0"]);
const FREQ: Ty = Ty::Float(Law::Log(20.0, 20000.0, 201), Fmt::Freq);
const DYN_MODE: Ty = Ty::Enum(&["COMP", "EXP"]);
const DETECT: Ty = Ty::Enum(&["PEAK", "RMS"]);
const ENVELOPE: Ty = Ty::Enum(&["LIN", "LOG"]);
const DYN_THR: Ty = Ty::Float(Law::Lin(-60.0, 0.0, 121), num(1, false, 0));
const RATIO: Ty = Ty::Enum(&["1.1", "1.3", "1.5", "2.0", "2.5", "3.0", "4.0", "5.0", "7.0", "10", "20", "100"]);
const KNEE: Ty = Ty::Float(Law::Lin(0.0, 5.0, 6), num(0, false, 0));
const MGAIN: Ty = Ty::Float(Law::Lin(0.0, 24.0, 49), num(2, false, 0));
const POS: Ty = Ty::Enum(&["PRE", "POST"]);
/// What a link can share (`/config/linkcfg`, in its order).
const LINK_SHARES: [&str; 4] = ["hadly", "eq", "dyn", "fdrmute"];
const MIX: Ty = Ty::Float(Law::Lin(0.0, 100.0, 21), num(0, false, 0));
pub const INSERTS: &[&str] = &[
    "OFF", "FX1L", "FX1R", "FX2L", "FX2R", "FX3L", "FX3R", "FX4L", "FX4R", "FX5L", "FX5R", "FX6L", "FX6R", "FX7L", "FX7R",
    "FX8L", "FX8R", "AUX1", "AUX2", "AUX3", "AUX4", "AUX5", "AUX6",
];
const INSERT: Ty = Ty::Enum(INSERTS);
const EQ_TYPE: Ty = Ty::Enum(livedaw_dsp::x32::EQ_TYPES);
const EQ_GAIN: Ty = Ty::Float(Law::Lin(-15.0, 15.0, 121), num(2, true, 0));
const EQ_Q: Ty = Ty::Float(Law::Log(10.0, 0.3, 72), num(1, false, 0));
const SEND_TYPE: Ty = Ty::Enum(&["IN/LC", "<-EQ", "EQ->", "PRE", "POST", "GRP"]);
const MTX_SEND_TYPE: Ty = Ty::Enum(&["IN/LC", "<-EQ", "EQ->", "PRE", "POST"]);
const FOLLOW: Ty = Ty::Int(0, 1);
const AUTOMIX: Ty = Ty::Enum(&["OFF", "X", "Y"]);
const WEIGHT: Ty = Ty::Float(Law::Lin(-12.0, 12.0, 49), num(1, true, 5));

/// The gate's modes and the dynamics' as the X32 models' tokens.
const GATE_MODELS: [&str; 5] = ["X32EXP2", "X32EXP3", "X32EXP4", "X32GATE", "X32DUCK"];
const DYN_MODELS: [&str; 2] = ["X32COMP", "X32EXP"];
/// The EQ models' parameter ids by band: type, frequency, gain, Q.
const EQ_IDS: [[&str; 4]; 6] = [
    ["1t", "1f", "1g", "1q"],
    ["2t", "2f", "2g", "2q"],
    ["3t", "3f", "3g", "3q"],
    ["4t", "4f", "4g", "4q"],
    ["5t", "5f", "5g", "5q"],
    ["6t", "6f", "6g", "6q"],
];

// ---- the tree -----------------------------------------------------------------------------

impl Tree {
    /// The X32's tree (built once).
    pub fn get() -> &'static Tree {
        static T: OnceLock<Tree> = OnceLock::new();
        T.get_or_init(build)
    }

    /// An address: with or without its leading `/`, a trailing `/` ignored.
    pub fn find(&self, addr: &str) -> Option<Found<'_>> {
        let a = addr.trim_end_matches('/');
        let a = if a.starts_with('/') { a.to_owned() } else { format!("/{a}") };
        if let Some(&i) = self.by_path.get(&a) {
            return Some(Found::Leaf(i));
        }
        self.node_by_path.get(&a).map(|&n| Found::Node(&self.nodes[n]))
    }

    /// `node` or its first sub-node (and so on) that has values.
    pub fn first_group<'a>(&'a self, mut node: &'a Node) -> Option<&'a Node> {
        while node.leaves.is_empty() {
            node = &self.nodes[*node.children.first()?];
        }
        Some(node)
    }

    /// The values an address names, in order: itself, or its node's (or its
    /// first sub-node's) as `/node` lists them.
    pub fn group(&self, found: Found<'_>) -> Vec<usize> {
        match found {
            Found::Leaf(i) => vec![i],
            Found::Node(n) => self
                .first_group(n)
                .map(|n| n.leaves.iter().copied().filter(|&i| self.entries[i].listed).collect())
                .unwrap_or_default(),
        }
    }

    /// The `/node` line of an address (without its line feed): the path, then
    /// each value in real units.
    pub fn node_line(&self, c: &Console, found: Found<'_>) -> Option<String> {
        let path = match found {
            Found::Leaf(i) => &self.entries[i].path,
            Found::Node(n) => &self.first_group(n)?.path,
        };
        let mut line = path.clone();
        for i in self.group(found) {
            line.push(' ');
            line.push_str(&self.entries[i].ty.text(&c.get(i)));
        }
        Some(line)
    }
}

struct Builder {
    t: Tree,
}

impl Builder {
    fn node(&mut self, path: &str) -> usize {
        if let Some(&n) = self.t.node_by_path.get(path) {
            return n;
        }
        let n = self.t.nodes.len();
        self.t.nodes.push(Node { path: path.to_owned(), ..Node::default() });
        self.t.node_by_path.insert(path.to_owned(), n);
        if let Some(cut) = path.rfind('/').filter(|&c| c > 0) {
            let parent = self.node(&path[..cut]);
            self.t.nodes[parent].children.push(n);
        }
        n
    }

    #[allow(clippy::too_many_arguments)]
    fn add(&mut self, node: &str, name: &str, target: Target, ty: Ty, bind: Bind, default: Val, listed: bool) {
        let n = self.node(node);
        let path = format!("{node}/{name}");
        let i = self.t.entries.len();
        self.t.entries.push(Entry { path: path.clone(), target, ty, bind, default, listed });
        self.t.nodes[n].leaves.push(i);
        self.t.by_path.insert(path, i);
    }

    fn leaf(&mut self, node: &str, name: &str, target: Target, ty: Ty, bind: Bind, default: Val) {
        self.add(node, name, target, ty, bind, default, true);
    }
}

fn int(i: i32) -> Val {
    Val::Int(i)
}

fn real(v: f32) -> Val {
    Val::Real(v)
}

/// The strip kinds' tree differences.
#[derive(Clone, Copy, PartialEq)]
enum Kind {
    Ch,
    AuxIn,
    FxRtn,
    Bus,
    Mtx,
    MainSt,
    MainM,
}

fn build() -> Tree {
    let mut b = Builder { t: Tree::default() };
    for i in 0..32 {
        strip(&mut b, &format!("/ch/{:02}", i + 1), Target::Ch(i), Kind::Ch);
    }
    for i in 0..8 {
        strip(&mut b, &format!("/auxin/{:02}", i + 1), Target::AuxIn(i), Kind::AuxIn);
    }
    for i in 0..8 {
        strip(&mut b, &format!("/fxrtn/{:02}", i + 1), Target::FxRtn(i), Kind::FxRtn);
    }
    for i in 0..16 {
        strip(&mut b, &format!("/bus/{:02}", i + 1), Target::Bus(i), Kind::Bus);
    }
    for i in 0..6 {
        strip(&mut b, &format!("/mtx/{:02}", i + 1), Target::Mtx(i), Kind::Mtx);
    }
    strip(&mut b, "/main/st", Target::MainSt, Kind::MainSt);
    strip(&mut b, "/main/m", Target::MainM, Kind::MainM);
    for i in 0..8 {
        let p = format!("/dca/{}", i + 1);
        let t = Target::Dca(i);
        b.leaf(&p, "on", t, ONOFF, Bind::DcaOn, int(1));
        b.leaf(&p, "fader", t, FADER, Bind::DcaFader, real(0.0));
        let c = format!("{p}/config");
        b.leaf(&c, "name", t, NAME, Bind::DcaName, Val::Text(format!("DCA {}", i + 1)));
        b.leaf(&c, "icon", t, ICON, Bind::State, int(70));
        b.leaf(&c, "color", t, COLOUR, Bind::DcaColour, int(7));
    }
    config(&mut b);
    b.t
}

/// `/config`: the mute groups engaged and the odd/even links.
fn config(b: &mut Builder) {
    let t = Target::Config;
    for g in 0..6 {
        b.leaf("/config/mute", &(g + 1).to_string(), t, ONOFF, Bind::MuteGroup(g), int(0));
    }
    // Links pair strips (1-2, 3-4...): a set on one goes to both for what
    // `linkcfg` shares (`Console::set`). Factory: the FX returns and USB
    // playback (aux in 7-8) linked.
    for (node, pairs, on) in [("chlink", 16, 0), ("auxlink", 4, 8), ("fxlink", 4, 0xf), ("buslink", 8, 0), ("mtxlink", 3, 0)] {
        for k in 0..pairs {
            let name = format!("{}-{}", 2 * k + 1, 2 * k + 2);
            b.leaf(&format!("/config/{node}"), &name, t, ONOFF, Bind::State, int((on >> k) & 1));
        }
    }
    // The mono bus's mode and link to LR; the solo / monitor settings (kept by
    // the server: the engine's monitor is the WING's).
    b.leaf("/config/mono", "mode", t, Ty::Enum(&["LR+M", "LCR"]), Bind::State, int(0));
    b.leaf("/config/mono", "link", t, ONOFF, Bind::State, int(0));
    let modes = Ty::Enum(&["PFL", "AFL"]);
    for (name, ty, v) in [
        ("level", LEVEL, real(0.0)),
        ("source", Ty::Enum(&["OFF", "LR", "LR+C", "LRPFL", "LRAFL", "AUX56", "AUX78"]), int(4)),
        ("sourcetrim", Ty::Float(Law::Lin(-18.0, 18.0, 73), num(1, false, 0)), real(0.0)),
        ("chmode", modes, int(0)),
        ("busmode", modes, int(0)),
        ("dcamode", modes, int(0)),
        ("exclusive", ONOFF, int(0)),
        ("followsel", ONOFF, int(0)),
        ("followsolo", ONOFF, int(0)),
        ("dimatt", Ty::Float(Law::Lin(-40.0, 0.0, 41), num(0, false, 0)), real(-20.0)),
        ("dim", ONOFF, int(0)),
        ("mono", ONOFF, int(0)),
        ("delay", ONOFF, int(0)),
        ("delaytime", DELAY, real(0.3)),
        ("masterctrl", ONOFF, int(0)),
        ("mute", ONOFF, int(0)),
        ("dimpfl", ONOFF, int(0)),
    ] {
        b.leaf("/config/solo", name, t, ty, Bind::State, v);
    }
    // What a link shares: preamp and delay, EQ, dynamics (gate and compressor),
    // fader and mute.
    for name in LINK_SHARES {
        b.leaf("/config/linkcfg", name, t, ONOFF, Bind::State, int(1));
    }
}

fn strip(b: &mut Builder, p: &str, t: Target, kind: Kind) {
    let input = matches!(kind, Kind::Ch | Kind::AuxIn | Kind::FxRtn);
    // config: name, icon, colour (and the source of channels and aux in).
    let (icon, colour, source) = match (kind, t) {
        (Kind::Ch, Target::Ch(i)) => (1, 3, Some(i as i32 + 1)),
        // Aux in 7-8 are the USB playback (icon 60).
        (Kind::AuxIn, Target::AuxIn(i)) => (if i >= 6 { 60 } else { 55 }, 2, Some(33 + i as i32)),
        (Kind::FxRtn, _) => (61, 5, None),
        (Kind::Bus, _) => (71, 5, None),
        (Kind::Mtx, _) => (72, 5, None),
        (Kind::MainSt, _) => (66, 7, None),
        _ => (67, 7, None),
    };
    let c = format!("{p}/config");
    b.leaf(&c, "name", t, NAME, Bind::Name, Val::Text(String::new()));
    b.leaf(&c, "icon", t, ICON, Bind::State, int(icon));
    b.leaf(&c, "color", t, COLOUR, Bind::Colour, int(colour));
    if let Some(s) = source {
        b.leaf(&c, "source", t, SOURCE, Bind::State, int(s));
    }
    if kind == Kind::Ch {
        let d = format!("{p}/delay");
        b.leaf(&d, "on", t, ONOFF, Bind::DelayOn, int(0));
        b.leaf(&d, "time", t, DELAY, Bind::DelayTime, real(0.3));
    }
    let pre = format!("{p}/preamp");
    match kind {
        Kind::Ch => {
            b.leaf(&pre, "trim", t, TRIM, Bind::Trim, real(0.0));
            b.leaf(&pre, "invert", t, ONOFF, Bind::Invert, int(0));
            b.leaf(&pre, "hpon", t, ONOFF, Bind::LowCut, int(0));
            b.leaf(&pre, "hpslope", t, HP_SLOPE, Bind::LowCutSlope, int(2));
            b.leaf(&pre, "hpf", t, HP_FREQ, Bind::LowCutFreq, real(20.0));
        }
        Kind::AuxIn => {
            b.leaf(&pre, "trim", t, TRIM, Bind::Trim, real(0.0));
            b.leaf(&pre, "invert", t, ONOFF, Bind::Invert, int(0));
        }
        Kind::Mtx => b.leaf(&pre, "invert", t, ONOFF, Bind::Invert, int(0)),
        _ => {}
    }
    if kind == Kind::Ch {
        let g = format!("{p}/gate");
        b.leaf(&g, "on", t, ONOFF, Bind::GateOn, int(0));
        b.leaf(&g, "mode", t, GATE_MODE, Bind::GateMode, int(3));
        b.leaf(&g, "thr", t, GATE_THR, Bind::Gate("thr"), real(-80.0));
        b.leaf(&g, "range", t, RANGE, Bind::Gate("range"), real(60.0));
        b.leaf(&g, "attack", t, ATTACK, Bind::Gate("att"), real(10.0));
        b.leaf(&g, "hold", t, HOLD, Bind::Gate("hld"), real(50.2));
        b.leaf(&g, "release", t, RELEASE, Bind::Gate("rel"), real(258.0));
        b.leaf(&g, "keysrc", t, SOURCE, Bind::State, int(0));
        key_filter(b, &format!("{g}/filter"), t);
    }
    if !input || kind == Kind::Ch {
        let d = format!("{p}/dyn");
        b.leaf(&d, "on", t, ONOFF, Bind::DynOn, int(0));
        b.leaf(&d, "mode", t, DYN_MODE, Bind::DynMode, int(0));
        b.leaf(&d, "det", t, DETECT, Bind::Dyn("det"), int(i32::from(kind != Kind::Ch)));
        b.leaf(&d, "env", t, ENVELOPE, Bind::Dyn("env"), int(1));
        b.leaf(&d, "thr", t, DYN_THR, Bind::Dyn("thr"), real(0.0));
        b.leaf(&d, "ratio", t, RATIO, Bind::Dyn("ratio"), int(5));
        b.leaf(&d, "knee", t, KNEE, Bind::Dyn("knee"), real(1.0));
        b.leaf(&d, "mgain", t, MGAIN, Bind::Dyn("mgain"), real(0.0));
        b.leaf(&d, "attack", t, ATTACK, Bind::Dyn("att"), real(10.0));
        b.leaf(&d, "hold", t, HOLD, Bind::Dyn("hld"), real(10.0));
        b.leaf(&d, "release", t, RELEASE, Bind::Dyn("rel"), real(151.0));
        b.leaf(&d, "pos", t, POS, Bind::DynPos, int(1));
        // A matrix's or main's key source answers a get, but its `/node` line
        // leaves it out (as an X32 does; a channel's and a bus's list it).
        b.add(&d, "keysrc", t, SOURCE, Bind::State, int(0), matches!(kind, Kind::Ch | Kind::Bus));
        b.leaf(&d, "mix", t, MIX, Bind::DynMix, real(100.0));
        b.leaf(&d, "auto", t, ONOFF, Bind::Dyn("auto"), int(0));
        key_filter(b, &format!("{d}/filter"), t);
    }
    if !input || kind == Kind::Ch {
        let s = format!("{p}/insert");
        b.leaf(&s, "on", t, ONOFF, Bind::InsertOn, int(0));
        b.leaf(&s, "pos", t, POS, Bind::InsertPos, int(i32::from(kind == Kind::Ch)));
        b.leaf(&s, "sel", t, INSERT, Bind::InsertSel, int(0));
    }
    let e = format!("{p}/eq");
    b.leaf(&e, "on", t, ONOFF, Bind::EqOn, int(0));
    let bands: &[(i32, f32)] = if input {
        &[(2, 124.7), (2, 496.6), (2, 1970.0), (4, 10020.0)]
    } else {
        &[(1, 79.6), (2, 158.9), (2, 496.6), (2, 1970.0), (2, 5020.0), (4, 10020.0)]
    };
    for (k, &(ty, f)) in bands.iter().enumerate() {
        let band = format!("{e}/{}", k + 1);
        let ids = EQ_IDS[k];
        b.leaf(&band, "type", t, EQ_TYPE, Bind::Eq(ids[0]), int(ty));
        b.leaf(&band, "f", t, FREQ, Bind::Eq(ids[1]), real(f));
        b.leaf(&band, "g", t, EQ_GAIN, Bind::Eq(ids[2]), real(0.0));
        b.leaf(&band, "q", t, EQ_Q, Bind::Eq(ids[3]), real(2.0));
    }
    // mix: on, fader, then the LR and M/C assigns where the strip has them.
    let m = format!("{p}/mix");
    b.leaf(&m, "on", t, ONOFF, Bind::On, int(1));
    b.leaf(&m, "fader", t, FADER, Bind::Fader, real(FADER_MIN));
    if input || kind == Kind::Bus {
        b.leaf(&m, "st", t, ONOFF, Bind::MainOn(0), int(i32::from(input)));
        b.leaf(&m, "pan", t, PAN, Bind::Pan, real(0.0));
        b.leaf(&m, "mono", t, ONOFF, Bind::MainOn(1), int(0));
        b.leaf(&m, "mlevel", t, LEVEL, Bind::MainLevel(1), real(FADER_MIN));
    } else if kind == Kind::MainSt {
        b.leaf(&m, "pan", t, PAN, Bind::Pan, real(0.0));
    }
    // Sends: inputs to the 16 buses, buses and mains to the 6 matrices. Odd
    // sends also have the pair's pan, tap point and pan follow.
    if input {
        let tap = if kind == Kind::FxRtn { 4 } else { 2 };
        for k in 0..16 {
            let s = format!("{m}/{:02}", k + 1);
            b.leaf(&s, "on", t, ONOFF, Bind::SendOn(k), int(1));
            b.leaf(&s, "level", t, LEVEL, Bind::SendLevel(k), real(FADER_MIN));
            if k % 2 == 0 {
                b.leaf(&s, "pan", t, PAN, Bind::SendPan(k), real(0.0));
                b.leaf(&s, "type", t, SEND_TYPE, Bind::SendType(k), int(tap));
                b.leaf(&s, "panFollow", t, FOLLOW, Bind::SendFollow(k), int(0));
            }
        }
    } else if kind != Kind::Mtx {
        for k in 0..6 {
            let s = format!("{m}/{:02}", k + 1);
            b.leaf(&s, "on", t, ONOFF, Bind::MtxOn(k), int(1));
            b.leaf(&s, "level", t, LEVEL, Bind::MtxLevel(k), real(FADER_MIN));
            if k % 2 == 0 {
                b.leaf(&s, "pan", t, PAN, Bind::MtxPan(k), real(0.0));
                b.leaf(&s, "type", t, MTX_SEND_TYPE, Bind::State, int(4));
                b.leaf(&s, "panFollow", t, FOLLOW, Bind::MtxFollow(k), int(0));
            }
        }
    }
    if input || matches!(kind, Kind::Bus | Kind::Mtx) {
        let g = format!("{p}/grp");
        b.leaf(&g, "dca", t, Ty::Bits(8), Bind::Dcas, int(0));
        b.leaf(&g, "mute", t, Ty::Bits(6), Bind::MuteGroups, int(0));
    }
    if kind == Kind::Ch {
        let a = format!("{p}/automix");
        b.leaf(&a, "group", t, AUTOMIX, Bind::State, int(0));
        b.leaf(&a, "weight", t, WEIGHT, Bind::State, real(0.0));
    }
}

/// A gate's or dynamics' key filter (kept by the server: the engine's side
/// chain filter isn't the X32's).
fn key_filter(b: &mut Builder, f: &str, t: Target) {
    b.leaf(f, "on", t, ONOFF, Bind::State, int(0));
    b.leaf(f, "type", t, KEY_FILTER, Bind::State, int(6));
    b.leaf(f, "f", t, FREQ, Bind::State, real(990.9));
}

// ---- values -------------------------------------------------------------------------------

/// What the server keeps itself: values without an engine equivalent, by entry.
#[derive(Debug, Default)]
pub struct State {
    values: Mutex<HashMap<usize, Val>>,
}

impl State {
    fn lock(&self) -> std::sync::MutexGuard<'_, HashMap<usize, Val>> {
        self.values.lock().unwrap_or_else(|e| e.into_inner())
    }

    pub fn get(&self, i: usize) -> Option<Val> {
        self.lock().get(&i).cloned()
    }

    pub fn set(&self, i: usize, v: Val) {
        self.lock().insert(i, v);
    }
}

/// The engine and the server's state at one moment, with the strips looked
/// up once.
pub struct Console<'a> {
    pub engine: &'a Engine,
    pub state: &'a State,
    pub tree: &'static Tree,
    ch: Vec<Arc<Strip>>,
    aux: Vec<Arc<Strip>>,
    bus: Vec<Arc<Strip>>,
    main: Vec<Arc<Strip>>,
    mtx: Vec<Arc<Strip>>,
}

impl<'a> Console<'a> {
    pub fn new(engine: &'a Engine, state: &'a State) -> Self {
        Self {
            engine,
            state,
            tree: Tree::get(),
            ch: engine.strips(StripKind::Channel),
            aux: engine.strips(StripKind::Aux),
            bus: engine.strips(StripKind::Bus),
            main: engine.strips(StripKind::Main),
            mtx: engine.strips(StripKind::Matrix),
        }
    }

    /// The engine strip of a target.
    pub fn strip(&self, t: Target) -> Option<&Arc<Strip>> {
        match t {
            Target::Ch(i) => self.ch.get(i),
            Target::AuxIn(i) => self.aux.get(i),
            Target::FxRtn(i) => self.aux.get(8 + i),
            Target::Bus(i) => self.bus.iter().find(|s| s.id() == i as i32),
            Target::Mtx(i) => self.mtx.get(i),
            Target::MainSt => self.main.first(),
            Target::MainM => self.main.get(1),
            Target::Dca(_) | Target::Config => None,
        }
    }

    /// The value of entry `i`.
    pub fn get(&self, i: usize) -> Val {
        let e = &self.tree.entries[i];
        self.read_engine(i, e).unwrap_or_else(|| self.state.get(i).unwrap_or_else(|| e.default.clone()))
    }

    /// Sets entry `i` (made a value it can hold), and the same on its linked
    /// partner for what the link shares; returns what it holds now.
    pub fn set(&self, i: usize, v: Val) -> Val {
        let e = &self.tree.entries[i];
        let v = e.ty.normalise(v);
        if let Some(partner) = self.linked_partner(&e.path) {
            self.set_one(partner, v.clone());
        }
        self.set_one(i, v)
    }

    fn set_one(&self, i: usize, v: Val) -> Val {
        let e = &self.tree.entries[i];
        if !self.write_engine(i, e, &v) {
            self.state.set(i, v);
        }
        self.get(i)
    }

    /// The entry of `path`'s linked partner (1 for 2, 4 for 3...), when the
    /// pair is linked and the link shares that part (`/config/linkcfg`).
    fn linked_partner(&self, path: &str) -> Option<usize> {
        let mut parts = path.trim_start_matches('/').splitn(3, '/');
        let (kind, n, rest) = (parts.next()?, parts.next()?, parts.next()?);
        let link = match kind {
            "ch" => "chlink",
            "auxin" => "auxlink",
            "fxrtn" => "fxlink",
            "bus" => "buslink",
            "mtx" => "mtxlink",
            _ => return None,
        };
        let share = if rest.starts_with("preamp") || rest.starts_with("delay") {
            "hadly"
        } else if rest.starts_with("eq") {
            "eq"
        } else if rest.starts_with("gate") || rest.starts_with("dyn") {
            "dyn"
        } else if rest == "mix/fader" || rest == "mix/on" {
            "fdrmute"
        } else {
            return None;
        };
        let k: usize = n.parse().ok()?;
        let pair = (k - 1) / 2;
        let on = |addr: &str| match self.tree.find(addr) {
            Some(Found::Leaf(i)) => self.get(i).on(),
            _ => false,
        };
        if !on(&format!("/config/{link}/{}-{}", 2 * pair + 1, 2 * pair + 2)) || !on(&format!("/config/linkcfg/{share}")) {
            return None;
        }
        let other = if k % 2 == 1 { k + 1 } else { k - 1 };
        match self.tree.find(&format!("/{kind}/{other:0w$}/{rest}", w = n.len())) {
            Some(Found::Leaf(i)) => Some(i),
            _ => None,
        }
    }

    fn typed(e: &Entry, v: f32) -> Val {
        match e.ty {
            Ty::Float(..) => Val::Real(v),
            _ => Val::Int(v.round() as i32),
        }
    }

    fn dca(&self, t: Target) -> Option<&livedaw_engine::params::DcaParams> {
        match t {
            Target::Dca(i) if i < self.engine.num_dcas() => self.engine.controls.dcas.get(i),
            _ => None,
        }
    }

    fn read_engine(&self, i: usize, e: &Entry) -> Option<Val> {
        let on = |b: bool| Some(Val::Int(i32::from(b)));
        match e.bind {
            Bind::State => return None,
            Bind::DcaOn => return on(!self.dca(e.target)?.mute.load(Relaxed)),
            Bind::DcaFader => return Some(Val::Real(self.dca(e.target)?.fader_db.load())),
            Bind::DcaColour => return Some(Val::Int(self.dca(e.target)?.colour.load(Relaxed) - 1)),
            Bind::DcaName => {
                let Target::Dca(d) = e.target else { return None };
                self.dca(e.target)?;
                return Some(Val::Text(self.engine.dca_name(d)));
            }
            Bind::MuteGroup(g) => {
                return (g < self.engine.num_mute_groups())
                    .then(|| Val::Int(i32::from(self.engine.controls.mute_groups.contains(g))))
            }
            _ => {}
        }
        let s = self.strip(e.target)?;
        let p = &s.params;
        let sr = self.engine.sample_rate();
        let bits =
            |set: &livedaw_engine::AtomicGroupSet, n: usize| Val::Int((0..n).filter(|&k| set.contains(k)).map(|k| 1 << k).sum());
        Some(match e.bind {
            Bind::Name => Val::Text(s.name()),
            Bind::Colour => Val::Int(p.colour.load(Relaxed) - 1),
            Bind::Fader => Val::Real(p.fader_db.load()),
            Bind::On => return on(!p.mute.load(Relaxed)),
            Bind::Pan => Val::Real(p.pan.load()),
            Bind::MainOn(m) => return on(s.mains.get_or_create(m)?.on.load(Relaxed)),
            Bind::MainLevel(m) => Val::Real(s.mains.get_or_create(m)?.level_db.load()),
            Bind::SendOn(k) => return on(s.sends.get_or_create(k)?.enabled.load(Relaxed)),
            Bind::SendLevel(k) => Val::Real(s.sends.get_or_create(k)?.level_db.load()),
            Bind::SendPan(k) => Val::Real(s.sends.get_or_create(k)?.pan.load()),
            Bind::SendFollow(k) => return on(s.sends.get_or_create(k)?.pan_link.load(Relaxed)),
            Bind::SendType(k) => Val::Int(send_type(s, s.sends.get_or_create(k)?)?),
            // Only a channel's are in its chain (buses, mains, matrices: kept).
            Bind::DynPos if s.kind() == StripKind::Channel => Val::Int(i32::from(order(s) & 1 == 0)),
            Bind::InsertPos if s.kind() == StripKind::Channel => Val::Int(i32::from(order(s) & 2 == 0)),
            Bind::MtxOn(k) => return on(s.matrices.get_or_create(k)?.on.load(Relaxed)),
            Bind::MtxLevel(k) => Val::Real(s.matrices.get_or_create(k)?.level_db.load()),
            Bind::MtxPan(k) => Val::Real(s.matrices.get_or_create(k)?.pan.load()),
            Bind::MtxFollow(k) => return on(s.matrices.get_or_create(k)?.pan_link.load(Relaxed)),
            Bind::Dcas => bits(&p.dca_mask, 8),
            Bind::MuteGroups => bits(&p.mute_group_mask, 6),
            Bind::Trim => Val::Real(p.input.trim_db.load()),
            Bind::Invert => return on(p.input.invert.load(Relaxed)),
            Bind::LowCut => return on(p.filter().low_cut.load(Relaxed)),
            // The engine's slopes are 6, 12, 18 and 24 dB/oct; the X32's 12, 18, 24.
            Bind::LowCutSlope => Val::Int((p.filter().low_cut_slope.load(Relaxed) - 1).clamp(0, 2)),
            Bind::LowCutFreq => Val::Real(p.filter().low_cut_freq.load()),
            Bind::DelayOn => return on(p.input.delay_on.load(Relaxed)),
            Bind::DelayTime => {
                let mode = p.input.delay_mode.load(Relaxed).clamp(0, 3) as usize;
                Val::Real(delay_to_ms(p.input.delay.load(), mode, sr))
            }
            Bind::GateOn => return on(p.gate().on.load(Relaxed)),
            Bind::GateMode => Val::Int(model_of(p.gate(), &GATE_MODELS)?),
            Bind::Gate(id) => Self::typed(e, p.gate().param(id)?.load()),
            Bind::DynOn => return on(p.dynamics().on.load(Relaxed)),
            Bind::DynMode => Val::Int(model_of(p.dynamics(), &DYN_MODELS)?),
            Bind::Dyn(id) => Self::typed(e, p.dynamics().param(id)?.load()),
            Bind::DynMix => Val::Real(p.dynamics().mix.load()),
            Bind::EqOn => return on(p.eq().on.load(Relaxed)),
            Bind::Eq(id) => Self::typed(e, p.eq().param(id)?.load()),
            Bind::InsertOn => return on(p.pre_insert().on.load(Relaxed)),
            Bind::InsertSel => {
                let fx = p.pre_insert().fx.load(Relaxed);
                let kept = self.state.get(i).map_or(0, |v| v.int());
                Val::Int(if insert_slot(kept) == fx {
                    kept
                } else if (0..8).contains(&fx) {
                    1 + 2 * fx
                } else {
                    0
                })
            }
            _ => return None,
        })
    }

    /// Writes a value to the engine; false if it has no place there.
    fn write_engine(&self, i: usize, e: &Entry, v: &Val) -> bool {
        let text = || match v {
            Val::Text(s) => s.clone(),
            _ => String::new(),
        };
        match e.bind {
            Bind::State => return false,
            Bind::DcaOn | Bind::DcaFader | Bind::DcaColour | Bind::DcaName => {
                let (Some(d), Target::Dca(k)) = (self.dca(e.target), e.target) else { return false };
                match e.bind {
                    Bind::DcaOn => d.mute.store(!v.on(), Relaxed),
                    Bind::DcaFader => d.fader_db.store(v.real()),
                    Bind::DcaColour => d.colour.store(v.int() + 1, Relaxed),
                    _ => self.engine.set_dca_name(k, &text()),
                }
                return true;
            }
            Bind::MuteGroup(g) => {
                if g >= self.engine.num_mute_groups() {
                    return false;
                }
                self.engine.controls.mute_groups.set(g, v.on());
                return true;
            }
            _ => {}
        }
        let Some(s) = self.strip(e.target) else { return false };
        let p = &s.params;
        let sr = self.engine.sample_rate();
        let set_bits = |set: &livedaw_engine::AtomicGroupSet, n: usize| {
            for k in 0..n {
                set.set(k, v.int() & (1 << k) != 0);
            }
        };
        let param = |slot: Option<&livedaw_engine::atomic::AtomicF32>| match slot {
            Some(a) => {
                a.store(v.real());
                true
            }
            None => false,
        };
        match e.bind {
            Bind::Name => s.set_name(&text()),
            Bind::Colour => p.colour.store(v.int() + 1, Relaxed),
            Bind::Fader => p.fader_db.store(v.real()),
            Bind::On => p.mute.store(!v.on(), Relaxed),
            Bind::Pan => p.pan.store(v.real()),
            Bind::MainOn(m) | Bind::MainLevel(m) => {
                let Some(send) = s.mains.get_or_create(m) else { return false };
                match e.bind {
                    Bind::MainOn(_) => send.on.store(v.on(), Relaxed),
                    _ => send.level_db.store(v.real()),
                }
            }
            Bind::SendOn(k) | Bind::SendLevel(k) | Bind::SendPan(k) | Bind::SendFollow(k) => {
                let Some(send) = s.sends.get_or_create(k) else { return false };
                match e.bind {
                    Bind::SendOn(_) => send.enabled.store(v.on(), Relaxed),
                    Bind::SendLevel(_) => send.level_db.store(v.real()),
                    Bind::SendPan(_) => send.pan.store(v.real()),
                    _ => send.pan_link.store(v.on(), Relaxed),
                }
            }
            Bind::SendType(k) => {
                for slot in [k, k + 1] {
                    let Some(send) = s.sends.get_or_create(slot) else { return false };
                    if !set_send_type(s, send, v.int()) {
                        return false;
                    }
                }
            }
            Bind::DynPos | Bind::InsertPos => {
                if s.kind() != StripKind::Channel {
                    return false;
                }
                // The sends keep their tap points (<-EQ, EQ->) in the new order.
                let types: Vec<(usize, i32)> =
                    (0..16).step_by(2).filter_map(|k| Some((k, send_type(s, s.sends.get_or_create(k)?)?))).collect();
                let bit = if matches!(e.bind, Bind::DynPos) { 1 } else { 2 };
                let pre = v.int() == 0;
                let o = (order(s) & !bit) | if pre { bit } else { 0 };
                p.process_order.store(o, Relaxed);
                for (k, ty) in types {
                    for slot in [k, k + 1] {
                        if let Some(send) = s.sends.get_or_create(slot) {
                            set_send_type(s, send, ty);
                        }
                    }
                }
            }
            Bind::MtxOn(k) | Bind::MtxLevel(k) | Bind::MtxPan(k) | Bind::MtxFollow(k) => {
                let Some(send) = s.matrices.get_or_create(k) else { return false };
                match e.bind {
                    Bind::MtxOn(_) => send.on.store(v.on(), Relaxed),
                    Bind::MtxLevel(_) => send.level_db.store(v.real()),
                    Bind::MtxPan(_) => send.pan.store(v.real()),
                    _ => send.pan_link.store(v.on(), Relaxed),
                }
            }
            Bind::Dcas => set_bits(&p.dca_mask, 8),
            Bind::MuteGroups => set_bits(&p.mute_group_mask, 6),
            Bind::Trim => p.input.trim_db.store(v.real()),
            Bind::Invert => p.input.invert.store(v.on(), Relaxed),
            Bind::LowCut => p.filter().low_cut.store(v.on(), Relaxed),
            Bind::LowCutSlope => p.filter().low_cut_slope.store(v.int() + 1, Relaxed),
            Bind::LowCutFreq => p.filter().low_cut_freq.store(v.real()),
            Bind::DelayOn => p.input.delay_on.store(v.on(), Relaxed),
            Bind::DelayTime => {
                // In the strip's own delay unit.
                let mode = p.input.delay_mode.load(Relaxed).clamp(0, 3) as usize;
                p.input.delay.store(ms_to_delay(v.real(), mode, sr));
            }
            Bind::GateOn => p.gate().on.store(v.on(), Relaxed),
            Bind::GateMode => return set_model(p.gate(), GATE_MODELS[v.int().clamp(0, 4) as usize]),
            Bind::Gate(id) => return param(p.gate().param(id)),
            Bind::DynOn => p.dynamics().on.store(v.on(), Relaxed),
            Bind::DynMode => return set_model(p.dynamics(), DYN_MODELS[v.int().clamp(0, 1) as usize]),
            Bind::Dyn(id) => return param(p.dynamics().param(id)),
            Bind::DynMix => p.dynamics().mix.store(v.real()),
            Bind::EqOn => p.eq().on.store(v.on(), Relaxed),
            Bind::Eq(id) => return param(p.eq().param(id)),
            Bind::InsertOn => p.pre_insert().on.store(v.on(), Relaxed),
            Bind::InsertSel => {
                // The FX slot goes to the engine; the side (L/R) and the AUX
                // loops are kept.
                self.state.set(i, v.clone());
                p.pre_insert().fx.store(insert_slot(v.int()), Relaxed);
            }
            _ => return false,
        }
        true
    }
}

/// The FX slot (0-7) of an insert choice, -1 for OFF and the AUX loops.
fn insert_slot(sel: i32) -> i32 {
    if (1..=16).contains(&sel) {
        (sel - 1) / 2
    } else {
        -1
    }
}

/// Which of `tokens` a slot's model is.
fn model_of(slot: &DynSlotParams, tokens: &[&str]) -> Option<i32> {
    let token = slot.model_spec()?.token;
    tokens.iter().position(|t| *t == token).map(|i| i as i32)
}

/// Puts a slot on model `token`, keeping the values when the new model has
/// the same parameters (the X32's gate modes, its compressor and expander).
fn set_model(slot: &DynSlotParams, token: &str) -> bool {
    let Some(model) = slot.table().iter().position(|m| m.token == token) else { return false };
    if slot.model_index() == model {
        return true;
    }
    let kept: Vec<f32> = slot.values.iter().map(|v| v.load()).collect();
    let same = slot.model_spec().is_some_and(|m| m.params == slot.table()[model].params);
    slot.set_model(model);
    if same {
        for (a, v) in slot.values.iter().zip(kept) {
            a.store(v);
        }
    }
    true
}

/// A channel's X32 process order (`chain::X32_ORDERS`: bit 1 the compressor
/// PRE the EQ, bit 2 the insert PRE).
fn order(s: &Strip) -> i32 {
    s.params.process_order.load(Relaxed).clamp(0, 3)
}

/// The strip's tap ids just before and after its EQ (the tap after `Slot(j)`
/// is `2 + j`; before the first slot, the low cut's).
fn eq_taps(s: &Strip) -> (i32, i32) {
    let letters = s.spec().pre_fader_order(s.kind(), order(s));
    match letters.iter().position(|&b| b == b'E') {
        Some(at) => {
            let at = at as i32;
            (if at == 0 { tap::FILT as i32 } else { 1 + at }, 2 + at)
        }
        // Aux in and FX returns: the EQ sits between taps 2 and 3 (`chain::X32_AUX`).
        None => (2, 3),
    }
}

/// The X32 type of a send (0 IN/LC, 1 <-EQ, 2 EQ->, 3 PRE, 4 POST, 5 GRP).
fn send_type(s: &Strip, send: &SendParams) -> Option<i32> {
    let (before, after) = eq_taps(s);
    Some(match (send.mode.load(Relaxed), send.tap.load(Relaxed)) {
        (send_mode::GROUP, _) => 5,
        (send_mode::POST, _) => 4,
        (_, -1) => 3, // PRE: the strip's tap point, PFL on an X32
        (_, t) if t == tap::FILT as i32 => 0,
        (_, t) if t == before => 1,
        (_, t) if t == after => 2,
        _ => 3,
    })
}

/// Sets a send to X32 type `ty`: the low cut's, the EQ's input or output tap
/// (`SendParams::tap`), pre-fader, post-fader or GRP.
fn set_send_type(s: &Strip, send: &SendParams, ty: i32) -> bool {
    let (before, after) = eq_taps(s);
    let (mode, at) = match ty {
        0 => (send_mode::PRE, tap::FILT as i32),
        1 => (send_mode::PRE, before),
        2 => (send_mode::PRE, after),
        3 => (send_mode::PRE, -1),
        4 => (send_mode::POST, -1),
        5 => (send_mode::GROUP, -1),
        _ => return false,
    };
    send.mode.store(mode, Relaxed);
    send.tap.store(at, Relaxed);
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_tree() {
        let t = Tree::get();
        let leaf = |a: &str| match t.find(a) {
            Some(Found::Leaf(i)) => i,
            other => panic!("{a}: {other:?}"),
        };
        assert_eq!(t.entries[leaf("/ch/01/mix/fader")].ty, FADER);
        assert_eq!(leaf("ch/01/mix/fader/"), leaf("/ch/01/mix/fader"));
        assert!(t.find("/ch/33/mix/fader").is_none());
        assert!(t.find("/ch/1/mix/fader").is_none());
        // Only odd sends have a pan, a tap point and a pan follow.
        assert!(t.find("/ch/01/mix/01/type").is_some() && t.find("/ch/01/mix/02/type").is_none());
        // The node lists in the X32's order.
        let names = |a: &str| {
            let Some(Found::Node(n)) = t.find(a) else { panic!("{a}") };
            t.group(Found::Node(n)).iter().map(|&i| t.entries[i].path.rsplit('/').next().unwrap().to_owned()).collect::<Vec<_>>()
        };
        assert_eq!(names("/ch/01/mix"), ["on", "fader", "st", "pan", "mono", "mlevel"]);
        assert_eq!(names("/ch/01/mix/01"), ["on", "level", "pan", "type", "panFollow"]);
        assert_eq!(names("/main/st/mix"), ["on", "fader", "pan"]);
        assert_eq!(names("/mtx/01/mix"), ["on", "fader"]);
        assert_eq!(names("/dca/1"), ["on", "fader"]);
        assert_eq!(names("/ch/01"), ["name", "icon", "color", "source"], "the first group");
        assert_eq!(names("/bus/01/dyn").len(), 15, "a bus lists its key source (the real X32 does)");
        assert_eq!(names("/mtx/01/dyn").len(), 14, "a matrix doesn't");
        assert_eq!(names("/ch/01/dyn").len(), 15);
        assert!(t.find("/bus/01/dyn/keysrc").is_some());
        assert_eq!(names("/bus/01/eq/6"), ["type", "f", "g", "q"]);
        assert!(t.find("/ch/01/eq/5").is_none(), "4 bands on inputs");
        assert_eq!(names("/config/chlink").len(), 16);
        // Meter ids.
        for id in 0..72 {
            assert_eq!(Target::of_meter_id(id).and_then(Target::meter_id), Some(id));
        }
    }
}
