"""Start installed local engines for the launcher; never download weights."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import re
import urllib.error
import urllib.request


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def endpoint(engine):
    if engine == "llama":
        return "http://127.0.0.1:8080"
    if engine != "ollama":
        raise ValueError("Unsupported engine")
    url = os.environ.get("OLLAMA_HOST", "http://127.0.0.1:11434")
    match = re.fullmatch(r"http://(127\.0\.0\.1|\[::1\]):([0-9]{1,5})", url)
    if not match or not 1 <= int(match.group(2)) <= 65535:
        raise ValueError("OLLAMA_HOST must specify a local HTTP address and port")
    return url


def available(engine):
    url = endpoint(engine) + ("/api/tags" if engine == "ollama" else "/v1/models")
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    try:
        with opener.open(url, timeout=0.5) as response:
            body = response.read(65537)
            if len(body) > 65536:
                return False
            result = json.loads(body)
            return isinstance(result, dict) and isinstance(
                result.get("models" if engine == "ollama" else "data"), list)
    except (OSError, ValueError, urllib.error.URLError):
        return False


def command(engine, gguf=None):
    if engine not in ("ollama", "llama"):
        raise ValueError("Unsupported engine")
    executable = shutil.which("ollama" if engine == "ollama" else "llama-server")
    if not executable:
        raise ValueError(f"Engine is not installed: {engine}")
    env = os.environ.copy()
    if engine == "ollama":
        env["OLLAMA_HOST"] = endpoint(engine).removeprefix("http://")
        return [executable, "serve"], env
    if not gguf:
        raise ValueError("llama.cpp requires --gguf /path/model.gguf")
    model = Path(gguf).expanduser().resolve(strict=True)
    if not model.is_file() or model.suffix.lower() != ".gguf":
        raise ValueError("Select an existing GGUF file")
    return [executable, "--model", str(model), "--host", "127.0.0.1",
            "--port", "8080", "--ctx-size", "4096", "--parallel", "1"], env


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def start(engine="ollama", gguf=None, timeout=30):
    if engine == "none" or available(engine):
        return None
    args, env = command(engine, gguf)
    process = subprocess.Popen(args, env=env, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and process.poll() is None:
            if available(engine):
                print(f"Started the local {engine} server.", flush=True)
                return process
            time.sleep(0.2)
        raise RuntimeError(f"Server {engine} did not become ready within {timeout} s")
    except BaseException:
        stop(process)
        raise
