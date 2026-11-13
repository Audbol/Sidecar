//! X32 effect presets (`.efx`) and routing presets (`.rou`), the LIBRARY's
//! other two file types: the scene file's text format for one effect slot
//! or for the routing.
//!
//! ```text
//! #2.1# "Big Hall" 2 %1 1
//! /fx HALL
//! /fx/source MIX5 MIX5
//! /fx/par 0.50 0.30 ...
//! ```
//!
//! An effect preset's lines are the slot's `/fx/N` lines with the slot's
//! number left out, so it loads into any slot (the model line first: the
//! parameters are typed by it). A routing preset holds the console's
//! `/config/routing` and `/outputs` lines as a scene file writes them. The
//! headers follow the X32's library entries: the name, its type, the elements
//! it holds and that it has data. From public descriptions only, not compared
//! with a file an X32 wrote. Reading takes any version and only the name from
//! the header.

use crate::map::{Console, Found};
use crate::scn::Read;
use crate::value::fields;

/// The header's version tag, as library presets carry it.
pub const VERSION: &str = "#2.1#";

fn quoted(s: &str) -> String {
    format!("\"{}\"", s.replace('"', ""))
}

/// The slot's node path (`/fx/1`-`/fx/8`), or None.
fn slot(address: &str) -> Option<String> {
    let n = address.trim_end_matches('/').strip_prefix("/fx/")?;
    (n.len() == 1 && matches!(n.as_bytes()[0], b'1'..=b'8')).then(|| format!("/fx/{n}"))
}

/// The effect in slot `address` (`/fx/1`-`/fx/8`) as an effect preset named
/// `name`; None for an address that isn't a slot.
pub fn efx_write(c: &Console, address: &str, name: &str) -> Option<String> {
    let tree = c.tree;
    let base = slot(address)?;
    let prefix = format!("{base}/");
    let mut nodes: Vec<_> = tree
        .nodes
        .iter()
        .filter(|n| (n.path == base || n.path.starts_with(&prefix)) && n.leaves.iter().any(|&i| tree.entries[i].listed))
        .collect();
    nodes.sort_by_key(|n| n.path != base);
    let mut out = format!("{VERSION} {} 2 %1 1\n", quoted(name));
    for n in nodes {
        if let Some(line) = tree.node_line(c, Found::Node(n)) {
            out.push_str("/fx");
            out.push_str(&line[base.len()..]);
            out.push('\n');
        }
    }
    Some(out)
}

/// Sets slot `address` from an effect preset's text; None for an address
/// that isn't a slot.
pub fn efx_read(c: &Console, address: &str, text: &str) -> Option<Read> {
    let base = slot(address)?;
    Some(read_lines(c, text, |path| {
        let rest = path.strip_prefix("/fx")?;
        (rest.is_empty() || rest.starts_with('/')).then(|| format!("{base}{rest}"))
    }))
}

/// The console's routing as a routing preset named `name`.
pub fn rou_write(c: &Console, name: &str) -> String {
    let tree = c.tree;
    let mut out = format!("{VERSION} {} 3 %11 1\n", quoted(name));
    for n in tree.nodes.iter().filter(|n| is_routing(&n.path) && n.leaves.iter().any(|&i| tree.entries[i].listed)) {
        if let Some(line) = tree.node_line(c, Found::Node(n)) {
            out.push_str(&line);
            out.push('\n');
        }
    }
    out
}

/// Sets the routing from a routing preset's text (lines of anything else are skipped).
pub fn rou_read(c: &Console, text: &str) -> Read {
    read_lines(c, text, |path| is_routing(path).then(|| path.to_owned()))
}

fn is_routing(path: &str) -> bool {
    path.starts_with("/config/routing") || path.starts_with("/outputs/")
}

/// Reads the lines of a preset, each path mapped to the console's by `target`.
fn read_lines(c: &Console, text: &str, target: impl Fn(&str) -> Option<String>) -> Read {
    let tree = c.tree;
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
        let Some(found) = target(path).and_then(|p| tree.find(&p)) else {
            r.skipped += 1;
            continue;
        };
        crate::sysex::set_values(c, tree.group(found), &f[1..]);
        r.applied += 1;
    }
    r
}
