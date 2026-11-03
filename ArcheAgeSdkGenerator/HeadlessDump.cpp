#include "HeadlessDump.h"

#include "Engine/Core/Core.h"
#include "Engine/Core/ObjectsManager.h"
#include "Engine/Generation/SDK.h"
#include "Engine/Userdefined/FeatureFlags.h"
#include "Frontend/Windows/LogWindow.h"
#include "Memory/Memory.h"
#include "Settings/EngineSettings.h"

#include <cstdio>
#include <Windows.h>

namespace
{
	LONG WINAPI headlessCrashHandler(EXCEPTION_POINTERS* info)
	{
		const auto code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
		const auto addr = info && info->ExceptionRecord ? reinterpret_cast<uint64_t>(info->ExceptionRecord->ExceptionAddress) : 0;
		fprintf(stderr, "[HEADLESS] CRASH: exception 0x%08lX at 0x%llX\n",
			static_cast<unsigned long>(code), static_cast<unsigned long long>(addr));
		fflush(stderr);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	bool failed(const char* step, const std::string& detail = {})
	{
		if (!detail.empty())
			printf("[HEADLESS] %s failed: %s\n", step, detail.c_str());
		else
			printf("[HEADLESS] %s failed: %s\n", step, windows::LogWindow::getLastLogMessage().c_str());
		return false;
	}
}

int HeadlessDump::run(int pid, const std::string& projectName, const std::string& cacheFile)
{
	setvbuf(stdout, nullptr, _IONBF, 0); //unbuffered: crash must not lose log tail
	SetUnhandledExceptionFilter(headlessCrashHandler);
	printf("[HEADLESS] UEDumper headless dump starting (PID %d, project '%s')\n", pid, projectName.c_str());

	if (!cacheFile.empty() && !std::filesystem::is_regular_file(cacheFile))
	{
		printf("[HEADLESS] Cache file does not exist: %s\n", cacheFile.c_str());
		return 1;
	}

	if (cacheFile.empty() && !EngineSettings::setProjectName(projectName))
	{
		printf("[HEADLESS] Failed to create/open project directory '%s'\n", projectName.c_str());
		return 1;
	}

	EngineSettings::setTargetApplicationName("headless");

	if (Memory::load(pid) != Memory::LoadError::success)
	{
		printf("[HEADLESS] Memory::load failed for PID %d\n", pid);
		return 1;
	}

	printf("[HEADLESS] Attached PID %d, base 0x%llX\n", Memory::getProcessID(), Memory::getBaseAddress());

	if (!cacheFile.empty())
	{
		int done = 0, total = 0;
		if (!EngineCore::loadProject(cacheFile, done, total))
			return failed("load cached project") ? 1 : 1;
		// Write into a new timestamped directory; never overwrite the captured
		// snapshot. The PID is still required to resolve module/global RVAs.
		if (!EngineSettings::setProjectName(projectName))
			return 1;
		printf("[HEADLESS] Regenerating reflection snapshot from '%s'\n", cacheFile.c_str());
		const bool ok = SDKGeneration::Generate(done, total, FeatureFlags::SDK::STABLE);
		printf("[HEADLESS] Cached SDK generation %s (%d/%d)\n", ok ? "finished" : "failed", done, total);
		printf("[HEADLESS] Project saved to '%s'\n", EngineSettings::getWorkingDirectory().string().c_str());
		return ok ? 0 : 1;
	}

	EngineCore();
	if (!EngineCore::initSuccess())
		return failed("EngineCore init") ? 1 : 1;

	ObjectsManager();
	if (ObjectsManager::CRITICAL_STOP_CALLED())
		return failed("ObjectsManager init", ObjectsManager::getErrorMessage()) ? 1 : 1;

	int64_t finishedBytes = 0;
	int64_t totalBytes = 0;
	CopyStatus status = CS_idle;

	ObjectsManager::copyGObjectPtrs(finishedBytes, totalBytes, status);
	if (status != CopyStatus::CS_success || ObjectsManager::CRITICAL_STOP_CALLED())
		return failed("copyGObjectPtrs", ObjectsManager::getErrorMessage()) ? 1 : 1;
	printf("[HEADLESS] copyGObjectPtrs OK (%lld bytes)\n", totalBytes);

	ObjectsManager::copyUBigObjects(finishedBytes, totalBytes, status);
	if (status != CopyStatus::CS_success || ObjectsManager::CRITICAL_STOP_CALLED())
		return failed("copyUBigObjects", ObjectsManager::getErrorMessage()) ? 1 : 1;
	printf("[HEADLESS] copyUBigObjects OK (%lld bytes)\n", totalBytes);

	EngineCore::cacheFNames(finishedBytes, totalBytes, status);
	if (status != CopyStatus::CS_success || ObjectsManager::CRITICAL_STOP_CALLED())
		return failed("cacheFNames") ? 1 : 1;
	printf("[HEADLESS] cacheFNames OK\n");

	int64_t finishedPackages = 0;
	int64_t totalPackages = 0;
	EngineCore::generatePackages(finishedPackages, totalPackages, status);
	if (status != CopyStatus::CS_success || ObjectsManager::CRITICAL_STOP_CALLED())
		return failed("generatePackages") ? 1 : 1;
	printf("[HEADLESS] generatePackages OK (%lld packages)\n", totalPackages);

	ObjectsManager::setSDKGenerationDone();
	int saveDone  = 0;
	int saveTotal = 0;
	EngineCore::saveToDisk(saveDone, saveTotal);
	printf("[HEADLESS] Project cache saved before SDK generation\n");

	int sdkDone = 0;
	int sdkTotal = 0;
	printf("[HEADLESS] Generating SDK...\n");
	const bool sdkOk = SDKGeneration::Generate(sdkDone, sdkTotal, FeatureFlags::SDK::STABLE);
	if (!sdkOk)
		printf("[HEADLESS] SDK or IDA bundle generation failed (%d/%d)\n", sdkDone, sdkTotal);
	else
		printf("[HEADLESS] SDK generation finished (%d/%d)\n", sdkDone, sdkTotal);

	printf("[HEADLESS] Project saved to '%s'\n", EngineSettings::getWorkingDirectory().string().c_str());
	if (sdkOk)
		printf("[HEADLESS] IDA bundle: '%s'\n", (EngineSettings::getWorkingDirectory() / "IDA").string().c_str());

	printf("[HEADLESS] Done. Memory reads: %d\n", Memory::getTotalReads());
	return sdkOk ? 0 : 1;
}
