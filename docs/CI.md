# Continuous integration

What runs on a push, what runs on a pull request, and what runs nightly.

Part of [Mayflower](../README.md).

Every push and pull request builds on Linux with GCC 13 and Clang, in Release and
Debug, and on Windows with MinGW-w64 on the UCRT runtime, which is the only
configuration that compiles `src/platform/bench_platform_win.cpp`. Each runs the
fast suite. The Linux legs build with `-Werror`; the Windows compiler floats with
the MSYS2 mirror, so warnings there are printed rather than fatal.

Two sanitizers run that the development machine cannot, since MinGW ships no
sanitizer runtimes: ASan with UBSan over the fast suite, and TSan over the
threaded rungs, which is the only concurrency in the engine. TSan needs
`vm.mmap_rnd_bits=28`, or its shadow mapping does not survive the kernel's
default ASLR entropy.

The extended suite (`-L pr`) gates a pull request rather than every push. Nightly
adds board-generator uniformity at 300,000 draws, and rebuilds the figure data
and the report end to end, which is where `report_data`, `results` and
`scrubber_js` run against real data instead of reporting Skipped.

Two things are asserted that a green tick would otherwise hide. Ten fast tests
are registered only when CMake finds Python or Node, so CI names them and fails
if any is missing rather than passing a suite that quietly shrank. And `ctest`
exits zero on a selection that matches nothing, so every invocation carries
`--no-tests=error`.

```sh
ctest --test-dir build -L fast     # ~35 s, what CI runs on every push
```
