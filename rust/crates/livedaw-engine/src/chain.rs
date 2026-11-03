//! A strip's processing as a chain of nodes (the Universal core, Phase 1
//! step 3): processing blocks, and markers where signals are tapped. The
//! strip runs its chain node by node (`Strip::run_chain`).
//!
//! The WING's strips are templates of this chain ([`template`]): input
//! channels run INPUT, FILTER, then the GATE, EQ, DYNAMICS and INS 1 slots in
//! the strip's process order (the four [`Node::Slot`]s, resolved through
//! `PROCESS_ORDERS` each block, so the 24 orders are permutations of one
//! group), then the fader, INS 2 and the post-fader delay; aux channels delay
//! before the fader and run DYNAMICS, EQ, INS 1; buses, mains and matrices run
//! DYNAMICS and INS 1 before the fader and their EQ after it. Tap markers sit
//! where the WING takes its taps (`TAP_POINTS`, the key taps of `keytap.rs`),
//! so TAP 3 / 4 / 5 stay after the 1st / 2nd / 3rd block whatever the order.

use crate::analyser::rta_tap;
use crate::keytap::{TAP_EQ_POST, TAP_INS2};
use crate::params::StripKind;
use crate::strip::tap;

/// A step of a strip's chain.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Node {
    /// INPUT: trim, balance and polarity (and the trim meters, the LR scope).
    Input,
    /// The strip's delay (`InputParams::delay*`).
    Delay,
    /// FILTER: low cut, high cut, tool filter.
    Filter,
    /// The GATE slot.
    Gate,
    /// The EQ (with its RTA points and BAND SOLO).
    Eq,
    /// The DYNAMICS slot.
    Dynamics,
    /// INS 1.
    PreInsert,
    /// INS 2 (an FX slot, or automix: its level is measured here).
    PostInsert,
    /// Block `k` (0-3) of the input channel's process order (GATE, EQ,
    /// DYNAMICS or INS 1).
    Slot(u8),
    /// PFL: the pre-fader copy (solo, direct outs, the METERS screen).
    Pfl,
    /// The fader with the DCAs, mutes, mute groups and MAIN LINK.
    Fader,
    /// POST: the post-processing copy (POST sends, the output stage).
    Post,
    /// TAP EQ and TAP WIDTH on the TAP-mode signal (input channels).
    TapShaper,
    /// A tap point (`strip::tap`, `keytap::TAP_EQ_POST`, `TAP_INS2`): the
    /// TAP-mode copy when it is the strip's TAP point, and the key tap.
    Tap(u8),
    /// An RTA point (`analyser::rta_tap`).
    Rta(u8),
}

const fn t(point: usize) -> Node {
    Node::Tap(point as u8)
}

const fn rta(point: usize) -> Node {
    Node::Rta(point as u8)
}

/// Input channels (Fig. 34 of the WING manual; TAP 3/4/5 after the 1st, 2nd
/// and 3rd block of the process order).
pub(crate) const CHANNEL: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    Node::Filter,
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Slot(0),
    t(2),
    Node::Slot(1),
    t(3),
    Node::Slot(2),
    t(4),
    Node::Slot(3),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    Node::PostInsert,
    Node::Delay,
    Node::Post,
    t(tap::POST),
    Node::TapShaper,
];

/// Aux channels (Fig. 68): the delay before the fader, DYNAMICS, EQ, INS 1.
pub(crate) const AUX: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    Node::Delay,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Dynamics,
    t(2),
    Node::Eq,
    t(3),
    Node::PreInsert,
    t(4),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    Node::PostInsert,
    t(TAP_INS2),
    Node::Post,
    t(tap::POST),
];

/// Buses, mains and matrices: DYNAMICS and INS 1 before the fader, the EQ after it.
pub(crate) const OUTPUT: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Dynamics,
    t(2),
    Node::PreInsert,
    t(3),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    Node::Eq,
    t(TAP_EQ_POST),
    Node::PostInsert,
    t(TAP_INS2),
    Node::Delay,
    Node::Post,
    t(tap::POST),
];

/// The X32's aux in and FX return channels: trim and polarity, the EQ, the
/// fader; no delay, dynamics or inserts (the X32's OSC tree has none). The
/// WING's tap points stay, so taps and keys read them where they are.
pub(crate) const X32_AUX: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    t(2),
    Node::Eq,
    t(3),
    t(4),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    t(TAP_INS2),
    Node::Post,
    t(tap::POST),
];

/// The WING's chain for a strip of `kind`.
pub fn template(kind: StripKind) -> &'static [Node] {
    match kind {
        StripKind::Channel => CHANNEL,
        StripKind::Aux => AUX,
        StripKind::Bus | StripKind::Main | StripKind::Matrix => OUTPUT,
    }
}

/// The block a process order letter names (`PROCESS_ORDERS`: G, E, D, I).
pub fn order_block(letter: u8) -> Option<Node> {
    match letter {
        b'G' => Some(Node::Gate),
        b'E' => Some(Node::Eq),
        b'D' => Some(Node::Dynamics),
        b'I' => Some(Node::PreInsert),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_dsp::wing::PROCESS_ORDERS;

    #[test]
    fn templates_hold_each_block_and_tap_once() {
        for kind in StripKind::ALL {
            let chain = template(kind);
            let count = |n: Node| chain.iter().filter(|&&x| x == n).count();
            for n in [Node::Input, Node::Delay, Node::Pfl, Node::Fader, Node::PostInsert, Node::Post] {
                assert_eq!(count(n), 1, "{kind:?} {n:?}");
            }
            for point in [tap::IN, tap::FILT, tap::PFL, tap::AFL, tap::POST] {
                assert_eq!(count(t(point)), 1, "{kind:?} tap {point}");
            }
            // PFL before the fader, the fader before POST.
            let at = |n: Node| chain.iter().position(|&x| x == n).unwrap();
            assert!(at(Node::Pfl) < at(Node::Fader) && at(Node::Fader) < at(Node::Post), "{kind:?}");
        }
        // Every process order names the four blocks once each.
        for order in PROCESS_ORDERS {
            let mut blocks: Vec<_> = order.bytes().map(|b| order_block(b).unwrap()).collect();
            blocks.sort_by_key(|b| format!("{b:?}"));
            assert_eq!(blocks, [Node::Dynamics, Node::Eq, Node::Gate, Node::PreInsert]);
        }
    }
}
