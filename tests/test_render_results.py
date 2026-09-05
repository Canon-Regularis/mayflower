"""The results dossier renderer, which nothing ran.

`tools/render_results.py` is 600 lines producing the secondary deliverable, and
no test and no CI job executed it. `compileall` proved it parses; nothing proved
it produces a page. The renderer concatenates strings, so it cannot fail on
output that came out wrong, only on output that came out not at all.

What it renders is checked against experiments/results.json rather than against
fixed text, because the file's own claim is that nothing on the page is typed in.

    python tests/test_render_results.py
"""

from __future__ import annotations

import html
import io
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP = 77
failures = 0


def check(ok, what, detail=""):
    global failures
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail and not ok:
        print("      " + detail)
    if not ok:
        failures += 1


def digits(s):
    """Strip what the page puts between digit groups: it separates thousands
    with a thin space written as the entity &thinsp;, so the entities are decoded
    before the separators are removed."""
    return re.sub(r"[\u2009\u202f,\s]", "", html.unescape(s))


def main():
    print("the results dossier")
    print("===================")
    src = os.path.join(ROOT, "experiments", "results.json")
    if not os.path.exists(src):
        print("  experiments/results.json is absent")
        return SKIP

    out = os.path.join(ROOT, "out", "results.html")
    before = io.open(out, "rb").read() if os.path.exists(out) else None

    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "render_results.py")],
                       capture_output=True, text=True, timeout=300, cwd=ROOT)
    check(r.returncode == 0, "the renderer succeeds", r.stderr.strip()[-160:])
    if not os.path.exists(out):
        check(False, "it wrote a page at all")
        return 1

    page = io.open(out, encoding="utf-8").read()
    flat = digits(page)

    check(len(page) > 15000, "the page is a page rather than a stub",
          "{} bytes".format(len(page)))
    check(page.count("<table") >= 5, "it carries the result tables",
          "{} tables".format(page.count("<table")))
    check(page.count("<tr") >= 30, "and rows in them",
          "{} rows".format(page.count("<tr")))
    check("<td></td>" not in page, "no cell was left empty")

    # A renderer that hands a None or a division result straight to format()
    # prints it, and the page still looks like a page.
    junk = re.findall(r">\s*(None|nan|-nan|inf|-inf|undefined|null|NaN)\s*<", page)
    check(not junk, "nothing printed as None, NaN or an infinity",
          "found {}".format(sorted(set(junk))))

    # Every figure has to stand alone, so each needs its own viewBox.
    svgs = re.findall(r"<svg[^>]*>", page)
    check(svgs, "the page carries figures", "none found")
    check(all("viewBox" in s for s in svgs),
          "and every figure declares a viewBox",
          "{} of {} without one".format(
              sum(1 for s in svgs if "viewBox" not in s), len(svgs)))

    # The claim the file makes about itself: the numbers come from the JSON.
    # Checked on the ones the report is built around, so a page that rendered
    # but lost its content fails here rather than passing on its shape.
    data = json.load(io.open(src, encoding="utf-8"))
    by_id = {r["id"]: r["value"] for r in data["results"]}
    anchors = ["omega0", "transcripts", "edges", "peakStates"]
    for key in anchors:
        if key not in by_id:
            check(False, "results.json still carries {}".format(key))
            continue
        check(str(by_id[key]) in flat,
              "the page prints the collected {}".format(key),
              "{} is not on the page".format(by_id[key]))

    # Same input, same page: the dossier is regenerated for every release and a
    # renderer that reordered a dict would churn the diff without changing a
    # number.
    again = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "render_results.py")],
                           capture_output=True, text=True, timeout=300, cwd=ROOT)
    check(again.returncode == 0 and io.open(out, encoding="utf-8").read() == page,
          "rendering twice gives the same page")

    if before is not None and io.open(out, "rb").read() != before:
        io.open(out, "wb").write(before)

    print("\n" + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
