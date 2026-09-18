"""Every Python module type-checks under mypy --strict.

The C++ has a type system the build enforces, and -Werror has caught real
defects in this tree three times, most recently a -Wshadow collision one
refactor introduced. The Python layer computes every published interval and
renders both deliverables, and until this file the only static check on it was
tests/test_python_names.py, which reports a name read and never bound.

Named for the property rather than the tool, as python_names, stated_counts and
figure_marks are.

Two things about how this runs.

It is SKIPPABLE and skips when mypy is absent, because mypy is the one thing in
this repository that is not standard library and README says so. Registering it
conditionally instead would leave it unregistered on the CI runners, and the
registration guard in ci.yml asserts that every interpreter-gated test exists on
every leg, so the guard would go red for the wrong reason.

A skip everywhere would then mean it ran nowhere, which is what
.github/workflows/nightly.yml records about scrubber_js and treats as a bug. So
the reference-models job installs a pinned mypy and runs this script directly,
and treats a skip there as a failure. That job is the hard gate; everywhere else
this reports Skipped.

    python tests/test_static_types.py
"""

from __future__ import annotations

import importlib.util
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _harness import ROOT, SKIP, check, report  # noqa: E402

CONFIG = os.path.join(ROOT, "mypy.ini")
ROOTS = ("python", "tools", "tests")


def main() -> int:
    print("every Python module under mypy --strict")
    print("=======================================")

    if importlib.util.find_spec("mypy") is None:
        print("  mypy is not installed for this interpreter, so there is")
        print("  nothing to check with. pip install mypy to run it here.")
        return SKIP
    if not os.path.exists(CONFIG):
        check(False, "mypy.ini is where the test expects it", CONFIG)
        return report()

    # sys.executable -m, never a mypy on PATH: CMake hands this script to the
    # interpreter it found, and mypy has to be installed for that one.
    #
    # --no-incremental because a cache can only ever turn a red into a green.
    # It costs about three seconds against half a second warm, which is a poor
    # trade for a gate whose whole value is that it cannot pass by accident.
    proc = subprocess.run(
        [sys.executable, "-m", "mypy", "--config-file", CONFIG, "--no-incremental"]
        + [os.path.join(ROOT, d) for d in ROOTS],
        cwd=ROOT, capture_output=True, text=True)

    ok = proc.returncode == 0
    # mypy's own output is the diagnosis. Summarising it would throw away the
    # file, the line and the error code, which is everything a reader needs.
    if not ok:
        print(proc.stdout.rstrip())
        if proc.stderr.strip():
            print(proc.stderr.rstrip())
    check(ok, "python, tools and tests check clean",
          "mypy exited {}".format(proc.returncode))

    # The gate is worth nothing if it ran over nothing. mypy states how many
    # files it read, on success and on failure alike, and that is compared
    # against the tree rather than against a number typed here: a config that
    # stopped matching some directory would otherwise pass in silence.
    m = re.search(r"checked (\d+) source files|no issues found in (\d+) source file",
                  proc.stdout)
    read = int(m.group(1) or m.group(2)) if m else -1
    present = sum(len([f for f in os.listdir(os.path.join(ROOT, d))
                       if f.endswith(".py")]) for d in ROOTS)
    check(read == present,
          "and it read every Python file in {}".format(", ".join(ROOTS)),
          "mypy read {}, the three directories hold {}".format(read, present))
    return report()


if __name__ == "__main__":
    sys.exit(main())
