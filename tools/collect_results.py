"""Gather every measured result into one machine-readable file.

The project's rule is that no number is transcribed by hand, so nothing here is
typed in. Everything is read from what a tool actually emitted: `out/figures.json`
for the core contract, and the captured runs in `docs/` for the extensions.

The text tables are parsed against their headers. A parser that cannot find its
exact header raises rather than guessing, so a tool changing its output breaks
this loudly instead of quietly producing a wrong dataset.

The point of pulling them together is not tidiness. Several quantities are
produced by more than one tool, and once they sit in one place they can be
checked against each other. The adaptive optimum of 4x4 {3,2}, for instance, is
computed independently by the belief MDP in `m9` and again in `maxcover`; if
those ever disagree, one of them is wrong and this is where it shows.

    python tools/collect_results.py            # writes experiments/results.json
    python tools/collect_results.py --check    # verify only, no write
"""

from __future__ import annotations

import argparse
import io
import json
import math
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def path(*parts):
    return os.path.join(ROOT, *parts)


def read(relative):
    return io.open(path(*relative.split("/")), encoding="utf-8").read()


# --- table parsing --------------------------------------------------------

def table(text, header, source, stop=None):
    """Rows under an exact header line, split on whitespace.

    `header` must appear verbatim. Rows end at the first blank line, or at
    `stop` if given. A cell of "-" becomes None.
    """
    lines = text.split("\n")
    for i, line in enumerate(lines):
        if line.strip() == header.strip():
            break
    else:
        raise KeyError("header not found in {}: {!r}".format(source, header))

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


def num(cell):
    if cell is None:
        return None
    return float(cell) if ("." in cell or "e" in cell.lower()) else int(cell)


def instance_of(row, width=2):
    """Instance names contain a space: '4x4 {3,2}'. Rejoin the leading cells."""
    return " ".join(row[:width])


# --- the families ---------------------------------------------------------

def core(results):
    """out/figures.json, which the engine writes directly."""
    d = json.loads(read("out/figures.json"))
    src = "out/figures.json"
    m, b, lat = d["meta"], d["bounds"], d["lattice"]

    add = lambda **kw: results.append(dict(source=src, **kw))
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

    for p in d["policies"]:
        add(family="policy", id="policy-" + p["name"].replace(" ", "-"),
            instance=m["instance"], metric="mean shots to clear", value=p["mean"],
            unit="shots", ci=p["ci"], sd=p["sd"], games=m["games"], fold="train",
            engine="cheap", exact=False, note=p["name"])

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


def m9(results):
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
        for r in table(block, "eps     beta   capacity  shots used      bound    ratio",
                       src + " (" + inst + ")"):
            results.append(dict(source=src, family="noisy", id="noisy-" + inst + "-" + r[0],
                                instance=inst, metric="shots to identify the board",
                                value=num(r[3]), unit="shots", exact=False,
                                eps=num(r[0]), capacity=num(r[2]), bound=num(r[4]),
                                ratio=num(r[5])))


def maxcover(results):
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


def opponent(results):
    t = read("docs/OPPONENT.txt")
    src = "docs/OPPONENT.txt"
    for r in table(t, "believes     worst case regret vs flat", src):
        results.append(dict(source=src, family="opponent", id="opp-worst-" + r[0],
                            instance="4x4 {3,2}", metric="worst case over opponents",
                            value=num(r[1]), unit="shots", exact=True,
                            believesTheta=num(r[0]), regret=num(r[2])))


def headline(results):
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


def constants(results):
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

def cross_checks(results):
    """Quantities two tools compute independently. Disagreement means a bug."""
    checks = []

    def pick(family, key):
        return {r["instance"]: r[key] for r in results
                if r["family"] == family and key in r and r[key] is not None}

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
        half = 1.959963985 * se
        if abs(diff) > half:
            apart.append({"instance": n, "difference": round(diff, 4),
                          "interval": [round(diff - half, 4), round(diff + half, 4)]})
    if shared:
        checks.append({"quantity": "TRAIN and TEST agree on the same policy",
                       "sources": ["selfplay TRAIN", "selfplay TEST"],
                       "instances": len(shared), "agree": not apart,
                       "disagreements": apart})
    for label, a, b, sa, sb in pairs:
        shared = sorted(set(a) & set(b))
        bad = [i for i in shared if abs(a[i] - b[i]) > 1e-9]
        checks.append({"quantity": label, "sources": [sa, sb],
                       "instances": len(shared), "agree": not bad,
                       "disagreements": [{"instance": i, sa: a[i], sb: b[i]} for i in bad]})
    return checks


def git_commit():
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"],
                                       cwd=ROOT, stderr=subprocess.DEVNULL
                                       ).decode().strip()
    except Exception:
        return "unknown"


# ctest reads this as "Skipped" via SKIP_RETURN_CODE. out/ is generated and
# gitignored, so a clean clone has nothing to collect and should say so rather
# than fail or quietly pass.
SKIP = 77


def main():
    if not os.path.exists(os.path.join(ROOT, "out", "figures.json")):
        print("out/figures.json is missing; run tools/report_data first")
        return SKIP

    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="verify without writing")
    args = ap.parse_args()

    results = []
    for fn in (core, m9, maxcover, opponent, headline, constants):
        fn(results)

    checks = cross_checks(results)
    failed = [c for c in checks if not c["agree"]]

    out = {
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
