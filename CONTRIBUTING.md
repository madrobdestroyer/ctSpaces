# Contributing To ctSpaces

ctSpaces is a native Windows C++20 application. Read the [Developer Guide](docs/DEVELOPER_GUIDE.md) and the current [release validation report](docs/RELEASE_VALIDATION_5.3.0.10.md) before changing profile, browser-launch, icon, installer, or update behavior.

## Prerequisites

- Visual Studio with the Desktop development with C++ workload.
- The v145 C++ toolset and a current Windows SDK.
- PowerShell for release tests and the current user backup/restore path.
- Microsoft Edge, Google Chrome, or Brave for browser integration testing.

## Build And Verify

Build `ctSpaces.sln` as `Release|x64`, then run:

```powershell
& .\tests\Test-Release.ps1
```

With ctSpaces closed, run the focused GUI checks when relevant:

```powershell
& .\tests\Test-LiveIconRefresh.ps1
& .\tests\Test-AutoFetchDialogLayout.ps1
```

GUI tests must use randomly named disposable profiles and remove only the exact data they created. Never test destructive behavior against a real client profile.

## Change Rules

- Preserve persistent cookies, logins, history, bookmarks, storage, and sessions for existing standard clients.
- Keep Default and temporary cleanup completely separate from `Sites`.
- Stage destructive replacements and provide rollback where practical.
- Keep the main launcher compact; put larger management workflows in separate windows.
- Keep client tabs icon-free and preserve theme choices.
- Update `version.h` for every distributed build.
- Update user and maintainer documentation with behavior changes.
- Add focused tests in proportion to the risk of the change.

## Pull Requests

Describe the user-visible result, data-safety impact, browsers tested, display scales tested, and commands run. Include screenshots for UI changes, but do not include client browser data.

## License

This project is licensed under MPL-2.0. Distributed modifications to MPL-covered files must remain available under MPL-2.0. Contributors must have the right to submit their changes. See [LICENSE](LICENSE).
