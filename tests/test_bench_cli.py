"""The benchmark's argument handling.

`dp_bench` is not run by any suite: a full pass is minutes, and the correctness
content it checks (every rung returns the same count) is covered far more
thoroughly by test_ladder. What is worth guarding cheaply is the entry point,
because `atoi` turns any unparsable argument into 0 and every sample vector in
the report is indexed with front() and v[n/2]. A rep count of zero was undefined
behaviour, surviving only as an abort because _GLIBCXX_ASSERTIONS is on.

    python tests/test_bench_cli.py
"""

from __future__ import annotations

import os
import subprocess
import sys

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


def exe():
    for name in ("dp_bench.exe", "dp_bench"):
        p = os.path.join(ROOT, "build", name)
        if os.path.exists(p):
            return p
    return None


def main():
    print("the benchmark's argument handling")
    print("=================================")
    if exe() is None:
        print("  build/dp_bench is missing; build first")
        return SKIP

    # Each of these used to reach the report with no samples collected.
    for arg in ("0", "-3", "abc", ""):
        r = subprocess.run([exe(), arg], capture_output=True, text=True, timeout=120)
        check(r.returncode == 2,
              "a rep count of {!r} is refused".format(arg),
              "exit {}, expected 2".format(r.returncode))
        check("positive integer" in r.stderr,
              "and says why",
              (r.stderr.strip().splitlines() or ["(no stderr)"])[0])

    # The refusal must come before any measurement, or the guard costs a minute.
    import time
    t0 = time.time()
    subprocess.run([exe(), "0"], capture_output=True, text=True, timeout=120)
    dt = time.time() - t0
    check(dt < 5.0, "the refusal precedes the warm-up", "{:.2f} s".format(dt))

    print("\n" + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
