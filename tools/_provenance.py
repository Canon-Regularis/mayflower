"""Which build produced an artefact.

Three answers to one question lived in this directory and one file above it:
tools/collect_results.py returned the short hash or "unknown",
tools/run_headline.py returned the full hash with a "-dirty" suffix, and
tools/report_data.cpp returned the short hash with a hex check. They land in
experiments/results.json, experiments/headline_*.json and out/figures.json
respectively, and tests/test_provenance.py reads across all three.

The two Python ones share their mechanism here. The formats stay different,
because they answer for different things and the difference is deliberate:

  short   for an artefact that is regenerated alongside the tree it describes,
          where the hash is a label a reader can look up.
  full    for a headline record, which is meant to pin a sealed-fold result to
          a build forever. A twelve-hex prefix is a shorthand, not an
          identifier, and a tree with uncommitted changes is not identified by
          its commit at all, so that case is marked.

The C++ one stays where it is. It is a cross-language third and cannot import
this; what it can do, and does, is agree on the short format.
"""

from __future__ import annotations

import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

UNKNOWN = "unknown"


def git(*args):
    """Stripped stdout, or None if git is absent, fails, or hangs.

    Every failure mode returns None rather than raising, because provenance is
    a field on an artefact and a checkout without git should still produce the
    artefact. The MSYS2 UCRT64 CI leg is that case.
    """
    try:
        r = subprocess.run(["git"] + list(args), cwd=ROOT, capture_output=True,
                           text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    return r.stdout.strip() if r.returncode == 0 else None


def short_commit():
    """The abbreviated hash, or "unknown"."""
    return git("rev-parse", "--short", "HEAD") or UNKNOWN


def full_commit():
    """The full hash, suffixed "-dirty" when the tree has uncommitted changes."""
    head = git("rev-parse", "HEAD")
    if not head:
        return UNKNOWN
    return head + ("-dirty" if git("status", "--porcelain") else "")
