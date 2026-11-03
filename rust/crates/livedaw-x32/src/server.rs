//! The X32's OSC server (UDP 10023), on std::net threads: one answers the
//! requests, one pushes changes to the `/xremote` clients and sends the
//! meters.
//!
//! Requests: `/info`, `/xinfo`, `/status`; a get (an address alone) and a set
//! (an address and its value; a node address and several ints and floats sets
//! its values in order); `/node ,s <path>` (the values as text, replied to
//! `node`) and `/ ,s "<path> v1 v2..."` (sets them from text, echoed back);
//! `/xremote` (every change, from anywhere, to up to 4 clients for 10 s);
//! `/meters`, `/renew` and `/unsubscribe`.
//!
//! Nothing here runs on the audio thread: the server reads and writes the
//! engine's shared atomics like the UI does.

use crate::map::{Console, Found, State, Tree};
use crate::meters::{self, Levels};
use crate::value::Val;
use livedaw_engine::Engine;
use livedaw_wing::osc::{self, Arg, Message};
use livedaw_wing::server::{bind_udp, local_ip_for, EngineHandle};
use std::io::ErrorKind;
use std::net::{IpAddr, Ipv4Addr, SocketAddr, UdpSocket};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

/// The X32's port.
pub const OSC_PORT: u16 = 10023;
/// `/xremote` lasts 10 s and serves up to 4 clients (the console's rules).
pub const XREMOTE_LIFE: Duration = Duration::from_secs(10);
pub const MAX_XREMOTE: usize = 4;
/// A meter request runs for 10 s.
pub const METER_LIFE: Duration = Duration::from_secs(10);
/// Meters go out every 50 ms times the request's time factor.
const TICK: Duration = Duration::from_millis(50);
/// Changes from elsewhere (the interface, scenes) are looked for this often (in ticks).
const POLL_TICKS: u64 = 2;
/// Pushed changes leave at most this many at once, then the sender pauses a
/// millisecond: a scene recall changes thousands of values, and a phone or
/// tablet on Wi-Fi would miss some sent in one go.
const PUSH_BURST: usize = 32;

/// What the console says it is (`/info`, `/xinfo`, `/status`).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Identity {
    /// The network name.
    pub name: String,
    /// X32, X32C, X32RACK, X32P, X32CORE...
    pub model: String,
    pub firmware: String,
}

impl Default for Identity {
    fn default() -> Self {
        Self { name: "Sidecar".into(), model: "X32".into(), firmware: "4.09".into() }
    }
}

#[derive(Clone, Debug)]
pub struct Config {
    pub identity: Identity,
    pub bind: IpAddr,
    /// 0 = any free port (tests).
    pub port: u16,
    /// How long `/xremote` and `/meters` last (tests shorten them).
    pub xremote_life: Duration,
    pub meter_life: Duration,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            identity: Identity::default(),
            bind: IpAddr::V4(Ipv4Addr::UNSPECIFIED),
            port: OSC_PORT,
            xremote_life: XREMOTE_LIFE,
            meter_life: METER_LIFE,
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct Status {
    /// Where the server listens (None if its port couldn't be bound).
    pub osc: Option<SocketAddr>,
    pub errors: Vec<String>,
    /// The `/xremote` clients.
    pub xremote: Vec<SocketAddr>,
    pub messages: u64,
}

struct MeterSub {
    dest: SocketAddr,
    /// The reply's address ("/meters/6").
    name: String,
    id: u32,
    params: Vec<i32>,
    every: u64,
    next: u64,
    expires: Instant,
}

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

fn send(sock: &UdpSocket, m: &Message, to: SocketAddr) {
    let _ = sock.send_to(&osc::encode(m), to);
}

/// State shared by the server threads.
pub struct Shared {
    engine: Arc<dyn EngineHandle>,
    /// The values the engine has no place for.
    pub state: State,
    identity: Mutex<Identity>,
    xremote_life: Duration,
    meter_life: Duration,
    xremote: Mutex<Vec<(SocketAddr, Instant)>>,
    meters: Mutex<Vec<MeterSub>>,
    /// The values the `/xremote` clients last heard, by entry (None while
    /// nobody listens).
    heard: Mutex<Option<Vec<Arg>>>,
    stop: AtomicBool,
    messages: AtomicU64,
}

impl Shared {
    pub fn new(engine: Arc<dyn EngineHandle>, config: &Config) -> Self {
        Self {
            engine,
            state: State::default(),
            identity: Mutex::new(config.identity.clone()),
            xremote_life: config.xremote_life,
            meter_life: config.meter_life,
            xremote: Mutex::default(),
            meters: Mutex::default(),
            heard: Mutex::new(None),
            stop: AtomicBool::new(false),
            messages: AtomicU64::new(0),
        }
    }

    pub fn engine(&self) -> &Engine {
        self.engine.engine()
    }

    pub fn console(&self) -> Console<'_> {
        Console::new(self.engine.engine(), &self.state)
    }

    pub fn set_identity(&self, id: Identity) {
        *lock(&self.identity) = id;
    }

    /// The `/xremote` clients still listening.
    pub fn xremote_clients(&self) -> Vec<SocketAddr> {
        let now = Instant::now();
        let mut subs = lock(&self.xremote);
        subs.retain(|(_, t)| *t > now);
        subs.iter().map(|(a, _)| *a).collect()
    }

    fn subscribe(&self, dest: SocketAddr) {
        let now = Instant::now();
        let mut subs = lock(&self.xremote);
        subs.retain(|(a, t)| *t > now && *a != dest);
        if subs.len() >= MAX_XREMOTE {
            // The one closest to running out makes way.
            if let Some(i) = subs.iter().enumerate().min_by_key(|(_, s)| s.1).map(|(i, _)| i) {
                subs.remove(i);
            }
        }
        subs.push((dest, now + self.xremote_life));
        drop(subs);
        // What the client starts from, so that a change before the next poll isn't lost.
        let mut heard = lock(&self.heard);
        if heard.is_none() {
            *heard = Some(self.snapshot(&self.console()));
        }
    }

    fn snapshot(&self, c: &Console) -> Vec<Arg> {
        c.tree.entries.iter().enumerate().map(|(i, e)| e.ty.to_arg(&c.get(i))).collect()
    }

    /// Sets values for a client and tells the other `/xremote` clients.
    pub fn apply(&self, sock: Option<&UdpSocket>, sets: Vec<(usize, Val)>, origin: Option<SocketAddr>) {
        if sets.is_empty() {
            return;
        }
        let mut heard = lock(&self.heard);
        let c = self.console();
        let mut out = Vec::with_capacity(sets.len());
        for (i, v) in sets {
            let e = &c.tree.entries[i];
            let arg = e.ty.to_arg(&c.set(i, v));
            if let Some(h) = heard.as_mut() {
                h[i] = arg.clone();
            }
            out.push(Message::new(e.path.clone(), vec![arg]));
        }
        drop(heard);
        if let Some(sock) = sock {
            for to in self.xremote_clients().into_iter().filter(|a| Some(*a) != origin) {
                for m in &out {
                    send(sock, m, to);
                }
            }
        }
    }

    /// Finds what changed since the last look and pushes it to the `/xremote` clients.
    pub fn poll_changes(&self, sock: &UdpSocket) {
        let clients = self.xremote_clients();
        let mut heard = lock(&self.heard);
        if clients.is_empty() {
            *heard = None;
            return;
        }
        let c = self.console();
        let now = self.snapshot(&c);
        let Some(old) = heard.as_mut() else {
            *heard = Some(now);
            return;
        };
        let mut changed = Vec::new();
        for (i, a) in now.into_iter().enumerate() {
            if old[i] != a {
                old[i] = a.clone();
                changed.push(Message::new(c.tree.entries[i].path.clone(), vec![a]));
            }
        }
        drop(heard);
        for (n, m) in changed.iter().enumerate() {
            for &to in &clients {
                send(sock, m, to);
            }
            if n % PUSH_BURST == PUSH_BURST - 1 {
                std::thread::sleep(Duration::from_millis(1));
            }
        }
    }

    fn subscribe_meters(&self, dest: SocketAddr, args: &[Arg]) {
        let Some(Arg::Str(name)) = args.first() else { return };
        let name = format!("/{}", name.trim_start_matches('/'));
        let Some(id) = name.strip_prefix("/meters/").and_then(|n| n.parse::<u32>().ok()) else { return };
        let ints: Vec<i32> = args[1..].iter().filter_map(|a| if let Arg::Int(i) = a { Some(*i) } else { None }).collect();
        let n = meters::param_count(id).min(ints.len());
        let params = ints[..n].to_vec();
        if Levels::default().frame(id, &params).is_none() {
            return;
        }
        // 50 ms times 1-99; anything else is 1.
        let every = ints.get(n).copied().filter(|t| (1..=99).contains(t)).unwrap_or(1) as u64;
        let mut subs = lock(&self.meters);
        subs.retain(|s| !(s.dest == dest && s.name == name && s.params == params));
        subs.push(MeterSub { dest, name, id, params, every, next: 0, expires: Instant::now() + self.meter_life });
    }

    fn renew(&self, dest: SocketAddr, args: &[Arg]) {
        let name = match args.first() {
            Some(Arg::Str(s)) => Some(format!("/{}", s.trim_start_matches('/'))),
            _ => None,
        };
        let until = Instant::now() + self.meter_life;
        for s in lock(&self.meters).iter_mut().filter(|s| s.dest == dest && name.as_ref().is_none_or(|n| *n == s.name)) {
            s.expires = until;
        }
    }

    fn unsubscribe(&self, dest: SocketAddr) {
        lock(&self.xremote).retain(|(a, _)| *a != dest);
        lock(&self.meters).retain(|s| s.dest != dest);
    }

    fn send_meters(&self, sock: &UdpSocket, tick: u64) {
        let now = Instant::now();
        let due: Vec<(SocketAddr, String, u32, Vec<i32>)> = {
            let mut subs = lock(&self.meters);
            subs.retain(|s| s.expires > now);
            subs.iter_mut()
                .filter(|s| tick >= s.next)
                .map(|s| {
                    s.next = tick + s.every;
                    (s.dest, s.name.clone(), s.id, s.params.clone())
                })
                .collect()
        };
        if due.is_empty() {
            return;
        }
        let levels = Levels::take(&self.console());
        for (dest, name, id, params) in due {
            if let Some(v) = levels.frame(id, &params) {
                send(sock, &Message::new(name, vec![Arg::Blob(meters::blob(&v))]), dest);
            }
        }
    }

    /// Handles one datagram; replies through `sock`.
    pub fn handle(&self, sock: &UdpSocket, data: &[u8], from: SocketAddr) {
        let Some(msg) = osc::decode(data) else { return };
        self.messages.fetch_add(1, Ordering::Relaxed);
        let reply = |m: Message| send(sock, &m, from);
        let s = |t: &str| Arg::Str(t.to_owned());
        let tree = Tree::get();
        match msg.addr.as_str() {
            "/info" => {
                let id = lock(&self.identity).clone();
                reply(Message::new("/info", vec![s("V2.07"), s("osc-server"), s(&id.model), s(&id.firmware)]));
            }
            "/xinfo" => {
                let id = lock(&self.identity).clone();
                let ip = local_ip_for(from).to_string();
                reply(Message::new("/xinfo", vec![s(&ip), s(&id.name), s(&id.model), s(&id.firmware)]));
            }
            "/status" => {
                let id = lock(&self.identity).clone();
                reply(Message::new("/status", vec![s("active"), s(&local_ip_for(from).to_string()), s(&id.name)]));
            }
            "/xremote" | "/xremotenfb" => self.subscribe(from),
            "/unsubscribe" => self.unsubscribe(from),
            "/renew" => self.renew(from, &msg.args),
            "/meters" => self.subscribe_meters(from, &msg.args),
            "/node" => {
                let Some(Arg::Str(path)) = msg.args.first() else { return };
                let Some(found) = tree.find(path) else { return };
                if let Some(line) = tree.node_line(&self.console(), found) {
                    reply(Message::new("node", vec![Arg::Str(line + "\n")]));
                }
            }
            "/" => {
                let Some(Arg::Str(text)) = msg.args.first() else { return };
                let f = crate::value::fields(text);
                let Some(found) = f.first().and_then(|p| tree.find(p)) else { return };
                // Values in the node's order; fewer than all set the first ones.
                let sets: Vec<(usize, Val)> = tree
                    .group(found)
                    .into_iter()
                    .zip(&f[1..])
                    .filter_map(|(i, t)| Some((i, tree.entries[i].ty.parse(t)?)))
                    .collect();
                self.apply(Some(sock), sets, Some(from));
                reply(msg);
            }
            addr => {
                let Some(found) = tree.find(addr) else { return };
                let group = tree.group(found);
                if msg.args.is_empty() {
                    // A get; a node's address answers its first value (as an X32 does).
                    let Some(&i) = group.first() else { return };
                    let e = &tree.entries[i];
                    reply(Message::new(e.path.clone(), vec![e.ty.to_arg(&self.console().get(i))]));
                    return;
                }
                let sets: Vec<(usize, Val)> = match found {
                    Found::Leaf(i) => tree.entries[i].ty.from_arg(&msg.args[0]).map(|v| (i, v)).into_iter().collect(),
                    // A node takes ints and floats only (strings to a node mean
                    // something else to an X32).
                    Found::Node(_) if msg.args.iter().all(|a| matches!(a, Arg::Int(_) | Arg::Float(_))) => {
                        group.into_iter().zip(&msg.args).filter_map(|(i, a)| Some((i, tree.entries[i].ty.from_arg(a)?))).collect()
                    }
                    Found::Node(_) => Vec::new(),
                };
                self.apply(Some(sock), sets, Some(from));
            }
        }
    }
}

/// The running server. Stops when dropped.
pub struct X32Server {
    shared: Arc<Shared>,
    threads: Vec<JoinHandle<()>>,
    status: Status,
}

impl X32Server {
    /// Starts the server; a port that can't be bound is reported in
    /// [`Status::errors`].
    pub fn start(engine: Arc<dyn EngineHandle>, config: &Config) -> X32Server {
        let shared = Arc::new(Shared::new(engine, config));
        let mut status = Status::default();
        let mut threads = Vec::new();
        match bind_udp(SocketAddr::new(config.bind, config.port)) {
            Ok(sock) => {
                status.osc = sock.local_addr().ok();
                match sock.try_clone() {
                    Ok(tx) => {
                        let sh = shared.clone();
                        threads.push(spawn("x32-tick", move || tick_loop(&sh, tx)));
                    }
                    Err(e) => status.errors.push(format!("X32 OSC (UDP {}): {e}", config.port)),
                }
                let sh = shared.clone();
                threads.push(spawn("x32-osc", move || osc_loop(&sh, sock)));
            }
            Err(e) => status.errors.push(format!("X32 OSC (UDP {}): {e}", config.port)),
        }
        X32Server { shared, threads, status }
    }

    pub fn shared(&self) -> &Arc<Shared> {
        &self.shared
    }

    pub fn status(&self) -> Status {
        let mut s = self.status.clone();
        s.xremote = self.shared.xremote_clients();
        s.messages = self.shared.messages.load(Ordering::Relaxed);
        s
    }

    pub fn stop(&mut self) {
        self.shared.stop.store(true, Ordering::Relaxed);
        for t in self.threads.drain(..) {
            let _ = t.join();
        }
    }
}

impl Drop for X32Server {
    fn drop(&mut self) {
        self.stop();
    }
}

fn spawn(name: &str, f: impl FnOnce() + Send + 'static) -> JoinHandle<()> {
    std::thread::Builder::new()
        .name(name.into())
        .spawn(move || {
            // A bug must not take the app down.
            let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(f));
        })
        .expect("spawning a thread")
}

/// Errors a UDP server reads past: timeouts, and the "port unreachable" a
/// client that went away leaves behind (Windows reports it on the next receive).
fn quiet(e: &std::io::Error) -> bool {
    matches!(
        e.kind(),
        ErrorKind::WouldBlock
            | ErrorKind::TimedOut
            | ErrorKind::Interrupted
            | ErrorKind::ConnectionReset
            | ErrorKind::ConnectionRefused
            | ErrorKind::ConnectionAborted
    )
}

fn osc_loop(sh: &Shared, sock: UdpSocket) {
    let _ = sock.set_read_timeout(Some(Duration::from_millis(100)));
    let mut buf = vec![0u8; 65536];
    while !sh.stop.load(Ordering::Relaxed) {
        match sock.recv_from(&mut buf) {
            Ok((n, from)) => sh.handle(&sock, &buf[..n], from),
            Err(e) if quiet(&e) => {}
            Err(_) => std::thread::sleep(Duration::from_millis(5)),
        }
    }
}

/// Meters every tick, changes every few.
fn tick_loop(sh: &Shared, sock: UdpSocket) {
    let mut tick = 0u64;
    let start = Instant::now();
    while !sh.stop.load(Ordering::Relaxed) {
        sh.send_meters(&sock, tick);
        if tick % POLL_TICKS == 0 {
            sh.poll_changes(&sock);
        }
        tick += 1;
        // On the clock, not after the work: a slow tick doesn't slow the meters down.
        let next = start + TICK * tick as u32;
        if let Some(wait) = next.checked_duration_since(Instant::now()) {
            std::thread::sleep(wait);
        }
    }
}
