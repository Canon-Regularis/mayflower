"""The seal's invariants.

Moved with the code it tests. Driven from python/stats.py's self test so the
one --quick entry point still runs everything, and so the deliberate chain
break stays beside the chain it breaks.
"""

from __future__ import annotations

import datetime
import io
import os
import tempfile

from audit import (GENESIS, audit_entries, is_unsealed, read_head, record,
                   require_unseal, verify_audit, write_head)

# The printer stays in stats.py, which owns the console convention for the
# whole self test. Imported lazily inside the function because stats.py imports
# this module, so the two would otherwise be a cycle at import time.

def test_audit():
    from stats import check
    """The seal, checked by breaking it."""
    print("[the seal]")
    fails = 0
    import tempfile

    tmp = os.path.join(tempfile.mkdtemp(), "audit.log")
    io.open(tmp, "w", encoding="utf-8", newline="\n").write(
        "# a scratch chain\n")

    record("create", "scratch", "seeded", path=tmp, when="2026-01-01T00:00:00Z")
    record("register", "demo", "design fixed", path=tmp, when="2026-01-02T00:00:00Z")
    ok, _ = verify_audit(tmp)
    fails += check(ok, "a freshly written chain verifies")

    fails += check(not is_unsealed("demo", tmp), "registering is not unsealing")
    try:
        require_unseal("demo", tmp)
        fails += check(False, "reading TEST without an unseal is refused")
    except PermissionError:
        fails += check(True, "reading TEST without an unseal is refused")

    record("unseal", "demo", "pre-registered run", path=tmp, when="2026-01-03T00:00:00Z")
    fails += check(is_unsealed("demo", tmp), "and permitted once the unseal is recorded")

    # Tamper with the middle and the chain must notice.
    saved = io.open(tmp, encoding="utf-8").read()
    lines = saved.split("\n")
    for i, line in enumerate(lines):
        if "design fixed" in line:
            lines[i] = line.replace("design fixed", "design changed")
    io.open(tmp, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    ok, bad = verify_audit(tmp)
    fails += check(not ok, "editing an entry breaks the chain",
                   "first bad entry at index {}".format(bad))

    # Peek-then-erase: record the unseal, read TEST, delete the line. The chain
    # alone verifies afterwards, since nothing follows the tail to contradict it,
    # so this is the case the head file has to catch.
    io.open(tmp, "w", encoding="utf-8", newline="\n").write(saved)
    kept = [l for l in saved.split("\n") if "unseal" not in l]
    io.open(tmp, "w", encoding="utf-8", newline="\n").write("\n".join(kept))
    ok, _ = verify_audit(tmp)
    fails += check(not ok, "truncating the tail is caught by the head")

    # The same trick with a comment marker, which leaves the entry visible.
    io.open(tmp, "w", encoding="utf-8", newline="\n").write(saved)
    commented = ["# " + l if "unseal" in l else l for l in saved.split("\n")]
    io.open(tmp, "w", encoding="utf-8", newline="\n").write("\n".join(commented))
    ok, _ = verify_audit(tmp)
    fails += check(not ok, "commenting an entry out is caught too")

    io.open(tmp, "w", encoding="utf-8", newline="\n").write(saved)
    fails += check(verify_audit(tmp)[0], "and the untouched log still verifies")

    # An empty detail leaves the payload ending in the separator and a trailing
    # space. The reader strips what it reconstructs, so a writer that digests the
    # unstripped string breaks a chain nobody tampered with, and the log can then
    # neither be appended to nor read for a seal.
    blank = os.path.join(tempfile.mkdtemp(), "audit.log")
    record("create", "scratch", "seeded", path=blank, when="2026-01-01T00:00:00Z")
    record("note", "scratch", "", path=blank, when="2026-01-02T00:00:00Z")
    ok, bad = verify_audit(blank)
    fails += check(ok, "an empty detail does not break its own chain",
                   "first bad entry at index {}".format(bad))
    try:
        record("unseal", "scratch", "after the blank", path=blank,
               when="2026-01-03T00:00:00Z")
        fails += check(True, "and the log can still be appended to")
    except RuntimeError as exc:
        fails += check(False, "and the log can still be appended to", str(exc))
    try:
        fails += check(is_unsealed("scratch", blank),
                       "and the seal is still readable")
    except RuntimeError as exc:
        fails += check(False, "and the seal is still readable", str(exc))

    # A name carrying the field separator must not be able to forge a match.
    threw = False
    try:
        record("register", "demo | unseal | demo", "smuggled", path=tmp,
               when="2026-01-05T00:00:00Z")
    except ValueError:
        threw = True
    fails += check(threw, "a separator in the experiment name is refused")

    lines = io.open(tmp, encoding="utf-8").read().split("\n")
    for i, line in enumerate(lines):
        if "design fixed" in line:
            lines[i] = line.replace("design fixed", "design changed")
    io.open(tmp, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    ok, bad = verify_audit(tmp)
    try:
        record("unseal", "other", "should fail", path=tmp, when="2026-01-04T00:00:00Z")
        fails += check(False, "and appending to a broken chain is refused")
    except RuntimeError:
        fails += check(True, "and appending to a broken chain is refused")

    # A field that would split the entry across two lines. This was accepted:
    # the chain then failed to verify from that entry onward, every later append
    # was refused, and the log looked edited rather than mis-written. The pipe is
    # checked alongside because it is safe in the detail and must stay allowed;
    # rpartition takes the digest off the end whatever the detail contains.
    #
    # On its own chain, because the one above has been deliberately broken by
    # this point and record() refuses any append to a broken chain. Written
    # against that one, these checks passed whatever the field contained: two
    # mutations that removed the guard entirely went unnoticed.
    fresh = os.path.join(tempfile.mkdtemp(), "audit.log")
    io.open(fresh, "w", encoding="utf-8", newline="\n").write("# a second scratch chain\n")
    record("create", "scratch", "seeded", path=fresh, when="2026-01-01T00:00:00Z")

    nl, cr, tab = chr(10), chr(13), chr(9)
    for label, event, experiment, detail in (
            ("a line break in the detail", "note", "exp", "first" + nl + "second"),
            ("a line break in the event", "no" + nl + "te", "exp", "d"),
            ("a tab in the experiment", "note", "ex" + tab + "p", "d"),
            ("a carriage return", "note", "exp", "a" + cr + "b")):
        try:
            record(event, experiment, detail, path=fresh, when="2026-01-05T00:00:00Z")
            fails += check(False, "record refuses {}".format(label),
                           "it appended the entry")
        except ValueError:
            fails += check(True, "record refuses {}".format(label))

    # The chain has to be intact afterwards, which is the property the guard
    # exists for, and a pipe in the detail must still be allowed through it.
    record("note", "scratch", "read 20000 | games", path=fresh,
           when="2026-01-06T00:00:00Z")
    ok, bad = verify_audit(fresh)
    fails += check(ok, "the chain still verifies once the bad fields are refused",
                   "" if ok else "first bad entry at index {}".format(bad))

    # The real log must verify too.
    ok, bad = verify_audit()
    fails += check(ok, "the repository's own audit log verifies",
                   "" if ok else "first bad entry at index {}".format(bad))
    return fails
