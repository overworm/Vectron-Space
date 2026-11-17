#!/usr/bin/env python3
"""stage_music.py - music of maps/stage1.json -> snd/music.it, action.it, boss.it
"lib:..." keeps the built-in module of that role; "file:name.xm" (maps/music/) is converted with tools/mod2it.py (all orders)."""
import json, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
mj = os.environ.get("STAGE_JSON") or os.path.join(ROOT, "maps", "stage1.json")
if not os.path.exists(mj):
    sys.exit(0)
mus = json.load(open(mj)).get("music") or {}
BUDGET = {"calm": 40000, "action": 55000, "boss": 50000}
OUT = {"calm": "music.it", "action": "action.it", "boss": "boss.it"}
for role in ("calm", "action", "boss"):
    v = mus.get(role, "")
    if not v.startswith("file:"):
        continue
    src = os.path.join(ROOT, "maps", "music", v[5:])
    n = 1
    try:
        sys.path.insert(0, HERE)
        import mod2it
        n = len(mod2it.load(src)["orders"])
    except Exception as e:
        sys.exit("cannot read %s: %s" % (src, e))
    out = os.path.join(ROOT, "snd", OUT[role])
    cmd = [sys.executable, os.path.join(HERE, "mod2it.py"), src, out, "0-%d" % (n - 1), str(BUDGET[role]), role, "14",
           "melodic" if role == "calm" else "all", "8"]
    if role == "boss": cmd[4] = "0-17"; cmd[5] = "38000"
    print(" ".join(cmd))
    subprocess.check_call(cmd)
