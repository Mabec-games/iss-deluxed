# v0.5.0-beta.3 release validation

Touch control candidate: native A/B/X/Y labels, eight-direction movement,
small circular dead zone, stable movement-finger capture and visible feedback.
Android version name 0.5.0-beta.3, upgrade code 9.
Private evidence is retained under `build/beta-release-0.5.0-beta.3/`.

## Regression gate

The fresh `python -m pytest tests -q -rs` run exited successfully:
**456 passed, 6 skipped, 8 subtests passed in 3183.84 seconds (53:03)**.
All 462 collected cases were accounted for with no failures.

The six skips were idle/default-device audio timing, the opt-in 35-match World
Series, sustained natural added-stadium goals, extended visual period transitions,
one system-Python Tcl/Tk editor initialization check, and an unavailable
penalty-area save fixture. The editor case subsequently passed independently;
the other five exclusions are retained from the previous beta's acceptance
scope, not fresh successful tests.

The suite uses the current rebuilt Windows executable and includes the new touch
and host rotation regression cases. Focused touch/host-input/archive checks passed
**29 tests**. Before implementation, the label and small-thumb-movement tests
failed for the reported defects. Deliberately disabling captured-finger ownership
then failed both identity and capture-safety regressions; restoring it returned
the focused checks to green.

The full run also encountered a system-Python Tcl/Tk initialization skip in the
editor tile-pane test. That exact test passed in a fresh isolated process
(**1 passed**). The two-pane check independently recorded **1 passed, 1 skipped**;
the skip is retained in the full-run totals rather than counted as a full-suite
pass. Frozen/relocated editor workflow checks passed separately.

Independent review found a rotation rearming problem. A controlled SDL contact
snapshot reproduced it against the actual host pump and touch module; the fixed
pump waits for all contacts to release after dimensions change. The reviewer
independently ran the rotation regression and approved the final runtime scope.

## Touch behavior and rendering

Tests cover all native button bits, direction plus action, eight angular sectors,
centre dead zone, sliding beyond the pad, stable device/finger IDs after action
finger release, avoiding Menu activation while dragging a captured thumb, and
clearing movement on hide/disable/viewport/layout/suspend resets. Existing saved
placement and scale controls retain their bounds and rotation behavior.

Actual production SDL drawing functions rendered overlays at 1280x720,
1920x1080 and 1080x1920. Pixel assertions verify visible diagonal highlighting
while holding an action; direction/action masks and thumb release also passed.
The resulting UI previews were visually inspected. This is software-renderer
host evidence, not a physical touchscreen test.

## Builds and package checks

Windows x64 and WSL Ubuntu 24.04 Linux x64 Release builds succeeded. Android
`assembleRelease` succeeded for arm64-v8a and x86_64. The frozen Mod Studio was
rebuilt and its isolated self-test passed.

All archive CRCs, explicit public resource lists and SHA-256 checksums passed.
Actual APK checks passed manifest/package/version/code/minimum SDK, both ABI
ELF headers, all native libraries, canonical boot deny asset, alignment and v2
signature. The certificate matches the published beta.2 APK; that previous
APK's local SHA-256 was checked against GitHub's published asset digest.

Clean extracted Windows/Linux bundles started both independent stadium profiles
through original menus: IDs 8/31, lengths 1728/1664, template 0, width 576, with
Tactical 80%/Tactical Wide 67% respectively. Camera trace confirms rendering;
Linux final images and full guest RAM match Windows. The extracted frozen editor
and relocated bundled editor source passed authored preview, invalid-draft and
portable export workflows. The source-editor check used a complete Windows
Python/Tk runtime; it does not certify a Linux GUI.

Each platform bundle contains editor source, licenses and the validated
MEADOW/HARBOR example pack; Windows includes the frozen editor. Separate editor,
example, Liga MX and World Cup 2026 ZIPs remain available. Packages select no ROM,
personal configuration, saves or investigation dumps. Final documentation
reassembly must retain the clean-tested runtime/resource bytes before upload.

## Remaining physical acceptance

No Android device was connected. Physical thumb feel, input latency, Android
match gameplay and lifecycle are not certified by these automated checks.
Full human campaign/camera switching and four-controller acceptance remain 1.0
work. Existing camera/stadium constraints are retained; netplay is not implemented.
No untested macOS or separate SteamOS binary is published.

See [controls](CONTROLS.md), [tactical cameras](TACTICAL_CAMERA_1_0_VALIDATION.md)
and [previous release validation](RELEASE_VALIDATION_0_5_0_BETA_2.md).
