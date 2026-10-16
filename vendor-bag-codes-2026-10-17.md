# Vendor bag codes ported to main (2026-10-17)

- Slug: vendor-bag-codes-2026-10-17
- Owner: Claude (interactive session with the owner)
- Status: done (on main as 0582f53 and f6b637f; branches deleted)
- Last updated: 2026-10-17
- Branch: feat/vendor-bag-codes (from origin/main 7683704)
- Worktree: /home/pawel/Dokumenty/Github/Inventatory-Firmware (primary checkout)

## Files claimed
- include/InventatoryScanCore.h (bag helpers)
- src/app/inventatory_scan_app.h, src/app/inventatory_scan_app_input.cpp (scan handling)
- test/test_inventatory_scan_core.cpp, CHANGELOG.md

## Done
- Ported origin/feature/bag-code-transport by hand. That branch was based on an old main and would have reverted newer work.
- TME label: the product-page requirement is dropped, to match the desktop (which accepts a label without it).
- Host suite 99 shared and 80 host tests pass. USB, OTA and Unity image build.

## Left
- Quantity lookup for DigiKey bags uses the DigiKey part number (f6b637f). TME bags still use the manufacturer number.
- Board check with real DigiKey and TME bags.

## Overlaps found
- origin/feature/bag-code-transport: the old branch. Its commit is ported here. Delete it once this is on main.
- Desktop repo: its CHANGELOG already requires bag identifiers from the firmware. Older firmware is refused.

## Notes for the next agent
- Do not merge origin/feature/bag-code-transport. Its base is old.
