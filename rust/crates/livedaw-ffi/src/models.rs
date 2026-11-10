//! Model metadata for the host UI: the UI builds its controls (knobs, choices,
//! switches, labels, value text) from the same tables the DSP uses.
//!
//! Tables: 0 = dynamics models (GATE and DYNAMICS slots), 1 = channel EQ
//! models, 2 = bus/main/matrix EQ models, 3 = filter tools, 4 = FX rack devices.
//! Lists: 0 = process
//! orders, 1 = tap points, 2 = sidechain filters, 3 = crossover types,
//! 4 = bus send modes, 5 = delay units, 6 = post insert modes, 7 = monitor
//! buses, 8 = low-cut slopes, 9 = high-cut slopes, 10 = aux dynamics models,
//! 11 = strip colours (index 0 = WING colour 1), 12 = MAIN LINK modes.

use crate::session::LdString;
use livedaw_dsp::delay::DELAY_MODES;
use livedaw_dsp::model::{ModelCategory, ModelSpec, ParamKind, ParamSpec};
use livedaw_dsp::wing::dynamics::{SC_FILTERS, XOVER_TYPES};
use livedaw_dsp::wing::eq::design;
use livedaw_dsp::wing::filter::{HIGH_CUT_SLOPES, LOW_CUT_SLOPES};
use livedaw_dsp::wing::{AUX_DYN_MODELS, MAX_EQ_PARAMS, PROCESS_ORDERS, TAP_POINTS};
use livedaw_engine::blocks::{Block, BlockParams};
use livedaw_engine::params::{
    colour_rgb, DynSlotParams, EqBlockParams, FilterParams, MAIN_LINK_MODES, MONITOR_BUSES, POST_INSERT_MODES, WING_COLOURS,
    WING_COLOUR_GRID,
};
use livedaw_engine::profile::DynSlot;
use livedaw_engine::{Strip, SEND_MODES};

/// The profile whose models the host's model calls describe (`ld_engine_profile`'s
/// numbers; the WING's until the host shows another console).
static SHOWN_PROFILE: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(1);

/// ABI 37: the host's model calls (`ld_model_*`) describe profile `profile`'s
/// models from now on (the console it shows; an unknown one: the WING's).
#[no_mangle]
pub extern "C" fn ld_models_show_profile(profile: i32) {
    SHOWN_PROFILE.store(profile, std::sync::atomic::Ordering::Relaxed);
}

/// The host's model tables: the shown profile's (`ProfileSpec::models`).
fn table(t: i32) -> &'static [ModelSpec] {
    crate::profile_of(SHOWN_PROFILE.load(std::sync::atomic::Ordering::Relaxed)).spec().models.table(t)
}

fn model(t: i32, m: i32) -> Option<&'static ModelSpec> {
    table(t).get(usize::try_from(m).ok()?)
}

fn param(t: i32, m: i32, p: i32) -> Option<&'static ParamSpec> {
    model(t, m)?.params.get(usize::try_from(p).ok()?)
}

fn list(l: i32) -> &'static [&'static str] {
    match l {
        0 => PROCESS_ORDERS,
        1 => TAP_POINTS,
        2 => &SC_FILTERS,
        3 => &XOVER_TYPES,
        4 => &SEND_MODES,
        5 => &DELAY_MODES,
        6 => &POST_INSERT_MODES,
        7 => &MONITOR_BUSES,
        8 => &LOW_CUT_SLOPES,
        9 => &HIGH_CUT_SLOPES,
        10 => AUX_DYN_MODELS,
        11 => &COLOUR_NAMES,
        12 => &MAIN_LINK_MODES,
        _ => &[],
    }
}

static COLOUR_NAMES: [&str; WING_COLOURS.len()] = {
    let mut names = [""; WING_COLOURS.len()];
    let mut i = 0;
    while i < names.len() {
        names[i] = WING_COLOURS[i].0;
        i += 1;
    }
    names
};

/// RGB (0xRRGGBB) of WING colour `index` (1..18).
#[no_mangle]
pub extern "C" fn ld_colour_rgb(index: i32) -> u32 {
    colour_rgb(index)
}

/// The colour index (1..18) at place `position` (0..17, row by row, three to
/// a row) of ICON/COLOR's colour grid; 0 outside it.
#[no_mangle]
pub extern "C" fn ld_colour_at_grid(position: i32) -> i32 {
    usize::try_from(position).ok().and_then(|i| WING_COLOUR_GRID.get(i)).copied().unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn ld_model_count(t: i32) -> i32 {
    table(t).len() as i32
}

/// Whether the shown console lets FX device `m` run in FX slot `slot` (0-based), by the
/// engine's own rule (`allowed_in`), so the screens offer what `set_fx_model` will take.
#[no_mangle]
pub extern "C" fn ld_model_fx_allowed(m: i32, slot: i32) -> bool {
    let (Ok(m), Ok(slot)) = (usize::try_from(m), usize::try_from(slot)) else { return false };
    let models = crate::profile_of(SHOWN_PROFILE.load(std::sync::atomic::Ordering::Relaxed)).spec().models;
    livedaw_dsp::wing::fx::allowed_in(models, m, slot)
}

/// what: 0 = token ("COMP"), 1 = display name.
#[no_mangle]
pub extern "C" fn ld_model_text(t: i32, m: i32, what: i32) -> *mut LdString {
    let s = model(t, m).map_or("", |m| if what == 0 { m.token } else { m.name });
    LdString::new(s.to_owned())
}

/// 0 = gate, 1 = compressor, 2 = special, 3 = EQ, 4 = filter; FX devices:
/// 5 = none, 6 = external (plugin), 7 = premium, 8 = standard, 9 = channel strip.
#[no_mangle]
pub extern "C" fn ld_model_category(t: i32, m: i32) -> i32 {
    model(t, m).map_or(0, |m| match m.category {
        ModelCategory::Gate => 0,
        ModelCategory::Compressor => 1,
        ModelCategory::Special => 2,
        ModelCategory::Eq => 3,
        ModelCategory::Filter => 4,
        ModelCategory::FxNone => 5,
        ModelCategory::FxExternal => 6,
        ModelCategory::FxPremium => 7,
        ModelCategory::FxStandard => 8,
        ModelCategory::FxChannel => 9,
    })
}

/// `slot` of the C ABI: 0 = gate slot, 1 = dynamics slot or an extra dynamics block,
/// 2 = the preamp block (`ld_strip_preamp_block`).
fn dyn_slot(slot: i32) -> Option<DynSlot> {
    match slot {
        0 => Some(DynSlot::Gate),
        1 => Some(DynSlot::Dynamics),
        2 => Some(DynSlot::Preamp),
        _ => None,
    }
}

/// ABI 56: whether dynamics model `m` (table 0, of the shown console:
/// `ld_models_show_profile`) may be offered for `slot` (0 = gate slot, 1 = dynamics
/// slot or an extra dynamics block, 2 = the preamp block). A dLive's preamp models
/// go in its preamp block alone, and it holds nothing else; on any other console,
/// the Universal first, every model of its table goes anywhere (false: no such model
/// or slot).
#[no_mangle]
pub extern "C" fn ld_dyn_model_allowed(m: i32, slot: i32) -> bool {
    dyn_model_allowed(crate::profile_of(SHOWN_PROFILE.load(std::sync::atomic::Ordering::Relaxed)), m, slot)
}

fn dyn_model_allowed(profile: livedaw_engine::Profile, m: i32, slot: i32) -> bool {
    let spec = profile.spec();
    let (Some(model), Some(slot)) = (usize::try_from(m).ok().and_then(|i| spec.models.table(0).get(i)), dyn_slot(slot)) else {
        return false;
    };
    spec.may_hold(slot, model.token)
}

/// The refusals of the strip setters (`ld_strip_set_model`, `ld_strip_set_block_model`):
/// may this dynamics slot of the strip hold model `m` of its table? (A model it
/// hasn't is the setter's no-op.)
pub(crate) fn dyn_slot_may_hold(s: &Strip, p: &DynSlotParams, slot: DynSlot, m: usize) -> bool {
    p.table().get(m).is_none_or(|spec| s.spec().may_hold_on(s.kind(), slot, spec.token))
}

#[no_mangle]
pub unsafe extern "C" fn ld_model_find(t: i32, utf8: *const u8, len: usize) -> i32 {
    let token = crate::session::str_arg(utf8, len);
    table(t).iter().position(|m| m.token == token).map_or(-1, |i| i as i32)
}

#[no_mangle]
pub extern "C" fn ld_model_num_params(t: i32, m: i32) -> i32 {
    model(t, m).map_or(0, |m| m.params.len() as i32)
}

#[repr(C)]
#[derive(Default)]
pub struct LdParamInfo {
    /// 0 = linear, 1 = log, 2 = choice, 3 = values, 4 = toggle, 5 = integer.
    pub kind: i32,
    pub min: f32,
    pub max: f32,
    pub steps: i32,
    pub default_value: f32,
    /// Number of choices (choice/values kinds).
    pub num_choices: i32,
}

#[no_mangle]
pub unsafe extern "C" fn ld_model_param_info(t: i32, m: i32, p: i32, out: *mut LdParamInfo) -> bool {
    let (Some(spec), Some(out)) = (param(t, m, p), out.as_mut()) else { return false };
    let (kind, num) = match spec.kind {
        ParamKind::Linear => (0, 0),
        ParamKind::Log => (1, 0),
        ParamKind::Choice(items) => (2, items.len()),
        ParamKind::Values(items) => (3, items.len()),
        ParamKind::Toggle => (4, 0),
        ParamKind::Int => (5, 0),
    };
    *out = LdParamInfo {
        kind,
        min: spec.min,
        max: spec.max,
        steps: spec.steps as i32,
        default_value: spec.default,
        num_choices: num as i32,
    };
    true
}

/// what: 0 = id ("thr"), 1 = label ("THR"), 2 = unit ("dB").
#[no_mangle]
pub extern "C" fn ld_model_param_text(t: i32, m: i32, p: i32, what: i32) -> *mut LdString {
    let s = param(t, m, p).map_or("", |p| match what {
        0 => p.id,
        1 => p.label,
        _ => p.unit,
    });
    LdString::new(s.to_owned())
}

/// Text of choice `i` of a choice/values parameter.
#[no_mangle]
pub extern "C" fn ld_model_param_choice(t: i32, m: i32, p: i32, i: i32) -> *mut LdString {
    let s = param(t, m, p).map(|p| p.display(i as f32)).unwrap_or_default();
    LdString::new(s)
}

/// Console-style text for a value ("-20.0 dB", "1:2", "8k", "5k7 Hz", "1503 ms").
#[no_mangle]
pub extern "C" fn ld_model_param_display(t: i32, m: i32, p: i32, value: f32) -> *mut LdString {
    LdString::new(param(t, m, p).map(|p| p.display(value)).unwrap_or_default())
}

/// Knob position 0..1 for a value (log taper for log parameters).
#[no_mangle]
pub extern "C" fn ld_model_param_to_normalised(t: i32, m: i32, p: i32, value: f32) -> f32 {
    param(t, m, p).map_or(0.0, |p| p.to_normalised(value))
}

#[no_mangle]
pub extern "C" fn ld_model_param_from_normalised(t: i32, m: i32, p: i32, pos: f32) -> f32 {
    param(t, m, p).map_or(0.0, |p| p.from_normalised(pos))
}

/// Static curve of a dynamics model of the shown console's table (as the other
/// model calls: `ld_models_show_profile`), its transfer graph: for each input level
/// in `in_db`, the output level, computed from the same configuration the
/// audio thread runs. Returns false (outputs untouched) for models without a
/// level curve.
///
/// # Safety
/// `values` must hold `num_values` floats, `in_db` and `out_db` `count` each.
#[no_mangle]
pub unsafe extern "C" fn ld_dyn_static_curve(
    model: i32,
    values: *const f32,
    num_values: i32,
    in_db: *const f32,
    out_db: *mut f32,
    count: i32,
) -> bool {
    let (Ok(model), Ok(nv), Ok(n)) = (usize::try_from(model), usize::try_from(num_values), usize::try_from(count)) else {
        return false;
    };
    if values.is_null() || in_db.is_null() || out_db.is_null() {
        return false;
    }
    let values = std::slice::from_raw_parts(values, nv);
    let ins = std::slice::from_raw_parts(in_db, n);
    let outs = std::slice::from_raw_parts_mut(out_db, n);
    livedaw_dsp::wing::dynamics::static_curve_in(table(0), model, values, ins, outs)
}

/// # Safety
/// `values` must point to `num_values` floats, `freqs` and `out_db` to `count` each.
#[no_mangle]
pub unsafe extern "C" fn ld_model_response(
    t: i32,
    model: i32,
    values: *const f32,
    num_values: i32,
    freqs: *const f32,
    out_db: *mut f32,
    count: i32,
) -> bool {
    let (Ok(model), Ok(nv), Ok(n)) = (usize::try_from(model), usize::try_from(num_values), usize::try_from(count)) else {
        return false;
    };
    if values.is_null() || freqs.is_null() || out_db.is_null() {
        return false;
    }
    let Some(spec) = table(t).get(model) else { return false };
    let values = std::slice::from_raw_parts(values, nv);
    let freqs = std::slice::from_raw_parts(freqs, n);
    let out = std::slice::from_raw_parts_mut(out_db, n);
    match t {
        1 | 2 => {
            let mut v = [0.0; MAX_EQ_PARAMS];
            for ((dst, p), src) in v.iter_mut().zip(spec.params).zip(values.iter().map(Some).chain(std::iter::repeat(None))) {
                *dst = src.map_or(p.default, |x| p.sanitise(*x));
            }
            let d = design(spec, &v[..spec.params.len().min(MAX_EQ_PARAMS)], 48_000.0, 1.0);
            for (f, o) in freqs.iter().zip(out.iter_mut()) {
                *o = d.response_db(f64::from(*f), 48_000.0) as f32;
            }
            true
        }
        4 => crate::guard(false, || livedaw_dsp::wing::fx::response(spec.token, values, freqs, out)),
        _ => false,
    }
}

#[no_mangle]
pub extern "C" fn ld_list_count(l: i32) -> i32 {
    list(l).len() as i32
}

#[no_mangle]
pub extern "C" fn ld_list_item(l: i32, i: i32) -> *mut LdString {
    LdString::new(usize::try_from(i).ok().and_then(|i| list(l).get(i)).copied().unwrap_or("").to_owned())
}

/// See `ld_strip_response`.
pub(crate) fn strip_response(s: &Strip, block: i32, freqs: &[f64], out: &mut [f64], sr: f64) {
    let p = &s.params;
    match block {
        4 => {
            // TAP EQ
            let d = livedaw_engine::tapsend::tap_eq_design(&p.tap_eq, sr);
            fill(freqs, out, |f| d.iter().flatten().map(|c| c.magnitude_at(f, sr)).product());
        }
        // The GATE's or the DYNAMICS' key filter, the DYNAMICS' crossover.
        5 => key_response(p.gate(), freqs, out, sr),
        6 => key_response(p.dynamics(), freqs, out, sr),
        7 => xover_response(p.dynamics(), freqs, out, sr),
        3 => filter_response(p.filter(), freqs, out, sr),
        _ => eq_response(p.eq(), freqs, out, sr),
    }
}

/// See `ld_strip_block_response`: an EQ or filter block's curve (`part` 0), a
/// dynamics block's key filter (0) or crossover (1). False for anything else.
pub(crate) fn block_response(b: &Block, part: i32, freqs: &[f64], out: &mut [f64], sr: f64) -> bool {
    match (&b.params, part) {
        (BlockParams::Eq(p), 0) => eq_response(p, freqs, out, sr),
        (BlockParams::Filter(p), 0) => filter_response(p, freqs, out, sr),
        (BlockParams::Dynamics(p), 0) => key_response(p, freqs, out, sr),
        (BlockParams::Dynamics(p), 1) => xover_response(p, freqs, out, sr),
        _ => return false,
    }
    true
}

/// `out` = the magnitude `mag(f)` at each of `freqs`, in dB.
fn fill(freqs: &[f64], out: &mut [f64], mag: impl Fn(f64) -> f64) {
    for (f, o) in freqs.iter().zip(out.iter_mut()) {
        *o = 20.0 * mag(*f).max(1e-9).log10();
    }
}

fn filter_response(p: &FilterParams, freqs: &[f64], out: &mut [f64], sr: f64) {
    let (coeffs, n) = p.snapshot().design(sr);
    fill(freqs, out, |f| coeffs[..n].iter().map(|c| c.magnitude_at(f, sr)).product());
}

fn key_response(p: &DynSlotParams, freqs: &[f64], out: &mut [f64], sr: f64) {
    let c = p.sidechain().coefficients(sr);
    fill(freqs, out, |f| c.magnitude_at(f, sr));
}

/// As the DYNAMICS page draws it: PEAK as the band taken down by DEPTH.
fn xover_response(p: &DynSlotParams, freqs: &[f64], out: &mut [f64], sr: f64) {
    let c = p.xover().graph_coefficients(sr);
    fill(freqs, out, |f| c.magnitude_at(f, sr));
}

fn eq_response(eq: &EqBlockParams, freqs: &[f64], out: &mut [f64], sr: f64) {
    let Some(spec) = eq.model_spec() else { return };
    let mut values = [0.0; MAX_EQ_PARAMS];
    eq.values_snapshot(&mut values);
    let on = eq.on.load(std::sync::atomic::Ordering::Relaxed);
    let d = design(spec, &values[..spec.params.len()], sr, if on { eq.mix.load() / 100.0 } else { 0.0 });
    for (f, o) in freqs.iter().zip(out.iter_mut()) {
        *o = d.response_db(*f, sr);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_engine::profile::DLiveLayout;
    use livedaw_engine::Profile;

    #[test]
    fn a_slot_is_offered_only_the_models_it_may_hold() {
        let dlive = Profile::AhDLive(DLiveLayout::FOH);
        let index = |p: Profile, token: &str| p.spec().models.table(0).iter().position(|m| m.token == token).unwrap() as i32;
        let (valve, tube, gate, comp) =
            (index(dlive, "DLVVALVE"), index(dlive, "DLVTUBE"), index(dlive, "DLVGATE"), index(dlive, "DLVPEAK"));
        for m in [valve, tube] {
            assert!(!dyn_model_allowed(dlive, m, 0), "not in the gate slot");
            assert!(!dyn_model_allowed(dlive, m, 1), "not in the compressor slot");
            assert!(dyn_model_allowed(dlive, m, 2), "in the preamp block");
        }
        for m in [gate, comp] {
            assert!(dyn_model_allowed(dlive, m, 0) && dyn_model_allowed(dlive, m, 1));
            assert!(!dyn_model_allowed(dlive, m, 2));
        }
        assert!(!dyn_model_allowed(dlive, -1, 2) && !dyn_model_allowed(dlive, 999, 2));
        assert!(!dyn_model_allowed(dlive, valve, 3));

        // The Universal offers them with every other console's models, anywhere.
        let u = Profile::Universal;
        let v = index(u, "DLVVALVE");
        for slot in 0..3 {
            assert!(dyn_model_allowed(u, v, slot), "slot {slot}");
        }
        assert!(dyn_model_allowed(Profile::Wing, index(Profile::Wing, "COMP"), 1));
    }
}
