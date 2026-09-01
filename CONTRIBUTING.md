# Contributing to AURA

Issues and pull requests are welcome. We will do our best to take them into account to make AURA better.

## Before you open a PR

Build and run the tests. Both are in the [README](README.md).

The suite is fast and covers the analyzer, the filter design, the IR export and
the processor. A change that breaks it will not be merged.

If you change how a curve is drawn or how the correction is derived, add or
adjust a test for it — most of the suite exists because something was once
subtly wrong and looked fine.

## Licensing

AURA is licensed under the [GNU AGPL v3](COPYING). Contributions are
accepted under the same licence — by opening a pull request you agree your work
is licensed that way. There is no CLA.

If you add a third-party dependency, it must be compatible with the AGPL, and it
needs an entry in [`LICENSE`](LICENSE) and its verbatim licence in
[`THIRD-PARTY-NOTICES`](THIRD-PARTY-NOTICES).
