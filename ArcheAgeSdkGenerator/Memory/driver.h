// ReSharper disable CppNonInlineFunctionDefinitionInHeaderFile
#pragma once

//add any other includes here your driver might use
#include <Windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <io.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include "AccessPolicy.h"

/*
██████╗░██╗░░░░░███████╗░█████╗░░██████╗███████╗  ██████╗░███████╗░█████╗░██████╗░██╗
██╔══██╗██║░░░░░██╔════╝██╔══██╗██╔════╝██╔════╝  ██╔══██╗██╔════╝██╔══██╗██╔══██╗██║
██████╔╝██║░░░░░█████╗░░███████║╚█████╗░█████╗░░  ██████╔╝█████╗░░███████║██║░░██║██║
██╔═══╝░██║░░░░░██╔══╝░░██╔══██║░╚═══██╗██╔══╝░░  ██╔══██╗██╔══╝░░██╔══██║██║░░██║╚═╝
██║░░░░░███████╗███████╗██║░░██║██████╔╝███████╗  ██║░░██║███████╗██║░░██║██████╔╝██╗
╚═╝░░░░░╚══════╝╚══════╝╚═╝░░╚═╝╚═════╝░╚══════╝  ╚═╝░░╚═╝╚══════╝╚═╝░░╚═╝╚═════╝░╚═╝
*/


/// here should be your driver functions
///	DO NOT call any of those functions from any class,
///	they should only get called from the memory class (memory.cpp and memory.h)
/// DO NOT include this file in any other file, you might get linker errors!
/// ANY CHANGES you do to the params in functions, make sure to also edit the memory.cpp and memory.h file!

//global variables here
HANDLE procHandle = nullptr;

// ---- PlaySharp bridge (kernel-mode memory access for anti-cheat protected processes) ----
static FILE* g_bridgeIn = nullptr;
static FILE* g_bridgeOut = nullptr;
static PROCESS_INFORMATION g_bridgeProcInfo = {};
static int g_bridgeCallId = 0;
static int g_bridgePid = 0;

static const char* GetMemoryAccessMode()
{
	static const char* mode = AccessPolicy::Mode(std::getenv("ARCHEAGE_SDK_MEMORY_MODE"));

	return mode;
}

static std::string GetBridgePath()
{
    const char* configuredPath = std::getenv("ARCHEAGE_SDK_BRIDGE_PATH");
    return configuredPath && *configuredPath ? configuredPath : "playsharp-bridge.exe";
}


// Base64 decoder
static const int b64_index[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,62,0,0,0,63, 52,53,54,55,56,57,58,59,60,61,0,0,0,0,0,0,
    0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14, 15,16,17,18,19,20,21,22,23,24,25,0,0,0,0,0,
    0,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40, 41,42,43,44,45,46,47,48,49,50,51,0,0,0,0,0
};

static std::vector<uint8_t> b64_decode(const std::string& s) {
    std::vector<uint8_t> out;
    int val = 0, valb = -8;
    for (unsigned char c : s) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        if (b64_index[c] == 0 && c != 'A') continue;
        val = (val << 6) + b64_index[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// Read one full NDJSON line from the bridge (replies can exceed any fixed
// buffer: a 32 KB memory read is ~44 KB of base64 plus JSON). A truncated
// read leaves leftover bytes that corrupt the next response, which is why
// this accumulates chunks until the terminating newline.
static bool bridgeReadLine(std::string& out) {
    out.clear();
    char chunk[8192];
    while (fgets(chunk, sizeof(chunk), g_bridgeOut)) {
        out += chunk;
        if (!out.empty() && out.back() == '\n')
            return true;
    }
    return !out.empty();
}

// Send NDJSON request to bridge and read the matching response line.
static nlohmann::json bridgeCall(const std::string& method, const nlohmann::json& params) {
    if (!g_bridgeIn || !g_bridgeOut) return {};
    g_bridgeCallId++;
    nlohmann::json req;
    req["id"] = g_bridgeCallId;
    req["method"] = method;
    req["params"] = params;
    std::string line = req.dump() + "\n";
    fputs(line.c_str(), g_bridgeIn);
    fflush(g_bridgeIn);

    std::string responseLine;
    while (bridgeReadLine(responseLine)) {
        try {
            auto resp = nlohmann::json::parse(responseLine);
            if (resp.value("id", 0) == g_bridgeCallId) {
                return resp;
            }
        } catch (...) {}
    }
    return {};
}

//in case you need to initialize anything BEFORE your com works, you can do this in here.
//this function IS NOT DESIGNED to already take the process name as input or anything related to the target process
//use the function "load" below which will contain data about the process name
inline void init()
{
    // Launch PlaySharp bridge subprocess for kernel-mode memory access
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hChildStdinRd = nullptr, hChildStdinWr = nullptr;
    HANDLE hChildStdoutRd = nullptr, hChildStdoutWr = nullptr;

    CreatePipe(&hChildStdinRd, &hChildStdinWr, &sa, 0);
    CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &sa, 0);

    SetHandleInformation(hChildStdinWr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hChildStdoutRd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hChildStdinRd;
    si.hStdOutput = hChildStdoutWr;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    ZeroMemory(&g_bridgeProcInfo, sizeof(g_bridgeProcInfo));
    const std::string bridgePath = GetBridgePath();
    std::string commandLine = "\"" + bridgePath + "\"";
    if (CreateProcessA(bridgePath.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &g_bridgeProcInfo))
    {
        g_bridgeIn = _fdopen(_open_osfhandle(reinterpret_cast<intptr_t>(hChildStdinWr), 0), "w");
        g_bridgeOut = _fdopen(_open_osfhandle(reinterpret_cast<intptr_t>(hChildStdoutRd), 0), "r");
        setvbuf(g_bridgeIn, nullptr, _IONBF, 0);
        setvbuf(g_bridgeOut, nullptr, _IONBF, 0);

        // Connect to PlaySharp kernel driver
        auto setupResp = bridgeCall("setup", nlohmann::json::object());
        printf("[BRIDGE] setup response: %s\n", setupResp.dump().c_str());
        auto dbgResp = bridgeCall("set_debug_privileges", nlohmann::json::object());
        printf("[BRIDGE] set_debug_privileges response: %s\n", dbgResp.dump().c_str());
    }
    else
    {
        CloseHandle(hChildStdinRd);
        CloseHandle(hChildStdinWr);
        CloseHandle(hChildStdoutRd);
        CloseHandle(hChildStdoutWr);
    }
}

uint64_t _getBaseAddress(const wchar_t* processName, int& pid);

void attachToProcess(const int& pid);

/**
 * \brief use this function to initialize the target process
 * \param processName process name as input
 * \param baseAddress base address of the process gets returned
 * \param processID process id of the process gets returned
 */
inline void loadData(std::string& processName, uint64_t& baseAddress, int& processID)
{
    const auto name = std::wstring(processName.begin(), processName.end());

    baseAddress = _getBaseAddress(name.c_str(), processID);

    attachToProcess(processID);
}

/**
 * \brief read function — uses PlaySharp bridge kernel-mode read to bypass anti-cheat
 * \param address memory address to read from
 * \param buffer memory address to write to
 * \param size size of memory to read (expects the buffer/address to have this size too)
 */
inline void _read(const void* address, void* buffer, const DWORD64 size)
{
    // Try PlaySharp bridge kernel read first
    if (g_bridgeOut && g_bridgePid)
    {
        const auto addr = reinterpret_cast<uint64_t>(address);
        DWORD64 remaining = size;
        uint8_t* dst = static_cast<uint8_t*>(buffer);
        uint64_t cur = addr;

        while (remaining > 0)
        {
            DWORD64 chunk = remaining > AccessPolicy::READ_CHUNK ? AccessPolicy::READ_CHUNK : remaining;

            nlohmann::json params;
            params["pid"] = g_bridgePid;
            params["address"] = "0x" + std::to_string(cur);  // simple hex
            // Use stringstream for proper hex formatting
            char hexAddr[32];
            sprintf_s(hexAddr, "0x%llX", static_cast<unsigned long long>(cur));
            params["address"] = hexAddr;
            params["size"] = static_cast<int>(chunk);
            params["mode"] = GetMemoryAccessMode();

            auto resp = bridgeCall("read_memory", params);

            if (resp.contains("result") && resp["result"].value("success", false))
            {
                std::string b64 = resp["result"]["data"].get<std::string>();
                auto decoded = b64_decode(b64);
                memcpy(dst, decoded.data(), decoded.size() < chunk ? decoded.size() : chunk);
                if (decoded.size() < chunk)
                {
                    memset(dst + decoded.size(), 0, chunk - decoded.size());
                }
            }
            else
            {
                memset(dst, 0, chunk);
            }

            cur += chunk;
            dst += chunk;
            remaining -= chunk;
        }
        return;
    }

    if (!AccessPolicy::AllowsUsermodeFallback(GetMemoryAccessMode()))
    {
        memset(buffer, 0, size);
        return;
    }

    // Fallback: usermode ReadProcessMemory
    size_t bytes_read = 0;
    BOOL b = ReadProcessMemory(procHandle, address, buffer, size, &bytes_read);
    if (!b)
    {
        for (int i = 1; i < static_cast<int>(size) && !b; i += 10)
        {
            b = ReadProcessMemory(procHandle, address, buffer, size - i, nullptr);
        }
    }
}


/**
 * \brief write function (replace with your write logic)
 * \param address memory address to write to
 * \param buffer memory address to write from
 * \param size size of memory to write (expects the buffer/address to have this size too)
 */
inline void _write(void* address, const void* buffer, const DWORD64 size)
{
    WriteProcessMemory(procHandle, address, buffer, size, nullptr);
}


/**
 * \brief gets the process base address. If you adjust the params, make sure to change them in memory.cpp too
 * \param processName the name of the process
 * \param pid returns the process id
 * \return process base address
 */
uint64_t _getBaseAddress(const wchar_t* processName, int& pid)
{
    uint64_t baseAddress = 0;

    if (!pid)
    {
        // Try PlaySharp bridge get_pid_by_name
        if (g_bridgeOut)
        {
            // Convert wchar_t to narrow string
            char narrow[260];
            WideCharToMultiByte(CP_ACP, 0, processName, -1, narrow, sizeof(narrow), nullptr, nullptr);
            nlohmann::json params;
            params["name"] = narrow;
            auto resp = bridgeCall("get_pid_by_name", params);
            if (resp.contains("result") && resp["result"].value("success", false))
            {
                if (resp["result"].contains("pid"))
                    pid = resp["result"]["pid"].get<int>();
            }
        }

        if (!pid)
        {
            const HANDLE hProcess = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hProcess == INVALID_HANDLE_VALUE) return false;
            PROCESSENTRY32 pe32 = { sizeof(PROCESSENTRY32) };
            if (!Process32First(hProcess, &pe32)) { CloseHandle(hProcess); return false; }
            while (Process32Next(hProcess, &pe32)) {
                if (wcscmp(pe32.szExeFile, processName) == 0) {
                    pid = pe32.th32ProcessID;
                    break;
                }
            }
            CloseHandle(hProcess);
        }
    }

    // Try PlaySharp bridge get_module_base (kernel mode)
    if (pid != 0 && g_bridgeOut)
    {
        // When PID is provided directly, processName may be nullptr
        // Use enum_modules to get the first module (main executable)
        nlohmann::json params;
        params["pid"] = pid;
        params["mode"] = GetMemoryAccessMode();
        auto resp = bridgeCall("enum_modules", params);
        printf("[BRIDGE] enum_modules response: %s\n", resp.dump().c_str());
        if (resp.contains("result") && resp["result"].value("success", false))
        {
            auto modules = resp["result"].value("modules", nlohmann::json::array());
            if (!modules.empty())
            {
                std::string baseStr = modules[0].value("base", "0x0");
                baseAddress = std::stoull(baseStr, nullptr, 16);
            }
        }

        // If enum_modules failed, try get_module_base with known name
        // processName may be nullptr when a PID was passed directly — default to ArcheAgeChronicles.exe
        if (!baseAddress)
        {
            char narrow[260];
            if (processName)
                WideCharToMultiByte(CP_ACP, 0, processName, -1, narrow, sizeof(narrow), nullptr, nullptr);
            else
                strcpy_s(narrow, "ArcheAgeChronicles.exe");
            nlohmann::json params2;
            params2["pid"] = pid;
            params2["name"] = narrow;
            params2["mode"] = GetMemoryAccessMode();
            auto resp2 = bridgeCall("get_module_base", params2);
            if (resp2.contains("result") && resp2["result"].value("success", false))
            {
                std::string baseStr = resp2["result"]["base"].get<std::string>();
                baseAddress = std::stoull(baseStr, nullptr, 16);
            }
        }
    }

    if (!AccessPolicy::AllowsUsermodeFallback(GetMemoryAccessMode()))
        return baseAddress;

    // Fallback: Toolhelp32 module snapshot
    if (!baseAddress && pid != 0) {
        const HANDLE hModule = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (hModule != INVALID_HANDLE_VALUE) {
            MODULEENTRY32 me32 = { sizeof(MODULEENTRY32) };
            if (Module32First(hModule, &me32)) {
                baseAddress = reinterpret_cast<DWORD64>(me32.modBaseAddr);
            }
            CloseHandle(hModule);
        }
    }

    // Fallback: PEB walk
    if (!baseAddress && pid != 0)
    {
        const HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hProc)
        {
            typedef LONG(NTAPI* pNtQueryInformationProcess)(HANDLE, ULONG, PVOID, ULONG, PULONG);
            const auto ntdll = GetModuleHandleW(L"ntdll.dll");
            const auto pNtQueryInfo = reinterpret_cast<pNtQueryInformationProcess>(
                GetProcAddress(ntdll, "NtQueryInformationProcess"));
            if (pNtQueryInfo)
            {
                PROCESS_BASIC_INFORMATION pbi = {};
                ULONG returnLength = 0;
                if (pNtQueryInfo(hProc, ProcessBasicInformation, &pbi, sizeof(pbi), &returnLength) == 0
                    && pbi.PebBaseAddress)
                {
                    SIZE_T bytesRead = 0;
                    uint64_t imageBase = 0;
                    if (ReadProcessMemory(hProc,
                        reinterpret_cast<LPCVOID>(reinterpret_cast<uintptr_t>(pbi.PebBaseAddress) + 0x10),
                        &imageBase, sizeof(imageBase), &bytesRead) && imageBase)
                    {
                        baseAddress = imageBase;
                    }
                }
            }
            CloseHandle(hProc);
        }
    }

    // Fallback: known base for ArcheAgeChronicles (UE default 0x140000000)
    if (!baseAddress && pid != 0)
    {
        baseAddress = 0x140000000;
    }

    return baseAddress;
}

/**
 * \brief this function might not be needed for your driver, this just attaches to the process
 * \param pid process id of the target process
 */
void attachToProcess(const int& pid)
{
    g_bridgePid = pid;
    procHandle = OpenProcess(PROCESS_ALL_ACCESS, 0, pid);
}
