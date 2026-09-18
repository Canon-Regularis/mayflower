"""One console convention for the analysis layer's self tests.

python/stats_test.py and python/audit_test.py both report through this. It is
a separate module rather than a name one of them owns, because audit_test is
driven from stats_test: whichever of the two held check() would be imported by
the other and imported back, which is the cycle that used to make
audit_test.py defer its import inside the function body.

tests/_harness.py is the same idea for the tests/ directory.
"""

from __future__ import annotations


def check(ok, what, detail=""):
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail:
        print("      " + detail)
    return 0 if ok else 1
