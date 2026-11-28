//! A Midas PRO2's channel safes but AUTO (Owner's Manual DOC02-DL2 Issue A, "OM", pp.174-175
//! and Appendix H "Parameters Protected By Safes", pp.385-430): EQ, DYN, MIC, MUTE and FADER,
//! set per strip (`Pro2StripParams::safes`), each keeps its part of the strip as it is
//! through a scene recall. AUTO, everything, is the strip's recall safe.
//!
//! The parts are Sidecar's scene sections cut finer (a fader and its mute are one section,
//! FADER, but two safes). A recall applies the scene's `<Strip>` with what the strip's safes
//! keep taken from the strip as it is now ([`keep`]), so fades, undo and the recall filter
//! work as for any recall. A MIC-safe input's sources (gain, 48V) and the ports its direct
//! output is patched to keep theirs too ([`mic_sources`], [`DirectPatch`]).
//!
//! Sidecar's readings, where Appendix H names a control Sidecar has no own setting for or
//! lists it under another heading: the inputs' "30Hz" and "input zone" are not in Sidecar;
//! an output's bus trim and delay (FADER) are its INPUT block; an output's direct input
//! (MIC) is its external input or, on a matrix, its direct in; a block of a strip's chain
//! (the Universal Mixer's) is kept by its section's safe.

use crate::console::{capture_strip, section, NoPlugins, StripIndex};
use crate::xml::Element;
use livedaw_engine::pro2::safe;
use livedaw_engine::routing::DirectSource;
use livedaw_engine::sources::source_index;
use livedaw_engine::{Engine, Strip, StripKind};
use std::sync::atomic::Ordering::Relaxed;

/// The strip's safes, when a recall should keep any.
pub(crate) fn safes_of(s: &Strip) -> i32 {
    s.pro2.safes.load(Relaxed) & safe::ALL
}

/// The scene's strip `scene` for a recall into `s`, whose safes are `safes`: what they keep
/// is the strip's own now.
pub(crate) fn keep(engine: &Engine, index: &StripIndex, s: &Strip, scene: &Element, safes: i32) -> Element {
    let (mains, matrices) = (engine.num_strips(StripKind::Main), engine.num_strips(StripKind::Matrix));
    let now = capture_strip(s, section::ALL, &index.bus_slots(), mains, matrices, &mut NoPlugins);
    let mut e = scene.clone();
    let input = s.kind().is_input();
    let on = |bit: i32| safes & bit != 0;
    if on(safe::EQ) {
        whole(&mut e, &now, "Eq");
        whole(&mut e, &now, "Flt");
        if input {
            attrs(&mut e, &now, &["Ins"], &["proc"]);
        }
        blocks(&mut e, &now, &["eq", "filter"], &[]);
    }
    if on(safe::DYN) {
        whole(&mut e, &now, "Gate");
        whole(&mut e, &now, "Dyn");
        blocks(&mut e, &now, &["dyn"], &[]);
    }
    if on(safe::MIC) {
        if input {
            // Gain trim, phase and delay (OM p.393; the gain and 48V are the source's).
            whole(&mut e, &now, "In");
            attrs(&mut e, &now, &["Pro2"], &["dir", "dirMode"]);
            blocks(&mut e, &now, &["delay"], &[]);
        } else {
            whole(&mut e, &now, "MixInput");
            whole(&mut e, &now, "Dir");
        }
        attrs(&mut e, &now, &["Ins", "Pre"], &["on"]);
        attrs(&mut e, &now, &["Ins", "Post"], &["on"]);
        blocks(&mut e, &now, &["ins"], &["on"]);
    }
    if on(safe::MUTE) {
        attrs(&mut e, &now, &["Fdr"], &["mute"]);
        if input {
            attrs(&mut e, &now, &["Pro2"], &["dirMute"]);
        }
    }
    if on(safe::FADER) {
        attrs(&mut e, &now, &["Fdr"], &["lvl", "pan", "wid"]);
        attrs(&mut e, &now, &["Pro2"], &["sis", "image"]);
        list(&mut e, &now, "Mains", &["on"]);
        if !input {
            // An output's bus trim, delay and (an aux send's) mode (OM pp.404, 418).
            whole(&mut e, &now, "In");
            attrs(&mut e, &now, &["Pro2"], &["mode"]);
            blocks(&mut e, &now, &["delay"], &[]);
        }
    }
    e
}

/// `name` as it is now: a copy of the strip's, or none if it has none (its defaults).
fn whole(e: &mut Element, now: &Element, name: &str) {
    if e.child(name).is_none() {
        return; // not in the scene: the recall leaves it anyway
    }
    match now.child(name) {
        Some(n) => {
            if let Some(c) = e.child_mut(name) {
                *c = n.clone();
            }
        }
        None => e.remove_children(name),
    }
}

/// The attributes `names` of the element at `path` as they are now.
fn attrs(e: &mut Element, now: &Element, path: &[&str], names: &[&str]) {
    let (Some(c), Some(n)) = (at_mut(e, path), at(now, path)) else { return };
    copy_attrs(c, n, names);
}

fn at<'a>(e: &'a Element, path: &[&str]) -> Option<&'a Element> {
    path.iter().try_fold(e, |e, name| e.child(name))
}

fn at_mut<'a>(e: &'a mut Element, path: &[&str]) -> Option<&'a mut Element> {
    path.iter().try_fold(e, |e, name| e.child_mut(name))
}

fn copy_attrs(to: &mut Element, from: &Element, names: &[&str]) {
    for name in names {
        if let (true, Some(v)) = (to.has_attr(name), from.attr(name)) {
            to.set(name, v);
        }
    }
}

/// The attributes `names` of each `<S i="..">` of `list` (the main sends) as they are now.
fn list(e: &mut Element, now: &Element, list: &str, names: &[&str]) {
    let (Some(c), Some(n)) = (e.child_mut(list), now.child(list)) else { return };
    for x in c.children_named_mut("S") {
        let i = x.attr("i").map(str::to_owned);
        if let Some(from) = n.children_named("S").find(|y| y.attr("i").map(str::to_owned) == i) {
            copy_attrs(x, from, names);
        }
    }
}

/// The chain's blocks of `kinds` as they are now: whole, or only the attributes `names`.
fn blocks(e: &mut Element, now: &Element, kinds: &[&str], names: &[&str]) {
    let (Some(c), Some(n)) = (e.child_mut("Chain"), now.child("Chain")) else { return };
    for x in c.children_named_mut("B") {
        if !kinds.contains(&x.str_or("kind", "")) {
            continue;
        }
        let uid = x.attr("uid").map(str::to_owned);
        let Some(from) = n.children_named("B").find(|y| y.attr("uid").map(str::to_owned) == uid) else { continue };
        if names.is_empty() {
            *x = from.clone();
        } else {
            copy_attrs(x, from, names);
        }
    }
}

/// The sources (`sources::source_index`) a MIC-safe input hears, which a recall leaves as
/// they are (their gain and 48V, OM p.393). Before the recall, as it may patch others.
pub(crate) fn mic_sources(engine: &Engine) -> Vec<usize> {
    let mut v = Vec::new();
    for kind in [StripKind::Channel, StripKind::Aux] {
        for s in engine.strips(kind).iter().filter(|s| safes_of(s) & safe::MIC != 0) {
            for port in [s.params.input_left.load(Relaxed), s.params.input_right.load(Relaxed)] {
                if let Some(i) = source_index(port).filter(|i| !v.contains(i)) {
                    v.push(i);
                }
            }
        }
    }
    v
}

/// The output patch before a recall, when a MIC-safe input channel's direct output may be
/// on it (its "dest", OM p.393).
pub(crate) struct DirectPatch {
    before: Vec<i32>,
    /// The channels' numbers as a direct out names them (CH 1 = 1).
    safe: Vec<i32>,
}

impl DirectPatch {
    pub(crate) fn before(engine: &Engine) -> Option<Self> {
        let channels = engine.strips(StripKind::Channel);
        let safe: Vec<i32> =
            channels.iter().enumerate().filter(|(_, s)| safes_of(s) & safe::MIC != 0).map(|(i, _)| i as i32 + 1).collect();
        let before = engine.output_patch().ports.iter().map(|p| p.load(Relaxed)).collect();
        (!safe.is_empty()).then_some(Self { before, safe })
    }

    fn is_safe(&self, source: i32) -> bool {
        DirectSource::from_code(source).is_some_and(|d| d.kind == StripKind::Channel && self.safe.contains(&d.number))
    }

    /// After the recall: a port that had or now has a safe channel's direct output gets back
    /// what it had.
    pub(crate) fn restore(&self, engine: &Engine) {
        for (port, (p, &was)) in engine.output_patch().ports.iter().zip(&self.before).enumerate() {
            let now = p.load(Relaxed);
            if now != was && (self.is_safe(now) || self.is_safe(was)) {
                engine.output_patch().set(port as i32, was);
            }
        }
    }
}
