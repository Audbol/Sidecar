//! The SQ-5 for the host (`livedaw-sq`, ABI 53): its MIDI protocol over the
//! engine. The app's `SqMidi` holds one. An `LdSq` borrows the engine it was
//! made with: the host destroys it before the engine. Every call does nothing
//! (or answers none) while the engine isn't an SQ-5.

use super::guard;
use livedaw_engine::Engine;
use livedaw_sq::{Event, Midi, Taper};

pub struct LdSq {
    engine: *const Engine,
    midi: Midi,
}

/// # Safety
/// `e` is a live engine that outlives the handle (or null: returns null).
#[no_mangle]
pub unsafe extern "C" fn ld_sq_create(e: *const Engine) -> *mut LdSq {
    if e.is_null() {
        return std::ptr::null_mut();
    }
    guard(std::ptr::null_mut(), || Box::into_raw(Box::new(LdSq { engine: e, midi: Midi::default() })))
}

/// # Safety
/// `p` is from `ld_sq_create` (or null), and isn't used after.
#[no_mangle]
pub unsafe extern "C" fn ld_sq_destroy(p: *mut LdSq) {
    if !p.is_null() {
        drop(Box::from_raw(p));
    }
}

/// Handles one received MIDI message of `len` bytes.
///
/// # Safety
/// `p` is from `ld_sq_create` (or null); `bytes` holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_sq_midi_in(p: *mut LdSq, bytes: *const u8, len: usize) {
    let Some(p) = p.as_mut() else { return };
    let Some(engine) = p.engine.as_ref() else { return };
    if bytes.is_null() || len == 0 {
        return;
    }
    let msg = std::slice::from_raw_parts(bytes, len);
    guard((), || p.midi.receive(engine, msg));
}

/// Queues the console's changes since the last call (the first sends nothing).
///
/// # Safety
/// `p` is from `ld_sq_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_sq_midi_poll(p: *mut LdSq) {
    let Some(p) = p.as_mut() else { return };
    let Some(engine) = p.engine.as_ref() else { return };
    guard((), || p.midi.poll(engine));
}

/// Sends every value again at the next poll.
///
/// # Safety
/// `p` is from `ld_sq_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_sq_midi_resend(p: *mut LdSq) {
    if let Some(p) = p.as_mut() {
        p.midi.resend_all();
    }
}

/// Scene `scene` (1-300) was recalled: its bank and program change go out.
///
/// # Safety
/// `p` is from `ld_sq_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_sq_scene_recalled(p: *mut LdSq, scene: i32) {
    if let (Some(p), Ok(scene)) = (p.as_mut(), u16::try_from(scene)) {
        p.midi.scene_recalled(scene);
    }
}

/// The next message to send into `out` (`max` bytes, 3 is always enough): its
/// length, 0 for none.
///
/// # Safety
/// `p` is from `ld_sq_create` (or null); `out` holds `max` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_sq_midi_out(p: *mut LdSq, out: *mut u8, max: usize) -> i32 {
    let Some(p) = p.as_mut() else { return 0 };
    if out.is_null() || max < 3 {
        return 0;
    }
    let Some(msg) = p.midi.take_out() else { return 0 };
    std::ptr::copy_nonoverlapping(msg.as_ptr(), out, msg.len());
    msg.len() as i32
}

/// The next scene the MIDI asks to recall (1-300), 0 for none.
///
/// # Safety
/// `p` is from `ld_sq_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_sq_scene_request(p: *mut LdSq) -> i32 {
    match p.as_mut().and_then(|p| p.midi.take_event()) {
        Some(Event::RecallScene(n)) => n as i32,
        None => 0,
    }
}

/// The MIDI channel (0-15) and the NRPN fader law (`audio`: Audio Taper).
///
/// # Safety
/// `p` is from `ld_sq_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_sq_set_setup(p: *mut LdSq, channel: i32, audio: bool) {
    if let Some(p) = p.as_mut() {
        p.midi.channel = channel.clamp(0, 15) as u8;
        p.midi.taper = if audio { Taper::Audio } else { Taper::Linear };
    }
}
