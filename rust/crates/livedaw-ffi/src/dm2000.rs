//! The DM2000 for the host (`livedaw-dm2000`): its MIDI protocol over the
//! engine. The app's `Dm2000Midi` holds one. An `LdDm2000` borrows the engine
//! it was made with: the host destroys it before the engine. Every call does
//! nothing (or answers none) while the engine isn't a DM2000. Its Automix is an
//! `LdDm2000Automix` of its own.

use super::guard;
use livedaw_dm2000::automix::{Automix, Key};
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

/// The DM2000's Automix (`livedaw_dm2000::automix`): the takes of the Input
/// Channels' faders and ONs, for the app's AUTOMIX page and keys. It holds no
/// engine: the host passes the faders and ONs to each tick.
pub struct LdDm2000Automix(Automix);

#[no_mangle]
pub extern "C" fn ld_dm2000_automix_create() -> *mut LdDm2000Automix {
    guard(std::ptr::null_mut(), || Box::into_raw(Box::new(LdDm2000Automix(Automix::default()))))
}

/// # Safety
/// `p` is from `ld_dm2000_automix_create` (or null), and isn't used after.
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_automix_destroy(p: *mut LdDm2000Automix) {
    if !p.is_null() {
        drop(Box::from_raw(p));
    }
}

/// Key `key` (`automix::Key`'s number) pressed at `now` (seconds, a steady clock).
///
/// # Safety
/// `p` is from `ld_dm2000_automix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_automix_press(p: *mut LdDm2000Automix, key: i32, now: f64) {
    if let (Some(p), Some(key)) = (p.as_mut(), Key::from_i32(key)) {
        guard((), || p.0.press(key, now));
    }
}

/// Whether key `key` is lit.
///
/// # Safety
/// `p` is from `ld_dm2000_automix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_automix_is(p: *const LdDm2000Automix, key: i32) -> bool {
    match (p.as_ref(), Key::from_i32(key)) {
        (Some(p), Some(key)) => p.0.is(key),
        _ => false,
    }
}

/// Input Channel `channel`'s (0-95) fader knob held or let go.
///
/// # Safety
/// `p` is from `ld_dm2000_automix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_automix_hold(p: *mut LdDm2000Automix, channel: i32, held: bool) {
    if let (Some(p), Ok(channel)) = (p.as_mut(), usize::try_from(channel)) {
        p.0.hold(channel, held);
    }
}

/// Runs the clock to `now`: `faders` (dB) and `ons` (0 off) are the first `n`
/// Input Channels'; what playback sets is written back. True when it set any.
///
/// # Safety
/// `p` is from `ld_dm2000_automix_create` (or null); `faders` and `ons` hold `n` values.
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_automix_tick(
    p: *mut LdDm2000Automix,
    now: f64,
    faders: *mut f32,
    ons: *mut u8,
    n: usize,
) -> bool {
    let Some(p) = p.as_mut() else { return false };
    if faders.is_null() || ons.is_null() || n == 0 {
        return false;
    }
    let faders = std::slice::from_raw_parts_mut(faders, n);
    let ons = std::slice::from_raw_parts_mut(ons, n);
    guard(false, || {
        let mut on: Vec<bool> = ons.iter().map(|&o| o != 0).collect();
        let wrote = p.0.tick(now, faders, &mut on);
        for (o, v) in ons.iter_mut().zip(on) {
            *o = v as u8;
        }
        wrote
    })
}

/// A scene was recalled at `now`: with SCENE CLOCK the clock starts again.
///
/// # Safety
/// `p` is from `ld_dm2000_automix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_automix_scene_recalled(p: *mut LdDm2000Automix, now: f64) {
    if let Some(p) = p.as_mut() {
        p.0.scene_recalled(now);
    }
}

/// How many moves the Automix holds.
///
/// # Safety
/// `p` is from `ld_dm2000_automix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_automix_events(p: *const LdDm2000Automix) -> i32 {
    p.as_ref().map_or(0, |p| p.0.moves().len().min(i32::MAX as usize) as i32)
}

/// The clock, in seconds since the run started.
///
/// # Safety
/// `p` is from `ld_dm2000_automix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_automix_time(p: *const LdDm2000Automix) -> f64 {
    p.as_ref().map_or(0.0, |p| p.0.time())
}

/// RETURN's TIME, 0.0 to 30.0 seconds.
///
/// # Safety
/// `p` is from `ld_dm2000_automix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_automix_return_time(p: *const LdDm2000Automix) -> f32 {
    p.as_ref().map_or(0.0, |p| p.0.return_time())
}

/// # Safety
/// `p` is from `ld_dm2000_automix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_dm2000_automix_set_return_time(p: *mut LdDm2000Automix, seconds: f32) {
    if let Some(p) = p.as_mut() {
        p.0.set_return_time(seconds);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_automix_takes_and_plays_through_the_c_calls() {
        unsafe {
            let p = ld_dm2000_automix_create();
            let mut faders = [0.0f32; 2];
            let mut ons = [1u8; 2];
            let tick = |p, t, f: &mut [f32; 2], o: &mut [u8; 2]| ld_dm2000_automix_tick(p, t, f.as_mut_ptr(), o.as_mut_ptr(), 2);
            ld_dm2000_automix_press(p, Key::Rec as i32, 0.0);
            assert!(!tick(p, 0.0, &mut faders, &mut ons));
            assert!(ld_dm2000_automix_is(p, Key::Rec as i32) && ld_dm2000_automix_is(p, Key::Run as i32));
            faders[1] = -6.0;
            ons[0] = 0;
            tick(p, 1.0, &mut faders, &mut ons);
            assert_eq!(ld_dm2000_automix_events(p), 2);
            ld_dm2000_automix_press(p, Key::Run as i32, 2.0);
            faders = [0.0; 2];
            ons = [1; 2];
            ld_dm2000_automix_press(p, Key::Run as i32, 10.0);
            tick(p, 10.0, &mut faders, &mut ons);
            assert!(tick(p, 11.5, &mut faders, &mut ons));
            assert_eq!((faders, ons), ([0.0, -6.0], [0, 1]));
            assert!((ld_dm2000_automix_time(p) - 1.5).abs() < 1e-9);
            ld_dm2000_automix_set_return_time(p, 4.44);
            assert_eq!(ld_dm2000_automix_return_time(p), 4.4);
            // Unknown keys and null handles do nothing.
            ld_dm2000_automix_press(p, 99, 12.0);
            assert!(!ld_dm2000_automix_is(p, 99));
            ld_dm2000_automix_press(std::ptr::null_mut(), 0, 0.0);
            assert_eq!(ld_dm2000_automix_events(std::ptr::null()), 0);
            ld_dm2000_automix_destroy(p);
        }
    }

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
