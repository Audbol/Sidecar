//! A strip's chain through the C ABI (ABI 28, the Universal Mixer, Phase 2
//! step 2): its steps in processing order, its extra blocks with their
//! parameters and meters (`livedaw_engine::blocks`) and its user taps
//! (`livedaw_engine::taps`). Message thread, like the engine calls they wrap.
//!
//! An `LdBlock`'s pointers stay valid until the block is removed (with its
//! strip, `ld_engine_clear_chain`, a reset or a new configuration) *and*
//! `ld_engine_collect` has run, as strip pointers do. The strip passed with
//! an engine must be one of that engine's.

use crate::{engine, guard, models, strip};
use livedaw_engine::blocks::{Block, BlockKind, BlockMeters, BlockParams, Step};
use livedaw_engine::chain::Node;
use livedaw_engine::profile::DynSlot;
use livedaw_engine::{Engine, Strip};
use std::ffi::c_void;

/// Step kinds (`LdChainStep::kind`): the nodes of a strip's template, then
/// an extra block.
pub mod step {
    pub const INPUT: i32 = 0;
    pub const DELAY: i32 = 1;
    pub const FILTER: i32 = 2;
    pub const GATE: i32 = 3;
    pub const EQ: i32 = 4;
    pub const DYNAMICS: i32 = 5;
    pub const PRE_INSERT: i32 = 6;
    pub const POST_INSERT: i32 = 7;
    /// A process order slot that runs no block.
    pub const SLOT: i32 = 8;
    pub const PFL: i32 = 9;
    pub const FADER: i32 = 10;
    pub const POST: i32 = 11;
    pub const TAP_SHAPER: i32 = 12;
    pub const TAP: i32 = 13;
    pub const RTA: i32 = 14;
    pub const BLOCK: i32 = 15;
}

/// A step of a strip's chain (`ld_strip_chain`).
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LdChainStep {
    /// What runs (`step`); for a process order slot, the block it runs now.
    pub kind: i32,
    /// A tap's id (the WING's 0-9, user taps from 16), an RTA point
    /// (`analyser::rta_tap`), an extra block's kind (`BlockKind` index); else -1.
    pub arg: i32,
    /// An extra block's id (unique on the strip); else -1.
    pub uid: i32,
    /// The process order slot (0-3, input channels) this step is; else -1.
    pub slot: i32,
}

impl Default for LdChainStep {
    fn default() -> Self {
        Self { kind: step::SLOT, arg: -1, uid: -1, slot: -1 }
    }
}

fn node_step(node: Node) -> (i32, i32) {
    match node {
        Node::Input => (step::INPUT, -1),
        Node::Delay => (step::DELAY, -1),
        Node::Filter => (step::FILTER, -1),
        Node::Gate => (step::GATE, -1),
        Node::Eq => (step::EQ, -1),
        Node::Dynamics => (step::DYNAMICS, -1),
        Node::PreInsert => (step::PRE_INSERT, -1),
        Node::PostInsert => (step::POST_INSERT, -1),
        Node::Slot(_) => (step::SLOT, -1),
        Node::Pfl => (step::PFL, -1),
        Node::Fader => (step::FADER, -1),
        Node::Post => (step::POST, -1),
        Node::TapShaper => (step::TAP_SHAPER, -1),
        Node::Tap(id) => (step::TAP, id as i32),
        Node::Rta(point) => (step::RTA, point as i32),
    }
}

fn describe(s: &Strip, st: &Step) -> LdChainStep {
    match st {
        Step::Block(b) => LdChainStep { kind: step::BLOCK, arg: b.kind().index() as i32, uid: b.uid() as i32, slot: -1 },
        Step::Node(Node::Slot(k)) => {
            let (kind, arg) = s.slot_block(*k).map_or((step::SLOT, -1), node_step);
            LdChainStep { kind, arg, uid: -1, slot: *k as i32 }
        }
        Step::Node(node) => {
            let (kind, arg) = node_step(*node);
            LdChainStep { kind, arg, uid: -1, slot: -1 }
        }
    }
}

/// The steps of the strip's chain, in processing order: the first `max` into
/// `out` (may be null); returns how many there are.
#[no_mangle]
pub unsafe extern "C" fn ld_strip_chain(s: *const Strip, out: *mut LdChainStep, max: i32) -> i32 {
    let Some(s) = strip(s) else { return 0 };
    let steps = guard(Vec::new(), || s.steps().iter().map(|st| describe(s, st)).collect());
    if !out.is_null() && max > 0 {
        let out = std::slice::from_raw_parts_mut(out, (max as usize).min(steps.len()));
        out.copy_from_slice(&steps[..out.len()]);
    }
    steps.len() as i32
}

/// An extra block of a strip's chain (`ld_strip_block`).
#[repr(C)]
#[derive(Debug)]
pub struct LdBlock {
    pub uid: i32,
    /// `BlockKind` index: 0 filter, 1 EQ, 2 dynamics, 3 insert, 4 delay.
    pub kind: i32,
    /// Its parameters, by kind: `FilterParams`, `EqBlockParams`,
    /// `DynSlotParams`, `InsertParams` or `blocks::DelayParams`.
    pub params: *const c_void,
    pub meters: *const BlockMeters,
}

impl Default for LdBlock {
    fn default() -> Self {
        Self { uid: -1, kind: -1, params: std::ptr::null(), meters: std::ptr::null() }
    }
}

fn find(s: &Strip, uid: i32) -> Option<&Block> {
    let uid = u32::try_from(uid).ok()?;
    s.layout()?.blocks().find(|b| b.uid() == uid).map(|b| &**b)
}

/// Block `uid` of the strip's chain into `out`; false if there's none.
#[no_mangle]
pub unsafe extern "C" fn ld_strip_block(s: *const Strip, uid: i32, out: *mut LdBlock) -> bool {
    let (Some(s), Some(out)) = (strip(s), out.as_mut()) else { return false };
    let Some(b) = find(s, uid) else { return false };
    let params = match &b.params {
        BlockParams::Filter(p) => p as *const _ as *const c_void,
        BlockParams::Eq(p) => p as *const _ as *const c_void,
        BlockParams::Dynamics(p) => p as *const _ as *const c_void,
        BlockParams::Insert(p) => p as *const _ as *const c_void,
        BlockParams::Delay(p) => p as *const _ as *const c_void,
    };
    *out = LdBlock { uid, kind: b.kind().index() as i32, params, meters: &b.meters };
    true
}

/// ABI 56: the id of a dLive input channel's preamp block (its parameters and
/// meters through `ld_strip_block`); -1 on any other strip or console.
#[no_mangle]
pub unsafe extern "C" fn ld_strip_preamp_block(s: *const Strip) -> i32 {
    let Some(s) = strip(s) else { return -1 };
    guard(-1, || s.preamp_block().map_or(-1, |b| b.uid() as i32))
}

/// Selects a model for block `uid` and loads its defaults: an EQ block's
/// (table 1, or 2 on a bus, main or matrix), a dynamics block's (table 0), a
/// filter block's tool (table 3). False for other blocks, and for a dynamics
/// model that block may not hold (`ld_dyn_model_allowed`).
#[no_mangle]
pub unsafe extern "C" fn ld_strip_set_block_model(s: *const Strip, uid: i32, model: i32) -> bool {
    let Some(s) = strip(s) else { return false };
    let Some(b) = find(s, uid) else { return false };
    let m = model.max(0) as usize;
    guard(false, || match &b.params {
        BlockParams::Eq(p) => {
            p.set_model(m);
            true
        }
        BlockParams::Dynamics(p) => {
            let preamp = s.preamp_block().is_some_and(|x| x.uid() == b.uid());
            let slot = if preamp { DynSlot::Preamp } else { DynSlot::Dynamics };
            if !models::dyn_slot_may_hold(s, p, slot, m) {
                return false;
            }
            p.set_model(m);
            true
        }
        BlockParams::Filter(p) => {
            p.set_tool_model(m);
            true
        }
        BlockParams::Insert(_) | BlockParams::Delay(_) => false,
    })
}

/// Writes the response (dB) of block `uid` at `count` frequencies, as the
/// engine runs it: an EQ or filter block's curve (`part` 0), a dynamics
/// block's key filter (0) or crossover (1). False (nothing written) else.
#[no_mangle]
pub unsafe extern "C" fn ld_strip_block_response(
    s: *const Strip,
    uid: i32,
    part: i32,
    freqs: *const f64,
    out_db: *mut f64,
    count: i32,
    sample_rate: f64,
) -> bool {
    let Some(s) = strip(s) else { return false };
    let Some(b) = find(s, uid) else { return false };
    if freqs.is_null() || out_db.is_null() || count <= 0 {
        return false;
    }
    let freqs = std::slice::from_raw_parts(freqs, count as usize);
    let out = std::slice::from_raw_parts_mut(out_db, count as usize);
    guard(false, || models::block_response(b, part, freqs, out, sample_rate))
}

/// A step index: past the end (or < 0) is the end.
fn at(at: i32) -> usize {
    usize::try_from(at).unwrap_or(usize::MAX)
}

/// Adds a block of `kind` (`BlockKind` index) at step `at` of the strip's
/// chain (< 0 or past the end: last); returns its id, or -1 (an unknown kind,
/// or the strip holds kMaxExtraBlocks already). A new EQ or dynamics block is off.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_insert_block(e: *const Engine, s: *const Strip, kind: i32, at_step: i32) -> i32 {
    let (Some(e), Some(s)) = (engine(e), strip(s)) else { return -1 };
    let Some(kind) = usize::try_from(kind).ok().and_then(BlockKind::from_index) else { return -1 };
    guard(-1, || e.insert_block(s, kind, at(at_step)).map_or(-1, |b| b.uid() as i32))
}

/// Moves block `uid` to step `at` of the chain without it, keeping its
/// settings and state.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_move_block(e: *const Engine, s: *const Strip, uid: i32, at_step: i32) -> bool {
    let (Some(e), Some(s), Ok(uid)) = (engine(e), strip(s), u32::try_from(uid)) else { return false };
    guard(false, || e.move_block(s, uid, at(at_step)))
}

#[no_mangle]
pub unsafe extern "C" fn ld_engine_remove_block(e: *const Engine, s: *const Strip, uid: i32) -> bool {
    let (Some(e), Some(s), Ok(uid)) = (engine(e), strip(s), u32::try_from(uid)) else { return false };
    guard(false, || e.remove_block(s, uid))
}

/// Back to the strip's template: no extra blocks, no user taps.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_clear_chain(e: *const Engine, s: *const Strip) {
    if let (Some(e), Some(s)) = (engine(e), strip(s)) {
        guard((), || e.clear_blocks(s));
    }
}

/// Places a user tap at step `at` of the strip's chain (< 0 or past the end:
/// last); returns its id (kFirstUserTap and up), or -1 when all are placed.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_insert_tap(e: *const Engine, s: *const Strip, at_step: i32) -> i32 {
    let (Some(e), Some(s)) = (engine(e), strip(s)) else { return -1 };
    guard(-1, || e.insert_tap(s, at(at_step)).map_or(-1, i32::from))
}

/// Moves user tap `id` to step `at` of the chain without it.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_move_tap(e: *const Engine, s: *const Strip, id: i32, at_step: i32) -> bool {
    let (Some(e), Some(s), Ok(id)) = (engine(e), strip(s), u8::try_from(id)) else { return false };
    guard(false, || e.move_tap(s, id, at(at_step)))
}

/// Takes user tap `id` out of the strip's chain (sends taking it go silent).
#[no_mangle]
pub unsafe extern "C" fn ld_engine_remove_tap(e: *const Engine, s: *const Strip, id: i32) -> bool {
    let (Some(e), Some(s), Ok(id)) = (engine(e), strip(s), u8::try_from(id)) else { return false };
    guard(false, || e.remove_tap(s, id))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        ld_engine_collect, ld_engine_counts, ld_engine_create, ld_engine_destroy, ld_engine_set_config,
        ld_engine_set_num_channels, ld_engine_strip_at, ld_strip_params, LdCounts,
    };
    use livedaw_dsp::wing::PROCESS_ORDERS;
    use livedaw_engine::blocks::{DelayParams, MAX_EXTRA_BLOCKS};
    use livedaw_engine::params::{DynSlotParams, EqBlockParams};
    use livedaw_engine::taps::FIRST_USER_TAP;
    use livedaw_engine::StripKind;
    use std::sync::atomic::Ordering::Relaxed;

    unsafe fn chain(s: *const Strip) -> Vec<LdChainStep> {
        let n = ld_strip_chain(s, std::ptr::null_mut(), 0);
        let mut v = vec![LdChainStep::default(); n as usize];
        assert_eq!(ld_strip_chain(s, v.as_mut_ptr(), n), n);
        v
    }

    unsafe fn block(s: *const Strip, uid: i32) -> LdBlock {
        let mut b = LdBlock::default();
        assert!(ld_strip_block(s, uid, &mut b), "block {uid}");
        b
    }

    fn order_step(letter: u8) -> i32 {
        match letter {
            b'G' => step::GATE,
            b'E' => step::EQ,
            b'D' => step::DYNAMICS,
            _ => step::PRE_INSERT,
        }
    }

    #[test]
    fn a_strips_chain_through_the_c_abi() {
        unsafe {
            let e = ld_engine_create(0);
            ld_engine_set_num_channels(e, 1);
            let ch = ld_engine_strip_at(e, StripKind::Channel as i32, 0);

            // The template; the process order's slots are the blocks they run now.
            let order = 7;
            (*ld_strip_params(ch)).process_order.store(order as i32, Relaxed);
            let template = chain(ch);
            let slots: Vec<_> = template.iter().filter(|s| s.slot >= 0).map(|s| (s.slot, s.kind)).collect();
            let expected: Vec<_> = PROCESS_ORDERS[order].bytes().enumerate().map(|(k, b)| (k as i32, order_step(b))).collect();
            assert_eq!(slots, expected);
            assert!(template.iter().all(|s| s.kind != step::BLOCK && s.uid == -1));
            let mut two = [LdChainStep::default(); 2];
            assert_eq!(ld_strip_chain(ch, two.as_mut_ptr(), 2), template.len() as i32, "the count, whatever fits");
            assert_eq!(two, template[..2]);
            let at = |steps: &[LdChainStep], kind: i32| steps.iter().position(|s| s.kind == kind).unwrap() as i32;

            // An EQ block before PFL: off, its parameters and meters through LdBlock.
            let eq = ld_engine_insert_block(e, ch, BlockKind::Eq.index() as i32, at(&template, step::PFL));
            assert!(eq >= 0);
            let steps = chain(ch);
            let i = at(&steps, step::BLOCK) as usize;
            assert_eq!(steps[i], LdChainStep { kind: step::BLOCK, arg: BlockKind::Eq.index() as i32, uid: eq, slot: -1 });
            assert_eq!(steps[i + 1].kind, step::PFL);
            let b = block(ch, eq);
            assert_eq!((b.uid, b.kind), (eq, BlockKind::Eq.index() as i32));
            let p = &*(b.params as *const EqBlockParams);
            assert!(!p.on.load(Relaxed), "a new EQ block is off");
            assert_eq!((*b.meters).out_peak.load(), 0.0);
            let BlockParams::Eq(engine_side) = &(*ch).blocks()[0].params else { panic!() };
            assert!(std::ptr::eq(p, engine_side), "the engine's own parameters");

            // Its curve: flat while off, +6 dB at the low shelf when on.
            let freqs = [30.0, 10000.0];
            let mut db = [1.0; 2];
            assert!(ld_strip_block_response(ch, eq, 0, freqs.as_ptr(), db.as_mut_ptr(), 2, 48000.0));
            assert!(db.iter().all(|d| d.abs() < 0.01), "{db:?}");
            p.param("lg").unwrap().store(6.0);
            p.on.store(true, Relaxed);
            assert!(ld_strip_block_response(ch, eq, 0, freqs.as_ptr(), db.as_mut_ptr(), 2, 48000.0));
            assert!(db[0] > 5.0 && db[1].abs() < 0.5, "{db:?}");
            assert!(!ld_strip_block_response(ch, eq, 1, freqs.as_ptr(), db.as_mut_ptr(), 2, 48000.0), "an EQ has one curve");
            assert!(ld_strip_set_block_model(ch, eq, 1));
            assert_eq!(p.model.load(Relaxed), 1);

            // A dynamics block (key filter and crossover curves), an insert (no
            // model), a delay; an unknown kind and a missing block.
            let dynamics = ld_engine_insert_block(e, ch, BlockKind::Dynamics.index() as i32, -1);
            let d = &*(block(ch, dynamics).params as *const DynSlotParams);
            assert!(!d.on.load(Relaxed));
            assert!(ld_strip_set_block_model(ch, dynamics, 3));
            assert_eq!(d.model_index(), 3);
            for part in 0..2 {
                assert!(ld_strip_block_response(ch, dynamics, part, freqs.as_ptr(), db.as_mut_ptr(), 2, 48000.0));
            }
            let insert = ld_engine_insert_block(e, ch, BlockKind::Insert.index() as i32, 0);
            assert!(!ld_strip_set_block_model(ch, insert, 1));
            let delay = ld_engine_insert_block(e, ch, BlockKind::Delay.index() as i32, 0);
            let dp = &*(block(ch, delay).params as *const DelayParams);
            dp.delay.store(12.0);
            assert_eq!(ld_engine_insert_block(e, ch, 5, 0), -1);
            assert_eq!(ld_engine_insert_block(e, std::ptr::null(), 0, 0), -1);
            assert!(!ld_strip_block(ch, 999, &mut LdBlock::default()));
            assert_eq!(chain(ch).iter().filter(|s| s.kind == step::BLOCK).count(), 4);

            // Moved last: the delay keeps its settings.
            assert!(ld_engine_move_block(e, ch, delay, -1));
            assert_eq!(chain(ch).last().unwrap().uid, delay);
            assert_eq!((*(block(ch, delay).params as *const DelayParams)).delay.load(), 12.0);
            assert!(!ld_engine_move_block(e, ch, 999, 0));

            // User taps: placed, moved, taken out; the WING's stay.
            let tap = ld_engine_insert_tap(e, ch, 0);
            assert!(tap >= FIRST_USER_TAP as i32);
            assert_eq!(chain(ch)[0], LdChainStep { kind: step::TAP, arg: tap, uid: -1, slot: -1 });
            assert!(ld_engine_move_tap(e, ch, tap, -1));
            assert_eq!(chain(ch).last().unwrap().arg, tap);
            assert!(!ld_engine_move_tap(e, ch, 2, 0), "a WING tap");
            assert!(ld_engine_remove_tap(e, ch, tap));
            assert!(!ld_engine_remove_tap(e, ch, tap));

            // Removed, then the rest cleared: the template again.
            assert!(ld_engine_remove_block(e, ch, eq));
            assert!(!ld_engine_remove_block(e, ch, eq));
            ld_engine_insert_tap(e, ch, 3);
            ld_engine_clear_chain(e, ch);
            assert_eq!(chain(ch), template);
            for _ in 0..MAX_EXTRA_BLOCKS {
                assert!(ld_engine_insert_block(e, ch, BlockKind::Filter.index() as i32, -1) >= 0);
            }
            assert_eq!(ld_engine_insert_block(e, ch, BlockKind::Filter.index() as i32, -1), -1, "at most kMaxExtraBlocks");
            ld_engine_collect(e);

            // A console's structure without its channels (a show load's first step).
            let counts = LdCounts { channels: 48, aux: 4, buses: 24, mains: 2, matrices: 6, dcas: 32, mute_groups: 12, fx: 20 };
            let mut used = LdCounts::default();
            ld_engine_set_config(e, &counts, &mut used);
            assert_eq!(used, counts, "the counts configured");
            let mut now = LdCounts::default();
            ld_engine_counts(e, &mut now);
            assert_eq!((now.channels, now.aux, now.buses, now.mains, now.matrices), (0, 4, 24, 2, 6));
            assert_eq!((now.dcas, now.mute_groups), (32, 12));
            ld_engine_set_config(e, std::ptr::null(), std::ptr::null_mut());
            ld_engine_counts(e, &mut now);
            assert_eq!((now.buses, now.mains, now.matrices), (16, 4, 8), "null: the WING");
            ld_engine_collect(e);
            ld_engine_destroy(e);
        }
    }

    #[test]
    fn a_dlive_channels_preamp_block_through_the_c_abi() {
        use crate::{ld_engine_configure_profile, ld_strip_set_model};
        use livedaw_dsp::model::model_index;
        const DLIVE: i32 = 7;
        unsafe {
            let e = ld_engine_create(0);
            assert_eq!(ld_strip_preamp_block(std::ptr::null()), -1);
            ld_engine_configure_profile(e, DLIVE);
            let ch = ld_engine_strip_at(e, StripKind::Channel as i32, 0);
            let uid = ld_strip_preamp_block(ch);
            assert!(uid >= 0);
            let steps = chain(ch);
            let input = steps.iter().position(|s| s.kind == step::INPUT).unwrap();
            assert_eq!(
                steps[input + 1],
                LdChainStep { kind: step::BLOCK, arg: BlockKind::Dynamics.index() as i32, uid, slot: -1 }
            );

            let b = block(ch, uid);
            let d = &*(b.params as *const DynSlotParams);
            assert!(!d.on.load(Relaxed));
            let dlive_models = d.table();
            let at = |token: &str| model_index(dlive_models, token).unwrap() as i32;
            assert_eq!(d.model_index() as i32, at("DLVVALVE"));

            // The preamp block takes the Tube Stage, not a compressor.
            assert!(ld_strip_set_block_model(ch, uid, at("DLVTUBE")));
            assert_eq!(d.model_index() as i32, at("DLVTUBE"));
            assert!(!ld_strip_set_block_model(ch, uid, at("DLV16T")));
            assert_eq!(d.model_index() as i32, at("DLVTUBE"), "refused");

            // The gate and compressor slots don't take the preamp models.
            let gate = (*ch).params.gate();
            let before = gate.model_index();
            ld_strip_set_model(ch, 0, at("DLVVALVE"));
            assert_eq!(gate.model_index(), before);
            ld_strip_set_model(ch, 2, at("DLVTUBE"));
            assert_ne!((*ch).params.dynamics().model_index() as i32, at("DLVTUBE"));
            ld_strip_set_model(ch, 2, at("DLV16T"));
            assert_eq!((*ch).params.dynamics().model_index() as i32, at("DLV16T"));
            // A dLive's other dynamics blocks don't either.
            let extra = ld_engine_insert_block(e, ch, BlockKind::Dynamics.index() as i32, -1);
            assert!(!ld_strip_set_block_model(ch, extra, at("DLVVALVE")));
            assert!(ld_strip_set_block_model(ch, extra, at("DLV16T")));
            assert_eq!(ld_strip_preamp_block(ch), uid, "still the same block");

            // Other strips and consoles: none.
            assert_eq!(ld_strip_preamp_block(ld_engine_strip_at(e, StripKind::Bus as i32, 0)), -1);
            ld_engine_configure_profile(e, 1); // the WING
            assert_eq!(ld_strip_preamp_block(ld_engine_strip_at(e, StripKind::Channel as i32, 0)), -1);
            ld_engine_collect(e);
            ld_engine_destroy(e);
        }
    }
}
