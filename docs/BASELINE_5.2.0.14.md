# Supplied colleague baseline: 5.2.0.14

This note records the exact artifact supplied as the colleagues' last-shipped
baseline for upgrade guidance. It is evidence metadata only; the executable was
not executed and no runtime comparison was performed.

- Source ZIP filename: `ctSpacesV5.2.0.14.zip`
- ZIP contents: exactly one file, `ctSpaces.exe`
- ZIP SHA-256: `C84556D416873E0256D6F93CC19E7586706A6759F58C6B515DAE5CF71DC38500`
- Extracted executable size: `5,091,840` bytes
- Extracted executable SHA-256: `4FB86BCF08E23FBCC7C57EE58D82C9EE26455FEDC443A4F89F98CB3798956DBF`
- Internal file/product version reported by the supplied executable: `5.2.0.2`

The filename supplied by the colleague (`5.2.0.14`) and the embedded version
metadata differ. The supplied bytes and hashes are authoritative for this
baseline record; the internal metadata is reported for transparency, not used
to relabel the artifact.

Static inspection of the supplied executable found embedded labels or messages
for the client chooser, Open/Show, opening the profile folder, Temporary,
up to eight pins, Edge/Chrome/Brave choices, generic
restore-last-session behavior, profile icon commands, Edit Default/save-for-
future behavior, backup/restore, Vacuum, Reset, and single profile deletion.
Strong static inferences are that the baseline lacks the current per-browser
slot/menu schema and the capture/drag API pattern used for reordering; the
post-5.2 changelog independently records later reordering additions. These are
static inferences, not runtime proof.

The post-baseline coverage map in
[`GUIDED_WALKTHROUGH.md`](GUIDED_WALKTHROUGH.md) uses release notes, changelog
history, current source labels, and static comparisons where available. A
static absence of a string or implementation pattern is not runtime proof that
the old executable lacked a behavior; imports/menu strings can be incomplete
evidence. The baseline ZIP/EXE and private review
captures are not part of the normal app-only distribution.
