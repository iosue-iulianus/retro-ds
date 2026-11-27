#!/usr/bin/env python3
"""The second screens' regression check: both games' harness checks in one run.

Runs the harness (build/host/retrods-harness) on the test states and compares
what RetroDS draws with the game's own screens and with the Python
references, as each check in docs/games/*.md was first made. Run it before
merging a branch that touches a second screen (scripts/check-games.sh
builds the harness first).

    check_games.py --alttp ROM --sm ROM --ff6 ROM [--data DIR] [--out DIR] [--only NAME,...]

--data: the test states (default ../test-data beside the repo); --out: where
the harness writes (default a new folder in /tmp, removed when everything
passes). A game whose ROM isn't given is skipped. Exits 1 if any check
fails.

A Link to the Past:
  items      the items screen's tilemap word for word as the game's
             (alttp_items.py --check), and the harness's picture of it the
             same as the Python one (three sets of items)
  dungeon    the dungeon map's tilemaps and sprites as the game's, and
             RetroDS's picture pixel for pixel as the game's screen
  marker     Link's marker: his pixels the same as on the game's picture,
             walking every way (Light and Dark World)
  pieces     the GPU map pieces (levels 0 to 2) the same as the map
             composed whole, away from the fog's edge (which only the
             pieces draw), with the owner's seen record from the Duo and
             a walk in each world
  writes     every item select's writes on the game's list; a write off it
             refused, nothing written
Super Metroid:
  smmap      the map as RetroDS draws it pixel for pixel as the game's pause
             screen (its frame and map; not the game's scroll arrows), and
             sm_map.py's reference too
  smpage     the equipment page as the game's but for its cursor
  smwrites   every item switch's writes on the game's list, twice; none
             while paused (SM-10's gate)
Final Fantasy VI (FF3 USA 1.0):
  ff6party   the party screen RetroDS draws pixel for pixel as the game's
             main menu (seven saves; not the menu's command list, which it
             leaves out, nor the clock's minute, which ticks while the menu
             opens)
  ff6menus   a tap on each command opens the game's own menu for it, and
             the lists read as the game holds them (the commands put in the
             battle's command list where the test party hasn't them)
  ff6acts    taps that act: Fire's damage, Cure's healing, each Blitz
             queued as itself, Row and Def. queued, a target the game's
             rules don't allow refused
  ff6pad     the pad on each battle page moves the game's hand the way it
             was pressed on the page as drawn (the game's cursor drives it):
             the command column and Row / Def. (Window set), the Short set's
             cross, each list (Magic and its esper, Item, Lore, Rage, Dance,
             Magitek, Throw, Tools) scrolling a line at a time
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HARNESS = os.path.join(REPO, "build", "host", "retrods-harness")
CORE = os.path.join(REPO, "build", "host", "bsnes_libretro.so")

results = []


def report(name, ok, detail):
    results.append((name, ok))
    print("%-8s %-4s %s" % (name, "ok" if ok else "FAIL", detail), flush=True)


def harness(rom, out, lines, tag):
    os.makedirs(out, exist_ok=True)
    script = os.path.join(out, tag + ".txt")
    with open(script, "w") as f:
        f.write("\n".join(lines) + "\n")
    r = subprocess.run([HARNESS, CORE, rom, out, script], capture_output=True, text=True)
    text = r.stdout + r.stderr
    with open(os.path.join(out, tag + ".log"), "w") as f:
        f.write(text)
    if r.returncode != 0:
        raise RuntimeError("harness %s exited %d (see %s.log)" % (tag, r.returncode, tag))
    return text


def png(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(int)


def tool(*args):
    r = subprocess.run([sys.executable] + list(args), capture_output=True, text=True)
    return r.returncode, (r.stdout + r.stderr).strip()


# ---- A Link to the Past ----

ITEM_STATES = ["harness-opening-rain-outside", "harness-pendants-outside", "harness-dw-pyramid-free"]
DUNGEON_STATES = ["harness-eastern-palace-dungeon-map-open", "harness-eastern-palace-dungeon-map-open-with-map-compass"]
MARKER_STATES = ["harness-lw-day-outside", "harness-pendants-outside", "harness-dw-pyramid-free"]
WALK = ["LEFT"] * 3 + ["DOWN"] * 3 + ["RIGHT"] * 3 + ["UP"] * 3 + ["-"]
PIECE = 512   # world pixels a piece
CELL = 16     # world pixels a seen square
PIECE_STATES = ["harness-lw-day-outside", "harness-dw-pyramid-free"]  # a state in each world
PIECE_WALK = ["LEFT", "LEFT", "DOWN", "DOWN", "RIGHT", "RIGHT", "RIGHT", "RIGHT", "UP", "UP"]  # (adds ground the record lacks)
SEEN_RECORD = "duo-seen-2026-11-23.alttp1"  # the owner's seen record (file 1), from the Duo
FOG_M = 3     # squares round a seen one the fog's edge can reach (alttp.c: FOG_REACH + 1)


def alttp_items(rom, data, out):
    lines = []
    for s in ITEM_STATES:
        lines += ["load %s/%s.state" % (data, s), "alttp on", "hold START 3", "wait 60",
                  "dumpram it-" + s, "dumpvram it-" + s, "alttp items itc-" + s]
    harness(rom, out, lines, "items")
    for s in ITEM_STATES:
        base = os.path.join(out, "it-" + s)
        rc, text = tool(os.path.join(REPO, "tools", "alttp_items.py"), rom, "--check", base)
        last = text.splitlines()[-1] if text else ""
        report("items", rc == 0, "%s: %s" % (s, last))
        rc2, text2 = tool(os.path.join(REPO, "tools", "alttp_items.py"), rom, base, base + "-py.png", "--layout", "touch")
        a, b = png(os.path.join(out, "itc-" + s + ".png")), png(base + "-py.png")
        n = int((a != b).any(2).sum()) if a.shape == b.shape else -1
        report("items", rc2 == 0 and n == 0, "%s: picture vs alttp_items.py, %d pixels differ" % (s, n))


def alttp_dungeon(rom, data, out):
    lines = []
    for s in DUNGEON_STATES:
        lines += ["load %s/%s.state" % (data, s), "alttp on", "wait 2",
                  "dumpram du-" + s, "dumpvram du-" + s, "shot du-" + s, "alttp dungeon duc-" + s]
    harness(rom, out, lines, "dungeon")
    for s in DUNGEON_STATES:
        base = os.path.join(out, "du-" + s)
        rc, text = tool(os.path.join(REPO, "tools", "alttp_dungeon.py"), rom, "--check", base, "--shot", base + ".png")
        report("dungeon", rc == 0, "%s: alttp_dungeon.py %s" % (s, "; ".join(text.splitlines())))
        a, b = png(os.path.join(out, "duc-" + s + ".png")), png(base + ".png")
        n = int((a != b).any(2).sum()) if a.shape == b.shape else -1
        report("dungeon", n == 0, "%s: RetroDS's picture vs the game's, %d pixels differ" % (s, n))


def alttp_marker(rom, data, out):
    lines = []
    for s in MARKER_STATES:
        lines += ["load %s/%s.state" % (data, s), "alttp on", "wait 3"]
        for i, d in enumerate(WALK):
            lines += ["hold %s 4" % d, "alttp marker mk-%s-%d" % (s, i), "shot ms-%s-%d" % (s, i)]
    text = harness(rom, out, lines, "marker")
    places = re.findall(r"alttp marker \S+/mk-(\S+)-(\d+)\.png \(on the picture at (-?\d+),(-?\d+)\)", text)

    def compare(m, shot, ox, oy):
        """(same, differ): the marker's pixels (black ones aren't Link's) on the picture at (ox, oy)"""
        h, w = shot.shape[:2]
        same = bad = 0
        for y in range(32):
            for x in range(32):
                c = m[y, x]
                if not c.any():
                    continue
                X, Y = ox + x, oy + y
                if 0 <= X < w and 0 <= Y < h and (shot[Y, X] == c).all():
                    same += 1
                else:
                    bad += 1
        return same, bad

    for s in MARKER_STATES:
        frames = [(int(i), int(x), int(y)) for st, i, x, y in places if st == s]
        bad = same = moved = 0
        for i, ox, oy in frames:
            m = png(os.path.join(out, "mk-%s-%d.png" % (s, i)))
            shot = png(os.path.join(out, "ms-%s-%d.png" % (s, i)))
            k, b = compare(m, shot, ox, oy)
            if b:
                # (the harness places the marker from this frame's Link and
                # camera, the picture is a frame behind: while the camera
                # scrolls he can be a pixel or two off; his pixels are what's checked)
                k, b = max((compare(m, shot, ox + dx, oy + dy) for dy in range(-2, 3) for dx in range(-2, 3)), key=lambda r: r[0] - r[1])
                moved += 1
            same += k
            bad += b
        ok = len(frames) == len(WALK) and bad == 0 and same >= 100 * len(WALK)
        report("marker", ok, "%s: %d frames (%d placed within 2 px), %d of Link's pixels as the game's, %d differ" % (s, len(frames), moved, same, bad))


def alttp_pieces(rom, data, out):
    g = PIECE // CELL        # squares a piece across
    for world, state in enumerate(PIECE_STATES):
        start = ["load %s/%s.state" % (data, state), "alttp on", "wait 1", "alttp load %s/%s" % (data, SEEN_RECORD)]
        start += ["hold %s 40" % d for d in PIECE_WALK]
        text = harness(rom, out, start + ["alttp seen sn-%d %d 0 0 256 256" % (world, world)], "seen-%d" % world)
        if "alttp load" not in text or ": ok" not in text.split("alttp load", 1)[1].splitlines()[0]:
            raise RuntimeError("the seen record %s didn't load" % SEEN_RECORD)
        seen = np.pad(png(os.path.join(out, "sn-%d.png" % world))[..., 0] > 0, FOG_M)
        # a square is clear of the fog's edge when every square within FOG_M of it is seen
        n = seen.shape[0] - 2 * FOG_M
        clear = np.ones((n, n), bool)
        for dy in range(-FOG_M, FOG_M + 1):
            for dx in range(-FOG_M, FOG_M + 1):
                clear &= seen[FOG_M + dy:FOG_M + dy + n, FOG_M + dx:FOG_M + dx + n]
        pieces = [i for i in range(64) if clear[(i // 8) * g:(i // 8 + 1) * g, (i % 8) * g:(i % 8 + 1) * g].any()]
        lines = list(start)
        for i in pieces:
            for level in range(3):
                k = PIECE >> level
                lines.append("alttp piece pc-%d-%d-%d %d %d %d" % (world, i, level, world, i, level))
                lines.append("alttp map mp-%d-%d-%d %d %d %d %d %d" % (world, i, level, 1 << level, (i % 8) * PIECE, (i // 8) * PIECE, k, k))
        harness(rom, out, lines, "pieces-%d" % world)
        compared, bad = [0, 0, 0], []
        for i in pieces:
            c = clear[(i // 8) * g:(i // 8 + 1) * g, (i % 8) * g:(i % 8 + 1) * g]
            for level in range(3):
                a = png(os.path.join(out, "pc-%d-%d-%d.png" % (world, i, level)))
                b = png(os.path.join(out, "mp-%d-%d-%d.png" % (world, i, level)))
                mask = np.kron(c, np.ones((CELL >> level, CELL >> level), bool))
                # level 0 is exact; averaged levels may round differently (by 2 at most)
                tol = 0 if level == 0 else 2
                d = int(((np.abs(a - b) > tol).any(2) & mask).sum())
                compared[level] += int(mask.sum())
                if d:
                    bad.append("piece %d level %d: %d" % (i, level, d))
        report("pieces", not bad and len(pieces) > 0,
               "%s world, %d pieces, pixels clear of the fog compared at levels 0/1/2: %s; %s" % (
                   ("Light", "Dark")[world], len(pieces), "/".join(map(str, compared)), "; ".join(bad) if bad else "none differ"))


def alttp_writes(rom, data, out):
    lines = ["load %s/harness-pendants-outside.state" % data, "alttp on", "wait 2"]
    lines += ["alttp select %d" % i for i in range(1, 21)]
    lines += ["wait 2", "alttp offlist"]
    text = harness(rom, out, lines, "writes")
    sel = re.findall(r"alttp select (\d+): (\d+) byte\(s\) written(.*)", text)
    refused = [i for i, n, rest in sel if "REFUSED" in rest]
    wrote = [i for i, n, rest in sel if int(n) > 0]
    report("writes", len(sel) == 20 and not refused and len(wrote) >= 10,
           "alttp select 1-20: %d selected, %d refused%s" % (len(wrote), len(refused), " (" + ",".join(refused) + ")" if refused else ""))
    m = re.search(r"alttp offlist: (\S+); \$7E0010 (\S+), \$7E0202 (\S+)", text)
    report("writes", bool(m) and m.groups() == ("refused", "unchanged", "unchanged"),
           "alttp offlist: " + (" ".join(m.groups()) if m else "no output"))


# ---- Super Metroid ----

# (state, paused by START first): the opening's, and two of the owner's with the map station's reveal
SM_MAP_STATES = [("crateria-pause-map", False), ("duo-slot0-mapstation-2026-11-21", False), ("duo-auto-latest", True)]
# the game's scroll arrows (x0, y0, x1, y1 on RetroDS's picture): RetroDS's map is dragged instead
SM_ARROWS = [(118, 11, 137, 23), (120, 140, 135, 150), (11, 74, 21, 89), (234, 74, 245, 91)]
# all items and beams collected; some on (Varia, Spring Ball off; Charge, Wave)
SM_ITEMS = ["poke 7E09A4 2 F32F", "poke 7E09A2 2 F32D", "poke 7E09A8 2 100F", "poke 7E09A6 2 1009"]
SM_TOGGLES = [(1, i) for i in range(5)] + [(2, i) for i in range(6)] + [(3, i) for i in range(3)]
SM_TOP = 38       # the game's pause screen frame starts 38 lines down
SM_ROWS = 161     # RetroDS's frame and map (its buttons below are its own)
SM_CURSOR = (0, 0, 80, 100)  # x0, y0, x1, y1: where the game's cursor and arrow are on the page


def sm_map(rom, data, out):
    lines = []
    for s, pause in SM_MAP_STATES:
        lines += ["load %s/super-metroid/%s.state" % (data, s), "wait 2"]
        if pause:
            lines += ["hold START 4", "wait 150"]
        lines += ["dumpram sm-" + s, "shot sm-" + s, "smmap smc-%s game" % s]
    harness(rom, out, lines, "smmap")
    for s, _ in SM_MAP_STATES:
        base = os.path.join(out, "sm-" + s)
        rc, text = tool(os.path.join(REPO, "tools", "sm_map.py"), rom, base + ".ram", base + "-py.png", "--check", base + ".png")
        report("smmap", rc == 0, "%s: sm_map.py %s" % (s, text.splitlines()[-1] if text else ""))
        a, b = png(os.path.join(out, "smc-%s.png" % s)), png(base + ".png")
        d = (a[:SM_ROWS] != b[SM_TOP:SM_TOP + SM_ROWS]).any(2)
        arrows = 0
        for x0, y0, x1, y1 in SM_ARROWS:
            arrows += int(d[y0:y1, x0:x1].sum())
            d[y0:y1, x0:x1] = False
        n = int(d.sum())
        report("smmap", n == 0, "%s: RetroDS's map vs the game's pause screen, %d pixels differ (%d in the game's scroll arrows)" % (s, n, arrows))


def sm_page(rom, data, out):
    lines = ["load %s/super-metroid/crateria-ship.state" % data] + SM_ITEMS
    lines += ["wait 10", "hold START 4", "wait 120", "hold R 10", "wait 120", "peek 7E0727 1", "shot pg", "smmap pgc page"]
    text = harness(rom, out, lines, "smpage")
    on_page = "peek 7E0727: 01" in text
    a, b = png(os.path.join(out, "pgc.png")), png(os.path.join(out, "pg.png"))
    d = (a[:SM_ROWS] != b[SM_TOP:SM_TOP + SM_ROWS]).any(2)
    x0, y0, x1, y1 = SM_CURSOR
    inside = int(d[y0:y1, x0:x1].sum())
    outside = int(d.sum()) - inside
    report("smpage", on_page and outside == 0 and inside < 400,
           "equipment page: %d pixels differ outside the cursor's corner, %d in it%s" % (outside, inside, "" if on_page else " (the game's page didn't open)"))


def sm_writes(rom, data, out):
    lines = ["load %s/super-metroid/crateria-ship.state" % data] + SM_ITEMS + ["wait 2"]
    lines += ["smtoggle %d %d" % t for t in SM_TOGGLES for _ in range(2)]
    lines += ["load %s/super-metroid/crateria-pause-map.state" % data] + SM_ITEMS + ["wait 2", "peek 7E0998 1"]
    lines += ["smtoggle %d %d" % t for t in SM_TOGGLES]
    text = harness(rom, out, lines, "smwrites")
    played, paused = text.split("peek 7E0998")
    res = re.findall(r"smtoggle (\d+) (\d+): (\d+) byte\(s\) written(.*)", played)
    bad = [(c, i) for c, i, n, rest in res if int(n) == 0 or "REFUSED" in rest]
    report("smwrites", len(res) == 2 * len(SM_TOGGLES) and not bad,
           "in play: %d switches written on the list%s" % (len(res) - len(bad), ", not: %s" % bad if bad else ""))
    res = re.findall(r"smtoggle (\d+) (\d+): (\d+) byte\(s\) written", paused)
    wrote = [(c, i) for c, i, n in res if int(n) > 0]
    report("smwrites", len(res) == len(SM_TOGGLES) and not wrote, "paused: %d switches, %d wrote" % (len(res), len(wrote)))


# ---- Final Fantasy VI ----

FF6_PARTY_STATES = ["narshe-gate", "ff3-3-slot1-scenario-split", "ff3-3-slot2-world-map", "ff3-3-slot3-all-characters",
                    "finalf3-slot1-floating-continent", "finalf3-slot2-world-map", "finalf3-slot3-kefkas-tower"]
FF6_CMDBOX = (168, 0, 256, 131)   # the main menu's command list (x0, y0, x1, y1), left out of the party screen
FF6_MINUTE = (232, 143, 240, 151)  # the clock's minute digit
FF6_SABIN = "harness-ff3-3-battle-behemoth-sabin"  # Sabin's turn: Edgar, Sabin, Terra, Shadow v Behemoth, 2 Misfits
FF6_TERRA = "harness-ff3-3-battle-behemoth-terra"  # the same battle, Terra's turn


def ff6_party(rom, data, out):
    lines = []
    for s in FF6_PARTY_STATES:
        lines += ["load %s/final-fantasy-vi/harness-%s.state" % (data, s), "wait 2", "ff6 menu ours-" + s, "tap X", "wait 90", "shot game-" + s]
    harness(rom, out, lines, "ff6party")
    for s in FF6_PARTY_STATES:
        a, b = png(os.path.join(out, "ours-%s.png" % s)), png(os.path.join(out, "game-%s.png" % s))
        d = (a != b).any(2)
        for x0, y0, x1, y1 in (FF6_CMDBOX, FF6_MINUTE):
            d[y0:y1, x0:x1] = False
        n = int(d.sum())
        report("ff6party", n == 0, "%s: RetroDS's party screen vs the game's menu, %d pixels differ" % (s, n))


def ff6_state(data, name):
    return "load %s/final-fantasy-vi/%s.state" % (data, name)


def ff6_menus(rom, data, out):
    # (command number, the menu the game opens, rows its list should hold: -1 not a list)
    cases = [(0x00, 0x38, -1), (0x0A, 0x3D, -1), (0x02, 0x0E, 15), (0x01, 0x0A, 64), (0x08, 0x2D, 13), (0x09, 0x30, 2),
             (0x10, 0x1E, 10), (0x13, 0x21, 1), (0x0C, 0x1B, 3), (0x1D, 0x2A, 4), (0x07, 0x37, -1), (0x0F, 0x08, -1)]
    lines = []
    for cmd, _, _ in cases:
        # (Fight is Sabin's first; the others put in his second slot of the battle's command list,
        # $202E + 12 + 3, where the list's own targeting doesn't matter)
        if cmd == 0x00:
            lines += [ff6_state(data, FF6_SABIN), "ff6 cmd 0", "wait 30", "ff6 battle", "ff6 list"]
        else:
            lines += [ff6_state(data, FF6_SABIN), "poke 7E203D 1 %02X" % cmd, "poke 7E203E 1 00", "ff6 cmd 1", "wait 30", "ff6 battle", "ff6 list"]
    text = harness(rom, out, lines, "ff6menus")
    menus = re.findall(r"ff6 battle: open (\d), menu ([0-9A-F]{2})", text)
    lists = re.findall(r"ff6 list: menu ([0-9A-F]{2}), (\d+) rows", text)
    bad = []
    for (cmd, menu, rows), (op, got), (_, n) in zip(cases, menus, lists):
        if op != "1" or int(got, 16) != menu or (rows >= 0 and int(n) != rows):
            bad.append("%02X: menu %s, %s rows (wanted %02X, %d)" % (cmd, got, n, menu, rows))
    report("ff6menus", len(menus) == len(cases) and not bad, "%d commands opened their menus%s" % (len(cases) - len(bad), "; not: " + "; ".join(bad) if bad else ""))


def ff6_acts(rom, data, out):
    lines = [ff6_state(data, FF6_SABIN), "peek 7E3BFC 2", "ff6 cmd 2", "wait 20", "ff6 row 9", "wait 20", "ff6 target 4", "wait 240", "peek 7E3BFC 2"]
    lines += [ff6_state(data, FF6_TERRA), "peek 7E3BFA 2", "ff6 cmd 2", "wait 20", "ff6 row 0", "wait 40", "ff6 target 3", "wait 300", "peek 7E3BFA 2"]
    for k in range(4):
        lines += [ff6_state(data, FF6_SABIN), "ff6 cmd 1", "wait 20", "ff6 blitz %d" % k, "wait 10", "peek 7E2BAE 3"]
    for d in ("LEFT", "RIGHT"):
        lines += [ff6_state(data, FF6_SABIN), "tap " + d, "wait 4", "tap A", "wait 10", "peek 7E2BAE 3"]
    lines += [ff6_state(data, FF6_SABIN), "poke 7E203D 1 0C", "poke 7E203E 1 00", "ff6 cmd 1", "wait 30", "ff6 row 7", "wait 30", "ff6 target 1"]
    text = harness(rom, out, lines, "ff6acts")
    hp = [int(b + a, 16) for a, b in re.findall(r"peek 7E3BF[AC]: ([0-9A-F]{2}) ([0-9A-F]{2})", text)]
    report("ff6acts", len(hp) == 4 and hp[1] < hp[0], "Magic > Fire > Behemoth: HP %s" % (" > ".join(map(str, hp[:2]))))
    report("ff6acts", len(hp) == 4 and hp[3] > hp[2], "Magic > Cure > Shadow: HP %s" % (" > ".join(map(str, hp[2:]))))
    acts = re.findall(r"peek 7E2BAE: FF ([0-9A-F]{2}) ([0-9A-F]{2})", text)
    want = [("0A", "%02X" % k) for k in range(4)] + [("14", None), ("15", None)]
    ok = len(acts) == len(want) and all(c == wc and (wa is None or a == wa) for (c, a), (wc, wa) in zip(acts, want))
    report("ff6acts", ok, "Blitzes, Row, Def. queued: %s" % ", ".join("%s %s" % x for x in acts))
    refused = re.search(r"ff6 target 1: -1 writes", text) is not None
    report("ff6acts", refused, "a lore kept to the enemies, tapped on a character: %s" % ("refused" if refused else "NOT refused"))


# FF6-2: the pad against the page. Each case: a start (commands put in Sabin's
# second slot where the test party hasn't them; the list opened), then presses;
# after each, the page drawn and where its hand is (in a list, from the list's
# top: its first line shown x 24 added).
FF6_PAD = [
    ("command column, Row, Def.", [], ["DOWN", "DOWN", "DOWN", "UP", "UP", "UP", "LEFT", "RIGHT", "RIGHT", "LEFT"]),
    ("Short set's cross, Row (L), Def. (R)", ["SHORT"], ["LEFT", "RIGHT", "UP", "DOWN", "UP", "L", "R"]),
    ("Magic and its esper", ["cmd 2"], ["RIGHT", "DOWN", "DOWN", "DOWN", "DOWN", "DOWN", "LEFT", "UP", "UP", "UP", "UP", "UP", "UP", "DOWN"]),
    ("Item", ["cmd 3"], ["DOWN"] * 7 + ["UP"] * 3 + ["LEFT", "RIGHT"]),
    ("Lore", ["poke 7E203D 1 0C", "cmd 1"], ["DOWN"] * 6 + ["UP", "LEFT", "RIGHT"]),
    ("Rage", ["poke 7E203D 1 10", "cmd 1"], ["RIGHT", "DOWN", "DOWN", "LEFT", "UP"]),
    ("Dance", ["poke 7E203D 1 13", "cmd 1"], ["RIGHT", "DOWN", "LEFT", "UP"]),
    ("Magitek", ["poke 7E203D 1 1D", "cmd 1"], ["RIGHT", "DOWN", "LEFT", "UP"]),
    ("Throw", ["poke 7E203D 1 08", "cmd 1"], ["DOWN"] * 6 + ["UP", "LEFT", "RIGHT"]),
    ("Tools", ["poke 7E203D 1 09", "cmd 1"], ["RIGHT", "DOWN", "LEFT", "UP"]),
]


def ff6_pad(rom, data, out):
    cfg = int(re.search(r"peek 7E1D4D: ([0-9A-F]{2})", harness(rom, out, [ff6_state(data, FF6_SABIN), "peek 7E1D4D 1"], "ff6pad-cfg")).group(1), 16)
    lines = []
    for k, (_, setup, presses) in enumerate(FF6_PAD):
        lines += [ff6_state(data, FF6_SABIN)]
        for st in setup:
            if st == "SHORT":   # (Config's Command set: short; the battle's copy too)
                lines += ["poke 7E1D4D 1 %02X" % (cfg | 0x80), "poke 7E2F2E 1 01", "wait 2"]
            elif st.startswith("cmd"):
                lines += ["poke 7E203E 1 00", "ff6 " + st, "wait 30"]
            else:
                lines += [st]
        lines += ["ff6 battlepic pad%d-0" % k]
        for j, key in enumerate(presses):
            # (the Short set's Row and Def. stay open while L or R is held: the page drawn
            # then, and a pause after for the window to close)
            if key in ("L", "R"):
                lines += ["hold %s 20" % key, "ff6 battlepic pad%d-%d" % (k, j + 1), "wait 30"]
            else:
                lines += ["tap " + key, "wait 24", "ff6 battlepic pad%d-%d" % (k, j + 1)]
    text = harness(rom, out, lines, "ff6pad")
    hands = {}
    taps = []
    for name, page, menu, top, xy, hit, arg, cur, lcur in re.findall(
            r"ff6 hand \S*?pad(\d+-\d+): page (\d+) menu ([0-9A-F]{2}) top (\d+):((?: -?\d+,-?\d+)*) \| hit (-?\d+) (-?\d+) \| cursor (\d+) list (-?\d+)", text):
        pts = [tuple(map(int, q.split(","))) for q in xy.split()]
        hands[name] = (int(page), menu, int(top), pts)
        # a tap where the hand points hits the button of the game's cursor (FF6_HIT_COMMAND 1,
        # FF6_HIT_ROW 6, FF6_HIT_ROWCMD 9, FF6_HIT_DEF 10; a list's empty place: nothing)
        hit, arg = int(hit), int(arg)
        want = {"05": (1, int(cur)), "24": (9, -1), "27": (10, -1)}.get(menu) if page == "1" else None
        if page == "3":
            want = (6, int(lcur)) if (hit, arg) != (0, -1) else (0, -1)
        if want and (hit, arg) != want:
            taps.append("%s: hit %d %d, wanted %d %d" % (name, hit, arg, want[0], want[1]))
    for k, (title, _, presses) in enumerate(FF6_PAD):
        bad = []
        for j, key in enumerate(presses):
            a, b = hands.get("%d-%d" % (k, j)), hands.get("%d-%d" % (k, j + 1))
            if not a or not b or len(a[3]) != 1 or len(b[3]) != 1:
                bad.append("%s: %s hands" % (key, "no" if not a or not b else "%d/%d" % (len(a[3]), len(b[3]))))
                continue
            (ax, ay), (bx, by) = a[3][0], b[3][0]
            lst = a[0] == 3 and b[0] == 3   # (FF6_PAGE_LIST: from the list's top)
            if lst:
                ay += a[2] * 24
                by += b[2] * 24
            dx, dy = bx - ax, by - ay
            if key == "UP":
                ok = dy < 0
            elif key == "DOWN":
                ok = dy > 0
            elif key in ("LEFT", "L"):
                ok = dx < 0 or (lst and dy < 0)   # (a list's wrap: the line before's last column, as the game's)
            else:
                ok = dx > 0 or (lst and dy > 0)
            # (Row's and Def.'s windows: the press opened them)
            want = {"LEFT": "24", "L": "24", "RIGHT": "27", "R": "27"}.get(key) if a[1] == "05" and b[1] in ("24", "27") else None
            if want and b[1] != want:
                ok = False
            if key in ("L", "R") and b[1] != {"L": "24", "R": "27"}[key]:
                ok = False
            if not ok:
                bad.append("%s moved %+d,%+d (menu %s > %s)" % (key, dx, dy, a[1], b[1]))
        report("ff6pad", not bad, "%s: %d presses%s" % (title, len(presses), "; " + "; ".join(bad) if bad else " as shown"))
    report("ff6pad", not taps and len(hands) > 0, "a tap where the hand is hits the game's cursor's button, %d pages%s" % (len(hands), "; " + "; ".join(taps) if taps else ""))


CHECKS = {
    "items": ("alttp", alttp_items), "dungeon": ("alttp", alttp_dungeon), "marker": ("alttp", alttp_marker),
    "pieces": ("alttp", alttp_pieces), "writes": ("alttp", alttp_writes),
    "smmap": ("sm", sm_map), "smpage": ("sm", sm_page), "smwrites": ("sm", sm_writes),
    "ff6party": ("ff6", ff6_party), "ff6menus": ("ff6", ff6_menus), "ff6acts": ("ff6", ff6_acts), "ff6pad": ("ff6", ff6_pad),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--alttp", help="A Link to the Past (USA) ROM")
    ap.add_argument("--sm", help="Super Metroid (JU) ROM")
    ap.add_argument("--ff6", help="Final Fantasy III (USA) 1.0 ROM")
    ap.add_argument("--data", default=os.path.join(os.path.dirname(REPO), "test-data"))
    ap.add_argument("--out")
    ap.add_argument("--only", help="comma-separated: " + ",".join(CHECKS))
    a = ap.parse_args()
    for p in (HARNESS, CORE):
        if not os.path.exists(p):
            sys.exit("missing %s: run make tools (scripts/setup-host.sh once)" % p)
    out = a.out or tempfile.mkdtemp(prefix="check-games-")
    names = a.only.split(",") if a.only else list(CHECKS)
    for name in names:
        game, fn = CHECKS[name]
        rom = {"alttp": a.alttp, "sm": a.sm, "ff6": a.ff6}[game]
        if not rom:
            print("%-8s skip (no %s ROM given)" % (name, game))
            continue
        data = os.path.join(a.data, "a-link-to-the-past") if game == "alttp" else a.data
        try:
            fn(os.path.abspath(rom), data, os.path.join(out, name))
        except Exception as ex:   # a check that can't run counts as failed
            report(name, False, "could not run: %s" % ex)
    failed = sum(1 for _, ok in results if not ok)
    print("\n%d of %d checks passed" % (len(results) - failed, len(results)))
    if failed or a.out:
        print("harness output in", out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if failed or not results else 0


if __name__ == "__main__":
    sys.exit(main())
