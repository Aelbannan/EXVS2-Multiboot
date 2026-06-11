#pragma once

#include <Windows.h>

// EXVS2 IB 1.04 — vsac29_Release.exe
static constexpr DWORD kRvaLocalizedLookupHash = 0x300860;  // FUN_140300860 NTXL hash lookup
static constexpr DWORD kRvaLocalizedExpand = 0x3012E0;      // FUN_1403012e0 markup expand + resolve
static constexpr DWORD kRvaSetTextKey = 0x1BF220;           // FUN_1401bf220 AnmShell *_dt key assign
static constexpr DWORD kRvaAssignTextString = 0x2149B0;     // FUN_1402149b0 AnmShell text object string assign
static constexpr DWORD kRvaBuildNuModuleString = 0x1BF540; // FUN_1401bf540 nu::ModuleAllocator basic_string assign
static constexpr DWORD kRvaLocalizedTextMgr = 0x27E03C0;  // DAT_1427e03c0 CGameLocalizedTextManager

void LocalizedTextLogStartup();
bool InstallLocalizedTextHooks();
void ShutdownLocalizedTextLog();
