//! The DM2000 for the host (`livedaw-dm2000`): its MIDI protocol over the
//! engine. The app's `Dm2000Midi` holds one. An `LdDm2000` borrows the engine
//! it was made with: the host destroys it before the engine. Every call does
//! nothing (or answers none) while the engine isn't a DM2000.

use super::guard;
use livedaw_dm2000::{Event, Midi, Setup, SETUP_BYTES};
use livedaw_engine::Engine;

pub struct LdDm2000 {
    engine: *const Engine,
    midi: Midi,
}

/// # Safety
/// `e` is a live engine that outlives the handle (or null: returns null).
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_create(e: *const Engine) -> *mut LdDm2000 {
    if e.is_null() {
        return std::ptr::null_mut();
    }
    guard(std::ptr::null_mut(), || Box::into_raw(Box::new(LdDm2000 { engine: e, midi: Midi::default() })))
}

/// # Safety
/// `p` is from `ld_dm2000_create` (or null), and isn't used after.
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_destroy(p: *mut LdDm2000) {
    if !p.is_null() {
        drop(Box::from_raw(p));
    }
}

/// Handles one received MIDI message of `len` bytes.
///
/// # Safety
/// `p` is from `ld_dm2000_create` (or null); `bytes` holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_midi_in(p: *mut LdDm2000, bytes: *const u8, len: usize) {
    let Some(p) = p.as_mut() else { return };
    let Some(engine) = p.engine.as_ref() else { return };
    if bytes.is_null() || len == 0 {
        return;
    }
    let msg = std::slice::from_raw_parts(bytes, len);
    guard((), || p.midi.receive(engine, msg));
}

/// Queues the console's changes since the last call and the running meter data
/// (the first call sends nothing). The app calls it every 50 ms.
///
/// # Safety
/// `p` is from `ld_dm2000_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_midi_poll(p: *mut LdDm2000) {
    let Some(p) = p.as_mut() else { return };
    let Some(engine) = p.engine.as_ref() else { return };
    guard((), || p.midi.poll(engine));
}

/// Sends every value again at the next poll.
///
/// # Safety
/// `p` is from `ld_dm2000_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_midi_resend(p: *mut LdDm2000) {
    if let Some(p) = p.as_mut() {
        p.midi.resend_all();
    }
}

/// Scene `scene` (0-99) was recalled on the console: its program change goes out.
///
/// # Safety
/// `p` is from `ld_dm2000_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_scene_recalled(p: *mut LdDm2000, scene: i32) {
    if let (Some(p), Ok(scene)) = (p.as_mut(), u16::try_from(scene)) {
        p.midi.scene_recalled(scene);
    }
}

/// An MMC command goes out (`F0 7F id 06 command F7`).
///
/// # Safety
/// `p` is from `ld_dm2000_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_mmc(p: *mut LdDm2000, command: u8) {
    if let Some(p) = p.as_mut() {
        p.midi.mmc(command);
    }
}

/// The next outgoing message into `out`: its length, 0 for none, or minus the
/// size it needs if `max` is too small (a meter message can be a kilobyte).
///
/// # Safety
/// `p` is from `ld_dm2000_create` (or null); `out` holds `max` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_midi_out(p: *mut LdDm2000, out: *mut u8, max: usize) -> i32 {
    let Some(p) = p.as_mut() else { return 0 };
    if out.is_null() {
        return 0;
    }
    let Some(msg) = p.midi.take_out() else { return 0 };
    if msg.len() > max {
        let n = msg.len() as i32;
        p.midi.put_back(msg);
        return -n;
    }
    std::ptr::copy_nonoverlapping(msg.as_ptr(), out, msg.len());
    msg.len() as i32
}

/// The next thing the MIDI asks of the app: `*kind` 0 recall scene `*value`
/// (0-99), 1 store scene `*value` (1-99), 2 a Key Remote key (`*value`: its
/// address H, M and L as bytes 2, 1 and 0, bit 24 pressed). False for none.
///
/// # Safety
/// `p` is from `ld_dm2000_create` (or null); `kind` and `value` are valid.
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_event(p: *mut LdDm2000, kind: *mut i32, value: *mut i32) -> bool {
    let (Some(p), Some(kind), Some(value)) = (p.as_mut(), kind.as_mut(), value.as_mut()) else { return false };
    match p.midi.take_event() {
        Some(Event::RecallScene(n)) => {
            *kind = 0;
            *value = n as i32;
            true
        }
        Some(Event::StoreScene(n)) => {
            *kind = 1;
            *value = n as i32;
            true
        }
        Some(Event::Key { h, m, l, press }) => {
            *kind = 2;
            *value = (h as i32) << 16 | (m as i32) << 8 | l as i32 | (press as i32) << 24;
            true
        }
        None => false,
    }
}

/// MIDI SETUP as `SETUP_BYTES` (`Setup::to_bytes`): Rx and Tx channels 0-15; Tx
/// of the six kinds (program change, control change, parameter change, bulk, MTC,
/// others), then Rx, OMNI and ECHO the same; NRPN; FADER LOW.
///
/// # Safety
/// `p` is from `ld_dm2000_create` (or null); `out` holds `SETUP_BYTES` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_get_setup(p: *const LdDm2000, out: *mut u8) {
    let (Some(p), false) = (p.as_ref(), out.is_null()) else { return };
    std::ptr::copy_nonoverlapping(p.midi.setup.to_bytes().as_ptr(), out, SETUP_BYTES);
}

/// # Safety
/// `p` is from `ld_dm2000_create` (or null); `bytes` holds `SETUP_BYTES` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_set_setup(p: *mut LdDm2000, bytes: *const u8) {
    let (Some(p), false) = (p.as_mut(), bytes.is_null()) else { return };
    let b = std::slice::from_raw_parts(bytes, SETUP_BYTES);
    let mut a = [0u8; SETUP_BYTES];
    a.copy_from_slice(b);
    p.midi.setup = Setup::from_bytes(&a);
}

/// The program change table (`out`: 128 scene numbers, 100 no assign).
///
/// # Safety
/// `p` is from `ld_dm2000_create` (or null); `out` holds 128 bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_get_pc_table(p: *const LdDm2000, out: *mut u8) {
    let (Some(p), false) = (p.as_ref(), out.is_null()) else { return };
    std::ptr::copy_nonoverlapping(p.midi.pc_table.as_ptr(), out, 128);
}

/// # Safety
/// `p` is from `ld_dm2000_create` (or null); `bytes` holds 128 bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_set_pc_table(p: *mut LdDm2000, bytes: *const u8) {
    let (Some(p), false) = (p.as_mut(), bytes.is_null()) else { return };
    p.midi.pc_table.copy_from_slice(std::slice::from_raw_parts(bytes, 128));
}

/// The initial Control Change table (Owner's Manual pp.353-368): what Control Change
/// `cc` (0-119) of table `table` (0-15, a MIDI channel) is assigned to, as
/// `kind * 256 + target` of `livedaw_dm2000::tables::CcAssign` (kind 1 FADER H, 2 FADER L,
/// 3 ON, 4 PAN, 5 EQ ATT H, 6 EQ ATT L, 7 EQ ON; target 0-95 INPUT, 96-103 BUS, 104-115
/// AUX, 116-119 MATRIX, 120 STEREO). 0: no assignment, or out of range.
#[no_mangle]
pub extern "C" fn ld_dm2000_cc_initial(table: i32, cc: i32) -> i32 {
    let (Ok(t), Ok(n)) = (usize::try_from(table), usize::try_from(cc)) else { return 0 };
    livedaw_dm2000::tables::CC_INITIAL.get(t).and_then(|row| row.get(n)).map_or(0, |a| a.kind as i32 * 256 + a.target as i32)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_initial_control_change_table_reads_out() {
        // Table 1: Control Change 1 is INPUT 1's FADER H, 33 its FADER L, 0 nothing (p.353).
        assert_eq!(ld_dm2000_cc_initial(0, 1), 256);
        assert_eq!(ld_dm2000_cc_initial(0, 33), 2 * 256);
        assert_eq!(ld_dm2000_cc_initial(0, 25), 256 + 96);
        assert_eq!(ld_dm2000_cc_initial(0, 0), 0);
        assert_eq!(ld_dm2000_cc_initial(16, 1), 0);
        assert_eq!(ld_dm2000_cc_initial(0, 120), 0);
        assert_eq!(ld_dm2000_cc_initial(-1, -1), 0);
    }
}
