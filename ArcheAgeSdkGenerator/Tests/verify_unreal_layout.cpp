#include "Engine/UEClasses/UnrealClasses.h"
#include <cstddef>

static_assert(sizeof(FUObjectItem) == FUOBJECTITEM_SIZE);
static_assert(offsetof(FUObjectItem, Object) == FUOBJECTITEM_OBJECTPTR_OFFSET);
static_assert(sizeof(FText) == 0x10);
static_assert(sizeof(UObject) == 0x28);
static_assert(sizeof(UField) == 0x30);
static_assert(offsetof(UStruct, SuperStruct) == 0x40);
static_assert(offsetof(UStruct, Children) == 0x48);
static_assert(offsetof(UStruct, ChildProperties) == 0x50);
static_assert(offsetof(UStruct, PropertiesSize) == 0x58);
static_assert(offsetof(UStruct, MinAlignment) == 0x5c);
static_assert(sizeof(UStruct::MinAlignment) == 2);
static_assert(offsetof(UStruct, StructStateFlags) == 0x5e);
static_assert(sizeof(UStruct) == 0xb0);
static_assert(offsetof(UFunction, FunctionFlags) == 0xb0);
static_assert(offsetof(UFunction, Func) == 0xd8);
static_assert(offsetof(FFieldClass, Id) == 0x10);
static_assert(sizeof(FFieldVariant) == 0x8);
static_assert(offsetof(FField, Next) == 0x18);
static_assert(offsetof(FProperty, ArrayDim) == 0x30);
static_assert(sizeof(FProperty) == 0x70);
static_assert(offsetof(FOptionalProperty, ValueProperty) == 0x70);
int main() { return 0; }
