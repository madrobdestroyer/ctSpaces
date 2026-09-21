# Bundled 7-Zip Library

ctSpaces links a narrow, static 7-Zip library for its in-process starter and
all-client backup compression, inspection, integrity-test, and extraction paths.

- Upstream: `https://github.com/ip7z/7zip`
- Tag: `26.02`
- Commit: `f9d78aff31a5f2521ae7ddbdc97c4a8855808959`
- Upstream release date: 2026-06-25

The Visual Studio project is `Format7z.vcxproj`. It compiles only the 7z format
handler and the codecs it requires from `..\3p\7zip`; it does not build the full
7-Zip application or every archive-format handler.

When updating the dependency, replace the checkout in `3p\7zip`, update this
file and the expected version in `tests\Test-Release.ps1`, then rebuild
`Release|x64` and run the release and archive round-trip checks.
