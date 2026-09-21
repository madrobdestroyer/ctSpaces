# ctSpaces 6.0.4.0

## Responsive cleanup discovery

Delete Multiple Clients and inactive cleanup now perform intensive profile
discovery and recursive validation away from the main window. The cleanup
workflow provides themed progress and Cancel feedback while discovery runs.
The existing validation rules and the fresh checks immediately before an
actual delete remain in force.

Testing reproduced a main window stall while inspecting local clients in the
previous release. The updated scan keeps the interface responsive and allows
cancellation before the selection preview. Cancelling this inspection does
not delete clients or open a partially scanned selection list.

The exact cause on the reporting colleague's computer has not been confirmed.
This update addresses the reproduced scan stall; it does not add cancellation
to an actual deletion after confirmation.

Client and browser profile layout, browser behavior, the full guide, and the
20 step Quick Tour remain unchanged. The normal runtime ZIP remains app only
and contains exactly `ctSpaces.exe`.
