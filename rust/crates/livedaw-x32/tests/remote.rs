//! The X32 OSC server over a real socket (a free port on localhost), replaying
//! the traffic of P.-G. Maillot's protocol document and the replies of a real
//! X32 (firmware 4.09) to read-only requests.

use livedaw_engine::{ConsoleConfig, Counts, Engine, Profile, StripKind};
use livedaw_wing::osc::{self, Arg, Message};
use livedaw_wing::server::EngineHandle;
use livedaw_x32::{Config, Identity, X32Server};
use std::net::{IpAddr, Ipv4Addr, SocketAddr, UdpSocket};
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;
use std::time::Duration;

fn start_with(life: Duration) -> (Arc<Engine>, X32Server) {
    let engine = Arc::new(Engine::new(0));
    engine.configure(&ConsoleConfig { profile: Profile::X32, counts: Counts::X32 });
    assert_eq!(engine.profile(), Profile::X32);
    let config = Config {
        identity: Identity { name: "TEST-X32".into(), model: "X32C".into(), firmware: "4.09".into() },
        bind: IpAddr::V4(Ipv4Addr::LOCALHOST),
        port: 0,
        xremote_life: life,
        meter_life: life,
    };
    let handle: Arc<dyn EngineHandle> = Arc::new(engine.clone());
    let server = X32Server::start(handle, &config);
    assert!(server.status().errors.is_empty(), "{:?}", server.status().errors);
    (engine, server)
}

fn start() -> (Arc<Engine>, X32Server) {
    start_with(Duration::from_secs(10))
}

struct Client {
    sock: UdpSocket,
    to: SocketAddr,
}

impl Client {
    fn new(server: &X32Server) -> Self {
        let sock = UdpSocket::bind("127.0.0.1:0").unwrap();
        sock.set_read_timeout(Some(Duration::from_millis(500))).unwrap();
        Self { sock, to: server.status().osc.unwrap() }
    }

    fn send(&self, addr: &str, args: Vec<Arg>) {
        self.sock.send_to(&osc::encode(&Message::new(addr, args)), self.to).unwrap();
    }

    /// Sends and waits until the server has handled it (it answers in order),
    /// so that the engine can be checked.
    fn set(&self, addr: &str, args: Vec<Arg>) {
        self.send(addr, args);
        self.send("/status", vec![]);
        while self.recv().expect("the server answers").addr != "/status" {}
    }

    /// Sends raw bytes (the document's hex dumps).
    fn send_raw(&self, bytes: &[u8]) {
        self.sock.send_to(bytes, self.to).unwrap();
    }

    fn recv(&self) -> Option<Message> {
        let mut buf = [0u8; 65536];
        let (n, _) = self.sock.recv_from(&mut buf).ok()?;
        osc::decode(&buf[..n])
    }

    fn quiet(&self, wait: Duration) -> Option<Message> {
        self.sock.set_read_timeout(Some(wait)).unwrap();
        let m = self.recv();
        self.sock.set_read_timeout(Some(Duration::from_millis(500))).unwrap();
        m
    }

    fn get(&self, addr: &str) -> Message {
        self.send(addr, vec![]);
        self.recv().unwrap_or_else(|| panic!("no reply for {addr}"))
    }

    fn get_f(&self, addr: &str) -> f32 {
        match self.get(addr).args[..] {
            [Arg::Float(f)] => f,
            ref a => panic!("{addr}: {a:?}"),
        }
    }

    fn get_i(&self, addr: &str) -> i32 {
        match self.get(addr).args[..] {
            [Arg::Int(i)] => i,
            ref a => panic!("{addr}: {a:?}"),
        }
    }

    /// The `/node` reply: "node~~~~,s~~<text>" (its address has no `/`, so
    /// it is read here by hand).
    fn node(&self, path: &str) -> String {
        self.send("/node", vec![Arg::Str(path.into())]);
        let mut buf = [0u8; 65536];
        let (n, _) = self.sock.recv_from(&mut buf).unwrap_or_else(|_| panic!("no node for {path}"));
        assert_eq!(&buf[..12], b"node\0\0\0\0,s\0\0", "{path}");
        let text = &buf[12..n];
        let end = text.iter().position(|&c| c == 0).expect("a null-terminated string");
        assert_eq!(n, 12 + (end + 4) / 4 * 4, "padded to 4 bytes");
        String::from_utf8(text[..end].to_vec()).unwrap()
    }

    fn set_text(&self, text: &str) {
        self.send("/", vec![Arg::Str(text.into())]);
        let echo = self.recv().expect("the console echoes a / command");
        assert_eq!((echo.addr.as_str(), &echo.args[..]), ("/", &[Arg::Str(text.into())][..]));
    }
}

fn s(t: &str) -> Arg {
    Arg::Str(t.into())
}

#[test]
fn info_xinfo_and_status() {
    let (_e, server) = start();
    let c = Client::new(&server);
    // "/info~~~,~~~" (an empty type tag string) and the bare address both work.
    c.send_raw(b"/info\0\0\0,\0\0\0");
    let info = c.recv().unwrap();
    assert_eq!((info.addr.as_str(), info.args), ("/info", vec![s("V2.07"), s("osc-server"), s("X32C"), s("4.09")]));
    assert_eq!(c.get("/xinfo").args, vec![s("127.0.0.1"), s("TEST-X32"), s("X32C"), s("4.09")]);
    assert_eq!(c.get("/status").args, vec![s("active"), s("127.0.0.1"), s("TEST-X32")]);
}

#[test]
fn faders_mutes_pans_and_sends_on_the_consoles_steps() {
    let (e, server) = start();
    let c = Client::new(&server);
    let ch = |i: usize| e.channel_at(i).unwrap();

    // The document's +3 dB: "/ch/01/mix/fader~~~~,f~~[0.8250]", as hex.
    let hex = "2f63682f30312f6d69782f6661646572000000002c6600003f5334cd";
    let bytes: Vec<u8> = (0..hex.len()).step_by(2).map(|i| u8::from_str_radix(&hex[i..i + 2], 16).unwrap()).collect();
    c.send_raw(&bytes);
    assert_eq!(c.get_f("/ch/01/mix/fader"), 844.0 / 1023.0, "on one of 1024 steps");
    assert!((ch(0).params.fader_db.load() - 3.0).abs() < 0.03);
    // A fader set to 0 dB answers step 767, as an X32 does (0.7497556).
    e.controls.dcas[0].fader_db.store(0.0);
    assert_eq!(c.get_f("/dca/1/fader"), 767.0 / 1023.0);

    // Mute is inverted: mix/on 1 passes audio.
    assert_eq!(c.get_i("/ch/01/mix/on"), 1);
    c.set("/ch/01/mix/on", vec![Arg::Int(0)]);
    assert_eq!(c.get_i("/ch/01/mix/on"), 0);
    assert!(ch(0).params.mute.load(Relaxed));
    c.set("/ch/01/mix/on", vec![s("ON")]);
    assert!(!ch(0).params.mute.load(Relaxed));

    // Half right: "/ch/02/mix/pan~~,f~~[0.7500]".
    c.set("/ch/02/mix/pan", vec![Arg::Float(0.75)]);
    assert_eq!(ch(1).params.pan.load(), 50.0);
    assert_eq!(c.get_f("/ch/02/mix/pan"), 0.75);

    // Sends: 161 steps (0.5 is -10 dB); LR and M/C are mains 1 and 2.
    c.set("/ch/01/mix/03/level", vec![Arg::Float(0.5)]);
    c.set("/ch/01/mix/03/on", vec![Arg::Int(1)]);
    let first = ch(0);
    let send = first.sends.find(2).unwrap();
    assert_eq!((send.level_db.load(), send.enabled.load(Relaxed)), (-10.0, true));
    c.set("/ch/01/mix/mlevel", vec![Arg::Float(0.3)]);
    assert_eq!(c.get_f("/ch/01/mix/mlevel"), 48.0 / 160.0);
    c.set("/ch/01/mix/mono", vec![Arg::Int(1)]);
    c.set("/ch/01/mix/st", vec![Arg::Int(0)]);
    assert!(ch(0).mains.find(1).unwrap().on.load(Relaxed) && !ch(0).mains.find(0).unwrap().on.load(Relaxed));

    // Every strip kind reaches its engine strip.
    for (addr, kind, index) in [
        ("/auxin/02/mix/fader", StripKind::Aux, 1),
        ("/fxrtn/01/mix/fader", StripKind::Aux, 8),
        ("/bus/03/mix/fader", StripKind::Bus, 2),
        ("/mtx/02/mix/fader", StripKind::Matrix, 1),
        ("/main/st/mix/fader", StripKind::Main, 0),
        ("/main/m/mix/fader", StripKind::Main, 1),
    ] {
        c.set(addr, vec![Arg::Float(0.5)]);
        let strip = e.strip_at(kind, index).unwrap();
        assert!((strip.params.fader_db.load() + 10.0).abs() < 0.05, "{addr}");
    }
    c.set("/bus/03/mix/02/level", vec![Arg::Float(0.75)]);
    assert!((e.strip_at(StripKind::Bus, 2).unwrap().matrices.find(1).unwrap().level_db.load()).abs() < 0.2, "bus to matrix 2");
    c.set("/dca/2/on", vec![Arg::Int(0)]);
    assert!(e.controls.dcas[1].mute.load(Relaxed));

    // Names, colours (X32 0-15 are palette 1-16), DCA and mute group bitmaps.
    c.set("/ch/01/config/name", vec![s("Kick")]);
    assert_eq!((ch(0).name(), c.get("/ch/01/config/name").args), ("Kick".into(), vec![s("Kick")]));
    c.set("/ch/05/config/color", vec![s("BL")]);
    assert_eq!((ch(4).params.colour.load(Relaxed), c.get_i("/ch/05/config/color")), (5, 4));
    c.set("/ch/05/grp/dca", vec![Arg::Int(5)]);
    assert!(ch(4).params.dca_mask.contains(0) && ch(4).params.dca_mask.contains(2) && !ch(4).params.dca_mask.contains(1));
    c.set("/ch/05/grp/mute", vec![Arg::Int(0b100000)]);
    assert!(ch(4).params.mute_group_mask.contains(5));
    c.set("/config/mute/2", vec![Arg::Int(1)]);
    assert!(e.controls.mute_groups.contains(1));
    c.set("/dca/3/config/name", vec![s("Drums")]);
    assert_eq!(e.dca_name(2), "Drums");
}

#[test]
fn enums_by_name_or_number_and_the_x32_models() {
    let (e, server) = start();
    let c = Client::new(&server);
    let p = &e.channel_at(0).unwrap().params;

    // "/ch/01/gate/mode~~~~,s~~GATE~~~~" and ",i~~[3]" are the same.
    c.set("/ch/01/gate/thr", vec![Arg::Float(0.5)]);
    c.set("/ch/01/gate/mode", vec![s("EXP3")]);
    assert_eq!(p.gate().model_spec().unwrap().token, "X32EXP3");
    assert_eq!(p.gate().param("thr").unwrap().load(), -40.0, "a mode keeps the gate's settings");
    c.set("/ch/01/gate/mode", vec![Arg::Int(3)]);
    assert_eq!((p.gate().model_spec().unwrap().token, c.get_i("/ch/01/gate/mode")), ("X32GATE", 3));
    c.set("/ch/01/gate/range", vec![Arg::Float(1.0)]);
    assert_eq!(p.gate().param("range").unwrap().load(), 60.0);

    c.set("/ch/01/dyn/mode", vec![s("EXP")]);
    assert_eq!(p.dynamics().model_spec().unwrap().token, "X32EXP");
    c.set("/ch/01/dyn/ratio", vec![s("3.0")]);
    assert_eq!(p.dynamics().param("ratio").unwrap().load(), 5.0);
    c.set("/ch/01/dyn/knee", vec![Arg::Float(0.2)]);
    assert_eq!((p.dynamics().param("knee").unwrap().load(), c.get_f("/ch/01/dyn/knee")), (1.0, 0.2));
    c.set("/ch/01/dyn/mix", vec![Arg::Float(0.5)]);
    assert_eq!(p.dynamics().mix.load(), 50.0);
    c.set("/ch/01/dyn/det", vec![Arg::Int(1)]);
    assert_eq!(c.get_i("/ch/01/dyn/det"), 1);
    // keysrc takes only an int.
    c.set("/ch/01/dyn/keysrc", vec![s("5")]);
    assert_eq!(c.get_i("/ch/01/dyn/keysrc"), 0);

    // "/ch/01/eq/1 ,ifff [2] [0.2650] [0.5000] [0.4648]": a node set.
    c.set("/ch/01/eq/1", vec![Arg::Int(2), Arg::Float(0.265), Arg::Float(0.5), Arg::Float(0.4648)]);
    let eq = p.eq();
    assert_eq!(eq.param("1t").unwrap().load(), 2.0);
    assert!((eq.param("1f").unwrap().load() - 124.7).abs() < 0.1);
    assert_eq!(eq.param("1g").unwrap().load(), 0.0);
    assert!((eq.param("1q").unwrap().load() - 1.96).abs() < 0.01);
    // The same four as single commands, and the console's float for Q 2.0.
    assert_eq!(c.get_i("/ch/01/eq/1/type"), 2);
    assert_eq!(c.get_f("/ch/01/eq/1/f"), 53.0 / 200.0);
    assert_eq!(c.get_f("/ch/01/eq/1/q"), 33.0 / 71.0);
    // A bus has six bands.
    c.set("/bus/01/eq/6/g", vec![Arg::Float(1.0)]);
    assert_eq!(e.strip_at(StripKind::Bus, 0).unwrap().params.eq().param("6g").unwrap().load(), 15.0);

    // The low cut: the X32's slopes are 12, 18 and 24.
    c.set("/ch/01/preamp/hpslope", vec![s("24")]);
    assert_eq!(p.filter().low_cut_slope.load(Relaxed), 3);
    c.set("/ch/01/preamp/hpf", vec![Arg::Float(1.0)]);
    assert!((p.filter().low_cut_freq.load() - 400.0).abs() < 0.01);
    c.set("/ch/01/preamp/trim", vec![Arg::Float(0.75)]);
    assert_eq!(p.input.trim_db.load(), 9.0);
    // Delay in ms (0.3 to 500 in 0.1 ms steps).
    c.set("/ch/01/delay/time", vec![Arg::Float(1.0)]);
    assert!((c.get_f("/ch/01/delay/time") - 1.0).abs() < 1e-6);
    assert!(c.node("ch/01/delay").ends_with("500.0\n"));
}

#[test]
fn node_text_and_sets_from_text() {
    let (e, server) = start();
    let c = Client::new(&server);
    let ch = |i: usize| e.channel_at(i).unwrap();

    // The document's examples: "/~~~,s~~/ch/01/mix/fader 3", 10, 0, -5, -90, -20.5.
    for (text, db) in [("3", 3.0), ("10", 10.0), ("0", 0.0), ("-5", -5.0), ("-20.5", -20.5)] {
        c.set_text(&format!("/ch/01/mix/fader {text}"));
        assert!((ch(0).params.fader_db.load() - db).abs() < 0.03, "{text}");
    }
    c.set_text("/ch/01/mix/fader -90");
    assert_eq!(c.get_f("/ch/01/mix/fader"), 0.0, "-90 is the bottom");
    c.set_text("/ch/02/mix/pan 50");
    assert_eq!(ch(1).params.pan.load(), 50.0);

    // A node's values in order; fewer than all set the first ones.
    c.set_text("/ch/03/mix OFF -10 OFF -50");
    let p = &ch(2).params;
    assert!(p.mute.load(Relaxed) && !ch(2).mains.find(0).unwrap().on.load(Relaxed));
    // -10 dB is between two of the 1024 steps.
    assert!((p.fader_db.load() + 10.0).abs() < 0.03 && p.pan.load() == -50.0);
    c.set_text("ch/03/mix ON -20.5 ON +0 OFF -oo");
    assert_eq!(c.node("ch/03/mix"), "/ch/03/mix ON -20.5 ON +0 OFF   -oo\n");
    c.set_text("/ch/03/config \"Lead Vox\" 12 RD 7");
    assert_eq!(c.node("/ch/03/config"), "/ch/03/config \"Lead Vox\" 12 RD 7\n");
    assert_eq!(ch(2).name(), "Lead Vox");

    // Lines as an X32 writes them (fw 4.09, its factory values where Sidecar's
    // engine starts the same).
    c.set_text("/ch/01/gate OFF GATE -80.0 60.0 10 50.2 258 0");
    assert_eq!(c.node("ch/01/gate"), "/ch/01/gate OFF GATE -80.0 60.0 10 50.2  258 0\n");
    c.set_text("/ch/01/dyn OFF COMP PEAK LOG 0.0 3.0 1 0.00 10 10.0 151 POST 0 100 OFF");
    assert_eq!(c.node("ch/01/dyn"), "/ch/01/dyn OFF COMP PEAK LOG 0.0 3.0 1 0.00 10 10.0  151 POST 0 100 OFF\n");
    assert_eq!(c.node("ch/01/gate/filter"), "/ch/01/gate/filter OFF 3.0 990.9\n");
    c.set_text("/ch/01/eq/1 PEQ 124.7 +0.00 2.0");
    assert_eq!(c.node("ch/01/eq/1"), "/ch/01/eq/1 PEQ 124.7 +0.00 2.0\n");
    c.set_text("/ch/01/eq/3 PEQ 1k97 -4.25 2.0");
    assert_eq!(c.node("ch/01/eq/3"), "/ch/01/eq/3 PEQ 1k97 -4.25 2.0\n");
    c.set_text("/ch/01/preamp +0.0 OFF OFF 24 20");
    assert_eq!(c.node("ch/01/preamp"), "/ch/01/preamp +0.0 OFF OFF 24  20\n");
    c.set_text("/ch/01/delay OFF 0.3");
    assert_eq!(c.node("ch/01/delay"), "/ch/01/delay OFF   0.3\n");
    assert_eq!(c.node("ch/01/insert"), "/ch/01/insert OFF POST OFF\n");
    c.set_text("/ch/01/mix/01 ON -oo +0 EQ-> 0");
    assert_eq!(c.node("ch/01/mix/01"), "/ch/01/mix/01 ON   -oo +0 EQ-> 0\n");
    c.set_text("/ch/01/mix/02 ON -oo");
    assert_eq!(c.node("ch/01/mix/02"), "/ch/01/mix/02 ON   -oo\n");
    assert_eq!(c.node("ch/01/grp"), "/ch/01/grp %00000000 %000000\n");
    assert_eq!(c.node("ch/01/automix"), "/ch/01/automix OFF  +0.0\n");
    // As the real X32 prints them: a bus lists its key source, a matrix doesn't.
    c.set_text("/bus/01/dyn OFF COMP RMS LOG 0.0 3.0 1 0.00 10 10.0 151 POST 0 100 OFF");
    assert_eq!(c.node("bus/01/dyn"), "/bus/01/dyn OFF COMP RMS LOG 0.0 3.0 1 0.00 10 10.0  151 POST 0 100 OFF\n");
    c.set_text("/mtx/01/dyn OFF COMP RMS LOG 0.0 3.0 1 0.00 10 10.0 151 POST 100 OFF");
    assert_eq!(c.node("mtx/01/dyn"), "/mtx/01/dyn OFF COMP RMS LOG 0.0 3.0 1 0.00 10 10.0  151 POST 100 OFF\n");
    c.set_text("/bus/01/mix/01 ON -oo +0 POST 0");
    assert_eq!(c.node("bus/01/mix/01"), "/bus/01/mix/01 ON   -oo +0 POST 0\n");
    c.set_text("/main/st/mix ON -oo +0");
    assert_eq!(c.node("main/st/mix"), "/main/st/mix ON   -oo +0\n");
    c.set_text("/dca/1 ON 0");
    assert_eq!(c.node("dca/1"), "/dca/1 ON   0.0\n");
    c.set_text("/dca/1/config \"DCA 1\" 70 WH");
    assert_eq!(c.node("dca/1/config"), "/dca/1/config \"DCA 1\" 70 WH\n");
    assert_eq!(e.controls.dcas[0].colour.load(Relaxed), 8);
    assert_eq!(c.node("config/fxlink"), "/config/fxlink ON ON ON ON\n");

    // A leaf has a line too; a node of nodes answers its first; a get of a
    // node answers its first value.
    assert_eq!(c.node("ch/01/mix/fader"), "/ch/01/mix/fader   -oo\n");
    assert_eq!(c.node("ch/03"), "/ch/03/config \"Lead Vox\" 12 RD 7\n");
    let m = c.get("/ch/03/mix");
    assert_eq!((m.addr.as_str(), &m.args[..]), ("/ch/03/mix/on", &[Arg::Int(1)][..]));

    // What doesn't exist gets no reply.
    c.set("/node", vec![s("ch/99/mix")]);
    assert!(c.quiet(Duration::from_millis(200)).is_none());
}

#[test]
fn values_without_an_engine_equivalent_answer_back() {
    let (e, server) = start();
    let c = Client::new(&server);
    let p = &e.channel_at(0).unwrap().params;

    // The insert's FX slot is the engine's; its side and the AUX loops are kept.
    c.set("/ch/01/insert/sel", vec![s("FX3R")]);
    assert_eq!((p.pre_insert().fx.load(Relaxed), c.get_i("/ch/01/insert/sel")), (2, 6));
    c.set("/ch/01/insert/sel", vec![s("AUX2")]);
    assert_eq!((p.pre_insert().fx.load(Relaxed), c.get_i("/ch/01/insert/sel")), (-1, 18));
    // The interface picking slot 5 shows as FX5L.
    p.pre_insert().fx.store(4, Relaxed);
    assert_eq!(c.get_i("/ch/01/insert/sel"), 9);

    for (addr, value, back) in [
        ("/ch/01/insert/pos", s("PRE"), Arg::Int(0)),
        ("/ch/01/dyn/pos", Arg::Int(0), Arg::Int(0)),
        ("/ch/01/mix/01/type", s("PRE"), Arg::Int(3)),
        ("/ch/01/config/icon", Arg::Int(42), Arg::Int(42)),
        ("/ch/01/config/source", Arg::Int(33), Arg::Int(33)),
        ("/config/chlink/1-2", s("ON"), Arg::Int(1)),
        ("/ch/01/gate/filter/f", Arg::Float(0.5), Arg::Float(0.5)),
        ("/bus/01/dyn/keysrc", Arg::Int(3), Arg::Int(3)),
    ] {
        c.set(addr, vec![value]);
        assert_eq!(c.get(addr).args, vec![back], "{addr}");
    }
    // The X32's factory values until set.
    assert_eq!(c.get_i("/ch/07/config/source"), 7);
    assert_eq!(c.get_i("/auxin/07/config/source"), 39);
    assert_eq!(c.get_i("/dca/4/config/icon"), 70);
    assert_eq!(c.get_i("/ch/02/mix/01/type"), 2, "EQ->");
}

#[test]
fn xremote_pushes_changes_to_up_to_four_clients() {
    let (e, server) = start_with(Duration::from_millis(1500));
    let a = Client::new(&server);
    let b = Client::new(&server);
    a.send("/xremote", vec![]);
    b.send("/xremote", vec![]);
    std::thread::sleep(Duration::from_millis(50));
    assert_eq!(server.status().xremote.len(), 2);

    // A client's change reaches the others, not itself, on the console's step
    // (0.5 is step 511).
    b.send("/ch/01/mix/fader", vec![Arg::Float(0.5)]);
    let m = a.recv().expect("pushed");
    assert_eq!((m.addr.as_str(), &m.args[..]), ("/ch/01/mix/fader", &[Arg::Float(511.0 / 1023.0)][..]));
    assert!(b.quiet(Duration::from_millis(300)).is_none(), "no echo to the sender");

    // A change from elsewhere (the interface) is found and pushed to both.
    e.channel_at(3).unwrap().params.mute.store(true, Relaxed);
    for c in [&a, &b] {
        let m = c.recv().expect("pushed");
        assert_eq!((m.addr.as_str(), &m.args[..]), ("/ch/04/mix/on", &[Arg::Int(0)][..]));
    }
    e.channel_at(1).unwrap().set_name("Snare");
    for c in [&a, &b] {
        assert_eq!(c.recv().unwrap().args, vec![s("Snare")]);
    }

    // At most four: a fifth takes the place of the one closest to running out.
    let more: Vec<Client> = (0..3).map(|_| Client::new(&server)).collect();
    for c in &more {
        c.send("/xremote", vec![]);
        std::thread::sleep(Duration::from_millis(20));
    }
    std::thread::sleep(Duration::from_millis(50));
    let subs = server.status().xremote;
    assert_eq!(subs.len(), 4);
    assert!(!subs.contains(&a.sock.local_addr().unwrap()), "the oldest made way");

    // Without a renewal it ends.
    std::thread::sleep(Duration::from_millis(1800));
    assert!(server.status().xremote.is_empty());
    e.channel_at(3).unwrap().params.mute.store(false, Relaxed);
    assert!(b.quiet(Duration::from_millis(300)).is_none());
}

#[test]
fn meters() {
    let (_e, server) = start_with(Duration::from_millis(400));
    // "/meters~,si~/meters/6~~~16": channel 17's four strip meters.
    let c = Client::new(&server);
    c.send_raw(b"/meters\0,si\0/meters/6\0\0\0\0\0\0\x10");
    let m = c.recv().expect("meters");
    assert_eq!(m.addr, "/meters/6");
    let [Arg::Blob(b)] = &m.args[..] else { panic!("{m:?}") };
    assert_eq!(b.len(), 4 + 4 * 4);
    assert_eq!(i32::from_le_bytes(b[..4].try_into().unwrap()), 4, "the count is little-endian");
    // No gain reduction is a gain of 1.
    assert_eq!(f32::from_le_bytes(b[8..12].try_into().unwrap()), 1.0);
    // They keep coming (every 50 ms), then stop without a renewal.
    assert!(c.recv().is_some());
    std::thread::sleep(Duration::from_millis(600));
    while c.quiet(Duration::from_millis(10)).is_some() {}
    assert!(c.quiet(Duration::from_millis(200)).is_none());

    for (name, n) in [("/meters/0", 70), ("meters/1", 96), ("/meters/2", 49), ("/meters/13", 48)] {
        let c = Client::new(&server);
        c.send("/meters", vec![s(name), Arg::Int(0), Arg::Int(0), Arg::Int(1)]);
        let m = c.recv().unwrap_or_else(|| panic!("{name}"));
        let [Arg::Blob(b)] = &m.args[..] else { panic!("{m:?}") };
        assert_eq!(
            (m.addr.as_str(), i32::from_le_bytes(b[..4].try_into().unwrap())),
            (format!("/{}", name.trim_start_matches('/')).as_str(), n)
        );
        assert_eq!(b.len(), 4 + 4 * n as usize);
        c.send("/unsubscribe", vec![]);
    }
    // A time factor of 10: every 500 ms.
    let c = Client::new(&server);
    c.send("/meters", vec![s("/meters/6"), Arg::Int(70), Arg::Int(10)]);
    assert!(c.recv().is_some());
    assert!(c.quiet(Duration::from_millis(300)).is_none());
    // Requests not served get nothing.
    let c = Client::new(&server);
    c.send("/meters", vec![s("/meters/3")]);
    assert!(c.quiet(Duration::from_millis(200)).is_none());
}

#[test]
fn bad_requests_are_ignored() {
    let (e, server) = start();
    let c = Client::new(&server);
    for (addr, args) in [
        ("/ch/33/mix/fader", vec![]),
        ("/ch/1/mix/fader", vec![]),
        ("/ch/01/mix/02/pan", vec![]),
        ("/fxrtn/01/preamp/trim", vec![]),
        ("/ch/01/mix/fader", vec![Arg::Int(1)]),
        ("/ch/01/gate/mode", vec![Arg::Int(9)]),
        ("/ch/01/config", vec![s("x"), Arg::Int(1), Arg::Int(3), Arg::Int(1)]),
        ("/node", vec![]),
        ("/", vec![s("/ch/99/mix ON")]),
    ] {
        c.send(addr, args);
        assert!(c.quiet(Duration::from_millis(100)).is_none(), "{addr}");
    }
    let p = &e.channel_at(0).unwrap().params;
    assert_eq!(p.gate().model_spec().unwrap().token, "X32GATE");
    assert_eq!(e.channel_at(0).unwrap().name(), "");
    // Garbage doesn't stop the server.
    c.send_raw(b"not osc");
    c.send_raw(b"/ch/01/mix/fader\0\0\0\0,f\0\0\x3f");
    assert_eq!(c.get("/info").addr, "/info");
}

#[test]
fn send_types_and_positions_reach_the_audio() {
    let (engine, server) = start();
    let c = Client::new(&server);
    let ch = engine.strip_at(StripKind::Channel, 0).unwrap();
    let send = |k: usize| ch.sends.get_or_create(k).unwrap();
    // The factory type is EQ->: with GEDI (the compressor POST the EQ) the tap
    // after the EQ, the second slot's (3).
    assert_eq!(c.get_i("/ch/01/mix/01/type"), 2, "EQ->, as an X32 starts");
    assert_eq!(send(0).tap.load(Relaxed), 3);
    c.set("/ch/01/mix/01/type", vec![Arg::Int(2)]);
    assert_eq!((send(0).tap.load(Relaxed), send(1).tap.load(Relaxed)), (3, 3), "both sends of the pair");
    assert_eq!(c.get_i("/ch/01/mix/01/type"), 2);
    c.set("/ch/01/mix/01/type", vec![Arg::Int(0)]);
    assert_eq!(send(0).tap.load(Relaxed), 1, "IN/LC: after the low cut");
    c.set("/ch/01/mix/03/type", vec![Arg::Int(1)]);
    assert_eq!(send(2).tap.load(Relaxed), 2, "<-EQ: after the gate");
    c.set("/ch/01/mix/05/type", vec![Arg::Int(4)]);
    assert_eq!((send(4).mode.load(Relaxed), send(4).tap.load(Relaxed)), (1, -1), "POST");
    // The compressor PRE the EQ (GDEI): <-EQ follows to after the compressor.
    c.set("/ch/01/dyn/pos", vec![Arg::Int(0)]);
    assert_eq!(ch.params.process_order.load(Relaxed), 1);
    assert_eq!(c.get_i("/ch/01/dyn/pos"), 0);
    assert_eq!(send(2).tap.load(Relaxed), 3, "<-EQ is now after the second slot");
    assert_eq!(c.get_i("/ch/01/mix/03/type"), 1);
    // The insert PRE as well (GIDE).
    c.set("/ch/01/insert/pos", vec![Arg::Int(0)]);
    assert_eq!(ch.params.process_order.load(Relaxed), 3);
    assert_eq!(c.get_i("/ch/01/insert/pos"), 0);
}

#[test]
fn linked_pairs_share_what_the_link_config_says() {
    let (engine, server) = start();
    let c = Client::new(&server);
    let ch = |i: usize| engine.strip_at(StripKind::Channel, i).unwrap();
    // Unlinked: a set stays on its channel.
    c.set("/ch/01/mix/fader", vec![Arg::Float(0.75)]);
    assert!(ch(1).params.fader_db.load() < -100.0);
    // Linked 1-2: the fader, the EQ and the dynamics follow; the pan doesn't.
    c.set("/config/chlink/1-2", vec![Arg::Int(1)]);
    c.set("/ch/02/mix/fader", vec![Arg::Float(0.5)]);
    assert_eq!(ch(0).params.fader_db.load(), ch(1).params.fader_db.load());
    assert!((ch(0).params.fader_db.load() + 10.0).abs() < 0.05, "-10 dB, on the fader's step");
    c.set("/ch/01/eq/2/g", vec![Arg::Float(0.75)]);
    assert_eq!(ch(1).params.eq().param("2g").unwrap().load(), 7.5);
    c.set("/ch/01/dyn/on", vec![Arg::Int(1)]);
    assert!(ch(1).params.dynamics().on.load(Relaxed));
    c.set("/ch/01/mix/pan", vec![Arg::Float(0.0)]);
    assert_eq!((ch(0).params.pan.load(), ch(1).params.pan.load()), (-100.0, 0.0));
    // linkcfg: without EQ sharing, the EQ stays on its channel.
    c.set("/config/linkcfg/eq", vec![Arg::Int(0)]);
    c.set("/ch/02/eq/2/g", vec![Arg::Float(0.5)]);
    assert_eq!((ch(0).params.eq().param("2g").unwrap().load(), ch(1).params.eq().param("2g").unwrap().load()), (7.5, 0.0));
    // Channels 3-4 aren't linked.
    c.set("/ch/03/mix/fader", vec![Arg::Float(0.75)]);
    assert!(ch(3).params.fader_db.load() < -100.0);
}
