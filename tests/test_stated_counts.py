"""Counts written into prose, against the configuration that decides them.

Every number here had drifted at least once, and one of them twice. The commit
before this file was written is titled "docs: Correct the test counts against
ctest"; it fixed docs/CI.md, missed the identical claim in the workflow's own
comment, and was followed within one change set by a new test that made both
wrong again. A count maintained by hand is a count that is wrong between the
moment someone adds a test and the moment someone notices.

So nothing here is typed twice. Each claim is read out of the prose and compared
against the thing that decides it:

  - the fast label's size, from the mf_test registrations in CMakeLists.txt;
  - how many tests wait on out/figures.json, from the list the nightly report
    pipeline runs for real;
  - the interpreter-gated and Node-gated counts, from the guard list in ci.yml
    and from the Python-and-Node block in CMakeLists.txt;
  - and the guard list itself against the registrations, so a test that gains
    an interpreter dependency without joining the list is caught here rather
    than by a CI leg quietly running a smaller suite.

CMakeLists.txt is parsed rather than ctest queried, so this works on a clean
checkout with no build directory, which is where a documentation check belongs.

Not covered here: docs/BENCHMARKS.md's ladder check count. That number is what
test_ladder prints at the end of a seven second run, and there is no honest way
to derive it from source, which is what this file does. It was the one count
maintained by hand, and it drifted a third time the moment test_ladder gained a
second assertion per rung comparison. It is now checked by running the thing:
tests/test_tools_output.py starts binaries and reads what they print already,
and seven seconds is nothing beside what that test already spends.
"""

from __future__ import annotations

import io
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _harness import ROOT, check, report  # noqa: E402

RESULTS = os.path.join(ROOT, "experiments", "results.json")
CMAKE = os.path.join(ROOT, "CMakeLists.txt")
CI = os.path.join(ROOT, ".github", "workflows", "ci.yml")
NIGHTLY = os.path.join(ROOT, ".github", "workflows", "nightly.yml")
README = os.path.join(ROOT, "README.md")
CI_DOC = os.path.join(ROOT, "docs", "CI.md")

WORDS = {
    "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
    "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12,
    "thirteen": 13, "fourteen": 14, "fifteen": 15, "sixteen": 16,
    "seventeen": 17, "eighteen": 18, "nineteen": 19, "twenty": 20,
    # Through thirty because twenty was the ceiling when static_types took the
    # count from nineteen to twenty, and a table one short of the next test is
    # a trap: stated() would return None and the failure would read "prose says
    # None", accusing a correct sentence of drifting.
    #
    # Every entry from twenty-one down was unreachable until the registry test
    # took the count past twenty and made one of them the answer. The five word
    # slots below captured (\w+), which cannot cross a hyphen, so "Twenty-one
    # tests" matched "one" in docs/CI.md and matched nothing at all in ci.yml,
    # where the pattern is anchored to the comment marker. The table was right
    # and could not be reached, which is the same shape as a guard whose branch
    # no test enters. The slots now capture ([\w-]+).
    "twenty-one": 21, "twenty-two": 22, "twenty-three": 23, "twenty-four": 24,
    "twenty-five": 25, "twenty-six": 26, "twenty-seven": 27, "twenty-eight": 28,
    "twenty-nine": 29, "thirty": 30,
}

# mf_test calls are not all at column zero: the ones inside an if() block are
# indented, and bond_dimension is one of them. An anchored pattern without the
# leading whitespace missed it, which undercounted the fast label by one and
# reported a registered test as unregistered.
CALL = r"^[ \t]*mf_test\(\s*(\w+)"


def read(path: str) -> str:
    return io.open(path, encoding="utf-8").read()


def mf_tests(text: str) -> list[tuple[str, str]]:
    """Every mf_test registration, as (name, body).

    The call spans lines, so the body runs to the next registration. Good
    enough for counting, and the first check below fails loudly rather than
    quietly if the file stops looking like this.
    """
    out = []
    for m in re.finditer(CALL, text, re.M):
        start = m.end()
        nxt = re.search(CALL, text[start:], re.M)
        out.append((m.group(1), text[start:start + nxt.start()] if nxt else text[start:]))
    return out


def guard_lists(text: str) -> list[list[str]]:
    """Every test-name list a ci.yml registration loop walks, in file order.

    Every one of them, because there are two: the linux build job writes the
    loop out and the windows job writes it out again. This returned after the
    first for years, so the windows copy was guarded by nothing, which is the
    silent-smaller-suite failure this file's docstring says it exists to catch.
    """
    lines = text.split("\n")
    out = []
    for i, ln in enumerate(lines):
        if "for t in" in ln:
            block, j = [], i
            while True:
                block.append(lines[j])
                if not lines[j].rstrip().endswith("\\"):
                    break
                j += 1
            joined = " ".join(b.replace("\\", " ") for b in block)
            out.append(joined.split("for t in", 1)[1].split("; do")[0].split())
    return out


def stated(text: str, pattern: str) -> int | None:
    """A count written as a word or a numeral, or None when absent."""
    m = re.search(pattern, text, re.I)
    if m is None:
        return None
    token = m.group(1).lower()
    return WORDS.get(token, int(token) if token.isdigit() else None)


def main() -> int:
    print("counts in prose, against the configuration that decides them")
    print("============================================================")

    cmake, ci, nightly = read(CMAKE), read(CI), read(NIGHTLY)
    tests = mf_tests(cmake)
    check(len(tests) > 30,
          "the CMakeLists parse found the registrations",
          "found {}, which means the file stopped looking like this".format(len(tests)))

    fast = [n for n, body in tests if re.search(r"LABEL\s+fast\b", body)]

    node_block = re.search(
        r"if\(Python3_Interpreter_FOUND AND MF_NODE\)(.*?)\nendif\(\)", cmake, re.S)
    check(bool(node_block), "the Python-and-Node block was located in CMakeLists.txt")
    node_gated = re.findall(CALL, node_block.group(1), re.M) if node_block else []

    lists = guard_lists(ci)
    check(len(lists) >= 2, "ci.yml's registration guard lists were located",
          "found {} of them, where the linux and windows jobs carry one each".format(len(lists)))
    disagree = [i for i, l in enumerate(lists) if l != lists[0]]
    check(not disagree,
          "and every job walks the same list",
          "list(s) {} differ from the first: {}".format(
              disagree, [sorted(set(lists[i]) ^ set(lists[0])) for i in disagree]))
    names = lists[0] if lists else []
    check(len(names) > 5, "which names more than a handful of tests",
          "found {} names".format(len(names)))

    # Not every SKIPPABLE test waits on out/figures.json: the label also covers
    # a missing binary and a missing interpreter. The set that waits on the
    # figure data is exactly the set the nightly report pipeline runs for real,
    # so that regex is what README's count has to agree with.
    m = re.search(r"ctest --test-dir build -R '\^\(([^)]*)\)\$'", nightly)
    check(bool(m), "the nightly report pipeline's test list was located")
    figure_gated = m.group(1).split("|") if m else []

    # 1. The guard list is the set of interpreter-gated tests, neither more nor
    #    less. A test that gains an interpreter and not a guard entry runs on
    #    whichever legs happen to have one, and the suite shrinks in silence.
    registered = {n for n, _ in tests}
    unknown = sorted(set(names) - registered)
    check(not unknown,
          "every name in the guard list is a registered test",
          "guard names nothing registers: " + ", ".join(unknown))
    unguarded = sorted(set(node_gated) - set(names))
    check(not unguarded,
          "and every Node-gated test is named in the guard",
          "registered but unguarded: " + ", ".join(unguarded))
    ungated = sorted(set(figure_gated) - registered)
    check(not ungated,
          "and every test the report pipeline runs is registered",
          "named but not registered: " + ", ".join(ungated))

    # 2. The prose.
    claims = [
        (README, r"#\s+\d+\s*s,\s*(\d+)\s+tests", len(fast),
         "README's fast-label count"),
        (README, r"([\w-]+) tests report `Skipped` until", len(figure_gated),
         "README's figure-data Skipped count"),
        # nightly.yml says of its gate list that "the list is the fact and
        # everything else is derived from it", and until these two entries
        # only README's copy was. Both of these were edited by hand from five
        # to six and neither would have failed if they had not been: the same
        # shape as docs/CI.md naming three for two rounds after the list
        # reached five, which is the drift that comment records.
        (CI_DOC, r"the ([\w-]+) figure-data tests", len(figure_gated),
         "docs/CI.md's figure-data count"),
        (CI, r"so the ([\w-]+) figure-data", len(figure_gated),
         "ci.yml's figure-data count"),
        (CI_DOC, r"([\w-]+) tests\s+are registered only when CMake finds Python", len(names),
         "docs/CI.md's interpreter-gated count"),
        (CI_DOC, r"([\w-]+) of them needing Node", len(node_gated),
         "docs/CI.md's Node-gated count"),
        (CI, r"#\s*([\w-]+) tests are registered only when CMake finds Python", len(names),
         "ci.yml's interpreter-gated count"),
        (CI, r"and ([\w-]+) of\s*\n\s*#\s*those need Node", len(node_gated),
         "ci.yml's Node-gated count"),
    ]
    for path, pattern, actual, what in claims:
        got = stated(read(path), pattern)
        check(got == actual, what + " matches the configuration",
              "prose says {!r}, configuration says {}".format(got, actual))

    # 2b. The dossier's own counts, which README states and nothing checked.
    #
    # README is the only hand-kept copy of these three. out/results.html
    # renders them from experiments/results.json two lines from where README
    # states them, and the two drifted once already: section 22 found README
    # saying 96 and 71 where the page rendered 118 and 93. The file is tracked,
    # so unlike the figure data it is here on a clean clone and this needs no
    # skip.
    record = json.loads(read(RESULTS))
    counts = record["counts"]
    check(counts["results"] == len(record["results"]),
          "results.json's own count matches the rows it carries",
          "counts says {}, there are {}".format(counts["results"],
                                                len(record["results"])))
    readme = read(README)
    for pattern, key, what in [
            (r"companion:\s*([\d,]+) recorded quantities", "results", "the dossier row count"),
            (r"recorded quantities,\s*([\d,]+) exact", "exact", "the exact count"),
            (r"exact and\s+([\d,]+)\s+measured", "measured", "the measured count")]:
        m = re.search(pattern, readme)
        check(bool(m), "README states " + what)
        if m:
            got = int(m.group(1).replace(",", ""))
            check(got == counts[key],
                  "and " + what + " matches experiments/results.json",
                  "README says {}, the record says {}".format(got, counts[key]))

    # 2c. The extended label's budget, against the budget it is given.
    #
    # Two workflow comments stated this arithmetic and both were stale: they
    # said thirteen tests at 900 and two at 1800 for 240 minutes, where the
    # registrations say twelve and three for 270, and both jobs then set a
    # timeout of 260. The budget was under the worst case, which is the fault
    # nightly.yml records finding on the uniformity job. Raising one TIMEOUT
    # in CMakeLists is all it takes, and nothing read either comment.
    #
    # ctest applies a default when a registration carries no TIMEOUT, so a pr
    # test without one is refused rather than counted as zero.
    pr = [(n, re.search(r"TIMEOUT\s+(\d+)", body)) for n, body in tests
          if re.search(r"LABEL\s+pr\b", body)]
    untimed = sorted(n for n, m in pr if m is None)
    check(not untimed, "every test in the pr label carries a TIMEOUT",
          "no TIMEOUT on " + ", ".join(untimed))
    worst = sum(int(m.group(1)) for _, m in pr if m) // 60
    buckets: dict[str, int] = {}
    for _, m in pr:
        if m:
            buckets[m.group(1)] = buckets.get(m.group(1), 0) + 1

    for path, what in ((CI, "ci.yml"), (NIGHTLY, "nightly.yml")):
        text = read(path)
        m = re.search(r"-L pr is ([\w-]+) tests, ([\w-]+) at TIMEOUT (\d+) and "
                      r"([\w-]+) at (\d+), so ctest[\s\S]{0,80}?may spend (\d+) minutes",
                      text)
        check(bool(m), what + " states the pr label's worst case")
        if not m:
            continue
        stated_total = WORDS.get(m.group(1).lower())
        lo, hi = m.group(3), m.group(5)
        check(stated_total == len(pr),
              what + "'s pr test count matches the registrations",
              "comment says {!r}, CMakeLists says {}".format(m.group(1), len(pr)))
        check(WORDS.get(m.group(2).lower()) == buckets.get(lo),
              what + " counts the {}-second tests correctly".format(lo),
              "comment says {!r}, there are {}".format(m.group(2), buckets.get(lo)))
        check(WORDS.get(m.group(4).lower()) == buckets.get(hi),
              what + " counts the {}-second tests correctly".format(hi),
              "comment says {!r}, there are {}".format(m.group(4), buckets.get(hi)))
        check(int(m.group(6)) == worst,
              what + "'s stated worst case matches the registrations",
              "comment says {} minutes, the registrations give {}".format(
                  m.group(6), worst))
        budget = re.search(r"timeout-minutes:\s*(\d+)", text[m.end():])
        check(bool(budget), what + " sets a budget under that comment")
        if budget:
            check(int(budget.group(1)) > worst,
                  what + "'s budget is above the worst case",
                  "budget {} minutes against a worst case of {}".format(
                      budget.group(1), worst))

    # 3. One constant, three languages' worth of homes.
    #
    # The 95 percent normal quantile is written in C++, in the report layer and
    # in the analysis layer, because none of the three can include either of the
    # others. That is the arrangement folds.hpp and python/stats.py already have,
    # and it is only safe with the pin those two have. Without one it drifted:
    # 1.959963985 here, 1.959964 there, both reaching the same page.
    # The Python patterns allow an optional annotation. Neither constant carries
    # one, and both would have stopped matching the moment it did, reporting the
    # quantile as missing from a file that states it correctly.
    PY = r"^Z_95\s*(?::\s*[\w.\[\]]+\s*)?=\s*([0-9.]+)"
    homes = [
        ("include/mayflower/constants.hpp", r"kZ95\s*=\s*([0-9.]+)"),
        ("tools/report_style.py", PY),
        ("python/stats.py", PY),
    ]
    found = {}
    for rel, pattern in homes:
        m = re.search(pattern, read(os.path.join(ROOT, rel)), re.M)
        check(bool(m), "the 95 percent quantile was located in " + rel)
        if m:
            found[rel] = m.group(1)
    check(len(set(found.values())) == 1,
          "and every language writes the same digits",
          "; ".join("{} says {}".format(p, v) for p, v in found.items()))
    # Correctly rounded, not merely agreeing. Three copies of a wrong value
    # agree too, and the previous two spellings were out by 4.6e-10 and 1.5e-8.
    # Every home, not one of them: checking a single home passes while another
    # drifts, which is the hole the agreement check above exists to cover and
    # no reason for this one to leave the same hole open.
    wrong = {p: v for p, v in found.items()
             if abs(float(v) - 1.959963984540054) >= 1e-15}
    check(not wrong,
          "and it is the value, to the last bit a double carries",
          "; ".join("{} says {}".format(p, v) for p, v in wrong.items()))

    # 4. The fleet, likewise.
    #
    # constants.hpp opens "Every module imports from here; nothing hardcodes
    # them", and web/live.js repeats that rule in its own comment and then
    # hardcodes {5,4,3,3,2} anyway, as does tools/sweep_timing.mjs. Generating
    # constants into JavaScript was considered and declined; a pin costs three
    # lines and catches the same drift.
    m = re.search(r"kFleet\[kFleetSize\]\s*=\s*\{([^}]*)\}",
                  read(os.path.join(ROOT, "include", "mayflower", "constants.hpp")))
    check(bool(m), "the fleet was located in constants.hpp")
    fleet = [s.strip() for s in m.group(1).split(",")] if m else []
    for rel, pattern in [("web/live.js", r"LENS\s*=\s*\[([^\]]*)\]"),
                         ("tools/sweep_timing.mjs", r"makeInstance\(10, 10, \[([^\]]*)\]")]:
        m2 = re.search(pattern, read(os.path.join(ROOT, rel)))
        carried = [s.strip() for s in m2.group(1).split(",")] if m2 else None
        check(carried == fleet, rel + " carries the fleet constants.hpp declares",
              "{} against {}".format(carried, fleet))

    print("  ({} fast, {} gated on the figure data, {} interpreter-gated, "
          "{} needing Node, {} guard lists agreeing, fleet {})".format(
              len(fast), len(figure_gated), len(names), len(node_gated), len(lists),
              ",".join(fleet)))
    return report()


if __name__ == "__main__":
    sys.exit(main())
