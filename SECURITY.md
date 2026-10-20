# Security Policy

## Supported versions

Security fixes are provided for the **latest tagged release** of Hawkeye ROP only (when published).

## Reporting a vulnerability

Email [hawkeye18485@gmail.com](mailto:hawkeye18485@gmail.com) with the subject `[Hawkeye ROP Security]`.

Please do not open public issues for unfixed vulnerabilities.

Include:

- Hawkeye ROP version or git commit / Release tag
- Windows version and build number
- Steps to reproduce
- Impact (local vs remote, crash vs memory corruption, privilege required)
- Whether the issue is in the **indexer**, **UI**, **demos**, or **third-party disassembler**

## Scope

**In scope:** defects in Hawkeye ROP (application or bundled demos) that cause memory corruption, unsafe deserialization, path traversal on cache files, or other unintended behavior when using the tool as documented.

**Out of scope:**

- Use on systems you do not own or are not explicitly authorized to administer
- Using gadgets or demos to attack third-party software, games, or production systems without authorization
- Malicious PE files that crash the disassembler (report upstream to bddisasm when appropriate)
- Demo hard-coded RVAs failing on a different Windows or DLL build (expected unless re-probed)
- Process injection or ROP execution in demos when used outside authorized lab environments

## Process

We acknowledge reports when possible and ship fixes in a future release. We may credit reporters with their permission.

## Authorized use

Hawkeye ROP is for **security research and education** on systems you administer. The included demos perform **cross-process memory writes and remote thread creation** in a target process (e.g. Notepad). Run them only in isolated lab VMs with explicit authorization. This is not a game cheat or bypass kit.
