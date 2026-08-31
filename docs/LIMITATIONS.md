# Known limitations

What does not work, or does not scale, and how far each one reaches.

Part of [Mayflower](../README.md).

- The `Sampler` holds backward counts for every layer, about 397 MB on the
  standard instance. Batch generation would remove that; it is not needed until
  board banks get large.
- The no-touching sweep packs its state into one uint64, so it stops at about 13
  rows. `noTouchSupports()` reports whether an instance fits.
- The report page is about 1.5 MB, over the 0.7 to 1.0 MB budget. Nearly all of it
  is the base64 board pool the live widget needs.
- The weighted forward-backward does not rescale, because the backward pass has to
  combine `f` and `b` from the same layer and a per-layer scale would not cancel
  the way a global one does. Weights extreme enough to leave a double need
  `weightedMarginalsByRecount`, which divides two equally scaled counts; the
  forward-backward detects the case and throws rather than returning marginals
  that are inside [0, 1] and wrong.
- The belief MDP caps near 300 configurations, which stops the adaptive column of
  the adaptivity table well before the subset lattice runs out.
- The `pr` suite does not complete reliably on the development machine. Two
  consecutive runs each hit one 900 s timeout, on a different test each time, and
  both of those tests finish in 34 and 48 seconds when run alone. The cause is
  background load rather than a tight limit, and it is the same interference that
  makes absolute timings here untrustworthy.
