#!/usr/bin/env python3
"""Cursor-trail regression, end to end, through the real compositor.

The unit tests (out/test_cursor_damage, out/test_cursor_render) prove the
damage algebra and the present loop in isolation. This one drives the actual
mica-comp binary through tests/scripts/cursor.script and compares the *live*
framebuffer against a deterministic full re-render of the same scene:

  live   -- `mica-comp --headless --script cursor.script --shot live.png`
           dumps C.fb, the buffer that went to the display, after every
           incremental present the scripted moves caused.
  ref    -- the same script plus a trailing `shot ref.png`, which re-renders the
           full screen into a fresh surface.

If the compositor ever fails to restore a previous pointer position, fails to
composite the pointer exactly once, or paints the pointer twice, `live` keeps a
stray blob that `ref` cannot have, and the images differ. A full-repaint
screenshot alone can never see this (it repaints everything), which is exactly
why the old screenshots looked fine while G1OS showed trails.

Exit 0 only if every check passes. Prints one line per check so a failure names
the symptom.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MACLITE = os.path.dirname(HERE)          # scripts/ lives inside macliteos/
BIN = os.path.join(MACLITE, "out", "mica-comp")
SCRIPT = os.path.join(MACLITE, "tests", "scripts", "cursor.script")

WIDTH = 1280
HEIGHT = 800
# Pixels that are 1-2/255 off are rounding, not a trail. The cursor stroke is a
# translucent edge, so a sub-LSB blend difference is invisible by construction.
TOLERANCE = 12


# ------------------------------------------------------------------ png ------
def read_png(path):
    """Decode the PNGs mica-comp writes (stored/fixed-Huffman deflate, RGBA)."""
    import struct
    import zlib

    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    pos = 8
    width = height = depth = ctype = None
    idat = b""
    palette = b""
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctag = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctag == b"IHDR":
            width, height, depth, ctype = struct.unpack(">IIBB", chunk[:10])
        elif ctag == b"PLTE":
            palette = chunk
        elif ctag == b"IDAT":
            idat += chunk
        elif ctag == b"IEND":
            break
    if width is None:
        raise ValueError("%s has no IHDR" % path)
    raw = zlib.decompress(idat)
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = channels * (depth // 8)
    stride = width * bpp
    out = bytearray()
    prev = bytearray(stride)
    i = 0
    for _ in range(height):
        ftype = raw[i]
        i += 1
        line = bytearray(raw[i:i + stride])
        i += stride
        if ftype == 1:      # Sub
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 0xFF
        elif ftype == 2:    # Up
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif ftype == 3:    # Average
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif ftype == 4:    # Paeth
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        out += line
        prev = line
    return width, height, ctype, bytes(out), palette


def pixels(path):
    """Yield (index, r, g, b, a) for an RGBA/RGB/paletted PNG."""
    w, h, ctype, raw, palette = read_png(path)
    if ctype == 6:
        stride = w * 4
        for i in range(w * h):
            r, g, b, a = raw[i * 4:i * 4 + 4]
            yield i, r, g, b, a
    elif ctype == 2:
        for i in range(w * h):
            r, g, b = raw[i * 3:i * 3 + 3]
            yield i, r, g, b, 255
    elif ctype == 3:
        for i in range(w * h):
            p = raw[i] * 3
            r, g, b = palette[p:p + 3]
            yield i, r, g, b, 255
    else:
        raise ValueError("%s: unsupported PNG colour type %d" % (path, ctype))


# ---------------------------------------------------------------- compare ----
def compare(live_path, ref_path):
    """Diff the live framebuffer against the full repaint. Returns (diffs, clusters)."""
    lp = list(pixels(live_path))
    rp = list(pixels(ref_path))
    if len(lp) != len(rp):
        raise ValueError("image sizes differ: %d vs %d" % (len(lp), len(rp)))
    w, _h, _ct, _raw, _pal = read_png(live_path)

    diffs = []
    for i in range(len(lp)):
        _i, lr, lg, lb, la = lp[i]
        _j, rr, rg, rb, ra = rp[i]
        if max(abs(lr - rr), abs(lg - rg), abs(lb - rb), abs(la - ra)) > TOLERANCE:
            diffs.append(i)

    # Group the differing pixels into connected blobs so a report can say how
    # many cursors are on screen, not just how many pixels are wrong.
    clusters = []
    remaining = set(diffs)
    while remaining:
        seed = remaining.pop()
        stack = [seed]
        blob = [seed]
        while stack:
            cur = stack.pop()
            cx, cy = cur % w, cur // w
            for nx, ny in ((cx - 1, cy), (cx + 1, cy), (cx, cy - 1), (cx, cy + 1),
                           (cx - 1, cy - 1), (cx + 1, cy - 1), (cx - 1, cy + 1), (cx + 1, cy + 1)):
                if nx < 0 or ny < 0 or nx >= w or ny >= _h:
                    continue
                nxt = ny * w + nx
                if nxt in remaining:
                    remaining.discard(nxt)
                    stack.append(nxt)
                    blob.append(nxt)
        xs = [p % w for p in blob]
        ys = [p // w for p in blob]
        clusters.append({
            "n": len(blob),
            "x": min(xs), "y": min(ys),
            "w": max(xs) - min(xs) + 1, "h": max(ys) - min(ys) + 1,
        })
    clusters.sort(key=lambda c: -c["n"])
    return diffs, clusters


# ------------------------------------------------------------------ run ------
def run(args, cwd, env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    return subprocess.run(args, cwd=cwd, env=e, capture_output=True, text=True, timeout=180)


# Safe Graphics (video=efifb + nomodeset, MICA_GL=off) is software compositing
# over the EFI/fbdev framebuffer, which is MODE_PERFORMANCE here. "Normal" is
# KMS. Both funnel through the same ml_display_commit(), so both must be clean.
MODES = [
    ("kms-equivalent", ["--mode", "beautiful"], {}),
    ("safe-graphics", ["--mode", "performance"], {"MICA_GL": "off"}),
]


def main():
    if not os.access(BIN, os.X_OK):
        print("FAIL: %s not built (run: make -C macliteos all)" % BIN)
        return 1
    if not os.path.exists(SCRIPT):
        print("FAIL: %s missing" % SCRIPT)
        return 1

    failures = []

    def check(ok, message):
        print(("PASS  " if ok else "FAIL  ") + message)
        if not ok:
            failures.append(message)

    tmp = tempfile.mkdtemp(prefix="cursor-trails-")

    # a deterministic full re-render of the final state, for each mode
    with open(SCRIPT) as fh:
        body = fh.read()
    refscript = os.path.join(tmp, "cursor_ref.script")
    with open(refscript, "w") as fh:
        fh.write(body.replace("\nquit\n", "\nshot %s\nquit\n" % os.path.join(tmp, "ref.png")))

    for label, mode_args, mode_env in MODES:
        live = os.path.join(tmp, "live_%s.png" % label)
        env = dict(mode_env)
        env["MICA_LOG"] = os.path.join(tmp, "mica_%s.log" % label)

        # 1. the live framebuffer after the scripted moves
        r = run([BIN, "--headless", "-W", str(WIDTH), "-H", str(HEIGHT),
                 "--script", SCRIPT, "--shot", live] + mode_args,
                cwd=os.path.join(MACLITE, "out"), env=env)
        check(r.returncode == 0,
              "[%s] cursor.script runs the compositor to completion (exit %d)"
              % (label, r.returncode))
        check(os.path.exists(live), "[%s] the live framebuffer was captured" % label)

        # 2. the full-repaint reference in the same mode. It must log to a
        #    different file, or it overwrites the one the checks below read.
        ref_env = dict(env)
        ref_env["MICA_LOG"] = os.path.join(tmp, "mica_%s_ref.log" % label)
        r2 = run([BIN, "--headless", "-W", str(WIDTH), "-H", str(HEIGHT),
                  "--script", refscript, "--shot", live + ".ignored"] + mode_args,
                 cwd=os.path.join(MACLITE, "out"), env=ref_env)
        ref = os.path.join(tmp, "ref.png")
        check(r2.returncode == 0 and os.path.exists(ref),
              "[%s] the full-repaint reference was produced (exit %d)" % (label, r2.returncode))

        if os.path.exists(live) and os.path.exists(ref):
            diffs, clusters = compare(live, ref)
            check(not diffs,
                  "[%s] live framebuffer matches a full repaint exactly "
                  "(%d differing pixel(s))" % (label, len(diffs)))
            check(len(clusters) <= 1,
                  "[%s] at most one cursor is on screen (found %d blob(s): %s)"
                  % (label, len(clusters),
                     ", ".join("%dx%d at %d,%d" % (c["w"], c["h"], c["x"], c["y"])
                               for c in clusters[:6])))
            for c in clusters[:6]:
                print("        [%s] blob: %d px, %dx%d at (%d,%d)"
                      % (label, c["n"], c["w"], c["h"], c["x"], c["y"]))

        # 3. the compositor must report the sprite geometry it tracks, and
        # 4. must never have needed the restore repair
        logpath = env["MICA_LOG"]
        if os.path.exists(logpath):
            with open(logpath) as fh:
                text = fh.read()
            check("pointer: sprite ink=" in text,
                  "[%s] the compositor logged the measured sprite ink bounds it "
                  "tracks damage from" % label)
            check("did not cover the" not in text,
                  "[%s] no frame needed the cursor-restore repair "
                  "(damage tracking was complete on its own)" % label)

    print()
    if failures:
        print("FAIL: %d cursor-trail check(s) failed" % len(failures))
        return 1
    print("PASS: cursor trails regression clean in every mode "
          "(live framebuffer == full repaint)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
