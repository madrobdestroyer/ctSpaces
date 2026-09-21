# ctSpaces 5.3.0.4 — Manual inactive-client cleanup

Use **Options (gear) → Clean Up Inactive Clients...** to review clients that
have not been opened in any browser for three calendar months. Check the
acknowledgement and click **Delete Listed Clients** to permanently remove the
listed clients, all their browser data, their icons/activity records, and
verified managed Desktop shortcuts. Archived clients are included.

Open clients and unsafe/unverifiable clients are skipped. Eligibility and
browser use are freshly checked again before each deletion. Cancel is the
default action, and merely viewing the list deletes nothing. Cleanup runs
only when requested, not automatically.

This version begins deliberate client-activity tracking. Existing/imported
clients without history get a fresh three-month grace period when discovered;
old folder timestamps are not evidence of past use. Opening any browser slot
refreshes the whole-client date. A failed browser launch can conservatively
extend retention. A tracking save failure blocks that launch rather than
leaving an old cleanup date behind. Renaming keeps the activity record.

Default and temporary profiles are excluded. Separately exported backups and
unrelated shortcuts are not erased. Partial deletion/shortcut failures are
reported rather than silently treated as complete removal.

Verification: Release x64 build; compiled activity boundary/persistence tests;
isolated real-browser cleanup workflow (confirmation/cancel, active client,
changed activity after preview, archived client, complete multi-browser
removal, shortcut removal, settings pruning); existing shortcut/rename/archive
workflow; standard release gates. Real client data and the installed app are
not replaced by these tests or by producing the package.
