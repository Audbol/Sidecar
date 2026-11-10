//! Console-level C ABI: sources (ROUTING > SOURCES), the strips' talk tags
//! and MUTE state (MUTEGROUP/SIP OVERRIDE), MON DCAs. Solo modes, talkback
//! and the monitor paths are fields of `EngineControls` (shared memory).

use crate::session::{str_arg, LdString};
use crate::{engine, guard, strip};
use livedaw_engine::sources::{self, OscParams, SourceParams, UserPatchParams, UserSignalParams};
use livedaw_engine::{Engine, Strip};
use std::sync::atomic::{AtomicI32, Ordering::Relaxed};

fn source(e: &Engine, index: i32) -> Option<&SourceParams> {
    e.sources().get(usize::try_from(index).ok()?)
}

/// The source table (`*count` entries): hardware ports, then playback channels.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_sources(e: *const Engine, count: *mut i32) -> *const SourceParams {
    let Some(e) = engine(e) else { return std::ptr::null() };
    if let Some(c) = count.as_mut() {
        *c = e.sources().params().len() as i32;
    }
    e.sources().params().as_ptr()
}

/// The source table index of an input port (hardware input or playback
/// channel), -1 if the port has no source.
#[no_mangle]
pub extern "C" fn ld_source_index(port: i32) -> i32 {
    sources::source_index(port).map_or(-1, |i| i as i32)
}

/// Source `index`'s GAIN range and step in dB (its WING group's: LOCAL IN
/// -2.5 .. +45 in 2.5 dB steps, AES50 -3 .. +45.5 in 0.5 dB steps); false
/// (all 0) for a source without a GAIN on the WING.
#[no_mangle]
pub unsafe extern "C" fn ld_source_gain_range(index: i32, min: *mut f32, max: *mut f32, step: *mut f32) -> bool {
    let r = usize::try_from(index).map_or(sources::GainRange::NONE, sources::source_gain_range);
    for (out, v) in [(min, r.min), (max, r.max), (step, r.step)] {
        if let Some(o) = out.as_mut() {
            *o = v;
        }
    }
    r.has_gain()
}

/// The input port of a source table index, -1 if out of range.
#[no_mangle]
pub extern "C" fn ld_source_port(index: i32) -> i32 {
    usize::try_from(index).ok().and_then(sources::source_port).unwrap_or(-1)
}

/// Message thread: a source's name (empty = the port's name).
#[no_mangle]
pub unsafe extern "C" fn ld_source_name(e: *const Engine, index: i32) -> *mut LdString {
    LdString::new(engine(e).map(|e| e.sources().name(index.max(0) as usize)).unwrap_or_default())
}

#[no_mangle]
pub unsafe extern "C" fn ld_source_set_name(e: *const Engine, index: i32, utf8: *const u8, len: usize) {
    if let Some(e) = engine(e) {
        let name = str_arg(utf8, len);
        guard((), || e.sources().set_name(index.max(0) as usize, &name));
    }
}

/// MONO / STEREO / MID-SIDE for a source and its pair partner.
#[no_mangle]
pub unsafe extern "C" fn ld_source_set_mode(e: *const Engine, index: i32, mode: i32) {
    if let Some(e) = engine(e) {
        if source(e, index).is_some() {
            guard((), || e.set_source_mode(index as usize, mode));
        }
    }
}

/// INITIALIZE: a source's settings and name back to the defaults (its pair's
/// too, if paired; the pairing stays).
#[no_mangle]
pub unsafe extern "C" fn ld_source_initialize(e: *const Engine, index: i32) {
    if let Some(e) = engine(e) {
        if source(e, index).is_some() {
            guard((), || e.sources().initialize(index as usize));
        }
    }
}

/// The oscillators' settings (`*count` entries: OSC 1, OSC 2).
#[no_mangle]
pub unsafe extern "C" fn ld_engine_oscillators(e: *const Engine, count: *mut i32) -> *const OscParams {
    let Some(e) = engine(e) else { return std::ptr::null() };
    if let Some(c) = count.as_mut() {
        *c = e.sources().oscillators().len() as i32;
    }
    e.sources().oscillators().as_ptr()
}

/// A console's own oscillator (`Engine::oscillator`), set from its surface: what it
/// plays (`mode`: OscMode; `burst`: 200 ms every 4 s; `exclusive`: what it's assigned
/// to carries nothing else).
#[no_mangle]
pub unsafe extern "C" fn ld_engine_console_osc_set(
    e: *const Engine,
    on: bool,
    mode: i32,
    freq_hz: f32,
    level_db: f32,
    burst: bool,
    exclusive: bool,
) {
    let Some(e) = engine(e) else { return };
    let o = &e.oscillator;
    o.burst.store(burst, std::sync::atomic::Ordering::Relaxed);
    o.exclusive.store(exclusive, std::sync::atomic::Ordering::Relaxed);
    o.set(on, mode, freq_hz, level_db);
}

/// Where the console oscillator plays: bus, main or matrix `number` (0-based) at
/// `left` and `right` (0 0: not at all).
#[no_mangle]
pub unsafe extern "C" fn ld_engine_console_osc_assign(e: *const Engine, kind: i32, number: i32, left: f32, right: f32) {
    let (Some(e), Some(kind), Ok(number)) = (engine(e), livedaw_engine::StripKind::from_i32(kind), usize::try_from(number))
    else {
        return;
    };
    e.oscillator.assign(kind, number, [left, right]);
}

/// HOME > TAGS talk tags (bits: TALK SOLO, TALK ON, TALK A SOLO, TALK A ON,
/// TALK B SOLO, TALK B ON).
#[no_mangle]
pub unsafe extern "C" fn ld_strip_talk_tags(s: *const Strip) -> u32 {
    strip(s).map_or(0, |s| s.console.talk_tags.load(Relaxed))
}

#[no_mangle]
pub unsafe extern "C" fn ld_strip_set_talk_tags(s: *const Strip, tags: u32) {
    if let Some(s) = strip(s) {
        s.console.talk_tags.store(tags & livedaw_engine::talk::talk_tag::ALL, Relaxed);
    }
}

/// The MUTE state of the last block: 0 off, 1 muted, 2 muted by a mute
/// group, DCA or SIP (the key flashes), 3 such a mute overridden.
#[no_mangle]
pub unsafe extern "C" fn ld_strip_mute_state(s: *const Strip) -> i32 {
    strip(s).map_or(0, |s| s.console.mute_state())
}

/// Soloed in the last block (own solo, a soloed DCA or TALK SOLO).
/// ABI 56: a mix's Ext In and Mix Source.
///
/// # Safety
/// `s` is a valid strip; the result lives as long as it does.
#[no_mangle]
pub unsafe extern "C" fn ld_strip_mix_input(s: *const Strip) -> *const livedaw_engine::params::MixInputParams {
    &(*s).mix_input
}

/// A strip's dLive settings (`livedaw_engine::dlive`; ignored on other consoles).
///
/// # Safety
/// `s` is a valid strip; the result lives as long as it does.
#[no_mangle]
pub unsafe extern "C" fn ld_strip_dlive(s: *const Strip) -> *const livedaw_engine::dlive::DLiveStripParams {
    &(*s).dlive
}

/// A dLive's Direct Out settings: source (`dlive::direct_source`), Follow Fader, Follow
/// Mute, Follow All. Each getter and setter works on one value of the four.
///
/// # Safety
/// `e` is a valid engine.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_dlive_direct_source(e: *const Engine) -> i32 {
    (*e).dlive.direct_source.load(Relaxed)
}

/// # Safety
/// `e` is a valid engine.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_dlive_set_direct_source(e: *const Engine, source: i32) {
    (*e).dlive.direct_source.store(source.clamp(0, 7), Relaxed);
}

/// Bits: 1 Follow Fader, 2 Follow Mute, 4 Follow All.
///
/// # Safety
/// `e` is a valid engine.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_dlive_direct_follow(e: *const Engine) -> i32 {
    let c = &(*e).dlive;
    i32::from(c.direct_follow_fader.load(Relaxed))
        | i32::from(c.direct_follow_mute.load(Relaxed)) << 1
        | i32::from(c.direct_follow_all.load(Relaxed)) << 2
}

/// # Safety
/// `e` is a valid engine.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_dlive_set_direct_follow(e: *const Engine, bits: i32) {
    let c = &(*e).dlive;
    c.direct_follow_fader.store(bits & 1 != 0, Relaxed);
    c.direct_follow_mute.store(bits & 2 != 0, Relaxed);
    c.direct_follow_all.store(bits & 4 != 0, Relaxed);
}

/// A stereo mix's Follow Main Pan and Invert (`Engine::set_follow_main_pan`).
///
/// # Safety
/// `e` and `s` are valid.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_set_follow_main_pan(e: *const Engine, s: *const Strip, follow: bool, invert: bool) {
    (*e).set_follow_main_pan(&*s, follow, invert);
}

/// The channel's ABCD inputs on or off, or the one it plays (0-3).
///
/// # Safety
/// `e` and `s` are valid.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_abcd_enable(e: *const Engine, s: *const Strip, on: bool) {
    (*e).abcd_enable(&*s, on);
}

/// # Safety
/// `e` and `s` are valid.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_abcd_select(e: *const Engine, s: *const Strip, which: i32) {
    if let Ok(which) = usize::try_from(which) {
        (*e).abcd_select(&*s, which);
    }
}

/// The Actions: `dca` < 0 sets all channels, else that DCA's members.
///
/// # Safety
/// `e` is a valid engine.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_abcd_set(e: *const Engine, dca: i32, which: i32) {
    let Ok(which) = usize::try_from(which) else { return };
    match usize::try_from(dca) {
        Ok(dca) => (*e).abcd_set_for_dca(dca, which),
        Err(_) => (*e).abcd_set_all(which),
    }
}

/// # Safety
/// `e` is a valid engine.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_abcd_set_range(e: *const Engine, first: i32, last: i32, which: i32) {
    if let (Ok(first), Ok(last), Ok(which)) = (usize::try_from(first), usize::try_from(last), usize::try_from(which)) {
        (*e).abcd_set_range(first, last, which);
    }
}

/// A Source Selector's output: `kind` as `StripKind` (a Bus or Matrix), its slot or
/// index. kind < 0: unassigned.
///
/// # Safety
/// `e` is a valid engine.
#[no_mangle]
pub unsafe extern "C" fn ld_selector_set_output(e: *const Engine, selector: i32, kind: i32, index: i32) {
    let Ok(selector) = usize::try_from(selector) else { return };
    let output = livedaw_engine::StripKind::from_i32(kind).zip(usize::try_from(index).ok());
    (*e).set_selector_output(selector, output);
}

/// A selector's source `member`: `kind` as `StripKind`, `number` from 1. kind < 0: empty.
///
/// # Safety
/// `e` is a valid engine.
#[no_mangle]
pub unsafe extern "C" fn ld_selector_set_source(e: *const Engine, selector: i32, member: i32, kind: i32, number: i32) {
    if let (Ok(selector), Ok(member)) = (usize::try_from(selector), usize::try_from(member)) {
        (*e).set_selector_source(selector, member, livedaw_engine::StripKind::from_i32(kind).map(|k| (k, number)));
    }
}

/// A source's on-level, dB.
///
/// # Safety
/// `e` is a valid engine.
#[no_mangle]
pub unsafe extern "C" fn ld_selector_set_on_level(e: *const Engine, selector: i32, member: i32, db: f32) {
    if let Some(m) = selector_source(e, selector, member) {
        m.on_db.store(db);
    }
}

/// The source playing (-1: Off).
///
/// # Safety
/// `e` is a valid engine.
#[no_mangle]
pub unsafe extern "C" fn ld_selector_select(e: *const Engine, selector: i32, member: i32) {
    if let Some(sel) = usize::try_from(selector).ok().and_then(|i| (*e).dlive.selectors.get(i)) {
        sel.active.store(member.max(-1), Relaxed);
    }
}

/// # Safety
/// `e` is a valid engine.
#[no_mangle]
pub unsafe extern "C" fn ld_selector_active(e: *const Engine, selector: i32) -> i32 {
    usize::try_from(selector).ok().and_then(|i| (*e).dlive.selectors.get(i)).map_or(-1, |s| s.active.load(Relaxed))
}

/// The fades, seconds (up to 10).
///
/// # Safety
/// `e` is a valid engine.
#[no_mangle]
pub unsafe extern "C" fn ld_selector_set_fades(e: *const Engine, selector: i32, fade_in: f32, fade_out: f32) {
    use livedaw_engine::dlive::MAX_SELECTOR_FADE_S as MAX;
    if let Some(sel) = usize::try_from(selector).ok().and_then(|i| (*e).dlive.selectors.get(i)) {
        sel.fade_in_s.store(fade_in.clamp(0.0, MAX));
        sel.fade_out_s.store(fade_out.clamp(0.0, MAX));
    }
}

/// A selector's output as `ld_selector_set_output` takes it. False: unassigned, and
/// `kind` and `index` are left alone.
///
/// # Safety
/// `e` is a valid engine; `kind` and `index` are null or writable.
#[no_mangle]
pub unsafe extern "C" fn ld_selector_output(e: *const Engine, selector: i32, kind: *mut i32, index: *mut i32) -> bool {
    let Some(sel) = usize::try_from(selector).ok().and_then(|i| (*e).dlive.selectors.get(i)) else { return false };
    let k = sel.output_kind.load(Relaxed);
    if k < 0 {
        return false;
    }
    if let Some(kind) = kind.as_mut() {
        *kind = k;
    }
    if let Some(index) = index.as_mut() {
        *index = sel.output.load(Relaxed);
    }
    true
}

/// A selector's source `member` as `ld_selector_set_source` takes it, and its on-level.
/// False: empty, and the three are left alone.
///
/// # Safety
/// `e` is a valid engine; `kind`, `number` and `on_db` are null or writable.
#[no_mangle]
pub unsafe extern "C" fn ld_selector_source(
    e: *const Engine,
    selector: i32,
    member: i32,
    kind: *mut i32,
    number: *mut i32,
    on_db: *mut f32,
) -> bool {
    let Some(m) = selector_source(e, selector, member) else { return false };
    let k = m.kind.load(Relaxed);
    if k < 0 {
        return false;
    }
    if let Some(kind) = kind.as_mut() {
        *kind = k;
    }
    if let Some(number) = number.as_mut() {
        *number = m.number.load(Relaxed);
    }
    if let Some(on_db) = on_db.as_mut() {
        *on_db = m.on_db.load();
    }
    true
}

/// A selector's fades, seconds. False: no such selector.
///
/// # Safety
/// `e` is a valid engine; `fade_in` and `fade_out` are null or writable.
#[no_mangle]
pub unsafe extern "C" fn ld_selector_fades(e: *const Engine, selector: i32, fade_in: *mut f32, fade_out: *mut f32) -> bool {
    let Some(sel) = usize::try_from(selector).ok().and_then(|i| (*e).dlive.selectors.get(i)) else { return false };
    if let Some(fade_in) = fade_in.as_mut() {
        *fade_in = sel.fade_in_s.load();
    }
    if let Some(fade_out) = fade_out.as_mut() {
        *fade_out = sel.fade_out_s.load();
    }
    true
}

unsafe fn selector_source<'a>(e: *const Engine, selector: i32, member: i32) -> Option<&'a livedaw_engine::dlive::SelectorSource> {
    (*e).dlive.selectors.get(usize::try_from(selector).ok()?)?.sources.get(usize::try_from(member).ok()?)
}

/// ABI 56: a dLive channel's AMM settings and read-back (the settings are ignored on
/// any other strip).
///
/// # Safety
/// `s` is a valid strip; the result lives as long as it does.
#[no_mangle]
pub unsafe extern "C" fn ld_strip_amm(s: *const Strip) -> *const livedaw_engine::amm::AmmStripParams {
    strip(s).map_or(std::ptr::null(), |s| &s.amm)
}

/// ABI 56: the console's AMM settings; lives as long as the engine.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_amm(e: *const Engine) -> *const livedaw_engine::amm::AmmParams {
    engine(e).map_or(std::ptr::null(), |e| &e.amm)
}

#[no_mangle]
pub unsafe extern "C" fn ld_strip_is_soloed(s: *const Strip) -> bool {
    strip(s).is_some_and(|s| s.console.is_soloed())
}

/// Message thread: the MUTE key (MUTEGROUP/SIP OVERRIDE aware).
#[no_mangle]
pub unsafe extern "C" fn ld_engine_press_mute(e: *const Engine, s: *const Strip) {
    if let (Some(e), Some(s)) = (engine(e), strip(s)) {
        guard((), || e.press_mute(s));
    }
}

/// Message thread: the global MAIN / ALT switch (WING `/io/altsw`): switches
/// the input and aux channels in GLOBAL INPUT SELECT ENABLE (ABI 22).
#[no_mangle]
pub unsafe extern "C" fn ld_engine_set_global_alt(e: *const Engine, alt: bool) {
    if let Some(e) = engine(e) {
        guard((), || e.set_global_alt(alt));
    }
}

/// The monitor paths (bit 0 = SPEAKERS, bit 1 = PHONES) whose level DCA `dca`
/// controls (named MON A: PHONES, MON B: SPEAKERS, MON LVL: both).
#[no_mangle]
pub unsafe extern "C" fn ld_engine_dca_monitor_paths(e: *const Engine, dca: i32) -> u32 {
    engine(e).map_or(0, |e| e.dca_monitor_paths(dca.max(0) as usize))
}

// ---- routing (ABI 19; livedaw-engine routing.rs, sources.rs) ----------------------------

/// USR 1-24, the user signals: what each copies (`*count` entries; shared memory).
#[no_mangle]
pub unsafe extern "C" fn ld_engine_user_signals(e: *const Engine, count: *mut i32) -> *const UserSignalParams {
    let Some(e) = engine(e) else { return std::ptr::null() };
    if let Some(c) = count.as_mut() {
        *c = e.sources().user_signals().len() as i32;
    }
    e.sources().user_signals().as_ptr()
}

/// USR 25-56, the user patches: the external source each copies (`*count` entries).
#[no_mangle]
pub unsafe extern "C" fn ld_engine_user_patches(e: *const Engine, count: *mut i32) -> *const UserPatchParams {
    let Some(e) = engine(e) else { return std::ptr::null() };
    if let Some(c) = count.as_mut() {
        *c = e.sources().user_patches().len() as i32;
    }
    e.sources().user_patches().as_ptr()
}

/// Patches user patch `n` (0-based) to an external source (an input index);
/// anything else is OFF.
#[no_mangle]
pub unsafe extern "C" fn ld_source_set_user_patch(e: *const Engine, n: i32, source: i32) {
    if let (Some(e), Ok(n)) = (engine(e), usize::try_from(n)) {
        e.sources().set_user_patch(n, source);
    }
}

/// ROUTING > OUTPUTS for sources: the source on each hardware output (-1
/// none; `*count` entries, shared memory). The strips' own outputs are
/// their `output_left` / `output_right`.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_output_patch(e: *const Engine, count: *mut i32) -> *const AtomicI32 {
    let Some(e) = engine(e) else { return std::ptr::null() };
    if let Some(c) = count.as_mut() {
        *c = e.output_patch().ports.len() as i32;
    }
    e.output_patch().ports.as_ptr()
}

/// Message thread: patches `source` (an input index; a bus, main or matrix
/// signal goes to that strip's output) to hardware output `port`, replacing
/// whatever fed it. False when the port can't be patched.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_route_output_source(e: *const Engine, port: i32, source: i32) -> bool {
    engine(e).is_some_and(|e| guard(false, || e.route_output_source(port, source)))
}

/// Message thread: nothing feeds hardware output `port` (strips, monitor
/// paths and sources routed there are cleared).
#[no_mangle]
pub unsafe extern "C" fn ld_engine_clear_output(e: *const Engine, port: i32) {
    if let Some(e) = engine(e) {
        guard((), || e.clear_output(port));
    }
}

/// The input index of a bus, main or matrix signal (kind as `StripKind`,
/// 0-based number (a bus's slot), side 0 L / 1 R), -1 for none.
#[no_mangle]
pub extern "C" fn ld_strip_source_input(kind: i32, number: i32, side: i32) -> i32 {
    match (livedaw_engine::StripKind::from_i32(kind), usize::try_from(number), usize::try_from(side)) {
        (Some(k), Ok(n), Ok(s)) => livedaw_engine::routing::strip_source_input(k, n, s),
        _ => -1,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{ld_engine_create, ld_engine_destroy, ld_engine_set_group_name, ld_engine_set_num_channels, ld_engine_strip_at};

    #[test]
    fn a_source_selector_reads_back_through_the_c_abi() {
        unsafe {
            let e = ld_engine_create(0);
            let (mut kind, mut index, mut number, mut db, mut fade_in, mut fade_out) = (-1, -1, -1, 1.0_f32, 0.0_f32, 0.0_f32);
            assert!(!ld_selector_output(e, 0, &mut kind, &mut index));
            assert_eq!((kind, index), (-1, -1), "left alone");
            assert!(!ld_selector_source(e, 0, 3, &mut kind, &mut number, &mut db));
            assert!(!ld_selector_source(e, 0, 20, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()));
            assert!(!ld_selector_output(e, 20, std::ptr::null_mut(), std::ptr::null_mut()));
            assert!(!ld_selector_fades(e, 20, &mut fade_in, &mut fade_out));

            ld_selector_set_output(e, 0, 4, 2);
            assert!(ld_selector_output(e, 0, &mut kind, &mut index));
            assert_eq!((kind, index), (4, 2));
            ld_selector_set_source(e, 0, 3, 0, 7);
            ld_selector_set_on_level(e, 0, 3, -6.0);
            assert!(ld_selector_source(e, 0, 3, &mut kind, &mut number, &mut db));
            assert_eq!((kind, number, db), (0, 7, -6.0));
            ld_selector_set_source(e, 0, 3, -1, 0);
            assert!(!ld_selector_source(e, 0, 3, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()));
            ld_selector_set_fades(e, 0, 2.5, 99.0);
            assert!(ld_selector_fades(e, 0, &mut fade_in, &mut fade_out));
            assert_eq!((fade_in, fade_out), (2.5, 10.0), "ten seconds at most");
            ld_engine_destroy(e);
        }
    }

    #[test]
    fn sources_tags_and_mutes_through_the_c_abi() {
        unsafe {
            let e = ld_engine_create(0);
            let mut count = 0;
            let table = ld_engine_sources(e, &mut count);
            assert_eq!(count as usize, sources::NUM_SOURCES);
            assert_eq!(ld_source_index(ld_source_port(600)), 600);
            assert_eq!(ld_source_index(-1), -1);
            ld_source_set_mode(e, 5, 1);
            assert_eq!((*table.add(4)).mode.load(Relaxed), 1);
            let name = "KICK";
            ld_source_set_name(e, 4, name.as_ptr(), name.len());
            let s = ld_source_name(e, 4);
            let text = std::slice::from_raw_parts(crate::session::ld_string_data(s), crate::session::ld_string_len(s));
            assert_eq!(text, b"KICK");
            crate::session::ld_string_free(s);
            ld_source_initialize(e, 4);
            assert!((*table.add(4)).is_default() || (*table.add(4)).mode.load(Relaxed) == 1);

            // The oscillators, then the USR group: USR 56 is the last source.
            assert_eq!(ld_source_index(sources::OSC_SOURCE_BASE + 1), sources::OSC_SOURCE_INDEX as i32 + 1);
            assert_eq!(ld_source_index(sources::USR_SOURCE_BASE + 55), count - 1);
            let mut n = 0;
            let usr = ld_engine_user_signals(e, &mut n);
            assert_eq!((n, std::mem::size_of::<UserSignalParams>()), (24, 16), "UserSignalParams in livedaw_engine.h");
            (*usr.add(3)).group.store(1, Relaxed);
            assert_eq!((*e).sources().user_signals()[3].strip(), Some((livedaw_engine::StripKind::Channel, 1)));
            let patches = ld_engine_user_patches(e, &mut n);
            assert_eq!(n, 32);
            ld_source_set_user_patch(e, 2, 7);
            assert_eq!((*patches.add(2)).source.load(Relaxed), 7);
            let outs = ld_engine_output_patch(e, &mut n);
            assert_eq!(n as usize, livedaw_engine::routing::NUM_PATCH_OUTPUTS);
            assert!(ld_engine_route_output_source(e, 4, 9));
            assert_eq!((*outs.add(4)).load(Relaxed), 9);
            ld_engine_clear_output(e, 4);
            assert_eq!((*outs.add(4)).load(Relaxed), -1);
            assert_eq!(ld_strip_source_input(2, 15, 1), livedaw_engine::routing::STRIP_SOURCE_BASE + 31);
            let osc = ld_engine_oscillators(e, &mut count);
            assert_eq!(count, 2);
            assert_eq!(std::mem::size_of::<OscParams>(), 12, "OscParams in livedaw_engine.h");
            assert_eq!((*osc.add(1)).freq_hz.load(), sources::OSC_DEFAULT_FREQ_HZ);

            ld_engine_set_num_channels(e, 1);
            let ch = ld_engine_strip_at(e, 0, 0);
            ld_strip_set_talk_tags(ch, 0xff);
            assert_eq!(ld_strip_talk_tags(ch), 0x3f);
            assert_eq!(ld_strip_mute_state(ch), 0);
            ld_engine_press_mute(e, ch);
            assert!((*ch).params.mute.load(Relaxed));

            let mon = "MON A";
            ld_engine_set_group_name(e, 0, 2, mon.as_ptr(), mon.len());
            assert_eq!(ld_engine_dca_monitor_paths(e, 2), 2);
            ld_engine_destroy(e);
        }
    }
}
