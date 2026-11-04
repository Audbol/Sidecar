//! The 02R for the host (`livedaw-02r`, ABI 46): its own state, its libraries
//! and its MIDI protocol, over the engine. The app's `Yamaha02R` holds one for
//! its MixerEngine; `Y02RMidi` drives its MIDI, the 02R surface its panel.
//!
//! The panel's values are its edit buffer's bytes (`livedaw_02r::params`,
//! Sidecar's addresses): `ld_y02r_get` / `ld_y02r_set` read and move any of
//! them as the console's controls do.
//!
//! An `LdY02R` borrows the engine it was made with: the host destroys it before
//! the engine. Every call does nothing (or answers none) while the engine
//! isn't an 02R.

use super::guard;
use super::session::{str_arg, LdString};
use livedaw_02r::library::{Kind, Library};
use livedaw_02r::midi::{Event, Midi, Setup};
use livedaw_02r::{params, Console, State, Y2Strip};
use livedaw_engine::{Engine, Profile};

pub struct LdY02R {
    engine: *const Engine,
    /// The console's own state: the engine's console text (`Engine::console_text`,
    /// which shows and scenes keep) as read at `version`.
    state: State,
    version: u64,
    library: Library,
    midi: Midi,
}

impl LdY02R {
    /// The console (None if the engine isn't an 02R), the library and the
    /// MIDI state; the state read again if a show or a scene set it since.
    fn with<R>(&mut self, f: impl FnOnce(&mut Console, &mut Library, &mut Midi) -> R) -> Option<R> {
        // SAFETY: the host keeps the engine alive while `self` is.
        let engine = unsafe { self.engine.as_ref()? };
        if engine.profile() != Profile::Yamaha02R {
            return None;
        }
        if engine.console_text_version() != self.version || self.version == 0 {
            let (text, version) = engine.console_text();
            if text.is_empty() {
                // A fresh console: its state from the start, so scenes keep it.
                self.state = State::default();
                self.version = engine.set_console_text(&self.state.to_text());
            } else {
                self.state = State::from_text(&text);
                self.version = version;
            }
        }
        let mut c = Console::new(engine, &mut self.state);
        Some(f(&mut c, &mut self.library, &mut self.midi))
    }

    /// As `with`, then the state written back to the engine.
    fn with_mut<R>(&mut self, f: impl FnOnce(&mut Console, &mut Library, &mut Midi) -> R) -> Option<R> {
        let r = self.with(f)?;
        // SAFETY: as `with`.
        if let Some(engine) = unsafe { self.engine.as_ref() } {
            self.version = engine.set_console_text(&self.state.to_text());
        }
        Some(r)
    }
}

/// An 02R over engine `e` (null if `e` is).
///
/// # Safety
/// `e` is an engine that outlives the result, or null.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_create(e: *const Engine) -> *mut LdY02R {
    if e.is_null() {
        return std::ptr::null_mut();
    }
    guard(std::ptr::null_mut(), || {
        Box::into_raw(Box::new(LdY02R {
            engine: e,
            state: State::default(),
            version: 0,
            library: Library::default(),
            midi: Midi::default(),
        }))
    })
}

/// # Safety
/// `p` is from `ld_y02r_create` (or null), not used after.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_destroy(p: *mut LdY02R) {
    if !p.is_null() {
        drop(Box::from_raw(p));
    }
}

/// Handles one received MIDI message (`len` bytes: a channel message, or a
/// whole SysEx from F0 to F7).
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null); `bytes` holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_midi_in(p: *mut LdY02R, bytes: *const u8, len: usize) {
    let Some(p) = p.as_mut() else { return };
    if bytes.is_null() || len == 0 {
        return;
    }
    let msg = std::slice::from_raw_parts(bytes, len).to_vec();
    guard((), || {
        p.with_mut(|c, lib, m| m.receive(c, lib, &msg));
    });
}

/// Queues the console's changes since the last call as parameter changes (the
/// first call takes the console's values and sends nothing).
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_midi_poll(p: *mut LdY02R) {
    if let Some(p) = p.as_mut() {
        guard((), || {
            p.with(|c, _, m| m.poll(c));
        });
    }
}

/// Scene `scene` (0-96) was recalled: its program change goes out.
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_scene_recalled(p: *mut LdY02R, scene: i32) {
    if let (Some(p), Ok(scene)) = (p.as_mut(), u8::try_from(scene)) {
        p.midi.scene_recalled(scene);
    }
}

/// The next message to send into `out` (`max` bytes): its length, 0 for none,
/// or minus its length if `out` is too small (it stays queued).
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null); `out` holds `max` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_midi_out(p: *mut LdY02R, out: *mut u8, max: usize) -> i32 {
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

/// ABI 46: what the MIDI asks the app to do (`ld_y02r_event`).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct LdY02REvent {
    /// 0 recall scene `value` (1-96), 1 store into scene `value`, 2 a request
    /// for scene `value`'s dump, 3 a dump of scene `value` (its bytes in the
    /// buffer, `size` of them; its title in `title`), 4 key remote `value`
    /// (`data` 1 on), 5 encoder `value` by `data` steps.
    pub kind: i32,
    pub value: i32,
    pub data: i32,
    pub size: i32,
    pub title: [u8; 17],
}

/// The next event into `out`, a scene dump's bytes into `bytes` (up to
/// `max`); false for none.
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null); `out` is writable; `bytes` holds `max` bytes or is null.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_event(p: *mut LdY02R, out: *mut LdY02REvent, bytes: *mut u8, max: usize) -> bool {
    let (Some(p), Some(out)) = (p.as_mut(), out.as_mut()) else { return false };
    let Some(e) = p.midi.take_event() else { return false };
    let ev = |kind: i32, value: i32, data: i32| LdY02REvent { kind, value, data, ..Default::default() };
    *out = match e {
        Event::RecallScene(s) => ev(0, s as i32, 0),
        Event::StoreScene(s) => ev(1, s as i32, 0),
        Event::SceneDumpRequest(s) => ev(2, s as i32, 0),
        Event::SceneDump(s, title, data) => {
            let n = data.len().min(max);
            if !bytes.is_null() {
                std::ptr::copy_nonoverlapping(data.as_ptr(), bytes, n);
            }
            let mut e = LdY02REvent { kind: 3, value: s as i32, data: 0, size: n as i32, title: [0; 17] };
            for (d, b) in e.title.iter_mut().zip(title.bytes().take(16)) {
                *d = b;
            }
            e
        }
        Event::Key(k, on) => ev(4, k as i32, on as i32),
        Event::Encoder(n, d) => ev(5, n as i32, d as i32),
    };
    true
}

/// The edit buffer as scene bytes (2016, `livedaw_02r::params`) into `out`;
/// how many were written (0 if the engine isn't an 02R).
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null); `out` holds `max` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_scene_bytes(p: *mut LdY02R, out: *mut u8, max: usize) -> i32 {
    let Some(p) = p.as_mut() else { return 0 };
    let bytes = guard(None, || p.with(|c, _, _| params::bytes(c))).unwrap_or_default();
    let n = bytes.len().min(max);
    if !out.is_null() {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), out, n);
    }
    n as i32
}

/// Sets the console from scene bytes.
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null); `data` holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_load_scene_bytes(p: *mut LdY02R, data: *const u8, len: usize) {
    let Some(p) = p.as_mut() else { return };
    if data.is_null() {
        return;
    }
    let data = std::slice::from_raw_parts(data, len).to_vec();
    guard((), || {
        p.with_mut(|c, _, _| params::load(c, &data));
    });
}

/// Queues a bulk dump of scene `scene` (1-96) titled `title`, holding `data`
/// (scene bytes): the answer to an `ld_y02r_event` request.
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null); `title` holds `title_len` bytes; `data` holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_send_scene_dump(
    p: *mut LdY02R,
    scene: i32,
    title: *const u8,
    title_len: usize,
    data: *const u8,
    len: usize,
) {
    let Some(p) = p.as_mut() else { return };
    if data.is_null() || !(1..=96).contains(&scene) {
        return;
    }
    let title = str_arg(title, title_len);
    let data = std::slice::from_raw_parts(data, len);
    p.midi.scene_dump(scene as u8, &title, data);
}

/// ABI 46: MIDI SETUP (`livedaw_02r::midi::Setup`): the channel 0-15; program
/// change RX, TX, OMNI, ECHO; parameter change RX, TX; control change RX (0 / 1).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct LdY02RSetup {
    pub channel: i32,
    pub switches: [u8; 7],
}

impl From<&Setup> for LdY02RSetup {
    fn from(s: &Setup) -> Self {
        LdY02RSetup {
            channel: s.channel as i32,
            switches: [s.pc_rx, s.pc_tx, s.pc_omni, s.pc_echo, s.param_rx, s.param_tx, s.cc_rx].map(u8::from),
        }
    }
}

impl From<&LdY02RSetup> for Setup {
    fn from(s: &LdY02RSetup) -> Self {
        let b = |i: usize| s.switches[i] != 0;
        Setup {
            channel: s.channel.clamp(0, 15) as u8,
            pc_rx: b(0),
            pc_tx: b(1),
            pc_omni: b(2),
            pc_echo: b(3),
            param_rx: b(4),
            param_tx: b(5),
            cc_rx: b(6),
        }
    }
}

/// # Safety
/// `p` is from `ld_y02r_create` (or null); `out` is writable or null.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_get_setup(p: *const LdY02R, out: *mut LdY02RSetup) {
    if let (Some(p), Some(out)) = (p.as_ref(), out.as_mut()) {
        *out = (&p.midi.setup).into();
    }
}

/// # Safety
/// `p` is from `ld_y02r_create` (or null); `setup` is readable or null.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_set_setup(p: *mut LdY02R, setup: *const LdY02RSetup) {
    if let (Some(p), Some(s)) = (p.as_mut(), setup.as_ref()) {
        p.midi.setup = s.into();
    }
}

/// The edit buffer's byte at address `a` (0-2015; -1: none, or not an 02R).
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_get(p: *mut LdY02R, a: i32) -> i32 {
    let (Some(p), Ok(a)) = (p.as_mut(), usize::try_from(a)) else { return -1 };
    if a >= params::SIZE {
        return -1;
    }
    guard(None, || p.with(|c, _, _| params::get(c, a))).map_or(-1, i32::from)
}

/// Sets the edit buffer's byte at address `a` as the console's control does;
/// `panel` true: as its panel (a fader group, a mute group, a gang follow).
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_set(p: *mut LdY02R, a: i32, value: i32, panel: bool) {
    let (Some(p), Ok(a)) = (p.as_mut(), usize::try_from(a)) else { return };
    if a >= params::SIZE {
        return;
    }
    let v = value.clamp(0, 255) as u8;
    guard((), || {
        p.with_mut(|c, _, _| {
            let item = a % params::PER_STRIP;
            match params::strip(a / params::PER_STRIP).filter(|_| panel && a < params::SENDS) {
                Some(s) if item == 0 => c.set_level(s, v as u16, livedaw_02r::Origin::Panel),
                Some(s) if item == 1 => c.set_on(s, v != 0, livedaw_02r::Origin::Panel),
                Some(s) if item == 2 && s.stereo_index().is_none() => c.set_pan(s, v as u16, livedaw_02r::Origin::Panel),
                _ => params::set(c, a, v),
            }
        });
    });
}

/// What address `a` is ("TAPE 1 EQ LOW G").
///
/// # Safety
/// The result is freed with `ld_string_free`.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_address_name(a: i32) -> *mut LdString {
    LdString::new(usize::try_from(a).map(params::name).unwrap_or_default())
}

/// Program `n` (0-127) of library `kind` (0 EQ, 1 dynamics, 2 effects): its
/// title ("" for an empty one).
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null); the result is freed with `ld_string_free`.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_program_name(p: *const LdY02R, kind: i32, n: i32) -> *mut LdString {
    let name = p.as_ref().and_then(|p| {
        let kind = [Kind::Eq, Kind::Dynamics, Kind::Effects].get(usize::try_from(kind).ok()?)?;
        Some(p.library.get(*kind, usize::try_from(n).ok()?)?.name.clone())
    });
    LdString::new(name.unwrap_or_default())
}

/// Recalls program `n` of library `kind` (0 EQ, 1 dynamics, 2 effects) onto
/// strip `strip` (the edit buffer's numbering, `livedaw_02r::params::strip`)
/// or, for the effects, Effect `strip` (0-1). False if nothing was recalled.
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_recall(p: *mut LdY02R, kind: i32, n: i32, strip: i32) -> bool {
    let (Some(p), Ok(n), Ok(strip)) = (p.as_mut(), usize::try_from(n), usize::try_from(strip)) else { return false };
    guard(None, || {
        p.with_mut(|c, lib, _| match kind {
            0 => params::strip(strip).is_some_and(|s| c.recall_eq(lib, s, n)),
            1 => params::strip(strip).is_some_and(|s| c.recall_dyn(lib, s, n)),
            2 => c.recall_fx(lib, strip, n),
            _ => false,
        })
    })
    .unwrap_or(false)
}

/// Recalls scene 0, the console's initial data.
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_recall_initial(p: *mut LdY02R) {
    if let Some(p) = p.as_mut() {
        guard((), || {
            p.with_mut(|c, _, m| m.recall_initial(c));
        });
    }
}

/// The console's own state as text (`livedaw_02r::State`).
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null); the result is freed with `ld_string_free`.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_state_text(p: *mut LdY02R) -> *mut LdString {
    let text = p.as_mut().and_then(|p| guard(None, || p.with(|c, _, _| c.state.to_text()))).unwrap_or_default();
    LdString::new(text)
}

/// Sets the console's own state from text.
///
/// # Safety
/// `p` is from `ld_y02r_create` (or null); `text` holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_y02r_set_state_text(p: *mut LdY02R, text: *const u8, len: usize) {
    let Some(p) = p.as_mut() else { return };
    let text = str_arg(text, len);
    guard((), || {
        p.with_mut(|c, _, _| *c.state = State::from_text(&text));
    });
}

/// The strip a panel fader (0-20: MIC 1-16, LINE 17/18-23/24, STEREO) moves,
/// in the edit buffer's numbering.
#[no_mangle]
pub extern "C" fn ld_y02r_panel_fader_strip(n: i32) -> i32 {
    u8::try_from(n).ok().and_then(livedaw_02r::midi::panel_fader).map_or(-1, |s: Y2Strip| params::index_of(s) as i32)
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_engine::{ConsoleConfig, Counts};

    #[test]
    fn the_handle_speaks_midi_for_an_02r_only() {
        let e = Engine::new(0);
        let p = unsafe { ld_y02r_create(&e) };
        unsafe {
            let tape1 = (20 * params::PER_STRIP) as i32;
            let change = livedaw_02r::midi::param_change(0, tape1 as usize, 100);
            // A WING: nothing.
            ld_y02r_midi_in(p, change.as_ptr(), change.len());
            assert_eq!(ld_y02r_get(p, tape1), -1);
            e.configure(&ConsoleConfig { profile: Profile::Yamaha02R, counts: Counts::Y02R });
            ld_y02r_midi_poll(p);
            ld_y02r_midi_in(p, change.as_ptr(), change.len());
            assert_eq!(ld_y02r_get(p, tape1), 100);
            // A move on the panel goes out as a parameter change.
            ld_y02r_set(p, 0, 103, true);
            ld_y02r_midi_poll(p);
            let mut buf = [0u8; 16];
            assert_eq!(ld_y02r_midi_out(p, buf.as_mut_ptr(), 16), 10);
            assert_eq!(buf[..10], livedaw_02r::midi::param_change(0, 0, 103)[..]);
            // A program change: an event.
            ld_y02r_midi_in(p, [0xc0, 9].as_ptr(), 2);
            let mut ev = LdY02REvent::default();
            assert!(ld_y02r_event(p, &mut ev, std::ptr::null_mut(), 0));
            assert_eq!((ev.kind, ev.value), (0, 10));
            // An edit buffer request: a dump too big for the buffer stays queued.
            let req = livedaw_02r::midi::request(0, b'M', 127);
            ld_y02r_midi_in(p, req.as_ptr(), req.len());
            assert_eq!(ld_y02r_midi_out(p, buf.as_mut_ptr(), 16), -4082);
            let mut big = vec![0u8; 4082];
            assert_eq!(ld_y02r_midi_out(p, big.as_mut_ptr(), big.len()), 4082);
            // Its own state lives in the engine's console text.
            e.set_console_text("pairs=1\n");
            assert!(ld_y02r_state_text(p).as_ref().map(|s| s.0.contains("pairs=1 0")).unwrap_or(false));
            // Libraries: EQ 9 onto CH 1, its name.
            assert!(ld_y02r_recall(p, 0, 8, 0));
            let name = ld_y02r_program_name(p, 0, 8);
            assert_eq!(name.as_ref().unwrap().0.as_str(), "E.Bass 1");
            assert_eq!(ld_y02r_panel_fader_strip(20), 54, "fader 21: STEREO");
            // Scene 0.
            ld_y02r_recall_initial(p);
            assert_eq!(ld_y02r_get(p, 0), 0);
            ld_y02r_destroy(p);
        }
    }
}
