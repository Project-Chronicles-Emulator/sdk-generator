"""Compile the complete generated SDK with MSVC and verify emitted field offsets.

Usage: python verify_sdk_compile.py G:/Dumps/ArcheAge/<dump>
Output and compiler logs are retained in <dump>/Validation/.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys

VCVARS = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat")


def offset_assertions(headers: list[Path]) -> list[str]:
    assertions = []
    # Each declaration is preceded by a reflected Class/Struct comment. Avoid
    # native helper classes, enums, bitfields (offsetof is invalid), and methods.
    type_name = None
    reflected = False
    for header in headers:
        type_name = None
        reflected = False
        for line in header.read_text(encoding="utf-8-sig").splitlines():
            if line.startswith("/// Class ") or line.startswith("/// Struct "):
                reflected = True
                type_name = None
            declaration = re.match(r"^(?:class|struct)\s+(\w+)\b", line)
            if reflected and declaration:
                type_name = declaration.group(1)
            if line.startswith("};"):
                type_name = None
                reflected = False
            if not type_name:
                continue
            member = re.search(r"\b(\w+)(?:\[[^\]]+\])*;\s*//\s*0x([0-9A-Fa-f]+)\s+\(", line)
            if member:
                name, offset = member.groups()
                assertions.append(
                    f'static_assert(offsetof({type_name}, {name}) == 0x{offset}, '
                    f'"{type_name}::{name} offset mismatch");'
                )
    return assertions


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path)
    parser.add_argument("--vcvars", type=Path, default=VCVARS)
    args = parser.parse_args()
    dump = args.dump.resolve()
    sdk = dump / "SDK"
    headers = sdk / "SDK"
    if not (sdk / "SDK.h").is_file() or not args.vcvars.is_file():
        parser.error("SDK/SDK.h or the Visual Studio vcvars64.bat was not found")
    output = dump / "Validation"
    output.mkdir(exist_ok=True)
    # Include only package files referenced by the master. Blueprint aliases
    # include their owning package and must not duplicate offset assertions.
    includes = re.findall(r'#include "SDK/([^"/]+\.h)"', (sdk / "SDK.h").read_text(encoding="utf-8-sig"))
    package_headers = [headers / name for name in includes if name not in ("BasicType.h", "EngineInternals.h")]
    assertions = offset_assertions(package_headers)
    full = output / "sdk_compile.cpp"
    full.write_text(
        '#include <cstdint>\n#include <cstddef>\n#include "SDK.h"\n' +
        '\n'.join(assertions) + '\nint main() { return 0; }\n', encoding="utf-8"
    )
    tests = [full, Path(__file__).with_name("verify_sdk_layout.cpp"),
             Path(__file__).with_name("verify_engine_internals.cpp")]
    for source in tests:
        log = output / (source.stem + ".log")
        obj = output / (source.stem + ".obj")
        exe = output / (source.stem + ".exe")
        execute = source.name == "verify_engine_internals.cpp"
        compile_only = "" if execute else "/c"
        link_output = f' /Fe:"{exe}"' if execute else ""
        command = (
            f'call "{args.vcvars}" >nul && cl /nologo /std:c++20 /EHsc {compile_only} /bigobj '
            f'/I"{sdk}" /I"{headers}" "{source}" /Fo:"{obj}"{link_output} >"{log}" 2>&1'
        )
        # Pass the command line directly: list2cmdline uses backslash-escaped
        # quotes, whereas cmd.exe requires its own outer quoting convention.
        result = subprocess.run('cmd.exe /d /s /c "' + command + '"')
        diagnostics = log.read_text(encoding="utf-8", errors="replace") if log.exists() else "Compiler did not start"
        errors = [line for line in diagnostics.splitlines() if "error " in line]
        print(f'{source.name}: {"PASS" if result.returncode == 0 else "FAIL"}; log: {log}')
        for error in errors[:25]:
            print(error)
        if result.returncode:
            return result.returncode
        if execute:
            result = subprocess.run([str(exe)])
            if result.returncode:
                print("FAIL: engine helper remote-read behavior")
                return result.returncode
    print(f"PASS: full SDK, native helper layouts, and {len(assertions)} reflected member offsets compiled")
    return 0


if __name__ == "__main__":
    sys.exit(main())
