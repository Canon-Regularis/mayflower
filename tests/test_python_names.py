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

Deliberately narrow, in both directions. It reports a name that is loaded and
never bound anywhere in its module, which is the mistake a refactor makes, and
a name a module imports and never reads, which is what that refactor leaves
behind. Over the current tree it reports nothing either way.

The second direction is not the first one inverted. A missing binding is safe
to report from a set difference, because a false negative is silent. A dead
import is not: the `from __future__ import annotations` in 33 of 34 files is
never read, a deliberate re-export is never read either, and a name shadowed
before its first read IS read and still dead. So it walks imports per scope,
keeps the line number, and honours the `import n as n` spelling that mypy
--strict reads as a re-export under --no-implicit-reexport. The two agree
about what is on purpose.

It is still not a linter and it still judges no style, so it has no opinions
to argue with and no noise to suppress.
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


def bound_names(tree: ast.AST) -> set[str]:
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


def loaded_names(tree: ast.AST) -> set[str]:
    return {n.id for n in ast.walk(tree)
            if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Load)}


def unbound(path: str) -> list[str]:
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


def scopes(tree: ast.AST) -> list[list[ast.stmt]]:
    """Every scope's body, module first."""
    out: list[list[ast.stmt]] = []
    if isinstance(tree, ast.Module):
        out.append(tree.body)
    for n in ast.walk(tree):
        if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            out.append(n.body)
    return out


def imports_in(body: list[ast.stmt]) -> list[tuple[int, str, bool]]:
    """(line, bound name, deliberate) for every import directly in this scope.

    Deliberate covers the two kinds that are never read in the importing module
    and are not dead: a __future__ directive, and the `import n as n` spelling,
    which is how PEP 484 says a module re-exports a name. mypy --strict reads
    that spelling the same way under --no-implicit-reexport, so the checker and
    this test agree about what is on purpose.
    """
    out = []
    for node in body:
        if isinstance(node, ast.Import):
            for a in node.names:
                out.append((node.lineno, (a.asname or a.name).split(".")[0],
                            a.asname is not None and a.asname == a.name))
        elif isinstance(node, ast.ImportFrom):
            future = node.module == "__future__"
            for a in node.names:
                out.append((node.lineno, a.asname or a.name,
                            future or (a.asname is not None and a.asname == a.name)))
    return out


def shadowed_unread(tree: ast.AST, name: str, lineno: int) -> bool:
    """Rebound after the import, with nothing having read it in between.

    Both halves matter, and a set cannot express either. python/stats_test.py
    reads VAL_SHARE and then assigns it back under a global, so the import is
    used and the later store is not a shadow. tests/test_selfplay_fold.py
    imported run from the harness and then defined one, so that import was dead
    however many times run was called, and set subtraction could not see it
    because the name is read all over the file.
    """
    first_store = None
    for n in ast.walk(tree):
        at = getattr(n, "lineno", 0)
        if at <= lineno:
            continue
        stores = ((isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef))
                   and n.name == name)
                  or (isinstance(n, ast.Name) and isinstance(n.ctx, ast.Store)
                      and n.id == name))
        if stores and (first_store is None or at < first_store):
            first_store = at
    if first_store is None:
        return False
    return not any(isinstance(n, ast.Name) and isinstance(n.ctx, ast.Load)
                   and n.id == name and lineno < getattr(n, "lineno", 0) < first_store
                   for n in ast.walk(tree))


def unread_imports(path: str) -> list[str]:
    """Names this module imports and never reads. Empty is the healthy answer.

    The other direction from unbound() above, and not the same subtraction
    inverted. A missing binding is safe to report from a set because a false
    negative is silent; a dead import is not, for four reasons this handles
    one at a time: the __future__ directive in 33 of 34 files is never read,
    a deliberate re-export is never read either, a name shadowed before its
    first read IS read and still dead, and the line number has to survive so
    the report says where.
    """
    source = io.open(path, encoding="utf-8").read()
    tree = ast.parse(source, filename=path)
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom) and any(a.name == "*" for a in node.names):
            return []
    read = loaded_names(tree)
    exported: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and any(
                isinstance(x, ast.Name) and x.id == "__all__" for x in node.targets):
            if isinstance(node.value, (ast.List, ast.Tuple)):
                exported |= {e.value for e in node.value.elts
                             if isinstance(e, ast.Constant) and isinstance(e.value, str)}
    out = []
    for body in scopes(tree):
        for lineno, name, deliberate in imports_in(body):
            if deliberate or name in exported:
                continue
            if name not in read:
                out.append("{} (line {})".format(name, lineno))
            elif shadowed_unread(tree, name, lineno):
                out.append("{} (line {}, shadowed before any read)".format(name, lineno))
    return sorted(set(out))


def main() -> int:
    print("names used are names bound")
    print("==========================")

    scanned = 0
    offenders = []
    dead = []
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
            unread = unread_imports(path)
            if unread:
                dead.append((directory + "/" + name, unread))

    for where, missing in offenders:
        print("  {}: {}".format(where, ", ".join(missing)))

    for where, unread in dead:
        print("  {}: {}".format(where, ", ".join(unread)))

    check(not offenders,
          "every name read by a Python module is bound in it",
          "{} module(s) read an unbound name".format(len(offenders)))
    check(not dead,
          "and every name a module imports, it reads",
          "{} module(s) carry an import nothing reads".format(len(dead)))
    check(scanned >= 20,
          "and the scan reached the tree it is meant to cover",
          "{} files across {}".format(scanned, ", ".join(DIRECTORIES)))
    return report()


if __name__ == "__main__":
    sys.exit(main())
