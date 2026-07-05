<!--
SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
SPDX-License-Identifier: AGPL-3.0-or-later
-->

# Porting the gateway from zhac-net-core

`zhac-mono-core/main/` is a **hard fork** of the ESP32-S3 gateway layer in
`zhac-net-core/main/` (`ws_bridge`, `main`, `api_*`, `groups_store`, `log_ring`,
`metrics_mqtt`). The two live in separate repos, so a fix made to the net-core
gateway does **not** reach this fork automatically. Until that layer is extracted
into a shared component, changes must be re-ported by hand — and the port-drift
alarm makes "a re-port is due" visible instead of silent.

This is the interim strategy chosen deliberately (mono-core is not yet a
supported release target; a full extraction is only worth it if that changes).
See the repo README's *Divergence* section.

## The alarm

`tools/check-port-drift.sh` hashes the net-core sources listed in
`tools/port-files.txt` and compares them to `tools/port-baseline.sha256` (the
state at the last re-port). It fails when any has changed.

- **CI:** `.github/workflows/port-drift.yml` runs it on push/PR and — crucially —
  on a **weekly schedule**, since a net-core change never triggers this repo's CI.
- **Locally** (net-core checked out as a sibling, as the build already requires):
  ```sh
  tools/check-port-drift.sh              # check   (exit 1 on drift)
  tools/check-port-drift.sh --update     # re-baseline after a re-port
  ```
  Pass an explicit path or set `$ZHAC_NET_CORE` if net-core is elsewhere.

The alarm only says a review is **due** — it does not verify the port is correct.

## When the alarm fires (re-port checklist)

1. **See what changed** in net-core since the baseline:
   ```sh
   git -C ../zhac-net-core log --oneline <baseline-net-core-commit>..master -- main/
   ```
   (`<baseline-net-core-commit>` is recorded at the top of `port-baseline.sha256`.)
2. **For each flagged file**, diff the net-core source against the mono fork and
   decide what applies (some net-core changes are S3-dual-chip-specific and do
   **not** belong in the single-chip build):
   ```sh
   diff -u ../zhac-net-core/main/<file> main/<file>
   ```
3. **Mirror the relevant changes** into `zhac-mono-core/main/<file>`.
4. **Re-baseline** to acknowledge you have reviewed net-core's current state:
   ```sh
   tools/check-port-drift.sh --update
   ```
   Re-baselining records that a human reviewed the net-core change — even if the
   conclusion was "nothing to port here." Never edit `port-baseline.sha256` by hand.
5. **Build + test** the mono firmware, then commit the port and the refreshed
   baseline together.

## Maintaining the tracked set

Edit `tools/port-files.txt` when the fork surface changes:
- **new fork:** add the net-core-relative path (e.g. `main/api_foo.cpp`);
- **retired / extracted-to-shared-component:** remove its line.

Then `--update` to refresh the baseline.
