//! X32 channel presets (`.chn`, the LIBRARY's channel files): the scene
//! file's text format for one strip, its lines' paths relative to the strip.
//!
//! ```text
//! #2.1# "Kick" 1 %111111111 1
//! /config "Kick" 1 RD 1
//! /gate ON GATE -40.0 60.0 7 10 199 0
//! /mix ON -10.0 ON +0 OFF -oo
//! ```
//!
//! The header follows the X32's library entries (`/-libs`): the name, its
//! type, the elements it holds and that it has data. From public descriptions
//! only, not compared with a file an X32 wrote. Reading takes any version and
//! only the name from the header. A preset doesn't move a strip's input:
//! `/config` sets the name, icon and colour, not the source. Head amp lines
//! (`/headamp/...`) are skipped, as Sidecar has no remote preamps.

use crate::map::{Console, Found, State};
use crate::scn::Read;
use crate::value::fields;
use livedaw_engine::{ConsoleConfig, Counts, Engine, Profile};

/// The header's version tag, as channel presets carry it.
pub const VERSION: &str = "#2.1#";

/// The strips a channel preset is for: `/ch/01`-`/ch/32`, `/auxin/01`-`08`,
/// `/fxrtn/01`-`08`, `/bus/01`-`16`, `/mtx/01`-`06`, `/main/st`, `/main/m`.
fn strip_node(c: &Console, strip: &str) -> Option<&'static crate::map::Node> {
    let strip = strip.trim_end_matches('/');
    let kinds = ["/ch/", "/auxin/", "/fxrtn/", "/bus/", "/mtx/"];
    let ok = kinds.iter().any(|k| strip.strip_prefix(k).is_some_and(|n| n.len() == 2 && n.bytes().all(|b| b.is_ascii_digit())))
        || strip == "/main/st"
        || strip == "/main/m";
    if !ok {
        return None;
    }
    match c.tree.find(strip)? {
        Found::Node(n) => Some(n),
        Found::Leaf(_) => None,
    }
}

fn quoted(s: &str) -> String {
    format!("\"{}\"", s.replace('"', ""))
}

/// The strip at `strip` (`/ch/01`...) as a channel preset named `name`;
/// None for an address that isn't a strip.
pub fn write(c: &Console, strip: &str, name: &str) -> Option<String> {
    let tree = c.tree;
    let node = strip_node(c, strip)?;
    let prefix = format!("{}/", node.path);
    let mut out = format!("{VERSION} {} 1 %111111111 1\n", quoted(name));
    for n in tree.nodes.iter().filter(|n| n.path.starts_with(&prefix) && n.leaves.iter().any(|&i| tree.entries[i].listed)) {
        if let Some(line) = tree.node_line(c, Found::Node(n)) {
            out.push_str(&line[node.path.len()..]);
            out.push('\n');
        }
    }
    Some(out)
}

/// Sets the strip at `strip` from a channel preset's text; None for an
/// address that isn't a strip.
pub fn read(c: &Console, strip: &str, text: &str) -> Option<Read> {
    let tree = c.tree;
    let node = strip_node(c, strip)?;
    let mut r = Read::default();
    for line in text.lines().map(str::trim).filter(|l| !l.is_empty()) {
        let f = fields(line);
        let Some(path) = f.first().map(String::as_str) else { continue };
        if line.starts_with('#') {
            // The header (`#2.1# "name" ...`; any version), or a comment.
            if path.len() > 2 && path.ends_with('#') && r.applied + r.skipped == 0 {
                r.name = line.split('"').nth(1).unwrap_or_default().to_owned();
            }
            continue;
        }
        let found = (path.starts_with('/') && !path.starts_with("/headamp"))
            .then(|| tree.find(&format!("{}{path}", node.path)))
            .flatten();
        let Some(found) = found else {
            r.skipped += 1;
            continue;
        };
        // `/config`: the name, icon and colour; the strip keeps its source.
        let values = if path == "/config" { &f[1..f.len().min(4)] } else { &f[1..] };
        crate::sysex::set_values(c, tree.group(found), values);
        r.applied += 1;
    }
    Some(r)
}

/// INIT: the strip at `strip` back to an X32's factory values (a freshly
/// configured X32's strip, as the scene file test checks against a real
/// one), keeping its source as a preset does. Returns how many lines it set;
/// None for an address that isn't a strip.
pub fn init(c: &Console, strip: &str) -> Option<usize> {
    strip_node(c, strip)?;
    let fresh = Engine::new(0);
    fresh.configure(&ConsoleConfig { profile: Profile::X32, counts: Counts::X32 });
    let state = State::default();
    let factory = write(&Console::new(&fresh, &state), strip, "")?;
    read(c, strip, &factory).map(|r| r.applied)
}
