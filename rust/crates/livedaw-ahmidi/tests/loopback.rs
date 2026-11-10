//! Real TCP connections to the server on the loopback address: partial messages,
//! running status, replies, state feedback, a client that drops away.

use livedaw_ahmidi::dlive::{Chan, Msg};
use livedaw_ahmidi::stream::Parser;
use livedaw_ahmidi::{Config, Server, Setup};
use livedaw_engine::profile::DLiveLayout;
use livedaw_engine::{ConsoleConfig, Counts, Engine, Profile, StripKind};
use livedaw_wing::server::EngineHandle;
use std::io::{Read, Write};
use std::net::{Ipv4Addr, SocketAddr, TcpStream};
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;
use std::time::{Duration, Instant};

const WAIT: Duration = Duration::from_secs(5);
const ROUND: Duration = Duration::from_millis(150);

fn dlive() -> Arc<Engine> {
    let e = Arc::new(Engine::new(0));
    e.configure(&ConsoleConfig { profile: Profile::AhDLive(DLiveLayout::FOH), counts: Counts::DLIVE });
    e
}

fn sq5() -> Arc<Engine> {
    let e = Arc::new(Engine::new(0));
    e.configure(&ConsoleConfig { profile: Profile::AhSq5, counts: Counts::SQ5 });
    e
}

fn start(engine: &Arc<Engine>) -> Server {
    let config = Config { bind: Ipv4Addr::LOCALHOST.into(), mixrack: Some(0), surface: Some(0), setup: Setup::default() };
    let handle: Arc<dyn EngineHandle> = Arc::new(engine.clone());
    let s = Server::start(handle, &config);
    assert!(s.status().errors.is_empty(), "{:?}", s.status().errors);
    s
}

fn mixrack(s: &Server) -> SocketAddr {
    s.status().mixrack.unwrap()
}

fn surface(s: &Server) -> SocketAddr {
    s.status().surface.unwrap()
}

/// A client: writes bytes, splits what comes back into MIDI messages.
struct Client {
    stream: TcpStream,
    parser: Parser,
    got: Vec<Vec<u8>>,
    raw: Vec<u8>,
}

impl Client {
    fn connect(to: SocketAddr) -> Self {
        let stream = TcpStream::connect(to).unwrap();
        stream.set_nodelay(true).unwrap();
        stream.set_read_timeout(Some(Duration::from_millis(20))).unwrap();
        Self { stream, parser: Parser::default(), got: Vec::new(), raw: Vec::new() }
    }

    fn send(&mut self, bytes: &[u8]) {
        self.stream.write_all(bytes).unwrap();
    }

    fn pump(&mut self) -> bool {
        let mut buf = [0u8; 8192];
        match self.stream.read(&mut buf) {
            Ok(0) => false,
            Ok(n) => {
                self.raw.extend_from_slice(&buf[..n]);
                let got = &mut self.got;
                self.parser.feed(&buf[..n], &mut |m| got.push(m.to_vec()));
                true
            }
            Err(_) => true,
        }
    }

    /// Waits for `n` messages; what arrived.
    fn wait_for(&mut self, n: usize) -> &Vec<Vec<u8>> {
        let end = Instant::now() + WAIT;
        while self.got.len() < n && Instant::now() < end {
            self.pump();
        }
        assert!(self.got.len() >= n, "got {:?}", self.got);
        &self.got
    }

    /// Everything that arrives in a short while.
    fn settle(&mut self) -> Vec<Vec<u8>> {
        let end = Instant::now() + ROUND;
        while Instant::now() < end {
            self.pump();
        }
        std::mem::take(&mut self.got)
    }
}

fn until(cond: impl Fn() -> bool) -> bool {
    let end = Instant::now() + WAIT;
    while Instant::now() < end {
        if cond() {
            return true;
        }
        std::thread::sleep(Duration::from_millis(5));
    }
    false
}

#[test]
fn listens_on_the_documented_ports_by_default() {
    assert_eq!(livedaw_ahmidi::MIXRACK_PORT, 51325);
    assert_eq!(livedaw_ahmidi::SURFACE_PORT, 51328);
}

#[test]
fn dlive_mutes_with_running_status_in_pieces() {
    let e = dlive();
    let s = start(&e);
    let mut c = Client::connect(mixrack(&s));
    // As the document's example: Inputs 1 to 3 on MIDI channel 1, a byte at a time.
    for b in [0x90, 0x00, 0x7F, 0x01, 0x7F, 0x02, 0x7F] {
        c.send(&[b]);
        std::thread::sleep(Duration::from_millis(2));
    }
    let strips = e.strips(StripKind::Channel);
    assert!(until(|| strips[2].params.mute.load(Relaxed)));
    assert!(strips[0].params.mute.load(Relaxed) && strips[1].params.mute.load(Relaxed));
}

#[test]
fn a_half_sent_message_does_nothing_until_it_ends() {
    let e = dlive();
    let s = start(&e);
    let mut c = Client::connect(mixrack(&s));
    let strip = e.strips(StripKind::Channel)[0].clone();
    let before = strip.params.fader_db.load();
    // NRPN: the channel and parameter, but the value is coming later.
    c.send(&[0xB0, 0x63, 0x00, 0x62, 0x17]);
    std::thread::sleep(ROUND);
    assert_eq!(strip.params.fader_db.load(), before);
    c.send(&[0x06, 0x43]);
    assert!(until(|| strip.params.fader_db.load() == -20.0));
    // A SysEx split across writes.
    let name = Msg::SetName { chan: Chan::Input(0), name: "Lead".into() }.encode(0);
    for part in name.chunks(3) {
        c.send(part);
        std::thread::sleep(Duration::from_millis(5));
    }
    assert!(until(|| strip.name() == "Lead"));
}

#[test]
fn a_dropped_client_leaves_nothing_half_done_and_the_next_one_works() {
    let e = dlive();
    let s = start(&e);
    let strip = e.strips(StripKind::Channel)[0].clone();
    strip.params.mute.store(false, Relaxed); // the factory mutes the inputs
    {
        let mut c = Client::connect(mixrack(&s));
        assert!(until(|| s.status().clients == 1));
        // Half a mute and half a SysEx, then the connection goes.
        c.send(&[0x90, 0x00]);
        c.send(&Msg::SetName { chan: Chan::Input(0), name: "Lost".into() }.encode(0)[..10]);
        std::thread::sleep(Duration::from_millis(30));
    }
    assert!(until(|| s.status().clients == 0), "the connection went");
    assert!(!strip.params.mute.load(Relaxed) && strip.name() != "Lost");
    let mut c = Client::connect(mixrack(&s));
    c.send(&[0x90, 0x00, 0x7F]);
    assert!(until(|| strip.params.mute.load(Relaxed)));
    drop(c);
    // A connection reset: closed with data unread by the peer.
    let mut d = Client::connect(mixrack(&s));
    d.send(&Msg::GetMute(Chan::Input(0)).encode(0));
    drop(d);
    assert!(until(|| s.status().clients == 0));
}

#[test]
fn replies_are_the_documents_bytes() {
    let e = dlive();
    let s = start(&e);
    let mut c = Client::connect(mixrack(&s));
    let strip = e.strips(StripKind::Channel)[1].clone();
    strip.params.fader_db.store(0.0);
    strip.params.mute.store(true, Relaxed);
    strip.set_name("Git");
    e.sources().get(3).unwrap().gain_db.store(30.0);
    e.sources().get(3).unwrap().phantom.store(true, Relaxed);
    c.send(&Msg::GetFader(Chan::Input(1)).encode(0));
    c.send(&Msg::GetMute(Chan::Input(1)).encode(0));
    c.send(&Msg::GetName(Chan::Input(1)).encode(0));
    c.send(&Msg::GetPreampGain(3).encode(0));
    c.send(&Msg::GetPhantom(3).encode(0));
    c.send(&Msg::GetPreampPad(3).encode(0));
    let got = c.wait_for(8).clone();
    assert_eq!(got[0], vec![0xB0, 0x63, 0x01]);
    assert_eq!(got[1], vec![0xB0, 0x62, 0x17]);
    assert_eq!(got[2], vec![0xB0, 0x06, 0x6B]);
    assert_eq!(got[3], vec![0x90, 0x01, 0x7F]);
    assert_eq!(got[4], vec![0x90, 0x01, 0x00]);
    assert_eq!(got[5], Msg::NameReply { chan: Chan::Input(1), name: "Git".into() }.encode(0));
    assert_eq!(got[6], vec![0xE0, 0x03, 0x3A], "gain +30 dB");
    assert_eq!(got[7], Msg::PhantomReply { socket: 3, on: true }.encode(0));
    c.wait_for(9);
    assert_eq!(c.got[8], Msg::PreampPadReply { socket: 3, on: false }.encode(0));
}

#[test]
fn sends_dcas_and_the_rest_reach_the_engine() {
    let e = dlive();
    let s = start(&e);
    let mut c = Client::connect(mixrack(&s));
    let input = e.strips(StripKind::Channel)[0].clone();
    let aux1 = e.bus_in_slot(DLiveLayout::FOH.aux_at() as i32).unwrap();
    c.send(&Msg::SendLevel { src: Chan::Input(0), dest: Chan::Aux(0), level: 0x6B }.encode(0));
    let slot = aux1.id() as usize;
    assert!(until(|| input.sends.find(slot).is_some_and(|p| p.level_db.load() == 0.0)));
    c.send(&Msg::SendOn { src: Chan::Input(0), dest: Chan::Aux(0), on: false }.encode(0));
    assert!(until(|| input.sends.find(slot).is_some_and(|p| !p.enabled.load(Relaxed))));
    c.send(&Msg::SendLevel { src: Chan::Input(0), dest: Chan::Matrix(1), level: 0x43 }.encode(0));
    assert!(until(|| input.matrix_send(1).unwrap().level_db.load() == -20.0));
    c.send(&[0xB0, 0x63, 0x00, 0x62, 0x40, 0x06, 0x40]); // DCA 1 on
    assert!(until(|| input.params.dca_mask.contains(0)));
    c.send(&[0xB0, 0x63, 0x00, 0x62, 0x18, 0x06, 0x3F]); // off the Main
    assert!(until(|| !input.main_send(0).unwrap().on.load(Relaxed)));
    c.send(&[0xE0, 0x05, 0x50]); // socket 6, +40 dB
    assert!(until(|| e.sources().get(5).unwrap().gain_db.load() == 40.0));
}

#[test]
fn changes_on_the_console_go_to_every_client_but_the_one_that_made_them() {
    let e = dlive();
    let strip = e.strips(StripKind::Channel)[4].clone();
    strip.params.mute.store(false, Relaxed);
    let s = start(&e);
    let mut a = Client::connect(mixrack(&s));
    let mut b = Client::connect(mixrack(&s));
    assert!(until(|| s.status().clients == 2));
    a.settle();
    b.settle();
    // From the console: both clients.
    strip.params.mute.store(true, Relaxed);
    assert_eq!(a.wait_for(2)[..2], [vec![0x90, 0x04, 0x7F], vec![0x90, 0x04, 0x00]]);
    b.wait_for(2);
    a.got.clear();
    b.got.clear();
    // From client A: B hears, A doesn't.
    a.send(&[0x90, 0x04, 0x3F]);
    assert!(until(|| !strip.params.mute.load(Relaxed)));
    assert_eq!(b.wait_for(2)[0], vec![0x90, 0x04, 0x3F]);
    assert_eq!(a.settle(), Vec::<Vec<u8>>::new(), "not echoed");
}

#[test]
fn scenes_go_to_the_app_and_back_out_to_the_others() {
    let e = dlive();
    let s = start(&e);
    let mut a = Client::connect(mixrack(&s));
    let mut b = Client::connect(mixrack(&s));
    assert!(until(|| s.status().clients == 2));
    // Scene 263 is bank 2, program 6.
    a.send(&[0xB0, 0x00, 0x02, 0xC0, 0x06]);
    let n = {
        let end = Instant::now() + WAIT;
        let mut got = None;
        while got.is_none() && Instant::now() < end {
            got = s.take_scene();
            std::thread::sleep(Duration::from_millis(2));
        }
        got
    };
    assert_eq!(n, Some(263));
    s.scene_recalled(263);
    assert_eq!(b.wait_for(2)[..2], [vec![0xB0, 0x00, 0x02], vec![0xC0, 0x06]]);
    b.got.clear();
    assert_eq!(a.settle(), Vec::<Vec<u8>>::new(), "the client that asked");
    // One recalled on the console: both.
    s.scene_recalled(1);
    assert_eq!(a.wait_for(2)[..2], [vec![0xB0, 0x00, 0x00], vec![0xC0, 0x00]]);
    b.wait_for(2);
    // The Surface: a Program Change is a cue, and the show has no cue ids.
    let mut c = Client::connect(surface(&s));
    c.send(&[0xB0, 0x00, 0x00, 0xC0, 0x05]);
    std::thread::sleep(ROUND);
    assert_eq!(s.take_scene(), None);
}

#[test]
fn clients_past_the_limit_are_turned_away() {
    let e = dlive();
    let s = start(&e);
    let keep: Vec<Client> = (0..livedaw_ahmidi::MAX_CLIENTS).map(|_| Client::connect(mixrack(&s))).collect();
    assert!(until(|| s.status().clients == livedaw_ahmidi::MAX_CLIENTS));
    let mut extra = Client::connect(mixrack(&s));
    let end = Instant::now() + WAIT;
    let mut closed = false;
    while Instant::now() < end && !closed {
        closed = !extra.pump();
    }
    assert!(closed, "the ninth connection is closed");
    drop(keep);
    assert!(until(|| s.status().clients == 0));
}

#[test]
fn the_base_channel_moves_the_whole_protocol() {
    let e = dlive();
    let handle: Arc<dyn EngineHandle> = Arc::new(e.clone());
    let config = Config {
        bind: Ipv4Addr::LOCALHOST.into(),
        mixrack: Some(0),
        surface: None,
        setup: Setup { channel: 5, audio_taper: false },
    };
    let s = Server::start(handle, &config);
    let mut c = Client::connect(mixrack(&s));
    let strip = e.strips(StripKind::Channel)[0].clone();
    strip.params.mute.store(false, Relaxed);
    c.send(&[0x90, 0x00, 0x7F]); // MIDI channel 1: not the base
    std::thread::sleep(ROUND);
    assert!(!strip.params.mute.load(Relaxed));
    c.send(&[0x95, 0x00, 0x7F]);
    assert!(until(|| strip.params.mute.load(Relaxed)));
    c.send(&Msg::GetMute(Chan::Input(0)).encode(5));
    assert_eq!(c.wait_for(1)[0][0], 0x95);
}

#[test]
fn sq5_over_tcp_answers_a_get_and_feeds_back() {
    let e = sq5();
    let s = start(&e);
    let mut c = Client::connect(mixrack(&s));
    let strip = e.strips(StripKind::Channel)[0].clone();
    // Ip1 Mute On, with the running status a client may use.
    c.send(&[0xB0, 0x63, 0x00, 0x62, 0x00, 0x06, 0x00, 0x26, 0x01]);
    assert!(until(|| strip.params.mute.load(Relaxed)));
    // Ip1 to LR level: Get (BN 60 7F).
    strip.params.fader_db.store(0.0);
    c.settle();
    c.send(&[0xB0, 0x63, 0x40, 0xB0, 0x62, 0x00, 0xB0, 0x60, 0x7F]);
    let got = c.wait_for(4)[..4].to_vec();
    assert_eq!(got, vec![vec![0xB0, 0x63, 0x40], vec![0xB0, 0x62, 0x00], vec![0xB0, 0x06, 0x76], vec![0xB0, 0x26, 0x5C]]);
    c.got.clear();
    // A console change.
    strip.params.mute.store(false, Relaxed);
    let got = c.wait_for(4)[..4].to_vec();
    assert_eq!(got, vec![vec![0xB0, 0x63, 0x00], vec![0xB0, 0x62, 0x00], vec![0xB0, 0x06, 0x00], vec![0xB0, 0x26, 0x00]]);
    // Scene 96: bank 0, program 5F.
    c.got.clear();
    c.send(&[0xB0, 0x00, 0x00, 0xC0, 0x5F]);
    let n = {
        let end = Instant::now() + WAIT;
        let mut got = None;
        while got.is_none() && Instant::now() < end {
            got = s.take_scene();
            std::thread::sleep(Duration::from_millis(2));
        }
        got
    };
    assert_eq!(n, Some(96));
}

#[test]
fn another_console_refuses_the_connection() {
    let e = Arc::new(Engine::new(0));
    e.configure(&ConsoleConfig::wing());
    let s = start(&e);
    let mut c = Client::connect(mixrack(&s));
    let end = Instant::now() + WAIT;
    let mut closed = false;
    while Instant::now() < end && !closed {
        closed = !c.pump();
    }
    assert!(closed);
    assert_eq!(s.status().clients, 0);
}

#[test]
fn stopping_ends_the_connections() {
    let e = dlive();
    let mut s = start(&e);
    let mut c = Client::connect(mixrack(&s));
    assert!(until(|| s.status().clients == 1));
    s.stop();
    let end = Instant::now() + WAIT;
    let mut closed = false;
    while Instant::now() < end && !closed {
        closed = !c.pump();
    }
    assert!(closed, "the server hung up");
}
