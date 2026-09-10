"""The seal over the TEST fold, and the log that records breaking it.

Split out of python/stats.py, where it sat among the intervals and the sample
size arithmetic and shared no call edge with any of it. The two halves do not
even share imports: this needs hashlib, datetime, io and os; the statistics need
math and random.

It is also the only part of that module anything outside it uses.
tools/run_headline.py reaches require_unseal, verify_audit and record, and
nothing else. python/stats.py re-exports those three so that resolving them
through the module attribute keeps working, which is how the caller reaches them
and how tests/test_run_headline.py replaces them.
"""

from __future__ import annotations

import datetime
import hashlib
import io
import os

# --- the seal -------------------------------------------------------------
#
# TEST is sealed, and a seal is worth something only if breaking it leaves a
# mark. The mechanism, and then its limits.
#
# Caught:
#   Editing an entry.        Every line carries the digest of everything before
#                            it, so an edit invalidates every later line.
#   Deleting an interior     Same reason.
#     entry.
#   Deleting or commenting   The head file records how many entries there should
#     out trailing entries.  be and what the last hash is. Removing the tail
#                            leaves a chain that is internally consistent, which
#                            is why the chain alone is not enough, and the head
#                            file is what notices.
#
# Not caught:
#   Anyone with write access to both files can recompute the whole history:
#   the digest takes only public inputs, so there is no key and no proof of
#   authorship. A determined author can rewrite the log and the head together
#   and both will verify.
#
#   The anchor against that is version control, not cryptography. Once the log
#   and its head are committed, rewriting them is a diff someone can see. The
#   chain reduces tampering from "edit one line" to "rewrite the file, the head,
#   and the history that contains them", which is the honest claim. It is not
#   append-only in the tamper-proof sense and this file does not claim to be.
#
# Two attacks the chain alone admits, and the head file is what closes both:
# truncating the last line leaves a prefix that still verifies, and prefixing it
# with "#" does the same while leaving the entry visible in the file.

AUDIT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "..", "experiments", "audit.log")
HEAD_PATH = AUDIT_PATH + ".head"
GENESIS = "0" * 64


def _digest(previous: str, payload: str) -> str:
    return hashlib.sha256((previous + "|" + payload).encode("utf-8")).hexdigest()


def audit_entries(path=None):
    """Every entry as (payload, recorded_hash), in file order."""
    path = path or AUDIT_PATH
    if not os.path.exists(path):
        return []
    out = []
    for line in io.open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        payload, _, recorded = line.rpartition(" | ")
        out.append((payload.strip(), recorded.strip()))
    return out


def _head_path(path):
    return (path or AUDIT_PATH) + ".head"


def read_head(path=None):
    """The expected (count, hash). Absent head means an unanchored log."""
    hp = _head_path(path)
    if not os.path.exists(hp):
        return None
    text = io.open(hp, encoding="utf-8").read().split()
    if len(text) != 2:
        return None
    return int(text[0]), text[1]


def write_head(count, digest, path=None):
    with io.open(_head_path(path), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("{} {}\n".format(count, digest))


def verify_audit(path=None):
    """Recompute the chain and check it against the head.

    Returns (ok, index of the first bad entry), with -1 for a chain that is
    internally fine but whose head disagrees, which is what a truncation looks
    like.
    """
    entries = audit_entries(path)
    previous = GENESIS
    for i, (payload, recorded) in enumerate(entries):
        # The sequence number is inside the payload, so a gap breaks the digest
        # as well as the count.
        expected = _digest(previous, payload)
        if expected != recorded:
            return False, i
        previous = recorded

    head = read_head(path)
    if head is None:
        return len(entries) == 0, -1
    count, digest = head
    if count != len(entries) or digest != previous:
        return False, -1
    return True, -1


def record(event: str, experiment: str, detail: str, path=None, when=None):
    """Append one entry, extend the chain, and move the head."""
    path = path or AUDIT_PATH
    ok, bad = verify_audit(path)
    if not ok:
        raise RuntimeError(
            "audit chain or head is broken at entry {}; refusing to append".format(bad))
    entries = audit_entries(path)
    previous = entries[-1][1] if entries else GENESIS
    stamp = when or datetime.datetime.now(datetime.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ")
    if "|" in experiment or "|" in event:
        raise ValueError("event and experiment must not contain the field separator")
    # One entry is one line. A control character in any field splits it in two,
    # and the second half carries a digest for a payload that no longer exists,
    # so verify_audit fails from that entry onward. Every later append is then
    # refused, is_unsealed raises, and reading TEST becomes impossible: a caller
    # passing a captured error message as the detail would brick the log and
    # leave it looking edited. The separator check above guards the two
    # positional fields against forgery; this guards all three against that.
    for _name, _value in (("event", event), ("experiment", experiment),
                          ("detail", detail)):
        if any(ch < " " for ch in _value):
            raise ValueError(
                "{} must not contain a control character; one entry is one line "
                "and a break in it stops the chain verifying".format(_name))
    # Stripped before it is digested, because the reader strips what it
    # reconstructs. An empty detail would otherwise leave a trailing space in the
    # digested string but not in the parsed one, breaking a chain nobody touched.
    payload = "{:04d} | {} | {} | {} | {}".format(
        len(entries) + 1, stamp, event, experiment, detail).strip()
    digest = _digest(previous, payload)
    with io.open(path, "a", encoding="utf-8", newline="\n") as fh:
        fh.write(payload + " | " + digest + "\n")
    write_head(len(entries) + 1, digest, path)
    return payload


def is_unsealed(experiment: str, path=None) -> bool:
    ok, bad = verify_audit(path)
    if not ok:
        raise RuntimeError("audit chain or head is broken at entry {}".format(bad))
    for payload, _ in audit_entries(path):
        parts = [p.strip() for p in payload.split("|")]
        # seq | timestamp | event | experiment | detail
        if len(parts) >= 4 and parts[2] == "unseal" and parts[3] == experiment:
            return True
    return False


def require_unseal(experiment: str, path=None):
    """Guards TEST-fold data. Raises unless the unseal is already on record."""
    if not is_unsealed(experiment, path):
        raise PermissionError(
            "experiment '{}' has not been unsealed; record the unseal in "
            "experiments/audit.log before reading TEST".format(experiment))


# --- the checks -----------------------------------------------------------
