//! A dLive's RackUltra (UFX) strips and Dyn8 engines, and every FX engine's key
//! source, the Global Key and MIDI notes (`livedaw_engine::ufx`).
//!
//! `slot` is an FX slot (0..kMaxFx) or, from `kDyn8FxBase`, a Dyn8 engine: the
//! number an insert's `fx` holds. The other `ld_fx_*` calls that take a slot and
//! read its rack (meter, readouts, load, state 1-3, clear flags, users) take a Dyn8
//! engine's number too; `ld_fx_set_model`, `ld_fx_set` and `ld_fx_replace` refuse it.

use crate::engine;
use livedaw_dsp::dlive_fx::ultra::strip::SECTIONS;
use livedaw_engine::atomic::AtomicF32;
use livedaw_engine::keytap::KeySource;
use livedaw_engine::params::FxParams;
use livedaw_engine::Engine;
use std::sync::atomic::Ordering::Relaxed;

fn slot_of(slot: i32) -> Option<usize> {
    usize::try_from(slot).ok()
}

/// The settings of FX slot `slot`, or of Dyn8 engine `slot - kDyn8FxBase` (the same
/// `FxParams` as `EngineControls::fx`, outside the shared controls; `values` in
/// Dyn8's parameter order, its model fixed). Null for no such engine. Valid while
/// the engine lives.
#[no_mangle]
pub unsafe extern "C" fn ld_fx_params(e: *const Engine, slot: i32) -> *const FxParams {
    let params = engine(e).and_then(|e| e.fx_params(slot_of(slot)?));
    params.map_or(std::ptr::null(), |p| p as *const FxParams)
}

/// Whether the console has RackUltra and Dyn8 engines (a dLive).
#[no_mangle]
pub unsafe extern "C" fn ld_engine_has_ufx(e: *const Engine) -> bool {
    engine(e).is_some_and(|e| e.ufx.live())
}

/// Message thread: the Dyn8 engines some insert names, a bit each (engine 1 = bit 0).
#[no_mangle]
pub unsafe extern "C" fn ld_dyn8_in_use(e: *const Engine) -> u64 {
    engine(e).map_or(0, |e| e.dyn8_in_use())
}

/// An engine's key source: a strip as `DynSlotParams::scSource` holds one (an
/// input channel's index, or kKeyStripBase * kind + index), -1 for none.
#[no_mangle]
pub unsafe extern "C" fn ld_fx_key_source(e: *const Engine, slot: i32) -> i32 {
    let key = engine(e).and_then(|e| e.fx_key(slot_of(slot)?));
    key.map_or(-1, |k| k.get().0.code())
}

/// The tap the key is taken at, as `DynSlotParams::scTap` (a tap point of an
/// input channel, one of kNumOutputKeyTaps on another strip).
#[no_mangle]
pub unsafe extern "C" fn ld_fx_key_tap(e: *const Engine, slot: i32) -> i32 {
    let key = engine(e).and_then(|e| e.fx_key(slot_of(slot)?));
    key.map_or(0, |k| k.tap.load(Relaxed))
}

/// Sets an engine's key source and tap (-1: none). The audio thread reads the tap
/// from the next `ld_engine_collect` on (it gives the source strip its buffer).
#[no_mangle]
pub unsafe extern "C" fn ld_fx_set_key(e: *const Engine, slot: i32, source: i32, tap: i32) -> bool {
    let Some(key) = engine(e).and_then(|e| e.fx_key(slot_of(slot)?)) else { return false };
    key.set(KeySource::from_code(source), tap);
    true
}

/// Whether FX slot `slot` runs its device in a RackUltra strip (a dLive's FX 17-24).
#[no_mangle]
pub unsafe extern "C" fn ld_ufx_is_strip(e: *const Engine, slot: i32) -> bool {
    engine(e).and_then(|e| e.ufx.strip_of(slot_of(slot)?)).is_some()
}

/// The strip's values, kUfxStripParams floats in model table 5's parameter order
/// (null when the slot has no strip). Valid while the engine lives.
#[no_mangle]
pub unsafe extern "C" fn ld_ufx_strip_values(e: *const Engine, slot: i32) -> *const AtomicF32 {
    let strip = engine(e).and_then(|e| e.ufx.strip_of(slot_of(slot)?));
    strip.map_or(std::ptr::null(), |s| s.values.as_ptr())
}

/// Sets value `param` of the strip, kept within its range. False for no such value.
#[no_mangle]
pub unsafe extern "C" fn ld_ufx_strip_set_value(e: *const Engine, slot: i32, param: i32, value: f32) -> bool {
    let strip = engine(e).and_then(|e| e.ufx.strip_of(slot_of(slot)?));
    match (strip, usize::try_from(param)) {
        (Some(s), Ok(i)) => s.set_value(i, value),
        _ => false,
    }
}

/// Every value of the strip back on its default.
#[no_mangle]
pub unsafe extern "C" fn ld_ufx_strip_reset(e: *const Engine, slot: i32) {
    if let Some(s) = engine(e).and_then(|e| e.ufx.strip_of(slot_of(slot)?)) {
        s.reset();
    }
}

/// The strip's gain reduction in dB (<= 0): `which` 0 = its pre dynamics, 1 = its
/// post dynamics. (Its input and output levels are the slot's: `ld_fx_readouts`.)
#[no_mangle]
pub unsafe extern "C" fn ld_ufx_strip_meter(e: *const Engine, slot: i32, which: i32) -> f32 {
    let strip = engine(e).and_then(|e| e.ufx.strip_of(slot_of(slot)?));
    match (strip, usize::try_from(which)) {
        (Some(s), Ok(i)) => s.reduction(i),
        _ => 0.0,
    }
}

/// Where a section of the strip starts in its values: 0 = the pre PEQ (15 values:
/// In, then the 4-band PEQ's 14), 1 = the pre dynamics (9), 2 = the post dynamics
/// (9), 3 = the post PEQ (15), 4 = Width (1). -1 for no such section.
#[no_mangle]
pub extern "C" fn ld_ufx_strip_section(section: i32) -> i32 {
    usize::try_from(section).ok().and_then(|i| SECTIONS.get(i)).map_or(-1, |&at| at as i32)
}

/// The console-wide Global Key: `what` 0 = its key (0 C to 11 B), 1 = its scale
/// (0 Major, 1 Minor, 2 Chromatic).
#[no_mangle]
pub unsafe extern "C" fn ld_engine_global_key(e: *const Engine, what: i32) -> i32 {
    engine(e).map_or(0, |e| if what == 0 { e.ufx.global_key.key() } else { e.ufx.global_key.scale() })
}

/// Sets the Global Key (kept within range). The devices that follow it take it
/// with their next block.
#[no_mangle]
pub unsafe extern "C" fn ld_engine_set_global_key(e: *const Engine, key: i32, scale: i32) {
    if let Some(e) = engine(e) {
        e.ufx.global_key.set(key, scale);
    }
}

/// Any thread: a MIDI note for the FX devices that follow notes (the MIDI
/// Harmoniser, the Vocal Tuner's note snap): `velocity` 0 = note off. Lock-free;
/// the devices hear it with the next block.
#[no_mangle]
pub unsafe extern "C" fn ld_fx_midi_note(e: *const Engine, note: i32, velocity: i32) {
    if let Some(e) = engine(e) {
        if (0..128).contains(&note) {
            e.ufx.notes.push(note as u8, velocity.clamp(0, 127) as u8);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_engine::config::{ConsoleConfig, Counts};
    use livedaw_engine::profile::{DLiveLayout, Profile};
    use livedaw_engine::ufx::{DYN8_FX_BASE, NUM_DYN8, UFX_FIRST, UFX_STRIP_PARAMS};

    #[test]
    fn the_calls_reach_a_dlives_engines_and_no_other_consoles() {
        let engine = Engine::new(0);
        engine.configure(&ConsoleConfig { profile: Profile::AhDLive(DLiveLayout::FOH), counts: Counts::DLIVE });
        let e: *const Engine = &engine;
        unsafe {
            assert!(ld_engine_has_ufx(e));
            // FX slots and Dyn8 engines by one number.
            assert_eq!(ld_fx_params(e, 3), &engine.controls.fx[3] as *const FxParams);
            let last = (DYN8_FX_BASE + NUM_DYN8 - 1) as i32;
            assert!(!ld_fx_params(e, last).is_null() && ld_fx_params(e, last + 1).is_null() && ld_fx_params(e, -1).is_null());
            // A key source.
            assert_eq!(ld_fx_key_source(e, last), -1);
            assert!(ld_fx_set_key(e, last, 0x20000 + 9, 2) && !ld_fx_set_key(e, last + 1, 0, 0));
            assert_eq!((ld_fx_key_source(e, last), ld_fx_key_tap(e, last)), (0x20000 + 9, 2));
            assert!(ld_fx_set_key(e, last, -1, 0));
            assert_eq!(ld_fx_key_source(e, last), -1);
            // The strips of FX 17-24.
            let slot = UFX_FIRST as i32;
            assert!(
                ld_ufx_is_strip(e, slot)
                    && ld_ufx_is_strip(e, slot + 7)
                    && !ld_ufx_is_strip(e, slot - 1)
                    && !ld_ufx_is_strip(e, slot + 8)
            );
            let width = ld_ufx_strip_section(4);
            assert_eq!((ld_ufx_strip_section(0), width, ld_ufx_strip_section(5)), (0, UFX_STRIP_PARAMS as i32 - 1, -1));
            let values = ld_ufx_strip_values(e, slot);
            assert!(!values.is_null() && ld_ufx_strip_values(e, 0).is_null());
            assert_eq!((*values.add(width as usize)).load(), 100.0);
            assert!(ld_ufx_strip_set_value(e, slot, width, 500.0) && !ld_ufx_strip_set_value(e, slot, width + 1, 0.0));
            assert_eq!((*values.add(width as usize)).load(), 200.0, "kept within its range");
            ld_ufx_strip_reset(e, slot);
            assert_eq!((*values.add(width as usize)).load(), 100.0);
            assert_eq!(ld_ufx_strip_meter(e, slot, 0), 0.0);
            // The Global Key and the Dyn8s in use.
            ld_engine_set_global_key(e, 9, 1);
            assert_eq!((ld_engine_global_key(e, 0), ld_engine_global_key(e, 1)), (9, 1));
            assert_eq!(ld_dyn8_in_use(e), 0);
            let ch = engine.channel_at(0).unwrap();
            ch.params.pre_insert().fx.store(DYN8_FX_BASE as i32 + 2, Relaxed);
            assert_eq!(ld_dyn8_in_use(e), 0b100);
            ld_fx_midi_note(e, 60, 100);
            ld_fx_midi_note(e, 200, 100);
        }

        let wing = Engine::new(0);
        let w: *const Engine = &wing;
        unsafe {
            assert!(!ld_engine_has_ufx(w) && !ld_ufx_is_strip(w, UFX_FIRST as i32));
            assert!(ld_ufx_strip_values(w, UFX_FIRST as i32).is_null());
            assert_eq!(ld_dyn8_in_use(w), 0);
            // A key source is any console's.
            assert!(ld_fx_set_key(w, 0, 4, 5));
            assert_eq!(ld_fx_key_source(w, 0), 4);
        }
    }
}
