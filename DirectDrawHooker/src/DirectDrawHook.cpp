
// Define INITGUID before ddraw.h
#define INITGUID

// DirectDraw includes
#include <ddraw.h>

// Standard includes
#include <windows.h>
#include <iostream>
#include <iomanip>
#include <stdio.h>
#include <stdlib.h>
#include <unordered_set>
#include <string>
#include <mutex>
#include <tlhelp32.h>
#include <tchar.h>

// Detours include
#include "detours.h"

#include "CpuSampler.h"

#include <iostream>
#include <fstream>

struct ddraw_dll
{
    HMODULE dll;
    FARPROC AcquireDDThreadLock;
    FARPROC CompleteCreateSysmemSurface;
    FARPROC D3DParseUnknownCommand;
    FARPROC DDGetAttachedSurfaceLcl;
    FARPROC DDInternalLock;
    FARPROC DDInternalUnlock;
    FARPROC DSoundHelp;
    FARPROC DirectDrawCreate;
    FARPROC DirectDrawCreateClipper;
    FARPROC DirectDrawCreateEx;
    FARPROC DirectDrawEnumerateA;
    FARPROC DirectDrawEnumerateExA;
    FARPROC DirectDrawEnumerateExW;
    FARPROC DirectDrawEnumerateW;
    FARPROC DllCanUnloadNow;
    FARPROC DllGetClassObject;
    FARPROC GetDDSurfaceLocal;
    FARPROC GetOLEThunkData;
    FARPROC GetSurfaceFromDC;
    FARPROC RegisterSpecialCase;
    FARPROC ReleaseDDThreadLock;
    FARPROC SetAppCompatData;
} ddraw;

// DirectDraw Hook Definitions
typedef HRESULT(__stdcall *DirectDrawCreateFunc)(GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter);

DirectDrawCreateFunc fnDirectDrawCreate = NULL;

HANDLE hMainThread;

typedef HRESULT(__stdcall* func_Blt)(LPRECT, LPDIRECTDRAWSURFACE, LPRECT, DWORD, LPDDBLTFX);

VE::Core::Util::CpuSampler CpuSampler;

void PrintError(const char* msg) 
{
    DWORD eNum;
    TCHAR sysMsg[256];
    TCHAR* p;

    eNum = GetLastError();
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, eNum,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
        sysMsg, 256, NULL);

    // Trim the end of the line and terminate it with a null
    p = sysMsg;
    while ((*p > 31) || (*p == 9))
        ++p;
    do { *p-- = 0; } while ((p >= sysMsg) &&
        ((*p == '.') || (*p < 33)));

    // Display the message
    _tprintf(TEXT("\n  WARNING: %s failed with error %d (%s)"), msg, eNum, sysMsg);
}

bool ListProcessThreads(DWORD dwOwnderPID) 
{
    HANDLE hThreadSnap = INVALID_HANDLE_VALUE;
    THREADENTRY32 te32;

    // Take a snapshot of all running threads.
    hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap == INVALID_HANDLE_VALUE) 
    {
        return false;
    }

    // Fill in the size of the structure before using it.
    te32.dwSize = sizeof(THREADENTRY32);

    // Retrieve the information about the first thread,
    // and exit if unsuccessful
    if (!Thread32First(hThreadSnap, &te32)) 
    {
        PrintError("Thread32First"); // Show cause of failure.
        CloseHandle(hThreadSnap); // Clean up snapshot object.
        return false;
    }

    // Now walk the thread list of the system,
    // and display information about each thread
    // associated with the specified process.
    do 
    {
        if (te32.th32OwnerProcessID == dwOwnderPID) 
        {
            HANDLE threadHandle = OpenThread(SPECIFIC_RIGHTS_ALL, FALSE, te32.th32ThreadID); // With great power comes great responsibility...
            if (threadHandle != INVALID_HANDLE_VALUE) 
            {
                // _tprintf(TEXT("\nTHREAD ID = 0x%08X"), te32.th32ThreadID);
                _tprintf(TEXT("\nTHREAD ID = %d"), te32.th32ThreadID);

                FILETIME lpCreationTime, lpExitTime, lpKernelTime, lpUserTime;
                if (GetThreadTimes(threadHandle, &lpCreationTime, &lpExitTime, &lpKernelTime, &lpUserTime)) 
                {
                    SYSTEMTIME sysCreatedTime;
                    if (FileTimeToSystemTime(&lpCreationTime, &sysCreatedTime)) 
                    {
                        _tprintf(TEXT("\nCreation time = %u:%u:%u:%u"), sysCreatedTime.wHour, sysCreatedTime.wMinute, sysCreatedTime.wSecond, sysCreatedTime.wMilliseconds);
                    }
                    else 
                    {
                        PrintError("Performing file time conversion");
                    }
                }
                else 
                {
                    PrintError("Acquiring thread times");
                }

                _tprintf(TEXT("\nbase priority = %d"), te32.tpBasePri);
                _tprintf(TEXT("\ndelta priority = %d"), te32.tpDeltaPri);
            }
        }
    } while (Thread32Next(hThreadSnap, &te32));

    _tprintf(TEXT("\n\n"));

    // Don't forget to clean up the snapshot object.
    CloseHandle(hThreadSnap);
    return true;
}

DWORD FindProcessId(const char* procName) 
{
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

    if (Process32First(snapshot, &entry) == TRUE)
    {
        while (Process32Next(snapshot, &entry) == TRUE)
        {
            if (_stricmp(entry.szExeFile, procName) == 0)
            {
                DWORD pid = entry.th32ProcessID;
                CloseHandle(snapshot);

                return pid;
            }
        }
    }

    CloseHandle(snapshot);
    return -1;
}

BOOL IsProcessRunning(DWORD pid)
{
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    DWORD ret = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return ret == WAIT_TIMEOUT;
}

void SetupConsole() 
{
    AllocConsole();
    SetConsoleTitle("DirectDraw Hooker");
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    freopen("CONIN$", "r", stdin);
}

HRESULT WINAPI MyDirectDrawCreate(GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter)
{
    std::cout << "Hej" << std::endl;
    return fnDirectDrawCreate(lpGUID, lplpDD, pUnkOuter);
}

void DetorDirectDrawCreate() 
{
    std::cout << "Calling fnDirectDrawCreate Detour" << std::endl;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(LPVOID&)fnDirectDrawCreate, (PBYTE)MyDirectDrawCreate);
    DetourTransactionCommit();
}

int main() 
{
    SetupConsole();

    std::ofstream outputFile("cpu-measure.txt");
    if (!outputFile.is_open())
    {
        return 0;
    }

    // Get path of executable file of the current process.
    char execProcessName[MAX_PATH + 1];
    execProcessName[MAX_PATH] = '\0';
    GetModuleFileName(NULL, execProcessName, MAX_PATH);

    std::cout << "Loaded from: " << execProcessName << std::endl;

    // Get the id of the process loading the dll.
    uint32_t pid = GetCurrentProcessId();
    std::cout << "Process ID: " << std::to_string(pid).c_str() << std::endl;

    HANDLE processHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    CpuSampler.SetProcessHandle(processHandle);

    std::cout << "Process ID: " << std::to_string(pid).c_str() << std::endl;

    // List the running threads for the process.
    ListProcessThreads(pid);

    ddraw.dll = GetModuleHandle("ddraw.dll");
    if (ddraw.dll != NULL)
    {
        std::cout << "Successfully loaded ddraw.dll handle!" << std::endl;
    }

    ddraw.AcquireDDThreadLock = GetProcAddress(ddraw.dll, "AcquireDDThreadLock");
    ddraw.CompleteCreateSysmemSurface = GetProcAddress(ddraw.dll, "CompleteCreateSysmemSurface");
    ddraw.D3DParseUnknownCommand = GetProcAddress(ddraw.dll, "D3DParseUnknownCommand");
    ddraw.DDGetAttachedSurfaceLcl = GetProcAddress(ddraw.dll, "DDGetAttachedSurfaceLcl");
    ddraw.DDInternalLock = GetProcAddress(ddraw.dll, "DDInternalLock");
    ddraw.DDInternalUnlock = GetProcAddress(ddraw.dll, "DDInternalUnlock");
    ddraw.DSoundHelp = GetProcAddress(ddraw.dll, "DSoundHelp");
    ddraw.DirectDrawCreate = GetProcAddress(ddraw.dll, "DirectDrawCreate");
    ddraw.DirectDrawCreateClipper = GetProcAddress(ddraw.dll, "DirectDrawCreateClipper");
    ddraw.DirectDrawCreateEx = GetProcAddress(ddraw.dll, "DirectDrawCreateEx");
    ddraw.DirectDrawEnumerateA = GetProcAddress(ddraw.dll, "DirectDrawEnumerateA");
    ddraw.DirectDrawEnumerateExA = GetProcAddress(ddraw.dll, "DirectDrawEnumerateExA");
    ddraw.DirectDrawEnumerateExW = GetProcAddress(ddraw.dll, "DirectDrawEnumerateExW");
    ddraw.DirectDrawEnumerateW = GetProcAddress(ddraw.dll, "DirectDrawEnumerateW");
    ddraw.DllCanUnloadNow = GetProcAddress(ddraw.dll, "DllCanUnloadNow");
    ddraw.DllGetClassObject = GetProcAddress(ddraw.dll, "DllGetClassObject");
    ddraw.GetDDSurfaceLocal = GetProcAddress(ddraw.dll, "GetDDSurfaceLocal");
    ddraw.GetOLEThunkData = GetProcAddress(ddraw.dll, "GetOLEThunkData");
    ddraw.GetSurfaceFromDC = GetProcAddress(ddraw.dll, "GetSurfaceFromDC");
    ddraw.RegisterSpecialCase = GetProcAddress(ddraw.dll, "RegisterSpecialCase");
    ddraw.ReleaseDDThreadLock = GetProcAddress(ddraw.dll, "ReleaseDDThreadLock");
    ddraw.SetAppCompatData = GetProcAddress(ddraw.dll, "SetAppCompatData");

    fnDirectDrawCreate = reinterpret_cast<DirectDrawCreateFunc>(ddraw.DirectDrawCreate);
    if (fnDirectDrawCreate != NULL) 
    {
        std::cout << "Successfully acquired DirectDrawCreate pointer" << std::endl;
        DetorDirectDrawCreate();
    }

    CpuSampler.SetCPUSamplingFrequency(100);

    while (IsProcessRunning(pid)) 
    {
        double cpuUsage = 0.0;
        if (CpuSampler.GetProcessUsage(cpuUsage))
        {
            outputFile << std::to_string(cpuUsage) << "\n";
        }
    }

    outputFile.close();
    return 0;
}

    __declspec(dllexport) BOOL __stdcall DllMain(HMODULE hModule,
	DWORD ul_reason_for_call,
	LPVOID lpReseved
)
{
    switch (ul_reason_for_call) 
    {
    case DLL_PROCESS_ATTACH:
    {
        // Sleep(10000);
        DisableThreadLibraryCalls(hModule);
        hMainThread = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)main, NULL, NULL, NULL);
        break;
    }
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
    	break;
    }
    return TRUE;
}