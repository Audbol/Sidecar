//! The TCP listener: the console's rendezvous port, a thread per connection.
//!
//! Each connection has its own [`stream::Parser`](crate::stream::Parser), so a
//! message that arrives in pieces waits for the rest of it there, and its own
//! [`Protocol`], which remembers what that client has heard: changes from the
//! interface, a scene or another client go out to it within [`POLL`], its own
//! changes don't. A client that goes away or stops reading is dropped; nothing it
//! half sent has done anything. The console's sockets are blocking-free (on Windows
//! a blocked read can't be woken when the server stops).

use crate::dlive;
use crate::sq;
use crate::stream::Parser;
use crate::Event;
use livedaw_engine::{Engine, Profile};
use livedaw_wing::server::EngineHandle;
use std::collections::VecDeque;
use std::io::{ErrorKind, Read, Write};
use std::net::{IpAddr, Ipv4Addr, SocketAddr, TcpListener, TcpStream};
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

/// The MixRack's rendezvous port, and the SQ's.
pub const MIXRACK_PORT: u16 = 51325;
/// The Surface's rendezvous port (a dLive only).
pub const SURFACE_PORT: u16 = 51328;
/// How many clients at once (the documents give no limit).
pub const MAX_CLIENTS: usize = 8;
/// A client's queue of unread bytes before it's dropped as gone.
const MAX_BACKLOG: usize = 1 << 20;
/// Console changes are looked for this often.
pub const POLL: Duration = Duration::from_millis(50);
/// A connection's sleep when nothing's happening.
const IDLE: Duration = Duration::from_millis(2);

/// The console's MIDI setup (its Utility > MIDI page).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Setup {
    /// The console's MIDI channel, from 0 (on a dLive, N: the first of five; at most 11).
    pub channel: u8,
    /// SQ only: the NRPN fader law is Audio Taper.
    pub audio_taper: bool,
}

#[derive(Clone, Debug)]
pub struct Config {
    pub bind: IpAddr,
    /// The MixRack's (or the SQ's) port: None doesn't listen, 0 takes any free port.
    pub mixrack: Option<u16>,
    /// The Surface's port (a dLive's).
    pub surface: Option<u16>,
    pub setup: Setup,
}

impl Default for Config {
    fn default() -> Self {
        Self { bind: IpAddr::V4(Ipv4Addr::UNSPECIFIED), mixrack: Some(MIXRACK_PORT), surface: None, setup: Setup::default() }
    }
}

/// Where it's listening, and what couldn't be done.
#[derive(Clone, Debug, Default)]
pub struct Status {
    pub mixrack: Option<SocketAddr>,
    pub surface: Option<SocketAddr>,
    pub errors: Vec<String>,
    pub clients: usize,
}

/// A console's protocol for one client (the dLive's, the SQ's).
pub trait Protocol: Send {
    /// Handles one whole received MIDI message.
    fn receive(&mut self, engine: &Engine, msg: &[u8]);
    /// Queues the console's changes since the last poll (the first sends nothing).
    fn poll(&mut self, engine: &Engine);
    /// Bytes to send, in order.
    fn take_out(&mut self) -> Option<Vec<u8>>;
    /// What the client asked of the app.
    fn take_event(&mut self) -> Option<Event>;
    /// Scene `n` (from 1) was recalled on the console.
    fn scene_recalled(&mut self, n: u16);
    /// The console's MIDI channel or fader law changed.
    fn set_setup(&mut self, setup: Setup);
}

/// Which console's protocol a connection speaks.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Kind {
    Sq,
    DLive,
}

impl Kind {
    fn of(p: Profile) -> Option<Self> {
        match p {
            Profile::AhSq5 => Some(Kind::Sq),
            Profile::AhDLive(_) => Some(Kind::DLive),
            _ => None,
        }
    }

    fn make(self, surface: bool) -> Box<dyn Protocol> {
        match self {
            Kind::Sq => Box::new(sq::Session::default()),
            Kind::DLive => Box::new(dlive_session(surface)),
        }
    }
}

/// [`dlive::Midi`] as a connection's protocol.
struct DliveSession {
    midi: dlive::Midi,
}

fn dlive_session(surface: bool) -> DliveSession {
    DliveSession { midi: dlive::Midi::new(surface) }
}

impl Protocol for DliveSession {
    fn receive(&mut self, engine: &Engine, msg: &[u8]) {
        self.midi.receive(engine, msg);
    }

    fn poll(&mut self, engine: &Engine) {
        self.midi.poll(engine);
    }

    fn take_out(&mut self) -> Option<Vec<u8>> {
        self.midi.take_out()
    }

    fn take_event(&mut self) -> Option<Event> {
        self.midi.take_event()
    }

    fn scene_recalled(&mut self, n: u16) {
        self.midi.scene_recalled(n);
    }

    fn set_setup(&mut self, setup: Setup) {
        self.midi.set_base(setup.channel);
    }
}

/// A connection, as the others see it.
struct Client {
    id: u64,
    /// Scenes recalled, for its thread to send.
    scenes: Mutex<VecDeque<u16>>,
}

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

struct Shared {
    engine: Arc<dyn EngineHandle>,
    stop: AtomicBool,
    channel: AtomicU8,
    audio_taper: AtomicBool,
    clients: Mutex<Vec<Arc<Client>>>,
    next_id: AtomicU64,
    /// Scene recalls clients asked for, and who asked.
    requests: Mutex<VecDeque<(u16, u64)>>,
    /// The request taken last: its client isn't told of the recall.
    taken: Mutex<Option<(u16, u64)>>,
}

impl Shared {
    fn setup(&self) -> Setup {
        Setup { channel: self.channel.load(Ordering::Relaxed), audio_taper: self.audio_taper.load(Ordering::Relaxed) }
    }

    fn engine(&self) -> &Engine {
        self.engine.engine()
    }
}

pub struct Server {
    shared: Arc<Shared>,
    threads: Vec<JoinHandle<()>>,
    status: Status,
}

impl Server {
    /// Binds the ports and starts listening; [`Status::errors`] says what couldn't be bound.
    pub fn start(engine: Arc<dyn EngineHandle>, config: &Config) -> Self {
        let shared = Arc::new(Shared {
            engine,
            stop: AtomicBool::new(false),
            channel: AtomicU8::new(config.setup.channel),
            audio_taper: AtomicBool::new(config.setup.audio_taper),
            clients: Mutex::default(),
            next_id: AtomicU64::new(1),
            requests: Mutex::default(),
            taken: Mutex::default(),
        });
        let mut status = Status::default();
        let mut threads = Vec::new();
        for (port, surface) in [(config.mixrack, false), (config.surface, true)] {
            let Some(port) = port else { continue };
            match TcpListener::bind((config.bind, port)) {
                Ok(listener) => {
                    let addr = listener.local_addr().ok();
                    if surface {
                        status.surface = addr;
                    } else {
                        status.mixrack = addr;
                    }
                    let sh = shared.clone();
                    threads.push(spawn("ahmidi-listen", move || listen(sh, listener, surface)));
                }
                Err(e) => status.errors.push(format!("TCP {port}: {e}")),
            }
        }
        Self { shared, threads, status }
    }

    pub fn status(&self) -> Status {
        let mut s = self.status.clone();
        s.clients = lock(&self.shared.clients).len();
        s
    }

    pub fn set_setup(&self, setup: Setup) {
        self.shared.channel.store(setup.channel, Ordering::Relaxed);
        self.shared.audio_taper.store(setup.audio_taper, Ordering::Relaxed);
    }

    /// The next scene (from 1) a client asked to recall, if any. The app recalls
    /// it and says so with [`scene_recalled`](Self::scene_recalled), and the client
    /// that asked isn't told of it.
    pub fn take_scene(&self) -> Option<u16> {
        let (n, id) = lock(&self.shared.requests).pop_front()?;
        *lock(&self.shared.taken) = Some((n, id));
        Some(n)
    }

    /// Scene `n` was recalled on the console (from a client too): its clients hear.
    pub fn scene_recalled(&self, n: u16) {
        let taken = lock(&self.shared.taken).take();
        let except = taken.filter(|(s, _)| *s == n).map(|(_, id)| id);
        for c in lock(&self.shared.clients).iter() {
            if Some(c.id) != except {
                lock(&c.scenes).push_back(n);
            }
        }
    }

    pub fn stop(&mut self) {
        self.shared.stop.store(true, Ordering::Relaxed);
        for t in self.threads.drain(..) {
            let _ = t.join();
        }
        lock(&self.shared.clients).clear();
    }
}

impl Drop for Server {
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

fn listen(sh: Arc<Shared>, listener: TcpListener, surface: bool) {
    let _ = listener.set_nonblocking(true);
    let mut sessions: Vec<JoinHandle<()>> = Vec::new();
    while !sh.stop.load(Ordering::Relaxed) {
        match listener.accept() {
            Ok((stream, _peer)) => {
                let Some(kind) = Kind::of(sh.engine().profile()) else { continue }; // dropped: not this console
                let client = {
                    let mut clients = lock(&sh.clients);
                    if clients.len() >= MAX_CLIENTS {
                        continue; // dropped
                    }
                    let c = Arc::new(Client { id: sh.next_id.fetch_add(1, Ordering::Relaxed), scenes: Mutex::default() });
                    clients.push(c.clone());
                    c
                };
                let sh2 = sh.clone();
                sessions.push(spawn("ahmidi-client", move || {
                    let id = client.id;
                    session(&sh2, stream, surface, kind, client);
                    lock(&sh2.clients).retain(|c| c.id != id);
                }));
            }
            Err(e) if e.kind() == ErrorKind::WouldBlock => std::thread::sleep(Duration::from_millis(20)),
            Err(_) => std::thread::sleep(Duration::from_millis(50)),
        }
        sessions.retain(|t| !t.is_finished());
    }
    for t in sessions {
        let _ = t.join();
    }
}

/// One connection, until it ends or the server stops.
fn session(sh: &Shared, mut stream: TcpStream, surface: bool, kind: Kind, client: Arc<Client>) {
    let engine = sh.engine();
    let mut proto = kind.make(surface);
    proto.set_setup(sh.setup());
    proto.poll(engine); // what the client starts from
    let _ = stream.set_nodelay(true);
    if stream.set_nonblocking(true).is_err() {
        return;
    }
    let mut parser = Parser::default();
    let mut buf = [0u8; 4096];
    let mut pending: Vec<u8> = Vec::new();
    let mut polled = Instant::now();
    loop {
        if sh.stop.load(Ordering::Relaxed) {
            return;
        }
        let mut busy = false;
        match stream.read(&mut buf) {
            Ok(0) => return, // the client closed
            Ok(n) => {
                busy = true;
                parser.feed(&buf[..n], &mut |m| proto.receive(engine, m));
            }
            Err(e) if matches!(e.kind(), ErrorKind::WouldBlock | ErrorKind::Interrupted) => {}
            Err(_) => return, // reset
        }
        while let Some(Event::RecallScene(n)) = proto.take_event() {
            lock(&sh.requests).push_back((n, client.id));
        }
        let scenes: Vec<u16> = lock(&client.scenes).drain(..).collect();
        for n in scenes {
            proto.scene_recalled(n);
        }
        if polled.elapsed() >= POLL {
            polled = Instant::now();
            if Kind::of(engine.profile()) != Some(kind) {
                return; // the console changed: the client starts again with the new one
            }
            proto.set_setup(sh.setup());
            proto.poll(engine);
        }
        while let Some(m) = proto.take_out() {
            pending.extend(m);
        }
        if !pending.is_empty() {
            match stream.write(&pending) {
                Ok(0) => return,
                Ok(n) => {
                    busy = true;
                    pending.drain(..n);
                }
                Err(e) if matches!(e.kind(), ErrorKind::WouldBlock | ErrorKind::Interrupted) => {}
                Err(_) => return,
            }
            if pending.len() > MAX_BACKLOG {
                return; // it stopped reading
            }
        }
        if !busy {
            std::thread::sleep(IDLE);
        }
    }
}
