# ArcheAge Chronicles offsets (UE 5.7.4)

Validated on 2026-10-23 against the live `ArcheAgeChronicles.exe` process,
image base `0x140000000`. These offsets are build-specific.
The client contains the engine version string `5.7.4-0+UE5`.

## GNames: FNamePool at RVA `0xC121A00`

Current extended signature in `Engine/Userdefined/Offsets.h`:

`48 8D 15 ? ? ? ? EB 16 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B D0 C6 05 ? ? ? ? 01 8B CB 0F B7 C3 89 44 24 34`

The instruction at VA `0x1419DE5C1` resolves to the pool. The shorter signature
also matches another site and is rejected by the scanner's uniqueness check;
use the complete current signature and its exact-length mask.

The block table begins at `+0x10`. Names use a two-byte entry stride and the
non-case-preserving header (`length = header >> 6`). Block zero begins with
`None`, `ByteProperty`, `IntProperty`, and `BoolProperty`. FName encryption and
case preservation are disabled; `sizeof(FName) == 8`.

## GObjects: FChunkedFixedUObjectArray at RVA `0xC1EFA60`

Current extended signature:

`48 8B 05 ? ? ? ? 48 8B 0C C8 48 8B 04 D1 48 C1 F8 20 85 05 ? ? ? ? 74 15 4C`

The instruction at VA `0x141AFA2DD` resolves directly to the chunked array,
not to an outer FUObjectArray wrapper. The shorter prefix is ambiguous.

Array fields:

- `+0x00`: Objects, pointer to the chunk table.
- `+0x08`: PreAllocatedObjects.
- `+0x10`: MaxElements.
- `+0x14`: NumElements, changes as the game loads objects.
- `+0x18`: MaxChunks.
- `+0x1c`: NumChunks.

Slots are 24 bytes with the UObject pointer at slot `+0x08`. Other slot bytes
are deliberately opaque rather than given unverified Flags/Serial fields.
Chunk table and object pointers are runtime addresses, not fixed offsets.

## Critical parser layouts

- `UObject`: size `0x28`; name at `0x18`, outer at `0x20`.
- `UField`: size `0x30`; Next at `0x28`. The base-chain fields belong to UStruct.
- `UStruct`: base-chain array at `0x30`, base-chain count at `0x38`, SuperStruct
  at `0x40`, Children at `0x48`, ChildProperties at `0x50`, PropertiesSize at
  `0x58`, **16-bit** MinAlignment at `0x5c`, state flags at `0x5e`.
- `UFunction`: FunctionFlags at `0xb0`, native Func pointer at `0xd8`.
- `FFieldClass`: Id at `0x10` on UE 5.7; ClassFlags precedes Id.
- `FFieldVariant`: size `8` (tagged pointer).
- Cooked `FText`: size `0x10`, not the older `0x18` definition.

Reading MinAlignment as a 32-bit integer includes the state flags and causes
valid reflected classes to be rejected. The old FText definition also causes
member overlaps and size assertion failures.

## GWorld

Unresolved and optional for SDK generation. The helper accessor returns null
when no offset is configured; it does not read the executable header as a world
pointer. Finding GWorld is a separate task from validating the reflection SDK.

## Validation

Run `Tests/verify_sdk_compile.py` against the output directory, followed by the
strict type-graph audit, IDA-bundle validation, and `Tests/compare_sdk_dumps.py`.
A successful generation exit is not a substitute for compiling the headers.
Only reflected objects loaded at capture time can be discovered by a live dump.
