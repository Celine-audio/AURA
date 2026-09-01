# Security policy

## Supported versions

Only the latest release is supported; fixes will not be backported.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting: the **Security** tab on this
repository, then **Report a vulnerability**. That keeps the report private until
a fix exists.

Please do not open a public issue for a security problem.

If a report is valid you will be credited in the release notes unless you would rather not be.

## Possible weak points

AURA runs inside a host application, with the host's privileges. Issues may arise from :

- **Saved session state** — the host hands back an XML blob holding the captured
  spectra, base64-encoded, decoded in `MatchEngine::restoreFrom()`. It is parsed
  before it is trusted, and a session file can come from anywhere.
- **Impulse response export** — a user-nominated path written through
  `IrExport::writeWav()`.

A crash, a hang or memory corruption reachable from either of those is worth
reporting.

## What is not

- **Unsigned binaries.** Releases are not code signed, so Gatekeeper and
  SmartScreen will warn. This is a known and documented state.
- **Matching accuracy.** How faithfully a correction curve reproduces the
  reference is a question of DSP, not security; open a normal issue.
- Vulnerabilities in JUCE or another dependency: please report those upstream.
