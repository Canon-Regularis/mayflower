"""experiments/registry.yaml, against the artefacts it claims to describe.

The registry records one entry per experiment that produced a number quoted
anywhere, written before the run so the analysis is fixed while the answer is
unknown. Every figure in it was transcribed by hand, and until this file
existed nothing in the tree parsed YAML, so nothing could disagree with any of
them.

They had already drifted. The fold fix moved every measured quantity in the
project and this file kept the old TRAIN baselines, reading 95.354 and 51.535
where the record said 95.401 and 51.596, and it described a paired difference
in a form that had been replaced. Both survived a correctness pass that was
looking for exactly that, because the reader needed to find it did not exist.

So the numbers are held to the records rather than to each other:

  - the two structured `result` mappings against experiments/headline_train.json
    and experiments/headline_test.json, every key of them, with a key this
    does not recognise failing rather than being passed over;
  - the three prose `result` strings: the ladder's bounds against
    experiments/results.json and its measured policy against the TRAIN
    capture, the objective gaps against results.json, and the opponent
    figures against docs/OPPONENT.txt;
  - the numbers inside the `notes` prose, which is where the drift actually
    happened, against the same records;
  - and the one figure no artefact holds, sigma = 8.87, against the document
    that derives it, which is the honest thing to compare it to.

Two structural claims are checked as well, because the file states them about
itself. The `status` and `fold` domains are read out of the header comments
rather than retyped here, and an entry declaring `fold: test` must have a
matching unseal recorded in experiments/audit.log, which is what makes reading
sealed data an event rather than a decision someone remembers making.

Not checked, and deliberately: that every `id` names something real. Four of
the five appear nowhere else in the tree, so the assertion would be a claim
about naming rather than about data.

    python tests/test_registry.py
"""

from __future__ import annotations

import io
import json
import os
import re
import sys
from typing import Any

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _harness import ROOT, check, report  # noqa: E402
from _yaml import YamlError, load, loads  # noqa: E402

REGISTRY = os.path.join(ROOT, "experiments", "registry.yaml")
AUDIT = os.path.join(ROOT, "experiments", "audit.log")
PREREG = os.path.join(ROOT, "experiments", "preregistration.md")
OPPONENT = os.path.join(ROOT, "docs", "OPPONENT.txt")

# The registry names policies its own way. The record names them another way,
# and two of the three tokens in the p95 line are abbreviations rather than
# names, so every token that means a policy goes through here.
POLICY = {
    "random": "random",
    "parity": "parity-hunt-target",
    "parity-hunt-target": "parity-hunt-target",
    "density": "density(b=10)",
    "density-b10": "density(b=10)",
    "density-b50": "density(b=50)",
    "density-b200": "density(b=200)",
}

# Keys in a result mapping that are not a policy and are checked on their own
# below. Naming them is what lets check_policies refuse anything else instead
# of skipping it: a fourth beam width added beside b10, b50 and b200 would
# otherwise be transcribed, never compared, and never noticed.
NON_POLICY = {"games", "p95", "paired-parity-minus-density", "correlation"}

INTERVAL = re.compile(r"^([+-]?[0-9.]+) \[([+-]?[0-9.]+), ([+-]?[0-9.]+)\]$")


def read_json(rel: str) -> Any:
    return json.loads(io.open(os.path.join(ROOT, rel), encoding="utf-8").read())


def rounds_to(actual: float, stated: str) -> bool:
    """Whether the record rounds to what the registry printed.

    The registry quotes two or three decimals. Some records carry more and
    some carry exactly as many, so a fixed tolerance would be a guess in one
    direction and float equality a trap in the other. The precision the
    registry chose is the precision it is held to, which is strict where the
    two agree on digits and forgiving only about the ones not printed.
    """
    text = stated.lstrip("+")
    places = len(text.split(".")[1]) if "." in text else 0
    return round(actual, places) == float(text)


def interval(stated: str) -> tuple[float, float, float] | None:
    """A "mean [low, high]" string as three floats.

    float() takes the leading + and the missing trailing zero, which is what
    makes this a numeric comparison rather than a string one. Four of the nine
    bracketed strings in the file need that: three write 44.320 and 44.530
    where the record carries 44.32 and 44.53, and one signs every figure.
    """
    m = INTERVAL.match(stated)
    if m is None:
        return None
    return float(m.group(1)), float(m.group(2)), float(m.group(3))


def domain(comments: list[str], field: str) -> set[str]:
    """The allowed values of a field, from the header comment that declares them.

    The file documents its own domains at the top, in comments shaped exactly
    like keys. Reading them here rather than retyping them means the comment
    and the entries cannot drift apart, which they had: two entries read
    `fold: none` against a comment that listed three values and not that one.
    """
    for c in comments:
        if c.startswith(field + ":"):
            return {p.strip() for p in c.split(":", 1)[1].split("|")}
    return set()


def audit_entries() -> list[list[str]]:
    """The audit log's data lines, six fields each.

    Comments are most of the file, so they come out before the field count is
    applied rather than after: the log's own format header is a comment and
    splits into six fields like a data line, and every other comment splits
    into one.
    """
    out = []
    for n, raw in enumerate(io.open(AUDIT, encoding="utf-8"), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        fields = [f.strip() for f in line.split("|")]
        if len(fields) != 6:
            raise ValueError("experiments/audit.log:{}: {} fields, not six"
                             .format(n, len(fields)))
        out.append(fields)
    return out


def opponent_matrix(instance: str) -> dict[float, dict[float, float]]:
    """The mismatch matrix for one instance, as actual -> believes -> shots.

    docs/OPPONENT.txt prints it as `rows are the opponent, columns what the
    engine believes`, with a trailing regret column this ignores: the two
    figures the registry quotes are differences along a row, so they are
    recomputed from the cells rather than read off a column.
    """
    text = io.open(OPPONENT, encoding="utf-8").read()
    at = text.find(instance + ": rows are the opponent")
    if at < 0:
        return {}
    out: dict[float, dict[float, float]] = {}
    believes: list[float] = []
    for line in text[at:].split("\n")[1:]:
        cells = line.split()
        if not cells:
            if out:
                break
            continue
        if cells[0] == "actual":
            believes = [float(c) for c in cells[1:] if c != "regret"]
            continue
        if not believes:
            break
        try:
            values = [float(c) for c in cells]
        except ValueError:
            break
        out[values[0]] = dict(zip(believes, values[1:1 + len(believes)]))
    return out


def check_policies(where: str, stated: dict[str, Any], record: Any) -> None:
    """Every policy interval in a result mapping, against the capture."""
    means = {p["name"]: p for p in record["results"]["policies"]}
    unknown = sorted(k for k in stated if k not in POLICY and k not in NON_POLICY)
    check(not unknown, "{}: every result key is one this compares".format(where),
          "nothing checks " + ", ".join(unknown))
    compared = 0
    for key, value in stated.items():
        if key not in POLICY:
            continue
        compared += 1
        got = interval(value) if isinstance(value, str) else None
        if got is None:
            check(False, "{}: {} is a mean and an interval".format(where, key),
                  "reads {!r}".format(value))
            continue
        name = POLICY[key]
        if name not in means:
            check(False, "{}: {} names a policy in the record".format(where, key),
                  "{} is not one of {}".format(name, sorted(means)))
            continue
        p = means[name]
        mean, low, high = got
        check(rounds_to(p["mean"], value.split(" ")[0]),
              "{}: {} mean matches the record".format(where, key),
              "registry {}, record {}".format(mean, p["mean"]))
        check(rounds_to(p["ci"][0], "{:.3f}".format(low))
              and rounds_to(p["ci"][1], "{:.3f}".format(high)),
              "{}: {} interval matches the record".format(where, key),
              "registry [{}, {}], record {}".format(low, high, p["ci"]))
    check(compared > 0, "{}: at least one policy was compared".format(where),
          "the result mapping named none")


def main() -> int:
    print("the experiment registry, against what it describes")
    print("==================================================")

    try:
        doc = load(REGISTRY)
    except YamlError as exc:
        check(False, "experiments/registry.yaml parses", str(exc))
        return report()
    check(True, "experiments/registry.yaml parses",
          "{} entries".format(len(doc.data.get("experiments", []))))

    # The reader is for this file and must refuse anything wider, or it would
    # be a general YAML parser nobody tested against general YAML.
    for rel in (".github/workflows/ci.yml", ".github/workflows/nightly.yml"):
        try:
            load(os.path.join(ROOT, rel))
            check(False, "the reader refuses " + rel, "it accepted it")
        except YamlError:
            check(True, "the reader refuses " + rel)
    try:
        loads("a: [1, 2]\n", "<flow>")
        check(False, "the reader refuses a flow collection")
    except YamlError:
        check(True, "the reader refuses a flow collection")

    data = doc.data
    check(data.get("version") == 1, "the registry declares version 1",
          "reads {!r}".format(data.get("version")))

    entries = data.get("experiments", [])
    by_id = {e["id"]: e for e in entries}
    check(len(by_id) == len(entries), "every entry id is distinct",
          "{} ids over {} entries".format(len(by_id), len(entries)))

    statuses = domain(doc.comments, "status")
    folds = domain(doc.comments, "fold")
    check(bool(statuses) and bool(folds),
          "the header declares the status and fold domains",
          "status {}, fold {}".format(sorted(statuses), sorted(folds)))
    off = [(e["id"], e["status"]) for e in entries if e["status"] not in statuses]
    check(not off, "every entry's status is one the header allows", str(off))
    off = [(e["id"], e["fold"]) for e in entries if e["fold"] not in folds]
    check(not off, "and every entry's fold is too", str(off))

    # Reading TEST is an event. An entry that says it read TEST has to point at
    # one, or the seal records less than the registry claims.
    unsealed = {row[3] for row in audit_entries() if len(row) > 3 and row[2] == "unseal"}
    want = {e["id"] for e in entries if e["fold"] == "test"}
    missing = sorted(want - unsealed)
    check(not missing,
          "every entry declaring fold: test has a recorded unseal",
          "no unseal for " + ", ".join(missing))

    # And the other direction, which is the half that can be made to fail by
    # editing the registry alone. Requiring only that a fold: test entry has an
    # unseal is satisfied by deleting the fold: test, so downgrading an entry
    # to val would drop the requirement and pass. An unseal is a record that
    # sealed data was read; the entry it names has to admit reading it.
    declared = {e["id"]: e["fold"] for e in entries}
    downgraded = sorted(i for i in unsealed
                        if i in declared and declared[i] != "test")
    check(not downgraded,
          "and every unsealed experiment the registry lists declares fold: test",
          ", ".join("{} says fold: {}".format(i, declared[i]) for i in downgraded))

    train = read_json("experiments/headline_train.json")
    test = read_json("experiments/headline_test.json")
    results = read_json("experiments/results.json")
    rows = results["results"]

    # 1. The two structured result mappings.
    check_policies("baseline", by_id["baseline-reconciliation"]["result"], train)

    head = by_id["headline-policy-comparison"]["result"]
    check_policies("headline", head, test)
    check(head["games"] == test["games"],
          "headline: the game count matches the capture",
          "registry {}, record {}".format(head["games"], test["games"]))

    # Coverage is decided by the record, not by this line. Counting the tokens
    # the line contains and comparing that to the tokens parsed out of it is
    # the same number twice: drop `density 61` and both fall to two.
    #
    # The line collapses the three density arms into one token, because all
    # three read the same p95, so a token covers every policy whose name it
    # prefixes rather than one exact name.
    p95 = dict(re.findall(r"([a-z][a-z-]*) (\d+)", head["p95"]))
    check(bool(p95), "headline: the p95 line parses", repr(head["p95"]))
    means = {p["name"]: p for p in test["results"]["policies"]}
    uncovered = []
    for name in sorted(means):
        tokens = [t for t in p95 if name.startswith(t)]
        if not tokens:
            uncovered.append(name)
            continue
        for t in tokens:
            check(means[name]["p95"] == int(p95[t]),
                  "headline: p95 for {} matches, under {!r}".format(name, t),
                  "registry {}, record {}".format(p95[t], means[name]["p95"]))
    check(not uncovered, "headline: the p95 line names every policy in the record",
          "nothing on it covers " + ", ".join(uncovered))
    unused = sorted(t for t in p95 if not any(n.startswith(t) for n in means))
    check(not unused, "and every token on it names a policy",
          "no policy matches " + ", ".join(unused))

    paired = test["results"]["paired"]
    got = interval(head["paired-parity-minus-density"])
    match = [q for q in paired
             if {q["a"], q["b"]} == {"parity-hunt-target", "density(b=10)"}]
    check(got is not None and len(match) == 1
          and rounds_to(match[0]["difference"], "{:.3f}".format(got[0]))
          and rounds_to(match[0]["ci"][0], "{:.3f}".format(got[1]))
          and rounds_to(match[0]["ci"][1], "{:.3f}".format(got[2])),
          "headline: the paired difference matches the capture",
          "registry {!r}, record {}".format(head["paired-parity-minus-density"],
                                            match[0] if match else "no such pair"))

    m = re.match(r"^(\d+\.\d+) inside the density family, (-?\d+\.\d+) across families$",
                 head["correlation"])
    check(m is not None, "headline: the correlation line has the shape it claims")
    if m:
        within = [q["rho"] for q in paired
                  if q["a"].startswith("density") and q["b"].startswith("density")]
        across = [q["rho"] for q in paired
                  if q["a"].startswith("density") != q["b"].startswith("density")]
        # The minimum, not any of them. Two of the three density arms pick the
        # same cell on every board, so their rho is exactly 1, and an any()
        # over this list accepts 1.000 as readily as the real figure.
        check(bool(within) and rounds_to(min(within), m.group(1)),
              "headline: the within-family correlation is the record's weakest",
              "registry {}, record {}".format(m.group(1), sorted(set(within))))
        check(bool(across) and rounds_to(min(across), m.group(2)),
              "headline: the across-family correlation is the record's extreme",
              "registry {}, record min {}".format(m.group(2),
                                                  min(across) if across else None))

    # 2. The three prose result strings.
    def row(rid: str) -> Any:
        hit = [r for r in rows if r["id"] == rid]
        return hit[0] if hit else None

    ladder = by_id["bound-ladder"]["result"]
    m = re.match(r"^coverage (\d+(?:\.\d+)?), water filling (\d+\.\d+), "
                 r"best measured policy (\d+\.\d+)$", ladder)
    check(m is not None, "the bound ladder result has the shape it claims", ladder)
    if m:
        for stated, rid, what in ((m.group(1), "bound-coverage", "coverage"),
                                  (m.group(2), "bound-waterfilling", "water filling")):
            r = row(rid)
            check(r is not None and rounds_to(r["value"], stated),
                  "the ladder's {} matches results.json".format(what),
                  "registry {}, record {}".format(stated, r["value"] if r else None))
        # The entry is fold: none because the bounds are exact combinatorics.
        # This third figure is not a bound, it is the measured policy the gap
        # is taken against, so it is held to the TRAIN capture regardless of
        # the entry's fold.
        best = min(p["mean"] for p in train["results"]["policies"])
        check(rounds_to(best, m.group(3)),
              "and its best measured policy matches the TRAIN capture",
              "registry {}, record {}".format(m.group(3), best))

    obj = by_id["objective-comparison"]["result"]
    m = re.match(r"^max-hit-probability within (\d+\.\d+) shots of optimal and exactly "
                 r"optimal on (\d+) of (\d+) instances; max-information-gain loses "
                 r"up to (\d+\.\d+) shots\.$", obj)
    check(m is not None, "the objective result has the shape it claims", obj)
    if m:
        opt = {r["instance"]: r["value"] for r in rows
               if r["family"] == "objective" and r["metric"] == "E[T] under optimal"}
        gaps = {}
        peaks = {}
        for r in rows:
            if r["family"] != "objective" or r["instance"] not in opt:
                continue
            if r["metric"] == "E[T] under max hit probability":
                gaps[r["instance"]] = r["value"] - opt[r["instance"]]
            elif r["metric"] == "E[T] under max information gain":
                peaks[r["instance"]] = r["value"] - opt[r["instance"]]
        check(bool(gaps) and rounds_to(max(gaps.values()), m.group(1)),
              "the objective gap matches results.json",
              "registry {}, record {}".format(m.group(1),
                                              max(gaps.values()) if gaps else None))
        exact = sum(1 for v in gaps.values() if abs(v) < 1e-9)
        check(exact == int(m.group(2)) and len(gaps) == int(m.group(3)),
              "and so does how many instances it is exact on",
              "registry {} of {}, record {} of {}".format(
                  m.group(2), m.group(3), exact, len(gaps)))
        check(bool(peaks) and rounds_to(max(peaks.values()), m.group(4)),
              "and the information-gain loss",
              "registry {}, record {}".format(m.group(4),
                                              max(peaks.values()) if peaks else None))

    opp = by_id["opponent-model-value"]["result"]
    # The instance comes out of the sentence that quotes the figures. Hard
    # coding 5x5 {4,3,2} here would let the registry name a different instance
    # beside numbers still being checked against this one.
    named = re.search(r"on (\d+x\d+ \{[^}]*\})", opp)
    check(named is not None, "the opponent result names the instance it measured",
          opp[:70])
    matrix = opponent_matrix(named.group(1)) if named else {}
    check(bool(matrix),
          "docs/OPPONENT.txt carries the mismatch matrix for that instance",
          "found {} rows for {}".format(
              len(matrix), named.group(1) if named else "no instance"))
    if matrix:
        # Both are differences across one row of the matrix, which is what
        # the matrix is for: the cost of believing the wrong thing is the
        # spread along a row rather than any cell in it. The oracle gain is
        # also printed, in the row's own regret column, so recomputing it from
        # the cells checks the transcript's internal arithmetic as well as the
        # registry's transcription of it. The mismatch cost is printed nowhere.
        #
        # Oracle gain at theta = 3: the opponent really is biased and the
        # engine either knows it or assumes flat.
        gain = matrix[3.0][0.0] - matrix[3.0][3.0]
        m = re.search(r"oracle gain (\d+\.\d+) shots at theta = 3", opp)
        check(m is not None, "the opponent result states the oracle gain", opp[:70])
        if m:
            check(rounds_to(gain, m.group(1)),
                  "and it is the spread along the theta = 3 row",
                  "registry {}, transcript {:.4f} - {:.4f} = {:.4f}".format(
                      m.group(1), matrix[3.0][0.0], matrix[3.0][3.0], gain))
        # The mismatch cost: the opponent has no bias and the engine believes
        # one anyway. This is the row the report calls the losing side of the
        # bet, so it is the one worth holding to the transcript.
        cost = matrix[0.0][3.0] - matrix[0.0][0.0]
        m = re.search(r"costs (\d+\.\d+) shots", opp)
        check(m is not None, "the opponent result states the mismatch cost", opp[:70])
        if m:
            check(rounds_to(cost, m.group(1)),
                  "and it is the spread along the unbiased row",
                  "registry {}, transcript {:.4f} - {:.4f} = {:.4f}".format(
                      m.group(1), matrix[0.0][3.0], matrix[0.0][0.0], cost))

    # 3. The prose, which is where the drift happened.
    notes = by_id["headline-policy-comparison"]["notes"]
    # \d+\.\d+ rather than [0-9.]+ throughout the prose patterns. The last of
    # these six sits at the end of a sentence, and a character class holding
    # the dot swallows the full stop, giving float("44.369.").
    m = re.search(r"random (\d+\.\d+) against (\d+\.\d+), parity (\d+\.\d+) against "
                  r"(\d+\.\d+), density (\d+\.\d+) against (\d+\.\d+)", notes)
    check(m is not None, "the headline notes compare TEST against TRAIN", notes[:80])
    if m:
        tm = {p["name"]: p["mean"] for p in test["results"]["policies"]}
        rm = {p["name"]: p["mean"] for p in train["results"]["policies"]}
        for i, name in enumerate(("random", "parity-hunt-target", "density(b=10)")):
            check(rounds_to(tm[name], m.group(1 + 2 * i)),
                  "and its TEST {} is the captured one".format(name),
                  "prose {}, record {}".format(m.group(1 + 2 * i), tm[name]))
            check(rounds_to(rm[name], m.group(2 + 2 * i)),
                  "and its TRAIN {} is the captured one".format(name),
                  "prose {}, record {}".format(m.group(2 + 2 * i), rm[name]))

    m = re.search(r"(\d+\.\d+) within the density family against (\d+\.\d+) on TRAIN",
                  notes)
    check(m is not None, "the notes restate the correlation both folds measured")
    if m:
        for which, record, stated in (("TEST", test, m.group(1)),
                                      ("TRAIN", train, m.group(2))):
            within = [q["rho"] for q in record["results"]["paired"]
                      if q["a"].startswith("density") and q["b"].startswith("density")]
            check(bool(within) and rounds_to(min(within), stated),
                  "and the {} figure is that fold's weakest".format(which),
                  "prose {}, record {}".format(stated, sorted(set(within))))

    # The degenerate pair, described in prose and recorded in the capture.
    # This entry is the TEST one and the sentence used to describe the TRAIN
    # record, which was regenerated after the zero-width interval was replaced
    # while this one was not.
    pair = [q for q in paired
            if {q["a"], q["b"]} == {"density(b=50)", "density(b=200)"}]
    check(len(pair) == 1, "the TEST capture holds the two identical beam widths",
          "found {}".format(len(pair)))
    if pair:
        zero_width = pair[0].get("ci") == [0.0, 0.0]
        says_zero = "[0.000, 0.000]" in notes
        check(zero_width == says_zero,
              "and the notes describe it the way the capture records it",
              "capture ci {!r}, notes {} a zero-width interval".format(
                  pair[0].get("ci"),
                  "quote" if says_zero else "do not quote"))

    m = re.search(r"measured (\d+\.\d+) against a theoretical k\(N\+1\)/\(k\+1\) of "
                  r"(\d+\.\d+)", notes)
    check(m is not None, "the notes state the random shooter's self-test")
    if m:
        theory = 17 * 101 / 18
        check(rounds_to(theory, m.group(2)),
              "and the theoretical value is k(N+1)/(k+1)",
              "prose {}, computed {:.4f}".format(m.group(2), theory))
        check(m.group(1) in test["raw"],
              "and the measured value is in the TEST capture",
              "prose {} is not in the raw transcript".format(m.group(1)))

    # 4. The one figure no artefact holds.
    base_notes = by_id["baseline-reconciliation"]["notes"]
    m = re.search(r"sigma = (\d+\.\d+)", base_notes)
    check(m is not None, "the baseline notes state sigma")
    if m:
        prereg = io.open(PREREG, encoding="utf-8").read()
        check("sigma = " + m.group(1) in prereg,
              "and it agrees with experiments/preregistration.md, which derives it",
              "registry says {} and the pre-registration does not".format(m.group(1)))

    m = re.search(r"(\d+\.\d+) within the density family and (\d+\.\d+) against the hunt",
                  base_notes)
    check(m is not None, "the baseline notes state the bimodal correlation")
    if m:
        within = [q["rho"] for q in train["results"]["paired"]
                  if q["a"].startswith("density") and q["b"].startswith("density")]
        across = [q["rho"] for q in train["results"]["paired"]
                  if q["a"].startswith("density") != q["b"].startswith("density")]
        check(bool(within) and rounds_to(min(within), m.group(1)),
              "and the within-family figure is TRAIN's weakest",
              "prose {}, record {}".format(m.group(1), sorted(set(within))))
        check(bool(across) and all(rounds_to(r, m.group(2)) for r in across),
              "and every across-family pair rounds to the figure beside it",
              "prose {}, record {}".format(m.group(2), sorted(set(across))))

    return report()


if __name__ == "__main__":
    sys.exit(main())
