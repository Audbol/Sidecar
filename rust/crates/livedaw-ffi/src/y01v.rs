//! The 01V for the host (`livedaw-01v`): its MIDI protocol over the engine.
//! The app's `Yamaha01V` holds one for its MixerEngine; `Y01VMidi` drives its
//! MIDI, the 01V surface its MIDI pages.
//!
//! An `LdY01V` borrows the engine it was made with: the host destroys it before
//! the engine. Every call does nothing (or answers none) while the engine
//! isn't a 01V.

use super::guard;
use super::promix::{scene_engine, store_into_scene};
use super::session::{str_arg, LdShow, LdString};
use livedaw_01v::midi::{Arrangement, CcMap, CcTarget, Event, Midi, PcTable};
use livedaw_01v::params;
use livedaw_engine::{ConsoleConfig, Counts, Engine, Profile};

const CONFIG: ConsoleConfig = ConsoleConfig { profile: Profile::Yamaha01V, counts: Counts::Y01V };

pub struct LdY01V {
    engine: *const Engine,
    midi: Midi,
}

impl LdY01V {
    /// The console (None if the engine isn't a 01V) and the MIDI state.
    fn with<R>(&mut self, f: impl FnOnce(&Engine, &mut Midi) -> R) -> Option<R> {
        // SAFETY: the host keeps the engine alive while `self` is.
        let engine = unsafe { self.engine.as_ref()? };
        (engine.profile() == Profile::Yamaha01V).then(|| f(engine, &mut self.midi))
    }
}

/// A 01V over engine `e` (null if `e` is).
///
/// # Safety
/// `e` is an engine that outlives the result, or null.
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_create(e: *const Engine) -> *mut LdY01V {
    if e.is_null() {
        return std::ptr::null_mut();
    }
    guard(std::ptr::null_mut(), || Box::into_raw(Box::new(LdY01V { engine: e, midi: Midi::default() })))
}

/// # Safety
/// `p` is from `ld_y01v_create` (or null), not used after.
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_destroy(p: *mut LdY01V) {
    if !p.is_null() {
        drop(Box::from_raw(p));
    }
}

/// Handles one received MIDI message (`len` bytes: a channel message, or a
/// whole SysEx from F0 to F7).
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null); `bytes` holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_midi_in(p: *mut LdY01V, bytes: *const u8, len: usize) {
    let Some(p) = p.as_mut() else { return };
    if bytes.is_null() || len == 0 {
        return;
    }
    let msg = std::slice::from_raw_parts(bytes, len).to_vec();
    guard((), || {
        p.with(|e, m| m.receive(e, &msg));
    });
}

/// Queues the console's changes since the last call.
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_midi_poll(p: *mut LdY01V) {
    if let Some(p) = p.as_mut() {
        guard((), || {
            p.with(|e, m| m.poll(e));
        });
    }
}

/// A recall the MIDI asked for is done: its values aren't sent back.
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_take_values(p: *mut LdY01V) {
    if let Some(p) = p.as_mut() {
        guard((), || {
            p.with(|e, m| m.remember(e));
        });
    }
}

/// Scene `scene` (0-99) was recalled: its program change goes out.
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_scene_recalled(p: *mut LdY01V, scene: i32) {
    if let (Some(p), Ok(scene)) = (p.as_mut(), u8::try_from(scene)) {
        guard((), || {
            p.with(|e, m| m.scene_recalled(e, scene));
        });
    }
}

/// The next message to send into `out` (`max` bytes): its length, 0 for none,
/// or minus its length if `out` is too small (it stays queued, at the front).
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null); `out` holds `max` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_midi_out(p: *mut LdY01V, out: *mut u8, max: usize) -> i32 {
    let Some(p) = p.as_mut() else { return 0 };
    let Some(msg) = p.midi.take_out() else { return 0 };
    if out.is_null() || msg.len() > max {
        let len = msg.len() as i32;
        let mut rest = vec![msg];
        while let Some(m) = p.midi.take_out() {
            rest.push(m);
        }
        for m in rest {
            p.midi.push_out(m);
        }
        return -len;
    }
    std::ptr::copy_nonoverlapping(msg.as_ptr(), out, msg.len());
    msg.len() as i32
}

/// What the MIDI asks the app to do (`ld_y01v_event`).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct LdY01VEvent {
    /// 0 recall scene `value` (1-99); 1 store into scene `value`; 2 a bulk
    /// request of scene `value`; 3 a dump of scene `value` (`size` bytes in the
    /// buffer, the title in `title`); 4 set scene `value`'s title; 5 scene
    /// `value`'s title requested (127 the edit buffer's); 6 scene 00 is about to
    /// load (take the undo first; `ld_y01v_recall_initial` loads it); 7 effects
    /// program `value` into EFFECT `data` (0-1); 8 key remote: key `value`, bit `data`.
    pub kind: i32,
    pub value: i32,
    pub data: i32,
    pub size: i32,
    pub title: [u8; 17],
}

/// The next event into `out`, a scene dump's bytes into `bytes` (up to `max`);
/// false for none.
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null); `out` is writable; `bytes` holds `max` bytes or is null.
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_event(p: *mut LdY01V, out: *mut LdY01VEvent, bytes: *mut u8, max: usize) -> bool {
    let (Some(p), Some(out)) = (p.as_mut(), out.as_mut()) else { return false };
    let Some(e) = p.midi.take_event() else { return false };
    let ev = |kind: i32, value: i32, data: i32| LdY01VEvent { kind, value, data, ..Default::default() };
    let title_of = |title: &str, e: &mut LdY01VEvent| {
        for (d, b) in e.title.iter_mut().zip(title.bytes().take(16)) {
            *d = b;
        }
    };
    *out = match e {
        Event::RecallScene(s) => ev(0, s as i32, 0),
        Event::StoreScene(s) => ev(1, s as i32, 0),
        Event::SceneRequest(s) => ev(2, s as i32, 0),
        Event::SceneDump(s, title, data) => {
            let n = data.len().min(max);
            if !bytes.is_null() {
                std::ptr::copy_nonoverlapping(data.as_ptr(), bytes, n);
            }
            let mut e = LdY01VEvent { kind: 3, value: s as i32, size: n as i32, ..Default::default() };
            title_of(&title, &mut e);
            e
        }
        Event::SetTitle(s, title) => {
            let mut e = ev(4, s as i32, 0);
            title_of(&title, &mut e);
            e
        }
        Event::TitleRequest(s) => ev(5, s as i32, 0),
        Event::RecallInitial => ev(6, 0, 0),
        Event::RecallEffect(n, slot) => ev(7, n as i32, slot as i32),
        Event::Key(k, b) => ev(8, k as i32, b as i32),
    };
    true
}

/// Loads scene 00, the initial data (recalled on the app's behalf).
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_recall_initial(p: *mut LdY01V) {
    if let Some(p) = p.as_mut() {
        guard((), || {
            p.with(|e, m| m.recall_initial(e));
        });
    }
}

/// Scene memory `scene` (1-99, the show's scene `scene - 1`) as scene bytes
/// (1014) into `out`, without recalling it; how many were written (0: no such
/// scene, or it holds no mix).
///
/// # Safety
/// `p` is from `ld_y01v_create`, `s` a show (or null); `out` holds `max` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_show_scene_bytes(p: *mut LdY01V, s: *mut LdShow, scene: i32, out: *mut u8, max: usize) -> i32 {
    let (Some(p), Some(s)) = (p.as_mut(), s.as_mut()) else { return 0 };
    let Some(live) = p.engine.as_ref().filter(|e| e.profile() == Profile::Yamaha01V) else { return 0 };
    if !(1..=99).contains(&scene) {
        return 0;
    }
    let bytes =
        guard(Vec::new(), || scene_engine(live, &s.show, scene - 1, &CONFIG).map_or_else(Vec::new, |e| params::bytes(&e)));
    let n = bytes.len().min(max);
    if !out.is_null() {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), out, n);
    }
    n as i32
}

/// Stores scene bytes into scene memory `scene` (1-99) without recalling it
/// (`store_into_scene`: its plugins kept, missing scenes up to it added).
///
/// # Safety
/// `p` is from `ld_y01v_create`, `s` a show (or null); `data` holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_store_scene_bytes(
    p: *mut LdY01V,
    s: *mut LdShow,
    scene: i32,
    data: *const u8,
    len: usize,
) -> bool {
    let (Some(p), Some(s)) = (p.as_mut(), s.as_mut()) else { return false };
    let Some(live) = p.engine.as_ref().filter(|e| e.profile() == Profile::Yamaha01V) else { return false };
    if data.is_null() || !(1..=99).contains(&scene) {
        return false;
    }
    let data = std::slice::from_raw_parts(data, len).to_vec();
    guard(false, || store_into_scene(live, &mut s.show, scene - 1, &CONFIG, |e| params::load(e, &data)))
}

/// Queues a bulk dump of scene `scene` (1-99) titled `title`, holding `data`:
/// the answer to an `ld_y01v_event` request.
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null); `title` holds `title_len` bytes; `data` holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_send_scene_dump(
    p: *mut LdY01V,
    scene: i32,
    title: *const u8,
    title_len: usize,
    data: *const u8,
    len: usize,
) {
    let Some(p) = p.as_mut() else { return };
    if data.is_null() || !(1..=99).contains(&scene) {
        return;
    }
    let title = str_arg(title, title_len);
    p.midi.scene_dump(scene as u8, &title, std::slice::from_raw_parts(data, len));
}

/// Queues scene `number`'s title (the answer to a title request).
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null); `title` holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_send_title(p: *mut LdY01V, number: i32, title: *const u8, len: usize) {
    let (Some(p), Ok(number)) = (p.as_mut(), u8::try_from(number)) else { return };
    let title = str_arg(title, len);
    p.midi.title(number, &title);
}

/// MIDI SETUP's value `i` (`Setup::values`: Tx CH, Rx CH 0-15; program change
/// Tx, Rx, OMNI, ECHO; control change Tx, Rx, OMNI, ECHO; parameter change Tx,
/// Rx, ECHO; BULK; the interval in tens of ms); -1 for none.
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_setup_value(p: *const LdY01V, i: i32) -> i32 {
    let (Some(p), Ok(i)) = (p.as_ref(), usize::try_from(i)) else { return -1 };
    p.midi.setup.values().get(i).map_or(-1, |&v| v as i32)
}

/// # Safety
/// `p` is from `ld_y01v_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_set_setup_value(p: *mut LdY01V, i: i32, value: i32) {
    let (Some(p), Ok(i)) = (p.as_mut(), usize::try_from(i)) else { return };
    p.midi.setup.set_value(i, value.clamp(0, 127) as u8);
}

/// PROGRAM CHANGE (MIDI page 2): the scene program `pc` (0-127) recalls (-1 none).
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_pc_scene(p: *const LdY01V, pc: i32) -> i32 {
    let (Some(p), Ok(pc)) = (p.as_ref(), usize::try_from(pc)) else { return -1 };
    p.midi.pc_table.scenes.get(pc).copied().flatten().map_or(-1, i32::from)
}

/// Sets it (`scene` -1: none; else 0-99).
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_set_pc_scene(p: *mut LdY01V, pc: i32, scene: i32) {
    let (Some(p), Ok(pc)) = (p.as_mut(), usize::try_from(pc)) else { return };
    if let Some(s) = p.midi.pc_table.scenes.get_mut(pc) {
        *s = u8::try_from(scene).ok().filter(|&x| x <= 99);
    }
}

/// CONTROL CHANGE (MIDI page 3): controller `cc`'s target as `CcTarget::to_bytes`
/// (`b0 | b1 << 8 | b2 << 16`; 0 NO ASSIGN).
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_cc_target(p: *const LdY01V, cc: i32) -> i32 {
    let (Some(p), Ok(cc)) = (p.as_ref(), usize::try_from(cc)) else { return 0 };
    let b = CcTarget::to_bytes(p.midi.cc_map.map.get(cc).copied().flatten());
    i32::from(b[0]) | i32::from(b[1]) << 8 | i32::from(b[2]) << 16
}

/// Sets it (0: NO ASSIGN).
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_set_cc_target(p: *mut LdY01V, cc: i32, code: i32) {
    let (Some(p), Ok(cc)) = (p.as_mut(), usize::try_from(cc)) else { return };
    if let Some(m) = p.midi.cc_map.map.get_mut(cc) {
        *m = CcTarget::of_bytes([(code & 0x7f) as u8, ((code >> 8) & 0x7f) as u8, ((code >> 16) & 0x7f) as u8]);
    }
}

/// Column `column` (0-2) of controller `code`'s target, as MIDI page 3 prints it.
///
/// # Safety
/// The result is freed with `ld_string_free`.
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_cc_label(code: i32, column: i32) -> *mut LdString {
    let t = CcTarget::of_bytes([(code & 0x7f) as u8, ((code >> 8) & 0x7f) as u8, ((code >> 16) & 0x7f) as u8]);
    LdString::new(CcTarget::describe(t).into_iter().nth(column.clamp(0, 2) as usize).unwrap_or_default())
}

/// INITIALIZE: the program change table (`table` 0), or the control change
/// table in an arrangement (1 DEFAULT, 2 P.MIX 01, 3 03D).
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_init_table(p: *mut LdY01V, table: i32) {
    let Some(p) = p.as_mut() else { return };
    match table {
        0 => p.midi.pc_table = PcTable::default(),
        1 => p.midi.cc_map = CcMap::of(Arrangement::Default),
        2 => p.midi.cc_map = CcMap::of(Arrangement::ProMix),
        3 => p.midi.cc_map = CcMap::of(Arrangement::O3d),
        _ => {}
    }
}

/// MIDI page 4: TRANSMIT queues a dump (a scene's through `ld_y01v_event`,
/// the kind a bulk letter: 'M', 'S', 'P', 'C').
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_transmit(p: *mut LdY01V, kind: i32, number: i32) {
    let (Some(p), Ok(kind), Ok(number)) = (p.as_mut(), u8::try_from(kind), u8::try_from(number)) else { return };
    guard((), || {
        p.with(|e, m| m.transmit(e, kind, number));
    });
}

/// MIDI page 4's REQUEST.
///
/// # Safety
/// `p` is from `ld_y01v_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y01v_request(p: *mut LdY01V, kind: i32, number: i32) {
    if let (Some(p), Ok(kind), Ok(number)) = (p.as_mut(), u8::try_from(kind), u8::try_from(number)) {
        p.midi.send_request(kind, number);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::session::{ld_show_add_scene, ld_show_create, ld_show_destroy};

    #[test]
    fn the_handle_speaks_midi_for_a_01v_only() {
        let e = Engine::new(0);
        let p = unsafe { ld_y01v_create(&e) };
        unsafe {
            let msg = livedaw_01v::midi::param_change(0, params::common(2, 0), 100);
            // A WING: nothing happens, and no scene bytes.
            ld_y01v_midi_in(p, msg.as_ptr(), msg.len());
            e.configure(&CONFIG);
            ld_y01v_midi_poll(p);
            ld_y01v_midi_in(p, msg.as_ptr(), msg.len());
            assert_eq!(params::get(&e, params::common(2, 0)), 100);

            // The setup and the tables.
            assert_eq!(ld_y01v_setup_value(p, 2), 1, "program change Tx on as p.224 shows it");
            ld_y01v_set_setup_value(p, 1, 6);
            assert_eq!(ld_y01v_setup_value(p, 1), 6);
            assert_eq!(ld_y01v_pc_scene(p, 99), 0, "#100 scene 00");
            ld_y01v_set_pc_scene(p, 0, 42);
            assert_eq!(ld_y01v_pc_scene(p, 0), 42);
            ld_y01v_init_table(p, 0);
            assert_eq!(ld_y01v_pc_scene(p, 0), 1);
            let code = ld_y01v_cc_target(p, 3);
            assert_eq!(code, 1 | 2 << 8, "fader CH3");
            let label = ld_y01v_cc_label(code, 2);
            assert_eq!(label.as_ref().unwrap().0.as_str(), "CH3");
            ld_y01v_set_cc_target(p, 3, 0);
            assert_eq!(ld_y01v_cc_target(p, 3), 0);
            ld_y01v_init_table(p, 2);
            assert_eq!(ld_y01v_cc_target(p, 3), 1 | 3 << 8, "P.MIX 01's: fader CH4");

            // Program change, the app's event.
            ld_y01v_set_setup_value(p, 1, 0);
            ld_y01v_midi_in(p, [0xc0, 9].as_ptr(), 2);
            let mut ev = LdY01VEvent::default();
            assert!(ld_y01v_event(p, &mut ev, std::ptr::null_mut(), 0));
            assert_eq!((ev.kind, ev.value), (0, 10));

            // A scene's bytes without recalling it; storing a dump into another.
            let s = ld_show_create(std::ptr::null());
            params::set(&e, params::common(0, 0), 77);
            assert_eq!(ld_show_add_scene(s, &e, b"A".as_ptr(), 1, -1), 0);
            params::set(&e, params::common(0, 0), 11);
            let mut read = vec![0u8; params::SIZE];
            assert_eq!(ld_y01v_show_scene_bytes(p, s, 1, read.as_mut_ptr(), read.len()), params::SIZE as i32);
            assert_eq!(read[params::common(0, 0)], 77);
            assert_eq!(params::get(&e, params::common(0, 0)), 11, "not recalled");
            assert_eq!(ld_y01v_show_scene_bytes(p, s, 2, read.as_mut_ptr(), read.len()), 0, "no such scene");
            read[params::common(1, 0)] = 66;
            assert!(ld_y01v_store_scene_bytes(p, s, 3, read.as_ptr(), read.len()));
            let mut back = vec![0u8; params::SIZE];
            assert_eq!(ld_y01v_show_scene_bytes(p, s, 3, back.as_mut_ptr(), back.len()), params::SIZE as i32);
            assert_eq!((back[params::common(0, 0)], back[params::common(1, 0)]), (77, 66));
            assert_eq!(
                params::get(&e, params::common(1, 0)),
                params::initial_bytes()[params::common(1, 0)],
                "the console is left alone"
            );

            // Scene 00.
            params::set(&e, params::common(0, 1), 0);
            ld_y01v_midi_in(p, [0xc0, 99].as_ptr(), 2);
            assert!(ld_y01v_event(p, &mut ev, std::ptr::null_mut(), 0));
            assert_eq!(ev.kind, 6);
            ld_y01v_recall_initial(p);
            assert_eq!(params::get(&e, params::common(0, 1)), 1);

            // A dump request of the edit buffer: too big for a small buffer, stays.
            let mut buf = [0u8; 16];
            let req = livedaw_01v::midi::request(0, b'M', 127);
            ld_y01v_midi_in(p, req.as_ptr(), req.len());
            assert_eq!(ld_y01v_midi_out(p, buf.as_mut_ptr(), 16), -2056);
            let mut big = vec![0u8; 2056];
            assert_eq!(ld_y01v_midi_out(p, big.as_mut_ptr(), big.len()), 2056);
            assert_eq!(ld_y01v_midi_out(p, buf.as_mut_ptr(), 16), 0);
            ld_y01v_transmit(p, b'S' as i32, 0);
            assert!(ld_y01v_midi_out(p, big.as_mut_ptr(), big.len()) > 0);

            ld_show_destroy(s);
            ld_y01v_destroy(p);
        }
    }
}
