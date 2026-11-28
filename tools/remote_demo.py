#!/usr/bin/env python3
"""The remote screen's demo: its data, and the one-file page (docs/remote.md, "The demo").

  python3 tools/remote_demo.py capture http://127.0.0.1:8080
      Asks a running GigRig (Remote screen switched on, or started with GIGRIG_REMOTE=8080) for
      its state with each song loaded in turn, and writes remote/demo-data.js. Run it on a
      machine that has the songs and presets the demo should show. It loads every song, so
      never run it against a rig that is in use.

  python3 tools/remote_demo.py top-up http://127.0.0.1:8080
      For when RemoteApi::state gains a part and the self-test says the demo lacks it, and the
      machine at hand does not have Tom's songs: keeps everything remote/demo-data.js has and
      adds only the parts it lacks, from a running GigRig (any one, a fresh install will do).
      It also adds the description of every effect "Add an effect" offers, by adding each to
      the first chain for a moment, so never run it against a rig that is in use. And where the
      app now describes an effect differently (a setting gained, a range widened), the demo's
      description of it is replaced.

  python3 tools/remote_demo.py build out.html
      Joins remote/index.html and its files into one file with no outside links, set to run
      as the demo only: the page to publish or to open straight from a disk.

Only Python's own library is used.
"""
import json, pathlib, re, sys, urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent / "remote"


def ask(base, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(urllib.request.Request(base + path, data=data), timeout=20) as r:
        return json.loads(r.read().decode())


def capture(base):
    first = ask(base, "/api/state")["s"]
    songs, fx_sets = [], []
    types = dict(first["types"])

    def fx_index(effects):
        text = json.dumps(effects, sort_keys=True)
        if text not in fx_sets:
            fx_sets.append(text)
        return fx_sets.index(text)

    for i, song in enumerate(first["songs"]):
        answer = ask(base, "/api/cmd", {"op": "song.load", "index": i})
        if not answer.get("ok"):
            sys.exit(f"The rig would not load song {i}: {answer}")
        s = ask(base, "/api/state")["s"]
        types.update(s["types"])
        songs.append({"layout": s["layout"], "layoutOwn": s["layoutOwn"], "layoutWords": s["layoutWords"], "songStatus": s["songStatus"],
                      "songProblems": s["songProblems"], "switches": s["switches"], "fx": fx_index(s["effects"])})

    start = next((i for i, s in enumerate(first["songs"]) if s["name"] == "Wicked Game"), 0)
    ask(base, "/api/cmd", {"op": "song.load", "index": start})
    state = ask(base, "/api/state")["s"]
    state["types"] = types
    state["songNote"] = ""
    data = {"state": state, "songs": songs, "fxSets": [json.loads(t) for t in fx_sets]}
    text = json.dumps(data, separators=(",", ":"), ensure_ascii=False)
    (ROOT / "demo-data.js").write_text(
        "/* The demo's copy of a real rig's state. MADE BY tools/remote_demo.py capture: do not edit by hand. */\n"
        "window.GIGRIG_DEMO = " + text + ";\n"
        "window.GIGRIG_DEMO.songs.forEach(function (s) { s.effects = window.GIGRIG_DEMO.fxSets[s.fx]; });\n", encoding="utf-8")
    print(f"Wrote remote/demo-data.js: {len(first['songs'])} songs, {len(fx_sets)} different sets of sounds, {len(text) // 1024} KB")


HEAD = "/* The demo's copy of a real rig's state. MADE BY tools/remote_demo.py capture: do not edit by hand. */\n"
TAIL = "window.GIGRIG_DEMO.songs.forEach(function (s) { s.effects = window.GIGRIG_DEMO.fxSets[s.fx]; });\n"


def top_up(base):
    path = ROOT / "demo-data.js"
    text = path.read_text(encoding="utf-8")
    data = json.loads(text.split("window.GIGRIG_DEMO = ", 1)[1].rsplit(";\nwindow.GIGRIG_DEMO.songs", 1)[0])
    real = ask(base, "/api/state")["s"]
    added = []

    def fill(have, new, where):
        for key, value in new.items():
            if key in ("v", "t"):
                continue        # an effect's settings, keyed by ids that differ from effect to effect
            if key not in have:
                have[key] = value
                added.append(where + key)
            elif isinstance(value, dict) and isinstance(have[key], dict) and where + key != "types":
                fill(have[key], value, where + key + ".")
            elif isinstance(value, list) and value and isinstance(value[0], dict) and isinstance(have[key], list):
                for item in have[key]:
                    if isinstance(item, dict):
                        fill(item, value[0], where + key + "[].")

    fill(data["state"], real, "")

    # Every effect that can be added needs its description (its name and settings) in "types".
    types, described = data["state"]["types"], 0
    for entry in real.get("fxCatalogue", []):
        if entry["id"] in types:
            continue
        before = len(ask(base, "/api/state")["s"]["effects"]["chains"][0]["slots"])
        if not ask(base, "/api/cmd", {"op": "fx.add", "chain": 0, "type": entry["id"]}).get("ok"):
            sys.exit(f"The rig would not add {entry['id']} to describe it.")
        seen = ask(base, "/api/state")["s"]["types"]
        ask(base, "/api/cmd", {"op": "fx.remove", "chain": 0, "slot": before})
        if entry["id"] in seen:
            types[entry["id"]] = seen[entry["id"]]
            described += 1

    # An effect whose description changed in the app (a setting gained, a range widened) is described afresh.
    refreshed = []
    for kind, facts in ask(base, "/api/state")["s"]["types"].items():
        if kind in types and types[kind] != facts:
            types[kind] = facts
            refreshed.append(kind)

    out = json.dumps(data, separators=(",", ":"), ensure_ascii=False)
    path.write_text(HEAD + "window.GIGRIG_DEMO = " + out + ";\n" + TAIL, encoding="utf-8")
    print(f"Topped up remote/demo-data.js: added {sorted(set(added)) or 'nothing'}, described {described} more effects, "
          f"described afresh {sorted(refreshed) or 'none'}, {len(out) // 1024} KB")


def build(out):
    page = (ROOT / "index.html").read_text(encoding="utf-8")
    body = re.search(r"<body>(.*)</body>", page, re.S).group(1)
    title = re.search(r"<title>.*?</title>", page, re.S).group(0)

    def script(m):
        code = (ROOT / m.group(1)).read_text(encoding="utf-8").replace("</script", "<\\/script")
        return "<script>\n" + code + "\n</script>"

    body = re.sub(r'<script src="([^"]+)"></script>', script, body)
    # Demo-only extras (not in index.html, so not in the rig's own page): the gear look sample.
    extras = "".join("<script>\n" + f.read_text(encoding="utf-8").replace("</script", "<\\/script") + "\n</script>\n"
                     for f in [ROOT / "gear-sample.js"] if f.exists())
    body = body.replace("<script>GR.start();</script>", extras + "<script>window.GIGRIG_DEMO_ONLY = true; GR.start();</script>")
    css = (ROOT / "app.css").read_text(encoding="utf-8")
    html = title + '\n<meta name="claude:viewport-fit-sides" content="cover">\n<style>\n' + css + "\n</style>\n" + body.strip() + "\n"
    pathlib.Path(out).write_text(html, encoding="utf-8")
    print(f"Wrote {out}: {len(html) // 1024} KB")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "capture":
        capture(sys.argv[2].rstrip("/"))
    elif len(sys.argv) == 3 and sys.argv[1] == "top-up":
        top_up(sys.argv[2].rstrip("/"))
    elif len(sys.argv) == 3 and sys.argv[1] == "build":
        build(sys.argv[2])
    else:
        sys.exit(__doc__)
