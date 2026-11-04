//! OSC over MIDI SysEx (Phase 4 step 6b): an X32 takes OSC commands as text in
//! a SysEx message, `F0 00 20 32 32 <text> F7`, the address, a space, then the
//! values in real units or as their names (`/ch/01/mix/on OFF`,
//! `/ch/01/eq/2/f 1020`; OSC document p.139). The framing is the host's; this
//! is what the text does.
//!
//! An address with values sets them as the `/` command and a scene file's line
//! do (a node's values in its order, fewer than all set the first ones). An
//! address alone is a query, answered with its `/node` line: Sidecar's
//! reading, as the document doesn't say what an X32 answers over MIDI.

use crate::map::Console;
use crate::value::fields;

/// The SysEx header before the text: Behringer's manufacturer id, then 0x32.
pub const HEADER: [u8; 5] = [0xF0, 0x00, 0x20, 0x32, 0x32];

/// What a text command did.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Done {
    /// Values were set (how many).
    Set(usize),
    /// A query's answer: the address's `/node` line.
    Answer(String),
    /// An address the console doesn't have, or no address.
    Unknown,
}

/// Runs one command's text on the console.
pub fn command(c: &Console, text: &str) -> Done {
    let f = fields(text.trim());
    let tree = c.tree;
    let Some(found) = f.first().and_then(|p| tree.find(p)) else { return Done::Unknown };
    if f.len() == 1 {
        return tree.node_line(c, found).map_or(Done::Unknown, Done::Answer);
    }
    Done::Set(set_values(c, tree.group(found), &f[1..]))
}

/// Sets `leaves` from `values` (texts, in order); returns how many were set.
pub(crate) fn set_values(c: &Console, leaves: Vec<usize>, values: &[String]) -> usize {
    let mut n = 0;
    for (i, t) in leaves.into_iter().zip(values) {
        if let Some(v) = c.ty(i).parse(t) {
            c.set(i, v);
            n += 1;
        }
    }
    n
}
