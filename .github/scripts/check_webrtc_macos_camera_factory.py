#!/usr/bin/env python3
import re
import subprocess
import sys
import tempfile
from pathlib import Path

TARGETS = [
    "__ZN6webrtc19VideoCaptureFactory16CreateDeviceInfoEv",
    "__ZN6webrtc19VideoCaptureFactory16CreateDeviceInfoEPNS_19VideoCaptureOptionsE",
    "__ZN6webrtc19VideoCaptureFactory6CreateEPKc",
    "__ZN6webrtc19VideoCaptureFactory6CreateEPNS_19VideoCaptureOptionsEPKc",
]


def run(*args, cwd=None):
    return subprocess.run(args, cwd=cwd, check=True, capture_output=True, text=True)


def parse_functions(asm_text: str):
    functions = {}
    current = None
    for line in asm_text.splitlines():
        if line.endswith(":") and line.startswith("__ZN"):
            current = line[:-1]
            functions[current] = []
            continue
        if current is None:
            continue
        stripped = line.strip()
        if stripped:
            functions[current].append(stripped)
    return functions


def normalize_inst(line: str) -> str:
    line = line.strip()
    line = re.sub(r"^[0-9a-f]+\t", "", line)
    return line.strip()


def is_stub_body(body_lines):
    inst = [normalize_inst(line) for line in body_lines if line]
    if len(inst) < 2:
        return False
    first = inst[0]
    second = inst[1]
    if re.match(r"^mov\s+x0,\s*#0x0$", first) and second == "ret":
        return True
    if re.match(r"^str\s+xzr,\s*\[x8\]$", first) and second == "ret":
        return True
    return False


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_webrtc_macos_camera_factory.py <libwebrtc.a>", file=sys.stderr)
        return 2

    archive = Path(sys.argv[1])
    if not archive.exists():
        print(f"libwebrtc archive not found: {archive}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="webrtc-factory-check-") as tmp:
        tmpdir = Path(tmp)
        run("ar", "-x", str(archive), "video_capture_factory.o", cwd=tmpdir)
        obj = tmpdir / "video_capture_factory.o"
        if not obj.exists():
            print("video_capture_factory.o not found in archive", file=sys.stderr)
            return 1


        nm_output = run("nm", "-gU", str(archive)).stdout
        if "__ZN6webrtc11FieldTrials6Create" not in nm_output:
            print(
                "ERROR: libwebrtc is missing webrtc::FieldTrials::Create, incompatible with current wrtc build.",
                file=sys.stderr,
            )
            return 1

        asm = run("otool", "-tvV", str(obj)).stdout
        functions = parse_functions(asm)

        missing = [name for name in TARGETS if name not in functions]
        if missing:
            print("missing expected VideoCaptureFactory symbols:", file=sys.stderr)
            for item in missing:
                print(f"  - {item}", file=sys.stderr)
            return 1

        stubbed = [name for name in TARGETS if is_stub_body(functions[name])]
        if len(stubbed) == len(TARGETS):
            print("ERROR: all macOS VideoCaptureFactory entry points are stubbed to null.", file=sys.stderr)
            print("This libwebrtc artifact cannot provide native camera capture via VideoCaptureFactory.", file=sys.stderr)
            return 1

        print("OK: VideoCaptureFactory has non-stub camera entry points.")
        if stubbed:
            print("WARNING: some factory entry points are stubbed:")
            for item in stubbed:
                print(f"  - {item}")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
