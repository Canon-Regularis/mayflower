"""Gather every measured result into one machine-readable file.

The project's rule is that no number is transcribed by hand, so nothing here is
typed in. Everything is read from what a tool actually emitted: `out/figures.json`
for the core contract, and the captured runs in `docs/` for the extensions.

The text tables are parsed against their headers. A parser that cannot find its
exact header raises rather than guessing, so a tool changing its output breaks
this loudly instead of quietly producing a wrong dataset.

The point of pulling them together is not tidiness. Several quantities are
produced by more than one tool, and once they sit in one place they can be
checked against each other.

What those checks establish is narrower than this paragraph used to claim. It
said the adaptive optimum was "computed independently" by `m9` and `maxcover`,
and it is not: `tools/m9/adaptivity.cpp` and `tools/maxcover.cpp` both call
`solveOptimal` from the same library, differing only in the node cap they pass.
The non-adaptive optimum and the configuration count are two transcriptions of
one enumerator and one subset-lattice DP, written out in both tools. So none of
the four pairs is a second implementation, and _pairs_agree now says per pair
what it does catch. They are kept apart rather than merged because merging
would leave nothing checking them at all; the same call `src/search/detail/
outcome.hpp` records for buildWorld.

    python tools/collect_results.py            # writes experiments/results.json
    python tools/collect_results.py --check    # verify only, no write
"""

from __future__ import annotations

from collections.abc import Sequence

import argparse
import io
import json
import math
import os
import re
import sys
from typing import Any

# tools/ on the path. A no-op as long as this module is only ever run as a
# script, which it is: Python puts a script's own directory first already. Kept
# because tools/render_report.py carries the same line and needs it, being
# imported from tests/, and a convention that holds in two files of three is
# worse than one that holds in three.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _provenance import short_commit as git_commit  # noqa: E402
from report_style import Z_95  # noqa: E402

# See tools/render_results.py for why a collected row is a mapping rather
# than a TypedDict.
Result = dict[str, Any]

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def path(*parts: str) -> str:
    return os.path.join(ROOT, *parts)


def read(relative: str) -> str:
    return io.open(path(*relative.split("/")), encoding="utf-8").read()


# --- table parsing --------------------------------------------------------

def table(text: str, header: str, source: str, stop: str | None = None,
          occurrence: int | None = None) -> list[list[str | None]]:
    """Rows under an exact header line, split on whitespace.

    `header` must appear verbatim. Rows end at the first blank line, or at
    `stop` if given. A cell of "-" becomes None.

    A header that appears more than once is ambiguous and must be disambiguated
    with `occurrence`. Silently taking the first match is how a damaged header
    on the first of two identically-headed tables made this read the second and
    label its rows with the first table's instance: every opponent value moved
    from the 4x4 figures to the 5x5 ones, under the 4x4 label, and the collector
    reported success.
    """
    lines = text.split("\n")
    hits = [i for i, line in enumerate(lines) if line.strip() == header.strip()]
    if not hits:
        raise KeyError("header not found in {}: {!r}".format(source, header))
    if occurrence is None:
        if len(hits) > 1:
            raise KeyError(
                "header appears {} times in {}, so it does not identify a table: "
                "{!r}".format(len(hits), source, header))
        i = hits[0]
    else:
        if occurrence >= len(hits):
            raise KeyError(
                "asked for occurrence {} of a header appearing {} times in {}: "
                "{!r}".format(occurrence, len(hits), source, header))
        i = hits[occurrence]

    rows = []
    for line in lines[i + 1:]:
        if not line.strip():
            break
        if stop and stop in line:
            break
        rows.append([None if c == "-" else c for c in line.split()])
    if not rows:
        raise ValueError("no rows under header in {}: {!r}".format(source, header))
    return rows


def num(cell: str | None) -> int | float | None:
    if cell is None:
        return None
    return float(cell) if ("." in cell or "e" in cell.lower()) else int(cell)


def required(cell: str | None, source: str, what: str) -> str:
    """A cell that must carry text, refusing the "-" that parses to None.

    table() turns a "-" into None because a dash means "not measured", and a
    column that is sometimes a dash is a real thing in these transcripts. The
    columns below are not those: an instance name or a row key that arrived as
    a dash means the table moved, and an id built from it is wrong.

    On the two sites that join with "+" this upgrades a TypeError to a message
    naming the file and the column, which is worth having and is all it does.
    The site it exists for is the "opp-worst-{}-{}".format below, where
    str.format renders None as the text "None": that one really did build
    opp-worst-4x4{3,2}-None and report success. An audit found the guard on
    the two that already refused and missing from the one that did not.
    """
    if cell is None:
        raise ValueError("{}: {} is '-' where a value is required".format(source, what))
    return cell


def instance_of(row: Sequence[str | None], width: int = 2) -> str:
    """Instance names contain a space: '4x4 {3,2}'. Rejoin the leading cells."""
    return " ".join(required(c, "a parsed row", "the instance name")
                    for c in row[:width])


# --- the families ---------------------------------------------------------

def core(results: list[Result]) -> None:
    """out/figures.json, which the engine writes directly."""
    d = json.loads(read("out/figures.json"))
    src = "out/figures.json"
    m, b, lat = d["meta"], d["bounds"], d["lattice"]

    # meta.fold arrived with the fold filter, and figure data generated before
    # that does not carry it. Reading it as m["fold"] raised a bare KeyError
    # naming nothing, which is a poor answer to "your out/ is older than your
    # build". The collector refuses, and says what to run.
    if "fold" not in m:
        raise KeyError(
            "out/figures.json carries no meta.fold, so it predates the fold "
            "filter and its policy rows are a fold mixture; regenerate it with "
            "build/report_data 20000 > out/figures.json")

    def add(**kw: Any) -> None:
        results.append(dict(source=src, **kw))

    add(family="counting", id="omega0", instance=m["instance"],
        metric="configurations", value=m["omega0"], exact=True)
    add(family="counting", id="entropy", instance=m["instance"],
        metric="entropy", value=m["entropyBits"], unit="bits", exact=True)
    add(family="lattice", id="edges", instance=m["instance"],
        metric="lattice edges", value=lat["edges"], exact=True)
    add(family="lattice", id="stateVisits", instance=m["instance"],
        metric="state visits", value=lat["stateVisits"], exact=True)
    add(family="lattice", id="peakStates", instance=m["instance"],
        metric="peak live states", value=lat["peakStates"], exact=True)

    for name, key, exact in (("coverage", "coverage", True),
                             ("entropy", "entropy", True),
                             ("waterfilling", "waterfilling", True)):
        add(family="bounds", id="bound-" + name, instance=m["instance"],
            metric="lower bound on E[T]", value=b[key], unit="shots", exact=exact,
            note=name)
    add(family="bounds", id="transcripts", instance=m["instance"],
        metric="feasible hit-transcripts", value=b["transcripts"], exact=True)
    for e in b["blocking"]:
        add(family="bounds", id="beta-{}".format(e["length"]), instance=m["instance"],
            metric="blocking number beta(L)", value=e["beta"], exact=True,
            note="L = {}".format(e["length"]))

    for e in d["scaling"]:
        add(family="counting", id="omega-{}x{}".format(e["n"], e["n"]),
            instance="{n}x{n} {{5,4,3,3,2}}".format(n=e["n"]),
            metric="configurations", value=e["omega"], exact=True)

    # The fold comes from the tool that drew the boards, not from a reader's
    # assumption about it. Stamping "train" here is how a 60/20/20 mixture got
    # published under a TRAIN label: report_data filtered nothing, and this line
    # asserted the fold it had not checked.
    for p in d["policies"]:
        add(family="policy", id="policy-" + p["name"].replace(" ", "-"),
            instance=m["instance"], metric="mean shots to clear", value=p["mean"],
            unit="shots", ci=p["ci"], sd=p["sd"], games=m["games"],
            fold=m["fold"], engine="cheap", exact=False, note=p["name"])

    for o in d["objectives"]:
        for key, label in (("optimal", "optimal"), ("maxProb", "max hit probability"),
                           ("maxInfo", "max information gain"), ("density", "density")):
            add(family="objective", id="obj-{}-{}".format(o["instance"], key),
                instance=o["instance"], metric="E[T] under " + label,
                value=o[key], unit="shots", exact=True, note=label,
                configurations=o["configurations"])
        # Where the loss goes. Exact, since every board is enumerated.
        for key, label in (("maxInfoWaste", "max information gain"),
                           ("maxProbWaste", "max hit probability"),
                           ("densityWaste", "density")):
            add(family="waste", id="waste-{}-{}".format(o["instance"], key),
                instance=o["instance"],
                metric="misses after the board is determined, " + label,
                value=o[key], unit="shots", exact=True, note=label,
                configurations=o["configurations"])


def m9(results: list[Result]) -> None:
    t = read("docs/M9_RESULTS.txt")
    src = "docs/M9_RESULTS.txt"

    for r in table(t, "instance       boards E[T] committed W* adaptive      gap  beta(L)", src):
        inst = instance_of(r)
        results.append(dict(source=src, family="adversary", id="adv-" + inst,
                            instance=inst, metric="worst case W*", value=num(r[4]),
                            unit="shots", exact=True, configurations=num(r[2]),
                            committed=num(r[3])))

    for r in table(t, "instance      boards    adaptive fixed order greedy order      gap   ratio", src):
        inst = instance_of(r)
        results.append(dict(source=src, family="adaptivity", id="adapt-" + inst,
                            instance=inst, metric="non-adaptive optimum",
                            value=num(r[4]), unit="shots", exact=True,
                            adaptive=num(r[3]), greedy=num(r[5]),
                            configurations=num(r[2])))

    # Two instances carry this table; the second is the larger one and the
    # header repeats, so the occurrence is selected by the line that precedes it.
    for inst, marker in (("4x4 {3,2}", "H0 = 8.0444"), ("5x5 {4,3,2}", "H0 = 13.1396")):
        block = t[t.index(marker):]
        # The block already starts at this instance, so the first header in it is
        # the right one. Said explicitly, because the slice runs to the end of
        # the file and therefore still contains the other instance's table.
        for r in table(block, "eps     beta   capacity  shots used      bound    ratio",
                       src + " (" + inst + ")", occurrence=0):
            results.append(dict(source=src, family="noisy", id="noisy-" + inst + "-" + required(r[0], src, "the eps column"),
                                instance=inst, metric="shots to identify the board",
                                value=num(r[3]), unit="shots", exact=False,
                                eps=num(r[0]), capacity=num(r[2]), bound=num(r[4]),
                                ratio=num(r[5])))


def maxcover(results: list[Result]) -> None:
    t = read("docs/MAXCOVER.txt")
    src = "docs/MAXCOVER.txt"
    header = "instance      boards     K  E4 water  adaptive  non-adapt     maxcov   K*maxcov"
    for r in table(t, header, src):
        inst = instance_of(r)
        results.append(dict(source=src, family="maxcover", id="mc-" + inst,
                            instance=inst, metric="E4 water filling", value=num(r[4]),
                            unit="shots", exact=True, configurations=num(r[2]),
                            transcripts=num(r[3]), adaptive=num(r[5]),
                            nonAdaptive=num(r[6]), maxcov=num(r[7]),
                            kMaxcov=num(r[8])))


def opponent(results: list[Result]) -> None:
    t = read("docs/OPPONENT.txt")
    src = "docs/OPPONENT.txt"
    header = "believes     worst case regret vs flat"
    # The tool prints this table once per instance, in this order. Reading only
    # the first meant the 5x5 figures were computed, printed, and dropped.
    for occurrence, instance in enumerate(("4x4 {3,2}", "5x5 {4,3,2}")):
        for r in table(t, header, src, occurrence=occurrence):
            results.append(dict(source=src, family="opponent",
                                id="opp-worst-{}-{}".format(
                                    instance.replace(" ", ""),
                                    required(r[0], src, "the believed theta")),
                                instance=instance, metric="worst case over opponents",
                                value=num(r[1]), unit="shots", exact=True,
                                believesTheta=num(r[0]), regret=num(r[2])))


def headline(results: list[Result]) -> None:
    """The pre-registered run, on whichever folds have been played.

    Only games and the per-policy rows are read. The commit field is not, and
    on headline_test.json it is the empty string: run_headline used to build it
    from a stats.COMMIT that never existed, and the file predates the fix.

    It stays that way on purpose. Rewriting it means running the TEST fold,
    which needs a recorded unseal, and section 21 declined to spend one to
    tidy a field with no reader. The two files also differ in shape for the
    same reason, since headline_train.json was regenerated after the paired
    block gained `identical` and headline_test.json was not. Both facts are
    invisible from here unless this says so, which is why it does.
    """
    for fold in ("train", "test"):
        target = path("experiments", "headline_{}.json".format(fold))
        if not os.path.exists(target):
            continue
        d = json.loads(io.open(target, encoding="utf-8").read())
        src = "experiments/headline_{}.json".format(fold)
        for p_ in d["results"]["policies"]:
            results.append(dict(source=src, family="headline",
                                id="{}-{}".format(fold, p_["name"]),
                                instance="10x10 {5,4,3,3,2}",
                                metric="mean shots to clear, {} fold".format(fold.upper()),
                                value=p_["mean"], unit="shots", exact=False,
                                fold=fold, games=d["games"], sd=p_["sd"],
                                ciLow=p_["ci"][0], ciHigh=p_["ci"][1],
                                p95=p_["p95"], note=p_["name"]))


def constants(results: list[Result]) -> None:
    t = read("include/mayflower/constants.hpp")
    src = "include/mayflower/constants.hpp"
    m = re.search(r"kOmegaNoTouch = ([0-9']+)ull", t)
    if not m:
        raise KeyError("kOmegaNoTouch not found")
    results.append(dict(source=src, family="counting", id="omega-notouch",
                        instance="10x10 {5,4,3,3,2}",
                        metric="configurations, ships may not touch",
                        value=int(m.group(1).replace("'", "")), exact=True))


# --- consistency ----------------------------------------------------------


def _train_against_test(results: Sequence[Result]) -> list[dict[str, Any]]:
    """TRAIN against TEST on the same policy, measured on disjoint boards."""
    out = []
    # TRAIN against TEST, measured by the same tool at the same size on disjoint
    # boards. These are independent samples, so the sound question is whether the
    # difference is distinguishable from zero, not whether one mean happens to
    # land inside the other's interval. A TRAIN figure that had been overfitted
    # would show a difference the interval excludes.
    train = {r["note"]: r for r in results
             if r["family"] == "headline" and r["fold"] == "train"}
    test = {r["note"]: r for r in results
            if r["family"] == "headline" and r["fold"] == "test"}
    shared = sorted(set(train) & set(test))
    apart = []
    for n in shared:
        a, b = train[n], test[n]
        se = math.sqrt(a["sd"] ** 2 / a["games"] + b["sd"] ** 2 / b["games"])
        diff = a["value"] - b["value"]
        half = Z_95 * se
        if abs(diff) > half:
            apart.append({"instance": n, "difference": round(diff, 4),
                          "interval": [round(diff - half, 4), round(diff + half, 4)]})
    if shared:
        out.append({"quantity": "TRAIN and TEST agree on the same policy",
                       "sources": ["selfplay TRAIN", "selfplay TEST"],
                       "instances": len(shared), "agree": not apart,
                       "disagreements": apart})
    return out


def _pairs_agree(results: Sequence[Result]) -> list[dict[str, Any]]:
    """Quantities two different tools compute on the same instances."""
    out = []
    def pick(family: str, key: str) -> dict[str, Any]:
        return {r["instance"]: r[key] for r in results
                if r["family"] == family and key in r and r[key] is not None}

    # What each pair catches, since none of them is a second implementation:
    #
    #   adaptive optimum    one solveOptimal against itself at node caps
    #                       kAdaptiveLimit and 600. Catches an answer that
    #                       depends on the cap, which a completed search must
    #                       not, and catches either tool building the wrong
    #                       instance.
    #   committed E[T]      the same, across Adversary::Committed at caps
    #                       60000 and kAdaptiveLimit.
    #   non-adaptive opt.   two transcriptions of one subset-lattice DP.
    #                       Catches a transcription or build error, not an
    #                       error in the reasoning both carry.
    #   configurations      two transcriptions of one enumerator, likewise.
    pairs = [
        ("adaptive optimum", pick("adaptivity", "adaptive"), pick("maxcover", "adaptive"),
         "m9 adaptivity", "maxcover"),
        ("non-adaptive optimum", pick("adaptivity", "value"), pick("maxcover", "nonAdaptive"),
         "m9 adaptivity", "maxcover"),
        ("committed E[T]", pick("adversary", "committed"), pick("adaptivity", "adaptive"),
         "m9 adversary", "m9 adaptivity"),
        ("configurations", pick("adaptivity", "configurations"),
         pick("maxcover", "configurations"), "m9 adaptivity", "maxcover"),
    ]

    for label, a, b, sa, sb in pairs:
        shared = sorted(set(a) & set(b))
        bad = [i for i in shared if abs(a[i] - b[i]) > 1e-9]
        out.append({"quantity": label, "sources": [sa, sb],
                       "instances": len(shared), "agree": not bad,
                       "disagreements": [{"instance": i, sa: a[i], sb: b[i]} for i in bad]})
    return out


def _policy_against_headline(results: Sequence[Result]) -> list[dict[str, Any]]:
    """The page's headline measurement against the pre-registered one.

    report_data and selfplay measure the same three policies, over the same
    board pool key, at the same game count, in the same fold. Nothing compared
    them, and the comparison costs nothing: both numbers are already collected.

    It is the check that would have caught the fold leak on the day it landed.
    report_data drew board ids 0..n-1 with no fold filter where selfplay filters,
    so the page's rows were a 60/20/20 mixture published as TRAIN. The means
    differed in the third decimal, which is small enough that nobody reading
    either page would have seen it, and exactly what this comparison is for.

    The tolerance is the headline record's own printed precision. It stores
    three decimals, so an exact comparison would fail on rounding.
    """
    # report_data names the density policy by its class and selfplay by its
    # bucket count. DensityPolicy's default bonus is 50, so density(b=50) is the
    # same policy under the other name.
    headline_names = {"random": "random",
                      "parity hunt/target": "parity-hunt-target",
                      "density": "density(b=50)"}
    page = {r["note"]: r for r in results if r["family"] == "policy"}
    # Keyed by fold as well as name: the headline family holds a row per policy
    # per fold, and TRAIN and TEST share the policy name.
    head = {(r["fold"], r["note"]): r for r in results if r["family"] == "headline"}
    apart, checked = [], 0
    for page_name, head_name in sorted(headline_names.items()):
        a = page.get(page_name)
        if a is None:
            continue
        b = head.get((a.get("fold"), head_name))
        if b is None:
            continue
        checked += 1
        if a.get("games") != b.get("games"):
            apart.append({"policy": page_name, "reason": "different sample size",
                          "out/figures.json": a.get("games"), "headline": b.get("games")})
        elif abs(a["value"] - b["value"]) > 1e-3:
            apart.append({"policy": page_name, "reason": "means differ",
                          "fold": a.get("fold"),
                          "out/figures.json": a["value"], "headline": b["value"]})
    out = []
    if checked:
        out.append({"quantity": "the page's policy means match the headline record",
                    "sources": ["out/figures.json", "experiments/headline_*.json"],
                    "instances": checked, "agree": not apart, "disagreements": apart})
    return out


def _transcripts_against_sweep(results: Sequence[Result]) -> list[dict[str, Any]]:
    """The captured transcripts against the sweep that is regenerated each build."""
    out = []
    # The captured transcripts against the live sweep. Every check above
    # compares one doc to another doc: docs/M9_RESULTS.txt and docs/MAXCOVER.txt
    # were captured together, so a pair that went stale together agrees with
    # itself and nothing notices. out/figures.json is regenerated by report_data
    # from the current build, and it carries the same optima, so it is the one
    # side of this that cannot be stale.
    #
    # The tolerance is the transcripts' own printed precision. They give four
    # decimals, so 5.117647 is captured as 5.1176 and an exact comparison would
    # fail on the rounding rather than on anything being wrong.
    live = {r["instance"]: r["value"] for r in results
            if r["metric"] == "E[T] under optimal"}
    stale = []
    checked = 0
    for r in results:
        if r.get("family") not in ("maxcover", "adaptivity"):
            continue
        got = r.get("adaptive")
        if got is None or r["instance"] not in live:
            continue
        checked += 1
        if abs(got - live[r["instance"]]) > 1e-4:
            stale.append({"instance": r["instance"], "family": r["family"],
                          "transcript": got, "out/figures.json": live[r["instance"]]})
    if checked:
        out.append({"quantity": "the transcripts match the live sweep",
                       "sources": ["docs", "out/figures.json"], "instances": checked,
                       "agree": not stale, "disagreements": stale})
    return out


def _by_instance(results: Sequence[Result]) -> tuple[dict[str, dict[str, Any]],
                                                     dict[str, set[str]]]:
    """Every metric that names one value on its instance, plus the ones that do not.

    Built once and passed to the two checks that read it. It used to be a
    local shared by the tail of one long function, which is the kind of
    coupling a split has to make explicit rather than inherit.

    A metric name is not unique within an instance. Ten pairs carry between two
    and six rows: beta(L) is four lengths, "shots to identify the board" is six
    noise levels, "mean shots to clear" is one row per policy. Written as a
    plain last-write-wins dict this discarded 32 of the 118 rows, and a rule
    naming one of those metrics would have compared whichever row happened to
    be parsed last and reported an instance count that looked like coverage.

    No rule names one today, so nothing was ever wrong. That is the whole
    reason to separate them now rather than after one does: an ambiguous
    metric is returned in the second map, and the two checks below raise on it
    instead of quietly picking a row.
    """
    seen: dict[tuple[str, str], int] = {}
    for r in results:
        key = (r["instance"], r["metric"])
        seen[key] = seen.get(key, 0) + 1

    per_instance: dict[str, dict[str, Any]] = {}
    ambiguous: dict[str, set[str]] = {}
    for r in results:
        if seen[(r["instance"], r["metric"])] > 1:
            ambiguous.setdefault(r["instance"], set()).add(r["metric"])
            continue
        per_instance.setdefault(r["instance"], {})[r["metric"]] = r["value"]

    return per_instance, ambiguous


def _one_value(m: dict[str, Any], ambiguous: set[str], metric: str, rule: str) -> bool:
    """Whether this instance names exactly one value for this metric.

    Raises when the metric is present several times, because a rule that reads
    one of several rows is not the check it prints itself as.
    """
    if metric in ambiguous:
        raise ValueError(
            "the rule {!r} reads {!r}, which carries more than one row on this "
            "instance; name the rows apart before comparing them".format(rule, metric))
    return metric in m


def _orderings_by_definition(results: Sequence[Result],
                             per_instance: dict[str, dict[str, Any]],
                             ambiguous: dict[str, set[str]]) -> list[dict[str, Any]]:
    """Orderings that hold by definition, between families rather than within one."""
    out = []
    compared, off = 0, []
    # Orderings that hold by definition, between families rather than within
    # one. The optimal policy is optimal, a lower bound bounds, a worst case is
    # no better than an average, and feedback cannot hurt. Each side is produced
    # by a different tool on a different run, and nothing had ever put them in
    # the same place, so a result contradicting another family's would have sat
    # in the table with nothing to notice it.
    OPT = "E[T] under optimal"
    rules = [
        ("E[T] under density", "ge", OPT),
        ("E[T] under max hit probability", "ge", OPT),
        ("E[T] under max information gain", "ge", OPT),
        ("E4 water filling", "le", OPT),
        ("worst case W*", "ge", OPT),
        ("non-adaptive optimum", "ge", OPT),
    ]
    for inst in sorted(per_instance):
        m = per_instance[inst]
        amb = ambiguous.get(inst, set())
        for left, rel, right in rules:
            claim = "{} {} {}".format(left, rel, right)
            if not _one_value(m, amb, left, claim) or not _one_value(m, amb, right, claim):
                continue
            compared += 1
            ok = (m[left] >= m[right] - 1e-9 if rel == "ge"
                  else m[left] <= m[right] + 1e-9)
            if not ok:
                off.append({"instance": inst,
                            "claim": "{} {} {}".format(
                                left, ">=" if rel == "ge" else "<=", right),
                            left: m[left], right: m[right]})
    if compared:
        out.append({"quantity": "orderings that hold by definition",
                       "sources": ["every family"], "instances": compared,
                       "agree": not off, "disagreements": off})
    return out


def _waste_within_misses(results: Sequence[Result],
                         per_instance: dict[str, dict[str, Any]],
                         ambiguous: dict[str, set[str]]) -> list[dict[str, Any]]:
    """Waste counts misses, so it cannot exceed the misses a game has."""
    out = []
    # Waste is a subset of the misses, not a separate quantity: it counts the
    # misses taken after the board was already determined, and a game takes
    # E[T] - shipCells misses in total. The two come from different sweeps, so
    # a waste figure larger than the misses available to it would mean one of
    # them is measuring something else.
    def ship_cells(instance: str) -> int | None:
        m = re.search(r"\{([^}]*)\}", instance)
        if not m:
            return None
        try:
            return sum(int(x) for x in m.group(1).split(","))
        except ValueError:
            return None

    waste_pairs = [
        ("misses after the board is determined, density", "E[T] under density"),
        ("misses after the board is determined, max hit probability",
         "E[T] under max hit probability"),
        ("misses after the board is determined, max information gain",
         "E[T] under max information gain"),
    ]
    counted, over = 0, []
    for inst in sorted(per_instance):
        cells = ship_cells(inst)
        if cells is None:
            continue
        m = per_instance[inst]
        amb = ambiguous.get(inst, set())
        for waste, shots in waste_pairs:
            claim = "{} <= {} - shipCells".format(waste, shots)
            if not _one_value(m, amb, waste, claim) or not _one_value(m, amb, shots, claim):
                continue
            counted += 1
            misses = m[shots] - cells
            if m[waste] > misses + 1e-9:
                over.append({"instance": inst, "waste": m[waste],
                             "misses available": round(misses, 4),
                             "claim": "waste <= E[T] - shipCells"})
    if counted:
        out.append({"quantity": "waste is a subset of the misses",
                       "sources": ["objective", "waste"], "instances": counted,
                       "agree": not over, "disagreements": over})
    return out


def _regret_is_the_difference(results: Sequence[Result]) -> list[dict[str, Any]]:
    """Regret against the flat prior is the difference it says it is.

    believesTheta and regret were collected onto all eight opponent rows and
    read by nothing, because render_results.py renders no opponent section.
    They are not unrelated numbers: the tool prints the worst case and the
    regret side by side, and regret is the worst case minus the theta = 0 row
    on the same instance.

    Two columns of one printed table, related by arithmetic, neither ever
    compared. The tolerance is the transcript's own four decimal places, twice
    over, since both sides are rounded before this ever sees them.
    """
    out, off, counted = [], [], 0
    rows = [r for r in results if r["family"] == "opponent"]
    for inst in sorted({r["instance"] for r in rows}):
        group = [r for r in rows if r["instance"] == inst]
        flat = [r for r in group if r.get("believesTheta") == 0]
        if len(flat) != 1:
            continue
        base = flat[0]["value"]
        for r in group:
            if r.get("regret") is None:
                continue
            counted += 1
            if abs(r["value"] - base - r["regret"]) > 2e-4:
                off.append({"instance": inst, "believes": r.get("believesTheta"),
                            "claim": "regret == worst case - worst case at theta 0",
                            "worst case": r["value"], "flat": base,
                            "regret": r["regret"]})
    if counted:
        out.append({"quantity": "regret is the difference from the flat prior",
                    "sources": ["docs/OPPONENT.txt"], "instances": counted,
                    "agree": not off, "disagreements": off})
    return out


def _maxcover_rung_stays_withdrawn(results: Sequence[Result]) -> list[dict[str, Any]]:
    """The withdrawn rung is still withdrawn, and the one that bounds still bounds.

    maxcov and kMaxcov were collected onto every maxcover row and read by
    nothing. kMaxcov is worse than unread: docs/MAXCOVER.txt says it "exceeds
    it on 6 of 6 instances, so it is not a bound on anything", so the record
    published a withdrawn quantity with no statement that it is withdrawn.

    Both halves are checkable and both are asserted here. K*maxcov must stay
    strictly above the adaptive optimum, which is the negative regression that
    stops the rung being quoted again; maxcov must stay at or below the
    non-adaptive optimum, which is what it does bound. tools/maxcover selftest
    already pins this, and nothing in the collected record did.
    """
    off = []
    rows = [r for r in results if r["family"] == "maxcover"]
    for r in rows:
        k, adaptive = r.get("kMaxcov"), r.get("adaptive")
        cov, non = r.get("maxcov"), r.get("nonAdaptive")
        if k is not None and adaptive is not None and k <= adaptive:
            off.append({"instance": r["instance"],
                        "claim": "K*maxcov is not a lower bound on the adaptive optimum",
                        "K*maxcov": k, "adaptive": adaptive})
        if cov is not None and non is not None and cov > non + 1e-9:
            off.append({"instance": r["instance"],
                        "claim": "maxcov <= the non-adaptive optimum",
                        "maxcov": cov, "non-adaptive": non})
    if not rows:
        return []
    return [{"quantity": "the max-coverage rung stays withdrawn",
             "sources": ["docs/MAXCOVER.txt"], "instances": len(rows),
             "agree": not off, "disagreements": off}]


def _omega_paths_agree(results: Sequence[Result]) -> list[dict[str, Any]]:
    """The hypothesis space, from the two places report_data derives it.

    meta.omega0 comes from the standard instance's own sweep; scaling[n=10].omega
    comes from the board-size ladder's tenth rung. Same quantity, same instance,
    two code paths through the same tool, and until this check nothing compared
    them: they collided on (instance, metric) and _by_instance kept one.

    This is the shape section 21f describes. The comparison costs nothing, both
    numbers are already collected, and the one it resembles most is the policy
    check that caught the fold leak on the day it landed. 15,046,987,768 is the
    number the whole report is built on, so two routes to it is worth pinning.
    """
    by_id = {r["id"]: r for r in results}
    a, b = by_id.get("omega0"), by_id.get("omega-10x10")
    if a is None or b is None:
        return []
    off = []
    if a["value"] != b["value"]:
        off.append({"quantity": "configurations on 10x10 {5,4,3,3,2}",
                    "meta.omega0": a["value"], "scaling[n=10].omega": b["value"]})
    return [{"quantity": "both routes to the hypothesis space",
             "sources": ["out/figures.json meta", "out/figures.json scaling"],
             "instances": 1, "agree": not off, "disagreements": off}]


def _differences(committed: dict[str, Any], fresh: dict[str, Any]) -> list[str]:
    """Where the committed record and a fresh collection disagree.

    Everything but `commit`, which names the tree that wrote the file and is
    therefore expected to be older than the tree checking it.
    """
    out: list[str] = []

    def walk(a: Any, b: Any, where: str) -> None:
        if isinstance(a, dict) and isinstance(b, dict):
            for k in sorted(set(a) | set(b)):
                if where == "" and k == "commit":
                    continue
                at = "{}.{}".format(where, k) if where else k
                if k not in a:
                    out.append("{} is new".format(at))
                elif k not in b:
                    out.append("{} has gone".format(at))
                else:
                    walk(a[k], b[k], at)
        elif isinstance(a, list) and isinstance(b, list):
            if len(a) != len(b):
                out.append("{}: {} entries against {}".format(where, len(a), len(b)))
                return
            for i, (x, y) in enumerate(zip(a, b)):
                walk(x, y, "{}[{}]".format(where, i))
        elif a != b:
            out.append("{}: committed {!r}, sources say {!r}".format(where, a, b))

    walk(committed, fresh, "")
    return out


def cross_checks(results: Sequence[Result]) -> list[dict[str, Any]]:
    """Quantities two tools compute independently. Disagreement means a bug.

    One function per check. The call order is load bearing: render_results.py
    renders this list in order and experiments/results.json is written with
    sort_keys=False, so reordering here moves the page and the file.
    """
    checks = []
    checks += _train_against_test(results)
    checks += _pairs_agree(results)
    checks += _policy_against_headline(results)
    checks += _transcripts_against_sweep(results)
    checks += _omega_paths_agree(results)
    checks += _maxcover_rung_stays_withdrawn(results)
    checks += _regret_is_the_difference(results)
    per_instance, ambiguous = _by_instance(results)
    checks += _orderings_by_definition(results, per_instance, ambiguous)
    checks += _waste_within_misses(results, per_instance, ambiguous)
    return checks



# ctest reads this as "Skipped" via SKIP_RETURN_CODE. out/ is generated and
# gitignored, so a clean clone has nothing to collect and should say so rather
# than fail or quietly pass.
#
# The twelfth copy of a number tests/_harness.py holds once for the other
# eleven, and it cannot import that: tests/ sits above tools/ in the layering
# and reaching up would invert it. Worth stating plainly, because it means this
# module is registered as a ctest test and speaks the suite's exit-code
# protocol while living in the report layer, which is the only place in the
# tree where that is true.
SKIP = 77


def main() -> int:
    if not os.path.exists(os.path.join(ROOT, "out", "figures.json")):
        print("out/figures.json is missing; run tools/report_data first")
        return SKIP

    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="verify without writing")
    args = ap.parse_args()

    results: list[Result] = []
    for fn in (core, m9, maxcover, opponent, headline, constants):
        fn(results)

    checks = cross_checks(results)
    failed = [c for c in checks if not c["agree"]]

    out: dict[str, Any] = {
        "schema": 1,
        "commit": git_commit(),
        "note": ("Every value is read from a tool's own output, never transcribed. "
                 "Text tables are parsed against their exact headers, so a tool "
                 "changing its output breaks this rather than corrupting it."),
        "counts": {
            "results": len(results),
            "families": len(sorted({r["family"] for r in results})),
            "exact": sum(1 for r in results if r.get("exact")),
            "measured": sum(1 for r in results if not r.get("exact")),
        },
        "crossChecks": checks,
        "results": results,
    }

    print("collected {} results across {} families".format(
        out["counts"]["results"], out["counts"]["families"]))
    print("  exact {}, measured {}".format(out["counts"]["exact"],
                                           out["counts"]["measured"]))
    for c in checks:
        print("  {:<24} {:>2} instances  {}".format(
            c["quantity"], c["instances"], "agree" if c["agree"] else "*** DISAGREE ***"))
        for d in c["disagreements"]:
            print("      ", d)

    if failed:
        print("\nFAILED: two tools disagree about the same quantity")
        return 1

    # Under --check, against the committed record as well as against itself.
    #
    # Everything above compares freshly parsed sources to each other. None of
    # it opened experiments/results.json, so the file the dossier renders from
    # was checked by nothing: --check verified that the sources agree among
    # themselves and never that the committed record still matches them. A
    # transcript that moved, or a figure sweep that moved, left the record
    # stale and CI said nothing.
    #
    # The commit stamp is excluded because it names the tree that wrote the
    # file, which is by construction older than the tree checking it. That is
    # the same "identical except the commit stamp" rule the figure data is
    # already held to.
    if args.check:
        target = path("experiments", "results.json")
        if not os.path.exists(target):
            print("\nFAILED: experiments/results.json is missing")
            return 1
        committed = json.loads(io.open(target, encoding="utf-8").read())
        drift = _differences(committed, out)
        if drift:
            print("\nFAILED: experiments/results.json is not what the sources now say")
            for d in drift[:20]:
                print("   " + d)
            if len(drift) > 20:
                print("   ... and {} more".format(len(drift) - 20))
            print("  regenerate with python tools/collect_results.py")
            return 1
        print("\nand experiments/results.json matches, apart from the commit stamp")

    if not args.check:
        target = path("experiments", "results.json")
        with io.open(target, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(out, fh, indent=1, sort_keys=False)
            fh.write("\n")
        print("\nwrote experiments/results.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
