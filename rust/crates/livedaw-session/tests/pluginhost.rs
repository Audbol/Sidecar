//! A plug-in host console saved and loaded: its size is its channel count.

use livedaw_engine::profile::HostSize;
use livedaw_engine::{ConsoleConfig, Engine, Profile, StripKind};
use livedaw_session::console::{load_console, save_console, NoPlugins};
use livedaw_session::xml;
use std::sync::atomic::Ordering::Relaxed;

#[test]
fn a_plug_in_host_keeps_its_size_and_its_bypass() {
    for size in HostSize::ALL {
        let profile = Profile::PluginHost(size);
        let engine = Engine::new(0);
        engine.configure(&ConsoleConfig { profile, counts: profile.spec().counts });
        engine.strips(StripKind::Channel)[1].params.input.bypass.store(true, Relaxed);
        let saved = xml::parse(&save_console(&engine, &mut NoPlugins).to_document()).unwrap();

        let other = Engine::new(0);
        let warnings = load_console(&other, &saved, &mut NoPlugins, false);
        assert!(warnings.is_empty(), "{warnings:?}");
        assert_eq!(other.profile(), profile);
        assert_eq!(other.strips(StripKind::Channel).len(), size.channels());
        assert!(other.strips(StripKind::Channel)[1].params.input.bypass.load(Relaxed));
        assert!(!other.strips(StripKind::Channel)[0].params.input.bypass.load(Relaxed));
    }
}
