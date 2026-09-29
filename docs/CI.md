# Continuous integration

What runs on a push, what runs on a pull request, and what runs nightly.

Part of [Mayflower](../README.md).

Every push and pull request builds on Linux with GCC 13 and Clang, in Release and
Debug, and on Windows with MinGW-w64 on the UCRT runtime, which is the only
configuration that compiles the Windows branch of
`src/platform/bench_platform.cpp`; every leg compiles the file, and the
others preprocess it to the fallback that reports no topology. Each runs the
fast suite. The Linux legs build with `-Werror`; the Windows compiler floats with
the MSYS2 mirror, so warnings there are printed rather than fatal.

Two sanitizers run that the development machine cannot, since MinGW ships no
sanitizer runtimes: ASan with UBSan over the fast suite, and TSan over the
threaded rungs, which is the only concurrency in the engine. TSan needs
`vm.mmap_rnd_bits=28`, or its shadow mapping does not survive the kernel's
default ASLR entropy.

The extended suite (`-L pr`) gates a pull request rather than every push. Nightly
adds board-generator uniformity at 300,000 draws, and rebuilds the figure data
and the report end to end, which is where the six figure-data tests run
against real data instead of reporting Skipped: `report_data`, `results`,
`scrubber_js`, `provenance`, `figure_marks` and `pool`.

The nightly also publishes. Once the page has passed its own size, figure-count
and pool-identity assertions, the report and the dossier are deployed to GitHub
Pages, so the deliverable has a permanent public address rather than a fourteen
day artifact behind a login. That job is the only one with write scope of any
kind: the workflow default is `contents: read` and the deploy job alone adds
`pages: write` and `id-token: write`. It builds nothing and runs no test.

Three things are asserted that a green tick would otherwise hide. Twenty-one tests
are registered only when CMake finds Python, four of them needing Node as well,
so CI names them and fails if any is missing rather than passing a suite that
quietly shrank. That check reads the listing rather than the exit code, because
`ctest` ignores `--no-tests=error` in `-N` mode and exits zero for a pattern
matching nothing: the earlier form could not fail. And `ctest` exits zero on a
selection that matches nothing, so every invocation that runs tests carries
`--no-tests=error`.

The third is the complement of the second. Thirteen tests in the fast label are
`SKIPPABLE`. Six are the figure-data list above, which the gate covers. The
other seven were in no protected list at all: `platform`, `static_types`,
`selfplay_fold`, `run_headline`, `render_results`, `tool_cli` and `live_js`.
Being registered proves nothing about whether they ran, and a skip exits zero,
so any of them could have skipped on every runner for as long as the project has
existed and reported green every time.

The report pipeline, which is the one job where everything the label reads
exists, now runs the whole label and names which tests are allowed to skip there
and why: `platform`, whose thread pinning is implemented for Windows only, and
`static_types`, whose checker is installed in the reference-models job instead.
Any other skip fails the job, and so does the absence of those two, because both
skip there by construction and their absence would mean the step had stopped
reading `ctest`'s summary rather than the suite having improved.
`tests/test_stated_counts.py` derives both lists and all three counts above
from the registrations, so neither this paragraph nor the workflow holds a
hand-kept copy of any of them.

One of the seven is a finding rather than a case the step covers. `run_headline`
carries `SKIPPABLE` and has no code path that returns 77, so the flag claims a
behaviour the test does not have.

Supply chain: the two actions not published by GitHub, `ccache-action` and
`setup-msys2`, are pinned to a commit rather than a tag, since a tag is a
mutable pointer. `actions/*` stays on its major tag so Dependabot can keep
patching it. Those actions are not the whole third-party surface: the
reference-models job installs `mypy` from PyPI and runs it over the checkout,
pinned to a version rather than a digest. No job needs the job token after its
clone, so every checkout sets `persist-credentials: false` rather than leaving
it in `.git/config` for the steps that follow.

```sh
ctest --preset fast     # what CI runs on every push
```

README times it. It was timed here too, in a sentence that had drifted to less
than half the figure two files away, which is what a number kept in two places
does.
