#include "Engine/UEClasses/UnrealClasses.h"
#include <cassert>
#include <type_traits>

static_assert(std::is_same_v<decltype(EngineStructs::Enum::members)::value_type::second_type, int64_t>,
    "Reflection enum values must not be narrowed to 32 bits");
int main()
{
    EngineStructs::Enum value{};
    value.fullName = "/Script/Test.WideEnum";
    value.cppName = "EWideEnum";
    value.type = "uint64_t";
    value.size = 8;
    value.members = {{"Wide", INT64_C(0x100000000)}, {"Negative", -1}};
    const auto loaded = EngineStructs::Enum::fromJson(value.toJson());
    assert(loaded.members == value.members);
    EngineStructs::Struct layout{};
    layout.cppName = "TOptional";
    layout.minAlignment = 16;
    layout.noFixedSize = true;
    const auto restored = EngineStructs::Struct::fromJson(layout.toJson());
    assert(restored.minAlignment == 16 && restored.noFixedSize);
    return 0;
}
