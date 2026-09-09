"""The seal verifier, which nothing ran.

`tools/run_headline.py` is the only sanctioned route to the TEST fold: it checks
the unseal is on record, checks the audit chain verifies, invokes selfplay with
the token, and records the result. No test and no CI job executed it. The one
thing that touched it was `compileall`, which proves only that it parses.

Its two gates are tested here by making each one refuse in turn, with selfplay
replaced by something that raises if it is ever launched, because the real run
reads TEST and appends to the audit log. The parser is tested on a captured row;
`test_tools_output.py` runs the same parser over live selfplay output, so the
captured row cannot drift away from the real format unnoticed.

    python tests/test_run_headline.py
"""

from __future__ import annotations

import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _harness import ROOT, SKIP, check, exe, report, run  # noqa: E402

sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "python"))


import run_headline  # noqa: E402
import stats  # noqa: E402


class NeverRuns:
    """Stands in for subprocess. Reaching it at all is the failure."""

    launched = []

    @staticmethod
    def run(*args, **kwargs):
        NeverRuns.launched.append(args)
        raise AssertionError("selfplay was launched past a refusal")


def guarded(fold="test", games="20000"):
    """Call main() with both gates observable and selfplay unreachable."""
    saved = (stats.require_unseal, stats.verify_audit, run_headline.subprocess,
             sys.argv)
    NeverRuns.launched = []
    run_headline.subprocess = NeverRuns
    sys.argv = ["run_headline.py", "--games", games, "--fold", fold]
    try:
        return run_headline.main()
    finally:
        (stats.require_unseal, stats.verify_audit, run_headline.subprocess,
         sys.argv) = saved


def test_gates():
    print("[the two gates on TEST]")

    # Gate one: the unseal must already be recorded.
    stats.require_unseal = lambda name: (_ for _ in ()).throw(
        PermissionError("no unseal on record for " + name))
    rc = guarded()
    check(rc == 2, "TEST is refused when no unseal is on record",
          "returned {}".format(rc))
    check(not NeverRuns.launched, "and selfplay is never launched")

    # Gate two: the chain the unseal sits in must verify. A recorded unseal in a
    # log that has been edited underneath it is worth nothing, and this gate is
    # the only thing that says so.
    stats.require_unseal = lambda name: None
    stats.verify_audit = lambda: (False, 7)
    rc = guarded()
    check(rc == 2, "TEST is refused when the audit chain does not verify",
          "returned {}".format(rc))
    check(not NeverRuns.launched, "and selfplay is never launched then either")


def test_train_needs_no_token():
    """TRAIN is not sealed, so neither gate applies to it."""
    print("\n[TRAIN is not gated]")
    consulted = []
    stats.require_unseal = lambda name: consulted.append(name)
    try:
        guarded(fold="train", games="10")
    except AssertionError:
        pass        # reached selfplay, which is the point: it was not refused
    check(not consulted, "TRAIN does not consult the seal at all")
    check(NeverRuns.launched, "and it proceeds to the run")


def test_parser():
    print("\n[the selfplay parser]")
    captured = (
        "policy                   mean      sd          95% CI        med    p95"
        "   best  worst   us/game\n"
        "random                 95.900   4.152  [ 95.430,  96.370]      97    100"
        "     74    100      181.8\n"
        "density(b=10)          44.173   8.875  [ 43.169,  45.178]      43     61"
        "     27     68      940.3\n"
        "\n"
        "  random               - density(b=10)         +51.727  "
        "[+50.630, +52.824]   rho 0.028   CRN saves 1.0x\n")

    p = run_headline.parse(captured)
    check(len(p["policies"]) == 2, "both policy rows are read",
          "got {}".format(len(p["policies"])))
    check(len(p["paired"]) == 1, "and the paired row is read",
          "got {}".format(len(p["paired"])))
    if p["policies"]:
        first = p["policies"][0]
        check(first["name"] == "random" and abs(first["mean"] - 95.9) < 1e-9,
              "the fields land in the right places",
              "{} mean {}".format(first["name"], first["mean"]))
        check(first["ci"][0] <= first["mean"] <= first["ci"][1],
              "and the interval it read contains the mean it read")

    # The caller refuses to record a partial result, which only works if a table
    # it cannot read comes back empty rather than half full.
    empty = run_headline.parse("selfplay: could not open the board bank\n")
    check(not empty["policies"] and not empty["paired"],
          "output with no table parses to nothing at all",
          "got {} policies".format(len(empty["policies"])))


def test_provenance():
    """The field that pins a headline number to a build.

    The defect was that this recorded the empty string on every run, so what
    must hold is that the field is never empty.

    Whether git can answer is the environment's business and not this function's:
    "unknown" is its documented fallback, and the Windows CI runner took it. The
    first version of this test required a real commit unconditionally, which
    asserted that git was installed rather than that the code was right, and it
    failed there while the function did exactly what it says.
    """
    print("\n[the recorded commit]")
    # A missing executable raises rather than coming back with a non-zero code,
    # which is what commit() catches and what this probe did not. The branch
    # below for a machine without git was therefore unreachable on a machine
    # without git: the line choosing between the branches threw first, and the
    # Windows leg died here rather than taking the path written for it.
    try:
        probe = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                               capture_output=True, text=True)
        have_git = probe.returncode == 0
        why = probe.stderr.strip()[:80]
    except (OSError, subprocess.SubprocessError) as exc:
        have_git, why = False, "{}: {}".format(type(exc).__name__, exc)[:80]

    c = run_headline.commit()

    check(bool(c), "something is always recorded, never the empty string it held",
          "got {!r}".format(c))
    if have_git:
        check(re.fullmatch(r"[0-9a-f]{40}(-dirty)?", c) is not None,
              "and where git answers, it is that commit", "got {!r}".format(c))
    else:
        check(c == "unknown", "and where git cannot answer, it says so",
              "got {!r}; git said {!r}".format(c, why))

    # A headline number from a tree with uncommitted changes is not reproducible
    # from the commit alone, so the marker carries that warning.
    # Checked against the tree as it actually is, either way round, because the
    # sha pattern above accepts the marker without requiring it.
    if have_git:
        st = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT,
                            capture_output=True, text=True)
        if st.returncode == 0:
            if st.stdout.strip():
                check(c.endswith("-dirty"),
                      "a tree with uncommitted changes is marked dirty",
                      "got {!r} with {} changed files".format(
                          c, len(st.stdout.strip().splitlines())))
            else:
                check(not c.endswith("-dirty"),
                      "and a clean tree is not marked dirty", "got {!r}".format(c))

    # The fallback itself, without needing a machine that has no git.
    saved = run_headline.subprocess

    class NoGit:
        # commit() catches subprocess.SubprocessError, and it looks the name up
        # on whatever this module is, so the stub has to carry it.
        SubprocessError = subprocess.SubprocessError

        @staticmethod
        def run(*a, **k):
            raise OSError("git is not installed")

    run_headline.subprocess = NoGit
    try:
        fallback = run_headline.commit()
    finally:
        run_headline.subprocess = saved
    check(fallback == "unknown",
          "and with no git at all it records the stated fallback",
          "got {!r}".format(fallback))


def main():
    print("the seal verifier")
    print("=================")
    test_gates()
    test_train_needs_no_token()
    test_parser()
    test_provenance()
    return report()


if __name__ == "__main__":
    sys.exit(main())
