"""Start one isolated ctSpaces CPU worker; never start or contact Lumen."""
from pathlib import Path
import json
import os
import subprocess
import sys

HERE = Path(__file__).resolve().parent
job = json.loads((HERE / "heart-speech-job.json").read_text(encoding="utf-8"))
assert sys.argv[1:] in (["--probe"], ["--all"])
runtime = Path(job["runtime"])
temp = HERE / "temp"
temp.mkdir(exist_ok=True)
environment = {key: os.environ[key] for key in ["SystemRoot", "WINDIR", "COMSPEC"] if key in os.environ}
environment.update({"PATH": str(runtime) + os.pathsep + "C:/Windows/System32", "TEMP": str(temp), "TMP": str(temp),
                    "PYTHONDONTWRITEBYTECODE": "1", "ONNX_PROVIDER": "CPUExecutionProvider",
                    "OMP_NUM_THREADS": "2", "OPENBLAS_NUM_THREADS": "2", "MKL_NUM_THREADS": "2", "LOG_LEVEL": "WARNING"})
paths = [str(runtime / name) for name in ["Lib", "DLLs", "python312.zip", "site-packages"]]
bootstrap = ("import sys,runpy;sys.path=" + repr(paths) + ";sys.dont_write_bytecode=True;"
             "sys.argv=" + repr([str(HERE / "heart-worker.py"), *sys.argv[1:]]) + ";"
             "runpy.run_path(sys.argv[0],run_name='__main__')")
log_path = HERE / ("heart-worker-all.log" if sys.argv[1] == "--all" else "heart-worker-probe.log")
with log_path.open("x", encoding="utf-8") as log:
    process = subprocess.Popen([job["python"], "-I", "-S", "-B", "-u", "-c", bootstrap],
                               cwd=HERE, env=environment,
                               creationflags=subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding="utf-8", errors="replace")
    for line in process.stdout:
        log.write(line)
        log.flush()
        print(line, end="", flush=True)
    result = process.wait()
raise SystemExit(result)
