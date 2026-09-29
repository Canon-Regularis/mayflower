"""The mutation campaign, run rather than remembered.

Comments in this repository cite mutation results as the reason a test exists,
among them tests/test_uniformity.cpp, tests/test_tools_output.py and
python/audit_test.py. The driver that produced those results lived in a scratch
directory and is gone, and the logs it wrote were untracked. So the evidence for
a claim this project makes about its own test suite could not be reproduced, in
a repository whose whole argument is that an unenforced fact drifts.

This is that driver, tracked, with the faults in tools/mutants.json as data.

What it does, per mutant and per label: plant one edit, rebuild, run that label,
restore the file, and compare the failing tests against what the data says they
should be. A mutant whose expect list names tests must make exactly those tests
fail. An empty expect list is a finding rather than an omission: it says the
label does not catch that fault, and it names which fault. Do not clear an
expect list to make a run green.

One test is excluded from every verdict, and the reason is the whole difficulty
of doing this honestly. See NOT_A_WITNESS below.

Three properties it has to have, and the reasons are all failures this project
has already met:

  - It refuses to run against a dirty working tree for any file it will touch,
    because a crash mid-run would otherwise leave a planted fault behind and the
    next person would be debugging a mutant.
  - It restores from bytes it read before editing, in a finally, and verifies
    the restore byte for byte rather than assuming it.
  - It rebuilds after restoring, so the build directory does not keep binaries
    compiled from a mutant. Section 30 of the plan records a link failure caused
    by a build racing a test run holding the executables; leaving mutated
    binaries in place is the quieter version of the same mistake.

Usage:

    python tools/mutate.py --list
    python tools/mutate.py                    # every mutant, every label it records
    python tools/mutate.py --only gate-h,fold-split
    python tools/mutate.py --label fast       # one label, ignoring what is recorded
    python tools/mutate.py --tests '^(weighted|gated)$'
    python tools/mutate.py --record           # write the measured verdicts back

--record establishes an entry that has none. It is for adding a mutant, and what
it writes has to be read before it is committed: it will just as happily record
a suite that has stopped catching something as one that never did.

Afterwards, regenerate the figure data:

    ./build/report_data 20000 > out/figures.json
    python tools/render_report.py out/figures.json out/report.html

A run restores every file byte for byte, but it restores them by writing them,
so their modification times move. tests/test_provenance.py asks that no engine
source is newer than out/figures.json, which is how a page built from stale data
is caught, and after a campaign every mutated source is newer than it. The test
is right to say so: it cannot tell a file that was rewritten identically from one
that changed, and section 21d of the plan records why a content check cannot
replace it. So the figure data has to be regenerated, not the test relaxed.
"""

from __future__ import annotations

import argparse
import io
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "tools", "mutants.json")
BUILD = os.path.join(ROOT, "build")

# Tests whose failure under mutation says nothing about the mutation.
#
# provenance asserts that no engine source is newer than out/figures.json, which
# is how a page built from stale data is caught. Planting a fault edits an engine
# source, so provenance fails under every mutant by construction and for the same
# reason each time. The first run of this driver recorded all eight mutants as
# "CAUGHT by provenance", including the four that nothing else touched, which
# would have made the campaign report a suite that catches everything and
# witnesses nothing.
#
# This is the same shape the rest of the repository keeps finding, arriving from
# the other side: usually a check that passes over nothing, here a check that
# fails over everything. Both are a verdict that does not depend on its subject.
NOT_A_WITNESS = {"provenance"}

# The other half of the same problem, and the one that would have bitten a
# reader rather than the author.
#
# Thirteen tests in the fast label are SKIPPABLE, and out/ is gitignored, so on
# a fresh clone six of them skip for want of out/figures.json. ctest reports a
# skip in the same summary block it reports a failure in, as "name (Skipped)"
# beside "name (Failed)". Reading that block without the status would have made
# every mutant CAUGHT on any machine that had not spent twelve minutes running
# report_data first, which is every machine but this one.
#
# So a skip is never a witness, and a run in which anything skipped cannot
# establish that a fault SURVIVED either: the tests that would have caught it
# may be the ones that did not run. That verdict is INCONCLUSIVE and names them.
INCONCLUSIVE = "INCONCLUSIVE"


class Mutant:
    """One planted fault, and the verdicts the data says each label produces."""

    def __init__(self, raw: dict[str, object]) -> None:
        self.id = str(raw["id"])
        self.file = str(raw["file"])
        self.what = str(raw["what"])
        self.find = str(raw["find"])
        self.replace = str(raw["replace"])
        expect = raw["expect"]
        assert isinstance(expect, dict)
        self.expect: dict[str, list[str] | None] = {}
        for label, names in expect.items():
            if names is None:
                self.expect[str(label)] = None
            else:
                assert isinstance(names, list)
                self.expect[str(label)] = sorted(str(t) for t in names)

    @property
    def path(self) -> str:
        return os.path.join(ROOT, self.file.replace("/", os.sep))


def load() -> list[Mutant]:
    with io.open(DATA, encoding="utf-8") as fh:
        raw = json.load(fh)
    return [Mutant(m) for m in raw["mutants"]]


def run(cmd: list[str], timeout: float) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                          timeout=timeout)


def dirty(paths: list[str]) -> list[str]:
    """Which of these files git reports as modified.

    A planted fault looks exactly like an edit, so the run must not start where
    it cannot tell one from the other. A git that cannot answer is therefore a
    refusal and not a clean tree: the first version returned [] on a non-zero
    exit, which turns a crashed git, a stale index.lock or a missing repository
    into permission to start a run on top of whatever the last campaign left
    behind.
    """
    proc = run(["git", "status", "--porcelain", "--"] + paths, timeout=60)
    if proc.returncode != 0:
        sys.exit("git could not report on {}: {}\nThe guard against a leftover "
                 "planted fault cannot run, so neither can this."
                 .format(", ".join(paths), proc.stderr.strip()[:300]))
    return [line[3:].strip() for line in proc.stdout.splitlines() if line.strip()]


def build() -> tuple[bool, str]:
    proc = run(["cmake", "--build", BUILD], timeout=3600)
    return proc.returncode == 0, (proc.stdout + proc.stderr)[-2000:]


def failing(selection: list[str],
            timeout: float) -> tuple[list[str] | None, list[str], str]:
    """(witnesses, skipped, tail). witnesses is None if ctest could not report.

    ctest names each non-passing test in its trailing summary, which is what
    makes a verdict a set of test names rather than an exit code. An exit code
    would say a mutant was caught; the names say by what, which is what the data
    records and what makes NOT_A_WITNESS possible at all.

    The status in that line is load bearing. ctest writes "name (Skipped)" in
    the same shape as "name (Failed)", so a pattern that reads the name and
    discards the status counts a test that did not run as one that caught
    something.
    """
    try:
        proc = run(["ctest", "--test-dir", BUILD] + selection +
                   ["--no-tests=error"], timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, [], "ctest did not finish inside {:.0f} s".format(timeout)
    out = proc.stdout + proc.stderr
    seen = re.findall(r"^\s*\d+ - (\S+) \((\w+)\)", out, re.M)
    skipped = sorted({n for n, status in seen if status == "Skipped"})
    failed = {n for n, status in seen if status != "Skipped"}

    # The guard reads what ctest named, before NOT_A_WITNESS is applied.
    #
    # It is there for the case where ctest exits non-zero and names no test at
    # all, which is a harness problem rather than a verdict. Applying the
    # exclusion first breaks it: every mutant makes provenance fail and nothing
    # else has to, so a fault that genuinely survives leaves ctest exiting
    # non-zero with an empty witness list, and the run reports NO VERDICT for
    # the one outcome it most needs to be able to state.
    if proc.returncode != 0 and not failed:
        return None, skipped, out[-2000:]
    return sorted(failed - NOT_A_WITNESS), skipped, out[-400:]


def probe(m: Mutant, selection: list[str], timeout: float) -> tuple[str, list[str]]:
    """Plant, build, run, restore. Returns (verdict, witnessing test names)."""
    original = io.open(m.path, "rb").read()
    text = original.decode("utf-8")
    occurrences = text.count(m.find)
    if occurrences != 1:
        return "NO SITE ({} occurrences)".format(occurrences), []

    try:
        io.open(m.path, "wb").write(
            text.replace(m.find, m.replace, 1).encode("utf-8"))
        ok, log = build()
        if not ok:
            print("    build failed:\n      " + log.replace("\n", "\n      "),
                  flush=True)
            return "DOES NOT BUILD", []
        names, skipped, tail = failing(selection, timeout)
        if names is None:
            print("    ctest could not report:\n      " +
                  tail.replace("\n", "\n      "), flush=True)
            return "NO VERDICT", []
        if skipped:
            print("    did not run: " + ", ".join(skipped), flush=True)
        if names:
            return "CAUGHT", names
        if skipped:
            # Nothing failed and something did not run, so this says nothing
            # about the fault: the test that would have caught it may be one of
            # the ones that skipped.
            return INCONCLUSIVE, []
        return "SURVIVED", []
    finally:
        io.open(m.path, "wb").write(original)
        restored = io.open(m.path, "rb").read()
        if restored != original:
            sys.exit("FATAL: {} was not restored; fix it before anything "
                     "else".format(m.file))
        # The binaries must not outlive the mutant either, and a rebuild that
        # failed and was not looked at leaves exactly that behind.
        ok, log = build()
        if not ok:
            sys.exit("FATAL: {} was restored but the rebuild after it failed, "
                     "so build/ still holds binaries compiled from the mutant. "
                     "Rebuild before running anything.\n{}"
                     .format(m.file, log))


def record(measured: dict[str, dict[str, list[str]]]) -> None:
    """Merge the measured verdicts into the data file, in place.

    Merge rather than replace. A label that did not settle, because ctest timed
    out or the mutant did not build, produces no measurement, and replacing the
    whole dict would delete that label's entry: the run would silently retire
    the question instead of leaving it open. Replacing also dropped any label
    this run was not asked to probe.
    """
    with io.open(DATA, encoding="utf-8") as fh:
        raw = json.load(fh)
    for entry in raw["mutants"]:
        got = measured.get(str(entry["id"]))
        if not got:
            continue
        expect = dict(entry.get("expect") or {})
        expect.update(got)
        entry["expect"] = {k: expect[k] for k in sorted(expect)}
    with io.open(DATA, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(raw, fh, indent=2)
        fh.write("\n")
    print("\ntools/mutants.json rewritten. Read the diff before committing it: "
          "--record cannot tell a suite that never caught a fault from one that "
          "has stopped.", flush=True)


def main() -> int:
    ap = argparse.ArgumentParser(description="Run the mutation campaign.")
    ap.add_argument("--list", action="store_true",
                    help="print the mutants and their recorded verdicts")
    ap.add_argument("--only", default="", help="comma-separated mutant ids")
    ap.add_argument("--label", default="",
                    help="one ctest label, instead of the labels each entry records")
    ap.add_argument("--tests", default="", help="ctest -R regex, instead of a label")
    ap.add_argument("--record", action="store_true",
                    help="write the measured verdicts into tools/mutants.json")
    ap.add_argument("--timeout", type=float, default=7200.0,
                    help="seconds allowed for one ctest run")
    args = ap.parse_args()

    mutants = load()
    if args.only:
        wanted = {s.strip() for s in args.only.split(",") if s.strip()}
        unknown = sorted(wanted - {m.id for m in mutants})
        if unknown:
            sys.exit("no such mutant: {}".format(", ".join(unknown)))
        mutants = [m for m in mutants if m.id in wanted]

    if args.list:
        for m in mutants:
            for label in sorted(m.expect):
                names = m.expect[label]
                print("{:<16} {:<5} {}".format(
                    m.id, label,
                    "(unestablished)" if names is None
                    else ", ".join(names) or "caught by nothing"))
            print("{:<16} {:<5} {}  [{}]".format("", "", m.what, m.file))
        return 0

    if not os.path.isdir(BUILD):
        sys.exit("no build directory; run cmake --preset release first")

    soiled = dirty(sorted({m.file for m in mutants}))
    if soiled:
        sys.exit("these files are already modified, so a planted fault could "
                 "not be told from an edit: {}".format(", ".join(soiled)))

    override = ["-R", args.tests] if args.tests else (
        ["-L", args.label] if args.label else [])
    if args.record and override:
        # An explicit selection has no label name to file a verdict under, and
        # the first version filed one under the literal string "override",
        # overwriting every real entry in the file with a verdict from a
        # selection nobody can reconstruct. --record establishes what a LABEL
        # does, so it takes the labels the data names.
        sys.exit("--record cannot be combined with --label or --tests: a "
                 "verdict is recorded against the label that produced it, and "
                 "an explicit selection is not one. Run --record on its own, or "
                 "add the label to the entry in tools/mutants.json first.")
    print("mutants: {}    excluded from every verdict: {}\n".format(
        len(mutants), ", ".join(sorted(NOT_A_WITNESS))), flush=True)

    measured: dict[str, dict[str, list[str]]] = {}
    disagreed: list[tuple[str, str, list[str] | None, list[str]]] = []
    # Counted here rather than summed out of `measured`, which holds only the
    # runs that settled. A clone with no out/figures.json makes every fast probe
    # INCONCLUSIVE, and the summary would then have reported zero label runs
    # alongside twelve disagreements arising from them, after two hours of work.
    ran = 0
    for m in mutants:
        print("{:<16} {}".format(m.id, m.what), flush=True)
        labels = ["override"] if override else sorted(m.expect)
        for label in labels:
            ran += 1
            selection = override if override else ["-L", label]
            started = time.time()
            verdict, names = probe(m, selection, args.timeout)
            took = time.time() - started
            want = None if override else m.expect[label]
            settled = verdict in ("CAUGHT", "SURVIVED")
            agrees = settled and want is not None and names == want
            if settled:
                measured.setdefault(m.id, {})[label] = names
            print("    {:<5} {:<9} {:>5.0f}s  by {}   {}".format(
                label, verdict, took, ", ".join(names) or "nothing",
                "as recorded" if agrees else
                ("measured" if override or want is None
                 else "NOT WHAT tools/mutants.json RECORDS")), flush=True)
            if not override and not agrees:
                disagreed.append((m.id, label, want, names))

    if args.record:
        record(measured)
        return 0
    if override:
        print("\nan explicit selection was given, so nothing was compared "
              "against the record", flush=True)
        return 0

    print("\n{} mutants, {} label runs, {} settled, {} disagreeing with the "
          "record".format(len(mutants), ran,
                          sum(len(v) for v in measured.values()), len(disagreed)),
          flush=True)
    for mid, label, want, names in disagreed:
        print("  {:<16} {:<5} record says {}, this run says {}".format(
            mid, label,
            "nothing yet" if want is None else (", ".join(want) or "nothing"),
            ", ".join(names) or "nothing"))
    if disagreed:
        print("\nThe record is what is wrong here, or the suite is. Decide "
              "which, then edit tools/mutants.json; do not clear an expect "
              "list to make this green.")
    return 1 if disagreed else 0


if __name__ == "__main__":
    sys.exit(main())
