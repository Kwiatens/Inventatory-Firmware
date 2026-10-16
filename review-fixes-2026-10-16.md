# Review fixes and build repair on main (2026-10-16)

- Slug: review-fixes-2026-10-16
- Owner: Claude (interactive session with the owner)
- Status: done (pushed to origin/main as ec3f69b, 9 commits)
- Last updated: 2026-10-16
- Branch: fix/review-fixes-2026-10-16 (from origin/main cbe3e86)
- Worktree: /home/pawel/Dokumenty/Github/Inventatory-Firmware (primary checkout)

## Files claimed
- src/storage/outbox_store.h, outbox_store.cpp (power-safe repair)
- src/app/inventatory_scan_app.cpp, inventatory_scan_app.h, inventatory_scan_app_ota.cpp (OTA listener, BLE restart)
- src/setup/ble_provisioning.cpp, ble_provisioning.h (start once per boot)
- src/net/inventatory_client.cpp, include/InventatoryScanSync.h (header limit, NUL, escaped names)
- src/display/display_renderer.cpp (device build fix)
- host-tests/tests/*, test/*, examples/gmg12864_spi_test/*, .github/workflows/firmware.yml, CONTRIBUTING.md, .gitignore, AGENTS.md, CHANGELOG.md, docs/testing.md

## Done
- Cloud branch merged into the owner's checkout and checked against it. Main now holds the cloud work, the owner's charger-LED branch, and later cloud commits.
- Main did not build on the device (display cast error). Fixed on this branch.
- Review fixes ported onto main: power-safe outbox repair, exact 1,024-byte header limit, NUL refusal in headers, escaped member names, OTA listener stop, BLE once per boot, boundary tests, CI logging gate covering include/, example rotation.
- Host suite: 98 shared + 80 host tests, 0 failures, 0 ignored. USB, OTA, Unity image and example build.

## Left
- Owner: review the pushed commits. Dropping the old stash entries and the local branches is still open.
- Board checks (docs/board-day-checklist.md). Nothing has run on hardware.
- Sanitizers: libasan is not installed on this machine. CI runs them.
- Rollback limitation after a repair is documented in CHANGELOG (known issues).

## Overlaps found (and what the user was told)
- Main had moved to cbe3e86 during the merge. The merge in the checkout was stale, so it was aborted and rebuilt on main. Backups: ~/Dokumenty/Inventatory-firmware-backup-2026-10-16/ and refs/backup/merge-state-late.

## Notes for the next agent
- Do not re-merge origin/ccr-793285a6-2t9u8g. Its commits are already on main.
- Backups dropped on 2026-10-17 with the owner's agreement: the two stash entries, refs/backup/merge-state-late and the backup folder. One unrelated old stash (on fix/review-remediation) is left.
- Remote branches not on main: origin/feature/bag-code-transport (one commit, see its own note), origin/dependabot/github_actions/actions-25c580fd63 (kept until its Code of Conduct is ported).
