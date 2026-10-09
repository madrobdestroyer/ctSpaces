# Release validation: 6.2.3.0

Validated October 9, 2026, using the running application and disposable client data under build/ni-623. No live installation or client data was changed.

## Interaction and visual verification

The previous release's programmatic layout checks did not catch the reported interaction defects. This correction was exercised with Computer Use mouse clicks, keyboard input, actual corner drags, and title-bar double-clicks.

- Reproduced the old resize problem: incomplete tabs, black rectangles, and controls painting at temporary sizes. After batching layout, retaining collapsed combo dimensions, clipping children, composing painting, and completing redraws, actual corner drags and maximize/restore rendered cleanly.
- Reviewed the final executable in System Dark, Dark Gothic, and Marine. Theme palettes and slim tabs are retained. The native outer frame is suppressed and only the subtle themed border remains. Both footer lines are hidden; the document reaches the bottom margin.
- Plus creates Untitled immediately with editor focus. A second addition creates Untitled 2 without prompting. Keyboard input was entered into the new tab. Two- and three-tab layouts fit without unnecessary scroll arrows.
- Opened the highlight menu with the mouse, applied green to selected text, observed the green indicator and dark text, and verified the highlight after restarting. No highlight removed it correctly. Checked a checklist item after resizing; completion and strikethrough remained correct.
- A locked disposable notebook caused the visible title to report Could not save. Closing displayed the error and kept the draft open. Unlocking and switching tabs saved successfully and cleared the error. Formatted note content survived reopening.
- Restored saved 900 x 540 dimensions across process restart. Final corner-drag checks also covered populated notes in Gothic and Marine, including a 1100 x 750 starting window. Maximize and restore were exercised on the final executable.

Final screenshots: build/notes-interaction-gothic.png and build/notes-interaction-marine.png. Interaction observations are retained in the task's Computer Use outputs. Verification was on the current Windows desktop at 100% DPI; Windows 10, additional DPI scales, other machines, and browser-link routing were not requalified for this patch.

## Build and package verification

- Release x64 build: passed without warnings or errors.
- Client Notes compiled unit suite: passed, including rich-text round trips and storage safety.
- Guided Walkthrough unit suite: passed after guidance changed.
- Release gates: passed on the final executable.
- Microsoft Defender: no threats found in the final executable.
- Updated the existing native UI regression harness for immediate tab creation and hidden footers; parsed successfully. That broad harness was not rerun in this pass. Interactive checks above supplement the earlier regression results, rather than claiming a new full-suite run.
- Package contains exactly one root ctSpaces.exe. Its bytes match both the tested release executable and the isolated executable used for the final interaction checks. All 21 previous ZIP hashes are unchanged.

Evidence: build/notes-interaction-build.log, notes-interaction-unit.log, notes-interaction-guide.log, notes-interaction-release.log, notes-interaction-defender.log, and notes-interaction-package.log.

Executable SHA256: 5A6DBB93DAD9AAD02F5470D31E2714D6155D99951AE40D3A39A8F42BEF6996D8

ZIP SHA256: D3C7D8AD3E543CCEF4469C2715264C323C5DB5650D4154D006DFFD47253C6A5C
