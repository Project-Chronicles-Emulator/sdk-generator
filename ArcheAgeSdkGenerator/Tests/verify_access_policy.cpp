#include "../Memory/AccessPolicy.h"

#include <cassert>
#include <iostream>
#include <string_view>

int main()
{
	assert(std::string_view(AccessPolicy::Mode(nullptr)) == "usermode");
	assert(std::string_view(AccessPolicy::Mode("")) == "usermode");
	assert(std::string_view(AccessPolicy::Mode("usermode")) == "usermode");
	assert(std::string_view(AccessPolicy::Mode("kernel")) == "kernel");
	assert(std::string_view(AccessPolicy::Mode("playsharp")) == "kernel");
	assert(AccessPolicy::AllowsUsermodeFallback("usermode"));
	assert(!AccessPolicy::AllowsUsermodeFallback("kernel"));
	assert((AccessPolicy::READ_CHUNK + 2) / 3 * 4 + 1024 < 65536);
	assert(AccessPolicy::CaptureMatches(4096, 123, 4096, 123));
	assert(!AccessPolicy::CaptureMatches(4096, 124, 4096, 123));
	assert(!AccessPolicy::CaptureMatches(4096, 123, 8192, 123));
	assert(!AccessPolicy::CaptureMatches(0, 123, 0, 123));
	assert(AccessPolicy::Contains(4096, 100, 3996));
	assert(!AccessPolicy::Contains(4096, 100, 3997));
	assert(!AccessPolicy::Contains(4096, UINT64_MAX, 2));
	assert(!AccessPolicy::Contains(4096, 0, 0));

	bool rejected = false;

	try
	{
		AccessPolicy::Mode("invalid");
	}
	catch (const std::invalid_argument&)
	{
		rejected = true;
	}

	assert(rejected);
	std::cout << "PASS: explicit kernel selection, default compatibility, fail-closed fallback and bounded replies\n";
}
