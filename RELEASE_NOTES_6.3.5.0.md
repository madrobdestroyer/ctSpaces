# ctSpaces 6.3.5.0 — backup restore repair

This patch fixes a restore failure that could reject a valid `.7z` backup with
“The backup failed its integrity check while being extracted” and error
`0x80004005`.

## What changed

- Temporary extraction and verification now support paths longer than the
  final client location. The temporary restore folder no longer causes a valid
  backup to fail solely because it adds characters to a file path.
- Extraction failures retain the actual Windows or decoder error and the
  archive item when available. File-creation failures are no longer described
  as checksum failures.
- An allocation failure during extraction now returns safely instead of
  terminating the callback. Existing data remains unchanged and a fresh
  restore attempt can succeed.
- Final client-folder limits remain checked before commit. Successful restores
  retain the previous client collection in the pre-restore recovery folder.

The backup format is unchanged. Existing valid backups do not need conversion
or recreation for this fix. Notes layout, browser settings and the accepted
Edge saved-session layout limitation remain unchanged from 6.3.4.0.

## Update and retry

1. Download and extract `ctSpaces6.3.5.0.zip`, then run `ctSpaces.exe` to update.
2. Close browsers using ctSpaces client profiles before choosing
   **Restore Client Data**.
3. Select the existing `.7z` backup and retry. For a OneDrive transfer, make sure
   the complete backup is available locally on the destination PC.
4. Check the restored clients before removing the original backup or the
   pre-restore recovery folder.

Restore replaces the active client collection rather than merging it. Browser
sign-ins can remain tied to the original Windows account or computer; see
[backup and privacy](https://github.com/madrobdestroyer/ctSpaces/blob/v6.3.5.0/docs/DATA_BACKUP_AND_PRIVACY.md).

## Validation

The new release build passed the release checks with zero production compiler
warnings or errors. Fresh 6.3.5.0 native checks passed 74 restore assertions,
20 background installer scenarios, 70 installer publication assertions, and
52 checks using the same unchanged valid archive that reproduced the failure.

The preceding repair checkpoint passed 59 background Core suites, 18 headless
Notes selectors / 4,852 assertions, and 716 additional client checks. The only
change to its 345 frozen inputs before the release build was `version.h`.
Those larger groups were not rerun or relabeled as fresh 6.3.5.0 runs.

The coworker's particular archive and its original producing version were not
available for direct testing. The earlier restricted archive-mutation suite
remains unrun, and this patch review used no foreground desktop interaction.
See the [release validation](https://github.com/madrobdestroyer/ctSpaces/blob/v6.3.5.0/docs/RELEASE_VALIDATION_6.3.5.0.md)
for exact scope, evidence and limitations. For all changes since the team's
6.1.1.0 baseline, see the [cumulative change list](https://github.com/madrobdestroyer/ctSpaces/blob/v6.3.5.0/docs/CHANGES_SINCE_6.1.1.0.md).

## Download verification

The Windows x64 ZIP contains exactly one root `ctSpaces.exe`.

| File | SHA-256 |
|---|---|
| ctSpaces6.3.5.0.zip | `493CF42CCC9FB0A471DAC230C0F1564A4F5499F1E6C9FF3D294DC0757784CB81` |
| ctSpaces.exe | `A953054DAF7876AE6EF6C35BF393041B9F0B956C0C055CB964FFDDDC044A1676` |
