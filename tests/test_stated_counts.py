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
CORRECTNESS = os.path.join(ROOT, "docs", "CORRECTNESS.md")
MUTANTS = os.path.join(ROOT, "tools", "mutants.json")

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
        # README's prerequisites table states it too, which made three hand-kept
        # copies of one count in the file whose job is to leave none. Adding a
        # Python-gated test failed the two entries above and left this one
        # green, so the number that prompted this file to be written could still
        # drift in the one place the file did not look.
        (README, r"([\w-]+) tests are registered only when CMake finds an interpreter",
         len(names), "README's interpreter-gated count"),
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

    # 5. The ccache key four jobs share, against the build they configure.
    #
    # The build matrix composes its key from ${{ matrix.os }}-${{ matrix.cxx
    # }}-${{ matrix.build_type }}. One job in ci.yml and three in nightly.yml
    # reuse the cache that first leg fills, and all four wrote the result of
    # that composition out as a literal two hundred lines away from the template
    # that produces it. Nothing connected them, so moving the matrix to another
    # runner or compiler would have left three jobs with 180 to 300 minute
    # budgets silently rebuilding from cold.
    #
    # The components are named in each workflow's env now, and this is what
    # holds them together: the two files agree, a matrix leg exists that fills
    # the key they compose, no job has gone back to a literal, and every job
    # reusing that cache runs on the image the key claims. runs-on cannot read
    # the env context, which is why that last one has to be checked rather than
    # derived away.
    parts = ("CACHE_OS", "CACHE_CXX", "CACHE_BUILD_TYPE")
    named: dict[str, dict[str, str]] = {}
    for rel, path in (("ci.yml", CI), ("nightly.yml", NIGHTLY)):
        text = read(path)
        declared: dict[str, str] = {}
        for part in parts:
            hits = re.findall(r"^  " + part + r": (\S+)$", text, re.M)
            check(len(hits) == 1, rel + " names " + part + " once at workflow env",
                  "{} occurrences".format(len(hits)))
            if hits:
                declared[part] = hits[0]
        named[rel] = declared
    check(named["ci.yml"] == named["nightly.yml"],
          "the two workflows agree on the shared build",
          "{} against {}".format(named["ci.yml"], named["nightly.yml"]))

    ci_text = read(CI)
    shared = named["ci.yml"]
    template = ("${{ matrix.os }}-${{ matrix.cxx }}-${{ matrix.build_type }}")
    check(("key: " + template) in ci_text,
          "the build matrix still composes its key from the matrix",
          "looked for " + template)

    # A matrix leg has to produce exactly the key the other four ask for, or
    # they share a cache nothing fills. Entries start at "- name:" and carry
    # their three components before the next one begins.
    entries = re.split(r"\n\s+- name: ", ci_text[ci_text.index("      include:"):])
    wanted = {"os": shared.get("CACHE_OS"), "cxx": shared.get("CACHE_CXX"),
              "build_type": shared.get("CACHE_BUILD_TYPE")}
    matched = [e for e in entries
               if all(re.search(r"^\s+" + k + r": " + re.escape(v) + r"\s*$", e, re.M)
                      for k, v in wanted.items() if v)]
    check(len(matched) == 1, "exactly one matrix leg fills the shared cache",
          "{} legs carry {}".format(len(matched), wanted))

    # No job may go back to a literal. The comment explaining why still names
    # the old string, so this looks only at what a ccache-action step is given.
    #
    # The count is checked as well as the values, and that is the load-bearing
    # half. The pattern requires key: to be the first input under with:, so a
    # step that gained a max-size: above it would not be matched at all, and an
    # unmatched step contributes no key and therefore never fails the test
    # below. Comparing against the number of ccache-action steps in the file
    # turns "this key is composed" into "every key is, and none was missed".
    composed = ("${{ env.CACHE_OS }}-${{ env.CACHE_CXX }}-"
                "${{ env.CACHE_BUILD_TYPE }}")
    allowed = {template, composed, "sanitizers-${{ matrix.name }}"}
    for rel, path in (("ci.yml", CI), ("nightly.yml", NIGHTLY)):
        text = read(path)
        steps = len(re.findall(r"uses: hendrikmuhs/ccache-action@", text))
        keys = re.findall(r"uses: hendrikmuhs/ccache-action@[^\n]*\n"
                          r"\s+with:\n\s+key: ([^\n]+)\n", text)
        check(bool(keys), rel + " declares at least one ccache key")
        check(len(keys) == steps,
              rel + "'s ccache steps all had their key read",
              "{} steps, {} keys read".format(steps, len(keys)))
        typed = [k for k in keys if k not in allowed]
        check(not typed, rel + "'s ccache keys are all composed rather than typed",
              "typed: {}".format(typed))

        # Every job that reuses the shared cache must run on the image the key
        # says it does, since a key describing one runner and a job running on
        # another is a cache that is filled and never read.
        #
        # The split accepts any identifier a job id may start with. Restricting
        # it to a lowercase initial would merge a job named Publish: into the
        # one above it, and that job's runs-on would then never be read: the
        # check would pass by not seeing it.
        for block in re.split(r"\n  (?=[A-Za-z_][\w-]*:\n)", text):
            if composed not in block:
                continue
            job = block.strip().split(":")[0]
            on = re.search(r"^    runs-on: (\S+)$", block, re.M)
            runner = on.group(1) if on is not None else None
            check(runner == shared.get("CACHE_OS"),
                  rel + "'s " + job + " job runs on the image its cache key names",
                  "runs-on {}, key says {}".format(runner, shared.get("CACHE_OS")))

    # 6. The one pinned dependency, in the two places that name it.
    #
    # mypy is the only thing outside the standard library this project installs,
    # and README tells a reader which version to install while ci.yml installs
    # it. Two hand-kept copies of one version, and the version is the whole
    # point of a pin: --strict changes between releases, so a README that names
    # a different one sends a reader to a checker that disagrees with the gate.
    pin = re.search(r"pip install mypy==(\S+)", read(CI))
    check(pin is not None, "ci.yml pins the type checker")
    if pin is not None:
        said = re.search(r"pip install mypy==([^\s`]+)", read(README))
        check(said is not None and said.group(1) == pin.group(1),
              "README names the version ci.yml installs",
              "README {}, ci.yml {}".format(
                  said.group(1) if said is not None else None, pin.group(1)))

    # 7. The tests allowed to skip in the report pipeline, against what can skip.
    #
    # That job runs the whole fast label and fails on any skip it does not
    # name, which is what stops a SKIPPABLE test skipping on every runner
    # forever. The names were typed into the workflow and again into
    # docs/CI.md, and the first version of both got the list wrong: it said six
    # where thirteen fast tests carry SKIPPABLE, and omitted static_types, which
    # is one of the two that actually skips there.
    #
    # So both sides are derived here. Every name the workflow allows must be a
    # fast SKIPPABLE test, or the allowance covers nothing. Every fast SKIPPABLE
    # test must be either in the figure-data gate list or in docs/CI.md's list
    # of the ones this step protects, so a new SKIPPABLE test cannot be added
    # without appearing in one of them.
    # mf_tests() runs each body to the NEXT registration, which is good enough
    # for counting and wrong here: the comment introducing the following test
    # falls inside the previous test's body, so stats and stated_counts both
    # came back SKIPPABLE off the word in the paragraph beneath them. This reads
    # the call itself, closing paren and all, and checks it found as many
    # registrations as the looser parse did so a body with a paren in it cannot
    # be skipped silently.
    calls = re.findall(r"mf_test\(\s*([\w-]+)((?:[^()]|\([^()]*\))*)\)", cmake, re.S)
    check(len(calls) == len(tests),
          "every registration parses as a complete mf_test call",
          "{} complete against {} found".format(len(calls), len(tests)))
    skippable = {name for name, body in calls
                 if "SKIPPABLE" in body and re.search(r"LABEL\s+fast\b", body)}
    check(len(skippable) > 5, "the fast label's SKIPPABLE tests were located",
          "{} found".format(len(skippable)))

    m = re.search(r'allowed = \{([^}]*)\}', read(NIGHTLY))
    check(m is not None, "the report pipeline names the skips it allows")
    if m is not None:
        allow = {s.strip().strip('"') for s in m.group(1).split(",") if s.strip()}
        stray = sorted(allow - skippable)
        check(not stray, "every allowed skip is a fast SKIPPABLE test",
              "not skippable: {}".format(stray))

        # The two it allows are the two that skip on a Linux runner with no
        # mypy. Naming more than that would let a real skip through.
        check(allow == {"platform", "static_types"},
              "the allowed skips are the two that skip there by construction",
              "allows {}".format(sorted(allow)))

        protected = sorted(skippable - set(figure_gated) - allow)
        doc = read(CI_DOC)
        documented = [n for n in protected if "`" + n + "`" in doc]
        check(documented == protected,
              "docs/CI.md names every SKIPPABLE test the step protects",
              "missing: {}".format(sorted(set(protected) - set(documented))))

        # And the counts those two paragraphs open with, which the names alone
        # do not cover. Adding a SKIPPABLE fast test and dutifully adding it to
        # docs/CI.md's list left every check above green while both paragraphs
        # went on saying thirteen and seven, in the one place docs/CI.md claims
        # to hold no hand-kept copy of them.
        outside = len(skippable) - len(figure_gated)
        for text, pattern, actual, what in (
                (doc, r"([\w-]+) tests in the fast label are\s*\n?\s*`SKIPPABLE`",
                 len(skippable), "docs/CI.md's SKIPPABLE count"),
                (doc, r"other ([\w-]+) were in no protected list", outside,
                 "docs/CI.md's count of the ones no list covered"),
                (read(NIGHTLY), r"([\w-]+) fast tests are SKIPPABLE",
                 len(skippable), "nightly.yml's SKIPPABLE count"),
                (read(NIGHTLY), r"the other ([\w-]+) were in no protected",
                 outside, "nightly.yml's count of the ones no list covered")):
            got = stated(text, pattern)
            check(got == actual, what + " matches the registrations",
                  "prose says {}, the registrations give {}".format(got, actual))

    # 8. The interpreter floors, against every version either workflow installs.
    #
    # CMakeLists.txt warns below a floor, README advertises one, and the
    # workflows install what CI actually runs on. Four hand-kept numbers for two
    # facts. Move a leg to a newer interpreter and the warning, the README table
    # and the gate would disagree with nothing going red.
    #
    # Both files, and both spellings. The first version of this read ci.yml
    # alone and matched only `node-version:`, which misses the build matrix's
    # own `node:` entries feeding `node-version: ${{ matrix.node }}` and misses
    # nightly.yml entirely. It would have said "the lowest version CI installs"
    # while reading a strict subset of them, which is the shape of a check that
    # passes for the wrong reason.
    floors = {}
    for name, pattern in (("python", r"set\(MF_PYTHON_FLOOR ([\d.]+)\)"),
                          ("node", r"set\(MF_NODE_FLOOR (\d+)\)")):
        m = re.search(pattern, read(CMAKE))
        check(m is not None, "CMakeLists.txt states the " + name + " floor")
        if m is not None:
            floors[name] = m.group(1)

    both = read(CI) + "\n" + read(NIGHTLY)
    pinned = {
        "python": sorted(set(re.findall(r"python-version: '?([\d.]+)'?", both))),
        "node": sorted(set(re.findall(r"node(?:-version)?: '?(\d+)'?", both))),
    }
    for name in ("python", "node"):
        check(bool(pinned[name]), "the workflows install a " + name + " version")
        if not pinned[name]:
            continue
        lowest = min(pinned[name], key=lambda v: [int(p) for p in v.split(".")])
        check(lowest == floors.get(name),
              "the " + name + " floor is the lowest version CI installs",
              "floor {}, CI installs {}".format(floors.get(name), pinned[name]))
        # Anchored to the table cell, since an unanchored search would accept a
        # floor that is a prefix of the number README states.
        check(re.search(r"\|\s*" + name.capitalize() + r"\s*\|\s*>= "
                        + re.escape(str(floors.get(name))) + r"\b", readme)
              is not None,
              "README states the " + name + " floor CMakeLists.txt warns on",
              "looked for a table row saying >= {}".format(floors.get(name)))

    # 9. The werror preset, against every copy of the flags it reproduces.
    #
    # The preset exists so a warning that would fail CI fails locally first.
    # That holds only while the two carry the same flags, and the preset was a
    # fifth hand-kept copy of the string: add a -Wno-error= to the matrix after
    # a runner image bump and the preset would turn a warning CI tolerates into
    # a local hard error, which is the inversion the preset is meant to prevent.
    #
    # Every copy, not one of them. Comparing against nightly.yml's env alone
    # left the three in ci.yml unchecked, including the two matrix legs the
    # paragraph above names as the thing that moves. The clang leg carries a
    # bare -Werror and is deliberately excluded: clang rejects the two
    # -Wno-error names outright, which is why the flags are per compiler.
    presets = json.loads(read(os.path.join(ROOT, "CMakePresets.json")))
    werror = next((p for p in presets["configurePresets"]
                   if p.get("name") == "werror"), None)
    check(werror is not None, "CMakePresets.json carries a werror preset")
    copies = re.findall(r"^\s*(?:WERROR|werror): (-Werror .*-Wno-error.*)$",
                        both, re.M)
    check(len(copies) == 4, "the four gcc -Werror copies were located",
          "{} found: {}".format(len(copies), copies))
    if werror is not None:
        have = werror["cacheVariables"].get("CMAKE_CXX_FLAGS")
        check({c.strip() for c in copies} == {have},
              "every gcc -Werror copy is the one the werror preset passes",
              "copies {}, preset {!r}".format(sorted({c.strip() for c in copies}),
                                              have))
        # And the other half of reproducing that leg. CMAKE_ARGS is where the
        # workflows turn -march=native off, so the preset is compared against
        # it rather than against a remembered value.
        native = re.findall(r"^  CMAKE_ARGS: (.+)$", both, re.M)
        check(len(native) == 2 and set(native) == {"-DMF_NATIVE=OFF"},
              "both workflows build with -march=native off",
              "found {}".format(native))
        check(werror["cacheVariables"].get("MF_NATIVE") == "OFF",
              "and the werror preset does too")

    # 10. The actions published by someone other than GitHub.
    #
    # Those are pinned to a digest, and three files say there are exactly two of
    # them. A third would be prose that is wrong and a pin that was never made,
    # and neither shows up as a failure anywhere else.
    #
    # The whole `uses:` value is read rather than an owner/repo pair, because an
    # action may be a sub-path (github/codeql-action/init) or a docker image,
    # and a pattern that matches only owner/repo skips both. Skipping them here
    # means skipping them in the count as well, so the one addition this check
    # exists to catch is the one it would not see.
    for rel, path in (("ci.yml", CI), ("nightly.yml", NIGHTLY)):
        for line in read(path).splitlines():
            m = re.search(r"uses:\s*(\S+)(.*)$", line)
            if m is None:
                continue
            spec, tail = m.group(1), m.group(2)
            if spec.startswith("actions/") or spec.startswith("./"):
                continue
            ref = spec.partition("@")[2]
            check(re.fullmatch(r"[0-9a-f]{40}", ref) is not None,
                  rel + " pins " + spec.partition("@")[0] + " to a commit",
                  "pinned to {!r}".format(ref or spec))
            check(re.search(r"#\s*v[\d.]+", tail) is not None,
                  rel + " records which release that is, for "
                  + spec.partition("@")[0],
                  "trailing text {!r}".format(tail.strip()))
    third_party = set()
    for path in (CI, NIGHTLY):
        for line in read(path).splitlines():
            m = re.search(r"uses:\s*(\S+)", line)
            if m is not None and not m.group(1).startswith(("actions/", "./")):
                third_party.add(m.group(1).partition("@")[0])
    # The number is read from the prose rather than typed a fourth time, so a
    # reword to "the three actions" fails here instead of shipping.
    counted = stated(read(CI_DOC),
                     r"the ([\w-]+) actions not published by GitHub")
    check(counted == len(third_party),
          "docs/CI.md counts the non-GitHub actions the workflows use",
          "prose says {}, workflows use {}".format(counted, sorted(third_party)))

    # 11. The mutation campaign's score, in the two places that quote it.
    #
    # tools/mutants.json is the record: a fault with a non-empty witness list in
    # any label is one the suite catches. README and docs/CORRECTNESS.md both
    # state that score in words, and both were wrong within an hour of being
    # written, because closing one survival moved it and neither sentence was
    # read by anything. That is this file's whole subject, arriving in the
    # document that describes the project's verification.
    mutants = json.loads(read(MUTANTS))["mutants"]
    check(len(mutants) > 3, "tools/mutants.json carries the campaign",
          "{} entries".format(len(mutants)))
    taken = sum(1 for m in mutants
                if any(v for v in m["expect"].values() if v is not None))
    for path, rel in ((README, "README"), (CORRECTNESS, "docs/CORRECTNESS.md")):
        text = read(path)
        m = re.search(r"([\w-]+) of the ([\w-]+) are caught", text, re.I)
        check(m is not None, rel + " states the campaign's score")
        if m is not None:
            check(WORDS.get(m.group(1).lower()) == taken
                  and WORDS.get(m.group(2).lower()) == len(mutants),
                  rel + "'s campaign score matches tools/mutants.json",
                  "prose says {} of {}, the record says {} of {}".format(
                      m.group(1), m.group(2), taken, len(mutants)))

    # And the table under it, which names the witnesses row by row. The score
    # alone would not catch a row crediting the wrong test, and one of them did:
    # the unrank row named test_uniformity where the witness is sampler.
    witnesses = {w for entry in mutants for names in entry["expect"].values()
                 if names for w in names}
    table = re.search(r"\| the fault planted \|.*?\n\n", read(CORRECTNESS), re.S)
    check(table is not None, "docs/CORRECTNESS.md carries the campaign table")
    if table is not None:
        drawn = set()
        for row in table.group(0).splitlines()[2:]:   # past the header and rule
            cells = [c.strip() for c in row.split("|")]
            if len(cells) < 4:
                continue
            for cell in cells[2:4]:
                for word in re.findall(r"[\w_]+", cell.replace("**", "")):
                    if word != "nothing":
                        drawn.add(word)
        check(drawn == witnesses,
              "every witness the table draws is one the record measured",
              "table {}, record {}".format(sorted(drawn), sorted(witnesses)))

    print("  ({} fast, {} gated on the figure data, {} interpreter-gated, "
          "{} needing Node, {} guard lists agreeing, fleet {}, cache {})".format(
              len(fast), len(figure_gated), len(names), len(node_gated), len(lists),
              ",".join(fleet),
              "-".join(shared.get(p, "?") for p in parts)))
    return report()


if __name__ == "__main__":
    sys.exit(main())
