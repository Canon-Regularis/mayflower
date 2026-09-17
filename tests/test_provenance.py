"""A published page names the engine that built it, and that engine is current.

The gap this closes. out/figures.json carried no commit, no version and no
timestamp: its meta held cells, entropyBits, games, instance, omega0 and
shipCells, all properties of the problem and none of the build. out/report.html
inherited that, so a page rendered from figure data seventeen commits old was
indistinguishable from one rendered a minute ago. experiments/results.json did
carry a commit, and it read 8e77fc3 against a HEAD of 1780c9e, seventeen commits
and thirty-one measurement-path files later, because collect_results.py only
ever runs as --check in CI and --check does not write.

Every one of those seventeen commits was checked by hand when it landed, which
is why the numbers were still right. None of it was enforced, and a hand check
that nothing repeats is a hand check that stops happening.

WHY THIS DOES NOT COMPARE AGAINST HEAD. The obvious test, that a page's stamp
equals HEAD, cannot hold. The stamp is written before the commit that contains
it exists, and out/ is gitignored, so the first commit after any regeneration
makes every artefact stale by that definition and the test fails until you
regenerate, whereupon committing makes it stale again. It would also fire on a
commit that edits nothing but this file, which cannot change a number.

What actually matters is narrower and checkable: an artefact must not be older
than the code whose output it claims to be. That is a freshness question, not an
identity question, so it is answered against the measurement path's modification
times. The commit stamp stays, because it tells a reader of the page which tree
produced it, which is worth having for its own sake.

Three properties, each catching a different way the chain breaks:

  1. The figure data names its commit and its fold.
  2. The page carries the same commit as the figure data it was built from, so a
     page re-rendered from data that has since moved on is caught.
  3. No artefact predates a source that feeds it.

The generated artefacts are gitignored, so a clean checkout skips rather than
fails.
"""

from __future__ import annotations

import io
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _harness import ROOT, SKIP, check, report  # noqa: E402

FIGURES = os.path.join(ROOT, "out", "figures.json")
REPORT = os.path.join(ROOT, "out", "report.html")
RESULTS = os.path.join(ROOT, "experiments", "results.json")

# A stamp from a tool that could not reach git. Not a mismatch, and not proof of
# anything either: the MSYS2 UCRT64 CI leg has no git at all.
UNKNOWN = "unknown"

# What decides the numbers in out/figures.json. The engine, its public headers
# and the tool that drives it.
#
# Wider than report_data's own object files: the src glob also picks up the
# platform and lattice sources it never links. That errs towards reporting
# staleness that is not there, never towards missing staleness that is, which
# is the safe direction to be wrong in.
FIGURE_SOURCES = (
    ("include/mayflower", (".hpp",)),
    ("src", (".cpp", ".hpp")),
    ("tools/report_data.cpp", None),
)

# What decides experiments/results.json on top of the figure data: the collector
# itself, the captured transcripts it parses, and the pre-registered runs.
RESULT_SOURCES = (
    ("tools/collect_results.py", None),
    ("docs", (".txt",)),
    ("experiments/headline_train.json", None),
    ("experiments/headline_test.json", None),
)


def walk(spec):
    """Every file a source specification names, as absolute paths."""
    out = []
    for rel, suffixes in spec:
        target = os.path.join(ROOT, rel.replace("/", os.sep))
        if suffixes is None:
            if os.path.isfile(target):
                out.append(target)
            continue
        for base, _dirs, names in os.walk(target):
            for name in names:
                if name.endswith(suffixes):
                    out.append(os.path.join(base, name))
    return out


def newer_than(artefact, spec):
    """Sources modified after the artefact was written, newest first.

    A one second slack, because a build that writes the artefact in the same
    second as it touches a source is not evidence of staleness, and some
    filesystems here carry two-second granularity on the other side of a copy.
    """
    stamp = os.path.getmtime(artefact) + 1.0
    late = [(os.path.getmtime(p), p) for p in walk(spec) if os.path.getmtime(p) > stamp]
    late.sort(reverse=True)
    return [os.path.relpath(p, ROOT).replace(os.sep, "/") for _t, p in late]


def head_commit():
    """HEAD at the short length the tools stamp, or None where git cannot say."""
    try:
        out = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"],
                                      cwd=ROOT, stderr=subprocess.DEVNULL)
        return out.decode().strip()
    except Exception:
        return None


def is_shallow():
    """Whether this clone holds only part of the history.

    CI checks out with actions/checkout's default fetch-depth of 1, so almost
    no commit resolves there. A shallow clone cannot answer whether a commit
    exists, and a test that reads "cannot answer" as "does not exist" fails for
    a reason that has nothing to do with the artefact.
    """
    try:
        out = subprocess.check_output(["git", "rev-parse", "--is-shallow-repository"],
                                      cwd=ROOT, stderr=subprocess.DEVNULL)
        return out.decode().strip() == "true"
    except Exception:
        return True


def commit_exists(rev):
    """Whether this repository has such a commit.

    Catches a stamp that never named anything, which a hand-edited artefact or a
    broken shell can produce. It deliberately does not ask whether the commit is
    an ancestor of HEAD: generating on a branch and reading on another is normal.

    Only meaningful on a full clone. Callers check is_shallow() first.
    """
    try:
        subprocess.check_output(["git", "cat-file", "-e", rev + "^{commit}"],
                                cwd=ROOT, stderr=subprocess.DEVNULL)
        return True
    except Exception:
        return False


def main():
    print("a page names the engine that built it, and nothing is stale")
    print("==========================================================")

    if not os.path.exists(FIGURES):
        print("out/figures.json is missing; run tools/report_data first")
        return SKIP

    meta = json.loads(io.open(FIGURES, encoding="utf-8").read())["meta"]
    # A shallow clone answers HEAD and resolves nothing else, so the
    # existence checks below are only asked where they can be answered.
    can_resolve = head_commit() is not None and not is_shallow()

    # 1. The figure data says what it is.
    check("commit" in meta,
          "out/figures.json carries the commit it was generated from",
          "meta holds only: " + ", ".join(sorted(meta)))
    # The fold is checked for presence, not for a value: a deliberate VAL run is
    # legitimate. What is not legitimate is a reader downstream assuming one,
    # which is how a 60/20/20 mixture came to be published as TRAIN.
    check("fold" in meta,
          "and the fold its policy rows were measured on",
          "meta holds only: " + ", ".join(sorted(meta)))

    stamped = meta.get("commit", UNKNOWN)
    if stamped != UNKNOWN and can_resolve:
        check(commit_exists(stamped),
              "and that commit exists in this repository",
              "figure data names {}, which git cannot resolve".format(stamped))

    # 2. The page belongs to the data it was built from.
    if os.path.exists(REPORT):
        page = io.open(REPORT, encoding="utf-8").read()
        m = re.search(r'data-commit="([^"]*)"', page)
        check(m is not None,
              "out/report.html carries the figure data's commit",
              "no data-commit attribute on the page")
        if m:
            check(m.group(1) == stamped,
                  "and it is the one out/figures.json carries",
                  "page says {}, figure data says {}; the page was built from "
                  "older data, so re-render it".format(m.group(1), stamped))
    else:
        print("  out/report.html is absent, so only the figure data is checked")

    # 3. Nothing predates what feeds it.
    late = newer_than(FIGURES, FIGURE_SOURCES)
    check(not late,
          "no engine source is newer than out/figures.json",
          "{} source(s) changed since it was generated, starting with {}; "
          "rerun build/report_data".format(len(late), ", ".join(late[:4])))

    if os.path.exists(REPORT):
        check(os.path.getmtime(REPORT) + 1.0 >= os.path.getmtime(FIGURES),
              "out/report.html is no older than the figure data",
              "the page was rendered before the figure data it reads")

    if os.path.exists(RESULTS):
        got = json.loads(io.open(RESULTS, encoding="utf-8").read()).get("commit", UNKNOWN)
        if got != UNKNOWN and can_resolve:
            check(commit_exists(got),
                  "experiments/results.json names a commit that exists",
                  "results.json names {}, which git cannot resolve".format(got))
        # Only against its own sources. The ordering against
        # out/figures.json used to be part of this and had to go: in the
        # nightly report pipeline results.json arrives at checkout and
        # figures.json is generated afterwards, so the clause is false on
        # every run of the one job that can execute this test. What it was
        # reaching for, that the two agree, is a content question, and
        # collect_results --check already answers it as the results test.
        stale = newer_than(RESULTS, RESULT_SOURCES)
        check(not stale,
              "experiments/results.json is no older than what it collects",
              "regenerate with python tools/collect_results.py; newer: "
              + ", ".join(stale[:4]))

    return report()


if __name__ == "__main__":
    sys.exit(main())
