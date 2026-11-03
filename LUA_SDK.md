# Generate Lua SDK output locally

The offline Python exporter consumes the C++ generator's reflection graph at
`IDA/unreal.json`. It does not read the process, replace the native generator or
infer a complete SDK from an executable file.

From this repository root:

```powershell
$dump = 'D:\PrivateResearch\ChroniclesSDK\Chronicles_YYYY-MM-DD'
python generate_lua_sdk.py $dump
```

Default output is `<dump>/LuaSDK`. Use `--output PATH` for another destination.
The manifest records the input hash and generated file identities. Its update-safe
writer refuses edited/unmanaged output and removes only files owned by a previous
manifest. Keep this generated game-derived output outside tracked project source.

For supported combined library/release-asset output, inspect the current CLI:

```powershell
python generate_lua_sdk.py --help
python generate_lua_sdk.py $dump --online --release-assets --output out/lua-release
```

These commands prepare local generated files; they do not publish a release or grant
compatibility with another running game build. The repository distributes neither
the input reflection graph nor generated game libraries.

The synthetic Lua checks under `ArcheAgeSdkGenerator/Tests/verify_lua_sdk.py` require
a Lua executable supplied through `--lua`. Generated-type compilation and layout
checks remain separate prerequisites for trusting the underlying reflection graph.
