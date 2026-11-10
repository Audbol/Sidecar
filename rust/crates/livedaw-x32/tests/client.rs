//! The X32 client (Sidecar controlling a console) against Sidecar's own server
//! on localhost: one engine plays the console, another connects. What this
//! cannot show is in the report (real firmware timing, packet loss, replies
//! that differ from this server's).

use livedaw_engine::{ConsoleConfig, Counts, Engine, Profile};
use livedaw_wing::server::EngineHandle;
use livedaw_x32::map::State;
use livedaw_x32::{Config, Console, Identity, LinkState, Tree, Val, X32Client, X32Server};
use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::sync::Arc;
use std::time::{Duration, Instant};

fn engine() -> Arc<Engine> {
    let e = Arc::new(Engine::new(0));
    e.configure(&ConsoleConfig { profile: Profile::X32, counts: Counts::X32 });
    e
}

fn serve(e: &Arc<Engine>, port: u16) -> X32Server {
    let config = Config {
        identity: Identity { name: "TEST-X32".into(), model: "X32C".into(), firmware: "4.09".into() },
        bind: IpAddr::V4(Ipv4Addr::LOCALHOST),
        port,
        xremote_life: Duration::from_secs(10),
        meter_life: Duration::from_secs(10),
    };
    let handle: Arc<dyn EngineHandle> = Arc::new(e.clone());
    let s = X32Server::start(handle, &config);
    assert!(s.status().errors.is_empty(), "{:?}", s.status().errors);
    s
}

/// Reads and writes an engine through the table (its own State: only engine-backed paths are used).
struct Acc {
    e: Arc<Engine>,
    st: State,
}

impl Acc {
    fn new(e: &Arc<Engine>) -> Self {
        Self { e: e.clone(), st: State::default() }
    }
    fn idx(path: &str) -> usize {
        match Tree::get().find(path) {
            Some(livedaw_x32::Found::Leaf(i)) => i,
            _ => panic!("no leaf {path}"),
        }
    }
    fn set(&self, path: &str, v: f32) {
        let c = Console::new(&self.e, &self.st);
        c.set(Self::idx(path), Val::Real(v));
    }
    fn get(&self, path: &str) -> f32 {
        let c = Console::new(&self.e, &self.st);
        c.get(Self::idx(path)).real()
    }
}

fn wait(what: &str, mut f: impl FnMut() -> bool) {
    let t = Instant::now();
    while t.elapsed() < Duration::from_secs(15) {
        if f() {
            return;
        }
        std::thread::sleep(Duration::from_millis(20));
    }
    panic!("timed out: {what}");
}

fn near(a: f32, b: f32) -> bool {
    (a - b).abs() < 0.3
}

#[test]
fn reads_first_then_both_ways_without_echo_then_lost_and_back() {
    let (console, local) = (engine(), engine());
    let (con, loc) = (Acc::new(&console), Acc::new(&local));
    con.set("/ch/01/mix/fader", -10.0);
    con.set("/ch/02/mix/fader", -20.0);
    // Sidecar's own differing state: the console must win, and must not hear of it.
    loc.set("/ch/01/mix/fader", -3.0);
    loc.set("/ch/05/mix/fader", -6.0);
    loc.set("/ch/05/mix/on", 0.0);
    let c5 = con.get("/ch/05/mix/fader");
    let server = serve(&console, 0);
    let addr: SocketAddr = server.status().osc.unwrap();
    let handle: Arc<dyn EngineHandle> = Arc::new(local.clone());
    let client = X32Client::start_with(handle, addr, Duration::from_millis(1500)).unwrap();
    wait("connected", || client.status().state == LinkState::Connected);
    let st = client.status();
    assert_eq!((st.name.as_str(), st.model.as_str()), ("TEST-X32", "X32C"));
    assert!(st.read_done > 500, "read {} addresses", st.read_done);
    // 1. The console wins on connect, and nothing was pushed to it.
    assert!(near(loc.get("/ch/01/mix/fader"), -10.0));
    assert!(near(loc.get("/ch/02/mix/fader"), -20.0));
    assert!(near(con.get("/ch/05/mix/fader"), c5), "the console was written to on connect");
    assert!(near(loc.get("/ch/05/mix/fader"), c5), "the console's value didn't reach Sidecar");
    // 2. A local change goes to the console, only that.
    loc.set("/ch/03/mix/fader", -15.0);
    wait("local change reaches the console", || near(con.get("/ch/03/mix/fader"), -15.0));
    // 3. A console change reaches Sidecar, without an echo back.
    std::thread::sleep(Duration::from_millis(700));
    let before = client.status().sent;
    for n in 1..=32 {
        con.set(&format!("/ch/{n:02}/mix/fader"), -30.0 + n as f32 * 0.5);
    }
    wait("console change reaches Sidecar", || {
        near(loc.get("/ch/32/mix/fader"), -14.0) && near(loc.get("/ch/07/mix/fader"), -26.5)
    });
    std::thread::sleep(Duration::from_millis(600));
    let sent = client.status().sent - before;
    assert!(sent <= 2, "{sent} messages sent after a change that came from the console (an echo)");
    // 4. The console goes quiet: LOST, and nothing is sent.
    let port = addr.port();
    drop(server);
    wait("lost", || client.status().state == LinkState::Lost);
    let lost_sent = client.status().sent;
    loc.set("/ch/08/mix/fader", -9.0); // made while LOST: must be dropped, never sent
                                       // 5. The console is back (with changes of its own): read again before anything is sent.
    con.set("/ch/09/mix/fader", -12.0);
    let server = serve(&console, port);
    wait("connected again", || client.status().state == LinkState::Connected);
    assert!(near(loc.get("/ch/09/mix/fader"), -12.0), "re-read on reconnect");
    assert!(near(loc.get("/ch/08/mix/fader"), -30.0 + 8.0 * 0.5), "the local change made while LOST is dropped");
    assert!(near(con.get("/ch/08/mix/fader"), -30.0 + 8.0 * 0.5), "a change made while LOST was pushed");
    assert!(client.status().sent > lost_sent);
    // ... and it works again both ways.
    loc.set("/ch/10/mix/fader", -4.0);
    wait("pushes again", || near(con.get("/ch/10/mix/fader"), -4.0));
    drop(client);
    drop(server);
}

#[test]
fn a_held_control_ignores_the_console_until_let_go() {
    let (console, local) = (engine(), engine());
    let (con, loc) = (Acc::new(&console), Acc::new(&local));
    let server = serve(&console, 0);
    let handle: Arc<dyn EngineHandle> = Arc::new(local.clone());
    let client = X32Client::start(handle, server.status().osc.unwrap()).unwrap();
    wait("connected", || client.status().state == LinkState::Connected);
    // The hand moves the fader; the console changes it at the same moment.
    loc.set("/ch/01/mix/fader", -8.0);
    con.set("/ch/01/mix/fader", -40.0);
    std::thread::sleep(Duration::from_millis(150));
    assert!(near(loc.get("/ch/01/mix/fader"), -8.0), "the console's value fought the control in the hand");
    // Let go: the console's last word stands.
    wait("applied after the hold", || near(loc.get("/ch/01/mix/fader"), -40.0) || near(con.get("/ch/01/mix/fader"), -8.0));
}

#[test]
fn nothing_answers_means_lost_with_nothing_sent_but_probes() {
    let local = engine();
    let loc = Acc::new(&local);
    let silent = std::net::UdpSocket::bind("127.0.0.1:0").unwrap();
    let handle: Arc<dyn EngineHandle> = Arc::new(local.clone());
    let client = X32Client::start(handle, silent.local_addr().unwrap()).unwrap();
    wait("lost", || client.status().state == LinkState::Lost);
    assert!(!client.status().error.is_empty());
    loc.set("/ch/01/mix/fader", -5.0);
    silent.set_read_timeout(Some(Duration::from_millis(300))).unwrap();
    let mut buf = [0u8; 2048];
    while let Ok((n, _)) = silent.recv_from(&mut buf) {
        let m = livedaw_wing::osc::decode(&buf[..n]).unwrap();
        assert!(m.addr == "/xinfo" || m.addr == "/xremote" || m.addr == "/status", "sent {}", m.addr);
    }
}
