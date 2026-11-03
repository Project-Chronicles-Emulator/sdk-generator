<h1 align="center">Project Chronicles · SDK Generator</h1>

<p align="center">Build the generator. Inspect your compatible local client. Generate your own SDK.</p>

<p align="center"><a href="https://github.com/Project-Chronicles-Emulator/game-server">Project home</a> · <a href="https://github.com/Project-Chronicles-Emulator/asset-extractor">Asset extractor</a> · <a href="OFFSETS.md">Build-specific layouts</a> · <a href="LUA_SDK.md">Lua output</a></p>

Windows C++20 reflection SDK generator derived from
[Spuckwaffel/UEDumper](https://github.com/Spuckwaffel/UEDumper), adapted for the
researched ArcheAge Chronicles client. The original MIT license and upstream
attribution are preserved in [LICENSE](LICENSE) and [upstream documentation](docs/UPSTREAM.md).

**Source project only:** no game executable, memory dump, reflection snapshot,
generated game SDK, archive, key, driver or bridge executable is distributed here.
The bundled tool UI source/resources are part of the generator project.

## Build

Requires Windows x64, Visual Studio C++ tools with Windows SDK, MSBuild and C++20.
The solution targets the VS 2022 `v143` toolset by default:

```powershell
pwsh -File build.ps1
```

For Visual Studio 2026 with its installed `v145` toolset:

```powershell
pwsh -File build.ps1 -PlatformToolset v145
```

Output: `x64/Release/ArcheAgeSdkGenerator.exe`. The ImGui, JSON and other required
source components are already present; no private sibling source checkout is needed.
The existing upstream project has compiler warnings; a successful build is not proof
of a valid SDK for a new client build.

## Generate your own SDK

Start a compatible local client and identify its process ID. Use a project label
without path separators. Run from this repository root:

```powershell
$targetProcessId = 12345 # Replace with your local client PID.
$env:ARCHEAGE_SDK_MEMORY_MODE = 'usermode'
.\x64\Release\ArcheAgeSdkGenerator.exe --headless --pid $targetProcessId --project Chronicles
```

The GUI can also be opened without headless arguments. The default memory backend
uses ordinary process access; it cannot promise access to a protected process.
Offsets/signatures and parser layouts are build-specific: read [OFFSETS.md](OFFSETS.md)
and the configuration under `ArcheAgeSdkGenerator/Engine/Userdefined/` before
supporting another build. GWorld remains optional/unresolved for this researched setup.

Output goes to dated directories below ignored **`out/dumps/`**, with the existing
collision-avoidance behavior. To select your own output root:

```powershell
$env:ARCHEAGE_SDK_OUTPUT_ROOT = 'D:\PrivateResearch\ChroniclesSDK'
```

The console's `Project saved to` path is authoritative. A successful generator exit
alone does not establish type/layout correctness; run the checks below.

## Optional memory backend

The source retains its existing optional NDJSON bridge integration. Users who already
have a compatible bridge can configure its full executable path with
`ARCHEAGE_SDK_BRIDGE_PATH` and explicitly select `ARCHEAGE_SDK_MEMORY_MODE=kernel`.
No bridge or kernel driver is included, installed or supplied by this repository.
Explicit kernel selection retains its no-usermode-fallback policy.

The optional `ARCHEAGE_SDK_CAPTURE_BASE` / `ARCHEAGE_SDK_CAPTURE_SIZE` pair redirects
signature reads to a matching in-process image capture. Most users should leave
them unset. Values belong to the exact running process and must not be copied from
another session or build. An executable file alone is not a reflection snapshot.

## Validate and export

Use your own generated output path:

```powershell
$dump = 'D:\PrivateResearch\ChroniclesSDK\Chronicles_YYYY-MM-DD'
python ArcheAgeSdkGenerator/Tests/verify_sdk_compile.py $dump
python ArcheAgeSdkGenerator/Tests/validate_unreal_ida_bundle.py "$dump/IDA"
python generate_lua_sdk.py $dump
```

The compile audit needs the documented MSVC environment. Additional checks and their
prerequisites are described in [Tests/README.md](ArcheAgeSdkGenerator/Tests/README.md).
`--cache FILE` regenerates a saved reflection snapshot into a fresh dated output;
it still requires the appropriate running process and does not discover newly loaded types.

The offline Lua generator consumes `IDA/unreal.json`; see [LUA_SDK.md](LUA_SDK.md).
Generated SDKs, metadata exports and captures remain local and ignored.

## Contribute

Keep parser changes tied to a specific build and evidence. Preserve upstream notices,
add meaningful layout/ordering checks and distinguish compile tests from observed
live generation. Do not submit game files or generated game SDK output. Report the
source commit, build identity, reproduction and redacted validation result.

Publication validation: standalone Release x64 build with `v145`, offline policy
checks and invalid-cache rejection. No new live client dump is claimed by publishing
this source project.

## Preview downloads and reproducible releases

The **Actions → Release preview** workflow builds a Windows x64 package from a
clean checkout and creates a draft prerelease with its SHA-256 checksum. Choose a
new `vMAJOR.MINOR.PATCH-preview.NUMBER` tag when dispatching it. A failed build does
not create a release. Published previews appear on this repository's Releases page.

Packages contain the application and selected support files only. They include no
client installation, extracted game data, archive keys or credentials. Keep the whole
package together; follow the setup instructions above before starting it. Windows
C++ applications require the Microsoft Visual C++ x64 runtime; desktop interfaces
also require Microsoft Edge WebView2 Runtime. The extractor package requires .NET 10.
These experimental previews do not establish compatibility with another client build.
