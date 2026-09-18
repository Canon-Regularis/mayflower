"""Test harness for the Python suite.

This replaces ten per file copies of check(). The copies used two conventions:
one printed the detail line for every check, the other only on failure. The
second is kept. A detail line under a passing check is misleading, because
"43.767 not in [41.8, 45.7]  ok" states a contradiction.

It also replaces SKIP, the ctest skip code, declared in eleven files; ROOT,
written out in fourteen; exe(), the .exe probe, in three; and the TimedOut
stand in, in three.

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from _harness import ROOT, SKIP, check, exe, report, run
"""

from __future__ import annotations

import io
import os
import re
import subprocess
from collections.abc import Sequence
from typing import Any

# ctest reads this as "Skipped" through SKIP_RETURN_CODE. It means the artefact
# the test reads is not there, or the binary is not built; it does not mean pass.
SKIP = 77

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def widget_env() -> dict[str, str]:
    """The values tools/render_report.py puts on the two report widgets.

    web/live.js and web/scrubber.js read the ramp width and the size of the
    hypothesis space off the element instead of carrying their own copies. A
    headless harness that leaves those attributes out hands them
    Number(undefined), which is NaN, so every cell paints var(--ramp-NaN) and the
    live widget reports a hypothesis count of NaN. That is not loud: it silently
    stopped test_live_exact.py from seeing the sampled regime at all, because its
    regex for the count cannot match NaN.

    Read from the same places the renderer reads them, so a harness cannot drift
    from the page. Returned as strings, which is what a real dataset holds.
    """
    header = io.open(os.path.join(ROOT, "include", "mayflower", "constants.hpp"),
                     encoding="utf-8").read()
    m = re.search(r"kOmega0 = ([0-9']+)ull", header)
    if not m:
        raise KeyError("kOmega0 not found in constants.hpp")

    builder = io.open(os.path.join(ROOT, "tools", "build_report.py"),
                      encoding="utf-8").read()
    # The optional annotation is not decoration. BUCKETS carries none today,
    # and the pattern that required a bare " = " would have stopped matching
    # the moment it gained one, raising below and killing the three Node
    # tests that call widget_env with a traceback rather than a skip.
    b = re.search(r"^BUCKETS\s*(?::\s*\w+\s*)?=\s*([0-9]+)", builder, re.M)
    if not b:
        raise KeyError("BUCKETS not found in build_report.py")

    env = dict(os.environ)
    env["MF_OMEGA"] = m.group(1).replace("'", "")
    env["MF_BUCKETS"] = b.group(1)
    return env

_failures: int = 0


def check(ok: bool, what: str, detail: str = "") -> bool:
    """Record one check. Returns ok, so a caller can branch on it."""
    global _failures
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail and not ok:
        print("      " + detail)
    if not ok:
        _failures += 1
    return ok


def failures() -> int:
    return _failures


def report() -> int:
    """Print the tail and return the process exit code."""
    print("\n" + ("FAILED" if _failures else "all checks passed"))
    return 1 if _failures else 0


def exe(name: str) -> str | None:
    """A built tool by name, with or without the Windows suffix."""
    for candidate in (name + ".exe", name):
        p = os.path.join(ROOT, "build", candidate)
        if os.path.exists(p):
            return p
    return None


def require_exe(name: str) -> str:
    """The same, refusing rather than returning None.

    Every caller of this has already asked exe() the same question in main()
    and returned SKIP if the answer was None. Asking again at the use site and
    dropping the result into an argument list means the absence is discovered
    as a subprocess failure on the string "None". This asks once and says what
    went wrong if the answer changed underneath it, which it can: the build
    directory is not held still between the two calls.
    """
    p = exe(name)
    if p is None:
        raise FileNotFoundError(
            "build/" + name + " is not built, but the caller did not skip")
    return p


class TimedOut:
    """Stands in for a completed process that never completed.

    A subprocess that outruns its timeout raises rather than returning, and none
    of these tests caught it, so a loaded machine failed them with a traceback
    and no statement of what went wrong. Returning a result whose code is non
    zero lets the checks report it the way they report any other failure. 124 is
    what timeout(1) uses for the same thing.
    """

    returncode = 124

    def __init__(self, seconds: float) -> None:
        self.stdout = ""
        self.stderr = "timed out after {} s".format(seconds)


# The union is the return type, not a Protocol. mypy resolves returncode,
# stdout and stderr across it member-wise, because TimedOut declares all three
# with compatible types, so every `r.returncode == 0` at a call site checks
# without one.
#
# **kw is Any because it forwards to subprocess.run, whose keyword surface is
# an overload set. Reproducing that here would be a worse lie than admitting
# the forward. The declared CompletedProcess[str] is only correct because
# text=True is a literal below; a caller passing text=False through **kw would
# make it wrong, and none does.
def run(args: Sequence[str], timeout: float,
        **kw: Any) -> "subprocess.CompletedProcess[str] | TimedOut":
    """subprocess.run that reports a timeout instead of raising on one."""
    try:
        return subprocess.run(args, capture_output=True, text=True,
                              timeout=timeout, **kw)
    except subprocess.TimeoutExpired:
        return TimedOut(timeout)
