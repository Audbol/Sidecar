//! The ProMix 01 for the host (`livedaw-promix`, ABI 44): its own state, its
//! program libraries and its MIDI protocol, over the engine. The app's
//! `ProMix` holds one for its MixerEngine; `ProMixMidi` drives its MIDI, the
//! ProMix surface its panel.
//!
//! An `LdProMix` borrows the engine it was made with: the host destroys it
//! before the engine. Every call does nothing (or answers none) while the
//! engine isn't a ProMix 01.

use super::guard;
use super::session::{str_arg, LdString};
use livedaw_engine::{Engine, Profile};
use livedaw_promix::console::{Console, Origin};
use livedaw_promix::library::Library;
use livedaw_promix::midi::{Event, Midi, Setup, Switches};
use livedaw_promix::{params, State};

pub struct LdProMix {
    engine: *const Engine,
    /// The console's own state: the engine's console text (`Engine::console_text`,
    /// which shows and scenes keep) as read at `version`.
    state: State,
    version: u64,
    library: Library,
    midi: Midi,
}

impl LdProMix {
    /// The console (None if the engine isn't a ProMix 01) and the MIDI state;
    /// the state read again if a show or a scene set it since.
    fn with<R>(&mut self, f: impl FnOnce(&mut Console, &mut Midi) -> R) -> Option<R> {
        // SAFETY: the host keeps the engine alive while `self` is.
        let engine = unsafe { self.engine.as_ref()? };
        if engine.profile() != Profile::ProMix01 {
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
        let mut c = Console::new(engine, &mut self.state, &self.library);
        Some(f(&mut c, &mut self.midi))
    }

    /// As `with`, then the state written back to the engine.
    fn with_mut<R>(&mut self, f: impl FnOnce(&mut Console, &mut Midi) -> R) -> Option<R> {
        let r = self.with(f)?;
        // SAFETY: as `with`.
        if let Some(engine) = unsafe { self.engine.as_ref() } {
            self.version = engine.set_console_text(&self.state.to_text());
        }
        Some(r)
    }
}

/// A ProMix 01 over engine `e` (null if `e` is).
///
/// # Safety
/// `e` is an engine that outlives the result, or null.
#[no_mangle]
pub unsafe extern "C" fn ld_promix_create(e: *const Engine) -> *mut LdProMix {
    if e.is_null() {
        return std::ptr::null_mut();
    }
    guard(std::ptr::null_mut(), || {
        Box::into_raw(Box::new(LdProMix {
            engine: e,
            state: State::default(),
            version: 0,
            library: Library::default(),
            midi: Midi::default(),
        }))
    })
}

/// # Safety
/// `p` is from `ld_promix_create` (or null), not used after.
#[no_mangle]
pub unsafe extern "C" fn ld_promix_destroy(p: *mut LdProMix) {
    if !p.is_null() {
        drop(Box::from_raw(p));
    }
}

/// Handles one received MIDI message (`len` bytes: a channel message, or a
/// whole SysEx from F0 to F7).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null); `bytes` holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_promix_midi_in(p: *mut LdProMix, bytes: *const u8, len: usize) {
    let Some(p) = p.as_mut() else { return };
    if bytes.is_null() || len == 0 {
        return;
    }
    let msg = std::slice::from_raw_parts(bytes, len).to_vec();
    guard((), || {
        p.with_mut(|c, m| m.receive(c, &msg));
    });
}

/// Queues the console's changes since the last call as control changes (the
/// first call takes the console's values and sends nothing).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_midi_poll(p: *mut LdProMix) {
    if let Some(p) = p.as_mut() {
        guard((), || {
            p.with(|c, m| m.poll(c));
        });
    }
}

/// Forgets what was sent: the next poll sends every value.
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_midi_resend(p: *mut LdProMix) {
    if let Some(p) = p.as_mut() {
        p.midi.resend_all();
    }
}

/// Scene `scene` (0-50; 0 scene 00) was recalled: its program change goes out.
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_scene_recalled(p: *mut LdProMix, scene: i32) {
    if let (Some(p), Ok(scene)) = (p.as_mut(), u8::try_from(scene)) {
        p.midi.scene_recalled(scene);
    }
}

/// The next message to send into `out` (`max` bytes): its length, 0 for none,
/// or minus its length if `out` is too small (it stays queued).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null); `out` holds `max` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_promix_midi_out(p: *mut LdProMix, out: *mut u8, max: usize) -> i32 {
    let Some(p) = p.as_mut() else { return 0 };
    let Some(msg) = p.midi.take_out() else { return 0 };
    if out.is_null() || msg.len() > max {
        let len = msg.len() as i32;
        // Put it back at the front: the caller asks again with room for it.
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

/// ABI 44: what the MIDI asks the app to do (`ld_promix_event`).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct LdProMixEvent {
    /// 0 recall scene `value` (0-50), 1 a request for scene `value`'s dump,
    /// 2 a dump of scene `value` (its bytes in the buffer, `size` of them),
    /// 3 Key Remote button `value` with data `data`.
    pub kind: i32,
    pub value: i32,
    pub data: i32,
    pub size: i32,
}

/// The next event into `out`, a scene dump's bytes into `bytes` (up to
/// `max`); false for none.
///
/// # Safety
/// `p` is from `ld_promix_create` (or null); `out` is writable; `bytes` holds `max` bytes or is null.
#[no_mangle]
pub unsafe extern "C" fn ld_promix_event(p: *mut LdProMix, out: *mut LdProMixEvent, bytes: *mut u8, max: usize) -> bool {
    let (Some(p), Some(out)) = (p.as_mut(), out.as_mut()) else { return false };
    let Some(e) = p.midi.take_event() else { return false };
    *out = match e {
        Event::RecallScene(s) => LdProMixEvent { kind: 0, value: s as i32, ..Default::default() },
        Event::SceneDumpRequest(s) => LdProMixEvent { kind: 1, value: s as i32, ..Default::default() },
        Event::SceneDump(s, data) => {
            let n = data.len().min(max);
            if !bytes.is_null() {
                std::ptr::copy_nonoverlapping(data.as_ptr(), bytes, n);
            }
            LdProMixEvent { kind: 2, value: s as i32, data: 0, size: n as i32 }
        }
        Event::Key(k, d) => LdProMixEvent { kind: 3, value: k as i32, data: d as i32, size: 0 },
    };
    true
}

/// The Edit Buffer as scene bytes (408, `livedaw_promix::midi::scene_bytes`)
/// into `out`; how many were written (0 if the engine isn't a ProMix 01).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null); `out` holds `max` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_promix_scene_bytes(p: *mut LdProMix, out: *mut u8, max: usize) -> i32 {
    let Some(p) = p.as_mut() else { return 0 };
    let bytes = guard(None, || p.with(|c, _| livedaw_promix::midi::scene_bytes(c))).unwrap_or_default();
    let n = bytes.len().min(max);
    if !out.is_null() {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), out, n);
    }
    n as i32
}

/// Sets the console from scene bytes.
///
/// # Safety
/// `p` is from `ld_promix_create` (or null); `data` holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_promix_load_scene_bytes(p: *mut LdProMix, data: *const u8, len: usize) {
    let Some(p) = p.as_mut() else { return };
    if data.is_null() {
        return;
    }
    let data = std::slice::from_raw_parts(data, len).to_vec();
    guard((), || {
        p.with_mut(|c, _| livedaw_promix::midi::load_scene_bytes(c, &data));
    });
}

/// Queues a bulk dump of scene `scene` (1-50) holding `data` (scene bytes):
/// the answer to an `ld_promix_event` request.
///
/// # Safety
/// `p` is from `ld_promix_create` (or null); `data` holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ld_promix_send_scene_dump(p: *mut LdProMix, scene: i32, data: *const u8, len: usize) {
    let Some(p) = p.as_mut() else { return };
    if data.is_null() || !(1..=50).contains(&scene) {
        return;
    }
    let data = std::slice::from_raw_parts(data, len);
    let msg = livedaw_promix::midi::dump(p.midi.setup.tx_channel, b'M', scene as u8, data);
    p.midi.push_out(msg);
}

/// ABI 44: MIDI SETUP (`livedaw_promix::midi::Setup`): channels 0-15; per
/// kind (program, control, bulk) TX, RX, OMNI, ECHO as 0 / 1.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct LdProMixSetup {
    pub tx_channel: i32,
    pub rx_channel: i32,
    pub switches: [[u8; 4]; 3],
    pub register_mode: u8,
    pub interval: u8,
}

impl From<&Setup> for LdProMixSetup {
    fn from(s: &Setup) -> Self {
        let sw = |x: Switches| [x.tx, x.rx, x.omni, x.echo].map(u8::from);
        LdProMixSetup {
            tx_channel: s.tx_channel as i32,
            rx_channel: s.rx_channel as i32,
            switches: [sw(s.program), sw(s.control), sw(s.bulk)],
            register_mode: s.register_mode as u8,
            interval: s.interval,
        }
    }
}

impl From<&LdProMixSetup> for Setup {
    fn from(s: &LdProMixSetup) -> Self {
        let sw = |x: [u8; 4]| Switches { tx: x[0] != 0, rx: x[1] != 0, omni: x[2] != 0, echo: x[3] != 0 };
        Setup {
            tx_channel: s.tx_channel.clamp(0, 15) as u8,
            rx_channel: s.rx_channel.clamp(0, 15) as u8,
            program: sw(s.switches[0]),
            control: sw(s.switches[1]),
            bulk: sw(s.switches[2]),
            register_mode: s.register_mode != 0,
            interval: s.interval.min(10),
        }
    }
}

/// # Safety
/// `p` is from `ld_promix_create` (or null); `out` is writable or null.
#[no_mangle]
pub unsafe extern "C" fn ld_promix_get_setup(p: *const LdProMix, out: *mut LdProMixSetup) {
    if let (Some(p), Some(out)) = (p.as_ref(), out.as_mut()) {
        *out = (&p.midi.setup).into();
    }
}

/// # Safety
/// `p` is from `ld_promix_create` (or null); `setup` is readable or null.
#[no_mangle]
pub unsafe extern "C" fn ld_promix_set_setup(p: *mut LdProMix, setup: *const LdProMixSetup) {
    if let (Some(p), Some(s)) = (p.as_mut(), setup.as_ref()) {
        p.midi.setup = s.into();
    }
}

/// Parameter `n`'s internal value (-1: none, or not a ProMix 01).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_param(p: *mut LdProMix, n: i32) -> i32 {
    let (Some(p), Ok(n)) = (p.as_mut(), u16::try_from(n)) else { return -1 };
    guard(None, || p.with(|c, _| params::get(c, n))).flatten().map_or(-1, i32::from)
}

/// How many values parameter `n` has (0: none).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_param_steps(p: *mut LdProMix, n: i32) -> i32 {
    let (Some(p), Ok(n)) = (p.as_mut(), u16::try_from(n)) else { return 0 };
    guard(None, || p.with(|c, _| params::steps(c, n))).map_or(0, i32::from)
}

/// Sets parameter `n` as the panel does (a group follows a fader).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_set_param(p: *mut LdProMix, n: i32, value: i32) {
    let (Some(p), Ok(n), Ok(v)) = (p.as_mut(), u16::try_from(n), u16::try_from(value.max(0))) else { return };
    guard((), || {
        p.with_mut(|c, _| params::set(c, n, v, Origin::Panel));
    });
}

/// Parameter `n`'s value as the ProMix's LCD shows it (empty for none).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_param_text(p: *mut LdProMix, n: i32) -> *mut LdString {
    let (Some(p), Ok(n)) = (p.as_mut(), u16::try_from(n)) else { return std::ptr::null_mut() };
    let text = guard(None, || p.with(|c, _| params::text(c, n))).unwrap_or_default();
    LdString::new(text)
}

/// Parameter `n`'s value `value` as the ProMix's LCD would show it (a choice
/// waiting for ENTER).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_value_text(p: *mut LdProMix, n: i32, value: i32) -> *mut LdString {
    let (Some(p), Ok(n), Ok(v)) = (p.as_mut(), u16::try_from(n), u16::try_from(value.max(0))) else {
        return std::ptr::null_mut();
    };
    let text = guard(None, || p.with(|c, _| params::text_of(c, n, v))).unwrap_or_default();
    LdString::new(text)
}

/// Parameter `n`'s short label on the ProMix's LCD ("CH3", "LOW F", "THRESHOLD").
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_param_label(p: *mut LdProMix, n: i32) -> *mut LdString {
    let (Some(p), Ok(n)) = (p.as_mut(), u16::try_from(n)) else { return std::ptr::null_mut() };
    let text = guard(None, || p.with(|c, _| params::label(c, n))).unwrap_or_default();
    LdString::new(text)
}

/// The name of program `n` (0-based) of library `kind` (0 effects, 1-40; 1
/// dynamics, 1-20; 2 EQ, 1-50), null past its end.
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_program_name(p: *const LdProMix, kind: i32, n: i32) -> *mut LdString {
    let (Some(p), Ok(n)) = (p.as_ref(), usize::try_from(n)) else { return std::ptr::null_mut() };
    let name = match kind {
        0 => p.library.fx_program(n).map(|x| x.name),
        1 => p.library.program(n).map(|x| x.name),
        _ => p.library.eq_program(n).map(|x| x.name),
    };
    name.map_or(std::ptr::null_mut(), LdString::new)
}

/// Recalls EQ program `n` (0-49) onto EQ strip `strip` (0-15 CH1-16, 16 ST IN,
/// 17-18 RTN1-2, 19 ST OUT) and its pair partner.
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_recall_eq(p: *mut LdProMix, strip: i32, n: i32) {
    let (Some(p), Ok(strip), Ok(n)) = (p.as_mut(), usize::try_from(strip), usize::try_from(n)) else { return };
    guard((), || {
        p.with_mut(|c, _| {
            if let Some(s) = livedaw_promix::console::PmStrip::eq_strip(strip) {
                c.recall_eq_program(s, n);
            }
        });
    });
}

/// Stores what's on `target` as user program `n` (0-based) of library `kind`
/// (0 effects 31-40 from effect `target`; 1 dynamics 11-20 from processor
/// `target`; 2 EQ 31-50 from EQ strip `target`), named `name`. Whether it was
/// stored (a preset can't be overwritten).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null); `name` holds `len` bytes of UTF-8.
#[no_mangle]
pub unsafe extern "C" fn ld_promix_store_program(p: *mut LdProMix, kind: i32, n: i32, target: i32, name: *const u8, len: usize) -> bool {
    let (Some(p), Ok(n), Ok(target)) = (p.as_mut(), usize::try_from(n), usize::try_from(target)) else { return false };
    let name = str_arg(name, len);
    guard(false, || {
        enum Prog {
            Fx(livedaw_promix::library::FxProgram),
            Dyn(livedaw_promix::library::DynProgram),
            Eq(livedaw_promix::library::EqProgram),
        }
        let prog = p
            .with(|c, _| match kind {
                0 => c.fx_program_of(target, &name).map(Prog::Fx),
                1 => c.dyn_program_of(target, &name).map(Prog::Dyn),
                _ => livedaw_promix::console::PmStrip::eq_strip(target).map(|s| Prog::Eq(c.eq_program_of(s, &name))),
            })
            .flatten();
        match prog {
            Some(Prog::Fx(x)) => p.library.store_fx(n, x),
            Some(Prog::Dyn(x)) => p.library.store_dyn(n, x),
            Some(Prog::Eq(x)) => p.library.store_eq(n, x),
            None => false,
        }
    })
}

/// The user programs as text (`Library::user_text`), for the host to keep.
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_library_text(p: *const LdProMix) -> *mut LdString {
    p.as_ref().map_or(std::ptr::null_mut(), |p| LdString::new(p.library.user_text()))
}

/// Sets the user programs from `ld_promix_library_text`'s text.
///
/// # Safety
/// `p` is from `ld_promix_create` (or null); `text` holds `len` bytes of UTF-8.
#[no_mangle]
pub unsafe extern "C" fn ld_promix_set_library_text(p: *mut LdProMix, text: *const u8, len: usize) {
    if let Some(p) = p.as_mut() {
        let text = str_arg(text, len);
        p.library.apply_text(&text);
    }
}

/// PROGRAM CHANGE ASSIGN (p.82): the scene program `pc` (0-127) recalls (-1 none).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_pc_scene(p: *const LdProMix, pc: i32) -> i32 {
    let (Some(p), Ok(pc)) = (p.as_ref(), usize::try_from(pc)) else { return -1 };
    p.midi.pc_table.scenes.get(pc).copied().flatten().map_or(-1, i32::from)
}

/// Sets it (`scene` -1: none; else 0-50).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_set_pc_scene(p: *mut LdProMix, pc: i32, scene: i32) {
    let (Some(p), Ok(pc)) = (p.as_mut(), usize::try_from(pc)) else { return };
    if let Some(s) = p.midi.pc_table.scenes.get_mut(pc) {
        *s = u8::try_from(scene).ok().filter(|&x| x <= 50);
    }
}

/// CONTROL CHANGE ASSIGN (p.83): parameter `n`'s controller as `bank << 8 | cc`
/// (-1 none).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_cc_of(p: *const LdProMix, n: i32) -> i32 {
    let (Some(p), Ok(n)) = (p.as_ref(), u16::try_from(n)) else { return -1 };
    p.midi.cc_map.of(n).map_or(-1, |(bank, cc)| i32::from(bank) << 8 | i32::from(cc))
}

/// Sets it (`code` -1: none; bank 0-15, cc 0-95).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_set_cc(p: *mut LdProMix, n: i32, code: i32) {
    let (Some(p), Ok(n)) = (p.as_mut(), usize::try_from(n)) else { return };
    if let Some(m) = p.midi.cc_map.map.get_mut(n) {
        let (bank, cc) = (code >> 8, code & 0xff);
        *m = (code >= 0 && bank < 16 && cc < 96).then_some((bank as u8, cc as u8));
    }
}

/// Parameter `n`'s name (the Control Change table's words).
#[no_mangle]
pub extern "C" fn ld_promix_param_name(n: i32) -> *mut LdString {
    u16::try_from(n).map_or(std::ptr::null_mut(), |n| LdString::new(params::name(n)))
}

/// The console's own state as text (`State::to_text`, for the show).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_state_text(p: *mut LdProMix) -> *mut LdString {
    let Some(p) = p.as_mut() else { return std::ptr::null_mut() };
    p.with(|_, _| ());
    LdString::new(p.state.to_text())
}

/// Sets the console's own state from text (missing values: the defaults).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null); `text` holds `len` bytes of UTF-8.
#[no_mangle]
pub unsafe extern "C" fn ld_promix_set_state_text(p: *mut LdProMix, text: *const u8, len: usize) {
    if let Some(p) = p.as_mut() {
        let state = State::from_text(&str_arg(text, len));
        p.with_mut(|c, _| *c.state = state);
    }
}

/// Recalls scene 00, the console's initial data (p.62): every mix setting as
/// a new ProMix 01 has it (its names and patches stay).
///
/// # Safety
/// `p` is from `ld_promix_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_promix_recall_initial(p: *mut LdProMix) {
    if let Some(p) = p.as_mut() {
        guard((), || {
            p.with_mut(|c, _| livedaw_promix::midi::load_scene_bytes(c, livedaw_promix::midi::initial_bytes()));
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_engine::{ConsoleConfig, Counts};

    #[test]
    fn the_handle_speaks_midi_for_a_promix01_only() {
        let e = Engine::new(0);
        let p = unsafe { ld_promix_create(&e) };
        unsafe {
            // A WING: nothing.
            ld_promix_midi_in(p, [0xb0, 3, 115].as_ptr(), 3);
            assert_eq!(ld_promix_param(p, 3), -1);
            e.configure(&ConsoleConfig { profile: Profile::ProMix01, counts: Counts::PROMIX01 });
            ld_promix_midi_poll(p);
            ld_promix_midi_in(p, [0xb0, 3, 115].as_ptr(), 3);
            assert_eq!(ld_promix_param(p, 3), 115);
            ld_promix_set_param(p, 22, 0);
            ld_promix_midi_poll(p);
            let mut buf = [0u8; 4];
            assert_eq!(ld_promix_midi_out(p, buf.as_mut_ptr(), 4), 3);
            assert_eq!(buf[..3], [0xb0, 22, 0]);
            assert_eq!(ld_promix_midi_out(p, buf.as_mut_ptr(), 4), 0);
            // A program change: an event.
            ld_promix_midi_in(p, [0xc0, 9].as_ptr(), 2);
            let mut ev = LdProMixEvent::default();
            assert!(ld_promix_event(p, &mut ev, std::ptr::null_mut(), 0));
            assert_eq!((ev.kind, ev.value), (0, 10));
            // A dump too big for the buffer stays queued.
            let req = livedaw_promix::midi::request(0, b'M', 127);
            ld_promix_midi_in(p, req.as_ptr(), req.len());
            assert_eq!(ld_promix_midi_out(p, buf.as_mut_ptr(), 4), -834);
            let mut big = vec![0u8; 834];
            assert_eq!(ld_promix_midi_out(p, big.as_mut_ptr(), big.len()), 834);
            let mut setup = LdProMixSetup::default();
            ld_promix_get_setup(p, &mut setup);
            assert_eq!((setup.tx_channel, setup.switches[1]), (0, [1, 1, 0, 0]));
            setup.register_mode = 1;
            ld_promix_set_setup(p, &setup);
            // Its own state lives in the engine's console text (shows and scenes keep it).
            ld_promix_set_param(p, 503, 3); // pair CH1/2
            assert!(e.console_text().0.contains("pairs=1 "));
            e.set_console_text("pairs=0 1 0 0 0 0 0 0\n");
            assert_eq!((ld_promix_param(p, 396), ld_promix_param(p, 397)), (0, 1), "a recalled scene's pairs");
            // Scene 00: the initial data.
            ld_promix_set_param(p, 3, 20);
            ld_promix_recall_initial(p);
            assert_eq!((ld_promix_param(p, 3), ld_promix_param(p, 397)), (0, 0));
            ld_promix_destroy(p);
        }
    }
}
