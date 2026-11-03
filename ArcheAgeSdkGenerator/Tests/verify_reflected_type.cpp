#include "../Engine/Core/ReflectedTypePolicy.h"

#include <cassert>

int main()
{
	assert(ReflectedTypePolicy::IsAsciiPath("/Script/X5.SkillTableItem"));
	assert(!ReflectedTypePolicy::IsAsciiPath(""));
	assert(!ReflectedTypePolicy::IsAsciiPath("/Script/Bad\xC0"));
	assert(!ReflectedTypePolicy::IsAsciiPath("/Script/Bad\x03"));
	assert(ReflectedTypePolicy::IsObjectPath("/Script/X5.SkillTableItem"));
	assert(!ReflectedTypePolicy::IsObjectPath("None.None"));

	assert(ReflectedTypePolicy::IsIdentifier("Window_WorldMap_C"));
	assert(!ReflectedTypePolicy::IsIdentifier("2Window"));
	assert(!ReflectedTypePolicy::IsIdentifier("Bad/Package"));

	assert(ReflectedTypePolicy::ValidLayout(0x176F5, 16));
	assert(!ReflectedTypePolicy::ValidLayout(0x3ED60000, 8));
	assert(!ReflectedTypePolicy::ValidLayout(0x100, 38));

	assert(ReflectedTypePolicy::ValidMember(0x20, 0x18, 0x40));
	assert(!ReflectedTypePolicy::ValidMember(0x30, 0x18, 0x40));
}
