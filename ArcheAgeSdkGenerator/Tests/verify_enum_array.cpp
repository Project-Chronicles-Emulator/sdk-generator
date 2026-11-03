#include "../Engine/UEClasses/EnumArray.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main()
{
	struct Entry
	{
		uint64_t Name;
		int64_t  Value;
	};

	const Entry source[] = {{101, -1}, {202, 0}, {303, 0x100000000LL}};
	int         reads    = 0;
	const auto  read     = [&](const void* address, void* buffer, size_t size)
	{
		++reads;
		assert(size == sizeof(source));
		std::memcpy(buffer, address, size);
	};

	const auto values = EnumArray::Read(source, 3, read);
	assert(values.size() == 3);
	assert(values[0].Name == 101 && values[0].Value == -1);
	assert(values[2].Name == 303 && values[2].Value == 0x100000000LL);
	assert(EnumArray::Read(source, 0, read).empty());
	assert(EnumArray::Read(source, -1, read).empty());
	assert(EnumArray::Read(static_cast<const Entry*>(nullptr), 3, read).empty());
	assert(reads == 1);

	std::cout << "PASS: enum array size, names, signed/wide values, empty and invalid inputs\n";
	return 0;
}
