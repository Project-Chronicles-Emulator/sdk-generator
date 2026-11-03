#include "Engine/Userdefined/Offsets.h"
#include <cassert>
#include <cstring>
int main()
{
    const Offset source{OFFSET_SIGNATURE_FOLLOW, "OFFSET_TEST", 0, "\x48\x8d\x00\x01", "xx?x"};
    const auto loaded = Offset::fromJson(source.toJson());
    assert(loaded.mask == source.mask);
    assert(std::memcmp(loaded.sig, source.sig, source.mask.size()) == 0);
    assert(loaded.toJson() == source.toJson());
    return 0;
}
