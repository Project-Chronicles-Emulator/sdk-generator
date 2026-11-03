#include "../Engine/Core/PackageOrderPolicy.h"

#include <cassert>
#include <vector>

int main()
{
	std::vector<std::string> packages { "Zeta", "BasicType", "x5", "Alpha", "x5" };

	for (const std::string& package : packages)
		assert(!PackageOrderPolicy::Before(package, package));

	assert(PackageOrderPolicy::Before("BasicType", "x5"));
	assert(!PackageOrderPolicy::Before("x5", "BasicType"));
	assert(!PackageOrderPolicy::Before("x5", "x5"));
	assert(!PackageOrderPolicy::Before("x5", "x5"));

	std::sort(packages.begin(), packages.end(), PackageOrderPolicy::Before);
	assert(packages.front() == "BasicType");
	assert(packages.back() == "Zeta");
}
