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
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "python"))

SKIP = 77
failures = 0


def check(ok, what, detail=""):
    global failures
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail and not ok:
        print("      " + detail)
    if not ok:
        failures += 1


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
    """The field that pins a headline number to a build."""
    print("\n[the recorded commit]")
    c = run_headline.commit()
    check(bool(c), "a commit is recorded at all", "got {!r}".format(c))
    check(c != "unknown", "and git actually answered", "got {!r}".format(c))
    check(re.fullmatch(r"[0-9a-f]{40}(-dirty)?", c) is not None,
          "and it is a commit rather than a placeholder", "got {!r}".format(c))


def main():
    print("the seal verifier")
    print("=================")
    test_gates()
    test_train_needs_no_token()
    test_parser()
    test_provenance()
    print("\n" + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
