//! The X32's OSC server for the host (`livedaw-x32`, UDP 10023), run while the
//! console is an X32 and the network remote is on (`WingRemote` in the app
//! starts and stops it with the WING's servers).
//!
//! Like the WING remote's handle, an `LdX32` borrows the engine it was made
//! with: the host destroys it before the engine.

use super::guard;
use super::session::str_arg;
use livedaw_engine::Engine;
use livedaw_wing::server::{EngineHandle, EnginePtr};
use livedaw_x32::{Config, Identity, X32Server};
use std::sync::Arc;

pub struct LdX32 {
    engine: *const Engine,
    server: Option<X32Server>,
}

/// A stopped X32 server for engine `e` (null if `e` is).
///
/// # Safety
/// `e` is an engine that outlives the result, or null.
#[no_mangle]
pub unsafe extern "C" fn ld_x32_create(e: *const Engine) -> *mut LdX32 {
    if e.is_null() {
        return std::ptr::null_mut();
    }
    guard(std::ptr::null_mut(), || Box::into_raw(Box::new(LdX32 { engine: e, server: None })))
}

/// Stops the server and frees `x`.
///
/// # Safety
/// `x` is from `ld_x32_create` (or null), not used after.
#[no_mangle]
pub unsafe extern "C" fn ld_x32_destroy(x: *mut LdX32) {
    if !x.is_null() {
        let mut x = Box::from_raw(x);
        if let Some(mut s) = x.server.take() {
            s.stop();
        }
    }
}

/// (Re)starts the server as console `name` on UDP `port` (0: any free port).
/// False if the port couldn't be bound (the server isn't running then).
///
/// # Safety
/// `x` is from `ld_x32_create`; `name` holds `name_len` bytes of UTF-8 (or is null).
#[no_mangle]
pub unsafe extern "C" fn ld_x32_start(x: *mut LdX32, name: *const u8, name_len: usize, port: i32) -> bool {
    let Some(x) = x.as_mut() else { return false };
    let name = str_arg(name, name_len).trim().to_string();
    guard(false, || {
        if let Some(mut s) = x.server.take() {
            s.stop();
        }
        let mut identity = Identity::default();
        if !name.is_empty() {
            identity.name = name;
        }
        let config = Config { identity, port: u16::try_from(port).unwrap_or(0), ..Config::default() };
        let handle: Arc<dyn EngineHandle> = Arc::new(EnginePtr(x.engine));
        let server = X32Server::start(handle, &config);
        if server.status().errors.is_empty() {
            x.server = Some(server);
            true
        } else {
            false
        }
    })
}

/// Stops the server.
///
/// # Safety
/// `x` is from `ld_x32_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_x32_stop(x: *mut LdX32) {
    if let Some(x) = x.as_mut() {
        if let Some(mut s) = x.server.take() {
            s.stop();
        }
    }
}

/// The UDP port the server listens on, or -1 when it isn't running.
///
/// # Safety
/// `x` is from `ld_x32_create` (or null).
#[no_mangle]
pub unsafe extern "C" fn ld_x32_port(x: *const LdX32) -> i32 {
    x.as_ref().and_then(|x| x.server.as_ref()).and_then(|s| s.status().osc).map_or(-1, |a| i32::from(a.port()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_engine::{ConsoleConfig, Counts, Profile};
    use std::net::UdpSocket;
    use std::time::Duration;

    #[test]
    fn the_x32_server_starts_answers_and_stops() {
        let engine = Engine::new(0);
        engine.configure(&ConsoleConfig { profile: Profile::X32, counts: Counts::X32 });
        unsafe {
            let x = ld_x32_create(&engine);
            assert_eq!(ld_x32_port(x), -1);
            let name = "STAGE";
            assert!(ld_x32_start(x, name.as_ptr(), name.len(), 0));
            let port = ld_x32_port(x);
            assert!(port > 0);
            // /xinfo answers with the name.
            let sock = UdpSocket::bind("127.0.0.1:0").unwrap();
            sock.set_read_timeout(Some(Duration::from_secs(2))).unwrap();
            sock.send_to(b"/xinfo\0\0", ("127.0.0.1", port as u16)).unwrap();
            let mut buf = [0u8; 512];
            let (n, _) = sock.recv_from(&mut buf).unwrap();
            assert!(buf[..n].windows(5).any(|w| w == b"STAGE"), "{:?}", String::from_utf8_lossy(&buf[..n]));
            ld_x32_stop(x);
            assert_eq!(ld_x32_port(x), -1);
            ld_x32_destroy(x);
        }
    }
}
