# SDK Verification

Run after a Release headless dump (read the output path from `Project saved to`):

```powershell
python ArcheAgeSdkGenerator/Tests/verify_sdk_compile.py '<your-generated-dump>'
./ArcheAgeSdkGenerator/Tests/verify_sdk_types.ps1 -Dump '<your-generated-dump>'
python ArcheAgeSdkGenerator/Tests/validate_unreal_ida_bundle.py '<your-generated-dump>/IDA'
python ArcheAgeSdkGenerator/Tests/compare_sdk_dumps.py --new '<your-generated-dump>'
```

`verify_access_policy.cpp` checks explicit kernel selection through
`ARCHEAGE_SDK_MEMORY_MODE=kernel`, the unchanged usermode default, invalid-mode
rejection and no usermode fallback for kernel requests. Its read-chunk bound
keeps Base64 bridge replies below the existing 64 KiB NDJSON reader limit.

`verify_enum_array.cpp` exercises the actual enum-array reader with a local
fixture, including negative and 64-bit values, zero counts, and null data.
The reader must construct vector elements before filling them; reserving
capacity alone makes every enum appear empty to the package generator.

`verify_outer_chain.cpp` exercises the generator's object-chain guard with a
valid chain, a cycle, an invalid pointer, and the maximum depth. Run it before
the headless dump when changing `UObject::getFullName` or class traversal.

`verify_package_name.cpp` checks package extraction, including an empty name
from an object that could not be resolved in the current cache.

`verify_package_order.cpp` checks the package sort's strict ordering, including
the `BasicType` package compared with itself.

`verify_package_dependency_order.cpp` checks the dependency aware sorter that
writes the packages and the types inside a package in a usable order. It
covers a chain of required dependencies, a cycle that only consists of
required dependencies, a cycle that is broken on a preferred (declaration only)
dependency, the deterministic order and that no node is ever lost.

`verify_sdk_package_order.py` audits a generated dump. The generated headers do
not include each other, so a type has to be written before it is used. The
audit walks the include order of `SDK.h` and reports every base class, by value
member, function signature or template argument that is used before its type
is written down. A base class and a by value member need the complete type, a
function signature and a template argument only need a declaration. Pointer
types are written as `class X*`, the elaborated type specifier declares them on
its own and therefore never need an ordering.

`verify_reflected_type.cpp` checks name, size, alignment, and field bounds used
to reject inconsistent objects from a live, changing Unreal object array.

`verify_sdk_layout.cpp` compiles the generated `BasicType.h`, checking the
delegate, map, and UE5 cooked field-path sizes and member offsets.
Field paths retain the native field class from `FFieldPathProperty::PropertyClass`;
those classes need forward declarations, not fake reflected UObject definitions.
The non-editor path layout is resolved field, weak owner, and name array.
The decoder requires the live property's element size to be `0x20`. Field-class
caching must preserve its initial `FName`, unlike `FField` whose initial vtable
slot is replaced with the source address. The audit requires real
`FStructProperty` and `FProperty` arguments and rejects the corrupted `F`/`Fnull`
names seen before that cache fix.

`verify_sdk_types.py` scans every SDK header and the combined IDA header for
unresolved placeholders, including the old macro definitions. It also checks
every reflected field, function parameter, return type, and nested template
argument against the exported type graph and native forward declarations.
It also checks for ArcheAge's `AX5PlayerController`, core engine types, and the
field-path function decoder. Unknown property decoders fail the audit too.
The `ECCF_FSoftObjectProperty` decoder emits `TSoftObjectPtr` separately from
`ECCF_FWeakObjectProperty`, which emits `TWeakObjectPtr`. Older dumps mislabeled
soft object parameters such as `KismetSystemLibrary.LoadAsset_Blocking` as weak
pointers; regenerate a dump to see the corrected declaration.

`verify_sdk_compile.py` compiles the complete `SDK.h` with MSVC, including all
package size assertions, and checks the emitted offsets of every non-bitfield
member. It also compiles the basic types and the separate `ArcheAgeInternals`
helper namespace. Logs and temporary compiler inputs are retained in the dump's
`Validation` directory. A successful generator exit alone does not establish
that these compilation checks pass.

`verify_unreal_layout.cpp` checks the parser's UE 5.7 layouts, including
`UStruct::MinAlignment` as a 16-bit field at `0x5c`, the 24-byte object slot,
16-byte cooked `FText`, and `UFunction::Func` at `0xd8`.
`verify_enum_storage.cpp` checks that signed and 64-bit enum values survive the
metadata JSON round trip. `verify_offset_cache.cpp` checks signature bytes after
loading a saved project. `verify_headless_cache.py` checks fail-fast handling of
an invalid `--cache` path.

For output-format debugging, `--headless --pid <same-build PID> --project ArcheAge
--cache <capture>/SaveState.uedproj` regenerates the existing reflection snapshot
into a new timestamped directory without walking the live object array again.
The running process is still required for module metadata and global resolution.
This does not discover types loaded since the capture or repair old cached
reflection-layout errors; use a fresh live capture after changing parser layouts.

The strict type-graph audit reports unsupported properties even when their
storage is emitted as an explicitly marked, exact-sized opaque byte array.
Unreflected bytes remain identified as padding; the audit does not relabel them
as recovered fields.
