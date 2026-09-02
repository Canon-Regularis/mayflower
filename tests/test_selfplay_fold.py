"""The guard on the sealed TEST fold, exercised through the tool that holds it.

`tools/selfplay` produces the headline policy numbers and is the only thing that
can read a fold. It refuses TEST without a token minted by the analysis layer,
which is the whole protection on the pre-registered data, and nothing tested it:
`test_folds.cpp` checks the fold *function*, and selfplay was not a registered
test at all.

The refusal is asserted to be cheap as well as correct. It used to sit after a
24 s board-bank build, so refusing cost as much as complying and no suite could
afford to check it.

    python tests/test_selfplay_fold.py
"""

from __future__ import annotations

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP = 77

failures = 0


def check(ok, what, detail=""):
    global failures
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail:
        print("      " + detail)
    if not ok:
        failures += 1


def exe():
    for name in ("selfplay.exe", "selfplay"):
        p = os.path.join(ROOT, "build", name)
        if os.path.exists(p):
            return p
    return None


def run(args, timeout=60):
    return subprocess.run([exe()] + args, capture_output=True, text=True, timeout=timeout)


def main():
    print("the seal on the TEST fold")
    print("=========================")
    if exe() is None:
        print("  build/selfplay is missing; build first")
        return SKIP

    # Asking for TEST without the token is refused, and nothing is drawn.
    r = run(["300", "0", "test"])
    check(r.returncode == 2, "asking for TEST without a token is refused",
          "exit {}".format(r.returncode))
    check("sealed" in r.stdout.lower(), "and says so")
    check("drew" not in r.stdout and "mean" not in r.stdout,
          "and no board is drawn and no policy is scored",
          r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "(no output)")

    # A misspelt fold must not fall back to a fold the caller did not ask for.
    r = run(["300", "0", "trian"])
    check(r.returncode != 0, "a misspelt fold is refused rather than defaulted",
          "exit {}".format(r.returncode))
    check("drew" not in r.stdout, "and it draws nothing either")

    # The token is the only way through. Checked by letting the tool get past
    # the guard and into its setup, then stopping it: reaching the build is the
    # evidence, and waiting for it to finish would cost half a minute.
    accepted = False
    try:
        run(["300", "0", "test", "--unsealed"], timeout=6)
    except subprocess.TimeoutExpired:
        accepted = True     # got past the guard and into the board bank
    else:
        accepted = False
    check(accepted, "the unseal token is accepted and the run proceeds")

    # Refusing must not depend on the expensive setup, or no suite can afford
    # to check it.
    import time
    t0 = time.time()
    run(["20000", "0", "test"])
    dt = time.time() - t0
    check(dt < 5.0, "refusal costs no setup", "{:.2f} s at 20,000 games".format(dt))

    print("\n" + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
