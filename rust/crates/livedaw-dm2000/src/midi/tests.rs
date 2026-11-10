use super::*;
use crate::tables::METER;
use livedaw_dsp::dm2000::{EQ_PARAMS, FREQS};
use livedaw_dsp::fader::FaderLaw;
use livedaw_engine::profile::dm2000 as dm2000_consts;
use livedaw_engine::{ConsoleConfig, Counts};

fn dm() -> Engine {
    let e = Engine::new(0);
    e.configure(&ConsoleConfig { profile: Profile::Dm2000, counts: Counts::DM2000 });
    e
}

fn ready() -> (Engine, Midi) {
    let e = dm();
    let mut m = Midi::default();
    m.setup.rx = [true; KINDS];
    m.setup.tx = [true; KINDS];
    m.poll(&e); // takes the console's values
    while m.take_out().is_some() {}
    (e, m)
}

fn change(tt: u8, ee: u8, pp: u8, ch: u8, v: i32) -> Vec<u8> {
    let d = data_bytes(v);
    let m = if tt == 1 { UNIVERSAL } else { MODEL };
    vec![0xF0, 0x43, 0x10, GROUP, m, tt, ee, pp, ch, d[0], d[1], d[2], d[3], 0xF7]
}

fn request(tt: u8, ee: u8, pp: u8, ch: u8) -> Vec<u8> {
    let m = if tt == 1 { UNIVERSAL } else { MODEL };
    vec![0xF0, 0x43, 0x30, GROUP, m, tt, ee, pp, ch, 0xF7]
}

fn value_of(msg: &[u8]) -> i32 {
    data_value(&msg[9..13])
}

fn def(element: &str, name: &str) -> usize {
    DEFS.iter().position(|d| d.element == element && d.name == name).unwrap_or_else(|| panic!("{element} {name}"))
}

fn address(element: &str, name: &str) -> (u8, u8, u8) {
    let d = &DEFS[def(element, name)];
    (d.tt, d.ee, d.pp)
}

#[test]
fn the_generated_tables_are_yamahas() {
    assert_eq!((FADER[0], FADER[1], FADER[15], FADER[823], FADER[1023]), (FADER_MIN, -138.0, -96.0, 0.0, 10.0));
    assert_eq!(
        (BUS_FADER[0], BUS_FADER[1], BUS_FADER[3], BUS_FADER[83], BUS_FADER[1023]),
        (FADER_MIN, -138.0, -134.0, -78.0, 0.0)
    );
    assert_eq!(METER[0], 0x0084, "-96 dB");
    assert_eq!(METER[96], 0x0FFF, "0 dB");
    // Every step of both tables is what the fader law gives.
    for (i, (&a, &b)) in FADER.iter().zip(BUS_FADER.iter()).enumerate() {
        let (x, y) = (FaderLaw::DM2000.to_db(i as f32 / 1023.0), FaderLaw::DM2000_MASTER.to_db(i as f32 / 1023.0));
        assert!((a - x).abs() < 0.03 || i == 0, "channel {i}: {a} {x}");
        assert!((b - y).abs() < 0.03 || i == 0, "output {i}: {b} {y}");
    }
    // The EQ's 120 frequencies are PRM TABLE #12's values 5-124.
    assert_eq!(&crate::tables::EQ_FREQ[5..125], &FREQS[..], "#12");
    // Input Fader: `F0 43 1n 3E 7F 01 1C 00 cc`.
    assert_eq!(address("kInputFader", "kFader"), (0x01, 0x1C, 0x00));
    assert_eq!(address("kInputAUX", "kAUX1Level"), (0x01, 0x23, 0x02));
    assert_eq!(address("kBusFader", "kFader"), (0x01, 0x2B, 0x00));
    assert_eq!(address("kChannelInput", "kChannelIn"), (0x02, 0x01, 0x00));
    assert_eq!(address("kSetupPGMChangeTable", "kPGMChangeTable"), (0x03, 0x5A, 0x00));
}

#[test]
fn every_generated_row_has_a_field_or_is_known_not_to() {
    let m = Midi::default();
    let unmapped: Vec<&str> = DEFS.iter().enumerate().filter(|(i, _)| m.fields[*i].is_none()).map(|(_, d)| d.name).collect();
    // Things the engine doesn't have (fade times, keying, delay mix, routing's PAN
    // and DIRECT, surround, ...), known one by one so a typo in `field_of` shows.
    let expected_prefixes = [
        "kFadeTime",
        "kAttBitShift",
        "kGateLink",
        "kGateKey",
        "kCompLink",
        "kInDelayMix",
        "kInDelayFB",
        "kRoutingPan",
        "kRoutingDirect",
        "kInGroupDynamics",
        "kInGroupEQ",
        "kInGroupLevel",
        "kInMasterFaderAsgn",
        "kInMasterMuteAsgn",
        "kBusGroupDynamics",
        "kBusGroupEQ",
        "kBusGroupLevel",
        "kOutMasterFaderAsgn",
        "kOutMasterMuteAsgn",
        "kAUXGroupDynamics",
        "kAUXGroupEQ",
        "kAUXGroupLevel",
        "kMatrixGroupDynamics",
        "kMatrixGroupEQ",
        "kMatrixGroupLevel",
        "kStereoGroupDynamics",
        "kStereoGroupEQ",
        "kStereoGroupLevel",
        "kEffectTitle",
        "kEffectParam",
        "kEffectBPM",
        "kEffectMode",
        "kEffectUtility",
        "kEffectReserved",
        "kChannelNameShort",
        "kBusModuleNameShort",
        "kAUXModuleNameShort",
        "kMatrixModuleNameShort",
        "kStOutModuleNameShort",
        "kMIDIRxTerminal",
        "kMIDIRxPortNum",
        "kMIDITxTerminal",
        "kMIDITxPortNum",
        "kTxBulkInterval",
    ];
    for name in unmapped {
        assert!(expected_prefixes.iter().any(|p| name.starts_with(p)), "unmapped: {name}");
    }
}

#[test]
fn every_field_answers_a_request_with_what_a_change_set() {
    let (e, mut m) = ready();
    let c = Console::new(&e).unwrap();
    for (i, d) in DEFS.iter().enumerate() {
        if m.fields[i].is_none() || !d.rx {
            continue;
        }
        // Enums that remap (Q's, the frequencies) stay in the ranges the engine has.
        let target = match m.fields[i] {
            Some((_, Field::Name(_))) => b'A' as i32,
            Some((_, Field::EqLowQ)) => 41,
            Some((_, Field::EqHighQ)) => 40,
            Some((_, Field::EqFreq(_))) => 60,
            Some((_, Field::SlotOut(_))) | Some((_, Field::OmniOut)) | Some((_, Field::Digital2tr)) => 3,
            Some((_, Field::StereoOut)) => 9,
            Some((_, Field::ChannelIn)) => 40,
            Some((_, Field::FxType)) => 12,
            Some((_, Field::CompKnee)) => 3,
            Some((_, Field::InsertLoc)) | Some((_, Field::CompLoc)) => 2,
            Some((_, Field::RxChannel)) | Some((_, Field::TxChannel)) => 2,
            Some((_, Field::PcTable)) => 42,
            _ => (d.min + d.max) / 2 + 1,
        }
        .clamp(d.min, d.max);
        let ch = (d.max_ch.min(1)) as u8;
        m.apply(&c, i as u16, ch as u16, target);
        let got = m.read(&c, i as u16, ch as u16);
        assert_eq!(got, Some(target), "{} {}", d.element, d.name);
    }
}

#[test]
fn a_change_moves_the_console_and_a_request_reads_it() {
    let (e, mut m) = ready();
    let ch = |i: usize| e.strips(StripKind::Channel)[i].clone();
    // CH1 fader to 0 dB: value 823.
    let (tt, ee, pp) = address("kInputFader", "kFader");
    m.receive(&e, &change(tt, ee, pp, 0, 823));
    assert!((ch(0).params.fader_db.load()).abs() < 0.01);
    while m.take_out().is_some() {}
    // Request it back: answered with a change, the same value.
    m.receive(&e, &request(tt, ee, pp, 0));
    let reply = m.take_out().unwrap();
    assert_eq!(reply[0..9], [0xF0, 0x43, 0x10, GROUP, UNIVERSAL, 0x01, 0x1C, 0x00, 0x00]);
    assert_eq!(value_of(&reply), 823);
    assert_eq!(m.take_out(), None);
    // CH3 AUX2 send level: -20 dB on value 423.
    let (tt, ee, pp) = address("kInputAUX", "kAUX2Level");
    m.receive(&e, &change(tt, ee, pp, 2, 423));
    let slot = dm2000_consts::AUX + 1;
    assert!((ch(2).sends.find(slot).unwrap().level_db.load() + 20.0).abs() < 0.01);
    // A negative value: the gate's threshold (28-bit two's complement).
    let (tt, ee, pp) = address("kInputGate", "kGateThreshold");
    m.receive(&e, &change(tt, ee, pp, 0, -260));
    assert!((ch(0).params.gate().values[0].load() + 26.0).abs() < 0.01);
    m.receive(&e, &request(tt, ee, pp, 0));
    assert_eq!(value_of(&m.take_out().unwrap()), -260);
    // Out of range values are clamped by the list's range.
    let (tt, ee, pp) = address("kInputChannelPan", "kChannelPan");
    m.receive(&e, &change(tt, ee, pp, 5, 200));
    assert_eq!(ch(5).params.pan.load(), 100.0);
    // Another device number: ignored (Rx channel 1).
    let (tt, ee, pp) = address("kInputChannelOn", "kChannelOn");
    let mut other = change(tt, ee, pp, 7, 0);
    other[2] = 0x13;
    m.receive(&e, &other);
    assert!(!ch(7).params.mute.load(Relaxed));
    m.setup.omni[PRM] = true;
    m.receive(&e, &other);
    assert!(ch(7).params.mute.load(Relaxed), "OMNI");
}

#[test]
fn eq_values_map_through_the_tables() {
    let (e, mut m) = ready();
    let ch = e.strips(StripKind::Channel)[0].clone();
    let eq = ch.params.eq();
    assert_eq!(EQ_PARAMS[13].id, "hpf");
    assert_eq!(EQ_PARAMS[1].id, "lq");
    // LOW Q: 44 HPF, 41 L.SHELF, 0-40 the Qs.
    let (tt, ee, pp) = address("kInputEQ", "kEQLowQ");
    m.receive(&e, &change(tt, ee, pp, 0, 44));
    assert_eq!(eq.values[1].load(), 0.0, "engine's HPF");
    m.receive(&e, &change(tt, ee, pp, 0, 41));
    assert_eq!(eq.values[1].load(), 42.0, "L.SHELF");
    m.receive(&e, &change(tt, ee, pp, 0, 20));
    assert_eq!(eq.values[1].load(), 21.0, "Q 1.0");
    // HIGH Q: 43 LPF, 42 H.SHELF.
    let (tt, ee, pp) = address("kInputEQ", "kEQHiQ");
    m.receive(&e, &change(tt, ee, pp, 0, 43));
    assert_eq!(eq.values[10].load(), 0.0);
    // LOW F: value 5 is 21.2 Hz, the engine's first.
    let (tt, ee, pp) = address("kInputEQ", "kEQLowF");
    m.receive(&e, &change(tt, ee, pp, 0, 5));
    assert_eq!(eq.values[2].load(), 0.0);
    m.receive(&e, &change(tt, ee, pp, 0, 72));
    assert_eq!(FREQS[eq.values[2].load() as usize], 1000.0, "value 72");
    // LOW G: tenths of a dB.
    let (tt, ee, pp) = address("kInputEQ", "kEQLowG");
    m.receive(&e, &change(tt, ee, pp, 0, -35));
    assert!((eq.values[3].load() + 3.5).abs() < 1e-3);
}

#[test]
fn dynamics_and_process_order_map() {
    let (e, mut m) = ready();
    let ch = e.strips(StripKind::Channel)[1].clone();
    let c = Console::new(&e).unwrap();
    let dynamics = ch.params.dynamics();
    // Type: a compander takes -18..0 dB and a width.
    let (tt, ee, pp) = address("kInputComp", "kCompType");
    m.receive(&e, &change(tt, ee, pp, 1, 2));
    assert_eq!(dynamics.model_spec().unwrap().token, "DMCPH");
    let (tt, ee, pp) = address("kInputComp", "kCompGain");
    m.receive(&e, &change(tt, ee, pp, 1, 0));
    assert!((dynamics.values[3].load() + 18.0).abs() < 1e-3, "0 is -18 dB on a compander (#09)");
    let (tt, ee, pp) = address("kInputComp", "kCompKnee");
    m.receive(&e, &change(tt, ee, pp, 1, 89));
    assert_eq!(dynamics.values[4].load(), 90.0, "width 90");
    assert_eq!(m.read(&c, def("kInputComp", "kCompKnee") as u16, 1), Some(89));
    // Back to Comp.
    let (tt, ee, pp) = address("kInputComp", "kCompType");
    m.receive(&e, &change(tt, ee, pp, 1, 0));
    assert_eq!(dynamics.model_spec().unwrap().token, "DMCMP");
    // Ducking.
    let (tt, ee, pp) = address("kInputGate", "kGateType");
    m.receive(&e, &change(tt, ee, pp, 1, 1));
    assert_eq!(ch.params.gate().model_spec().unwrap().token, "DMDUK");
    // Comp and Insert positions: the new channel is comp PRE FADER, insert PRE EQ.
    assert_eq!(order_parts(3), (1, 0, false));
    assert_eq!(ch.params.process_order.load(Relaxed), 3);
    let (tt, ee, pp) = address("kInputComp", "kCompLocComp");
    m.receive(&e, &change(tt, ee, pp, 1, 0)); // Comp PRE EQ, with Insert PRE EQ.
    assert_eq!(ch.params.process_order.load(Relaxed), 0, "shared place, Comp->Ins");
    let (tt, ee, pp) = address("kInputInsert", "kInsertLocInsertFirst");
    m.receive(&e, &change(tt, ee, pp, 1, 0)); // Insert->Comp
    assert_eq!(ch.params.process_order.load(Relaxed), 9);
    let (tt, ee, pp) = address("kInputInsert", "kInsertLocInsert");
    m.receive(&e, &change(tt, ee, pp, 1, 2)); // Insert POST FADER.
    assert_eq!(ch.params.process_order.load(Relaxed), 2);
}

#[test]
fn sends_groups_matrix_and_patches_map() {
    let (e, mut m) = ready();
    let channels = e.strips(StripKind::Channel);
    let buses = e.strips(StripKind::Bus);
    let ch0 = &channels[0];
    // Routing to BUS 3, on at 0 dB.
    let (tt, ee, pp) = address("kInputRouting", "kRoutingBus3");
    m.receive(&e, &change(tt, ee, pp, 0, 1));
    let s = ch0.sends.find(2).unwrap();
    assert!(s.enabled.load(Relaxed));
    assert_eq!(s.level_db.load(), 0.0);
    // The Stereo Out's matrix sends: MATRIX 2 (params 2 and 3 are its L and R).
    let main = e.strips(StripKind::Main)[0].clone();
    let (tt, ee, pp) = address("kStereoToMatrix", "kBusToMatrix3Level");
    m.receive(&e, &change(tt, ee, pp, 0, 823));
    assert!((main.matrix_send(1).unwrap().level_db.load()).abs() < 0.01);
    // Bus to Stereo.
    let (tt, ee, pp) = address("kBusToStereo", "kBusToStereoOn");
    m.receive(&e, &change(tt, ee, pp, 3, 1));
    assert!(buses[3].main_send(0).unwrap().on.load(Relaxed));
    // Fader Group A and Mute Group B memberships.
    let (tt, ee, pp) = address("kInputGroup", "kInGroupFader1");
    m.receive(&e, &change(tt, ee, pp, 0, 1));
    assert!(ch0.params.dca_mask.contains(0));
    let (tt, ee, pp) = address("kInputGroup", "kInGroupMute2");
    m.receive(&e, &change(tt, ee, pp, 0, 1));
    assert!(ch0.params.mute_group_mask.contains(1));
    // An output group is after the inputs' eight.
    let (tt, ee, pp) = address("kBusOutGroup", "kBusGroupFader1");
    m.receive(&e, &change(tt, ee, pp, 4, 1));
    assert!(buses[4].params.dca_mask.contains(8));
    // Group masters.
    let (tt, ee, pp) = address("kInFaderGrpMaster", "kFader");
    m.receive(&e, &change(tt, ee, pp, 7, 823));
    assert!((e.controls.dcas[7].fader_db.load()).abs() < 0.01);
    let (tt, ee, pp) = address("kOutFaderGrpMaster", "kMasterOn");
    m.receive(&e, &change(tt, ee, pp, 0, 0));
    assert!(e.controls.dcas[8].mute.load(Relaxed));
    let (tt, ee, pp) = address("kInMuteGrpMaster", "kMasterMute");
    m.receive(&e, &change(tt, ee, pp, 2, 1));
    assert!(e.controls.mute_groups.contains(2));
    // Input patch #19: AD 2 is value 2 (engine input 1), S2-1 is 41 (engine 40).
    let (tt, ee, pp) = address("kChannelInput", "kChannelIn");
    m.receive(&e, &change(tt, ee, pp, 10, 2));
    assert_eq!(channels[10].params.input_left.load(Relaxed), 1);
    m.receive(&e, &change(tt, ee, pp, 10, 41));
    assert_eq!(channels[10].params.input_left.load(Relaxed), 24 + 16);
    m.receive(&e, &request(tt, ee, pp, 10));
    assert_eq!(value_of(&m.take_out().unwrap()), 41);
    // FX1-1, BUS 2, none.
    m.receive(&e, &change(tt, ee, pp, 10, 121));
    assert_eq!(channels[10].params.input_left.load(Relaxed), FX_RETURN_SOURCE_BASE);
    m.receive(&e, &request(tt, ee, pp, 10));
    assert_eq!(value_of(&m.take_out().unwrap()), 121);
    m.receive(&e, &change(tt, ee, pp, 10, 160));
    assert_eq!(channels[10].params.input_left.load(Relaxed), strip_source_input(StripKind::Bus, 1, 0));
    m.receive(&e, &change(tt, ee, pp, 10, 0));
    assert_eq!(channels[10].params.input_left.load(Relaxed), -1);
    // SLOT 2, output 3 to AUX 5 (#22 value 13).
    let (tt, ee, pp) = address("kSlotOutput", "kSlotOut3");
    m.receive(&e, &change(tt, ee, pp, 1, 13));
    assert_eq!(e.output_patch().get(dm2000_ports::SLOT + 16 + 2), strip_source_input(StripKind::Bus, 12, 0));
    m.receive(&e, &request(tt, ee, pp, 1));
    assert_eq!(value_of(&m.take_out().unwrap()), 13);
    // A channel's long name, a letter at a time (it isn't transmitted).
    let (tt, ee, pp) = address("kInputChannelName", "kChannelNameLong2");
    m.receive(&e, &change(tt, ee, pp, 0, b'X' as i32));
    assert_eq!(channels[0].name().as_bytes()[1], b'X');
}

#[test]
fn control_changes_scale_as_the_manual_says() {
    let (e, mut m) = ready();
    let ch = e.strips(StripKind::Channel)[0].clone();
    // CC 1 on channel 1 is FADER H INPUT1, CC 33 FADER L. 0 dB is value 823:
    // cur = 823 * 15 + 511 = 12856 -> 100 and 56.
    m.receive(&e, &[0xB0, 1, 100]);
    m.receive(&e, &[0xB0, 33, 56]);
    assert!(ch.params.fader_db.load().abs() < 0.01);
    // CC 64 is ON INPUT1: 0 off, 63 and up on (add 63).
    m.receive(&e, &[0xB0, 64, 0]);
    assert!(ch.params.mute.load(Relaxed));
    m.receive(&e, &[0xB0, 64, 63]);
    assert!(!ch.params.mute.load(Relaxed));
    m.receive(&e, &[0xB0, 64, 127]);
    assert!(!ch.params.mute.load(Relaxed), "clamped to on");
    // CC 89 PAN INPUT1: 63 centre (steps 127, add 1, 0 is L63).
    m.receive(&e, &[0xB0, 89, 0]);
    assert_eq!(ch.params.pan.load(), -100.0);
    m.receive(&e, &[0xB0, 89, 63]);
    assert_eq!(ch.params.pan.load(), 0.0);
    // Table 5, CC 1 is EQ ATT H INPUT1: both bytes (15 x value + 84 of -960..120).
    let att = 0 - (-960); // 0 dB
    let cur = att * 15 + 84;
    m.receive(&e, &[0xB4, 1, (cur >> 7) as u8]);
    m.receive(&e, &[0xB4, 33, (cur & 0x7F) as u8]);
    assert!(ch.params.input.trim_db.load().abs() < 0.01);
    // CC changes are off by default on the console (Rx).
    let mut m2 = Midi::default();
    m2.receive(&e, &[0xB0, 64, 0]);
    assert!(!ch.params.mute.load(Relaxed));
    // No NRPN numbers are published: NRPN mode does nothing.
    m.setup.nrpn = true;
    m.receive(&e, &[0xB0, 64, 0]);
    assert!(!ch.params.mute.load(Relaxed));
}

#[test]
fn console_changes_go_out_as_cc_or_sysex() {
    let (e, mut m) = ready();
    let ch = e.strips(StripKind::Channel)[0].clone();
    // A fader with a control change: H then L on the table's MIDI channel.
    ch.params.fader_db.store(FADER[823]);
    m.poll(&e);
    assert_eq!(m.take_out(), Some(vec![0xB0, 1, 100]));
    assert_eq!(m.take_out(), Some(vec![0xB0, 33, 56]));
    assert_eq!(m.take_out(), None);
    // CC transmit off: a parameter change, on the Tx channel.
    m.setup.tx[CTL] = false;
    m.setup.tx_channel = 2;
    ch.params.fader_db.store(FADER[0]);
    m.poll(&e);
    let out = m.take_out().unwrap();
    assert_eq!(out[..9], [0xF0, 0x43, 0x12, GROUP, UNIVERSAL, 0x01, 0x1C, 0x00, 0x00]);
    assert_eq!(value_of(&out), 0);
    // Received values aren't sent back.
    let (tt, ee, pp) = address("kInputFader", "kFader");
    m.setup.rx_channel = 0;
    m.receive(&e, &change(tt, ee, pp, 0, 500));
    m.poll(&e);
    assert_eq!(m.take_out(), None);
    // Nothing at start, or with TX off (the initial data).
    let mut quiet = Midi::default();
    quiet.poll(&e);
    ch.params.mute.store(true, Relaxed);
    quiet.poll(&e);
    assert_eq!(quiet.take_out(), None);
    m.poll(&e);
    while m.take_out().is_some() {}
    // FADER LOW: the value is snapped to fours.
    m.setup.fader_low = true;
    ch.params.fader_db.store(FADER[501]);
    m.poll(&e);
    assert_eq!(value_of(&m.take_out().unwrap()), 500);
}

#[test]
fn program_changes_follow_the_table() {
    let (e, mut m) = ready();
    m.receive(&e, &[0xC0, 0]);
    assert_eq!(m.take_event(), Some(Event::RecallScene(1)));
    m.receive(&e, &[0xC0, 98]);
    assert_eq!(m.take_event(), Some(Event::RecallScene(99)));
    m.receive(&e, &[0xC0, 99]);
    assert_eq!(m.take_event(), Some(Event::RecallScene(0)), "program 100, scene 0");
    m.receive(&e, &[0xC0, 100]);
    assert_eq!(m.take_event(), None, "no assign");
    // Assign program 11 (10) to scene 5: a Setup parameter change.
    let (tt, ee, pp) = address("kSetupPGMChangeTable", "kPGMChangeTable");
    m.receive(&e, &change(tt, ee, pp, 10, 5));
    m.receive(&e, &[0xC0, 10]);
    assert_eq!(m.take_event(), Some(Event::RecallScene(5)));
    // A scene recalled: its lowest program number.
    m.scene_recalled(5);
    assert_eq!(m.take_out(), Some(vec![0xC0, 4]), "scene 5 on program 5");
    // Scene 40's program moved away: the function call goes out instead.
    m.pc_table.fill(100);
    m.scene_recalled(40);
    assert_eq!(m.take_out().unwrap(), vec![0xF0, 0x43, 0x10, GROUP, UNIVERSAL, 0x10, 0x00, 0, 40, 0x02, 0x00, 0xF7]);
}

#[test]
fn scenes_by_function_call() {
    let (e, mut m) = ready();
    // Recall scene 7, store 8: number High/Low, channel 256 is H 02 L 00.
    m.receive(&e, &[0xF0, 0x43, 0x10, GROUP, UNIVERSAL, 0x10, 0x00, 0, 7, 0x02, 0x00, 0xF7]);
    assert_eq!(m.take_event(), Some(Event::RecallScene(7)));
    m.receive(&e, &[0xF0, 0x43, 0x10, GROUP, UNIVERSAL, 0x10, 0x20, 0, 8, 0x02, 0x00, 0xF7]);
    assert_eq!(m.take_event(), Some(Event::StoreScene(8)));
}

#[test]
fn key_remote_on_and_layers() {
    let (e, mut m) = ready();
    let channels = e.strips(StripKind::Channel);
    let key = |h, m_, l, press| vec![0xF0, 0x43, 0x10, GROUP, MODEL, 0x20, h, m_, l, press as u8, 0xF7];
    // ON 1 on layer 1-24.
    m.receive(&e, &key(0x00, 0x00, 0x00, true));
    assert!(channels[0].params.mute.load(Relaxed));
    m.receive(&e, &key(0x00, 0x00, 0x00, false));
    assert!(channels[0].params.mute.load(Relaxed), "a release does nothing");
    // Layer 25-48, ON 2: CH26.
    m.receive(&e, &key(0x00, 0x10, 0x01, true));
    m.receive(&e, &key(0x00, 0x00, 0x01, true));
    assert_eq!(m.layer, 1);
    assert!(channels[25].params.mute.load(Relaxed));
    // SOLO 3.
    m.receive(&e, &key(0x00, 0x01, 0x02, true));
    assert!(channels[26].params.solo.load(Relaxed));
    // MASTER layer: ON 9 is AUX 1.
    m.receive(&e, &key(0x00, 0x10, 0x04, true));
    m.receive(&e, &key(0x00, 0x00, 0x08, true));
    assert!(e.bus_in_slot(8).unwrap().params.mute.load(Relaxed));
    // ON STEREO.
    m.receive(&e, &key(0x00, 0x00, 0x18, true));
    assert!(e.strips(StripKind::Main)[0].params.mute.load(Relaxed));
    // Every key reaches the surface.
    assert_eq!(m.take_event(), Some(Event::Key { h: 0, m: 0, l: 0, press: true }));
}

#[test]
fn meters_run_ten_seconds_and_stop() {
    let (e, mut m) = ready();
    let ch = e.strips(StripKind::Channel)[2].clone();
    ch.meters.output_peak[0].store(1.0);
    // Input channel meter, CH3, one meter.
    m.receive(&e, &[0xF0, 0x43, 0x30, GROUP, MODEL, 0x21, 0x00, 0x00, 0x02, 0x00, 0x01, 0xF7]);
    m.poll(&e);
    let msg = m.take_out().unwrap();
    assert_eq!(msg[..9], [0xF0, 0x43, 0x10, GROUP, MODEL, 0x21, 0x00, 0x00, 0x02]);
    assert_eq!(((msg[9] as u16) << 7 | msg[10] as u16), 0x0FFF, "0 dB");
    assert_eq!(msg.len(), 12);
    // Every poll for ten seconds (200 polls), then it stops.
    for _ in 0..199 {
        m.poll(&e);
    }
    let mut n = 0;
    while m.take_out().is_some() {
        n += 1;
    }
    assert_eq!(n, 199);
    m.poll(&e);
    assert_eq!(m.take_out(), None, "after ten seconds");
    // All: ALL INPUT, the channel meter of every channel.
    m.receive(&e, &[0xF0, 0x43, 0x30, GROUP, MODEL, 0x21, 0x00, 0x00, 0x7F, 0x00, 0x01, 0xF7]);
    m.poll(&e);
    assert_eq!(m.take_out().unwrap().len(), 10 + 96 * 2);
    // Disable: address UL 7F.
    m.receive(&e, &[0xF0, 0x43, 0x30, GROUP, MODEL, 0x21, 0x7F, 0x00, 0x00, 0x00, 0x00, 0xF7]);
    m.poll(&e);
    assert_eq!(m.take_out(), None);
}

#[test]
fn the_meter_scale() {
    assert_eq!(meter_value(1.0), 0x0FFF);
    assert_eq!(meter_value(1.5), 0x1FFF, "over");
    assert_eq!(meter_value(0.0), 0);
    assert_eq!(meter_value_db(-48.0), 0x0804);
    assert_eq!(meter_value_db(-96.0), 0x0084);
    assert_eq!(meter_value_db(-96.5), 0x0084);
    assert_eq!(meter_of_reduction(0.0), 0x0FFF, "no reduction");
    assert_eq!(meter_of_reduction(6.0), 0x0F00);
}

#[test]
fn mmc_goes_out_and_responses_are_noted() {
    let (e, mut m) = ready();
    m.mmc(0x01);
    assert_eq!(m.take_out(), Some(vec![0xF0, 0x7F, 0x7F, 0x06, 0x01, 0xF7]), "STOP");
    m.receive(&e, &[0xF0, 0x7F, 0x7F, 0x07, 0x4D, 0x01, 0x00, 0xF7]);
    assert_eq!(m.mmc_last, Some(0x4D), "RECORD STATUS");
}

#[test]
fn setup_bytes_round_trip_and_echo() {
    let s = Setup { rx_channel: 3, tx_channel: 9, nrpn: true, fader_low: true, ..Setup::default() };
    assert_eq!(Setup::from_bytes(&s.to_bytes()), s);
    let (e, mut m) = ready();
    m.setup.echo[PRM] = true;
    let msg = change(0x01, 0x1A, 0x00, 0, 0);
    m.receive(&e, &msg);
    assert_eq!(m.take_out(), Some(msg));
    // Setup by parameter change: the Rx channel (element 4E, parameter 2).
    let (tt, ee, pp) = address("kSetupMIDIRx", "kMIDIRxMidiCh");
    m.receive(&e, &change(tt, ee, pp, 0, 4));
    while m.take_out().is_some() {}
    assert_eq!(m.setup.rx_channel, 4);
    let mut other = change(tt, ee, pp, 0, 0);
    other[2] = 0x10;
    m.setup.echo[PRM] = false;
    m.receive(&e, &other);
    assert_eq!(m.setup.rx_channel, 4, "device 0 isn't the Rx channel any more");
}

#[test]
fn another_console_is_left_alone() {
    let e = Engine::new(0);
    e.configure(&ConsoleConfig::wing());
    let mut m = Midi::default();
    let before = e.strips(StripKind::Channel)[0].params.mute.load(Relaxed);
    m.setup.rx = [true; KINDS];
    m.receive(&e, &[0xB0, 64, 0]);
    assert_eq!(e.strips(StripKind::Channel)[0].params.mute.load(Relaxed), before);
    m.poll(&e);
    assert_eq!(m.take_out(), None);
}
