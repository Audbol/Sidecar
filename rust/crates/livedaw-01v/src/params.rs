//! The 01V's edit buffer as bytes: what its SysEx parameter changes address
//! and its scene dumps carry (1014 bytes; addresses 0-1023: Owner's Manual
//! pp.281, 286).
//!
//! The manual gives the size and the address range, not the map: this is
//! Sidecar's layout, so a parameter change or a dump comes back into Sidecar,
//! not into a 01V. The strips, 0-34: CH 1-12, 13/14, 15/16, CH 17-24 (the
//! engine's channels 0-21), RETURN 1-2, BUS 1-4, AUX 1-4, EFFECT 1-2, STEREO.
//!
//! * 0-314: each strip's 9 bytes: 0 the fader step, 1 ON, 2-3 the pans (a mono
//!   strip's or STEREO's balance in 2; a stereo strip's L and R), 4 the
//!   attenuator (+96: 0-96), 5 the switches (bit 0 phase, 1 delay on, 2 EQ on,
//!   3 dynamics on, 4 to ST, 5 HPF, 6 LPF), 6-7 the delay (high 7 bits, low 7
//!   bits, tenths of the engine's delay unit), 8 the routing (BUS 1-4, a bit each).
//! * 315-566: the 4-band EQ of CH 1-16, the returns, AUX 1-4 and STEREO: 12
//!   bytes each (LOW, L-MID, H-MID, HIGH: F, G, Q steps).
//! * 567-614: the 2-band EQ of CH 17-24: 6 bytes each (LOW and HIGH: F, G, Q).
//! * 615-747: the dynamics of CH 1-16, AUX 1-4 and STEREO: 7 bytes each, the
//!   type then its parameters' steps.
//! * 748-875: the sends of CH 1-16 and the returns: 8 bytes each, AUX 1-4
//!   and EFFECT 1-2 as fader steps, then ON and PRE as a bit per send.
//! * 876-923: the sends of CH 17-24, 6 bytes each: AUX 1-2, EFFECT 1-2, ON, PRE.
//! * 924-1005: EFFECT 1-2, 41 bytes each: the type, then up to 20 parameter
//!   steps (high 7 bits, low 7 bits).
//! * 1006-1013: zero.

use livedaw_02r::console::{db_step, eq_step, set_eq_step, step_db, EQ_IDS};
use livedaw_dsp::fader::FaderLaw;
use livedaw_dsp::y02r::DYN_MODELS;
use livedaw_engine::profile::y01v;
use livedaw_engine::sends::send_mode;
use livedaw_engine::{Engine, Profile, SendParams, Strip, StripKind};
use livedaw_promix::console::{pan_of, pan_step, param_step, step_param};
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

/// The edit buffer's bytes (a scene's data, Owner's Manual p.286).
pub const SIZE: usize = 1014;
/// The strips: see the module.
pub const STRIPS: usize = 35;
/// CH 13/14 and 15/16, then the option channels' first strip, and the returns.
pub const STEREO_CH: usize = 12;
pub const OPTION_CH: usize = 14;
pub const RETURNS: usize = 22;
pub const BUSES: usize = 24;
pub const AUXES: usize = 28;
pub const EFFECTS: usize = 32;
/// STEREO's strip.
pub const STEREO: usize = 34;

const COMMON: usize = 0;
const PER_STRIP: usize = 9;
const EQ4: usize = STRIPS * PER_STRIP;
const EQ4_SIZE: usize = 12;
const EQ2: usize = EQ4 + EQ_STRIPS.len() * EQ4_SIZE;
const EQ2_SIZE: usize = 6;
const DYN: usize = EQ2 + (RETURNS - OPTION_CH) * EQ2_SIZE;
const DYN_SIZE: usize = 7;
const SEND: usize = DYN + DYN_STRIPS.len() * DYN_SIZE;
const SEND_SIZE: usize = 8;
const SEND2: usize = SEND + SEND_STRIPS.len() * SEND_SIZE;
const SEND2_SIZE: usize = 6;
const FX: usize = SEND2 + (RETURNS - OPTION_CH) * SEND2_SIZE;
const FX_SIZE: usize = 41;
const FX_END: usize = FX + 2 * FX_SIZE;

/// The strips with a 4-band EQ.
const EQ_STRIPS: [usize; 21] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 22, 23, 28, 29, 30, 31, 34];
/// The strips with a dynamics processor (CH 1-16, AUX 1-4, STEREO: p.60).
const DYN_STRIPS: [usize; 19] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 28, 29, 30, 31, 34];
/// The strips with the full sends (CH 1-16 and the returns).
const SEND_STRIPS: [usize; 16] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 22, 23];

/// The bus slots of the full send block, and of CH 17-24's (p.264).
const SEND_SLOTS: [usize; 6] = [y01v::AUX, y01v::AUX + 1, y01v::AUX + 2, y01v::AUX + 3, y01v::EFF, y01v::EFF + 1];
const OPTION_SLOTS: [usize; 4] = [y01v::AUX, y01v::AUX + 1, y01v::EFF, y01v::EFF + 1];

/// The EFFECT parameters a scene holds (`FX_SIZE`: the type and two bytes each).
pub const FX_VALUES: usize = (FX_SIZE - 1) / 2;

/// What an address is.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Field {
    Common(usize, usize),
    Eq4(usize, usize, usize),
    Eq2(usize, usize, usize),
    Dyn(usize, usize),
    Send(usize, usize),
    Fx(usize, usize),
}

/// The field at address `a`.
fn field(a: usize) -> Option<Field> {
    Some(match a {
        a if a < EQ4 => Field::Common((a - COMMON) / PER_STRIP, (a - COMMON) % PER_STRIP),
        a if a < EQ2 => {
            let (s, k) = ((a - EQ4) / EQ4_SIZE, (a - EQ4) % EQ4_SIZE);
            Field::Eq4(EQ_STRIPS[s], k / 3, k % 3)
        }
        a if a < DYN => {
            let (s, k) = ((a - EQ2) / EQ2_SIZE, (a - EQ2) % EQ2_SIZE);
            // LOW and HIGH of the 4 bands.
            Field::Eq2(OPTION_CH + s, if k < 3 { 0 } else { 3 }, k % 3)
        }
        a if a < SEND => Field::Dyn(DYN_STRIPS[(a - DYN) / DYN_SIZE], (a - DYN) % DYN_SIZE),
        a if a < SEND2 => Field::Send(SEND_STRIPS[(a - SEND) / SEND_SIZE], (a - SEND) % SEND_SIZE),
        a if a < FX => Field::Send(OPTION_CH + (a - SEND2) / SEND2_SIZE, (a - SEND2) % SEND2_SIZE),
        a if a < FX_END => Field::Fx((a - FX) / FX_SIZE, (a - FX) % FX_SIZE),
        _ => return None,
    })
}

/// A strip's engine reference.
pub fn engine_ref(i: usize) -> Option<(StripKind, usize)> {
    Some(match i {
        i if i < RETURNS => (StripKind::Channel, i),
        i if i < BUSES => (StripKind::Aux, i - RETURNS),
        i if i < AUXES => (StripKind::Bus, i - BUSES),
        i if i < EFFECTS => (StripKind::Bus, y01v::AUX + i - AUXES),
        i if i < STEREO => (StripKind::Bus, y01v::EFF + i - EFFECTS),
        STEREO => (StripKind::Main, 0),
        _ => return None,
    })
}

fn strip(c: &Engine, i: usize) -> Option<Arc<Strip>> {
    let (kind, n) = engine_ref(i)?;
    c.strip_at(kind, n)
}

fn law(c: &Engine, i: usize) -> FaderLaw {
    engine_ref(i).map_or(FaderLaw::Y01V, |(kind, _)| c.profile().spec().fader_for(kind))
}

/// Whether strip `i` is a stereo input (13/14, 15/16, the returns: two pans).
pub fn is_stereo_input(i: usize) -> bool {
    matches!(i, STEREO_CH | 13 | RETURNS | 23)
}

fn has_input(i: usize) -> bool {
    i < BUSES
}

/// The send slot of send `n` of strip `i`'s block.
fn send_slot(i: usize, n: usize) -> Option<usize> {
    if (OPTION_CH..RETURNS).contains(&i) {
        OPTION_SLOTS.get(n).copied()
    } else {
        SEND_SLOTS.get(n).copied()
    }
}

/// A send's bytes in its block: the levels, then the ON and PRE bits.
fn send_count(i: usize) -> usize {
    if (OPTION_CH..RETURNS).contains(&i) {
        OPTION_SLOTS.len()
    } else {
        SEND_SLOTS.len()
    }
}

/// The address of strip `i`'s item `item`.
pub fn common(i: usize, item: usize) -> usize {
    COMMON + i * PER_STRIP + item
}

/// The address of byte `n` of a strip's send block (the levels in its own
/// order, then ON and PRE), if it has one.
pub fn send_address(i: usize, n: usize) -> Option<usize> {
    if let Some(p) = SEND_STRIPS.iter().position(|&s| s == i) {
        return (n < SEND_SIZE).then_some(SEND + p * SEND_SIZE + n);
    }
    ((OPTION_CH..RETURNS).contains(&i) && n < SEND2_SIZE).then_some(SEND2 + (i - OPTION_CH) * SEND2_SIZE + n)
}

/// The address of strip `i`'s EQ.
pub fn eq_address(i: usize) -> Option<usize> {
    if let Some(p) = EQ_STRIPS.iter().position(|&s| s == i) {
        return Some(EQ4 + p * EQ4_SIZE);
    }
    (OPTION_CH..RETURNS).contains(&i).then_some(EQ2 + (i - OPTION_CH) * EQ2_SIZE)
}

/// The EQ bytes of strip `i` (12 for a 4-band, 6 for CH 17-24's 2-band).
pub fn eq_len(i: usize) -> usize {
    if (OPTION_CH..RETURNS).contains(&i) {
        EQ2_SIZE
    } else {
        EQ4_SIZE
    }
}

/// The address of strip `i`'s dynamics.
pub fn dyn_address(i: usize) -> Option<usize> {
    DYN_STRIPS.iter().position(|&s| s == i).map(|p| DYN + p * DYN_SIZE)
}

pub const DYN_LEN: usize = DYN_SIZE;

/// The address of EFFECT `slot` (0-1).
pub fn fx_address(slot: usize) -> usize {
    FX + slot * FX_SIZE
}

pub const FX_LEN: usize = FX_SIZE;

/// The byte at address `a`.
pub fn get(c: &Engine, a: usize) -> u8 {
    match field(a) {
        Some(Field::Common(i, item)) => strip(c, i).map_or(0, |s| common_byte(c, i, &s, item)),
        Some(Field::Eq4(i, band, what)) | Some(Field::Eq2(i, band, what)) => {
            strip(c, i).and_then(|s| eq_step(&s, EQ_IDS[band][what])).unwrap_or(0) as u8
        }
        Some(Field::Dyn(i, 0)) => dyn_type(c, i) as u8,
        Some(Field::Dyn(i, k)) => strip(c, i)
            .and_then(|s| {
                let d = s.params.dynamics();
                let spec = d.model_spec()?.params.get(k - 1)?;
                Some(param_step(spec, d.values.get(k - 1)?.load()))
            })
            .unwrap_or(0) as u8,
        Some(Field::Send(i, n)) => send_byte(c, i, n),
        Some(Field::Fx(slot, k)) => fx_byte(c, slot, k),
        None => 0,
    }
}

fn dyn_type(c: &Engine, i: usize) -> usize {
    strip(c, i)
        .and_then(|s| {
            let token = s.params.dynamics().model_spec()?.token;
            DYN_MODELS.iter().position(|m| m.token == token)
        })
        .unwrap_or(0)
}

/// The engine's stereo strip pans as L and R (0-32).
fn stereo_pans(s: &Strip) -> (u16, u16) {
    let (pan, width) = (s.params.pan.load(), s.params.width.load());
    (pan_step(pan - width), pan_step(pan + width))
}

fn common_byte(c: &Engine, i: usize, s: &Strip, item: usize) -> u8 {
    let p = &s.params;
    match item {
        0 => db_step(law(c, i), p.fader_db.load()) as u8,
        1 => !p.mute.load(Relaxed) as u8,
        2 if is_stereo_input(i) => stereo_pans(s).0 as u8,
        3 if is_stereo_input(i) => stereo_pans(s).1 as u8,
        2 if has_input(i) || i == STEREO => pan_step(p.pan.load()) as u8,
        4 if has_input(i) => (p.input.trim_db.load().round() as i32 + 96).clamp(0, 96) as u8,
        5 => {
            let bits = [
                has_input(i) && p.input.invert.load(Relaxed),
                has_input(i) && p.input.delay_on.load(Relaxed),
                eq_address(i).is_some() && p.eq().on.load(Relaxed),
                dyn_address(i).is_some() && p.dynamics().on.load(Relaxed),
                s.main_send(0).is_some_and(|m| m.on.load(Relaxed)),
                eq_address(i).is_some() && eq_step(s, "hpf").unwrap_or(0) != 0,
                eq_address(i).is_some() && eq_step(s, "lpf").unwrap_or(0) != 0,
            ];
            bits.iter().enumerate().fold(0, |b, (k, &on)| b | (on as u8) << k)
        }
        6 | 7 if has_input(i) => {
            let tenths = (p.input.delay.load() * 10.0).round().clamp(0.0, 16383.0) as u16;
            (if item == 6 { tenths >> 7 } else { tenths & 0x7f }) as u8
        }
        8 if has_input(i) => (0..4).fold(0, |b, bus| b | (route_on(s, bus) as u8) << bus),
        _ => 0,
    }
}

fn route_on(s: &Strip, bus: usize) -> bool {
    s.sends.get_or_create(bus).is_some_and(|p| p.enabled.load(Relaxed) && p.level_db.load() > -89.5)
}

fn send_params(s: &Strip, i: usize, n: usize) -> Option<&SendParams> {
    s.sends.get_or_create(send_slot(i, n)?)
}

fn send_byte(c: &Engine, i: usize, n: usize) -> u8 {
    let Some(s) = strip(c, i) else { return 0 };
    let count = send_count(i);
    if n < count {
        return send_params(&s, i, n).map_or(0, |p| db_step(FaderLaw::Y01V, p.level_db.load()) as u8);
    }
    (0..count).fold(0, |b, k| {
        let on = send_params(&s, i, k).is_some_and(|p| {
            if n == count {
                p.enabled.load(Relaxed)
            } else {
                p.mode.load(Relaxed) == send_mode::PRE
            }
        });
        b | (on as u8) << k
    })
}

fn fx_byte(c: &Engine, slot: usize, k: usize) -> u8 {
    let Some(p) = c.controls.fx.get(slot) else { return 0 };
    let models = c.fx_models();
    let model = p.model_index_in(models);
    if k == 0 {
        return model as u8;
    }
    let n = (k - 1) / 2;
    let step = models[model].params.get(n).zip(p.values.get(n)).map_or(0, |(spec, v)| param_step(spec, v.load()));
    (if (k - 1) % 2 == 0 { step >> 7 & 0x7f } else { step & 0x7f }) as u8
}

/// Sets the byte at address `a`.
pub fn set(c: &Engine, a: usize, v: u8) {
    match field(a) {
        Some(Field::Common(i, item)) => {
            if let Some(s) = strip(c, i) {
                set_common(c, i, &s, item, v);
            }
        }
        Some(Field::Eq4(i, band, what)) | Some(Field::Eq2(i, band, what)) => {
            if let Some(s) = strip(c, i) {
                set_eq_step(&s, EQ_IDS[band][what], v as u16);
            }
        }
        Some(Field::Dyn(i, 0)) => set_dyn_type(c, i, v as usize),
        Some(Field::Dyn(i, k)) => {
            if let Some(s) = strip(c, i) {
                let d = s.params.dynamics();
                if let (Some(spec), Some(x)) = (d.model_spec().and_then(|m| m.params.get(k - 1)), d.values.get(k - 1)) {
                    x.store(step_param(spec, v as u16));
                }
            }
        }
        Some(Field::Send(i, n)) => set_send(c, i, n, v),
        Some(Field::Fx(slot, k)) => set_fx(c, slot, k, v),
        None => {}
    }
}

fn set_dyn_type(c: &Engine, i: usize, t: usize) {
    if dyn_type(c, i) == t {
        return;
    }
    let (Some(s), Some(token)) = (strip(c, i), DYN_MODELS.get(t).map(|m| m.token)) else { return };
    let d = s.params.dynamics();
    if let Some(model) = d.table().iter().position(|m| m.token == token) {
        d.set_model(model);
    }
}

fn set_stereo_pan(s: &Strip, side: usize, v: u16) {
    let (mut l, mut r) = stereo_pans(s);
    if side == 0 {
        l = v.min(32);
    } else {
        r = v.min(32);
    }
    let (l, r) = (pan_of(l), pan_of(r));
    s.params.pan.store((l + r) / 2.0);
    s.params.width.store((r - l) / 2.0);
}

fn set_common(c: &Engine, i: usize, s: &Strip, item: usize, v: u8) {
    let p = &s.params;
    let bit = |k: u8| v >> k & 1 != 0;
    match item {
        0 => p.fader_db.store(step_db(law(c, i), v as u16)),
        1 => p.mute.store(v == 0, Relaxed),
        2 if is_stereo_input(i) => set_stereo_pan(s, 0, v as u16),
        3 if is_stereo_input(i) => set_stereo_pan(s, 1, v as u16),
        2 if has_input(i) || i == STEREO => p.pan.store(pan_of(v as u16)),
        4 if has_input(i) => p.input.trim_db.store(v.min(96) as f32 - 96.0),
        5 => {
            if has_input(i) {
                p.input.invert.store(bit(0), Relaxed);
                p.input.delay_on.store(bit(1), Relaxed);
            }
            if eq_address(i).is_some() {
                p.eq().on.store(bit(2), Relaxed);
                set_eq_step(s, "hpf", bit(5) as u16);
                set_eq_step(s, "lpf", bit(6) as u16);
            }
            if dyn_address(i).is_some() {
                p.dynamics().on.store(bit(3), Relaxed);
            }
            if let Some(m) = s.main_send(0) {
                m.on.store(bit(4), Relaxed);
            }
        }
        6 | 7 if has_input(i) => {
            let tenths = (p.input.delay.load() * 10.0).round().clamp(0.0, 16383.0) as u16;
            let tenths = if item == 6 { (v as u16 & 0x7f) << 7 | tenths & 0x7f } else { tenths & !0x7f | v as u16 & 0x7f };
            p.input.delay.store(tenths as f32 / 10.0);
        }
        8 if has_input(i) => {
            for b in 0..4 {
                if let Some(send) = s.sends.get_or_create(b) {
                    send.level_db.store(0.0);
                    send.enabled.store(bit(b as u8), Relaxed);
                }
            }
        }
        _ => {}
    }
}

fn set_send(c: &Engine, i: usize, n: usize, v: u8) {
    let Some(s) = strip(c, i) else { return };
    let count = send_count(i);
    if n < count {
        if let Some(p) = send_params(&s, i, n) {
            p.level_db.store(step_db(FaderLaw::Y01V, v as u16));
        }
        return;
    }
    for k in 0..count {
        let Some(p) = send_params(&s, i, k) else { continue };
        let on = v >> k & 1 != 0;
        if n == count {
            p.enabled.store(on, Relaxed);
        } else {
            p.mode.store(if on { send_mode::PRE } else { send_mode::POST }, Relaxed);
        }
    }
}

fn set_fx(c: &Engine, slot: usize, k: usize, v: u8) {
    let Some(p) = c.controls.fx.get(slot) else { return };
    let models = c.fx_models();
    if k == 0 {
        if p.model_index_in(models) != v as usize {
            c.set_fx_model(slot, v as usize, false);
        }
        return;
    }
    let n = (k - 1) / 2;
    let model = p.model_index_in(models);
    let (Some(spec), Some(x)) = (models[model].params.get(n), p.values.get(n)) else { return };
    let old = param_step(spec, x.load());
    let step = if (k - 1) % 2 == 0 { (v as u16 & 0x7f) << 7 | old & 0x7f } else { old & !0x7f | v as u16 & 0x7f };
    x.store(step_param(spec, step));
}

/// The whole edit buffer (`SIZE` bytes).
pub fn bytes(c: &Engine) -> Vec<u8> {
    (0..SIZE).map(|a| get(c, a)).collect()
}

/// Loads an edit buffer (a scene's data): every address, each type before its
/// parameters (the layout has them in that order).
pub fn load(c: &Engine, data: &[u8]) {
    for (a, &v) in data.iter().enumerate().take(SIZE) {
        set(c, a, v);
    }
}

/// Scene 00's bytes: the edit buffer of a new 01V (read-only, the initial data).
pub fn initial_bytes() -> &'static [u8] {
    static BYTES: std::sync::OnceLock<Vec<u8>> = std::sync::OnceLock::new();
    BYTES.get_or_init(|| {
        let e = Engine::new(0);
        e.configure(&livedaw_engine::ConsoleConfig { profile: Profile::Yamaha01V, counts: livedaw_engine::Counts::Y01V });
        bytes(&e)
    })
}

/// The send level byte of strip `i` for send `n` of AUX 1-4, EFFECT 1-2 (0-5):
/// CH 17-24 have only AUX 1-2 and EFFECT 1-2 (p.264).
pub fn send_level_address(i: usize, n: usize) -> Option<usize> {
    let block = SEND_SLOTS.get(n).and_then(|slot| (0..send_count(i)).find(|&k| send_slot(i, k) == Some(*slot)))?;
    send_address(i, block)
}

/// A strip's name as the assign table prints it ("CH1", "CH13-14", "RETURN1", "ST OUT").
pub fn strip_name(i: usize) -> String {
    match i {
        i if i < STEREO_CH => format!("CH{}", i + 1),
        STEREO_CH => "CH13-14".into(),
        13 => "CH15-16".into(),
        i if i < RETURNS => format!("CH{}", i + 17 - OPTION_CH),
        i if i < BUSES => format!("RETURN{}", i - RETURNS + 1),
        i if i < AUXES => format!("BUS{}", i - BUSES + 1),
        i if i < EFFECTS => format!("AUX{}", i - AUXES + 1),
        i if i < STEREO => format!("EFFECT{}", i - EFFECTS + 1),
        STEREO => "ST OUT".into(),
        _ => String::new(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_engine::config::{ConsoleConfig, Counts};

    fn y01v() -> Engine {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::Yamaha01V, counts: Counts::Y01V });
        e
    }

    #[test]
    fn the_layout_fits_the_manuals_bytes() {
        assert_eq!((EQ4, FX_END), (315, 1006));
        const { assert!(FX_END <= SIZE) };
        const { assert!(SIZE <= 1024) }; // the manual's address range
        assert_eq!(field(0), Some(Field::Common(0, 0)));
        assert_eq!(field(315), Some(Field::Eq4(0, 0, 0)));
        assert_eq!(field(567), Some(Field::Eq2(14, 0, 0)));
        assert_eq!(field(615), Some(Field::Dyn(0, 0)));
        assert_eq!(field(748), Some(Field::Send(0, 0)));
        assert_eq!(field(924), Some(Field::Fx(0, 0)));
        assert_eq!(field(1006), None);
        assert_eq!(strip_name(STEREO_CH), "CH13-14");
        assert_eq!(strip_name(21), "CH24");
    }

    #[test]
    fn every_01v_effect_fits_its_block() {
        for m in livedaw_dsp::y01v::fx::FX_MODELS.iter() {
            assert!(m.params.len() <= FX_VALUES, "{}", m.token);
        }
    }

    #[test]
    fn a_console_comes_back_from_its_bytes() {
        let e = y01v();
        set(&e, common(0, 0), 90); // CH1's fader
        set(&e, common(1, 1), 0); // CH2 off
        set(&e, common(0, 2), 4); // CH1 pan L12
        set(&e, common(STEREO_CH, 2), 6);
        set(&e, common(STEREO_CH, 3), 26);
        set(&e, common(STEREO, 2), 20); // balance
        set(&e, common(2, 4), 80); // ATT -16
        set(&e, common(2, 5), 0b0101); // phase, EQ on
        set(&e, common(2, 6), 3);
        set(&e, common(2, 7), 7);
        set(&e, common(4, 8), 0b1010);
        set(&e, eq_address(3).unwrap() + 4, 22);
        set(&e, eq_address(14).unwrap() + 4, 33);
        set(&e, dyn_address(0).unwrap(), 2);
        set(&e, dyn_address(0).unwrap() + 2, 3);
        set(&e, send_address(0, 4).unwrap(), 100); // CH1 to EFFECT 1
        set(&e, send_address(0, 6).unwrap(), 1);
        set(&e, send_address(14, 1).unwrap(), 70); // CH17 to AUX 2
        set(&e, fx_address(1), 9);
        set(&e, fx_address(1) + 2, 12);

        let data = bytes(&e);
        assert_eq!(data.len(), SIZE);
        let e2 = y01v();
        load(&e2, &data);
        assert_eq!(bytes(&e2), data);
        assert!(e2.strip_at(StripKind::Channel, 1).unwrap().params.mute.load(Relaxed));
        assert_eq!(get(&e2, common(0, 0)), 90);
        assert_eq!((get(&e2, common(STEREO_CH, 2)), get(&e2, common(STEREO_CH, 3))), (6, 26));
        assert_eq!(get(&e2, common(STEREO, 2)), 20);
        assert_eq!(get(&e2, common(2, 4)), 80);
        assert_eq!(get(&e2, common(2, 5)) & 0b0101, 0b0101);
        assert_eq!((get(&e2, common(2, 6)), get(&e2, common(2, 7))), (3, 7));
        assert_eq!(get(&e2, common(4, 8)), 0b1010);
        assert_eq!(get(&e2, eq_address(3).unwrap() + 4), 22);
        assert_eq!(get(&e2, eq_address(14).unwrap() + 4), 33);
        assert_eq!(get(&e2, dyn_address(0).unwrap()), 2);
        assert_eq!(get(&e2, dyn_address(0).unwrap() + 2), 3);
        assert_eq!((get(&e2, send_address(0, 4).unwrap()), get(&e2, send_address(14, 1).unwrap())), (100, 70));
        assert_eq!(get(&e2, send_address(0, 6).unwrap()) & 1, 1);
        assert_eq!(get(&e2, fx_address(1)), 9);
        assert_eq!(get(&e2, fx_address(1) + 2), 12);
    }

    #[test]
    fn the_initial_data_is_a_new_console() {
        let e = y01v();
        assert_eq!(bytes(&e), initial_bytes());
        assert_eq!(initial_bytes()[common(0, 1)], 1, "a new console's channels are on");
    }
}
