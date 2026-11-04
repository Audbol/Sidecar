//! X32 scene files (`.scn`, Phase 4 step 7): plain text, a header, then one
//! `/node`-style line per parameter group, the path and its values in real
//! units (OSC document p.43).
//!
//! ```text
//! #2.7# "Band" "Soundcheck" %000000000 1
//! /config/chlink ON OFF ...
//! /ch/01/config "Kick" 1 RD 1
//! ```
//!
//! Writing walks the tree as the server's `/node` answers it, `/config` first
//! as an X32 writes it; reading sets each line's values as the server's `/`
//! command does (a linked pair's partner too). What Sidecar doesn't emulate
//! (outputs, head amps...) is left out of a written file and skipped
//! in a read one.

use crate::map::{Console, Found};
use crate::value::fields;

/// The header's version tag (firmware 2.7 and later write it).
pub const VERSION: &str = "#2.7#";

/// What reading a scene file did.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Read {
    pub name: String,
    pub note: String,
    /// Lines whose values were set.
    pub applied: usize,
    /// Lines with a path Sidecar doesn't have (skipped).
    pub skipped: usize,
}

fn quoted(s: &str) -> String {
    format!("\"{}\"", s.replace('"', ""))
}

/// A scene file of the console as it is now.
pub fn write(c: &Console, name: &str, note: &str) -> String {
    let tree = c.tree;
    let mut out = format!("{VERSION} {} {} %000000000 1\n", quoted(name), quoted(note));
    let groups = tree.nodes.iter().filter(|n| !n.path.starts_with("/-") && n.leaves.iter().any(|&i| tree.entries[i].listed));
    let (config, rest): (Vec<_>, Vec<_>) = groups.partition(|n| n.path.starts_with("/config"));
    for n in config.into_iter().chain(rest) {
        if let Some(line) = tree.node_line(c, Found::Node(n)) {
            out.push_str(&line);
            out.push('\n');
        }
    }
    out
}

/// Sets the console from a scene file's text.
pub fn read(c: &Console, text: &str) -> Read {
    let tree = c.tree;
    let mut r = Read::default();
    for line in text.lines().map(str::trim).filter(|l| !l.is_empty()) {
        let f = fields(line);
        if line.starts_with('#') {
            // The header (`#2.7# "name" "note" %safes 1`; any version), or a comment.
            let header = f[0].len() > 2 && f[0].ends_with('#') && r.applied + r.skipped == 0;
            if header {
                r.name = f.get(1).cloned().unwrap_or_default();
                r.note = f.get(2).cloned().unwrap_or_default();
            }
            continue;
        }
        let Some(found) = f.first().filter(|p| !p.starts_with("/-")).and_then(|p| tree.find(p)) else {
            r.skipped += 1;
            continue;
        };
        // Values in the node's order; fewer than all set the first ones.
        crate::sysex::set_values(c, tree.group(found), &f[1..]);
        r.applied += 1;
    }
    r
}
