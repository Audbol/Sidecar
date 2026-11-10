//! Allen & Heath MIDI over TCP for the host (`livedaw-ahmidi`): the dLive's and
//! the SQ-5's. The app's `WingRemote` holds one, starts it while the console's
//! profile names the protocol, and runs the scene recalls the clients ask for
//! (the show lives on the message thread). A handle borrows the engine it was
//! made with: the host destroys it before the engine.

use super::guard;
use livedaw_ahmidi::{Config, Server, Setup};
use livedaw_engine::Engine;
use livedaw_wing::server::{EngineHandle, EnginePtr};
use std::sync::Arc;

pub struct LdAhMidi {
    engine: *const Engine,
    server: Option<Server>,
    setup: Setup,
}

/// A stopped server for engine `e` (null if `e` is).
///
/// # Safety
/// `e` is a live engine that outlives the handle, or null.
#[no_mangle]
pub unsafe extern "C" fn ld_ahmidi_create(e: *const Engine) -> *mut LdAhMidi {
    if e.is_null() {
        return std::ptr::null_mut();
    }
    guard(std::ptr::null_mut(), || Box::into_raw(Box::new(LdAhMidi { engine: e, server: None, setup: Setup::default() })))
}

/// Stops the server and frees `p`.
///
/// # Safety
/// `p` is from `ld_ahmidi_create` (or null), not used after.
#[no_mangle]
pub unsafe extern "C" fn ld_ahmidi_destroy(p: *mut LdAhMidi) {
    if !p.is_null() {
        let mut p = Box::from_raw(p);
        if let Some(mut s) = p.server.take() {
            s.stop();
        }
    }
}

/// (Re)starts it on TCP `mixrack` (the MixRack's or SQ's port, 51325) and `surface`
/// (the Surface's, 51328); -1 doesn't listen, 0 takes any free port. False if a
/// port couldn't be bound (the other still listens; `ld_ahmidi_port` says which).
///
/// # Safety
/// `p` is from `ld_ahmidi_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_ahmidi_start(p: *mut LdAhMidi, mixrack: i32, surface: i32) -> bool {
    let Some(p) = p.as_mut() else { return false };
    guard(false, || {
        if let Some(mut s) = p.server.take() {
            s.stop();
        }
        let port = |n: i32| (n >= 0).then(|| u16::try_from(n).unwrap_or(0));
        let config = Config { mixrack: port(mixrack), surface: port(surface), setup: p.setup, ..Config::default() };
        let handle: Arc<dyn EngineHandle> = Arc::new(EnginePtr(p.engine));
        let server = Server::start(handle, &config);
        let ok = server.status().errors.is_empty();
        p.server = Some(server);
        ok
    })
}

/// # Safety
/// `p` is from `ld_ahmidi_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_ahmidi_stop(p: *mut LdAhMidi) {
    if let Some(p) = p.as_mut() {
        guard((), || {
            if let Some(mut s) = p.server.take() {
                s.stop();
            }
        });
    }
}

/// The TCP port it listens on: `which` 0 the MixRack's, 1 the Surface's (-1 not listening).
///
/// # Safety
/// `p` is from `ld_ahmidi_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_ahmidi_port(p: *const LdAhMidi, which: i32) -> i32 {
    let Some(s) = p.as_ref().and_then(|p| p.server.as_ref()) else { return -1 };
    let st = s.status();
    if which == 0 { st.mixrack } else { st.surface }.map_or(-1, |a| i32::from(a.port()))
}

/// How many clients are connected.
///
/// # Safety
/// `p` is from `ld_ahmidi_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_ahmidi_clients(p: *const LdAhMidi) -> i32 {
    p.as_ref().and_then(|p| p.server.as_ref()).map_or(0, |s| s.status().clients as i32)
}

/// The console's MIDI channel (from 0; a dLive's N, at most 11) and, on an SQ, the
/// NRPN fader law. Takes effect at once.
///
/// # Safety
/// `p` is from `ld_ahmidi_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_ahmidi_set_setup(p: *mut LdAhMidi, channel: i32, audio_taper: bool) {
    let Some(p) = p.as_mut() else { return };
    let setup = Setup { channel: channel.clamp(0, 15) as u8, audio_taper };
    p.setup = setup;
    if let Some(s) = &p.server {
        s.set_setup(setup);
    }
}

/// The next scene (from 1) a client asked to recall, 0 for none.
///
/// # Safety
/// `p` is from `ld_ahmidi_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_ahmidi_take_scene(p: *mut LdAhMidi) -> i32 {
    p.as_ref().and_then(|p| p.server.as_ref()).and_then(|s| s.take_scene()).map_or(0, i32::from)
}

/// Scene `n` (from 1) was recalled on the console: the clients are told (not the
/// one that asked for it).
///
/// # Safety
/// `p` is from `ld_ahmidi_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_ahmidi_scene_recalled(p: *mut LdAhMidi, n: i32) {
    if let (Some(s), Ok(n)) = (p.as_ref().and_then(|p| p.server.as_ref()), u16::try_from(n)) {
        s.scene_recalled(n);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_engine::{ConsoleConfig, Counts, Profile};

    #[test]
    fn starts_and_stops_on_a_free_port() {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::AhSq5, counts: Counts::SQ5 });
        unsafe {
            let p = ld_ahmidi_create(&e);
            assert_eq!(ld_ahmidi_port(p, 0), -1);
            assert!(ld_ahmidi_start(p, 0, -1));
            assert!(ld_ahmidi_port(p, 0) > 0);
            assert_eq!(ld_ahmidi_port(p, 1), -1, "no Surface on an SQ");
            assert_eq!(ld_ahmidi_clients(p), 0);
            ld_ahmidi_set_setup(p, 3, true);
            assert_eq!(ld_ahmidi_take_scene(p), 0);
            ld_ahmidi_scene_recalled(p, 3);
            ld_ahmidi_stop(p);
            assert_eq!(ld_ahmidi_port(p, 0), -1);
            ld_ahmidi_destroy(p);
            ld_ahmidi_destroy(std::ptr::null_mut());
        }
    }
}
