#include "EngineInternals.h"
#include <cstddef>
#include <cassert>
#include <type_traits>

// The helper kit must coexist with the full SDK without redefining its types.
namespace Kit = ArcheAgeInternals;
static_assert(sizeof(Kit::FUObjectItem) == 0x18);
static_assert(offsetof(Kit::FUObjectItem, Object) == 0x8);
static_assert(offsetof(Kit::FUObjectArray, NumElements) == 0x14);
static_assert(sizeof(Kit::FText) == 0x10);
static_assert(sizeof(Kit::UField) == 0x30);
static_assert(offsetof(Kit::UStruct, SuperStruct) == 0x40);
static_assert(offsetof(Kit::UStruct, MinAlignment) == 0x5c);
static_assert(sizeof(Kit::UStruct::MinAlignment) == 2);
static_assert(sizeof(Kit::UStruct) == 0xb0);
static_assert(offsetof(Kit::UFunction, FunctionFlags) == 0xb0);
static_assert(offsetof(Kit::UFunction, Func) == 0xd8);
static_assert(offsetof(Kit::FFieldClass, Id) == 0x10);
static_assert(sizeof(Kit::FFieldVariant) == 0x8);
static_assert(offsetof(Kit::FField, Next) == 0x18);
static_assert(offsetof(Kit::FProperty, ArrayDim) == 0x30);
namespace ArcheAgeInternals
{
    int reads = 0;
    inline uintptr_t GetImageBase() { return 0x10000000; }
    template <class T> T ReadMem(uintptr_t address)
    {
        ++reads;
        if constexpr (std::is_same_v<T, FUObjectArray>)
        {
            assert(address == GetImageBase() + OFFSET_GOBJECTS);
            FUObjectArray result{};
            result.Objects = reinterpret_cast<FUObjectItem**>(0x20000000);
            result.NumElements = 2;
            result.NumChunks = 1;
            return result;
        }
        else if constexpr (std::is_same_v<T, FUObjectItem*>)
        {
            assert(address == 0x20000000);
            return reinterpret_cast<FUObjectItem*>(0x30000000);
        }
        else if constexpr (std::is_same_v<T, UObject*>)
        {
            assert(address == 0x30000000 + 0x18 + 0x8);
            return reinterpret_cast<UObject*>(0x40000000);
        }
        else
            static_assert(!sizeof(T), "Unexpected remote read type");
    }
}
int main()
{
    assert(Kit::GetObjectAt(-1) == nullptr && Kit::reads == 0);
    assert(Kit::GetObjectAt(1) == reinterpret_cast<Kit::UObject*>(0x40000000));
    assert(Kit::reads == 3);
    assert(Kit::GetObjectAt(2) == nullptr && Kit::reads == 4);
    assert(Kit::GetGWorldPtr() == nullptr);
    return 0;
}
