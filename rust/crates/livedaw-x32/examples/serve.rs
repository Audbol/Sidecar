//! Runs Sidecar's X32 OSC server over a fresh X32-configured engine, for
//! comparing its replies with a real console's (`tools/x32/compare.py`).
//!
//! cargo run -p livedaw-x32 --example serve -- [--port 10024] [--seconds 600]

use livedaw_engine::{ConsoleConfig, Counts, Engine, Profile};
use livedaw_wing::server::EngineHandle;
use livedaw_x32::{Config, X32Server};
use std::sync::Arc;
use std::time::Duration;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let arg = |name: &str, default: u64| {
        args.iter().position(|a| a == name).and_then(|i| args.get(i + 1)).and_then(|v| v.parse().ok()).unwrap_or(default)
    };
    let (port, seconds) = (arg("--port", 10024), arg("--seconds", 600));
    let engine = Arc::new(Engine::new(0));
    engine.configure(&ConsoleConfig { profile: Profile::X32, counts: Counts::X32 });
    let handle: Arc<dyn EngineHandle> = Arc::new(engine.clone());
    let config = Config { port: port as u16, ..Config::default() };
    let mut server = X32Server::start(handle, &config);
    let status = server.status();
    if !status.errors.is_empty() {
        eprintln!("serve: {:?}", status.errors);
        std::process::exit(1);
    }
    println!("serving X32 OSC on {:?} for {seconds} s", status.osc);
    std::thread::sleep(Duration::from_secs(seconds));
    server.stop();
}
