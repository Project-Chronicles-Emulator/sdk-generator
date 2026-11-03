#include "../Engine/UEClasses/PackageNamePolicy.h"

#include <cassert>

int main()
{
	assert(PackageNamePolicy::Second("").empty());
	assert(PackageNamePolicy::Second("/").empty());
	assert(PackageNamePolicy::Second("/Script/X5") == "X5");
	assert(PackageNamePolicy::Second("/Game/Characters/Hero") == "Characters");
	assert(PackageNamePolicy::Second("Name").empty());
	assert(PackageNamePolicy::Second("None").empty());
}
