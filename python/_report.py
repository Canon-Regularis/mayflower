"""One console convention for the analysis layer's self tests.

python/stats_test.py and python/audit_test.py both report through this. It is
a separate module rather than a name one of them owns, because audit_test is
driven from stats_test: whichever of the two held check() would be imported by
the other and imported back, which is the cycle that used to make
audit_test.py defer its import inside the function body.

tests/_harness.py is the same idea for the tests/ directory, and it is not
the same function. Three things differ deliberately. That one accumulates
failures in a module global and this one returns 0 or 1 for the caller to add
up, because the analysis layer's self tests already count their own. That one
returns the bool so a caller can branch on it; this one returns the count.

And that one prints the detail line only on failure, because
tests/_harness.py's own docstring says a detail under a passing check states a
contradiction. This prints it always. That is the older convention, kept here
because these two self tests use the detail line to report the measured value
on every run, not only when it is wrong, and their output is read as a table.
"""

from __future__ import annotations


def check(ok: bool, what: str, detail: str = "") -> int:
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail:
        print("      " + detail)
    return 0 if ok else 1
