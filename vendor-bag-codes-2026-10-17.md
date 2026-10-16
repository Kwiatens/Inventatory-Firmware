# Vendor bag codes ported to main (2026-10-17)

- Slug: vendor-bag-codes-2026-10-17
- Owner: Claude (interactive session with the owner)
- Status: active (committed locally on feat/vendor-bag-codes, not pushed)
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
- Owner: push to main (fast-forward) or keep the branch.
- Quantity-screen lookup still sends the manufacturer number. The desktop matches its machine code, DigiKey part number or SKU, so a known DigiKey part can show "Not in inventory". Switching the lookup to the DigiKey part number is a small follow-up.
- Board check with real DigiKey and TME bags.

## Overlaps found
- origin/feature/bag-code-transport: the old branch. Its commit is ported here. Delete it once this is on main.
- Desktop repo: its CHANGELOG already requires bag identifiers from the firmware. Older firmware is refused.

## Notes for the next agent
- Do not merge origin/feature/bag-code-transport. Its base is old.
