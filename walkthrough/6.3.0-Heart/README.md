# Real application walkthroughs with Kokoro Heart

This ctSpaces-owned workspace contains two narrated native application
recordings and a local player with clickable chapters. The folder began while
6.3.0.0 was under review; final recordings use the 6.3.1.0 release executable.
Generated media, disposable profiles and failure diagnostics are not committed.

`Record-Walkthrough.ps1` performs real mouse clicks and keyboard input in copied
QA launchers. Its fixtures are short project-owned paths below `build`, with
fictional clients seeded by `CaptureNative --seed-demo`. Process metadata checks
identify only browsers launched against those fixtures. Cleanup never targets
an installed launcher or ordinary browser profile. Run GUI work serially.

`CaptureNative.cpp` captures changing live pixels from explicitly allowed
process windows, including their popups. It captures no microphone, unrelated
windows or whole desktop. Native Media Foundation encodes H.264 using actual
wall-clock sample timestamps. It does not assemble still screenshots into scenes.
The Microsoft references are [Sink Writer](https://learn.microsoft.com/en-us/windows/win32/medfound/using-the-sink-writer-to-encode-video)
and [image stride](https://learn.microsoft.com/en-us/windows/win32/medfound/image-stride).

## Read-only reuse of the existing Heart setup

The reference inspected was HomeLabAtlas's completed `walkthrough/0.23.0-Heart`
worker, launcher, reuse notes, speech job and Windows media tools. Its completed
files are not overwritten. Lumen is never launched, contacted or interrupted.

The local worker borrows explicit paths to Lumen's existing Python runtime,
Kokoro model/voices, and `espeakng_loader` library/data. The standard paths were
available; Windows Store virtualization alternatives were also verified. The
runtime contained kokoro-onnx 0.6.1 and ONNX Runtime 1.29.0. No packages or model
files were installed or downloaded.

The worker runs isolated (`-I -S -B`), uses CPUExecutionProvider with two intra-op
threads and one inter-op thread, and creates one engine reused across all cues.
Voice is `af_heart`, language `en-us`, speed `1.0`. Output is 24 kHz, mono, 16-bit
PCM WAV. The short probe was validated before the fourteen full narration cues.
The local receipts contain actual durations, hashes, RMS/clipping checks and
package/provider information. A read-only hash baseline covers borrowed model,
voice, eSpeak and Atlas files; assembly verifies it again.

Python's platform probe encountered this host's unavailable WMI provider. The
isolated worker supplies native Windows version information before importing
ONNX, preventing a shell fallback. This changes no shared runtime or settings.

## Reproduce locally

1. Copy `heart-speech-job.example.json` to `heart-speech-job.json`, check all
   paths against the actual runtime and assets, and choose a fresh output folder.
   Keep shared resources read-only. Prepare cues with `prepare-speech.py --prepare`.
2. Run `run-heart-worker.py --probe`, inspect its receipt/audio, then run `--all`.
   These scripts refuse to overwrite their existing generation logs.
3. Run `prepare-speech.py --time` to use measured WAV durations for scene slots.
   Build `CaptureNative.vcxproj` with Release x64. Build the actual app first.
4. Run `Record-Walkthrough.ps1 -Video ctSpaces` and then `-Video Notes`, from
   the repository root. Optional `-FastReview` executes all interactions without
   waiting through narration slots; `-Smoke` checks only the first chapter.
   Neither option publishes a final recording pointer.
5. Run `assemble.py` after both full takes pass. It joins speech with explicit
   silence, exports real video clips plus narration through the adapted Atlas
   Windows media route, decodes chapter frames and validates media properties.
   Export invokes a script block in Windows PowerShell 5 without changing the
   machine's execution policy. The shared-resource baseline must be available.

Deliverables are `output/index.html`, two `*-Walkthrough.mp4` files, VTT/SRT
captions, transcripts and inspection/delivery receipts. The player embeds its
chapter/caption data and needs no network requests, server or framework. Keep
its MP4 files beside it. Open it in a browser; chapter buttons seek within the
selected recording. The MP4 files also play independently. Caption groups use
the measured narration duration; they are not phoneme-level forced alignment.
