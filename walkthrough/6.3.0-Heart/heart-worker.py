"""ctSpaces-owned offline CPU speech process borrowing explicit read-only Kokoro files."""
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import sys
import time
import wave

HERE = Path(__file__).resolve().parent
job = json.loads((HERE / "heart-speech-job.json").read_text(encoding="utf-8"))
out = Path(job["outputDirectory"])
assert out.parent == HERE
denied = []

def audit(event, args):
    if event in {"socket.connect", "socket.getaddrinfo", "socket.bind", "subprocess.Popen", "os.system"}:
        denied.append(event)
        raise RuntimeError("Offline Heart worker denies network and external commands: " + event)
    if event == "open":
        path, mode, flags = args
        write = isinstance(mode, str) and any(letter in mode for letter in "wax+")
        write = write or isinstance(flags, int) and bool(flags & (os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_TRUNC | os.O_APPEND))
        if write and isinstance(path, (str, bytes, os.PathLike)):
            resolved = Path(os.fsdecode(path)).resolve()
            if not resolved.is_relative_to(HERE):
                raise RuntimeError("Writing outside ctSpaces Heart output is prohibited")
    if event in {"os.remove", "os.mkdir", "os.rmdir", "os.rename"}:
        for candidate in args[:2] if event == "os.rename" else args[:1]:
            if isinstance(candidate, (str, bytes, os.PathLike)) and not Path(os.fsdecode(candidate)).resolve().is_relative_to(HERE):
                raise RuntimeError("Filesystem mutation outside ctSpaces Heart output is prohibited")

sys.addaudithook(audit)
# This PC's WMI provider is unavailable. Python's platform.system() otherwise
# tries a shell fallback while importing ONNX. Use the native, read-only Windows
# version API in this isolated worker, keeping the offline/no-subprocess guard.
import platform
windows = sys.getwindowsversion()
platform._uname_cache = platform.uname_result(
    "Windows", "", f"{windows.major}.{windows.minor}",
    f"{windows.major}.{windows.minor}.{windows.build}", "AMD64")
import numpy as np
import onnxruntime as ort
from kokoro_onnx import Kokoro
from kokoro_onnx.config import EspeakConfig

ort.disable_telemetry_events()
options = ort.SessionOptions()
options.intra_op_num_threads = 2
options.inter_op_num_threads = 1
options.add_session_config_entry("session.intra_op.allow_spinning", "0")
options.add_session_config_entry("session.inter_op.allow_spinning", "0")
options.enable_profiling = False
session = ort.InferenceSession(job["model"], sess_options=options, providers=["CPUExecutionProvider"])
engine = Kokoro.from_session(session, job["voices"],
                            espeak_config=EspeakConfig(lib_path=job["espeakLibrary"], data_path=job["espeakData"]))
assert "af_heart" in engine.get_voices()
probe = sys.argv[1:] == ["--probe"]
assert probe or sys.argv[1:] == ["--all"]
cues = [{"id": "probe-heart", "text": "Welcome to C T Spaces. Keep each client separate, and keep your notes with the client.",
         "path": str(out / "probe-heart.wav")}] if probe else job["cues"]
results = []
start = time.perf_counter()
for index, cue in enumerate(cues, 1):
    destination = Path(cue["path"])
    assert destination.parent == out and not destination.exists(), destination
    samples, sample_rate = engine.create(cue["text"], voice="af_heart", speed=1.0, lang="en-us")
    samples = np.asarray(samples, dtype=np.float32).ravel()
    assert sample_rate == 24000 and len(samples) > 2400 and np.isfinite(samples).all()
    peak = float(np.max(np.abs(samples)))
    gain = min(1.0, .97 / peak) if peak else 1.0
    pcm = np.rint(np.clip(samples * gain, -.97, .97) * 32767).astype("<i2")
    rms = float(np.sqrt(np.mean(pcm.astype(np.float64) ** 2)))
    assert rms > 100 and not np.any(np.abs(pcm.astype(np.int32)) >= 32767)
    with wave.open(str(destination), "wb") as target:
        target.setnchannels(1)
        target.setsampwidth(2)
        target.setframerate(24000)
        target.writeframes(pcm.tobytes())
    with destination.open("rb") as source:
        file_hash = hashlib.file_digest(source, "sha256").hexdigest()
    results.append({"id": cue["id"], "path": str(destination), "sha256": file_hash,
                    "durationSeconds": len(samples) / sample_rate, "rms": rms, "peakBeforeGain": peak,
                    "gain": gain, "clippedSamples": 0})
    print(json.dumps({"cue": cue["id"], "number": index, "total": len(cues),
                      "seconds": len(samples) / sample_rate}), flush=True)
receipt = {"status": "passed", "voice": "af_heart", "voiceSpeed": 1.0, "lang": "en-us",
           "format": "24kHz mono16bitPCM", "providers": session.get_providers(), "cpuThreads": 2,
           "packages": {name: importlib.metadata.version(name) for name in ["kokoro-onnx", "onnxruntime", "numpy", "phonemizer", "espeakng-loader"]},
           "cues": results, "audioSeconds": sum(cue["durationSeconds"] for cue in results),
           "elapsedSeconds": time.perf_counter() - start, "deniedExternalOperations": denied,
           "networkOrServiceContact": False, "audioDeviceOpened": False,
           "copies": [], "installs": [], "LumenAppLaunchedOrChanged": False}
receipt_path = HERE / ("heart-probe-receipt.json" if probe else "heart-speech-receipt.json")
assert not receipt_path.exists()
receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
print(json.dumps({"status": "passed", "voice": "af_heart", "cues": len(results),
                  "seconds": receipt["audioSeconds"], "receipt": str(receipt_path)}), flush=True)
