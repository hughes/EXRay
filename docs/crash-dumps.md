# Inspecting EXRay Crash Dumps

EXRay writes a minidump (`.dmp`) file to the user's `%TEMP%` directory when an
unhandled exception occurs. The filename format is:

    EXRay_crash_YYYYMMDD_HHMMSS_<pid>.dmp

In interactive (GUI) mode, a dialog tells the user where the file was saved.
In headless modes (`--validate`, `--benchmark`, `--smoke-test`), the dump is
written silently and the process exits.

## What's in the dump

- Thread stacks and CPU register context for all threads
- Loaded and recently unloaded module list (DLLs, driver versions)
- Global/static data segments (exposure, zoom, file path, app state)
- Open handle table (files, mutexes, events)
- Thread names and timing information

It does NOT include the full heap or EXR pixel data. Typical size: 2-10 MB.

## Opening a dump in Visual Studio

1. **File > Open > File** and select the `.dmp` file.
2. In the Minidump File Summary page, click **Debug with Native Only**.
3. Visual Studio will show the crash location and call stack. If symbols are
   available (debug build or matching PDB), you get full source-level debugging.

## Opening a dump in WinDbg

1. Install [WinDbg](https://aka.ms/windbg) from the Microsoft Store (free), or
   use the classic WinDbg from the Windows SDK.

2. **File > Open Dump File** (or `windbg -z path\to\EXRay_crash_....dmp`).

3. At the command prompt, run:

       !analyze -v

   This performs automatic crash analysis: identifies the faulting thread,
   exception type (access violation, stack overflow, etc.), and the faulting
   instruction address.

4. To see all thread stacks:

       ~*k

5. To see the faulting thread's stack with local variables (debug builds only):

       .ecxr
       kP

## Getting useful stack traces

### Debug builds (best)

Build with `bazelisk build //:EXRay --config=dbg`. MSVC
produces `/Z7` debug info embedded directly in the `.exe`. WinDbg and Visual
Studio will resolve full function names, line numbers, and local variables
automatically — no separate PDB file needed.

### Release builds with PDB (recommended for diagnosable releases)

By default, release builds (`-c opt`) do NOT produce debug symbols. To create
a PDB alongside the release binary:

    bazelisk build -c opt //:EXRay --copt=/Zi --linkopt=/DEBUG:FULL

This produces `EXRay.pdb` alongside `EXRay.exe` in the Bazel output directory.
The PDB must match the exact binary that crashed (same build, same source).

To use the PDB in WinDbg:

    .sympath+ C:\path\to\directory\containing\pdb
    .reload
    !analyze -v

### Release builds without PDB (current default)

Without symbols, WinDbg shows raw addresses like:

    EXRay+0x12a4f

You can still determine the crash location:

1. Note the module base address and fault address from `!analyze -v`.
2. Compute the RVA: `fault_address - module_base`.
3. Use `dumpbin /disasm /out:disasm.txt EXRay.exe` to generate a disassembly
   from the same build, then search for that RVA.
4. Alternatively, open the same binary in IDA Free or Ghidra and navigate to
   the RVA.

This is tedious. Prefer building with PDB for any release you plan to support.

## Common crash signatures

| Exception code | Meaning | Likely cause |
|----------------|---------|--------------|
| `0xC0000005`   | Access violation (read/write of bad address) | Null pointer, use-after-free, buffer overrun |
| `0xC00000FD`   | Stack overflow | Infinite recursion or very deep call stack |
| `0xC0000374`   | Heap corruption | Buffer overrun, double-free, use-after-free |
| `0xE06D7363`   | C++ exception (`throw`) | Unhandled exception from OpenEXR or stdlib |
| `0xC0000409`   | Stack buffer overrun (`__fastfail`) | Security cookie mismatch, buffer overrun on stack |

## Responding to a user-submitted dump

1. Ask the user for: EXRay version, the `.exr` file that was open (if
   possible), and what they were doing when it crashed.
2. Open the dump in Visual Studio or WinDbg with the matching PDB (if
   available).
3. Run `!analyze -v` and check the faulting stack.
4. If the crash is in `EXRay!` code, identify the source location and
   reproduce.
5. If the crash is in a driver DLL (e.g., `nvoglv64.dll`, `amdxc64.dll`),
   note the driver version from `lm` and check for known driver bugs.
6. If the crash is in `ntdll.dll` with heap corruption, try reproducing under
   a debug build (`--config=dbg`) where the CRT debug heap can catch the
   corruption closer to its source.

## CRT debug leak reports

Debug builds (`--config=dbg`) automatically enable CRT heap leak detection.
On exit, unfreed allocations are dumped to:

    %TEMP%\EXRay_debug.log

This output also goes to `OutputDebugString`, visible in Visual Studio's
Output window or [DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview).
