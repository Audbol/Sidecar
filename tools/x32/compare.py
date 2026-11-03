"""Compares Sidecar's X32 OSC replies with a real X32's, read-only.

Asks both for the same parameter groups (`/node`, nothing is set) and prints
where the text differs. A console's own settings (names, routing, scenes
loaded) differ from a fresh Sidecar's factory state, so read the report with
the console's state in mind.

    cargo run -p livedaw-x32 --example serve -- --port 10024
    python -I tools/x32/compare.py --console 192.168.1.151 --sidecar 127.0.0.1:10024 [--paths FILE]
"""
import argparse
import socket


def osc_string(s):
    b = s.encode() + b"\0"
    return b + b"\0" * (-len(b) % 4)


def node(sock, addr, path):
    for _ in range(3):
        sock.sendto(osc_string("/node") + osc_string(",s") + osc_string(path), addr)
        try:
            data, _ = sock.recvfrom(16384)
        except socket.timeout:
            continue
        parts = [p for p in data.split(b"\0") if p and not p.startswith(b",") and p != b"node"]
        return parts[-1].decode(errors="replace").rstrip("\n") if parts else None
    return None


def default_paths():
    paths = ["config/linkcfg", "config/mono", "config/solo", "config/chlink", "config/auxlink", "config/fxlink",
             "config/buslink", "config/mtxlink", "config/mute"]
    subs = {
        "ch": (32, ["config", "delay", "preamp", "gate", "gate/filter", "dyn", "dyn/filter", "insert", "eq",
                    "eq/1", "eq/2", "eq/3", "eq/4", "mix", "grp", "automix", "mix/01", "mix/02"]),
        "auxin": (8, ["config", "preamp", "eq", "eq/1", "eq/4", "mix", "grp", "mix/01"]),
        "fxrtn": (8, ["config", "eq", "eq/1", "mix", "grp", "mix/01"]),
        "bus": (16, ["config", "dyn", "dyn/filter", "insert", "eq", "eq/1", "eq/6", "mix", "grp", "mix/01"]),
        "mtx": (6, ["config", "preamp", "dyn", "insert", "eq", "eq/1", "eq/6", "mix", "grp"]),
    }
    for kind, (n, ss) in subs.items():
        for i in range(1, n + 1):
            paths += [f"{kind}/{i:02d}/{s}" for s in ss]
    for m in ["st", "m"]:
        paths += [f"main/{m}/{s}" for s in ["config", "dyn", "insert", "eq", "eq/1", "eq/6", "mix", "mix/01"]]
    paths += [f"dca/{i}" for i in range(1, 9)] + [f"dca/{i}/config" for i in range(1, 9)]
    return paths


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--console", required=True)
    ap.add_argument("--sidecar", default="127.0.0.1:10024")
    ap.add_argument("--paths")
    a = ap.parse_args()
    host, port = a.sidecar.split(":")
    console, sidecar = (a.console, 10023), (host, int(port))
    paths = [l.strip() for l in open(a.paths) if l.strip()] if a.paths else default_paths()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.5)
    same = differ = 0
    for p in paths:
        x, s = node(sock, console, p), node(sock, sidecar, p)
        if x == s:
            same += 1
        else:
            differ += 1
            print(f"{p}\n  X32:     {x}\n  Sidecar: {s}")
    print(f"{same} the same, {differ} different, of {len(paths)}")


if __name__ == "__main__":
    main()
