#!/usr/bin/env python3
"""Generates rust/crates/livedaw-dm2000/src/tables.rs from Yamaha's DM2000 V2 MIDI
specification (`DM2000 V2 Parameter Change List.xls`, kept outside the repository)
and the Control Change table of the V2 Owner's Manual (pp.353-368, as plain text).

    pip install --target <dir> xlrd
    python -X utf8 -I gen_midi_tables.py --pylib <dir> <spec.xls> <cc-pages.txt> <tables.rs>

`cc-pages.txt` is the text of those pages, each table cell on a line of its own
(any PDF text extractor gives that).

Only what interoperability needs comes out: parameter names, addresses and
ranges, and the fader and meter value tables. Run it again if ELEMENTS changes.
"""
import argparse
import re
import sys

# The elements Sidecar's engine answers (midi.rs maps each parameter to a field).
ELEMENTS = {
    "kInputPhase", "kInputInsert", "kInputChannelOn", "kInputChannelPan", "kInputFader", "kInputAttenuator",
    "kInputGate", "kInputComp", "kInputEQ", "kInputDelay", "kInputRouting", "kInputAUX", "kInputAUXPan", "kInputGroup",
    "kBusInsert", "kBusChannelOn", "kBusFader", "kBusAttenuator", "kBusComp", "kBusEQ", "kBusDelay", "kBusToMatrix",
    "kBusToMatrixPan", "kBusToStereo", "kBusOutGroup",
    "kAUXInsert", "kAUXChannelOn", "kAUXFader", "kAUXAttenuator", "kAUXComp", "kAUXEQ", "kAUXDelay", "kAUXToMatrix",
    "kAUXToMatrixPan", "kAUXOutGroup",
    "kMatrixInsert", "kMatrixChannelOn", "kMatrixBalance", "kMatrixFader", "kMatrixAttenuator", "kMatrixComp", "kMatrixEQ",
    "kMatrixDelay", "kMatrixOutGroup",
    "kStereoInsert", "kStereoChannelOn", "kStereoBalance", "kStereoFader", "kStereoAttenuator", "kStereoComp", "kStereoEQ",
    "kStereoDelay", "kStereoToMatrix", "kStereoToMatrixPan", "kStereoOutGroup",
    "kEffect",
    "kInFaderGrpMaster", "kInMuteGrpMaster", "kOutFaderGrpMaster", "kOutMuteGrpMaster",
    "kChannelInput", "kSlotOutput", "kOmniOutput", "kStereoOut", "kDigital2trOut",
    "kInputChannelName", "kBusModuleName", "kAUXModuleName", "kMatrixModuleName", "kStereoOutModuleName",
    "kSetupMIDIRx", "kSetupMIDITx", "kSetupMIDITxEnable", "kSetupMIDIRxEnable", "kSetupMIDIOmniEnable",
    "kSetupMIDIEchoEnable", "kSetupMIDIControl", "kSetupPGMChangeTable", "kSetupMIDISetup",
}

# Control Change table names (HIGH column, then MID if needed) -> kinds in midi.rs.
CC_KINDS = {("FADER H",): 1, ("FADER L",): 2, ("ON",): 3, ("PAN",): 4, ("EQ", "ATT H"): 5, ("EQ", "ATT L"): 6, ("EQ", "ON"): 7}


def cell(sheet, r, c):
    return sheet.cell_value(r, c) if c < sheet.ncols else ""


def num(x):
    return int(x) if isinstance(x, float) and x == int(x) else x


def hexbyte(s):
    return int(str(s).strip(), 16)


def param_rows(book):
    sheet = book.sheet_by_name("Parameter Change List")
    defs = []
    element = None
    element_ch = 0
    for r in range(sheet.nrows):
        name = cell(sheet, r, 1)
        if isinstance(name, str) and name.startswith("k"):
            element = name
            element_ch = cell(sheet, r, 3)
            continue
        param = cell(sheet, r, 4)
        if not isinstance(param, str) or not param.startswith("k") or element not in ELEMENTS:
            continue
        if cell(sheet, r, 10) != "F0":
            continue
        tt, ee, pp = hexbyte(cell(sheet, r, 15)), hexbyte(cell(sheet, r, 16)), hexbyte(cell(sheet, r, 17))
        model = str(cell(sheet, r, 14)).strip().upper()
        minv, maxv, default = num(cell(sheet, r, 6)), num(cell(sheet, r, 7)), num(cell(sheet, r, 8))
        tx = cell(sheet, r, 34) == "O"
        rx = cell(sheet, r, 35) == "O"
        defs.append((element, param, model == "7F", tt, ee, pp, int(element_ch), int(minv), int(maxv), int(default), tx, rx))
    return defs


def prm_table(book, tag, length):
    """A PRM TABLE's Value/Data columns: value -> text, `length` values long."""
    sheet = book.sheet_by_name("PRM TABLE")
    start = None
    for r in range(sheet.nrows):
        s = cell(sheet, r, 0)
        if isinstance(s, str) and s.strip() == "PRM TABLE " + tag:
            start = r
            break
    assert start is not None, tag
    out = {}
    for r in range(start + 2, sheet.nrows):
        s = cell(sheet, r, 0)
        if isinstance(s, str) and s.startswith("PRM TABLE"):
            break
        for c in range(1, sheet.ncols - 1):
            v, t = cell(sheet, r, c), cell(sheet, r, c + 1)
            if isinstance(v, float) and t != "" and v == int(v):
                out.setdefault(int(v), str(t).strip())
    assert len(out) >= length, (tag, len(out))
    return [out[k] for k in sorted(out)][:length]


def db(s):
    if "Inf" in s:
        return -144.0  # livedaw_dsp::fader::FADER_MIN: silence
    return float(re.sub(r"[^0-9.+\-]", "", s))


def fmt(v):
    s = ("%.2f" % v).rstrip("0").rstrip(".")
    return s if "." in s else s + ".0"


def chunk(items, n):
    return [" ".join(x + "," for x in items[i: i + n]) for i in range(0, len(items), n)]


def cc_table(path):
    lines = [l.strip() for l in open(path, encoding="utf8") if l.strip()]
    tables = {}
    channel = None
    i = 0
    stop = {"#", "HIGH", "MID", "LOW"}

    def is_num(s):
        return re.fullmatch(r"\d{1,3}", s) is not None

    def junk(s):
        return s.startswith("=====") or "Owner" in s or s.startswith("Initial Parameter") or s.startswith("Appendix")

    while i < len(lines):
        s = lines[i]
        m = re.fullmatch(r"CHANNEL\s*(\d+)", s)
        if m and i + 1 < len(lines) and lines[i + 1] == "#":
            channel = int(m.group(1))
            tables[channel] = {}
            i += 1
            continue
        if channel is None or junk(s) or s in stop or not is_num(s):
            i += 1
            continue
        k = int(s)
        j = i + 1
        cells = []
        while j < len(lines) and not is_num(lines[j]) and not junk(lines[j]) and lines[j] not in stop \
                and not re.fullmatch(r"CHANNEL\s*\d+", lines[j]):
            cells.append(lines[j])
            j += 1
        tables[channel][k] = cells
        i = j
    assert sorted(tables) == list(range(1, 17)), sorted(tables)
    return tables


def target_code(name):
    m = re.fullmatch(r"INPUT(\d+)", name)
    if m:
        return int(m.group(1)) - 1
    for prefix, base, count in (("BUS", 96, 8), ("AUX", 104, 12), ("MATRIX", 116, 4)):
        m = re.fullmatch(prefix + r"(\d+)", name)
        if m:
            n = int(m.group(1))
            assert 1 <= n <= count, name
            return base + n - 1
    assert name == "STEREO", name
    return 120


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pylib", help="a folder holding xlrd")
    ap.add_argument("spec")
    ap.add_argument("cc_text")
    ap.add_argument("out")
    args = ap.parse_args()
    if args.pylib:
        sys.path.insert(0, args.pylib)
    import xlrd

    book = xlrd.open_workbook(args.spec)
    defs = param_rows(book)
    fader = [db(x) for x in prm_table(book, "#05-2", 1024)]
    bus = [db(x) for x in prm_table(book, "#15-2", 1024)]
    eq_f = prm_table(book, "#12", 128)
    meters = book.sheet_by_name("METER DATA")
    meter = {}
    for r in range(meters.nrows):
        for c in (0, 3):
            m = re.match(r"\s*(-?\d+)\[dB\]", str(cell(meters, r, c)))
            val = cell(meters, r, c + 1)
            if m and isinstance(val, str) and val.startswith("0x"):
                meter[int(m.group(1))] = int(val, 16)
    assert sorted(meter) == list(range(-96, 1)), len(meter)
    # The breakpoints the laws in livedaw-dsp/src/fader.rs are built from.
    print("channel:", [(k, fader[k]) for k in (1, 15, 33, 223, 423, 1023)])
    print("output :", [(k, bus[k]) for k in (1, 3, 35, 83, 223, 423, 1023)])

    cc = cc_table(args.cc_text)
    out = []
    w = out.append
    w("// @generated by tools/dm2000/gen_midi_tables.py from Yamaha's DM2000 V2 MIDI")
    w("// specification (parameter names, addresses and ranges for interoperability) and")
    w("// the V2 Owner's Manual's initial Control Change table. Do not edit by hand.")
    w("")
    w("/// A parameter of the parameter change list: its element and parameter names, its")
    w("/// address (`F0 43 1n 3E m tt ee pp cc ...`), the highest channel, its range and")
    w("/// initial value, and whether the console transmits (`tx`) and receives it.")
    w("#[derive(Clone, Copy, Debug)]")
    w("pub struct Def {")
    w("    pub element: &'static str,")
    w("    pub name: &'static str,")
    w("    /// `3E 7F` (the general digital mixer format), else `3E 06`.")
    w("    pub universal: bool,")
    w("    pub tt: u8,")
    w("    pub ee: u8,")
    w("    pub pp: u8,")
    w("    pub max_ch: u16,")
    w("    pub min: i32,")
    w("    pub max: i32,")
    w("    pub default: i32,")
    w("    pub tx: bool,")
    w("    pub rx: bool,")
    w("}")
    w("")
    w("pub static DEFS: &[Def] = &[")
    for (element, name, universal, tt, ee, pp, ch, mn, mx, de, tx, rx) in defs:
        w('    Def { element: "%s", name: "%s", universal: %s, tt: 0x%02X, ee: 0x%02X, pp: 0x%02X, max_ch: %d, min: %d, max: %d, default: %d, tx: %s, rx: %s },'
          % (element, name, str(universal).lower(), tt, ee, pp, ch, mn, mx, de, str(tx).lower(), str(rx).lower()))
    w("];")
    w("")
    w("/// PRM TABLE #05-2: the channel faders' 10 bits, in dB (0 is -oo: `FADER_MIN`).")
    w("pub static FADER: [f32; 1024] = [")
    for line in chunk([fmt(x) for x in fader], 16):
        w("    " + line)
    w("];")
    w("")
    w("/// PRM TABLE #15-2: the output faders' 10 bits, in dB (0 is -oo: `FADER_MIN`).")
    w("pub static BUS_FADER: [f32; 1024] = [")
    for line in chunk([fmt(x) for x in bus], 16):
        w("    " + line)
    w("];")
    w("")
    w("/// PRM TABLE #12: the EQ's frequencies in Hz, by parameter value (0-127).")
    w("pub static EQ_FREQ: [f32; 128] = [")
    values = []
    for x in eq_f:
        hz = float(re.sub(r"[^0-9.]", "", x)) * (1000.0 if "kHz" in x else 1.0)
        values.append(fmt(hz))
    for line in chunk(values, 16):
        w("    " + line)
    w("];")
    w("")
    w("/// METER DATA: the meter value of -96 dB to 0 dB, a dB apart (CLIP is 0x1FFF).")
    w("pub static METER: [u16; 97] = [")
    for line in chunk(["0x%04X" % meter[d] for d in range(-96, 1)], 16):
        w("    " + line)
    w("];")
    w("")
    w("/// A Control Change's assignment (the initial table, Owner's Manual pp.353-368).")
    w("#[derive(Clone, Copy, Debug, PartialEq, Eq)]")
    w("pub struct CcAssign {")
    w("    /// 0 nothing, 1 FADER H, 2 FADER L, 3 ON, 4 PAN, 5 EQ ATT H, 6 EQ ATT L, 7 EQ ON.")
    w("    pub kind: u8,")
    w("    /// 0-95 INPUT, 96-103 BUS, 104-115 AUX, 116-119 MATRIX, 120 STEREO.")
    w("    pub target: u8,")
    w("}")
    w("")
    w("const X: CcAssign = CcAssign { kind: 0, target: 0 };")
    w("macro_rules! a {")
    w("    ($k:expr, $t:expr) => {")
    w("        CcAssign { kind: $k, target: $t }")
    w("    };")
    w("}")
    w("")
    w("/// The initial table: 16 tables (one per MIDI channel) of Control Changes 0-119.")
    w("pub static CC_INITIAL: [[CcAssign; 120]; 16] = [")
    for channel in range(1, 17):
        row = []
        for n in range(120):
            cells = cc[channel].get(n, [])
            if len(cells) == 3:
                key = (cells[0],) if cells[1] in ("CHANNEL", "MASTER") else (cells[0], cells[1])
                kind = CC_KINDS.get(key)
                if kind is not None:
                    row.append("a!(%d, %d)" % (kind, target_code(cells[2])))
                    continue
            row.append("X")
        w("    [")
        for line in chunk(row, 24):
            w("        " + line)
        w("    ],")
    w("];")
    with open(args.out, "w", encoding="utf8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("wrote", args.out, len(defs), "defs")


if __name__ == "__main__":
    main()
