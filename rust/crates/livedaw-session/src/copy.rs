//! COPY FROM: another console's settings copied into the running one
//! (`docs/COPY_BETWEEN_CONSOLES.md`).
//!
//! The other console is loaded into a scratch engine of its own, its chosen sections are
//! captured as a scene captures them, and that state is applied to the running console:
//! each strip onto the strip of the same kind and number (by position), sends only to the
//! buses, mains and matrices this console has. What has no place here is left out and
//! named in the report. Scenes, effects and a strip's extra blocks are never copied.
//!
//! EQ, gate and dynamics come across as they were (the other console's own models, which
//! any console runs: KEEP THEIRS). One whose model this console can't run is left as it was.

use crate::console::{self, section, NoPlugins, PluginBridge};
use crate::refs::StripRef;
use crate::show::Show;
use crate::xml::{Element, Node};
use livedaw_dsp::model::model_index;
use livedaw_engine::{Engine, Strip, StripKind};
use std::collections::{BTreeSet, HashMap};
use std::path::Path;

/// The sections COPY FROM can take: everything a scene holds but the inserts (effects
/// and plug-ins belong to the console they were set up on).
pub const COPYABLE: u32 = section::ALL & !section::INSERTS;

/// What is copied by default: everything but routing (the other console may have been on
/// another interface) and groups.
pub const DEFAULT_SECTIONS: u32 =
    section::NAME | section::PREAMP | section::GATE | section::EQ | section::DYNAMICS | section::SENDS | section::FADER;

/// The strip kinds, in the order the report names them.
pub const KINDS: [StripKind; 5] = [StripKind::Channel, StripKind::Aux, StripKind::Bus, StripKind::Main, StripKind::Matrix];

/// What to copy.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CopyPlan {
    /// `console::section` bits (outside `COPYABLE` ignored).
    pub sections: u32,
    /// The kinds of strip copied.
    pub kinds: Vec<StripKind>,
    /// Only these channels (1-based numbers, both included); `None`: all of them.
    pub channels: Option<(usize, usize)>,
}

impl Default for CopyPlan {
    fn default() -> Self {
        Self { sections: DEFAULT_SECTIONS, kinds: KINDS.to_vec(), channels: None }
    }
}

/// What a copy did.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct CopyReport {
    /// The other console's name.
    pub source: String,
    /// Strips copied onto.
    pub strips: usize,
    /// What had no place in this console, one line each ("Channels 41-48: this console has 40").
    pub left_out: Vec<String>,
    /// Loading the other console's file.
    pub warnings: Vec<String>,
}

/// Copies from a show file (a kept console's or any show) into `engine`. Err: the file
/// couldn't be read; nothing changed then.
pub fn copy_from_file(
    engine: &Engine,
    path: &Path,
    plan: &CopyPlan,
    bridge: &mut dyn PluginBridge,
) -> Result<CopyReport, String> {
    let source = Engine::new(0);
    let warnings = Show::new().load_from_file(&source, &mut NoPlugins, path, false)?;
    let mut report = copy_console(engine, &source, plan, bridge);
    report.warnings = warnings;
    Ok(report)
}

/// Copies `plan` from the console in `source` (an engine of its own) into `engine`.
pub fn copy_console(engine: &Engine, source: &Engine, plan: &CopyPlan, bridge: &mut dyn PluginBridge) -> CopyReport {
    let sections = plan.sections & COPYABLE;
    let mut report = CopyReport { source: source.profile().spec().name.to_owned(), ..CopyReport::default() };
    let mut left = LeftOut::default();

    // Each of the other console's strips onto this console's at the same place.
    let mut targets: HashMap<StripRef, Strip2> = HashMap::new();
    for kind in KINDS {
        let (from, to) = (source.strips(kind), engine.strips(kind));
        let wanted = plan.kinds.contains(&kind);
        for (i, s) in from.iter().enumerate() {
            let number = i + 1;
            if !wanted || (kind == StripKind::Channel && plan.channels.is_some_and(|(a, b)| !(a..=b).contains(&number))) {
                continue;
            }
            match to.get(i) {
                Some(t) => {
                    targets.insert(StripRef::of(s), Strip2 { to: StripRef::of(t), strip: t.clone() });
                }
                None => {
                    left.strips.entry(kind_index(kind)).or_default().insert(number);
                }
            }
        }
        if wanted && to.len() < from.len() {
            left.counts.insert(kind_index(kind), to.len());
        }
    }

    // This console's buses (by slot), mains and matrices: the only sends kept.
    let bus_slots: BTreeSet<usize> = engine.strips(StripKind::Bus).iter().filter_map(|b| usize::try_from(b.id()).ok()).collect();
    let (mains, matrices) = (engine.num_strips(StripKind::Main), engine.num_strips(StripKind::Matrix));

    let captured = console::capture(source, sections, None, &mut NoPlugins);
    let mut state = Element::new("State");
    state.attrs = captured.attrs.clone();
    for node in &captured.children {
        let Node::Element(e) = node else { continue };
        if e.name != "Strip" {
            state.add(e.clone()); // the console's own parts: DCAs, mute groups, monitor, sources
            continue;
        }
        let Some(target) = targets.get(&StripRef::parse(e.str_or("ref", ""))) else { continue };
        let mut e = e.clone();
        e.set("ref", target.to);
        // A strip's extra blocks are matched by id, which means nothing on another console.
        e.children.retain(|n| !matches!(n, Node::Element(c) if c.name == "Chain"));
        keep_sends(&mut e, "Sends", "slot", |slot| bus_slots.contains(&slot), &mut left.buses);
        keep_sends(&mut e, "Mains", "i", |i| i < mains, &mut left.mains);
        keep_sends(&mut e, "Mtx", "i", |i| i < matrices, &mut left.matrices);
        // A model this console can't run: that block is left as it was.
        let p = &target.strip.params;
        for (tag, table, what) in
            [("Eq", p.eq().table(), "EQ"), ("Gate", p.gate().table(), "gate"), ("Dyn", p.dynamics().table(), "dynamics")]
        {
            let unknown = e.child(tag).and_then(|x| x.attr("mdl")).is_some_and(|t| model_index(table, t).is_none());
            if unknown {
                e.children.retain(|n| !matches!(n, Node::Element(c) if c.name == tag));
                left.models.insert(what);
            }
        }
        state.add(e);
    }

    let result = console::apply(engine, &state, sections, None, false, None, bridge);
    report.strips = usize::try_from(result.strips_applied).unwrap_or(0);
    report.left_out = left.lines();
    report
}

struct Strip2 {
    to: StripRef,
    strip: std::sync::Arc<Strip>,
}

fn kind_index(kind: StripKind) -> usize {
    KINDS.iter().position(|k| *k == kind).unwrap_or(0)
}

/// Drops the sends of `<tag>` whose `attr` (0-based) this console has no destination for.
fn keep_sends(e: &mut Element, tag: &str, attr: &str, keep: impl Fn(usize) -> bool, dropped: &mut BTreeSet<usize>) {
    let Some(list) = e.child_mut(tag) else { return };
    list.children.retain(|n| match n {
        Node::Element(s) if s.name == "S" => match usize::try_from(s.int_or(attr, -1)) {
            Ok(i) if keep(i) => true,
            Ok(i) => {
                if s.bool_or("on", false) {
                    dropped.insert(i + 1); // only a send that was in use is worth a line
                }
                false
            }
            Err(_) => false,
        },
        _ => true,
    });
}

/// What was left out, gathered while copying.
#[derive(Default)]
struct LeftOut {
    /// By kind (`KINDS` order): the strip numbers with no counterpart.
    strips: HashMap<usize, BTreeSet<usize>>,
    /// By kind: how many this console has, where it has fewer.
    counts: HashMap<usize, usize>,
    buses: BTreeSet<usize>,
    mains: BTreeSet<usize>,
    matrices: BTreeSet<usize>,
    models: BTreeSet<&'static str>,
}

impl LeftOut {
    fn lines(&self) -> Vec<String> {
        const NAMES: [(&str, &str); 5] = [
            ("Channel", "channels"),
            ("Aux channel", "aux channels"),
            ("Bus", "buses"),
            ("Main", "mains"),
            ("Matrix", "matrices"),
        ];
        let mut lines = Vec::new();
        for (k, (one, many)) in NAMES.iter().enumerate() {
            let Some(numbers) = self.strips.get(&k).filter(|n| !n.is_empty()) else { continue };
            let have = self.counts.get(&k).copied().unwrap_or(0);
            lines.push(match have {
                0 => format!("{}: this console has none", capitalise(many)),
                _ => {
                    let label = if numbers.len() == 1 { (*one).to_owned() } else { capitalise(many) };
                    format!("{label} {}: this console has {have}", ranges(numbers))
                }
            });
        }
        for (set, one, many) in
            [(&self.buses, "bus", "buses"), (&self.mains, "main", "mains"), (&self.matrices, "matrix", "matrices")]
        {
            if !set.is_empty() {
                let (what, it) = if set.len() == 1 { (one, "it") } else { (many, "them") };
                lines.push(format!("Sends to {what} {}: this console doesn't have {it}", ranges(set)));
            }
        }
        if !self.models.is_empty() {
            let list: Vec<&str> = self.models.iter().copied().collect();
            lines.push(format!("Some {}: a model this console can't run, left as it was", list.join(" and ")));
        }
        lines
    }
}

fn capitalise(s: &str) -> String {
    let mut c = s.chars();
    c.next().map(|f| f.to_uppercase().collect::<String>() + c.as_str()).unwrap_or_default()
}

/// "1-8, 12, 14-16".
fn ranges(numbers: &BTreeSet<usize>) -> String {
    let mut parts = Vec::new();
    let mut it = numbers.iter().copied().peekable();
    while let Some(start) = it.next() {
        let mut end = start;
        while it.peek() == Some(&(end + 1)) {
            end += 1;
            it.next();
        }
        parts.push(if end == start { start.to_string() } else { format!("{start}-{end}") });
    }
    parts.join(", ")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn numbers_are_written_as_ranges() {
        let set: BTreeSet<usize> = [1, 2, 3, 5, 7, 8].into_iter().collect();
        assert_eq!(ranges(&set), "1-3, 5, 7-8");
        assert_eq!(ranges(&BTreeSet::new()), "");
    }
}
