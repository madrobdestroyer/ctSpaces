# Release validation: 6.3.1.0

Validated October 13, 2026. This release preserves the approved Notes Inspector
design and incorporates the full-app adversarial review. See
[the coverage matrix and findings](FULL_APP_STRESS_REVIEW_6.3.1.0.md).

- Twelve core suites passed, including notebook storage, configuration,
  starter sanitizers, archive/rename/shortcut transactions and 21 isolated
  production installer scenarios.
- Sixteen integration suites passed, including real same-client Edge/Chrome,
  browser close/restore, two live icon changes, coordinated shutdown, launcher
  filtering/cancellation, cleanup faults and 1,000 theme-selection changes.
- The final native Notes suite passed 531 checks after fixing inspector focus,
  dropdown keyboard handling, caption click targets, delayed browser closure
  and stale code-table border pixels. Evidence:
  `build/full-app-stress/dropdown-key-native.log`.
- The 6.3.1.0 Release x64 build completed without compiler warnings or errors:
  `build/full-app-stress/release-631-build.log`.
- Release gates passed on the final executable:
  `build/full-app-stress/release-631-gates.log`.
- Microsoft Defender found no threats in that executable:
  `build/full-app-stress/release-631-defender.log`.
- Packaging verified exactly one root `ctSpaces.exe` with matching bytes:
  `build/full-app-stress/release-631-package.log`. Prior root release ZIP hashes
  match `build/full-app-stress/release-631-prior-packages.json`; earlier review
  packages remain intact.
- Two actual-input recordings use this exact executable: ctSpaces (2:19,
  six chapters) and Client Notes (3:52, eight chapters). The footage includes
  real Edge/Chrome sessions, direct template/snippet editing, code/work-log
  insertion, Trash restoration and an actual HTML handover export. Decoded
  chapter frames were visually reviewed, including the complete code-box border.
- The local chapter player passed video switching, seeking, captions and narrow
  viewport checks. Actual MP4 audio was decoded and verified as non-silent AAC.
  Kokoro narration uses `af_heart`, `en-us`, speed 1.0; the original Lumen/Atlas
  resource hashes remain unchanged. Local evidence is in
  `walkthrough/6.3.0-Heart/output/player-check.json`, media inspection files and
  the delivery receipt. Generated media are delivered separately from the runtime ZIP.

The user-visible Windows version increased once, from 6.3.0.0 to 6.3.1.0,
for this deliberate release. Intermediate fixes retained the same version.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ctSpaces.exe | 6363648 | C4E04C9598E9DE065F929B9E3A5868AECB52B85E1E04FDC25A5D4289F1FBCBE3 |
| ctSpaces6.3.1.0.zip | 2627676 | 7F06245F14F8F090CEAD501F78D34FF9B3B57E8760061832A6C596DD663983A3 |

Fixtures and recorded workflows use fictional client data. No live client
profiles or installed launcher were modified. The detailed review distinguishes
native isolated assertions, actual browser/UI coverage and synthetic DPI tests.
Brave/Firefox live coverage, deployment over a real installed copy, and every
possible display/timing configuration are not established by these results.
