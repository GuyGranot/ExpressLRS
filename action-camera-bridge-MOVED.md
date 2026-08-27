# The Action Camera Bridge specification moved

It now lives in the repository it specifies:

    https://github.com/GuyGranot/camerabridge  →  docs/spec/

Documents, superseded drafts and the five checkers went together, and the
checkers resolve the corpus relative to themselves, so they run from a clean
checkout with nothing to configure.

This tree held them only because the bridge had no repository when they were
written; nothing in ExpressLRS depends on them. The history up to `4fcc194b`
(specification v1.10) remains on the `camera-bridge-prs` branch here.
