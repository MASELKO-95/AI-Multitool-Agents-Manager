#!/usr/bin/env python3
"""Build and run the native AI Multitool Agents Manager window."""

from pathlib import Path
import shutil
import subprocess
import sys
import argparse
from tools.local_engines import start, stop


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--engine", choices=("ollama", "llama", "none"), default="ollama")
    parser.add_argument("--gguf")
    options, gui_args = parser.parse_known_args()
    root = Path(__file__).resolve().parent
    build = root / "build"
    cmake = shutil.which("cmake")
    if cmake is None:
        print("CMake not found. Install CMake and the dependencies listed in README.md.",
              file=sys.stderr)
        return 1

    owned_server = None
    try:
        subprocess.run(
            [cmake, "-S", str(root), "-B", str(build),
             "-DAIMT_BUILD_GUI=ON", "-DAIMT_BUILD_LOCAL_SERVICES=ON"],
            cwd=root, check=True,
        )
        subprocess.run(
            [cmake, "--build", str(build), "--target", "ai-multitool-agents-manager-gui",
             "--parallel", "2"],
            cwd=root, check=True,
        )
        if "--help" not in gui_args:
            try:
                owned_server = start(options.engine, options.gguf)
            except (ValueError, OSError, RuntimeError) as exc:
                print(f"Local engine: {exc}. The window remains available.", file=sys.stderr)
        return subprocess.run(
            [str(build / "ai-multitool-agents-manager-gui"), *gui_args], cwd=root,
        ).returncode
    except subprocess.CalledProcessError as exc:
        print("Could not build the application. Check the error above and the "
              "SDL2, libcurl and json-c dependencies listed in README.md.",
              file=sys.stderr)
        return exc.returncode
    except OSError as exc:
        print(f"Could not start the application: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    finally:
        stop(owned_server)


if __name__ == "__main__":
    sys.exit(main())
