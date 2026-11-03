#include "BasicType.h"

#include <cstddef>
#include <iostream>

class FProperty;
class FStructProperty;

static_assert(sizeof(FName) == 0x8);
// UE 5.7 cooked FText is 16 bytes; the older 24-byte definition overlaps
// following fields (e.g. FTemplateString::Resolved and polyglot cached text).
static_assert(sizeof(FText) == 0x10);
static_assert(sizeof(FMulticastSparseDelegate) == 0x1);
static_assert(sizeof(FAnsiString) == 0x10);
static_assert(sizeof(FUtf8String) == 0x10);
static_assert(sizeof(TOptional<int32_t, 8>) == 8);
static_assert(sizeof(TOptional<FString, 16>) == 16);
static_assert(sizeof(TOptional<FName, 8>) == 8);
static_assert(sizeof(FDelegateProperty) == 0x10);
static_assert(offsetof(FDelegateProperty, Object) == 0x0);
static_assert(offsetof(FDelegateProperty, FunctionName) == 0x8);
static_assert(sizeof(TMap<uint8_t, uint64_t>) == 0x50);
static_assert(sizeof(TFieldPath<FProperty>) == 0x20);
static_assert(offsetof(TFieldPath<FProperty>, ResolvedField) == 0x0);
static_assert(offsetof(TFieldPath<FProperty>, ResolvedOwner) == 0x8);
static_assert(offsetof(TFieldPath<FProperty>, Path) == 0x10);
static_assert(sizeof(TArray<TFieldPath<FStructProperty>>) == 0x10);

int main()
{
	std::cout << "PASS: generated enum, delegate, map, and field-path storage layouts\n";
	return 0;
}
