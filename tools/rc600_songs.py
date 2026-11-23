#!/usr/bin/env python3
"""Turns an RC-600 backup's DATA folder into the song table in src/TestSongs.cpp.

    python3 tools/rc600_songs.py <path to DATA folder> [memory numbers to leave out]  >  songs.txt

Prints one entry per used memory (a memory whose name is not the factory "Memory NN" and that is
not the MIDI CTRL memory), in the layout of rawSongs[]. Paste the output over the copied songs.
Of each MEMORYnnnA/B pair the file with the higher count at its end is the live one.
"""
import re, sys, pathlib

def live(folder, n):
    best, text = -1, None
    for side in "AB":
        p = folder / f"MEMORY{n:03d}{side}.RC0"
        if not p.exists():
            continue
        t = p.read_text(encoding="utf-8", errors="replace")
        m = re.search(r"<count>([0-9A-Fa-f]+)</count>", t)
        c = int(m.group(1), 16) if m else 0
        if c > best:
            best, text = c, t
    return text

def sec(t, name):
    m = re.search(rf"^<{name}>\n(.*?)^</{name}>", t, re.S | re.M)
    return {k: int(v) for k, v in re.findall(r"<([A-Z]+)>(-?\d+)</\1>", m.group(1))} if m else {}

def main():
    folder = pathlib.Path(sys.argv[1])
    leave_out = {int(a) for a in sys.argv[2:]}
    for n in range(1, 100):
        if n in leave_out:
            continue
        t = live(folder, n)
        if t is None:
            continue
        name = "".join(chr(v) for v in sec(t, "NAME").values()).strip()
        if re.fullmatch(r"Memory\s*\d+", name) or name in ("", "MIDI CTRL"):
            continue
        master, rec = sec(t, "MASTER"), sec(t, "REC")
        tracks = []
        for i in range(1, 7):
            tr = sec(t, f"TRACK{i}")
            tracks.append(f"{{ {tr['Q']}, {tr['S']}, {tr['F']}, {tr['D']}, {tr.get('Y', 2)} }}")
        banks = [", ".join(str(sec(t, f"ICTL{b}_PEDAL{p}")["A"]) for p in range(1, 10)) for b in (1, 2)]
        assigns = []
        for i in range(1, 17):
            a = sec(t, f"ASSIGN{i}")
            if a.get("A") == 1:
                assigns.append(f"{{ {a['B']}, {a['G']} }}")
        esc = name.replace("\\", "\\\\").replace('"', '\\"')
        print(f'    {{ "{esc}", {n}, {master["A"] / 10:.1f}, {"true" if rec["A"] == 1 else "false"}, {"true" if rec["B"] == 1 else "false"},')
        print(f"      {{ {{ {', '.join(tracks)} }} }},")
        print(f"      {{ {banks[0]} }},")
        print(f"      {{ {banks[1]} }},")
        print(f"      {{ {{ {', '.join(assigns)} }} }}, {len(assigns)} }},")

if __name__ == "__main__":
    main()
