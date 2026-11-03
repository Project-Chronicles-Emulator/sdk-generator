#include "memory.h"

#include "driver.h"
#include "Frontend/Windows/LogWindow.h"
#include <Engine/Userdefined/Offsets.h>


Memory::Memory()
{
	windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_ONLY_LOG, "MEMORY", "Initializing memory class...");
	//only call the init function if status is bad
	if (status == bad)
	{
		//call the init function
		init();

		//set the status to inizilized
		status = inizilaized;
		windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_INFO, "MEMORY", "Initialized Memory class!");
	}

}

Memory::LoadError Memory::load(std::string processName)
{
	//should not happen!
	if (status == bad) DebugBreak();

	//only call the load function if the status is initialized
	if (status == inizilaized)
	{
		loadData(processName, baseAddress, processID);

		if (!baseAddress) {
			windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_ERROR, "MEMORY", "Error getting base address!");
			return noBaseAddress;
		}

		if (!processID) {
			windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_ERROR, "MEMORY", "Error getting process ID!");
			return noProcessID;
		}

		windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_INFO, "MEMORY", "Loaded Memory class!");
	}

	status = loaded;
	return success;
}

Memory::LoadError Memory::load(int processPID)
{
	//should not happen!
	if (status == bad) DebugBreak();

	//only call the load function if the status is initialized
	if (status == inizilaized)
	{
		baseAddress = _getBaseAddress(nullptr, processPID);

		if (!baseAddress) {
			windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_ERROR, "MEMORY", "Error getting base address!");
			return noBaseAddress;
		}

		processID = processPID;
		if (!processID) {
			windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_ERROR, "MEMORY", "Error getting process ID!");
			return noProcessID;
		}

		attachToProcess(processID);

		windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_INFO, "MEMORY", "Loaded Memory class!");
	}

	status = loaded;
	return success;
}

void Memory::checkStatus()
{
	if (status != loaded) DebugBreak();
}

Memory::MemoryStatus Memory::getStatus()
{
	return status;
}

uint64_t Memory::getBaseAddress()
{
	return baseAddress;
}

int Memory::getProcessID()
{
	return processID;
}

int Memory::getTotalReads()
{
	return totalReads;
}

int Memory::getTotalWrites()
{
	return totalWrites;
}

bool Memory::getImageInfo(ImageInfo& out, std::string& error)
{
	out = {};
	error.clear();

	if (status != loaded || !baseAddress || !processID)
	{
		error = "target process is not loaded";
		return false;
	}

	try
	{
		const auto dosHeader = read<IMAGE_DOS_HEADER>(baseAddress);
		if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE || dosHeader.e_lfanew <= 0)
		{
			error = "invalid DOS header";
			return false;
		}

		const auto ntAddress = baseAddress + static_cast<uint32_t>(dosHeader.e_lfanew);
		const auto ntHeaders = read<IMAGE_NT_HEADERS64>(ntAddress);
		if (ntHeaders.Signature != IMAGE_NT_SIGNATURE)
		{
			error = "invalid PE signature";
			return false;
		}
		if (ntHeaders.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
		{
			error = "target is not a PE32+ image";
			return false;
		}
		if (!ntHeaders.OptionalHeader.SizeOfImage || ntHeaders.FileHeader.NumberOfSections == 0 ||
			ntHeaders.FileHeader.NumberOfSections > 96)
		{
			error = "invalid PE image size or section count";
			return false;
		}

		out.machine = ntHeaders.FileHeader.Machine;
		out.optionalHeaderMagic = ntHeaders.OptionalHeader.Magic;
		out.timeDateStamp = ntHeaders.FileHeader.TimeDateStamp;
		out.sizeOfImage = ntHeaders.OptionalHeader.SizeOfImage;
		out.imageBase = baseAddress;

		const auto sectionAddress = ntAddress + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
			ntHeaders.FileHeader.SizeOfOptionalHeader;
		std::vector<IMAGE_SECTION_HEADER> sectionHeaders(ntHeaders.FileHeader.NumberOfSections);
		read(sectionAddress, reinterpret_cast<DWORD64>(sectionHeaders.data()),
			sectionHeaders.size() * sizeof(IMAGE_SECTION_HEADER));

		for (const auto& section : sectionHeaders)
		{
			const auto nameLength = strnlen_s(reinterpret_cast<const char*>(section.Name), IMAGE_SIZEOF_SHORT_NAME);
			out.sections.push_back({
				std::string(reinterpret_cast<const char*>(section.Name), nameLength),
				section.VirtualAddress,
				section.Misc.VirtualSize,
				section.Characteristics
			});
		}
	}
	catch (const std::exception& exception)
	{
		out = {};
		error = std::string("could not read PE headers: ") + exception.what();
		return false;
	}

	const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processID);
	if (snapshot != INVALID_HANDLE_VALUE)
	{
		MODULEENTRY32 module{ sizeof(MODULEENTRY32) };
		if (Module32First(snapshot, &module))
		{
			char utf8Name[MAX_PATH]{};
			const auto length = WideCharToMultiByte(
				CP_UTF8, 0, module.szModule, -1,
				utf8Name, static_cast<int>(std::size(utf8Name)), nullptr, nullptr);
			if (length > 0)
				out.moduleName = utf8Name;
		}
		CloseHandle(snapshot);
	}

	if (out.moduleName.empty())
		out.moduleName = "unknown.exe";

	return true;
}

void Memory::read(const void* address, void* buffer, const DWORD64 size)
{
	totalReads++;
	checkStatus();

	_read(address, buffer, size);
}

void Memory::write(void* address, const void* buffer, const DWORD64 size)
{
	totalWrites++;
	checkStatus();

	_write(address, buffer, size);
}

BOOLEAN CheckMask(const char* Base, const char* Pattern, const char* Mask) {
	for (; *Mask; ++Base, ++Pattern, ++Mask) {
		if (*Mask == 'x' && *Base != *Pattern) {
			return FALSE;
		}
	}
	return TRUE;
}

uint64_t Memory::patternScan(int flag, const char* pattern, const std::string& mask)
{
	static std::unordered_map<const char*, uint64_t> patternMap{};
	struct CodeRegion
	{
		uint64_t vaStart = 0;
		std::vector<char> data;
	};
	static std::vector<CodeRegion> codeRegions;
	static bool init = false;

	if (patternMap.contains(pattern))
		return patternMap[pattern];

	if (!init)
	{
		ImageInfo image;
		std::string error;
		if (!getImageInfo(image, error))
			throw std::runtime_error(error);

		// ArcheAge: signatures resolve against the live process image, so read
		// the executable sections directly from the target. The optional
		// ARCHEAGE_SDK_CAPTURE_BASE/ARCHEAGE_SDK_CAPTURE_SIZE pair (used by the
		// old Aion 2 Lua capture flow) redirects reads to a decrypted copy of
		// the image when both are set.
		uint64_t readBase = baseAddress;
		const char* captureAddress = std::getenv("ARCHEAGE_SDK_CAPTURE_BASE");
		const char* captureLength = std::getenv("ARCHEAGE_SDK_CAPTURE_SIZE");
		if (captureAddress && captureLength)
		{
			const auto captureBase = std::stoull(captureAddress, nullptr, 0);
			const auto captureSize = std::stoull(captureLength, nullptr, 0);
			const auto captureDos = read<IMAGE_DOS_HEADER>(captureBase);
			if (!captureBase || captureBase > UINT64_MAX - captureSize
				|| captureDos.e_magic != IMAGE_DOS_SIGNATURE || captureDos.e_lfanew <= 0
				|| !AccessPolicy::Contains(captureSize, captureDos.e_lfanew, sizeof(IMAGE_NT_HEADERS64)))
				throw std::runtime_error("Invalid decrypted capture bounds or DOS header");

			const auto captureHeaders = read<IMAGE_NT_HEADERS64>(captureBase + captureDos.e_lfanew);
			if (captureHeaders.Signature != IMAGE_NT_SIGNATURE
				|| captureHeaders.FileHeader.Machine != image.machine
				|| captureHeaders.OptionalHeader.Magic != image.optionalHeaderMagic
				|| captureHeaders.OptionalHeader.SizeOfImage != captureSize
				|| !AccessPolicy::CaptureMatches(captureSize, captureHeaders.FileHeader.TimeDateStamp,
					image.sizeOfImage, image.timeDateStamp))
				throw std::runtime_error("Decrypted capture does not match the loaded client");

			readBase = captureBase;
		}

		for (const auto& section : image.sections)
		{
			if (!(section.characteristics & IMAGE_SCN_MEM_EXECUTE))
				continue;
			if (!AccessPolicy::Contains(image.sizeOfImage, section.rva, section.virtualSize))
				throw std::runtime_error("Executable section escapes the target image");

			CodeRegion region;
			region.vaStart = baseAddress + section.rva;
			region.data.assign(section.virtualSize, 0);
			read(readBase + section.rva, reinterpret_cast<DWORD64>(region.data.data()), section.virtualSize);
			codeRegions.push_back(std::move(region));
		}
		if (codeRegions.empty())
			throw std::runtime_error("no executable section found");
		init = true;
	}

	uint64_t selected = 0;
	for (const auto& region : codeRegions)
	{
		const int64_t length = static_cast<int64_t>(region.data.size()) - mask.length();

		for (int64_t i = 0; i <= length; ++i)
		{
			const char* addr = region.data.data() + i;

			if (!CheckMask(addr, pattern, mask.c_str()))
				continue;

			if (selected)
			{
				// A second hit means the signature is not unique; the caller
				// treats 0 as "not found". Log both sites to aid re-scanning.
				printf("[SCAN] ambiguous signature: first 0x%llX second 0x%llX\n",
					static_cast<unsigned long long>(selected),
					static_cast<unsigned long long>(region.vaStart + i));
				return 0;
			}

			if (flag & OFFSET_SIG_RVA)
			{
				if (!AccessPolicy::Contains(region.data.size(), i, 7))
					return 0;
				int displacement = 0;
				std::memcpy(&displacement, addr + 3, sizeof(displacement));
				selected = region.vaStart + i + displacement + 7;
			}
			else
				selected = region.vaStart + i;
		}
	}
	if (selected)
		patternMap.insert(std::pair(pattern, selected));
	return selected;
}
