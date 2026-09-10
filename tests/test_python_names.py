"""Every name a Python module uses is a name it binds.

CI was red for two days on one line:

    return 1 if failures else SKIP        tests/test_report_data.py

`failures` had been dropped from that file's `from _harness import ...` during
the harness conversion. Nothing caught it, for a reason worth stating: the line
sits in the branch taken when out/figures.json is absent, and out/ is gitignored
and expensive to regenerate, so every CI leg takes that branch and no local run
ever does. The name was unbound for as long as it took anyone to look.

An import that loses a name is invisible to the interpreter until the line runs.
This reads the syntax instead, so a branch that never executes is checked
anyway, which is the whole point: the lines least likely to be run are the ones
most likely to be wrong.

Deliberately narrow. It reports a name that is loaded and never bound anywhere
in its module, which is the mistake a refactor makes. It is not a linter and it
does not judge style, so it has no opinions to argue with and no noise to
suppress. Over the current tree it reports nothing.
"""

from __future__ import annotations

import ast
import builtins
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _harness import ROOT, check, report  # noqa: E402

# Bound by the interpreter, not by the module.
MODULE_DUNDERS = {
    "__file__", "__name__", "__doc__", "__package__", "__spec__",
    "__loader__", "__builtins__", "__debug__", "__path__",
}

DIRECTORIES = ("tests", "tools", "python")


def bound_names(tree):
    """Every name the module binds, by any means Python offers."""
    out = set()
    for node in ast.walk(tree):
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            for alias in node.names:
                out.add((alias.asname or alias.name).split(".")[0])
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            out.add(node.name)
        elif isinstance(node, ast.Name) and isinstance(node.ctx, (ast.Store, ast.Del)):
            out.add(node.id)
        elif isinstance(node, ast.arg):
            out.add(node.arg)
        elif isinstance(node, ast.ExceptHandler) and node.name:
            out.add(node.name)
        elif isinstance(node, (ast.Global, ast.Nonlocal)):
            out.update(node.names)
    return out


def loaded_names(tree):
    return {n.id for n in ast.walk(tree)
            if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Load)}


def unbound(path):
    """Names this module reads and never binds. Empty is the healthy answer."""
    source = io.open(path, encoding="utf-8").read()
    tree = ast.parse(source, filename=path)
    seen = bound_names(tree) | set(dir(builtins)) | MODULE_DUNDERS
    # A star import brings in names this cannot see, so such a module is not
    # judged rather than judged wrongly.
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom):
            if any(a.name == "*" for a in node.names):
                return []
    return sorted(loaded_names(tree) - seen)


def main():
    print("names used are names bound")
    print("==========================")

    scanned = 0
    offenders = []
    for directory in DIRECTORIES:
        root = os.path.join(ROOT, directory)
        if not os.path.isdir(root):
            continue
        for name in sorted(os.listdir(root)):
            if not name.endswith(".py"):
                continue
            path = os.path.join(root, name)
            scanned += 1
            missing = unbound(path)
            if missing:
                offenders.append((directory + "/" + name, missing))

    for where, missing in offenders:
        print("  {}: {}".format(where, ", ".join(missing)))

    check(not offenders,
          "every name read by a Python module is bound in it",
          "{} module(s) read an unbound name".format(len(offenders)))
    check(scanned >= 20,
          "and the scan reached the tree it is meant to cover",
          "{} files across {}".format(scanned, ", ".join(DIRECTORIES)))
    return report()


if __name__ == "__main__":
    sys.exit(main())
