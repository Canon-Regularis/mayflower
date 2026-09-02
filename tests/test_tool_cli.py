"""Every tool that takes a count must refuse a bad one.

`atoi` turns any unparsable argument into 0, and each of these tools then divides
by that count, indexes a vector it left empty, or writes an artefact from it.
The failures were not uniform and the worst one was silent:

    dp_bench 0      aborted in report(), front() on an empty sample
    selfplay 0      aborted in the summary, which divides by the game count
    report_data 0   same, and it is what the report is built from
    export_pool 0   wrote a zero-byte pool and exited 0

That last one is the reason this is a test rather than a tidy-up. web/pool.bin is
read by the live widget and by tests/test_pool.py, and a tool that empties it
while reporting success would be believed.

    python tests/test_tool_cli.py
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP = 77
failures = 0


def check(ok, what, detail=""):
    global failures
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail:
        print("      " + detail)
    if not ok:
        failures += 1


def exe(name):
    for candidate in (name + ".exe", name):
        p = os.path.join(ROOT, "build", candidate)
        if os.path.exists(p):
            return p
    return None


BAD = ("0", "-5", "abc", "")


def main():
    print("tool argument handling")
    print("======================")

    tools = ("dp_bench", "selfplay", "report_data", "export_pool")
    missing = [t for t in tools if exe(t) is None]
    if missing:
        print("  not built: {}".format(", ".join(missing)))
        return SKIP

    tmp = tempfile.mkdtemp()
    pool = os.path.join(tmp, "probe_pool.bin")

    for tool in tools:
        for arg in BAD:
            # export_pool takes the path first, so its count is the second slot.
            args = [exe(tool), pool, arg] if tool == "export_pool" else [exe(tool), arg]
            started = time.time()
            r = subprocess.run(args, capture_output=True, text=True, timeout=180)
            elapsed = time.time() - started

            check(r.returncode == 2,
                  "{} refuses a count of {!r}".format(tool, arg),
                  "exit {}, expected 2".format(r.returncode))
            check("positive integer" in r.stderr,
                  "{} says why for {!r}".format(tool, arg),
                  (r.stderr.strip().splitlines() or ["(no stderr)"])[0])
            # The refusal has to precede the expensive setup, or no suite can
            # afford to check it: selfplay builds a 24 s board bank.
            check(elapsed < 10.0,
                  "{} refuses {!r} before doing any work".format(tool, arg),
                  "{:.2f} s".format(elapsed))

    # The silent one, stated directly: no artefact may be left behind.
    check(not os.path.exists(pool),
          "export_pool writes no pool at all when it refuses",
          "left {} bytes".format(os.path.getsize(pool)) if os.path.exists(pool) else "")

    print("\n" + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
