# Hawkeye ROP

**PE64 ROP gadget finder** for Windows — index executable sections, search for **gadgets** (instruction sequences ending in **`0xC3` `ret`**), and export **RVA / VA / bytes** plus normalized gadget text. The repo also includes **two lab demos** that perform cross-process **ROP against Notepad** with gadgets you discover here — **for security research and education only** (see [Demos](#demos)).

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
![Platform: Windows 10/11 x64](https://img.shields.io/badge/Platform-Windows%2010%2F11%20x64-0078D4)

## Authorized use only

Use Hawkeye ROP and the demos only on **systems you own or are explicitly authorized to administer**, in **isolated lab environments**. The demos write memory into another process and start remote threads. Do not use this against third-party software, games, or production systems without authorization.

## How it works

**Hawkeye ROP** analyzes **PE** modules you choose—on-disk `.exe` / `.dll`, or modules loaded in a process (indexed from disk images)—and **identifies gadgets**. A **gadget** is one reusable part: a short instruction sequence whose last instruction is a near **`ret` (`0xC3`)**. The indexer records gadgets of **at most four instructions including that `ret`**; longer code is not stored as a single gadget. A **ROP chain** is built **outside** this tool—you combine **many** gadgets (addresses on the stack or a pivoted stack) to implement an exploit or lab routine. **Two-instruction** gadgets such as `pop rax; ret` are what you normally want. **Three- and four-instruction** gadgets are uncommon in practice—usually a fallback when no suitable two-instruction gadget exists. Each indexed gadget has normalized text, **RVA**, **VA** when a module base is known, and raw **bytes**. **Probe** and **Batch** search the catalog.

## Requirements

- Windows 10/11 **x64**
- **Visual Studio 2019+** with Desktop development and **Qt 5.15.2 MSVC2019 64-bit** (see `QtInstall` in `app/HawkeyeRopApp.vcxproj`)
- For demos: ability to run **Notepad** and open the target process with appropriate privileges

## Build

- Open **`HawkeyeRop.sln`**, build **Release | x64** (`HawkeyeRopApp`). The exe is written to `x64/Release/` and copied to **`bin/`** (gitignored).
- Copy **Qt 5.15** runtime DLLs next to the exe you run (usually `bin/HawkeyeRop.exe`). If you **redistribute** a build, include Qt’s shared libraries (and typical `platforms/` plugin) and comply with **LGPL-3.0** — see [NOTICES.txt](NOTICES.txt).

## Using Hawkeye ROP

### Scan scope (which modules)

Hawkeye ROP supports **two modes**; they are **mutually exclusive** in the UI.

**Directory scan (`targets/`)** — index PE files you place under **`targets/`** (or any folder you **Browse** to). This suits **exploit-style ROP** where you want gadgets from **as few modules as possible**. A common strategy is to copy universal bases such as **`ntdll.dll`** and **`KernelBase.dll`** into `targets/` and hunt there first.

**Process module scan (PID)** — enable **Process DLLs**, enter a **PID**, and scan **every loaded module** of that process (images are still read **from disk**; reported **VA = module base in the process + RVA**). Use this when you need **game-specific**, **app-specific**, or otherwise **narrow** code—gadgets will be **spread across many DLLs** and the search is heavier.

The **first time** a PE needs indexing, Hawkeye ROP performs a **full gadget harvest** over its executable sections—a **one-time, CPU-heavy pass**. The cache key is **`gadgets/<file name>/<SHA256>/r<rules version>/`** (not the full path). A **cache hit** skips rebuild when the same **file name**, **content hash**, and **index rules** already have a valid catalog—moving the DLL to another folder does **not** by itself force a rescan. A **rebuild** happens when the file **changes on disk**, the **index rules version** bumps, the cache entry is missing or invalid, or you use a **different file name** for the same bytes (that creates a separate cache slot). Later **Probe** and **Batch** runs against cached images are much faster.

### What to search for (Batch vs Probe)

**Batch** — put **`*.chains`** files in **`chains/`** to run many gadget queries in one pass (see `chains/HawkeyeRop.chains` for format). Use this when you already know the patterns you care about.

**Probe** — search for **one gadget pattern** at a time. In practice you often **iterate**: try an instruction sequence, refine operands or mnemonics, repeat until something useful appears. Probe fits that **trial-and-error** workflow.

The indexer stores gadgets of **up to four instructions including `ret`**. In queries, **`;`** separates fixed instructions; each extra **`;`** inserts one **wildcard instruction** (`*`) between fixed parts:

| Query | Matches (conceptually) |
|--------|-------------------------|
| `pop rax; ret` | `pop rax` then `ret` (two instructions) |
| `pop rax;; ret` | `pop rax`, **any one** instruction, then `ret` |
| `pop rax;;; ret` | `pop rax`, **any two** instructions, then `ret` |

## Demos

The **two demos** illustrate the usual workflow: hunt **gadgets** with **Hawkeye ROP**, then **assemble them into a closed ROP chain** that performs a **concrete task** in **Notepad** (cross-process). They are for **education** and to clarify **what this approach can and cannot do**:

- **What the demos show:** a function call made from the ROP chain **returns into the main chain**, and execution **continues** from there.
- **Practical limits:** ROP chains are **often** tied to **specific module builds**; designing and debugging them is **more involved** than injecting a short **shellcode** stub. When a needed **gadget** is hard to find, you often need **substitute instructions and workarounds**, and **expand the search** to **more modules**.
- **Versus shellcode:** ROP **reuses** bytes already mapped as code and advances control through **`ret`** and prepared stacks; shellcode **writes** a contiguous executable buffer and runs it directly. Same high-level goal (code in another process), different mechanics, constraints, and trade-offs.

**Version binding.** The demos ship with **hard-coded module bases and RVAs**. A build from this repo will **usually not run as-is** on your machine. Expect to use **Hawkeye ROP locally** to resolve the gadgets you need and **patch the source** before rebuilding. In production-style ROP work, teams often **resolve gadgets at runtime** (module enumeration plus scan or signature logic); these samples stay **static** on purpose to keep the lab easy to read.

| Demo | Behavior |
|------|----------|
| **`Demo/notepad-test/`** | Starts a **remote thread** in Notepad that runs a ROP chain, calls **MessageBoxA** (dialog), then **safely exits the thread** when you dismiss the dialog. |
| **`Demo/draw-test/`** | **Remote-thread** ROP loop: **GetDC**, **GetCursorPos**, **SetPixel** at the cursor, **Sleep**, repeat. |

Build each demo from its own **`.sln`** under `Demo/`.

## License

**Hawkeye ROP** source and Hawkeye-authored code: **GPL-3.0-or-later** — see [LICENSE](LICENSE).

**Third-party:** [NOTICES.txt](NOTICES.txt) covers **Qt 5.15** (LGPL-3.0, dynamic link at runtime; you deploy Qt DLLs yourself when running or shipping binaries) and **bddisasm** (Apache-2.0, vendored under `third_party/bddisasm/`).

## Security

See [SECURITY.md](SECURITY.md).
