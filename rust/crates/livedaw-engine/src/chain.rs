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
    /// The attenuator, where a console has it after the gate (the DM2000's):
    /// `InputParams::trim_db`, INPUT leaving it to this.
    Attenuator,
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

/// The X32's input channels: the delay before the low cut (the filter's low
/// cut), the gate, then the compressor before or after the EQ and the insert
/// before or after both (the three `Slot`s after the gate, by the strip's
/// process order: `X32_ORDERS`), the fader. One insert, no TAP EQ.
pub(crate) const X32_CHANNEL: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    Node::Delay,
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
    t(TAP_INS2),
    Node::Post,
    t(tap::POST),
];

/// The X32 channel's process orders: the gate first, then (index 0, the
/// X32's default) EQ, compressor, insert; (1) compressor PRE the EQ; (2) the
/// insert PRE; (3) both PRE. Index = compressor PRE + 2 x insert PRE.
pub(crate) const X32_ORDERS: &[&str] = &["GEDI", "GDEI", "GIED", "GIDE"];

/// The X32's buses, mains and matrices: the EQ, the compressor and the insert
/// before the fader (`Slot`s, the profile's output order), nothing after it.
pub(crate) const X32_OUTPUT: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Slot(0),
    t(2),
    Node::Slot(1),
    t(3),
    Node::Slot(2),
    t(TAP_EQ_POST),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    t(TAP_INS2),
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

/// The ProMix 01's input channels (User's Guide p.8): phase (INPUT), the
/// 3-band EQ, then a dynamics processor's patch point (post-EQ, pre-fader:
/// p.72; the block is on only where one of its three processors is patched),
/// the fader. No delay, filter, gate or insert.
pub(crate) const PROMIX01_CHANNEL: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Eq,
    t(2),
    Node::Dynamics,
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

/// The ProMix 01's input channels have no process orders (one, unused).
pub(crate) const PROMIX01_ORDERS: &[&str] = &["ED"];

/// The ProMix 01's sends (SEND1-4): the fader (SEND3-4's master; SEND1-2 have
/// none and stay at 0 dB), and a dynamics patch point before or after it
/// (`Slot`s by the strip's output order, `PROMIX01_OUTPUT_ORDERS`: p.74).
pub(crate) const PROMIX01_SEND: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Slot(0),
    t(2),
    t(3),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    Node::Slot(1),
    t(TAP_EQ_POST),
    t(TAP_INS2),
    Node::Post,
    t(tap::POST),
];

/// ST OUT: its 3-band EQ, then the dynamics patch point before or after the
/// fader, as the sends'.
pub(crate) const PROMIX01_MAIN: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Eq,
    t(2),
    Node::Slot(0),
    t(3),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    Node::Slot(1),
    t(TAP_EQ_POST),
    t(TAP_INS2),
    Node::Post,
    t(tap::POST),
];

/// The output patch points of SEND3-4 and ST OUT (the strip's process order):
/// 0 POST fader (as the console starts, p.74), 1 PRE. A letter that names no
/// block (`-`) leaves its slot empty.
pub(crate) const PROMIX01_OUTPUT_ORDERS: &[&str] = &["-D", "D-"];

/// The 02R's input channels and tape returns (MAN p.193): the attenuator and
/// phase (INPUT), the 4-band EQ, the dynamics processor (keyed PRE or POST EQ:
/// MAN p.70), the delay, then the fader. Sidecar's reading of where the delay
/// sits (the guide's block diagram puts it before the fader). No filter, gate
/// or insert.
pub(crate) const Y02R_CHANNEL: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Eq,
    t(2),
    Node::Dynamics,
    t(3),
    Node::Delay,
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

/// The 02R's effect returns EFF1-2: attenuator and phase, the EQ, the delay,
/// the fader; no dynamics (MAN p.197).
pub(crate) const Y02R_RETURN: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Eq,
    t(2),
    t(3),
    Node::Delay,
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

/// The 02R's BUS 1-8: the dynamics processor, then the master; no EQ (MAN p.197).
pub(crate) const Y02R_BUS: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Dynamics,
    t(2),
    t(3),
    t(TAP_EQ_POST),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    t(TAP_INS2),
    Node::Post,
    t(tap::POST),
];

/// The 02R's AUX 1-8: the master alone, no EQ or dynamics.
pub(crate) const Y02R_AUX: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    t(2),
    t(3),
    t(TAP_EQ_POST),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    t(TAP_INS2),
    Node::Post,
    t(tap::POST),
];

/// The 01V's CH 1-16 (Owner's Manual p.60): attenuator, 4-band EQ, dynamics,
/// delay, ON, fader: the 02R's channel.
pub(crate) const Y01V_CHANNEL: &[Node] = Y02R_CHANNEL;

/// The 01V's CH 17-24 (the Option I/O card's, p.60): attenuator, the 2-band EQ,
/// ON, fader; no dynamics or delay.
pub(crate) const Y01V_OPTION: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Eq,
    t(2),
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

/// The 01V's effect returns (p.25): the 4-band EQ, ON, level; no delay either.
pub(crate) const Y01V_RETURN: &[Node] = Y01V_OPTION;

/// The 01V's BUS 1-4 and EFFECT 1-2: the master alone (p.24).
pub(crate) const Y01V_BUS: &[Node] = Y02R_AUX;

/// The 01V's AUX 1-4: 4-band EQ, dynamics, the master (p.24).
pub(crate) const Y01V_AUX: &[Node] = Y02R_STEREO;

/// The 01V's STEREO: 4-band EQ, dynamics, fader, ON, then the output delay (p.24).
pub(crate) const Y01V_STEREO: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Eq,
    t(2),
    Node::Dynamics,
    t(3),
    t(TAP_EQ_POST),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    t(TAP_INS2),
    Node::Delay,
    Node::Post,
    t(tap::POST),
];

/// The 02R's STEREO: its attenuator, the 4-band EQ, the dynamics processor, the
/// fader (MAN p.197).
pub(crate) const Y02R_STEREO: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Eq,
    t(2),
    Node::Dynamics,
    t(3),
    t(TAP_EQ_POST),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    t(TAP_INS2),
    Node::Post,
    t(tap::POST),
];

/// The SQ's input channels (Reference Guide p.126): trim and polarity, HPF,
/// gate, insert, PEQ, compressor, delay, then mute and fader.
pub(crate) const SQ_CHANNEL: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    Node::Filter,
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Gate,
    t(2),
    Node::PreInsert,
    Node::Eq,
    t(3),
    Node::Dynamics,
    t(4),
    Node::Delay,
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    t(TAP_INS2),
    Node::Post,
    t(tap::POST),
];

/// The SQ's FX returns (p.127): the PEQ, then mute and fader.
pub(crate) const SQ_RETURN: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Eq,
    t(2),
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

/// The SQ's mixes, LR and matrices (p.126): insert, PEQ (the 28-band GEQ comes
/// before it, with the RackFX), compressor, delay, then mute and fader.
pub(crate) const SQ_MIX: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::PreInsert,
    Node::Eq,
    t(2),
    Node::Dynamics,
    t(3),
    t(TAP_EQ_POST),
    Node::Delay,
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    t(TAP_INS2),
    Node::Post,
    t(tap::POST),
];

/// The SQ's FX sends (p.127): mute and fader alone.
pub(crate) const SQ_FX_SEND: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    t(2),
    t(3),
    t(TAP_EQ_POST),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    t(TAP_INS2),
    Node::Post,
    t(tap::POST),
];

/// The dLive's input channels (the datasheets' block diagram): trim and
/// polarity, filters, gate, Insert A, the PEQ and the compressor in the
/// channel's order (the PEQ first, or the compressor first:
/// [`DLIVE_ORDERS`]), Insert B, delay, then mute and fader.
pub(crate) const DLIVE_CHANNEL: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    Node::Filter,
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Gate,
    t(crate::dlive::TAP_POST_GATE),
    Node::PreInsert,
    t(2), // Post Insert A
    Node::Slot(0),
    t(3),
    Node::Slot(1),
    t(4),
    Node::PostInsert,
    t(crate::dlive::TAP_POST_INSERT_B),
    Node::Delay,
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    t(TAP_INS2),
    Node::Post,
    t(tap::POST),
];

/// The dLive's FX returns: the PEQ, then mute and fader: the SQ's.
pub(crate) const DLIVE_RETURN: &[Node] = SQ_RETURN;

/// The dLive's Groups, Aux, Main and Matrices: insert, PEQ (the 28-band GEQ
/// follows it, a block: `factory_eq_block`), compressor, delay, then mute and
/// fader: the SQ's.
pub(crate) const DLIVE_MIX: &[Node] = SQ_MIX;

/// The dLive's FX sends: mute and fader alone.
pub(crate) const DLIVE_FX_SEND: &[Node] = SQ_FX_SEND;

/// A dLive input's compressor comes after the PEQ, or before it (per channel).
pub(crate) const DLIVE_ORDERS: &[&str] = &["ED", "DE"];

/// The SQ's strips have one fixed order of processing.
pub(crate) const SQ_ORDERS: &[&str] = &["GED"];

/// The 02R's strips have no process orders (one, unused).
pub(crate) const Y02R_ORDERS: &[&str] = &["ED"];

/// The DM2000's Input Channels (block diagram, V2 Owner's Manual p.399; Insert
/// page p.136): phase, the gate, the attenuator and 4-band EQ, the delay, ON and
/// the fader, with the compressor and the insert PRE EQ, PRE FADER or POST FADER:
/// two `Slot`s at each of the three, by the strip's process order. The attenuator
/// and the EQ are fixed where the diagram has them. No filter block.
pub(crate) const DM2000_CHANNEL: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Gate,
    Node::Slot(0),
    Node::Slot(1),
    t(2),
    Node::Attenuator,
    Node::Eq,
    t(3),
    Node::Slot(2),
    Node::Slot(3),
    Node::Delay,
    t(4),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    Node::Slot(4),
    Node::Slot(5),
    t(TAP_INS2),
    Node::Post,
    t(tap::POST),
];

/// The DM2000's Bus Outs, Aux Sends, Matrix Sends and Stereo Out (p.399): the
/// attenuator, the 4-band EQ, the compressor and the insert PRE EQ, PRE FADER or
/// POST FADER (as a channel's), ON and the fader, then the output delay.
pub(crate) const DM2000_OUTPUT: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Slot(0),
    Node::Slot(1),
    t(2),
    Node::Attenuator,
    Node::Eq,
    t(TAP_EQ_POST),
    t(3),
    Node::Slot(2),
    Node::Slot(3),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    Node::Slot(4),
    Node::Slot(5),
    t(TAP_INS2),
    Node::Delay,
    Node::Post,
    t(tap::POST),
];

/// The DM2000's process orders: the compressor (D) and the insert (I) each PRE
/// EQ, PRE FADER or POST FADER (p.136), twelve in all: index = 3 x the
/// compressor's place + the insert's for the nine, and at a shared place COMP
/// ORDER picks Comp->Ins (those nine) or Ins->Comp (the last three). Two slots at
/// each place: slots 0-1 PRE EQ, 2-3 PRE FADER, 4-5 POST FADER.
pub(crate) const DM2000_ORDERS: &[&str] =
    &["DI----", "D-I---", "D---I-", "I-D---", "--DI--", "--D-I-", "I---D-", "--I-D-", "----DI", "ID----", "--ID--", "----ID"];

/// The PRO2's input channels (Mono input channel signal flow, Owner's Manual p.296):
/// digital gain and phase, the high and low pass filters, then the processing block
/// in its order, the mute and the fader. The same steps as an X32's channel. The
/// delay's place isn't on the diagram (p.243 says only "added to the input signal
/// processing"): by the input, Sidecar's reading.
pub(crate) const PRO2_CHANNEL: &[Node] = X32_CHANNEL;

/// The PRO2's processing orders (p.247, p.296): index 0 EQ, insert, dynamics (its
/// default), index 1 dynamics, insert, EQ. The dynamics are gate then compressor
/// (Dynamics signal flow, p.303).
pub(crate) const PRO2_ORDERS: &[&str] = &["EIGD", "GDIE"];

/// The PRO2's aux returns: their EQ and insert (Figure 24, p.264; the specification,
/// p.315: Returns signal flow, p.297, draws neither), no dynamics, the mute and fader.
pub(crate) const PRO2_AUX: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Eq,
    t(3),
    Node::PreInsert,
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

/// The PRO2's auxes, matrices and masters (Aux/group and Master signal flow, pp.298-299):
/// the compressor, insert and EQ in their order (three `Slot`s), the mute and fader;
/// the output delay after it (Sidecar's reading: p.281 gives its range, not its place).
pub(crate) const PRO2_OUTPUT: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Slot(0),
    t(2),
    Node::Slot(1),
    t(3),
    Node::Slot(2),
    t(TAP_EQ_POST),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    t(TAP_INS2),
    Node::Delay,
    Node::Post,
    t(tap::POST),
];

/// The outputs' processing orders: 0 graphic or parametric EQ, insert, dynamics
/// (its default), 1 dynamics, insert, EQ.
pub(crate) const PRO2_OUTPUT_ORDERS: &[&str] = &["EID", "DIE"];

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

/// The XENYX Q1202USB's channels (the panel's order, Sidecar's reading of the signal
/// flow: the guide gives no block diagram in the part read): the preamp's gain (INPUT),
/// the low cut (channels 1-4), the soft clip (the GATE slot), the compressor (channels
/// 1-4), the 3-band EQ (channels 1-4), then the LEVEL control.
pub(crate) const XENYX1202_CHANNEL: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    rta(rta_tap::PRE_FILTER),
    Node::Filter,
    t(tap::FILT),
    rta(rta_tap::POST_FILTER),
    Node::Gate,
    t(2),
    Node::Dynamics,
    t(3),
    Node::Eq,
    t(4),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    Node::Post,
    t(tap::POST),
];

/// The XENYX Q1202USB's 2-TR/USB input, its FX send and MAIN MIX: a level control and
/// nothing else.
pub(crate) const XENYX1202_PLAIN: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    Node::Post,
    t(tap::POST),
];

/// Its channels' process order (one, unused: the blocks sit in the template's order).
pub(crate) const XENYX1202_ORDERS: &[&str] = &["GDE"];
/// Its outputs have no blocks.
pub(crate) const XENYX1202_OUTPUT_ORDERS: &[&str] = &["---"];

/// The plug-in host's channels: the input's trim and polarity, then whatever blocks the user
/// puts in the chain (the plug-ins), then the level, then the channel's delay (the host's latency
/// alignment sets it in samples: it makes every channel as late as the longest rack). Its mains,
/// which nothing feeds, run `XENYX1202_PLAIN`.
pub(crate) const PLUGINHOST_CHANNEL: &[Node] = &[
    rta(rta_tap::IN),
    Node::Input,
    t(tap::IN),
    Node::Pfl,
    t(tap::PFL),
    Node::Fader,
    t(tap::AFL),
    rta(rta_tap::POST_FADER),
    Node::Delay,
    Node::Post,
    t(tap::POST),
];

/// Its channels' process order (unused: it has no blocks of its own). Its outputs have none.
pub(crate) const PLUGINHOST_ORDERS: &[&str] = &["---"];

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
