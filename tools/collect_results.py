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
    a dash means the table moved, and silently building "noisy--None" out of it
    is how a wrong id reaches experiments/results.json.
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
                                id="opp-worst-{}-{}".format(instance.replace(" ", ""), r[0]),
                                instance=instance, metric="worst case over opponents",
                                value=num(r[1]), unit="shots", exact=True,
                                believesTheta=num(r[0]), regret=num(r[2])))


def headline(results: list[Result]) -> None:
    """The pre-registered run, on whichever folds have been played."""
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


def _by_instance(results: Sequence[Result]) -> dict[str, dict[str, Any]]:
    """Every metric, keyed by instance then metric name.

    Built once and passed to the two checks that read it. It used to be a
    local shared by the tail of one long function, which is the kind of
    coupling a split has to make explicit rather than inherit."""
    per_instance: dict[str, dict[str, Any]] = {}
    for r in results:
        per_instance.setdefault(r["instance"], {})[r["metric"]] = r["value"]

    return per_instance


def _orderings_by_definition(results: Sequence[Result],
                             per_instance: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
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
        for left, rel, right in rules:
            if left not in m or right not in m:
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
                         per_instance: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
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
        for waste, shots in waste_pairs:
            if waste not in m or shots not in m:
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
    per_instance = _by_instance(results)
    checks += _orderings_by_definition(results, per_instance)
    checks += _waste_within_misses(results, per_instance)
    return checks



# ctest reads this as "Skipped" via SKIP_RETURN_CODE. out/ is generated and
# gitignored, so a clean clone has nothing to collect and should say so rather
# than fail or quietly pass.
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

    if not args.check:
        target = path("experiments", "results.json")
        with io.open(target, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(out, fh, indent=1, sort_keys=False)
            fh.write("\n")
        print("\nwrote experiments/results.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
