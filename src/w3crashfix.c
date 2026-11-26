// W3CrashFix - in-memory crash fix for The Witcher 3: Wild Hunt Remastered, DX12, witcher3.exe 5.0.16.41566.
// Copyright (c) 2026 Onno Struijk (Havoc104). MIT License, see LICENSE.
//
// The fix (THE FIX below): descriptor-page leak that ends in a crash at witcher3.exe+0x280ad3d.
// Also logs descriptor heap statistics to W3CrashFix.activity.log next to the module.
// Built as dinput8.dll (standalone proxy) or W3CrashFix.asi (for Ultimate ASI Loader). Nothing on disk is modified;
// every patch first verifies the exact game code it was written for and disables itself on any mismatch.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dbghelp.h>     // only the types of MiniDumpWriteDump; dbghelp.dll is loaded at run time, see "crash dump"

// ---------------------------------------------------------------- dinput8 proxy
static HMODULE g_real;
static FARPROC real(const char *name) {
    if (!g_real) {
        wchar_t p[MAX_PATH];
        GetSystemDirectoryW(p, MAX_PATH);
        lstrcatW(p, L"\\dinput8.dll");
        g_real = LoadLibraryW(p);
    }
    return g_real ? GetProcAddress(g_real, name) : NULL;
}
typedef HRESULT (WINAPI *DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID *o, LPUNKNOWN u) {
    DI8Create_t f = (DI8Create_t)real("DirectInput8Create");
    return f ? f(h, v, r, o, u) : E_FAIL;
}
HRESULT WINAPI Proxy_DllCanUnloadNow(void) { FARPROC f = real("DllCanUnloadNow"); return f ? ((HRESULT (WINAPI *)(void))f)() : S_FALSE; }
HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID c, REFIID r, LPVOID *o) { FARPROC f = real("DllGetClassObject"); return f ? ((HRESULT (WINAPI *)(REFCLSID, REFIID, LPVOID *))f)(c, r, o) : E_FAIL; }
HRESULT WINAPI Proxy_DllRegisterServer(void) { FARPROC f = real("DllRegisterServer"); return f ? ((HRESULT (WINAPI *)(void))f)() : E_FAIL; }
HRESULT WINAPI Proxy_DllUnregisterServer(void) { FARPROC f = real("DllUnregisterServer"); return f ? ((HRESULT (WINAPI *)(void))f)() : E_FAIL; }
void *WINAPI Proxy_GetdfDIJoystick(void) { FARPROC f = real("GetdfDIJoystick"); return f ? ((void *(WINAPI *)(void))f)() : NULL; }

// exports are defined in dinput8.def

// ---------------------------------------------------------------- logging
static HANDLE g_log = INVALID_HANDLE_VALUE;
static DWORD g_t0;
static void logf(const char *fmt, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char buf[1024];
    SYSTEMTIME st; GetLocalTime(&st);
    DWORD up = (GetTickCount() - g_t0) / 1000;
    int n = snprintf(buf, sizeof buf, "%02d:%02d:%02d +%5lus  ", st.wHour, st.wMinute, st.wSecond, (unsigned long)up);
    va_list ap; va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\r'; buf[n++] = '\n';
    DWORD w; WriteFile(g_log, buf, n, &w, NULL);   // unbuffered: survives a crash
}

// ---------------------------------------------------------------- heap tracking
typedef struct { void *p; int type, flags, num; } Ent;
#define CAP (1 << 17)
static Ent g_tab[CAP];
static SRWLOCK g_lk = SRWLOCK_INIT;
#define TOMB ((void *)1)
static long long g_live[4][2], g_liveDesc[4][2], g_created[4][2], g_failed;
static long long g_peakSv[2];                        // highest live shader-visible heap count (CBV_SRV_UAV, SAMPLER)
static volatile LONG64 g_rescued, g_retiredNormal;   // fix statistics

static size_t hsh(void *p) { return (((size_t)p >> 4) * 0x9E3779B97F4A7C15ull) >> (64 - 17); }
static void tab_add(void *p, int t, int f, int n) {
    size_t i = hsh(p);
    for (int k = 0; k < CAP; k++, i = (i + 1) & (CAP - 1))
        if (!g_tab[i].p || g_tab[i].p == TOMB) { g_tab[i] = (Ent){p, t, f, n}; return; }
}
static int tab_del(void *p, Ent *out) {
    size_t i = hsh(p);
    for (int k = 0; k < CAP; k++, i = (i + 1) & (CAP - 1)) {
        if (!g_tab[i].p) return 0;
        if (g_tab[i].p == p) { *out = g_tab[i]; g_tab[i].p = TOMB; return 1; }
    }
    return 0;
}

// ---------------------------------------------------------------- video memory
static IDXGIAdapter3 *g_adapter;
static ID3D12Device *g_dev;                          // the game's D3D12 device (set by init; see "GPU state" below)
static char g_driver[64] = "unknown";                // graphics driver version (see init)
static int g_dredWanted, g_dredOn;                   // GpuCrashDetails=1 in W3CrashFix.ini / DRED really switched on in time
static int g_dumpWanted;                             // CrashDump=1 in W3CrashFix.ini (see "crash dump")
static int g_dumpDone;                               // this crash's dump was written (see "crash dump")
static void vram(char *out, size_t n) {
    DXGI_QUERY_VIDEO_MEMORY_INFO l = {0}, nl = {0};
    if (g_adapter) {
        IDXGIAdapter3_QueryVideoMemoryInfo(g_adapter, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &l);
        IDXGIAdapter3_QueryVideoMemoryInfo(g_adapter, 0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nl);
    }
    MEMORYSTATUSEX ms = { sizeof ms }; GlobalMemoryStatusEx(&ms);
    snprintf(out, n, "VRAM %llu/%llu MB, shared %llu/%llu MB, commit avail %llu MB",
             l.CurrentUsage >> 20, l.Budget >> 20, nl.CurrentUsage >> 20, nl.Budget >> 20,
             ms.ullAvailPageFile >> 20);
}

static const char *tname(int t) {
    static const char *n[] = {"CBV_SRV_UAV", "SAMPLER", "RTV", "DSV"};
    return (t >= 0 && t < 4) ? n[t] : "?";
}
static void summary(const char *why) {
    char v[256]; vram(v, sizeof v);
    AcquireSRWLockShared(&g_lk);
    logf("[%s] live shader-visible: CBV_SRV_UAV %lld heaps / %lld desc, SAMPLER %lld heaps / %lld desc | "
         "live cpu-only: CBV %lld, SMP %lld, RTV %lld, DSV %lld | created sv: %lld/%lld, failed %lld | "
         "fix: pages rescued %lld, retired normally %lld | %s",
         why, g_live[0][1], g_liveDesc[0][1], g_live[1][1], g_liveDesc[1][1],
         g_live[0][0], g_live[1][0], g_live[2][0], g_live[3][0],
         g_created[0][1], g_created[1][1], g_failed, g_rescued, g_retiredNormal, v);
    ReleaseSRWLockShared(&g_lk);
}

// ---------------------------------------------------------------- events before a crash (since 2.0.0)
// Onno, 19 Nov 2026 ("all of them", idea 5): many crashes come right after a loading screen, a fast travel, a save load
// or an alt-tab (Onno's own second crash in Nov 2026 came on fast travel). The mod can't see those directly, but it sees
// their traces: graphics memory jumps by a gigabyte or more within seconds at a load, and the game window loses or
// gets the focus at an alt-tab. Every 2 seconds the work thread looks; what it notices goes into the log as
// "event: ..." and the last 8 into the crash summary ("Before the crash:"). Reading only, no hooks.
#define EV_N 8
static struct { DWORD t; char s[120]; } g_ev[EV_N];
static volatile LONG g_evCount;
static void event_add(const char *fmt, ...) {
    LONG i = g_evCount % EV_N;
    va_list a; va_start(a, fmt);
    vsnprintf(g_ev[i].s, sizeof g_ev[i].s, fmt, a);
    va_end(a);
    g_ev[i].t = GetTickCount();
    InterlockedIncrement(&g_evCount);
    logf("event: %s", g_ev[i].s);
}
static void watch_events(void) {
    // graphics memory: a window of the last 15 samples (30 s); a change of 1 GB or more inside it is one event
    static unsigned long long mb[15]; static DWORD at[15]; static int n;
    if (g_adapter) {
        DXGI_QUERY_VIDEO_MEMORY_INFO l = {0};
        if (SUCCEEDED(IDXGIAdapter3_QueryVideoMemoryInfo(g_adapter, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &l))) {
            if (n == 15) { memmove(mb, mb + 1, 14 * sizeof mb[0]); memmove(at, at + 1, 14 * sizeof at[0]); n = 14; }
            mb[n] = l.CurrentUsage >> 20; at[n] = GetTickCount(); n++;
            int lo = 0, hi = 0;
            for (int i = 1; i < n; i++) { if (mb[i] < mb[lo]) lo = i; if (mb[i] > mb[hi]) hi = i; }
            if (mb[hi] >= mb[lo] + 1000) {
                int a = lo < hi ? lo : hi, b = lo < hi ? hi : lo, rose = lo < hi;
                // Since test49 (Onno, 20 Nov 2026: "yes"): a load climbs in steps over several seconds, and each step of
                // 1 GB or more used to be an event of its own (four lines for one loading screen in Onno's F9 test).
                // A jump in the same direction within 10 s of the previous one, while that is still the newest event,
                // now extends it: "rose from <first start> to <new end> within <whole time>".
                static int lastDir; static DWORD lastEnd, lastStart; static unsigned long long lastFrom; static LONG lastSlot = -1;
                LONG newest = g_evCount - 1;
                if (lastSlot >= 0 && newest == lastSlot && lastDir == (rose ? 1 : -1) && at[a] - lastEnd <= 10000) {
                    LONG i = lastSlot % EV_N;
                    snprintf(g_ev[i].s, sizeof g_ev[i].s, "graphics memory %s from %llu to %llu MB within %lu s (a loading screen, fast travel, save load or menu?)",
                             rose ? "rose" : "fell", lastFrom, mb[b], (unsigned long)((at[b] - lastStart) / 1000));
                    g_ev[i].t = GetTickCount();
                    logf("event (continued): %s", g_ev[i].s);
                } else {
                    event_add("graphics memory %s from %llu to %llu MB within %lu s (a loading screen, fast travel, save load or menu?)",
                              rose ? "rose" : "fell", mb[a], mb[b], (unsigned long)((at[b] - at[a]) / 1000));
                    lastSlot = g_evCount - 1; lastFrom = mb[a]; lastStart = at[a];
                }
                lastDir = rose ? 1 : -1; lastEnd = at[b];
                mb[0] = mb[n - 1]; at[0] = at[n - 1]; n = 1;   // start a new window
            }
        }
    }
    // the game window in front or not (alt-tab, another program, the game's own crash dialog)
    static int was = -1;
    DWORD pid = 0; HWND fg = GetForegroundWindow();
    if (fg) GetWindowThreadProcessId(fg, &pid);
    int now = fg && pid == GetCurrentProcessId();
    if (was != -1 && now != was)
        event_add(now ? "the game window came to the front" : "the game window went to the background (alt-tab or another window)");
    was = now;
}
// "Before the crash:" for the summary: the last events, newest first, with how long before the crash they were
static void fmt_events(char *out, int sz) {
    LONG c = g_evCount; int n = 0; DWORD now = GetTickCount();
    out[0] = 0;
    for (int k = 0; k < EV_N && k < c && n < sz - 1; k++) {
        LONG i = (c - 1 - k) % EV_N;
        DWORD ago = (now - g_ev[i].t) / 1000;
        int w = ago < 120 ? snprintf(out + n, sz - n, "%s%lu s ago: %s", k ? "; " : "", (unsigned long)ago, g_ev[i].s)
                          : snprintf(out + n, sz - n, "%s%lu min ago: %s", k ? "; " : "", (unsigned long)(ago / 60), g_ev[i].s);
        if (w < 0) break;
        n += w < sz - n ? w : sz - n - 1;
    }
    if (!out[0]) snprintf(out, sz, "nothing noticed (no loading jump in graphics memory, no alt-tab)");
}

// ---------------------------------------------------------------- the game's render settings (since 2.0.0)
// Onno, 20 Nov 2026 ("Yes, write it"), after the check of all crash reports: the game's descriptor page allocator (the
// code with the leak) ran in only one player's sessions, and fast on Onno's PC with path tracing; everywhere else it
// never needed a page. Ray tracing or path tracing is the likely difference, but the logs did not say which settings
// were on. So the mod now logs them: it reads the game's own settings file, Documents\The Witcher 3\dx12user.settings,
// and writes the lines that matter (ray tracing, path tracing, upscaler, frame generation, resolution, screen mode) to
// the log as "game settings:", and into the crash summary. Reading only; the file's path is not logged (it holds the
// Windows user name). It is read at start and again every 30 s when the file changed (the game writes it when the
// player changes a setting), never during a crash.
static char g_gameSettings[512] = "not read yet";
static void game_settings(void) {
    static wchar_t path[MAX_PATH]; static int tried; static FILETIME seen;
    if (!tried) {
        tried = 1;
        typedef HRESULT (WINAPI *SHGFP_t)(HWND, int, HANDLE, DWORD, LPWSTR);
        HMODULE sh = LoadLibraryW(L"shell32.dll");   // on the work thread, never in DllMain
        SHGFP_t gfp = sh ? (SHGFP_t)GetProcAddress(sh, "SHGetFolderPathW") : NULL;
        wchar_t docs[MAX_PATH];
        if (gfp && SUCCEEDED(gfp(NULL, 0x0005 /* CSIDL_PERSONAL: Documents, also when moved to OneDrive */, NULL, 0, docs))
            && lstrlenW(docs) < MAX_PATH - 48) { lstrcpyW(path, docs); lstrcatW(path, L"\\The Witcher 3\\dx12user.settings"); }
    }
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!path[0] || !GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) {
        if (seen.dwLowDateTime != 1) {
            lstrcpyA(g_gameSettings, "not found (no Documents\\The Witcher 3\\dx12user.settings)");
            logf("game settings: %s", g_gameSettings);
            seen.dwLowDateTime = 1; seen.dwHighDateTime = 0;
        }
        return;
    }
    if (!CompareFileTime(&fa.ftLastWriteTime, &seen)) return;    // unchanged since the last look
    int first = !seen.dwLowDateTime && !seen.dwHighDateTime;
    seen = fa.ftLastWriteTime;
    static char buf[65536];
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got = 0;
    if (!ReadFile(f, buf, sizeof buf - 1, &got, NULL)) got = 0;
    CloseHandle(f);
    buf[got] = 0;
    static const char *const keys[] = { "EnableRT", "PTEnable", "EnableRtRadiance", "RTAOEnabled", "EnableDLSSRR", "DLSSQuality",
        "DLSSGMode", "DLSSGNumFramesToGenerate", "FSR2Quality", "FSRFramegen", "XESSQuality", "XessFrameGeneration",
        "Resolution", "FullScreenMode" };
    char vals[sizeof keys / sizeof keys[0]][40] = {{0}};
    for (char *l = buf; *l; ) {
        char *e = l; while (*e && *e != '\n' && *e != '\r') e++;
        char c = *e; *e = 0;
        char *eq = strchr(l, '=');
        if (eq && *l != '[' && *l != ';')
            for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++)
                if ((size_t)(eq - l) == strlen(keys[k]) && !strncmp(l, keys[k], eq - l)) {
                    const char *v = eq + 1; if (*v == '"') v++;
                    snprintf(vals[k], sizeof vals[k], "%s", v);
                    char *q = strchr(vals[k], '"'); if (q) *q = 0;
                }
        *e = c; l = e; while (*l == '\n' || *l == '\r') l++;
    }
    // Built in a buffer of its own first: a 2.0.0 log (20 Nov 2026) showed "game settings changed" every 30 s with the
    // same values, so something on that PC rewrites the file without changing it. Only a real change of these values is
    // logged now (Onno, 20 Nov 2026: "game settings fix?").
    static char now[sizeof g_gameSettings];
    int on = !strcmp(vals[0], "true"), pt = !strcmp(vals[1], "true");
    int n = snprintf(now, sizeof now, "ray tracing %s, path tracing %s |",
                     on ? "ON" : vals[0][0] ? "off" : "?", pt ? "ON" : vals[1][0] ? "off" : "?");
    for (size_t k = 0; k < sizeof keys / sizeof keys[0] && n < (int)sizeof now - 60; k++)
        if (vals[k][0]) n += snprintf(now + n, sizeof now - n, " %s=%s", keys[k], vals[k]);
    if (!first && !strcmp(now, g_gameSettings)) return;           // file rewritten, values the same
    lstrcpyA(g_gameSettings, now);
    logf("game settings%s: %s", first ? "" : " changed", g_gameSettings);
}

// ---------------------------------------------------------------- other DLL and ASI files in the game folder (since 2.0.0)
// Onno, 20 Nov 2026: "why would they need to put clean in if we can easily tell by the logs. we need this to work with
// as little steps as possible for them". The log already lists the in-game mods; this lists the rest: every DLL or ASI
// file loaded from the game's own folder (and its subfolders) that is not one of the game's own files, e.g. an ASI
// loader, ReShade, a script extender, ASI mods, DLSS replacements. Logged as "other files:" at start and whenever the
// list grows, and put into the crash summary. A report then shows by itself whether the game was clean. The game's
// own files are taken from the 5.0.16 Steam folder; a file the game adds later would show up here until it is added.
static char g_otherFiles[1024] = "not read yet";
static void mod_version(HMODULE m, char *out, int sz);   // see "the call stack" below
static const char *const g_gameOwn[] = { "witcher3.exe", "amd_ags_x64.dll", "amd_fidelityfx_framegeneration_dx12.dll",
        "amd_fidelityfx_loader_dx12.dll", "amd_fidelityfx_upscaler_dx12.dll", "crashreporter.resources.dll", "cudart64_50_35.dll",
        "d3d12core.dll", "d3d12sdklayers.dll", "d3d12stateobjectcompiler.dll", "d3dcompiler_47.dll", "dxcompiler.dll", "dxil.dll",
        "galaxy64.dll", "gfsdk_aftermath_lib.x64.dll", "gfsdk_hairworks.win64.dll", "gfsdk_ssao.win64.dll", "gfsdk_ssao_d3d12.win32.dll",
        "gfsdk_ssao_d3d12.win64.dll", "glew32.dll", "grb_1_1_api3_x64.dll", "igxess.dll", "libcurl.dll", "libxell.dll", "libxess.dll",
        "libxess_dx11.dll", "libxess_fg.dll", "msvcp110.dll", "normaliz.dll", "nvcamerasdk64.dll", "nvhair_x64.dll", "nvhairext_x64.dll",
        "nvngx_dlss.dll", "nvngx_dlssd.dll", "nvngx_dlssg.dll", "physx3common_x64.dll", "physx3_x64.dll", "physx3cooking_x64.dll",
        "physx3characterkinematic_x64.dll", "redgalaxy64.dll", "redtelemetrylib.dll", "sl.common.dll", "sl.dlss.dll", "sl.dlss_d.dll",
        "sl.dlss_g.dll", "sl.interposer.dll", "sl.pcl.dll", "sl.reflex.dll", "steam_api64.dll", "wldap32.dll", "xaudio2_9redist.dll",
        "xefx.dll", "xefx_loader.dll", "eossdk-win64-shipping.dll" };
static int game_own(const char *file) {
    for (size_t k = 0; k < sizeof g_gameOwn / sizeof g_gameOwn[0]; k++) if (!_stricmp(file, g_gameOwn[k])) return 1;
    return 0;
}
static void other_files(void) {
    typedef BOOL (WINAPI *EPM_t)(HANDLE, HMODULE *, DWORD, DWORD *);
    static EPM_t epm; static int tried, lastCount = -1;
    if (!tried) { tried = 1; epm = (EPM_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32EnumProcessModules"); }
    if (!epm) return;
    wchar_t root[MAX_PATH]; DWORD rl = GetModuleFileNameW(NULL, root, MAX_PATH);
    if (!rl || rl >= MAX_PATH) return;
    for (int up = 0; up < 3; up++) { wchar_t *s = wcsrchr(root, L'\\'); if (s) *s = 0; }   // ...\bin\x64_dx12\witcher3.exe -> game folder
    size_t rootLen = wcslen(root);
    HMODULE mods[512], self = NULL; DWORD need = 0;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(void *)other_files, &self);
    if (!epm(GetCurrentProcess(), mods, sizeof mods, &need)) return;
    int n = (int)((need < sizeof mods ? need : sizeof mods) / sizeof mods[0]), count = 0, o = 0;
    char out[1024]; out[0] = 0;
    for (int i = 0; i < n; i++) {
        wchar_t p[MAX_PATH]; DWORD pl = GetModuleFileNameW(mods[i], p, MAX_PATH);
        if (!pl || pl >= MAX_PATH || mods[i] == self || _wcsnicmp(p, root, rootLen) || p[rootLen] != L'\\') continue;
        char rel[MAX_PATH * 3]; WideCharToMultiByte(CP_UTF8, 0, p + rootLen + 1, -1, rel, sizeof rel, NULL, NULL);
        const char *file = rel; for (const char *c = rel; *c; c++) if (*c == '\\' || *c == '/') file = c + 1;
        if (game_own(file) || !_strnicmp(rel, "bin\\ddi\\", 8)) continue;   // bin\ddi holds the game's own store file (Steam.dll, by CD PROJEKT RED)
        char ver[48]; mod_version(mods[i], ver, sizeof ver);
        count++;
        if (o < (int)sizeof out - 120) o += snprintf(out + o, sizeof out - o, "%s%s%s%s", count > 1 ? ", " : "", rel, ver[0] ? " " : "", ver);
    }
    if (count == lastCount) return;
    lastCount = count;
    if (!count) lstrcpyA(g_otherFiles, "none (only the game's own files and W3CrashFix)");
    else snprintf(g_otherFiles, sizeof g_otherFiles, "%d: %s", count, out);
    logf("other files: DLL and ASI files in the game folder that are not the game's own: %s", g_otherFiles);
}

// ---------------------------------------------------------------- hooks
typedef HRESULT (STDMETHODCALLTYPE *CDH_t)(ID3D12Device *, const D3D12_DESCRIPTOR_HEAP_DESC *, REFIID, void **);
typedef ULONG (STDMETHODCALLTYPE *Rel_t)(ID3D12DescriptorHeap *);
static CDH_t o_CreateDescriptorHeap;
static Rel_t o_HeapRelease;

// Names the code that called us as "file.dll+0xOFFSET", so a log shows which DLL created a heap (the game itself,
// the graphics driver, an upscaler, an overlay, ...). Falls back to the raw address if the module can't be found.
static void where(void *addr, char *out, size_t n) {
    HMODULE m = NULL; char path[MAX_PATH];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &m) && m) {
        DWORD len = GetModuleFileNameA(m, path, sizeof path);
        if (len > 0 && len < sizeof path) {
            const char *file = path;
            for (const char *c = path; *c; c++) if (*c == '\\' || *c == '/') file = c + 1;
            // NVIDIA's downloaded DLSS files (...\NGX\models\<feature>\versions\<n>\files\160_E658700.bin) have the
            // same file name for every feature, so the feature folder is put in front: "dlssg\160_E658700.bin".
            char name[128]; const char *base = file;
            static const char ngx[] = "\\NGX\\models\\";
            for (const char *c = path; c < file; c++) {
                if (_strnicmp(c, ngx, sizeof ngx - 1)) continue;
                const char *feat = c + sizeof ngx - 1, *end = feat;
                while (*end && *end != '\\' && *end != '/') end++;
                if (end > feat && end < file) { snprintf(name, sizeof name, "%.*s\\%s", (int)(end - feat), feat, file); base = name; }
                break;
            }
            snprintf(out, n, "%s+0x%llX", base, (unsigned long long)((char *)addr - (char *)m));
            // First time a file shows up: log its full path once. NVIDIA's downloaded DLSS files all share one
            // file name (e.g. 160_E658700.bin), so only the folder tells which feature it is. The user's profile
            // folder is replaced by %USERPROFILE% to keep the Windows user name out of the log.
            static PVOID seen[64];
            for (int i = 0; i < 64; i++) {
                PVOID was = InterlockedCompareExchangePointer(&seen[i], m, NULL);
                if (was == (PVOID)m) break;
                if (was == NULL) {
                    char prof[MAX_PATH]; DWORD pl = GetEnvironmentVariableA("USERPROFILE", prof, sizeof prof);
                    if (pl > 0 && pl < sizeof prof && !_strnicmp(path, prof, pl)) logf("module: %s = %%USERPROFILE%%%s", base, path + pl);
                    else logf("module: %s = %s", base, path);
                    break;
                }
            }
            return;
        }
    }
    snprintf(out, n, "%p", addr);
}

static ULONG STDMETHODCALLTYPE h_HeapRelease(ID3D12DescriptorHeap *self) {
    ULONG r = o_HeapRelease(self);
    if (r == 0) {
        Ent e;
        AcquireSRWLockExclusive(&g_lk);
        if (tab_del(self, &e)) { g_live[e.type][e.flags]--; g_liveDesc[e.type][e.flags] -= e.num; }
        ReleaseSRWLockExclusive(&g_lk);
    }
    return r;
}

static HRESULT STDMETHODCALLTYPE h_CreateDescriptorHeap(ID3D12Device *self, const D3D12_DESCRIPTOR_HEAP_DESC *d,
                                                        REFIID riid, void **pp) {
    HRESULT hr = o_CreateDescriptorHeap(self, d, riid, pp);
    int t = (int)d->Type & 3, sv = (d->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) ? 1 : 0;
    char caller[96]; where(__builtin_return_address(0), caller, sizeof caller);
    if (SUCCEEDED(hr) && pp && *pp) {
        AcquireSRWLockExclusive(&g_lk);
        tab_add(*pp, t, sv, (int)d->NumDescriptors);
        g_live[t][sv]++; g_liveDesc[t][sv] += d->NumDescriptors; g_created[t][sv]++;
        if (sv && t < 2 && g_live[t][sv] > g_peakSv[t]) g_peakSv[t] = g_live[t][sv];
        long long live = g_live[t][sv], desc = g_liveDesc[t][sv], made = g_created[t][sv];
        ReleaseSRWLockExclusive(&g_lk);
        if (sv) logf("create %s n=%u sv -> live %lld heaps / %lld desc (total created %lld) caller=%s",
                     tname(t), d->NumDescriptors, live, desc, made, caller);
    } else {
        g_failed++;
        HRESULT rr = ID3D12Device_GetDeviceRemovedReason(self);
        logf("!!!! CreateDescriptorHeap FAILED hr=0x%08lX type=%s n=%u flags=%u caller=%s deviceRemovedReason=0x%08lX",
             (unsigned long)hr, tname(t), d->NumDescriptors, d->Flags, caller, (unsigned long)rr);
        summary("FAILURE");
    }
    return hr;
}

static int patch(void **slot, void *fn, void **orig) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return 0;
    *orig = *slot;
    InterlockedExchangePointer(slot, fn);
    VirtualProtect(slot, sizeof(void *), old, &old);
    return 1;
}

// ---------------------------------------------------------------- THE FIX
// witcher3.exe 5.0.16 (DX12) shader-visible descriptor page allocator, w3+0x2809760 ("ensure room for n").
// When the current page is full it is moved to the retired list. Pages leave the retired list only when a
// GPU-fence queue entry for them completes while they are NOT current. A page whose last fence entry already
// completed while it was still current (common when the GPU keeps up with the CPU) is retired with
// pending==0 and usesThisFrame==0, never gets another fence entry, and is orphaned forever. One new
// 1024-descriptor heap leaks per page fill until CreateDescriptorHeap returns E_OUTOFMEMORY (~4000 heaps),
// the game stores a NULL page and crashes at w3+0x280ad3d.
// Fix: reimplement the function; a retired page that is already idle goes straight to the free list.
enum {
    RVA_ENSURE = 0x2809760, RVA_INVALIDATE = 0x2809bb0, RVA_CREATEPAGE = 0x2809ef0, RVA_VECGROW = 0x28080d0,
    RVA_FORCENEW = 0x2809810,
    A_USED = 0x0, A_CUR = 0x8, A_RETIRED = 0xe8, A_FREE = 0x108, A_CAP = 0x130,   // allocator fields
    P_PENDING = 0x8, P_FRAMEUSES = 0xc,                                            // page fields
    V_DATA = 0x0, V_COUNT = 0x10, V_CAPACITY = 0x18,                                // game vector
};
static const struct { unsigned rva; const char *name, *hex; } g_sig[] = {
    {RVA_ENSURE,     "ensure",     "40534883ec200311488bd93b91300100000f8e8c00000048"},
    {RVA_FORCENEW,   "forcenew",   "48895c2408574883ec20488db9e8000000488bd9488b4710"},
    {RVA_INVALIDATE, "invalidate", "4881c15801000041b8040000004533c9488b41f84885c074"},
    {RVA_CREATEPAGE, "createpage", "48895c2410574883ec308b81300100004c8d4c2440894424"},
    {RVA_VECGROW,    "vecgrow",    "48895c241048896c24184889742420574883ec304c8b4118"},
};
// Finding the code (Onno, 19 Nov 2026: "good idea"). A game update that only moves code around should not need a new
// version of the mod. So each function is checked by a fingerprint of its WHOLE body, not only its first 24 bytes,
// and if it is not at its 5.0.16 place, the mod looks for it in the rest of the game's code.
// The fingerprint: the body's length and a 64-bit FNV-1a hash of all its bytes, in which the 4-byte fields that
// change whenever code moves, even when the code itself is the same, count as 0: the distance of a call or jump to
// code outside the function, and of a reference to the game's data. Everything else counts: every instruction, every
// field offset in the allocator and page structures, every jump inside the function. The table holds no game code,
// only these numbers. The fix is switched on only when ALL of this holds, and otherwise stays off (FIX DISABLED, with
// the diag: lines as before):
//   1. each of the five functions is at its 5.0.16 place with the same fingerprint, or else at exactly ONE other place
//      in the game's code with the same first 24 bytes and the same fingerprint (two or more such places: off);
//   2. the calls between them point at each other: ensure and forcenew must call vecgrow, invalidate and createpage
//      exactly where those were found.
// The crash function (crashfn, which holds the crash address) is looked for the same way, only for the verdict of the
// crash report; if it is not found the fix still works, and the verdict says it cannot tell which crash it was.
// Made with Claude's script from the 5.0.16 executable (Steam), 19 Nov 2026.
enum { F_ENSURE, F_FORCENEW, F_INVALIDATE, F_CREATEPAGE, F_VECGROW, F_CRASHFN, F_COUNT, F_NEEDED = F_CRASHFN };
static const struct { const char *name; unsigned rva, len; unsigned long long fnv; int nmask; unsigned short mask[5]; } g_body[F_COUNT] = {
    {"ensure",     RVA_ENSURE,     0xA9,  0x45AA40975DA7D0B3ULL, 3, {0x36, 0x7D, 0x92}},
    {"forcenew",   RVA_FORCENEW,   0x9C,  0x7A8B08B99E5FF502ULL, 3, {0x27, 0x69, 0x83}},
    {"invalidate", RVA_INVALIDATE, 0x222, 0xC739991A451FC58AULL, 0, {0}},
    {"createpage", RVA_CREATEPAGE, 0x103, 0xCBE81B8F6D7434B3ULL, 2, {0x1C, 0x9D}},
    {"vecgrow",    RVA_VECGROW,    0xE6,  0x6894705B3814E5EAULL, 4, {0x22, 0x77, 0x9B, 0xB5}},
    {"crashfn",    0x280ABA0,      0x1EB, 0x7F902515BEAF8820ULL, 5, {0x48, 0xC4, 0xDA, 0x159, 0x18A}},
};
static const char g_crashfnStart[] = "48895c240844894424184889542410555657415441554156";   // first 24 bytes of crashfn
#define CRASHFN_CRASH_AT 0x19D                       // the crash address inside crashfn (5.0.16: w3+0x280AD3D)
static const struct { int from; unsigned at; int to; } g_xref[] = {   // call rel32 at body offset 'at' must reach 'to'
    {F_ENSURE, 0x36, F_VECGROW}, {F_ENSURE, 0x7D, F_INVALIDATE}, {F_ENSURE, 0x92, F_CREATEPAGE},
    {F_FORCENEW, 0x27, F_VECGROW}, {F_FORCENEW, 0x69, F_INVALIDATE}, {F_FORCENEW, 0x83, F_CREATEPAGE},
    {F_CRASHFN, 0x159, F_ENSURE},
};
static unsigned g_at[F_COUNT];                       // where each function was found in this game (0 = not found)
static unsigned g_crashRva;                          // the crash address in this game (0 = crash function not found)
static int g_moved;                                  // the code was found at other places than in 5.0.16
static unsigned char *g_base;
static wchar_t g_dir[MAX_PATH];                      // folder of the mod, with trailing backslash
static int g_buildOk, g_fixInstalled;                // game code recognised / detour installed
static volatile LONG g_fixActive;                    // detour installed AND the license accepted: pages are rescued
// The license dialog (see "license dialog" below) is compiled in with -DW3_LICENSE, which build.ps1 always passes.
// Built without it, g_accepted is always 1 and the mod starts working right away.
static volatile LONG g_accepted = 1;                 // the user accepted the license (now or in an earlier session)
#ifdef W3_CRASHTEST
static volatile LONG g_testCrash;                    // 1 = real heap crash requested, 2 = done, 3 = plain crash, 4 = GPU device removed (F9), 5 / 6 = known renderer crash (F10 / F11)
#endif
typedef void (*Invalidate_t)(unsigned char *alloc, void *page);
typedef void *(*CreatePage_t)(unsigned char *alloc);
typedef void (*VecGrow_t)(unsigned char *vec, long long elemSize);

#define F64(p, o) (*(long long *)((unsigned char *)(p) + (o)))
#define F32(p, o) (*(int *)((unsigned char *)(p) + (o)))
#define FPTR(p, o) (*(void **)((unsigned char *)(p) + (o)))

static void vec_push(unsigned char *v, void *x) {
    if (!(F64(v, V_CAPACITY) > F64(v, V_COUNT))) ((VecGrow_t)(g_base + g_at[F_VECGROW]))(v, 8);
    long long n = F64(v, V_COUNT);
    F64(v, V_COUNT) = n + 1;
    ((void **)FPTR(v, V_DATA))[n] = x;
}

static void fixed_ensure(unsigned char *a, int n) {
#ifdef W3_CRASHTEST
    // Test builds only: reproduce the real crash. The game stores a NULL page when it can't create another heap
    // and then crashes at w3+0x280ad3d; doing the same here gives that exact crash on demand.
    if (InterlockedCompareExchange(&g_testCrash, 2, 1) == 1) {
        logf("TEST: storing a NULL page, as the game does when it runs out of descriptor heaps");
        FPTR(a, A_CUR) = NULL; F32(a, A_USED) = 0;
        return;
    }
#endif
    if (F32(a, A_USED) + n <= F32(a, A_CAP)) return;
    void *old = FPTR(a, A_CUR);
    if (old) {
#ifdef W3_LICENSE
        // Until the license is accepted (g_fixActive == 0) this function does exactly what the game's own code
        // does: every full page goes to the retired list.
        if (g_fixActive && F32(old, P_PENDING) == 0 && F32(old, P_FRAMEUSES) == 0) {
#else
        if (F32(old, P_PENDING) == 0 && F32(old, P_FRAMEUSES) == 0) {
#endif
            vec_push(a + A_FREE, old);           // idle: reusable right away
            InterlockedIncrement64(&g_rescued);
        } else {
            vec_push(a + A_RETIRED, old);        // in flight: normal fence-based recycling
            InterlockedIncrement64(&g_retiredNormal);
        }
    }
    void *page;
    long long nfree = F64(a, A_FREE + V_COUNT);
    if (nfree > 0) {
        page = ((void **)FPTR(a, A_FREE + V_DATA))[nfree - 1];
        F64(a, A_FREE + V_COUNT) = nfree - 1;
        ((Invalidate_t)(g_base + g_at[F_INVALIDATE]))(a, page);   // drop cached tables pointing into it
    } else {
        page = ((CreatePage_t)(g_base + g_at[F_CREATEPAGE]))(a);
    }
    FPTR(a, A_CUR) = page;
    F32(a, A_USED) = 0;
}


// ---------------------------------------------------------------- diagnostic for unsupported game builds
// When the game's code is not the build this fix was written for (another store's executable, a game update),
// nothing is patched. To make it possible to add support without having that executable, the log then describes
// the build: its PE header values, the code bytes found where each function was expected, and where in the
// executable the same code pattern does occur. Read-only; a few dozen bytes of code per function, no user data.
static int readable(const unsigned char *p, size_t n) {
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return (const unsigned char *)mi.BaseAddress + mi.RegionSize >= p + n;
}
static int hexbytes(const char *hex, unsigned char *out, int max) {
    int n = 0;
    for (; hex[0] && hex[1] && n < max; hex += 2) { char b[3] = {hex[0], hex[1], 0}; out[n++] = (unsigned char)strtoul(b, NULL, 16); }
    return n;
}
static void hexstr(const unsigned char *p, int n, char *out) {
    for (int i = 0; i < n; i++) out += sprintf(out, "%02x", p[i]);
    *out = 0;
}
// Calls cb(rva) for every occurrence of pat in the executable sections of the image; returns the count.
static int scan(const unsigned char *pat, int n, unsigned *hits, int maxhits) {
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    int count = 0;
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; s++) {
        if (!(sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const unsigned char *p = g_base + sec[s].VirtualAddress, *end = p + sec[s].Misc.VirtualSize;
        while (p < end) {                                    // walk the section one committed region at a time
            MEMORY_BASIC_INFORMATION mi;
            if (!VirtualQuery(p, &mi, sizeof mi)) break;
            const unsigned char *rend = (const unsigned char *)mi.BaseAddress + mi.RegionSize;
            if (rend > end) rend = end;
            if (mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                for (const unsigned char *q = p; q + n <= rend; q++) {
                    q = memchr(q, pat[0], (size_t)(rend - q) - n + 1);
                    if (!q) break;
                    if (!memcmp(q, pat, n)) { if (count < maxhits) hits[count] = (unsigned)(q - g_base); count++; }
                }
            p = rend;
        }
    }
    return count;
}
static void diagnose(void) {
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew);
    DWORD image = nt->OptionalHeader.SizeOfImage;
    wchar_t exe[MAX_PATH]; WIN32_FILE_ATTRIBUTE_DATA fa = {0};
    GetModuleFileNameW(NULL, exe, MAX_PATH); GetFileAttributesExW(exe, GetFileExInfoStandard, &fa);
    logf("diag: this build of the game is not supported yet. The 'diag:' lines describe it (game code locations only,");
    logf("diag: nothing personal). Posting this log on the mod page is what is needed to add support for your version.");
    logf("diag: exe timestamp 0x%08lX, image size 0x%lX, checksum 0x%lX, file size %llu",
         (unsigned long)nt->FileHeader.TimeDateStamp, (unsigned long)image, (unsigned long)nt->OptionalHeader.CheckSum,
         ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow);
    int moved = 0, total = (int)(sizeof g_sig / sizeof g_sig[0]);
    for (int i = 0; i < total; i++) {
        unsigned char pat[32], *at = g_base + g_sig[i].rva; char hx[2 * 48 + 1];
        int n = hexbytes(g_sig[i].hex, pat, sizeof pat);
        if ((unsigned long long)g_sig[i].rva + 48 <= image && readable(at, 48)) {
            if (!memcmp(at, pat, n)) { logf("diag: %s: as expected at w3+0x%X", g_sig[i].name, g_sig[i].rva); moved++; continue; }
            hexstr(at, 48, hx); logf("diag: %s: not at w3+0x%X, found there: %s", g_sig[i].name, g_sig[i].rva, hx);
        } else logf("diag: %s: w3+0x%X is outside this executable", g_sig[i].name, g_sig[i].rva);
        unsigned hits[4]; int c = scan(pat, n, hits, 4);
        if (c > 0) {
            char where[80] = ""; for (int k = 0; k < c && k < 4; k++) sprintf(where + strlen(where), " w3+0x%X", hits[k]);
            logf("diag: %s: same code found %d time(s) at:%s", g_sig[i].name, c, where);
            if (c == 1) moved++;
        } else {
            c = scan(pat, 12, hits, 3);                      // function start only: the body differs in this build
            logf("diag: %s: same code not found; %d place(s) start the same way", g_sig[i].name, c);
            for (int k = 0; k < c && k < 3; k++)
                if (readable(g_base + hits[k], 48)) { hexstr(g_base + hits[k], 48, hx); logf("diag: %s:   w3+0x%X: %s", g_sig[i].name, hits[k], hx); }
        }
    }
    logf("diag: %d of %d functions located%s", moved, total,
         moved == total ? " - this build can probably be supported with a small update" : "");
}

// Finding the code (see g_body): the fingerprint of the function body at p, compared with function i's.
static int body_ok(const unsigned char *p, int i) {
    unsigned len = g_body[i].len;
    if (!readable(p, len)) return 0;
    unsigned long long h = 0xcbf29ce484222325ULL;               // FNV-1a 64
    for (unsigned k = 0; k < len; k++) {
        unsigned char b = p[k];
        for (int m = 0; m < g_body[i].nmask; m++)
            if (k >= g_body[i].mask[m] && k < g_body[i].mask[m] + 4u) { b = 0; break; }
        h = (h ^ b) * 0x100000001b3ULL;
    }
    return h == g_body[i].fnv;
}
// Where function i is in this game: its 5.0.16 place if the body is the same there, or else the one and only other
// place in the game's code with the same first 24 bytes and the same body; 0 if there is none or more than one.
static unsigned find_fn(int i, DWORD image) {
    unsigned r = g_body[i].rva;
    if ((unsigned long long)r + g_body[i].len <= image && body_ok(g_base + r, i)) return r;
    unsigned char pat[24];
    int n = hexbytes(i < F_NEEDED ? g_sig[i].hex : g_crashfnStart, pat, sizeof pat);
    unsigned hits[16], found = 0;
    int c = scan(pat, n, hits, 16), same = 0;
    if (c > 16) return 0;                                       // too many candidates to tell: off
    for (int k = 0; k < c; k++)
        if ((unsigned long long)hits[k] + g_body[i].len <= image && body_ok(g_base + hits[k], i)) { found = hits[k]; same++; }
    return same == 1 ? found : 0;
}
// Does the call whose 4-byte distance sits at offset 'at' of the function at rva 'from' reach rva 'to'?
static int calls_to(unsigned from, unsigned at, unsigned to) {
    int rel; memcpy(&rel, g_base + from + at, 4);
    return (long long)from + at + 4 + rel == (long long)to;
}

static void install_fix(void) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!wcsstr(exe, L"witcher3.exe")) { logf("fix: not witcher3.exe - disabled"); return; }
    g_base = (unsigned char *)GetModuleHandleW(NULL);
    DWORD image = ((IMAGE_NT_HEADERS64 *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew))->OptionalHeader.SizeOfImage;
    // See "Finding the code" above: all five functions, by their whole body, and the calls between them.
    for (int i = 0; i < F_NEEDED; i++) {
        g_at[i] = find_fn(i, image);
        if (!g_at[i]) {
            logf("fix: %s (w3+0x%X in 5.0.16) not found with the same code in this game build - FIX DISABLED (probe still logs)",
                 g_body[i].name, g_body[i].rva);
            diagnose();
            return;
        }
        if (g_at[i] != g_body[i].rva) g_moved = 1;
    }
    for (unsigned k = 0; k < sizeof g_xref / sizeof g_xref[0]; k++)
        if (g_xref[k].from < F_NEEDED && !calls_to(g_at[g_xref[k].from], g_xref[k].at, g_at[g_xref[k].to])) {
            logf("fix: %s does not call %s where that was found - FIX DISABLED (probe still logs)",
                 g_body[g_xref[k].from].name, g_body[g_xref[k].to].name);
            diagnose();
            return;
        }
    // The crash function: only for the verdict of a crash report.
    g_at[F_CRASHFN] = find_fn(F_CRASHFN, image);
    if (g_at[F_CRASHFN] && calls_to(g_at[F_CRASHFN], 0x159, g_at[F_ENSURE])) g_crashRva = g_at[F_CRASHFN] + CRASHFN_CRASH_AT;
    else { g_at[F_CRASHFN] = 0; logf("fix: the function that holds the crash address was not found; the crash report cannot tell which crash it was"); }
    if (g_moved) {
        logf("fix: this game build is not 5.0.16 as W3CrashFix knows it, but the code the fix needs is the same, at other places:");
        for (int i = 0; i < F_COUNT; i++)
            if (g_at[i]) logf("fix:   %s at w3+0x%X (5.0.16: w3+0x%X)", g_body[i].name, g_at[i], g_body[i].rva);
    }
    g_buildOk = 1;
#ifdef W3_CRASHTEST
    {   // Test builds only: an empty file W3CrashFix.nofix next to the mod leaves the fix out, so the real crash
        // can be awaited with the crash report active. A release build ignores such a file.
        wchar_t off[MAX_PATH]; lstrcpyW(off, g_dir); lstrcatW(off, L"W3CrashFix.nofix");
        if (GetFileAttributesW(off) != INVALID_FILE_ATTRIBUTES) {
            logf("fix: NOT installed because W3CrashFix.nofix exists (test build) - the game will leak descriptor heaps and crash");
            return;
        }
    }
#endif
    unsigned char jmp[12] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};   // mov rax, imm64 ; jmp rax
    void *target = (void *)fixed_ensure;
    memcpy(jmp + 2, &target, 8);
    DWORD old;
    unsigned char *ens = g_base + g_at[F_ENSURE];
    if (VirtualProtect(ens, sizeof jmp, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy(ens, jmp, sizeof jmp);
        VirtualProtect(ens, sizeof jmp, old, &old);
        FlushInstructionCache(GetCurrentProcess(), ens, sizeof jmp);
        g_fixInstalled = 1;
        if (g_accepted) {
            g_fixActive = 1;
            logf("fix: descriptor page recycling fix ACTIVE (w3+0x%X detoured)", g_at[F_ENSURE]);
        } else logf("fix: ready, but switched off until the license is accepted (the game behaves as without the mod)");
    } else logf("fix: VirtualProtect failed %lu - FIX DISABLED", GetLastError());
}

// ---------------------------------------------------------------- crash report
// When the game crashes, the mod writes what crashed into the log, saves the session as W3CrashFix.crash.log with a
// short summary on top, and writes W3CrashFix-report.html next to it and opens that page. The page shows the summary
// and two ways to report it (a pre-filled GitHub issue, or copy the summary and open the Nexus page). Nothing is
// sent by the mod itself. OpenReportPage=0 in W3CrashFix.ini stops the page from opening.
// <mod version>-<game version it is built for>; the game part changes whenever the fix is ported to a new patch
#define W3CF_VERSION "2.3.0-5.0.16"
static const char *g_flavor = "";
static char g_adapterName[192] = "unknown";
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter;
static volatile LONG g_crashed, g_noticed;
static char g_crSum[12288 + 65536], g_crEsc[6144], g_crBuf[65536];   // the summary holds the call stack (g_crStack)

static const char *exc_name(DWORD c) {
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
    case EXCEPTION_IN_PAGE_ERROR:         return "in-page error";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "division by zero";
    case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case 0xC0000409:                      return "fail-fast / stack buffer overrun";
    case 0xC0000374:                      return "heap corruption";
    case 0xE06D7363:                      return "unhandled C++ exception";
    default:                              return "other";
    }
}
static int fatal_looking(DWORD c) { return c != 0xE06D7363 && strcmp(exc_name(c), "other") != 0; }

static void html_esc(const char *s, char *out, int max) {
    int n = 0;
    for (; *s && n < max - 7; s++) {
        if (*s == '&') n += sprintf(out + n, "&amp;");
        else if (*s == '<') n += sprintf(out + n, "&lt;");
        else if (*s == '>') n += sprintf(out + n, "&gt;");
        else if (*s == '"') n += sprintf(out + n, "&quot;");
        else out[n++] = *s;
    }
    out[n] = 0;
}
static void put(HANDLE f, const char *s) { DWORD w; WriteFile(f, s, (DWORD)strlen(s), &w, NULL); }
static HANDLE create_in_dir(const wchar_t *name, wchar_t *path) {
    lstrcpyW(path, g_dir); lstrcatW(path, name);
    return CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

// The processor's registers at the moment of the crash, as Windows hands them to the exception handler. A value
// that points into the game's executable is written as w3+0x... (the same on every PC); everything else as a
// plain number. Together with the crash address this shows, for example, which pointer was empty.
static char g_crRegs[700];
static void fmt_regs(const CONTEXT *c) {
    static const char *nm[16] = {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
    const DWORD64 v[16] = {c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->Rbp, c->Rsp, c->R8, c->R9, c->R10, c->R11, c->R12, c->R13, c->R14, c->R15};
    DWORD64 lo = (DWORD64)(ULONG_PTR)g_base, hi = lo;
    if (g_base) hi = lo + ((IMAGE_NT_HEADERS64 *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew))->OptionalHeader.SizeOfImage;
    int n = 0;
    g_crRegs[0] = 0;
    for (int i = 0; i < 16 && n < (int)sizeof g_crRegs - 40; i++) {
        if (i == 8) n += snprintf(g_crRegs + n, sizeof g_crRegs - n, "\r\n           ");
        if (v[i] >= lo && v[i] < hi) n += snprintf(g_crRegs + n, sizeof g_crRegs - n, "%s=w3+0x%llX ", nm[i], (unsigned long long)(v[i] - lo));
        else n += snprintf(g_crRegs + n, sizeof g_crRegs - n, "%s=0x%llX ", nm[i], (unsigned long long)v[i]);
    }
}

// The call stack (since 1.6.2): how the game got to the crash. The first entry is the crash address itself, every next
// one the place that called the one before (a return address), as file+0xOFFSET. It is found by unwinding from the
// registers of the crash with the unwind data every 64-bit Windows program carries; nothing is loaded and nothing is
// written but this text. Many crashes stop in a small helper that many places use (for example the game's own "stop
// here" when a check fails), and only the callers show which of those places it was. Safety on the crash path: no
// heap, no waiting, and the stack pointer must stay inside this thread's own stack and move up at every step; on any
// doubt the walk stops and the summary shows what was found so far.
// No limit on the number of steps (Onno, 19 Nov 2026: "make this unlimited"; 1.6.2 to 1.7.0 stopped after 16, which cut
// off the stacks of the GPU crash at witcher3.exe+0x1F103A3). The walk still always ends: every step must move the
// stack pointer up by at least 8 bytes and stay below the top of the stack, so it ends at the top at the latest. The
// text has room for some 500 entries; on a stack deeper than that, the rest is still counted and the last entry says
// how many more there were.
// Since 2.0.0 (Onno, 19 Nov 2026, "all of them", ideas 4 and 6):
//  - A crash at address 0 (the game called through an empty pointer) used to give no call stack at all, because the
//    walk started at address 0 and stopped there. Such a call leaves its return address on top of the stack, so the
//    walk now goes on from there like for any function without unwind data: the second entry names the caller.
//  - Every file that shows up in the call stack is listed once with its version ("Files in the call stack:"), read
//    from the version resource of the file as it is loaded in memory (no file is opened). Then reports of one crash
//    can show whether all players had the same, perhaps old, version of a script extender or other DLL.
static char g_crStack[65536], g_crFiles[1024];
// Files in the call stack that are not the game's own, not Windows' and not W3CrashFix: a script extender, an ASI mod,
// an overlay, ReShade and so on. Onno, 20 Nov 2026, about crash reports with a script extender in every call stack:
// "we should note the script extender (not just for this person) because we see it as a possible culprit". The verdict
// names them as possibly involved; worded as a possibility, not a cause.
static char g_crOthers[320];
static int g_crOurs;            // W3CrashFix's own code is at the crash address or anywhere in the call stack (see the verdict)
static DWORD64 g_crFrames[16]; static int g_crNFrames;   // the first addresses as numbers, for the verdict (it needs 4)
static void mod_version(HMODULE m, char *out, int sz) {
    out[0] = 0;
    HRSRC r = FindResourceW(m, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));   // 16 = RT_VERSION
    HGLOBAL g = r ? LoadResource(m, r) : NULL;
    const unsigned char *p = g ? (const unsigned char *)LockResource(g) : NULL;
    DWORD len = r ? SizeofResource(m, r) : 0;
    if (!p || len < 64) return;
    for (DWORD o = 0; o + sizeof(VS_FIXEDFILEINFO) <= len && o < 256; o += 4) {
        const VS_FIXEDFILEINFO *f = (const VS_FIXEDFILEINFO *)(p + o);
        if (f->dwSignature != 0xFEEF04BD) continue;
        snprintf(out, sz, "%u.%u.%u.%u", (unsigned)HIWORD(f->dwFileVersionMS), (unsigned)LOWORD(f->dwFileVersionMS),
                 (unsigned)HIWORD(f->dwFileVersionLS), (unsigned)LOWORD(f->dwFileVersionLS));
        return;
    }
}
static void fmt_stack(const CONTEXT *start) {
    CONTEXT c = *start;
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    DWORD64 lo = (DWORD64)(ULONG_PTR)tib->StackLimit, hi = (DWORD64)(ULONG_PTR)tib->StackBase;
    int n = 0, i = 0, more = 0, fn = 0;
    HMODULE files[24]; int nfiles = 0;
    HMODULE self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(void *)fmt_stack, &self);
    g_crStack[0] = 0; g_crFiles[0] = 0; g_crOthers[0] = 0;
    g_crNFrames = 0; g_crOurs = 0;
    int on = 0;
    char win[MAX_PATH]; UINT wl = GetWindowsDirectoryA(win, sizeof win);
    if (!wl || wl >= sizeof win) wl = 0;
    for (; c.Rip || i == 0; i++) {
        if (g_crNFrames < (int)(sizeof g_crFrames / sizeof g_crFrames[0])) g_crFrames[g_crNFrames++] = c.Rip;
        if (n < (int)sizeof g_crStack - 300) {
            char at[192];
            if (!c.Rip) lstrcpyA(at, "0x0 (an empty address: the game called through an empty pointer)");
            else where((void *)(ULONG_PTR)c.Rip, at, sizeof at);
            n += snprintf(g_crStack + n, sizeof g_crStack - n, "%s%s", i ? " < " : "", at);
        } else more++;
        HMODULE m = NULL;
        if (c.Rip && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        (LPCWSTR)(ULONG_PTR)c.Rip, &m) && m && self && m == self) g_crOurs = 1;
        if (c.Rip && nfiles < 24 && m) {
            int known = 0;
            for (int k = 0; k < nfiles; k++) if (files[k] == m) known = 1;
            if (!known) {
                files[nfiles++] = m;
                char path[MAX_PATH], ver[48]; DWORD pl = GetModuleFileNameA(m, path, sizeof path);
                const char *file = path;
                if (pl > 0 && pl < sizeof path) { for (const char *q = path; *q; q++) if (*q == '\\' || *q == '/') file = q + 1; }
                else lstrcpyA(path, "?");
                mod_version(m, ver, sizeof ver);
                if (fn < (int)sizeof g_crFiles - 100)
                    fn += snprintf(g_crFiles + fn, sizeof g_crFiles - fn, "%s%s %s", nfiles > 1 ? ", " : "", file, ver[0] ? ver : "(no version)");
                int windows = wl && pl > wl && !_strnicmp(path, win, wl) && (path[wl] == '\\' || path[wl] == '/');
                int ddi = 0;                                                     // the game's own store file in bin\ddi
                for (const char *q = path; *q && !ddi; q++) ddi = !_strnicmp(q, "\\bin\\ddi\\", 9);
                if (m != self && m != (HMODULE)g_base && !windows && !ddi && !game_own(file) && on < (int)sizeof g_crOthers - 80)
                    on += snprintf(g_crOthers + on, sizeof g_crOthers - on, "%s%s", on ? ", " : "", file);
            }
        }
        if (c.Rsp < lo || c.Rsp + 8 > hi || (c.Rsp & 7)) break;
        DWORD64 prevRsp = c.Rsp, imageBase = 0;
        PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(c.Rip, &imageBase, NULL);
        if (rf) { PVOID hd; DWORD64 est; RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, rf, &c, &hd, &est, NULL); }
        else { c.Rip = *(DWORD64 *)(ULONG_PTR)c.Rsp; c.Rsp += 8; }   // no unwind data (a leaf function): the return address is on top
        if (c.Rsp < prevRsp + 8 || c.Rsp > hi) break;              // must move up, and stay on this thread's stack
    }
    if (more) n += snprintf(g_crStack + n, sizeof g_crStack - n, " < ... and %d more (too many to list)", more);
    if (!g_crStack[0]) lstrcpyA(g_crStack, "not available");
    if (!g_crFiles[0]) lstrcpyA(g_crFiles, "none found");
}

// ---------------------------------------------------------------- settings: W3CrashFix.ini
// One readable file next to the mod instead of several small ones:
//   [W3CrashFix]
//   LicenseAccepted=MIT-2      the license text the user accepted (W3CF_LICENSE_ID); written by the mod or the installer
//   ReportId=0123456789ABCDEF  the random report id (see below)
//   OpenReportPage=1           0 = after a crash only the crash log is saved; no page and no folder window open
//   GpuCrashDetails=1          1 = switch on D3D12's DRED, so a GPU crash report says which GPU work was unfinished
//                              (since 2.0.0; on by default since 2.3.0; no measurable frame-rate
//                              cost in Onno's test; see "GPU state")
//   DefaultsSet=2              which defaults this file has been given (see load_settings); not a setting for users
//   CrashDump=0                1 = a crash also writes W3CrashFix.crash.dmp, a Windows minidump (since 2.0.0; off by
//                              default because it can hold bits of the game's memory; see "crash dump")
// Files of earlier versions are taken over once and then removed, so nobody is asked again because of this change:
// W3CrashFix.accepted (1.5.0), W3CrashFix.noreport (an empty file users could make), W3CrashFix.id (test builds).
static wchar_t g_ini[MAX_PATH];
static int g_openPage = 1;
static void ini_get(const wchar_t *key, char *out, int n) {
    wchar_t w[128]; w[0] = 0;
    GetPrivateProfileStringW(L"W3CrashFix", key, L"", w, 128, g_ini);
    if (!WideCharToMultiByte(CP_ACP, 0, w, -1, out, n, NULL, NULL)) out[0] = 0;
    out[n - 1] = 0;
}
static int ini_set(const wchar_t *key, const char *val) {
    wchar_t w[128];
    MultiByteToWideChar(CP_ACP, 0, val, -1, w, 128); w[127] = 0;
    return WritePrivateProfileStringW(L"W3CrashFix", key, w, g_ini);
}
// Reads a small file of an earlier version (trimmed) and removes it. Returns 1 if the file was there.
static int take_old_file(const wchar_t *name, char *out, int n) {
    wchar_t path[MAX_PATH]; lstrcpyW(path, g_dir); lstrcatW(path, name);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD got = 0;
    if (!ReadFile(f, out, (DWORD)n - 1, &got, NULL)) got = 0;
    CloseHandle(f);
    while (got && (out[got - 1] == '\r' || out[got - 1] == '\n' || out[got - 1] == ' ')) got--;
    out[got] = 0;
    DeleteFileW(path);
    return 1;
}
static void load_settings(void) {
    lstrcpyW(g_ini, g_dir); lstrcatW(g_ini, L"W3CrashFix.ini");
    char v[64], cur[64];
    if (take_old_file(L"W3CrashFix.accepted", v, sizeof v)) {
        ini_get(L"LicenseAccepted", cur, sizeof cur);
        if (v[0] && !cur[0]) ini_set(L"LicenseAccepted", v);
        logf("settings: W3CrashFix.accepted of an earlier version was taken over into W3CrashFix.ini and removed");
    }
    if (take_old_file(L"W3CrashFix.id", v, sizeof v)) {
        ini_get(L"ReportId", cur, sizeof cur);
        if (v[0] && !cur[0]) ini_set(L"ReportId", v);
        logf("settings: W3CrashFix.id of an earlier test build was taken over into W3CrashFix.ini and removed");
    }
    if (take_old_file(L"W3CrashFix.noreport", v, sizeof v)) {
        ini_set(L"OpenReportPage", "0");
        logf("settings: W3CrashFix.noreport was taken over into W3CrashFix.ini as OpenReportPage=0 and removed");
    }
    ini_get(L"OpenReportPage", v, sizeof v);
    g_openPage = strcmp(v, "0") != 0;
    // GpuCrashDetails is ON by default since 2.3.0 (Onno, 26 Nov 2026: "yes switch it on"; all six GPU
    // crash reports so far came without the details). Earlier versions wrote "GpuCrashDetails=0" into the file themselves
    // as the visible default, so a 0 there is not the user's choice: it is switched on once, and DefaultsSet=2 records
    // that this was done. A 0 the user sets after that stays.
    ini_get(L"DefaultsSet", cur, sizeof cur);
    ini_get(L"GpuCrashDetails", v, sizeof v);
    if (atoi(cur) < 2) {
        if (strcmp(v, "1")) {
            int was0 = !strcmp(v, "0");
            ini_set(L"GpuCrashDetails", "1"); lstrcpyA(v, "1");
            if (was0) logf("settings: GpuCrashDetails is on by default from this version and was switched on once (set it to 0 in W3CrashFix.ini to switch it off)");
        }
        ini_set(L"DefaultsSet", "2");
    }
    g_dredWanted = strcmp(v, "1") == 0;
    ini_get(L"CrashDump", v, sizeof v);
    g_dumpWanted = strcmp(v, "1") == 0;
}

// ---------------------------------------------------------------- in-game mods and the report id
// Where the report page's one button leads: an upload page ("file request") of the author's pCloud storage. The
// user's browser opens it and the user picks the file there; the mod itself sends nothing.
#define W3CF_UPLOAD_URL "https://u.pcloud.com/#/puplink?code=AilXZFWjrIdFCRypf5hzHODSQLbE119u7"
// The crash log's own folder, next to the mod (see crash_report).
#define W3CF_REPORT_DIR   "W3CrashFix crash report"
#define W3CF_REPORT_DIR_W L"W3CrashFix crash report"

// In-game mods are the ones the game itself loads: folders named mod... in <game>\Mods, and add-on folders in
// <game>\DLC that are not the game's own. DLL and ASI mods next to the exe are not looked at. The mod only names
// them: one line in the activity log, and the line "In-game mods:" in the crash summary, so that whoever reads a
// crash report can see which in-game mods were enabled and can rule them in or out. Nothing is switched off
// because of a mod, and no mod is treated differently from another.
// Since 1.6.2 the crash summary lists ALL of them, with their number (Onno, 18 Nov 2026, after a report with a
// long mod list was cut off at 600 characters: "we need to list them all"). 64 KB holds thousands of folder names;
// only beyond that the summary says how many more there were. The activity log line stays short.
static char g_modNames[65536] = "none";
static size_t g_modLen;
static int g_modsFound, g_modsLeftOut;

static int own_dlc(const char *n) {      // the game's own add-on folders, if a copy of the game keeps them there
    if (!lstrcmpiA(n, "bob") || !lstrcmpiA(n, "ep1")) return 1;
    if ((n[0] | 32) != 'd' || (n[1] | 32) != 'l' || (n[2] | 32) != 'c' || !n[3]) return 0;
    for (n += 3; *n; n++) if (*n < '0' || *n > '9') return 0;
    return 1;                            // "dlc" followed by digits only
}
static void scan_mod_folder(const wchar_t *root, const wchar_t *sub, const wchar_t *mask, int dlc) {
    wchar_t pat[MAX_PATH + 32]; WIN32_FIND_DATAW fd;
    if (lstrlenW(root) + lstrlenW(sub) + lstrlenW(mask) + 2 >= MAX_PATH) return;
    lstrcpyW(pat, root); lstrcatW(pat, sub); lstrcatW(pat, L"\\"); lstrcatW(pat, mask);
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        char n8[200];
        if (!WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, n8, sizeof n8, NULL, NULL)) lstrcpyA(n8, "(long name)");
        if (dlc && own_dlc(n8)) continue;
        if (g_modLen + strlen(n8) + 3 < sizeof g_modNames) {
            snprintf(g_modNames + g_modLen, sizeof g_modNames - g_modLen, "%s%s", g_modLen ? ", " : "", n8);
            g_modLen += strlen(g_modNames + g_modLen);
        } else g_modsLeftOut++;
        g_modsFound++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
// Looked at once, when the mod starts working: the game reads its mods at start as well.
static void scan_mods(void) {
    wchar_t root[MAX_PATH]; lstrcpyW(root, g_dir);          // <game>\bin\x64_dx12\  ->  <game>\ .
    for (int up = 0; up < 3; up++) { wchar_t *s = wcsrchr(root, L'\\'); if (!s) return; *s = 0; }
    lstrcatW(root, L"\\");
    scan_mod_folder(root, L"Mods", L"mod*", 0);
    scan_mod_folder(root, L"DLC", L"*", 1);
    if (!g_modsFound) { logf("mods: no in-game mods found"); return; }
    // The log line shows at most about 600 characters of the list, cut after a whole name.
    char shortList[640]; size_t n = g_modLen; int cut = g_modsLeftOut > 0;
    if (n > 600) { n = 600; while (n && (g_modNames[n] != ',' || g_modNames[n + 1] != ' ')) n--; cut = 1; }
    memcpy(shortList, g_modNames, n); shortList[n] = 0;
    logf("mods: in-game mods found (%d): %s%s", g_modsFound, shortList,
         cut ? ", ... (shortened here; a crash report lists all of them)" : "");
}

// The report id: a random number made on this PC the first time the mod works, kept as ReportId in W3CrashFix.ini
// and shown in the crash summary. Its only use: several crash reports from the same machine/installation can be recognised
// as such. It is not derived from the hardware, Windows or the user's name; removing the line gives a new one, and
// Uninstall and a declined license remove the whole file.
// Since 1.7.0 users may personalise it (Onno, 18 Nov 2026): their own text before or after the 16 characters of the
// id ("Havoc_0123456789ABCDEF", "0123456789ABCDEFhavoc"), so the original id stays intact and new logs can still be
// matched with logs sent before the change. The id is kept as written in W3CrashFix.ini if it is at most 40 letters,
// digits, '_' and '-' with 16 hex digits in a row among them. A completely own id is shelved for now. Anything else
// is replaced by a new random id; the log says only that it was not usable, and a line in the ini says so too.
static char g_reportId[48] = "none";
// The only place that tells users they may personalise the id (Onno, 18 Nov 2026: "it should not be mentioned in the
// mod itself, including the installer. but it should be mentioned in the ini file"): a note of comment lines right
// above the ReportId line in W3CrashFix.ini. Windows' own ini functions skip lines that begin with ';' and keep them
// when they change a value. Added once, at game start (never on the crash path); a file in another encoding than
// the plain one Windows writes is left alone.
// Since 2.0.0 (Onno, 20 Nov 2026: "it should be known in the ini that the custom report id can be useful for a user with
// multiple installations of the game. they can make themselves known this way, the 16 digit hex number will distinguish
// their installations"): one more line. Every installation has its own W3CrashFix.ini and so its own random id; the
// same own text in each shows the reports come from one person. Added to notes written by earlier versions too
// (right above their "; Allowed:" line), see ini_note_report_id.
// Every note line stays under 80 characters (Onno, 21 Nov 2026, looking at his ini in Notepad: "the lines in the ini
// are too long without word wrap"). Notes written by earlier versions are replaced, see ini_shorten_notes.
#define G_ID_MULTI "; More than one installation of the game? Use the same text in each, for\r\n" \
                   "; example MyName_ before each id: your text shows the reports are yours,\r\n" \
                   "; and the 16 characters tell your installations apart.\r\n"
static const char g_idNote[] =
    "; ReportId: lets the mod's author recognise several crash reports from\r\n"
    "; this machine/installation.\r\n"
    "; You may personalise it: add your own text before or after the id, for\r\n"
    "; example MyName_0123456789ABCDEF.\r\n"
    "; Keep the 16 characters of the id itself, then your new crash logs can\r\n"
    "; still be matched with the ones you sent before.\r\n"
    G_ID_MULTI
    "; Allowed: letters, digits, _ and -, 40 characters at most.\r\n"
    "; Anything else is replaced by a new random id.\r\n";
// A one-line notice right above the ReportId line when the id the user wrote there could not be used (Onno, 18 Nov
// 2026: "the user should be notified that their id is invalid ... without revealing the reason"). It names the id
// that was refused, never the reason. An older notice is removed first; with notice NULL only that happens (used
// when the id is fine again). Same rules as the note: game start only, plain encoding only.
static const char g_idNoticePrefix[] = "; The ReportId you entered";
static void ini_set_id_notice(const char *refused) {
    static char buf[16384], out[16384 + 256];
    HANDLE f = CreateFileW(g_ini, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got = 0;
    BOOL ok = ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    if (!ok || got >= sizeof buf - 1 || (got >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE)) return;
    buf[got] = 0;
    char *old = strstr(buf, g_idNoticePrefix);
    if (!refused && !old) return;                                    // nothing to remove
    if (!refused) {                                                  // keep it while the id is still the one made then
        char *eol = strchr(old, '\n'); size_t ol = eol ? (size_t)(eol - old) : strlen(old);
        char tmp[256]; if (ol >= sizeof tmp) ol = sizeof tmp - 1; memcpy(tmp, old, ol); tmp[ol] = 0;
        if (strstr(tmp, g_reportId)) return;
    }
    size_t o = 0; int placed = 0;
    for (char *line = buf; *line; ) {
        char *end = strchr(line, '\n'); size_t len = end ? (size_t)(end - line + 1) : strlen(line);
        if (!strncmp(line, g_idNoticePrefix, sizeof g_idNoticePrefix - 1)) { line += len; continue; }   // drop the old notice
        if (refused && !placed && !strncmp(line, "ReportId=", 9)) {
            char clean[64]; int k = 0;                               // the refused id, printable characters only
            for (const char *c = refused; *c && k < 60; c++) if ((unsigned char)*c >= 32 && *c != 127) clean[k++] = *c;
            clean[k] = 0;
            o += snprintf(out + o, sizeof out - o, "%s (%s) can't be used, so a new random id was made: %s\r\n", g_idNoticePrefix, clean, g_reportId);
            placed = 1;
        }
        if (o + len >= sizeof out) return;
        memcpy(out + o, line, len); o += len;
        line += len;
    }
    f = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(f, out, (DWORD)o, &w, NULL);
    CloseHandle(f);
}
// Notes of earlier versions had lines of 100 to 200 characters. Such a note is taken out here, whole, so that the
// short-lined one is written in its place right after (ini_note_report_id, ini_note_settings). A note is recognised
// by its first line: it begins with "; <Setting>: " and is longer than 80 characters; it runs until the next line
// that is not a comment, which is the setting itself. The line about a refused report id stays. Settings and their
// values are never touched. Game start only, plain encoding only, like the notes themselves.
static void ini_shorten_notes(void) {
    static const char *const starts[] = { "; LicenseAccepted: ", "; GpuCrashDetails: ", "; CrashDump: ", "; OpenReportPage: ", "; ReportId: " };
    static char buf[16384], out[16384];
    HANDLE f = CreateFileW(g_ini, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got = 0;
    BOOL ok = ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    if (!ok || got >= sizeof buf - 1 || (got >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE)) return;
    buf[got] = 0;
    size_t o = 0; int dropped = 0, inNote = 0;
    for (char *line = buf; *line; ) {
        char *end = strchr(line, '\n'); size_t len = end ? (size_t)(end - line + 1) : strlen(line);
        size_t text = len; while (text && (line[text - 1] == '\n' || line[text - 1] == '\r')) text--;
        if (inNote && line[0] != ';') inNote = 0;                               // the setting's own line ends the note
        if (!inNote && text > 80)
            for (size_t k = 0; k < sizeof starts / sizeof starts[0] && !inNote; k++) inNote = !strncmp(line, starts[k], strlen(starts[k]));
        // 2.3.0: the GpuCrashDetails note of 2.1.0 to 2.2.0 says "0 = off (default)"; the setting is on by default now.
        // That note began with "; GpuCrashDetails: 1 = "; it is taken out the same way, and the new one follows.
        if (!inNote && !strncmp(line, "; GpuCrashDetails: 1 = ", 23)) inNote = 1;
        if (inNote && strncmp(line, g_idNoticePrefix, sizeof g_idNoticePrefix - 1)) { dropped++; line += len; continue; }
        memcpy(out + o, line, len); o += len;
        line += len;
    }
    if (!dropped) return;
    f = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(f, out, (DWORD)o, &w, NULL);
    CloseHandle(f);
    logf("settings: %d note line(s) of an earlier version taken out of W3CrashFix.ini; the current ones follow", dropped);
}
static void ini_note_report_id(void) {
    static char buf[16384];
    HANDLE f = CreateFileW(g_ini, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got = 0;
    BOOL ok = ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    if (!ok || got >= sizeof buf - 1 || (got >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE)) return;
    buf[got] = 0;
    // Since 1.8.0 the note's example id is 0123456789ABCDEF; an ini written by 1.7.0 gets the new example (same length,
    // in that comment line only). Nothing else in the file changes.
    // The old example is recognised by its form (16 hex digits after "MyName_" in the note line), so no id is
    // written out here.
    static const char exHead[] = "; You may personalise it: add your own text before or after the id, for example MyName_";
    static const char newId[] = "0123456789ABCDEF";
    char *ex = strstr(buf, exHead);
    if (ex) ex += sizeof exHead - 1;
    int hex = ex != NULL;
    for (int k = 0; hex && k < 16; k++) hex = isxdigit((unsigned char)ex[k]) != 0;
    if (hex && ex[16] == '.' && memcmp(ex, newId, 16) != 0) {
        memcpy(ex, newId, 16);
        f = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, buf, got, &w, NULL); CloseHandle(f); }
    }
    if (strstr(buf, "; ReportId: ")) {                              // the note is there already
        // a note of an earlier version: add the 2.0.0 line about several installations above its "; Allowed:" line
        char *al = strstr(buf, "\n; Allowed: ");
        if (strstr(buf, "; More than one installation") || !al) return;
        al++;
        f = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) return;
        DWORD w;
        WriteFile(f, buf, (DWORD)(al - buf), &w, NULL);
        WriteFile(f, G_ID_MULTI, sizeof G_ID_MULTI - 1, &w, NULL);
        WriteFile(f, al, (DWORD)strlen(al), &w, NULL);
        CloseHandle(f);
        return;
    }
    char *at = strstr(buf, "\nReportId=");
    if (at) at++; else if (!strncmp(buf, "ReportId=", 9)) at = buf; else return;
    f = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(f, buf, (DWORD)(at - buf), &w, NULL);
    WriteFile(f, g_idNote, sizeof g_idNote - 1, &w, NULL);
    WriteFile(f, at, (DWORD)strlen(at), &w, NULL);
    CloseHandle(f);
}
// Every setting in W3CrashFix.ini gets a short explanation above its line (Onno, 21 Nov 2026: "all .ini settings should
// have explanations in the next (test) build"); ReportId already had one, see g_idNote. A note is added once: it is
// recognised by its first words ("; Key: "), so a note the user removed comes back, but one that is there is never
// written twice or changed. Same rules as the ReportId note: game start only, plain encoding only, the setting lines
// themselves are not touched.
static void ini_note_settings(void) {
    static const struct { const char *key, *note; } notes[] = {
        { "LicenseAccepted",
          "; LicenseAccepted: the license you accepted in the license window.\r\n"
          "; Leave it as it is: without this line the license window comes back at\r\n"
          "; the next game start, and the fix stays off until you accept.\r\n" },
        { "GpuCrashDetails",
          "; GpuCrashDetails: after a graphics card crash (\"Gpu Crash for unknown\r\n"
          "; reasons\") the crash log also says which work the graphics card had not\r\n"
          "; finished. 1 = on (default), 0 = off. It may cost a little frame rate on\r\n"
          "; some PCs; none was measurable on the author's.\r\n"
          "; Takes effect at the next game start.\r\n" },
        { "CrashDump",
          "; CrashDump: 1 = a crash also saves a crash dump, W3CrashFix.crash.dmp,\r\n"
          "; next to the crash log. 0 = off (default).\r\n"
          "; A dump shows much more than the log, but it can contain bits of the\r\n"
          "; game's memory, such as file paths with your Windows user name.\r\n"
          "; You choose whether to send it. Takes effect at the next game start.\r\n" },
        { "OpenReportPage",
          "; OpenReportPage: 1 = after a crash the report page and a small folder\r\n"
          "; window open (default). 0 = nothing opens.\r\n"
          "; The crash log is saved either way.\r\n" },
    };
    static char buf[16384], out[16384 + 2048];
    HANDLE f = CreateFileW(g_ini, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got = 0;
    BOOL ok = ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    if (!ok || got >= sizeof buf - 1 || (got >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE)) return;
    buf[got] = 0;
    size_t o = 0; int added = 0;
    for (char *line = buf; *line; ) {
        char *end = strchr(line, '\n'); size_t len = end ? (size_t)(end - line + 1) : strlen(line);
        for (size_t k = 0; k < sizeof notes / sizeof notes[0]; k++) {
            size_t kl = strlen(notes[k].key);
            char mark[40]; snprintf(mark, sizeof mark, "; %s: ", notes[k].key);
            if (!strncmp(line, notes[k].key, kl) && line[kl] == '=' && !strstr(buf, mark)) {
                size_t nl = strlen(notes[k].note);
                if (o + nl >= sizeof out) return;
                memcpy(out + o, notes[k].note, nl); o += nl; added++;
            }
        }
        if (o + len >= sizeof out) return;
        memcpy(out + o, line, len); o += len;
        line += len;
    }
    if (!added) return;
    f = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(f, out, (DWORD)o, &w, NULL);
    CloseHandle(f);
    logf("settings: explanations added to W3CrashFix.ini for %d setting(s)", added);
}
static int report_id_ok(const char *s, int *hex16) {
    // 1.7.0: own text only before or after the 16 characters of the id, which must stay unbroken (16 hex digits in a
    // row). A completely own id is shelved (Onno, 18 Nov 2026: "don't include this option in 1.7.0").
    size_t n = strlen(s); int hex = n == 16, run = 0, longest = 0;
    if (n < 16 || n > 40) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-')) return 0;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) { if (++run > longest) longest = run; }
        else { run = 0; hex = 0; }
    }
    if (longest < 16) return 0;
    *hex16 = hex;
    // The test builds' id is reserved (Onno, 18 Nov 2026: "another user can never use 1337testlulz in their custom
    // id"), in any mix of capitals and small letters, so a player's report can never be taken for a test build's.
    static const char reserved[] = "1337testlulz";
    for (size_t i = 0; i + sizeof reserved - 1 <= n; i++)
        if (!_strnicmp(s + i, reserved, sizeof reserved - 1)) return 0;
    return 1;
}
static void load_report_id(void) {
#ifdef W3_CRASHTEST
    // Test builds always use the same id (Onno, 18 Nov 2026: "in test builds, my report id should always be:
    // 1337testlulz"), so a crash report from a test build can never be taken for one of a released version. It is
    // also written to W3CrashFix.ini, with the note above it (Onno: "my ini doesn't show my test id"). That is safe:
    // with its 12 characters it is too short for a released version (16 at least), which therefore replaces it
    // with a new random id of its own.
    lstrcpyA(g_reportId, "1337testlulz");
    ini_set(L"ReportId", g_reportId);
    ini_note_report_id();
    logf("report id: %s (fixed in TEST BUILDS, written to W3CrashFix.ini)", g_reportId);
    return;
#endif
    char buf[64]; ini_get(L"ReportId", buf, sizeof buf);
    int hex16 = 0;
    if (report_id_ok(buf, &hex16)) {
        lstrcpyA(g_reportId, buf);
        // The mod itself (its log, the report page, the installer) never mentions that the id can be personalised
        // (Onno, 18 Nov 2026); only the note in W3CrashFix.ini does, see ini_note_report_id().
        (void)hex16;
        logf("report id: %s (from W3CrashFix.ini)", g_reportId);
        ini_note_report_id();
        ini_set_id_notice(NULL);       // an old "can't be used" notice goes once the user has set an id of their own
        return;
    }
    if (buf[0]) logf("report id: the ReportId in W3CrashFix.ini is not usable, so a new random one is made");
    LARGE_INTEGER q; FILETIME ft; QueryPerformanceCounter(&q); GetSystemTimeAsFileTime(&ft);
    unsigned long long x = (unsigned long long)q.QuadPart ^ (((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime);
    x ^= ((unsigned long long)GetCurrentProcessId() << 32) ^ ((unsigned long long)GetCurrentThreadId() << 13) ^ (unsigned long long)(ULONG_PTR)&x;
    for (int i = 0; i < 2; i++) {                            // mix the bits (splitmix64)
        x += 0x9E3779B97F4A7C15ULL; x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL; x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL; x ^= x >> 31;
    }
    snprintf(g_reportId, sizeof g_reportId, "%016llX", x);
    if (ini_set(L"ReportId", g_reportId)) {
        logf("report id: %s (random, newly made and saved in W3CrashFix.ini; remove the ReportId line there to get a new one)", g_reportId);
        ini_note_report_id();
        if (buf[0]) ini_set_id_notice(buf);   // tell the user in the ini that their id was refused (no reason given)
    }
    else logf("report id: %s (random; W3CrashFix.ini could not be written, error %lu, so the next session gets another one)", g_reportId, GetLastError());
}

// Finds the Explorer window that shows the crash log's own folder: a folder window whose title contains that
// folder's name (the title is the folder name, the full path, or the name followed by " - File Explorer").
//
// RULES FOR EVERYTHING THAT TOUCHES WINDOWS AFTER A CRASH (learned the hard way: a test build once hung the
// crashed game for good, and the PC had to be reset):
//   1. Never ask a window of the game's own process for anything. Asking for its title means waiting for the
//      game's window thread to answer, and after a crash that thread may never answer. other_process() below
//      is the first check in both searches.
//   2. Never make a call that waits for another program. Windows of other programs are only read (their class,
//      their title, their state: none of these wait) and changed with the non-waiting calls ShowWindowAsync and
//      SetWindowPos with SWP_ASYNCWINDOWPOS.
//   3. All of it runs on a thread of its own, and crash_report() waits for that thread for a limited time only.
//      Whatever happens there, the crash path ends and the game's own crash reporter gets its turn.
static int other_process(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid && pid != GetCurrentProcessId();
}
// A folder window with that name may already be open (left from an earlier crash). The search therefore works
// in two steps: before Explorer is started, "collect" notes the windows that are there; afterwards only a window
// that was not there before is taken, because that is the one the user is looking at.
typedef struct { HWND old[32]; int nOld; int collect; HWND found; } folder_search;
static BOOL CALLBACK find_report_folder(HWND h, LPARAM lp) {
    folder_search *s = (folder_search *)lp;
    wchar_t cls[64], title[2 * MAX_PATH];
    if (!other_process(h)) return TRUE;
    if (!IsWindowVisible(h) || !GetClassNameW(h, cls, 64) || lstrcmpW(cls, L"CabinetWClass")) return TRUE;
    if (!GetWindowTextW(h, title, 2 * MAX_PATH) || !wcsstr(title, W3CF_REPORT_DIR_W)) return TRUE;
    if (s->collect) { if (s->nOld < 32) s->old[s->nOld++] = h; return TRUE; }
    for (int i = 0; i < s->nOld; i++) if (s->old[i] == h) return TRUE;   // was there before: not the new one
    s->found = h;
    return FALSE;
}
// Finds the browser window that shows the report page: its title begins with the page's title (browsers put the
// title of the tab in front). A folder window is never taken for it, although the folder's name is the same.
static BOOL CALLBACK find_report_page(HWND h, LPARAM out) {
    static const wchar_t pageTitle[] = L"W3CrashFix crash report";
    wchar_t cls[64], title[2 * MAX_PATH];
    if (!other_process(h)) return TRUE;
    if (!IsWindowVisible(h) || !GetClassNameW(h, cls, 64) || !lstrcmpW(cls, L"CabinetWClass")) return TRUE;
    if (!GetWindowTextW(h, title, 2 * MAX_PATH) || wcsncmp(title, pageTitle, sizeof pageTitle / sizeof pageTitle[0] - 1)) return TRUE;
    *(HWND *)out = h;
    return FALSE;
}

// Whether window a is in front of window b: walks the windows from the front, only reading (never waits).
static int folder_in_front(HWND a, HWND b) {
    HWND h = GetTopWindow(NULL);
    for (int i = 0; h && i < 10000; i++, h = GetWindow(h, GW_HWNDNEXT)) {
        if (h == a) return 1;
        if (h == b) return 0;
    }
    return 0;
}

// Opens the folder window and the report page and arranges them. Runs on its own thread (rule 3 above).
static wchar_t g_crPage[MAX_PATH];                   // full path of W3CrashFix-report.html
static DWORD WINAPI report_windows(LPVOID unused) {
    (void)unused;
    DWORD t0 = GetTickCount();                           // the log lines below give times in ms from here
    // Order matters. Windows does not let a program in the background put other programs' windows in front
    // (tried in several ways on 17 Nov 2026; it is silently refused). What does work: the window that opens LAST
    // comes to the front by its own program's doing. So the report page is opened first, the mod waits until the
    // browser window is there and active, and only then is Explorer started on the crash log's folder: its new
    // window opens in front of the browser. The mod then makes it small and moves it to the lower right.
    // Since 1.6.1 it also asks Windows to keep that window "always on top" (otherwise it goes behind the browser
    // as soon as the user clicks there). Windows may refuse that without saying so; the mod reads it back and logs
    // the answer. The report page no longer depends on it: its Send button copies the crash log's path as well.
    HWND fw = NULL, bw = NULL;
    // 1. The report page, in the user's default browser.
    wchar_t cmd[MAX_PATH + 32]; wsprintfW(cmd, L"explorer.exe \"%s\"", g_crPage);
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        // A browser that is already running puts the page in a new tab; if its window is minimized it would stay
        // so. The mod waits up to three seconds for the window that shows the page and restores it if needed.
        for (int i = 0; i < 30 && !bw; i++) { Sleep(100); EnumWindows(find_report_page, (LPARAM)&bw); }
        if (bw) {
            int wasMin = IsIconic(bw);
            if (wasMin) ShowWindowAsync(bw, SW_RESTORE);
            DWORD ta = GetTickCount();                   // until the browser really is the active window, at most 2 s
            while (GetForegroundWindow() != bw && GetTickCount() - ta < 2000) Sleep(50);
            wchar_t bcls[64] = L"?"; char bcls8[100];
            GetClassNameW(bw, bcls, 64);
            WideCharToMultiByte(CP_UTF8, 0, bcls, -1, bcls8, sizeof bcls8, NULL, NULL);
            logf("crash report: saved and opened W3CrashFix-report.html (browser window found after %lu ms, window class %s; it %s; %s)",
                 GetTickCount() - t0, bcls8, wasMin ? "was minimized and has been restored" : "was not minimized",
                 GetForegroundWindow() == bw ? "it is the active window" : "it had not become the active window within 2 s");
        } else logf("crash report: saved and opened W3CrashFix-report.html (its browser window was not found within 3 seconds)");
    } else logf("crash report: saved, but the page could not be opened (%lu)", GetLastError());
    // 2. Then the folder window on the crash log's own folder, with the file highlighted, for dragging it onto
    //    the upload page. Made small, put in the lower right and asked to stay "always on top".
    wchar_t sel[2 * MAX_PATH + 80]; wsprintfW(sel, L"explorer.exe /select,\"%s" W3CF_REPORT_DIR_W L"\\W3CrashFix.crash.log\"", g_dir);
    STARTUPINFOW si2 = { sizeof si2 }; PROCESS_INFORMATION pi2;
    static folder_search fs;
    memset(&fs, 0, sizeof fs);
    fs.collect = 1; EnumWindows(find_report_folder, (LPARAM)&fs); fs.collect = 0;   // the ones already open
    if (CreateProcessW(NULL, sel, NULL, NULL, FALSE, 0, NULL, NULL, &si2, &pi2)) {
        CloseHandle(pi2.hThread); CloseHandle(pi2.hProcess);
        for (int i = 0; i < 20 && !fs.found; i++) { Sleep(100); EnumWindows(find_report_folder, (LPARAM)&fs); }
        fw = fs.found;
        int reused = 0;
        // No new window in time: Explorer may have brought an existing window on that folder forward instead.
        for (int i = 0; !fw && i < fs.nOld; i++) if (IsWindow(fs.old[i]) && IsWindowVisible(fs.old[i])) { fw = fs.old[i]; reused = 1; }
        MONITORINFO mi; mi.cbSize = sizeof mi;
        if (fw && GetMonitorInfoW(MonitorFromWindow(fw, MONITOR_DEFAULTTONEAREST), &mi)) {
            int ww = mi.rcWork.right - mi.rcWork.left, wh = mi.rcWork.bottom - mi.rcWork.top;
            int cx = ww / 3 < 520 ? 520 : ww / 3, cy = wh / 4 < 260 ? 260 : wh / 4;
            // With a crash dump next to the log (CrashDump=1) the folder holds two files, and at a quarter of the
            // screen the crash log, the file that matters, was half cut off below the dump (Onno's test, 20 Nov 2026).
            // Then the window is a third of the screen high, at least 360 pixels.
            if (g_dumpDone) cy = wh / 3 < 360 ? 360 : wh / 3;
            if (cx > ww) cx = ww;
            if (cy > wh) cy = wh;
            if (IsZoomed(fw) || IsIconic(fw)) ShowWindowAsync(fw, SW_RESTORE);
            // Since 1.6.2 (Onno, 18 Nov 2026: "can you change the timing a bit to reduce the chances of this not
            // working?"): test38 found the new folder window only 1.4 s after the browser had become active, asked at
            // once, and Windows did not keep it on top; test35, where everything happened later, got YES. So the mod
            // first gives Explorer 400 ms to finish opening the window, and if Windows has not made it "always on
            // top" half a second after the request, asks again, up to 4 times (at most about 3.5 s in all, well
            // within the 12 s the crash path waits for this thread).
            Sleep(400);
            int onTop = 0, tries = 0;
            while (!onTop && tries < 4 && IsWindow(fw)) {
                tries++;
                // HWND_TOPMOST = "always on top"; asynchronous, so the mod never waits for Explorer (rule 2 above)
                SetWindowPos(fw, HWND_TOPMOST, mi.rcWork.right - cx - 16, mi.rcWork.bottom - cy - 16, cx, cy, SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
                Sleep(500);                              // let Explorer carry it out, then look
                // GetWindowLongW only reads the window's style; it does not send a message, so it cannot wait either
                onTop = IsWindow(fw) && (GetWindowLongW(fw, GWL_EXSTYLE) & WS_EX_TOPMOST);
            }
            logf("crash report: opened the folder \"" W3CF_REPORT_DIR "\" with W3CrashFix.crash.log highlighted (%s found after %lu ms, %d such window(s) were open before; made small, lower right; in front of the browser: %s, checked; always on top: %s, checked, asked %d time(s))",
                 reused ? "an already open window" : "new window", GetTickCount() - t0, fs.nOld,
                 !bw ? "no browser window to compare with" : (IsWindow(fw) && folder_in_front(fw, bw) ? "YES" : "NO"),
                 onTop ? "YES" : "NO (Windows did not allow it)", tries);
        } else logf("crash report: opened the folder \"" W3CF_REPORT_DIR "\" with W3CrashFix.crash.log highlighted (its window was not found in time, so it keeps Explorer's own size and place)");
    } else logf("crash report: the folder window could not be opened (%lu)", GetLastError());
    return 0;
}

// ---------------------------------------------------------------- GPU state (since 2.0.0)
// Onno, 19 Nov 2026: "yes write it", after the analysis of the crash at witcher3.exe+0x1F103A3: there the graphics
// driver drops the device and the game stops itself with "Gpu Crash for unknown reasons". Five players had it. To tell
// the possible causes apart (a timeout, a page fault, a driver error), a crash report now also says:
//  1. whether the GPU device is still working, and if not, why Windows removed it (ID3D12Device::GetDeviceRemovedReason);
//  2. with GpuCrashDetails=1 in W3CrashFix.ini (off by default; it may cost a little performance, none measurable in Onno's test of 20 Nov 2026): D3D12's own
//     "Device Removed Extended Data" (DRED), which records which GPU work was unfinished and, for a page fault, the
//     GPU address and the resources around it. DRED must be switched on before the game creates its device.
// Crash path safety (see "test28" in next-release.md): the questions to D3D12 run on a thread of their own and the
// crash report waits for them at most 1.5 s; if they don't answer, the report says so and goes on.
static void dred_name(const char *a, const WCHAR *w, char *out, int n) {
    out[0] = 0;
    if (a && readable((const unsigned char *)a, 1)) { snprintf(out, n, "%.60s", a); return; }
    if (w && readable((const unsigned char *)w, 2)) WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL);
    out[n - 1] = 0;
}
static const char *dred_op(unsigned op) {
    static const char *n[] = { "SetMarker", "BeginEvent", "EndEvent", "DrawInstanced", "DrawIndexedInstanced", "ExecuteIndirect",
        "Dispatch", "CopyBufferRegion", "CopyTextureRegion", "CopyResource", "CopyTiles", "ResolveSubresource",
        "ClearRenderTargetView", "ClearUnorderedAccessView", "ClearDepthStencilView", "ResourceBarrier", "ExecuteBundle",
        "Present", "ResolveQueryData", "BeginSubmission", "EndSubmission", "DecodeFrame", "ProcessFrames",
        "AtomicCopyBufferUint", "AtomicCopyBufferUint64", "ResolveSubresourceRegion", "WriteBufferImmediate", "DecodeFrame1",
        "SetProtectedResourceSession", "DecodeFrame2", "ProcessFrames1", "BuildRaytracingAccelerationStructure",
        "EmitRaytracingAccelerationStructurePostbuildInfo", "CopyRaytracingAccelerationStructure", "DispatchRays",
        "InitializeMetaCommand", "ExecuteMetaCommand", "EstimateMotion", "ResolveMotionVectorHeap", "SetPipelineState1",
        "InitializeExtensionCommand", "ExecuteExtensionCommand", "DispatchMesh", "EncodeFrame", "ResolveEncoderOutputMetadata" };
    return op < sizeof n / sizeof n[0] ? n[op] : "?";
}
static char g_gpuState[1536] = "not checked";
static int g_gpuLen;
// appends to g_gpuState; never writes past its end, however long the text
static void gpu_add(const char *fmt, ...) {
    int room = (int)sizeof g_gpuState - g_gpuLen;
    if (room <= 1) return;
    va_list a; va_start(a, fmt);
    int w = vsnprintf(g_gpuState + g_gpuLen, room, fmt, a);
    va_end(a);
    if (w > 0) g_gpuLen += w < room ? w : room - 1;
}
static DWORD WINAPI gpu_query(LPVOID unused) {
    (void)unused;
    g_gpuLen = 0; g_gpuState[0] = 0;
    ID3D12Device *d = g_dev;
    if (!d || !readable((const unsigned char *)d, sizeof(void *)) || !readable(*(const unsigned char **)d, 0x140)) {
        gpu_add("not known (no D3D12 device seen)");
        return 0;
    }
    HRESULT r = ID3D12Device_GetDeviceRemovedReason(d);
    if (r == S_OK) { gpu_add("working (the graphics card did not drop out)"); return 0; }
    const char *why =
        r == (HRESULT)0x887A0006 ? "DEVICE_HUNG: the GPU took too long on a task and Windows reset the driver" :
        r == (HRESULT)0x887A0005 ? "DEVICE_REMOVED: the graphics card stopped (driver crash or update, overclock, power or heat)" :
        r == (HRESULT)0x887A0007 ? "DEVICE_RESET: the GPU was reset after a bad command" :
        r == (HRESULT)0x887A0020 ? "DRIVER_INTERNAL_ERROR: an error inside the graphics driver" :
        r == (HRESULT)0x8007000E ? "E_OUTOFMEMORY: out of memory" : "another reason";
    gpu_add("REMOVED, reason 0x%08lX (%s)", (unsigned long)r, why);
    if (!g_dredOn) {
        gpu_add("; more details: set GpuCrashDetails=1 in W3CrashFix.ini");
        return 0;
    }
    ID3D12DeviceRemovedExtendedData *dred = NULL;
    if (FAILED(ID3D12Device_QueryInterface(d, &IID_ID3D12DeviceRemovedExtendedData, (void **)&dred)) || !dred) {
        gpu_add("; DRED: not available");
        return 0;
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT pf = {0};
    if (SUCCEEDED(ID3D12DeviceRemovedExtendedData_GetPageFaultAllocationOutput(dred, &pf))) {
        if (pf.PageFaultVA) {
            gpu_add("; page fault at GPU address 0x%llX", (unsigned long long)pf.PageFaultVA);
            const D3D12_DRED_ALLOCATION_NODE *lists[2] = { pf.pHeadExistingAllocationNode, pf.pHeadRecentFreedAllocationNode };
            for (int l = 0; l < 2; l++) {
                int k = 0;
                for (const D3D12_DRED_ALLOCATION_NODE *a = lists[l]; a && k < 3 && readable((const unsigned char *)a, sizeof *a);
                     a = a->pNext, k++) {
                    char nm[64]; dred_name(a->ObjectNameA, a->ObjectNameW, nm, sizeof nm);
                    gpu_add("%s type %d '%s'", k ? "," : (l ? "; recently freed there:" : "; in use there:"), (int)a->AllocationType, nm);
                }
            }
        } else gpu_add("; no page fault");
    }
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bc = {0};
    if (SUCCEEDED(ID3D12DeviceRemovedExtendedData_GetAutoBreadcrumbsOutput(dred, &bc))) {
        int shown = 0, nodes = 0;
        for (const D3D12_AUTO_BREADCRUMB_NODE *b = bc.pHeadAutoBreadcrumbNode; b && nodes < 500 && shown < 3 &&
             readable((const unsigned char *)b, sizeof *b); b = b->pNext, nodes++) {
            if (!b->pLastBreadcrumbValue || !readable((const unsigned char *)b->pLastBreadcrumbValue, 4)) continue;
            UINT32 done = *b->pLastBreadcrumbValue;
            if (done >= b->BreadcrumbCount) continue;                 // this command list finished
            unsigned op = 0xFFFF;
            if (b->pCommandHistory && readable((const unsigned char *)(b->pCommandHistory + done), sizeof *b->pCommandHistory))
                op = (unsigned)b->pCommandHistory[done];
            char cl[64], q[64];
            dred_name(b->pCommandListDebugNameA, b->pCommandListDebugNameW, cl, sizeof cl);
            dred_name(b->pCommandQueueDebugNameA, b->pCommandQueueDebugNameW, q, sizeof q);
            gpu_add("%s command list '%s' on queue '%s' stopped at step %u of %u (%s)",
                    shown ? "," : "; unfinished:", cl, q, (unsigned)done + 1, (unsigned)b->BreadcrumbCount,
                    op == 0xFFFF ? "?" : dred_op(op));
            shown++;
        }
        if (!shown) gpu_add("; no unfinished GPU work recorded");
    }
    ID3D12DeviceRemovedExtendedData_Release(dred);
    return 0;
}
// ---------------------------------------------------------------- crash dump (since 2.0.0)
// Onno, 19 Nov 2026 ("all of them", idea 2): with CrashDump=1 in W3CrashFix.ini (off by default) a crash also writes
// W3CrashFix.crash.dmp next to the crash log: a Windows minidump with the crashed game's threads, their stacks, the
// loaded files and the memory the stacks and registers point to. That shows the values the game was working with,
// which a call stack can't; the descriptor heap leak itself was found in such dumps. It can hold bits of the game's
// memory (for example file paths with the Windows user name), which is why it is opt-in and the docs say so.
// dbghelp.dll (Windows' own, from System32) is loaded at game start when the setting is on, never during a crash. The
// dump is written by a thread of its own; the crash report waits for it at most 20 s and then goes on without it.
typedef BOOL (WINAPI *MDWD_t)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                              PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
static MDWD_t g_mdwd;                          // MiniDumpWriteDump, loaded at start
static struct { HANDLE f; MINIDUMP_EXCEPTION_INFORMATION x; BOOL ok; } g_dumpJob;
static DWORD WINAPI dump_load(LPVOID unused) {
    (void)unused;
    wchar_t p[MAX_PATH]; UINT n = GetSystemDirectoryW(p, MAX_PATH);
    if (n && n < MAX_PATH - 16) { lstrcatW(p, L"\\dbghelp.dll"); HMODULE h = LoadLibraryW(p); if (h) g_mdwd = (MDWD_t)GetProcAddress(h, "MiniDumpWriteDump"); }
    logf(g_mdwd ? "crash dump: on (CrashDump=1): a crash also writes W3CrashFix.crash.dmp"
                : "crash dump: CrashDump=1, but Windows' dbghelp.dll could not be loaded; no dump will be written");
    return 0;
}
static DWORD WINAPI dump_write(LPVOID unused) {
    (void)unused;
    g_dumpJob.ok = g_mdwd(GetCurrentProcess(), GetCurrentProcessId(), g_dumpJob.f,
                          (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo),
                          &g_dumpJob.x, NULL, NULL);
    return 0;
}
static void crash_dump(EXCEPTION_POINTERS *ep, const wchar_t *dir) {
    wchar_t p[MAX_PATH]; lstrcpyW(p, dir); lstrcatW(p, L"\\W3CrashFix.crash.dmp");
    DeleteFileW(p);                            // never leave the dump of an older crash next to this crash's log
    g_dumpDone = 0;
    if (!g_dumpWanted || !g_mdwd) return;
    g_dumpJob.f = CreateFileW(p, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_dumpJob.f == INVALID_HANDLE_VALUE) { logf("crash dump: could not create the file (%lu)", GetLastError()); return; }
    g_dumpJob.x.ThreadId = GetCurrentThreadId(); g_dumpJob.x.ExceptionPointers = ep; g_dumpJob.x.ClientPointers = FALSE;
    g_dumpJob.ok = FALSE;
    HANDLE t = CreateThread(NULL, 0, dump_write, NULL, 0, NULL);
    if (!t) { CloseHandle(g_dumpJob.f); DeleteFileW(p); logf("crash dump: could not start the writer"); return; }
    if (WaitForSingleObject(t, 20000) != WAIT_OBJECT_0) {
        // still writing: leave the thread and its file alone (closing the file under it could crash it), go on
        logf("crash dump: not finished within 20 s; the crash report goes on without it");
        CloseHandle(t); return;
    }
    CloseHandle(t);
    LARGE_INTEGER sz = {0}; GetFileSizeEx(g_dumpJob.f, &sz);
    CloseHandle(g_dumpJob.f);
    if (!g_dumpJob.ok) { DeleteFileW(p); logf("crash dump: Windows could not write it"); return; }
    g_dumpDone = 1;
    logf("crash dump: written, W3CrashFix.crash.dmp, %lld KB", (long long)(sz.QuadPart >> 10));
}

// Switched on from a thread of its own at game start, before the game creates its device (LoadLibrary must not run
// inside DllMain). If the device already exists, it only takes effect at the next start, and the log says so.
// The first session after an install (Onno, 26 Nov 2026, after the first start of 2.3.0 on a clean install logged "it
// takes effect at the next start"; "yeah worth it"): the license window is open while the game starts, and by the time
// the user accepts, D3D12Core.dll is loaded, so the check below gave up. But the game is still held before its window
// then (hold_game), and its real device comes after the window (the hooks see it about 2 s after the game continues).
// So right after "accepted", while the game is still held, DRED is switched on without that check (held = 1). Nothing
// is done before the user accepts. Whether it took effect when D3D12 was already loaded shows in a GPU crash report.
static int g_dredTried;
static DWORD WINAPI dred_switch_on(LPVOID held) {
    if (g_dredTried) return 0;                           // start_work asks again after the license path did it
    g_dredTried = 1;
    int loaded = GetModuleHandleW(L"D3D12Core.dll") != NULL;
    if (loaded && !held) { logf("gpu details: GpuCrashDetails=1, but the game's graphics device already exists; it takes effect at the next start"); return 0; }
    HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
    typedef HRESULT (WINAPI *GDI_t)(REFIID, void **);
    GDI_t gdi = d3d12 ? (GDI_t)GetProcAddress(d3d12, "D3D12GetDebugInterface") : NULL;
    ID3D12DeviceRemovedExtendedDataSettings *s = NULL;
    if (!gdi || FAILED(gdi(&IID_ID3D12DeviceRemovedExtendedDataSettings, (void **)&s)) || !s) {
        logf("gpu details: GpuCrashDetails=1, but this Windows does not offer DRED (needs Windows 10 version 1903 or newer)");
        return 0;
    }
    ID3D12DeviceRemovedExtendedDataSettings_SetAutoBreadcrumbsEnablement(s, D3D12_DRED_ENABLEMENT_FORCED_ON);
    ID3D12DeviceRemovedExtendedDataSettings_SetPageFaultEnablement(s, D3D12_DRED_ENABLEMENT_FORCED_ON);
    ID3D12DeviceRemovedExtendedDataSettings_Release(s);
    g_dredOn = 1;
    if (held && loaded) logf("gpu details: DRED switched on (GpuCrashDetails=1) right after the license was accepted, before the game made its window; the game had already loaded D3D12, so a GPU crash report will show whether the details are there");
    else logf("gpu details: DRED switched on (GpuCrashDetails=1): a GPU crash report will name the unfinished GPU work and any page fault");
    return 0;
}

static void crash_report(EXCEPTION_POINTERS *ep) {
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    DWORD code = er->ExceptionCode;
    char at[192]; where(er->ExceptionAddress, at, sizeof at);
    char detail[96] = "";
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) && er->NumberParameters >= 2)
        snprintf(detail, sizeof detail, ", %s address 0x%llX",
                 er->ExceptionInformation[0] == 1 ? "writing" : er->ExceptionInformation[0] == 8 ? "executing" : "reading",
                 (unsigned long long)er->ExceptionInformation[1]);
    logf("!!!! CRASH: exception 0x%08lX (%s%s) at %s", (unsigned long)code, exc_name(code), detail, at);
    if (code == EXCEPTION_STACK_OVERFLOW) return;        // almost no stack left: the log line is all we can do
    summary("CRASH");
    if (ep->ContextRecord) {
        fmt_regs(ep->ContextRecord); logf("[CRASH] registers: %s", g_crRegs);
        fmt_stack(ep->ContextRecord);
        // not with logf: its line ends after 1024 characters, and the call stack can be much longer
        if (g_log != INVALID_HANDLE_VALUE) {
            char pre[64]; SYSTEMTIME st; GetLocalTime(&st);
            snprintf(pre, sizeof pre, "%02d:%02d:%02d +%5lus  [CRASH] call stack: ", st.wHour, st.wMinute, st.wSecond,
                     (unsigned long)((GetTickCount() - g_t0) / 1000));
            DWORD w; WriteFile(g_log, pre, (DWORD)strlen(pre), &w, NULL);
            WriteFile(g_log, g_crStack, (DWORD)strlen(g_crStack), &w, NULL); WriteFile(g_log, "\r\n", 2, &w, NULL);
        }
    } else { lstrcpyA(g_crRegs, "not available"); lstrcpyA(g_crStack, "not available"); lstrcpyA(g_crFiles, "not available"); }
    static char evs[EV_N * 140]; fmt_events(evs, sizeof evs);
    logf("[CRASH] files in the call stack: %s", g_crFiles);

    // GPU state (since 2.0.0): asked on a thread of its own, waited for at most 1.5 s (see gpu_query)
    {
        HANDLE gq = CreateThread(NULL, 0, gpu_query, NULL, 0, NULL);
        if (!gq) lstrcpyA(g_gpuState, "not checked (could not start the check)");
        else {
            if (WaitForSingleObject(gq, 1500) != WAIT_OBJECT_0)
                lstrcpyA(g_gpuState, "not known (D3D12 did not answer within 1.5 s)");
            CloseHandle(gq);
        }
        logf("[CRASH] GPU state: %s", g_gpuState);
    }

    long long rva =g_base ? (long long)((unsigned char *)er->ExceptionAddress - g_base) : -1;
    // Since 1.6.2 the verdict also looks at the call stack (Onno, 18 Nov 2026, after a test where the empty page was
    // used one call deeper, at witcher3.exe+0x2809364, called from +0x280AD2E). The same crash, an empty page read
    // through a NULL pointer, then shows up in a function that the crash function called. It counts as the
    // descriptor heap crash when the exception is a read near address 0 and one of the next three entries of the
    // call stack lies in the function that holds witcher3.exe+0x280AD3D (found with the game's own unwind data).
    // Since 1.8.0 the crash address is the one found in this game (g_crashRva, see "Finding the code"); in 5.0.16 it is
    // witcher3.exe+0x280AD3D.
    int deeper = 0;
    if (g_buildOk && g_base && g_crashRva && rva != g_crashRva && code == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2 &&
        er->ExceptionInformation[0] == 0 && er->ExceptionInformation[1] < 0x10000) {
        DWORD64 ib = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry((DWORD64)(ULONG_PTR)(g_base + g_crashRva), &ib, NULL);
        if (fn && ib == (DWORD64)(ULONG_PTR)g_base)
            for (int i = 1; i < g_crNFrames && i <= 3 && !deeper; i++) {
                DWORD64 r = g_crFrames[i] - ib;
                if (g_crFrames[i] > ib && r > fn->BeginAddress && r <= fn->EndAddress) deeper = 1;
            }
    }
    // Since 2.0.0 (Onno, 20 Nov 2026: "it should be made clearer that w3crashfix does not cause this crash"): the verdict
    // also says whether W3CrashFix's own code took part. It is checked, not assumed: the crash address and every entry of
    // the call stack are looked up, and only when none of them lies in W3CrashFix's own file does the verdict say that
    // W3CrashFix did not cause it. If one does, it says so and asks for the log.
    // Worded as what was checked, not as a certainty (Onno agreed, 20 Nov 2026): the stack check is strong evidence, not
    // proof (a wrong page handed out earlier could make the game crash later somewhere else). So it says "no sign", and
    // adds that the fix rescued no pages only when that is true in this session.
    static char notOurs[1100], others[440];
    others[0] = 0;
    if (g_crOthers[0])
        snprintf(others, sizeof others, " Also in the call stack: %s, which %s not part of the game or Windows (a mod, script extender, "
                 "overlay or other add-on), so %s may be involved.", g_crOthers, strchr(g_crOthers, ',') ? "are" : "is",
                 strchr(g_crOthers, ',') ? "they" : "it");
    int stackKnown = ep->ContextRecord && strcmp(g_crStack, "not available");
    const char *why =
        g_crOurs ? "W3CrashFix's own code is in the call stack of this crash, so it may be involved: please send this crash log."
        : stackKnown && g_rescued == 0 ? "There is no sign that W3CrashFix caused it: its own code is not at the crash address or in the call stack, "
                                         "and its fix rescued no pages in this session. W3CrashFix only noticed the crash and saved this report."
        : stackKnown ? "There is no sign that W3CrashFix caused it: its own code is not at the crash address or in the call stack. "
                       "W3CrashFix only noticed the crash and saved this report."
        : "W3CrashFix only noticed the crash and saved this report.";
    snprintf(notOurs, sizeof notOurs, "This is NOT the crash that W3CrashFix fixes (that one is at witcher3.exe+0x%X). %s%s%s", g_crashRva,
             why, g_crOurs ? "" : " Something else crashed the game.", others);
    const char *verdict =
        !g_buildOk ? "W3CrashFix does not support this build of the game, so it cannot tell which crash this is."
        : !g_crashRva ? "W3CrashFix did not find the place of its crash in this build of the game, so it cannot tell which crash this is."
        : rva != g_crashRva && !deeper ? notOurs
        : g_fixActive ? (deeper ? "This is the descriptor heap crash that W3CrashFix is made for (here one call deeper than usual, see the call stack), although the fix was active. That should not happen: please report it."
                                : "This is the descriptor heap crash that W3CrashFix is made for, although the fix was active. That should not happen: please report it.")
        : (deeper ? "This is the descriptor heap crash that W3CrashFix is made for (here one call deeper than usual, see the call stack). The fix was not active in this session."
                  : "This is the descriptor heap crash that W3CrashFix is made for. The fix was not active in this session.");
    // Known crashes (since 2.0.0; Onno, 19 Nov 2026, "all of them", idea 3): crash places that several players' reports
    // share, with what is known and what may help. Shown on the report page and in the summary, so a player gets help
    // at once. Only for the game build the addresses come from (5.0.16, not moved). The list grows with each release,
    // from promo\Crash-Table.ps1; an entry needs reports from at least three players.
    // Since test48 (Onno's test of 20 Nov 2026, "all 4"): the tips are a list of their own, shown on the report page as a
    // separate block below the summary instead of one long paragraph in it; the GPU tip says whether GpuCrashDetails is
    // already on; and no "yet" in "the cause is not known" (no promises in public texts).
    // Onno, 26 Nov 2026 ("yes", after the analysis of the +0x1EECxxx crashes of three players): an entry can also ask for
    // a crash dump (kind 2). Those crashes are the game's renderer binding draw state to a command-list context that is
    // gone (+0x1EECD86: set index buffer, the sub-list pointer was garbage; +0x1EECF31: the neighbour, called with a
    // null handle); the log cannot show what the memory was reused for, a dump can. Such an entry has no tips, so the
    // "Things that may help:" heading is left out for it.
    static const char renderListCrash[] =
        "The game crashed while preparing a draw call: its renderer used a drawing list that was no longer valid. "
        "Seen in reports from several players, with and without mods. The cause is not known.";
    static const struct { unsigned rva; int kind; const char *what; const char *tips[4]; } known_crashes[] = {   // kind 1 = GPU crash, 2 = a dump would help
        { 0x1F103A3, 1,
          "The graphics card dropped out, and the game stopped itself with \"Gpu Crash for unknown reasons\". "
          "Seen in reports from several players, half of them on graphics cards with 6 GB of memory. The cause is not known.",
          { "Update the graphics driver.", "Remove any overclock or undervolt of the graphics card.",
            "On a card with 8 GB of memory or less, lower texture quality and ray tracing.", NULL } },
        { 0x1EECD86, 2, renderListCrash, { NULL } },
        { 0x1EECF31, 2, renderListCrash, { NULL } },
    };
    int ki = -1;
    for (size_t k = 0; g_buildOk && !g_moved && rva >= 0 && k < sizeof known_crashes / sizeof known_crashes[0]; k++)
        if ((long long)known_crashes[k].rva == rva) ki = (int)k;
    const char *gpuTip = ki < 0 ? NULL
        : known_crashes[ki].kind == 2 ? (g_dumpWanted && g_mdwd
            ? "CrashDump is on, so a crash dump is saved next to this log. Please send both files: the dump shows what the log cannot."
            : "To help find the cause, set CrashDump=1 in W3CrashFix.ini and send the log and the crash dump of the next crash: the dump shows what the log cannot.")
        : known_crashes[ki].kind != 1 ? NULL
        : g_dredOn ? "GpuCrashDetails is on, so this crash log already holds the extra GPU details (see \"GPU state\")."
        : g_dredWanted ? "GpuCrashDetails=1 is set, but it could not take effect in this session (see the activity log); it works from the next game start."
        : "To help find the cause, set GpuCrashDetails=1 in W3CrashFix.ini and send the log of the next crash: it then says more.";
    static char knownLine[1024];
    knownLine[0] = 0;
    if (ki >= 0) {
        int n = snprintf(knownLine, sizeof knownLine, "Known crash: %s%s", known_crashes[ki].what, known_crashes[ki].tips[0] ? " Things that may help:" : "");
        for (int t = 0; t < 4 && known_crashes[ki].tips[t] && n < (int)sizeof knownLine - 160; t++)
            n += snprintf(knownLine + n, sizeof knownLine - n, " %s", known_crashes[ki].tips[t]);
        if (gpuTip && n < (int)sizeof knownLine - 160) n += snprintf(knownLine + n, sizeof knownLine - n, " %s", gpuTip);
        snprintf(knownLine + n, sizeof knownLine - n, "\r\n");
        logf("[CRASH] %.*s", n, knownLine);
    }
    const char *test = "";
#ifdef W3_CRASHTEST
    if (g_testCrash) test = "Note: this crash was triggered on purpose with a test key of this TEST BUILD.\r\n";
#endif
    // The summary has to be useful on its own: many reports arrive without the log file attached.
    char mem[256]; vram(mem, sizeof mem);
    // The summary is written in three parts: the lines above the mods, the mods list (which can be long, see
    // g_modNames), and the lines below it.
    char head[448], mods[96];
    // Date and time of the crash (Onno, 18 Nov 2026). The log lines only carry the time of day, so without this an old
    // crash log could not be told from a new one (a player sent the same log twice; a later crash the mod did not catch
    // leaves the old file in place). The report page uses the same moment for its "older crash" notice.
    static const char *const mon[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
    SYSTEMTIME ct; GetLocalTime(&ct);
    char when[48];
    snprintf(when, sizeof when, "%d %s %04d, %02d:%02d", ct.wDay, mon[(ct.wMonth >= 1 && ct.wMonth <= 12) ? ct.wMonth - 1 : 0], ct.wYear, ct.wHour, ct.wMinute);
    FILETIME cft; GetSystemTimeAsFileTime(&cft);
    unsigned long long crashMs = ((((unsigned long long)cft.dwHighDateTime << 32) | cft.dwLowDateTime) - 116444736000000000ULL) / 10000ULL;
    snprintf(head, sizeof head, "W3CrashFix crash report\r\nCrashed at: %s (this PC's time)\r\nMod: %s (%s)\r\nReport id: %s\r\n",
             when, "W3CrashFix " W3CF_VERSION, g_flavor, g_reportId);
    if (g_modsFound) snprintf(mods, sizeof mods, "In-game mods (%d): ", g_modsFound); else lstrcpyA(mods, "In-game mods: ");
    snprintf(g_crSum, sizeof g_crSum,
             "Game build supported: %s\r\n"
             "Fix active: %s\r\n"
             "Crash: exception 0x%08lX (%s%s) at %s\r\n"
             "Registers: %s\r\n"
             "Call stack: %s\r\n"
             "Files in the call stack: %s\r\n"
             "Session length: %lu min\r\n"
             "Before the crash: %s\r\n"
             "Graphics card: %s\r\n"
             "GPU state: %s\r\n"
             "Game settings: %s\r\n"
             "Other DLL and ASI files: %s\r\n"
             "Descriptor heaps: %lld live + %lld sampler (peak %lld + %lld), %lld failed creations\r\n"
             "Fix: %lld pages rescued, %lld retired normally\r\n"
             "Memory: %s\r\n"
             "Verdict: %s\r\n%s%s",
             !g_buildOk ? "no" : g_moved ? "yes (not 5.0.16, but the same code at other places)" : "yes", g_fixActive ? "yes" : "no",
             (unsigned long)code, exc_name(code), detail, at, g_crRegs, g_crStack, g_crFiles,
             (unsigned long)((GetTickCount() - g_t0) / 60000), evs, g_adapterName, g_gpuState, g_gameSettings, g_otherFiles,
             g_live[0][1], g_live[1][1], g_peakSv[0], g_peakSv[1], g_failed,
             (long long)g_rescued, (long long)g_retiredNormal, mem, verdict, knownLine, test);

    // W3CrashFix.crash.log = the summary + this session's whole log. Only a newer crash overwrites it. It gets a
    // folder of its own next to the mod, holding nothing else: the folder window that opens after a crash then
    // shows exactly one file, so there is nothing to scroll to and nothing to mix up with the activity log.
    wchar_t path[MAX_PATH], src[MAX_PATH];
    lstrcpyW(path, g_dir); lstrcatW(path, W3CF_REPORT_DIR_W);
    CreateDirectoryW(path, NULL);                                   // fine if it is there already
    lstrcpyW(src, g_dir); lstrcatW(src, L"W3CrashFix.crash.log");
    DeleteFileW(src);                                               // a crash log of an earlier version, in the old place
    HANDLE f = create_in_dir(W3CF_REPORT_DIR_W L"\\W3CrashFix.crash.log", path);
    if (f != INVALID_HANDLE_VALUE) {
        put(f, head); put(f, mods); put(f, g_modNames);
        if (g_modsLeftOut) { char more[96]; snprintf(more, sizeof more, ", and %d more (too many to list)", g_modsLeftOut); put(f, more); }
        put(f, "\r\n"); put(f, g_crSum); put(f, "\r\n---------------- full log of the crashed session ----------------\r\n");
        lstrcpyW(src, g_dir); lstrcatW(src, L"W3CrashFix.activity.log");
        HANDLE in = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        if (in != INVALID_HANDLE_VALUE) {
            DWORD got, w;
            while (ReadFile(in, g_crBuf, sizeof g_crBuf, &got, NULL) && got) WriteFile(f, g_crBuf, got, &w, NULL);
            CloseHandle(in);
        }
        CloseHandle(f);
    }
    {   // CrashDump=1: the dump goes into the same folder, after the log (the log is the part that must be there)
        wchar_t dd[MAX_PATH]; lstrcpyW(dd, g_dir); lstrcatW(dd, W3CF_REPORT_DIR_W);
        crash_dump(ep, dd);
    }

    // W3CrashFix-report.html
    // One button: it opens the author's upload page in the user's browser, where the user picks the crash log, and
    // it copies the crash log's path at the same time, so the user can paste it into the upload page's file window
    // (since 1.6.1: a click on the browser puts the browser in front of the folder window). Together with the page
    // the mod opens a folder window on the crash log's own folder (see below), so the user can also drag the file
    // onto the upload page; a page in a browser is not allowed to open Explorer itself.
    char dir8[3 * MAX_PATH], dirEsc[4 * MAX_PATH];
    f = create_in_dir(L"W3CrashFix-report.html", path);
    if (f == INVALID_HANDLE_VALUE) { logf("crash report: could not write the report page (%lu)", GetLastError()); return; }
    WideCharToMultiByte(CP_UTF8, 0, g_dir, -1, dir8, sizeof dir8, NULL, NULL);
    html_esc(dir8, dirEsc, sizeof dirEsc);
    put(f, "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><title>W3CrashFix crash report</title><style>"
           // the text keeps to the left: the small folder window sits in the lower right corner, a third of the screen wide
           "body{font:16px/1.5 Segoe UI,Arial,sans-serif;width:min(860px,max(420px,60vw));margin:32px 0 32px 40px;padding:0 16px;box-sizing:border-box;background:#16181c;color:#e8e6e3}"
           "h1{font-size:26px;margin:0 0 4px}pre{background:#0d0e11;border:1px solid #333;border-radius:6px;padding:14px;white-space:pre-wrap;word-break:break-word}"
           "a{color:#8ab4f8}.b{display:inline-block;margin:6px 10px 6px 0;padding:10px 18px;border-radius:6px;border:0;background:#c8102e;color:#fff;font:inherit;font-weight:600;text-decoration:none;cursor:pointer}"
           ".g{background:#30363d;padding:6px 12px;font-weight:400}code{background:#0d0e11;padding:2px 5px;border-radius:4px}.s{color:#9aa0a6;font-size:14px}li{margin:4px 0}"
           ".o{background:#3a2a00;border:1px solid #b8860b;border-radius:6px;padding:10px 14px}"
           // Send button always in view (Onno, 20 Nov 2026: "the send button should always be visible. i had to scroll down"): its line sticks to the bottom of the window until the page is scrolled past it
           ".sb{position:sticky;bottom:0;margin:0;padding:10px 0;background:#16181c;border-top:1px solid #2c3036;z-index:1}"
           ".k{background:#10263a;border:1px solid #2f6da3;border-radius:6px;padding:10px 14px;margin:12px 0}.k ul{margin:6px 0}"
           "</style></head><body><h1>The Witcher 3 crashed</h1>");
    // When the crash happened, and a notice if the page is opened again later (Onno, 18 Nov 2026): the page and the
    // crash log stay until the next crash the mod catches, so an old page must not look like a new crash.
    {
        char t[640], tEsc[800];
        snprintf(t, sizeof t, "%s (your PC's time)", when);
        html_esc(t, tEsc, sizeof tEsc);
        put(f, "<p class=\"s\" id=\"w\" data-t=\"");
        snprintf(t, sizeof t, "%llu", crashMs); put(f, t);
        put(f, "\">Crash of "); put(f, tEsc); put(f, ".</p>"
               "<p class=\"o\" id=\"o\" hidden><b>This page belongs to an older crash</b>, the one above. "
               "If the game crashed again since then and no new page opened, W3CrashFix did not catch that crash "
               "(for example the game froze instead of crashing). Its crash log is then not on your PC, and the file below "
               "is still the one from the older crash: if you sent it before, there is no need to send it again.</p>");
    }
    put(f, "<p>W3CrashFix noticed the crash and saved a report on your PC. <b>Nothing has been sent anywhere.</b> "
           "If you would like to help, you can send the crash log to the mod's author with the button below.</p>"
           "<p class=\"s\">This page is a file on your own PC, not a website: look at the address bar, it starts with <code>file:</code>. "
           "The mod itself never connects to the internet. Only your browser does, and only when you click the button.</p><pre id=\"s\">");
    // The page shows only whether the fix was active and the verdict (Onno, 17 Nov 2026: "the less clutter the better");
    // the full summary stays at the top of the crash log, which is what gets sent.
    {
        char page[2048];
        snprintf(page, sizeof page, "Fix active: %s\r\nVerdict: %s\r\n%s", g_fixActive ? "yes" : "no", verdict, test);
        html_esc(page, g_crEsc, sizeof g_crEsc); put(f, g_crEsc);
    }
    if (ki >= 0) {   // a known crash: what is known and the tips, in a block of its own below the summary
        char e[1024];
        put(f, "</pre><div class=\"k\"><b>This crash is known.</b> ");
        html_esc(known_crashes[ki].what, e, sizeof e); put(f, e);
        if (known_crashes[ki].tips[0]) {
            put(f, "<br>Things that may help:<ul>");
            for (int t = 0; t < 4 && known_crashes[ki].tips[t]; t++) { put(f, "<li>"); html_esc(known_crashes[ki].tips[t], e, sizeof e); put(f, e); put(f, "</li>"); }
            put(f, "</ul>");
        } else put(f, "<br>");
        if (gpuTip) { html_esc(gpuTip, e, sizeof e); put(f, e); }
        put(f, "</div><pre hidden>");   // keeps the "</pre>" below balanced
    }
    // With a crash dump (CrashDump=1, since 2.0.0) both files are listed under the heading (Onno, 20 Nov 2026, looking at
    // the page: "we don't need the crashdump here?" - "yes"); the dump stays marked optional, and the button still copies
    // only the crash log's path (a file window takes one pasted path).
    put(f, g_dumpDone ? "</pre><p><b>The files to send:</b><br><code>" : "</pre><p><b>The file to send:</b><br><code>");
    put(f, dirEsc);
    if (g_dumpDone) {
        put(f, W3CF_REPORT_DIR "\\W3CrashFix.crash.log</code><br><code>");
        put(f, dirEsc);
        put(f, W3CF_REPORT_DIR "\\W3CrashFix.crash.dmp</code> <span class=\"s\">(optional: the crash dump, because you switched on CrashDump=1. "
               "It helps to find the cause, and it may contain bits of the game's memory, such as file paths with your Windows user name.)</span><br>"
               "<span class=\"s\">A small folder window has opened in the lower right corner with both files. To send both, pick both in the file window "
               "of the upload page, or drag both from the folder window onto the page. The folder stays as it is until the game crashes again.</span></p>");
    } else
        put(f, W3CF_REPORT_DIR "\\W3CrashFix.crash.log</code><br><span class=\"s\">A small folder window has opened in the lower right corner that holds only this file. "
               "You don't need it: the button below also copies the file's path. It stays there until the game crashes again.</span></p>");
    put(f, "<p class=\"sb\"><a class=\"b\" target=\"_blank\" rel=\"noopener\" href=\"" W3CF_UPLOAD_URL "\" data-p=\"");
    put(f, dirEsc);
    // With a crash dump listed under "The files to send", the button says "files" too (noticed on Onno's screenshots of
    // 21 Nov 2026; Onno: "anything that needs a build must be written"). It still copies only the crash log's path.
    put(f, W3CF_REPORT_DIR "\\W3CrashFix.crash.log\" onclick=\"cp(this)\">");
    put(f, g_dumpDone ? "Send the crash files" : "Send the crash log");
    put(f, "</a> <span class=\"s\" id=\"c\"></span></p>"
           "<p><b>How it works</b></p><ol>"
           "<li>Click the button. A pCloud page opens, and the crash log's path is copied. If the page shows a cookie notice, answer it first; it can cover the buttons.</li>"
           "<li>On that page, click <b>Click Here to Add Files</b>. In the window that opens, press Ctrl+V and then Enter. "
           "(Or drag the file from the small folder window onto the page.)</li>"
           "<li>Type any name you like. A made-up one is fine.</li>"
           "<li>Click Upload. When you see &quot;Files are uploaded!&quot;, you are done and can close that tab.</li></ol>"
           "<p class=\"s\">pCloud then offers you an account. You don't need one.</p>"
           // Onno, 20 Nov 2026: "inform the user that accidentally uploading a wrong file is not a big problem"
           "<p class=\"s\">Sent the wrong file by mistake? No problem: anything that isn't a W3CrashFix log or crash dump is deleted. You can just send the right file afterwards.</p>"
           "<p class=\"s\">Ctrl+V pasted nothing? <button class=\"b g\" data-p=\"");
    put(f, dirEsc);
    put(f, W3CF_REPORT_DIR "\\W3CrashFix.crash.log\" onclick=\"cp(this)\">Copy the file's path</button> and paste it again into the &quot;File name&quot; box.</p>"
           // Onno, 19 Nov 2026: reports are always welcome, but a test on a clean game tells whether a mod plays a part
           "<p class=\"s\"><b>Every crash log is welcome, also without a test.</b> It helps even more if you also check whether the crash "
           "happens on a clean game: move your other mods (in-game mods, ASI and DLL mods, ReShade) out of the game folder for a while, "
           // 2.0.0 (Onno, 20 Nov 2026: "why would they need to put clean in if we can easily tell by the logs. we need this
           // to work with as little steps as possible for them"): no naming request; the log lists the mods itself
           "keep W3CrashFix, and play the same part again. If it still crashes, send that crash log too. Logs of both, with and "
           "without mods, from the same PC are especially helpful; the log shows by itself which mods were there.</p>"
           "<p>Thanks for sending your crash log. It helps find and fix crashes in W3CrashFix.</p>"
           "<p class=\"s\">What is sent: only the file you pick, and the name you type on the upload page. The file holds a summary "
           "and the mod's log of the crashed session: technical data, which can include the folder the game is installed in "
           "and the folder names of your in-game mods. "
           "Only the mod's author can see uploaded files. The &quot;Report id&quot; in the file is a random number made on your PC, so that several "
           "reports from the same machine/installation can be recognised. It says nothing about you; remove the <code>ReportId</code> line from "
           "<code>W3CrashFix.ini</code> (next to the mod, in the game's <code>bin\\x64_dx12</code> folder) to get a new one.</p>"
           "<p class=\"s\">A report sent this way can't be answered. For questions, use the "
           "<a target=\"_blank\" rel=\"noopener\" href=\"https://www.nexusmods.com/witcher3/mods/15624?tab=posts\">posts on Nexus Mods</a> or the "
           "<a target=\"_blank\" rel=\"noopener\" href=\"https://github.com/Havoc104/W3CrashFix/issues\">issues on GitHub</a>.</p>"
           "<p class=\"s\">The game's own crash reporter may also appear. That one reports to the game's developers; this page is only about the W3CrashFix mod. You can use both.</p>"
           "<p class=\"s\">Don't want this page to open after a crash? Open <code>W3CrashFix.ini</code> (next to the mod, in the game's <code>bin\\x64_dx12</code> folder) with Notepad and "
           "change <code>OpenReportPage=1</code> to <code>OpenReportPage=0</code>. The crash log is still saved.</p>"
           // footnote: why the report is shown in the browser (the other ways that were considered, and why not those).
           // Never call it a "web page": it is a local file and not on the web.
           "<hr style=\"border:0;border-top:1px solid #333;margin:22px 0 10px\">"
           "<p class=\"s\"><b>Why use my browser?</b> It is on purpose. A window inside the crashed game could freeze, or hide behind "
           "the game. A separate helper program would put a second executable in your game folder. Asking at the next game start would "
           "come too late for anyone who had removed the mod by then. Your browser is a program that is working right after the crash, ");
    // The footnote names the page's own size (Onno, 17 Nov 2026). The size includes the number itself, so it is worked
    // out from what is written so far plus the rest of the page, a second time if the number's length changed it.
    static const char sizeHead[] = "and showing this file in it adds nothing to your game folder but the file itself (";
    static const char sizeTail[] = " KB), and the installer's Uninstall removes it.</p>"
           // the only script on the page: shows the "older crash" notice when the page is opened more than an hour after
           // the crash, and puts the file's path on the clipboard when the Send button or the small button is clicked.
           // The Send link still opens the upload page as usual (nothing is stopped or redirected).
           "<script>(function(){var w=document.getElementById('w'),t=+w.getAttribute('data-t');"
           "if(t&&Date.now()-t>3600000){document.getElementById('o').hidden=false}})();"
           "function cp(b){var t=b.getAttribute('data-p'),a=document.createElement('textarea');"
           "a.value=t;document.body.appendChild(a);a.select();try{document.execCommand('copy')}catch(e){}document.body.removeChild(a);"
           "if(navigator.clipboard&&navigator.clipboard.writeText){navigator.clipboard.writeText(t).catch(function(){})}"
           "if(b.tagName=='A'){document.getElementById('c').textContent='The path is copied: paste it with Ctrl+V on the upload page.'}"
           "else{b.textContent='Path copied'}}</script></body></html>";
    DWORD sofar = SetFilePointer(f, 0, NULL, FILE_CURRENT);          // bytes written up to here
    char kb[24] = "";
    for (int pass = 0; pass < 3; pass++) {                            // settles after one or two passes
        unsigned long total = sofar + (unsigned long)(sizeof sizeHead - 1 + strlen(kb) + sizeof sizeTail - 1);
        unsigned long tenths = (total * 10 + 512) / 1024;               // KB with one decimal, rounded
        char next[24]; snprintf(next, sizeof next, "%lu.%lu", tenths / 10, tenths % 10);
        if (!strcmp(next, kb)) break;
        lstrcpyA(kb, next);
    }
    put(f, sizeHead); put(f, kb); put(f, sizeTail);
    CloseHandle(f);

    if (!g_openPage) { logf("crash report: saved; the page was not opened because OpenReportPage=0 in W3CrashFix.ini"); return; }
    // The folder window and the report page are opened and arranged on a thread of their own, and this thread
    // waits for it for at most twelve seconds (see the rules above find_report_folder). If that thread should ever
    // get stuck, the crash path still ends here and the game's own crash reporter gets its turn.
    lstrcpyW(g_crPage, path);
    HANDLE wt = CreateThread(NULL, 0, report_windows, NULL, 0, NULL);
    if (!wt) { logf("crash report: saved, but the windows could not be opened (%lu)", GetLastError()); return; }
    if (WaitForSingleObject(wt, 12000) != WAIT_OBJECT_0) logf("crash report: opening the windows took longer than 12 seconds - not waited for any longer");
    CloseHandle(wt);
}
// Top-level filter: runs when nothing else handled the exception, i.e. the game is really crashing.
static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep) {
    if (InterlockedExchange(&g_crashed, 1) == 0) crash_report(ep);
    return g_prevFilter ? g_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;   // let the game's own crash reporter run too
}
// First notice of a serious-looking exception, before anyone had a chance to handle it. Only logs a line (at most
// five per session): if the game's crash reporter ends the process itself, this line is what remains.
static LONG CALLBACK crash_notice(EXCEPTION_POINTERS *ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if (fatal_looking(c) && c != EXCEPTION_STACK_OVERFLOW && InterlockedIncrement(&g_noticed) <= 5) {
        char at[192]; where(ep->ExceptionRecord->ExceptionAddress, at, sizeof at);
        logf("exception: 0x%08lX (%s) at %s - first notice, the game may still handle it", (unsigned long)c, exc_name(c), at);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
// The game installs its own top-level filter at some point; make sure ours stays first and theirs is still called.
static void guard_filter(int quiet) {
    LPTOP_LEVEL_EXCEPTION_FILTER p = SetUnhandledExceptionFilter(crash_filter);
    if (p != crash_filter) {
        g_prevFilter = p;
        if (!quiet) logf("crash handler: installed again in front of another handler (%p)", (void *)p);
    }
}

#ifdef W3_CRASHTEST
// TEST BUILDS ONLY - never part of a release. Three key combinations crash the game on purpose, while the game window
// is in front:  Ctrl+Alt+Shift+F7 = plain crash inside the mod,  Ctrl+Alt+Shift+F8 = the real descriptor heap crash,
// Ctrl+Alt+Shift+F9 = the GPU crash (the game's graphics device removed, see below),  Ctrl+Alt+Shift+F10 / F11 = the
// two known renderer crashes at witcher3.exe+0x1EECD86 / +0x1EECF31,  Ctrl+Alt+Shift+F12 = a call through an empty
// function pointer (see below).
static DWORD WINAPI crash_keys(LPVOID unused) {
    (void)unused;
    for (;;) {
        Sleep(50);
        HWND fg = GetForegroundWindow(); DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) continue;
        if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000) || !(GetAsyncKeyState(VK_MENU) & 0x8000) || !(GetAsyncKeyState(VK_SHIFT) & 0x8000)) continue;
        if (GetAsyncKeyState(VK_F7) & 0x8000) {
            logf("TEST: Ctrl+Alt+Shift+F7 - plain crash inside the mod");
            g_testCrash = 3;
            *(volatile int *)0 = 1;
        }
        if ((GetAsyncKeyState(VK_F8) & 0x8000) && InterlockedCompareExchange(&g_testCrash, 1, 0) == 0)
            logf("TEST: Ctrl+Alt+Shift+F8 - real descriptor heap crash requested%s", g_fixActive ? "" : " (fix not active: nothing will happen)");
        // F9 (since test47, Onno, 20 Nov 2026: "are you able to add a key combo to simulate the gpu crash?" - "yes"):
        // D3D12's own test function ID3D12Device5::RemoveDevice puts the game's device in the "removed" state in
        // software, as if the graphics card had dropped out; the driver and the card are not touched. The game then
        // runs into it the way players do: its next GPU check fails and its own handler stops it ("Gpu Crash for
        // unknown reasons", witcher3.exe+0x1F103A3), and the crash report shows the GPU state (and DRED with
        // GpuCrashDetails=1). If the game hangs instead, the log says so after 20 s.
        if ((GetAsyncKeyState(VK_F9) & 0x8000) && InterlockedCompareExchange(&g_testCrash, 4, 0) == 0) {
            ID3D12Device5 *d5 = NULL;
            if (!g_dev || FAILED(ID3D12Device_QueryInterface(g_dev, &IID_ID3D12Device5, (void **)&d5)) || !d5) {
                logf("TEST: Ctrl+Alt+Shift+F9 - GPU crash requested, but %s; nothing done",
                     g_dev ? "this Windows or driver offers no ID3D12Device5" : "no D3D12 device seen yet");
                g_testCrash = 0;
                continue;
            }
            logf("TEST: Ctrl+Alt+Shift+F9 - removing the game's graphics device (ID3D12Device5::RemoveDevice), as if the graphics card dropped out");
            ID3D12Device5_RemoveDevice(d5);
            ID3D12Device5_Release(d5);
            for (int s = 0; s < 20; s++) Sleep(1000);
            logf("TEST: 20 s after the device removal the game has not crashed (it may hang): end it in Task Manager if needed");
        }
        // F10 and F11 (since test58, Onno, 26 Nov 2026: "can you simulate those crashes too?" - "yes, do it"): the two
        // known renderer crashes of the +0x1EECxxx family, at the players' exact addresses, by calling the game's own
        // two functions with the bad input the players' reports showed. Only on the game build the addresses come
        // from (5.0.16, code not moved), and only when the functions' first bytes are the expected ones. The call
        // comes from this thread, so the call stack names W3CrashFix as the caller; the report's test note says why.
        //   F10: +0x1EECD30 ("set index buffer") with a stand-in renderer object whose context 1 has one sub-list,
        //        and that sub-list pointer is garbage (the value from a player's report): faults at +0x1EECD86.
        //   F11: +0x1EECF10 with context handle 0 (the function only refuses -1) and an object address that makes
        //        the read non-canonical, so it faults at +0x1EECF31 whatever is mapped in this process.
        int f10 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0, f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
        if ((f10 || f11) && InterlockedCompareExchange(&g_testCrash, f10 ? 5 : 6, 0) == 0) {
            static const unsigned char sigD30[] = { 0x48, 0x83, 0xEC, 0x38, 0x41, 0x8B, 0xC1, 0x83, 0xFA, 0xFF };
            static const unsigned char sigF10[] = { 0x45, 0x8B, 0xD0, 0x45, 0x8B, 0xD9, 0x83, 0xFA, 0xFF };
            unsigned char *fn = g_base ? g_base + (f10 ? 0x1EECD30 : 0x1EECF10) : NULL;
            int ok = g_buildOk && !g_moved && fn;
            if (ok) ok = !memcmp(fn, f10 ? sigD30 : sigF10, f10 ? sizeof sigD30 : sizeof sigF10);   // readable: the build was recognised
            if (!ok) {
                logf("TEST: Ctrl+Alt+Shift+%s - known renderer crash requested, but this is not the game build its addresses come from; nothing done", f10 ? "F10" : "F11");
                g_testCrash = 0;
                Sleep(1000);
                continue;
            }
            if (f10) {
                const size_t ctx = (size_t)0x1345 * 0x1C0;            // context 1 inside the renderer object
                unsigned char *obj = VirtualAlloc(NULL, ctx + 0x1C0 + 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (!obj) { logf("TEST: Ctrl+Alt+Shift+F10 - no memory for the stand-in object; nothing done"); g_testCrash = 0; continue; }
                unsigned long long *lists = (unsigned long long *)(obj + ctx + 0x1C0);
                lists[0] = 0xF988000000020000ull;                     // the garbage "sub-list pointer"
                *(unsigned *)(obj + ctx + 0x100) = 1;                 // one sub-list
                *(unsigned long long **)(obj + ctx + 0x68) = lists;
                logf("TEST: Ctrl+Alt+Shift+F10 - known renderer crash: the game's index buffer bind (witcher3.exe+0x1EECD30) with a garbage sub-list pointer");
                ((void (*)(void *, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned))fn)(obj, 1, 0, 1, 0, 1, 4);
            } else {
                logf("TEST: Ctrl+Alt+Shift+F11 - known renderer crash: the game's bind at witcher3.exe+0x1EECF10 with context handle 0");
                ((void (*)(void *, unsigned, unsigned, unsigned, unsigned, void *))fn)((void *)0x8000000000000000ull, 0, 0, 0, 0, NULL);
            }
            logf("TEST: the game's function returned without a crash; nothing else done");
            g_testCrash = 0;
            Sleep(1000);
        }
        // F12 (since the test build after test58): a call through an empty function pointer, the kind of crash one
        // player's report showed (crash address 0). The call stack must then start with "0x0 (an empty address ...)"
        // and go on with the caller taken from the top of the stack (here this function), instead of "not available".
        if ((GetAsyncKeyState(VK_F12) & 0x8000) && InterlockedCompareExchange(&g_testCrash, 7, 0) == 0) {
            logf("TEST: Ctrl+Alt+Shift+F12 - a call through an empty function pointer (crash address 0)");
            void (*volatile nothing)(void) = NULL;
            nothing();
        }
    }
    return 0;
}
#endif

// ---------------------------------------------------------------- init thread
typedef HRESULT (WINAPI *D3D12CreateDevice_t)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
typedef HRESULT (WINAPI *CreateDXGIFactory1_t)(REFIID, void **);

static DWORD WINAPI init(LPVOID unused) {
    (void)unused;
    // Wait for the game itself to bring up D3D12 (D3D12Core.dll loads at device creation).
    for (int i = 0; i < 1200 && !GetModuleHandleW(L"D3D12Core.dll"); i++) Sleep(100);
    if (!GetModuleHandleW(L"D3D12Core.dll")) { logf("D3D12Core.dll never loaded - not hooking"); return 0; }
    Sleep(2000);

    HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
    D3D12CreateDevice_t create = d3d12 ? (D3D12CreateDevice_t)GetProcAddress(d3d12, "D3D12CreateDevice") : NULL;

    // pick the adapter with the most dedicated VRAM (the RTX card, not the iGPU)
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");   // returns whatever dxgi the game already loaded
    CreateDXGIFactory1_t cf = dxgi ? (CreateDXGIFactory1_t)GetProcAddress(dxgi, "CreateDXGIFactory1") : NULL;
    IDXGIFactory1 *fac = NULL; IDXGIAdapter1 *best = NULL; SIZE_T bestMem = 0;
    if (cf && SUCCEEDED(cf(&IID_IDXGIFactory1, (void **)&fac))) {
        IDXGIAdapter1 *a;
        for (UINT i = 0; IDXGIFactory1_EnumAdapters1(fac, i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 ad; IDXGIAdapter1_GetDesc1(a, &ad);
            if (ad.DedicatedVideoMemory > bestMem) { if (best) IDXGIAdapter1_Release(best); best = a; bestMem = ad.DedicatedVideoMemory; }
            else IDXGIAdapter1_Release(a);
        }
    }
    if (best) {
        IDXGIAdapter1_QueryInterface(best, &IID_IDXGIAdapter3, (void **)&g_adapter);
        DXGI_ADAPTER_DESC1 ad; IDXGIAdapter1_GetDesc1(best, &ad);
        char name[128]; WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof name, NULL, NULL);
        logf("adapter: %s (%llu MB)", name, (unsigned long long)(ad.DedicatedVideoMemory >> 20));
        // The graphics driver version (Onno, 19 Nov 2026, with the GPU crash diagnostics of 2.0.0): if several GPU
        // crashes share one driver, that is a lead. Windows reports it as four numbers, for example 32.0.15.6614;
        // NVIDIA's own number (566.14) is the last five digits. Read once here, never on the crash path.
        LARGE_INTEGER umd = {0};
        if (SUCCEEDED(IDXGIAdapter1_CheckInterfaceSupport(best, &IID_IDXGIDevice, &umd))) {
            unsigned a = (unsigned)(umd.HighPart >> 16) & 0xFFFF, b = (unsigned)umd.HighPart & 0xFFFF,
                     c = (unsigned)(umd.LowPart >> 16) & 0xFFFF, d = (unsigned)umd.LowPart & 0xFFFF;
            if (ad.VendorId == 0x10DE) {
                unsigned nv = (c % 10) * 10000 + d;
                snprintf(g_driver, sizeof g_driver, "%u.%u.%u.%u (NVIDIA %u.%02u)", a, b, c, d, nv / 100, nv % 100);
            } else snprintf(g_driver, sizeof g_driver, "%u.%u.%u.%u", a, b, c, d);
        }
        logf("driver: %s", g_driver);
        snprintf(g_adapterName, sizeof g_adapterName, "%s (%llu MB), driver %s", name, (unsigned long long)(ad.DedicatedVideoMemory >> 20), g_driver);
    }

    ID3D12Device *dev = NULL;
    // D3D12 devices are per-adapter singletons: this returns the game's own device (AddRef'd).
    HRESULT hr = create ? create((IUnknown *)best, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev) : E_FAIL;
    if (FAILED(hr) || !dev) { logf("D3D12CreateDevice failed 0x%08lX - not hooking", (unsigned long)hr); return 0; }
    g_dev = dev;                                      // kept for the crash report's "GPU state" (we hold a reference)

    D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
    ID3D12DescriptorHeap *heap = NULL;
    ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&heap);

    int ok = patch((void **)&dev->lpVtbl->CreateDescriptorHeap, (void *)h_CreateDescriptorHeap, (void **)&o_CreateDescriptorHeap);
    if (heap) {
        ok &= patch((void **)&heap->lpVtbl->Release, (void *)h_HeapRelease, (void **)&o_HeapRelease);
        o_HeapRelease(heap);
    }
    logf("hooks installed: %s (device vtbl %p)", ok ? "yes" : "PARTIAL", (void *)dev->lpVtbl);
    // keep our device reference: it is the game's device and must outlive our hooks anyway
    guard_filter(0);

    game_settings(); other_files();
    for (;;) {
        for (int k = 0; k < 15; k++) { Sleep(2000); watch_events(); }
        summary("30s"); guard_filter(0); game_settings(); other_files();
    }
    return 0;
}

static void start_work(void);

#ifdef W3_LICENSE
// ---------------------------------------------------------------- license dialog
// At game start the mod shows a Windows dialog with the MIT License. Until the user accepts with OK, the mod does
// nothing: the fix is switched off, no hooks, no crash report. OK accepts: it stores the id of the license text as
// LicenseAccepted in W3CrashFix.ini next to the mod, and the dialog is not shown again unless the license changes (the
// installer stores the same when the user accepts there). OK with the "I decline" box ticked renames the mod's own file to
// W3CrashFix.removed, so that it no longer loads; nothing is deleted and no other program is started.
// The dialog is only shown on a game build the mod supports: on any other build the fix is off anyway.
#include "w3cf_license.h"
#define ID_ACCEPT  101
#define ID_DECLINE 102
#define ID_INTRO   104
#define ID_LICENSE 105
#define ID_NOTE    106
#define ID_CHANGED 108
#define ID_SOURCE  107
static HINSTANCE g_hmod;
static wchar_t g_self[MAX_PATH];                     // full path of the mod's own file
static const char g_licTitle[] = "W3CrashFix " W3CF_VERSION " - license";
static const char g_licIntro[] =
    "W3CrashFix is free software under the MIT License. It comes WITHOUT ANY WARRANTY: you use it at your own risk.\r\n\r\n"
    "It fixes a bug in the game's code in memory while the game runs. It changes no game files, and the mod itself "
    "sends nothing over the internet.\r\n\r\n"
    "Please read the license below, then click \"OK (Apply this mod)\". To decline, tick the box at the bottom first: "
    "W3CrashFix is then switched off and removed from this game.";
static const char g_licText[] = W3CF_LICENSE_TEXT;
static DWORD g_licOpened;                            // when the dialog appeared (GetTickCount)
static int g_licOlder;                               // an acceptance is stored, but for an earlier license text
static const char g_licChanged[] = "The license has changed since you accepted it. That is why this window is shown once more.";

// The acceptance is remembered per license text: LicenseAccepted in W3CrashFix.ini holds the id of the license it was
// given for (W3CF_LICENSE_ID). Without it the dialog is shown; with another id it is shown again and says why.
static int accepted_this_license(void) {
    char buf[64]; ini_get(L"LicenseAccepted", buf, sizeof buf);
    if (!buf[0]) return 0;
    if (!strcmp(buf, W3CF_LICENSE_ID)) return 1;
    g_licOlder = 1;                                  // accepted before, but another license text: the dialog says so
    return 0;
}

static WORD *dlg_item(WORD *p, DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const wchar_t *text) {
    p = (WORD *)(((ULONG_PTR)p + 3) & ~(ULONG_PTR)3);              // every item starts on a 4-byte boundary
    DLGITEMTEMPLATE *it = (DLGITEMTEMPLATE *)p;
    it->style = style | WS_CHILD | WS_VISIBLE; it->dwExtendedStyle = 0;
    it->x = x; it->y = y; it->cx = cx; it->cy = cy; it->id = id;
    p = (WORD *)(it + 1);
    *p++ = 0xFFFF; *p++ = cls;                                     // predefined class: 0x80 button, 0x81 edit, 0x82 static
    while (*text) *p++ = *text++;
    *p++ = 0;
    *p++ = 0;                                                      // no creation data
    return p;
}

static INT_PTR CALLBACK lic_proc(HWND d, UINT m, WPARAM w, LPARAM l) {
    (void)l;
    switch (m) {
    case WM_INITDIALOG:
        SetWindowTextA(d, g_licTitle);
        SetDlgItemTextA(d, ID_INTRO, g_licIntro);
        if (g_licOlder) SetDlgItemTextA(d, ID_CHANGED, g_licChanged);
        {   // Keep the company's and the game's name on one line each: no-break spaces inside them, so the text
            // box never wraps there.
            static wchar_t lic[4000];
            static const wchar_t *whole[] = { W3CF_COMPANY_W, W3CF_GAME_W };
            MultiByteToWideChar(CP_UTF8, 0, g_licText, -1, lic, 4000);
            for (int k = 0; k < 2; k++)
                for (wchar_t *n = wcsstr(lic, whole[k]); n; n = wcsstr(n + 1, whole[k]))
                    for (size_t i = 0; i < wcslen(whole[k]); i++) if (n[i] == L' ') n[i] = 0x00A0;
            SetDlgItemTextW(d, ID_LICENSE, lic);
        }
        SetForegroundWindow(d);
        SetFocus(GetDlgItem(d, ID_LICENSE));   // not on a button: a key pressed to skip the game's intro must not choose
        SendDlgItemMessageW(d, ID_LICENSE, EM_SETSEL, 0, 0);       // the focus would otherwise select the whole text
        g_licOpened = GetTickCount();
        return FALSE;
    case WM_CLOSE:                             // Alt+F4: ignored. The dialog has no X; it only closes with the OK button
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(w)) {                   // IDCANCEL (Esc) is ignored on purpose
        case IDOK:                             // Enter: the same as the OK button, for players without a mouse - but
            if (GetTickCount() - g_licOpened < 3000) return TRUE;   // not in the first 3 seconds (intro-skipping keys)
            /* fall through */
        case ID_ACCEPT:                        // the OK button: accepts, unless the decline box is ticked
            EndDialog(d, IsDlgButtonChecked(d, ID_DECLINE) == BST_CHECKED ? 3 : 1);
            return TRUE;
        case ID_SOURCE: {                      // "View source code": the project page, in the user's own browser
            wchar_t line[] = L"explorer.exe \"" W3CF_SOURCE_URL_W L"\"";   // the same way the crash report page is opened
            STARTUPINFOW si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof si); si.cb = sizeof si;
            if (CreateProcessW(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
            return TRUE;
        }
        case ID_DECLINE:                       // the decline box: the button says what OK will now do
            SetDlgItemTextA(d, ID_ACCEPT, IsDlgButtonChecked(d, ID_DECLINE) == BST_CHECKED ? "OK (Remove this mod)" : "OK (Apply this mod)");
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static int license_dialog(void) {
    static DWORD buf[256];                                         // DWORD array: correctly aligned for the template
    memset(buf, 0, sizeof buf);
    DLGTEMPLATE *t = (DLGTEMPLATE *)buf;
    t->style = DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION;   // no WS_SYSMENU: no X in the title bar
    t->dwExtendedStyle = WS_EX_TOPMOST;                            // stays visible on top of the game's window
    // After an acceptance of an earlier license text, one extra line on top says why the window is back; everything
    // else moves down by o dialog units.
    short o = g_licOlder ? 22 : 0;
    t->cdit = g_licOlder ? 7 : 6; t->cx = 300; t->cy = 254 + o;
    WORD *p = (WORD *)(t + 1);
    *p++ = 0; *p++ = 0; *p++ = 0;                                  // no menu, default dialog class, title set later
    *p++ = 9;                                                      // font: 9 pt Segoe UI
    for (const wchar_t *f = L"Segoe UI"; *f; f++) *p++ = *f;
    *p++ = 0;
    if (g_licOlder) p = dlg_item(p, SS_LEFT, 7, 7, 286, 18, ID_CHANGED, 0x82, L"");
    p = dlg_item(p, SS_LEFT, 7, 7 + o, 286, 64, ID_INTRO, 0x82, L"");
    p = dlg_item(p, ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_BORDER | WS_TABSTOP, 7, 74 + o, 286, 118, ID_LICENSE, 0x81, L"");
    p = dlg_item(p, BS_AUTOCHECKBOX | WS_TABSTOP, 7, 198 + o, 220, 12, ID_DECLINE, 0x80, L"I decline. Remove W3CrashFix from this game.");
    p = dlg_item(p, BS_PUSHBUTTON | WS_TABSTOP, 7, 218 + o, 100, 15, ID_SOURCE, 0x80, L"View source code");
    p = dlg_item(p, BS_PUSHBUTTON | WS_TABSTOP, 193, 218 + o, 100, 15, ID_ACCEPT, 0x80, L"OK (Apply this mod)");   // text changes with the decline box
    p = dlg_item(p, SS_RIGHT, 7, 238 + o, 286, 10, ID_NOTE, 0x82, L"This window will not appear again unless the license changes.");
    INT_PTR r = DialogBoxIndirectParamW(g_hmod, t, NULL, lic_proc, 0);
    if (r < 0) logf("license: the dialog could not be shown (error %lu)", GetLastError());
    return (int)r;
}

// While the dialog is open the game must not continue behind it. A dialog can't be shown safely while Windows is still
// loading the mod, so the game is held a little later, at a safe point: when it asks Windows to create its window.
// The game's import of CreateWindowExA is pointed at h_CreateWindowExA, which waits until the user has chosen.
typedef HWND (WINAPI *CWEA_t)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static HANDLE g_licDone;                             // set once the user has chosen (or the dialog is gone)
static void **g_cwSlot;
static CWEA_t g_cwOrig;
static volatile LONG g_held;
static HWND WINAPI h_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h,
                                     HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    if (WaitForSingleObject(g_licDone, 0) == WAIT_TIMEOUT) {
        if (InterlockedExchange(&g_held, 1) == 0) logf("license: the game wants to create its window - held until the user has chosen");
        WaitForSingleObject(g_licDone, INFINITE);
        logf("license: the game continues");
    }
    return g_cwOrig(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
}
static void set_slot(void **slot, void *value) {
    DWORD old;
    if (VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) { *slot = value; VirtualProtect(slot, sizeof *slot, old, &old); }
}
static void hold_game(void) {
    unsigned char *b = (unsigned char *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    HMODULE user = GetModuleHandleW(L"user32.dll");
    g_cwOrig = user ? (CWEA_t)GetProcAddress(user, "CreateWindowExA") : NULL;
    g_licDone = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (rva && g_cwOrig && g_licDone)
        for (IMAGE_IMPORT_DESCRIPTOR *im = (IMAGE_IMPORT_DESCRIPTOR *)(b + rva); im->Name && !g_cwSlot; im++) {
            if (lstrcmpiA((const char *)b + im->Name, "USER32.dll") || !im->OriginalFirstThunk) continue;
            IMAGE_THUNK_DATA64 *nm = (IMAGE_THUNK_DATA64 *)(b + im->OriginalFirstThunk), *sl = (IMAGE_THUNK_DATA64 *)(b + im->FirstThunk);
            for (; nm->u1.AddressOfData; nm++, sl++)
                if (!(nm->u1.Ordinal & IMAGE_ORDINAL_FLAG64) &&
                    !strcmp((const char *)((IMAGE_IMPORT_BY_NAME *)(b + nm->u1.AddressOfData))->Name, "CreateWindowExA")) {
                    g_cwSlot = (void **)&sl->u1.Function;
                    set_slot(g_cwSlot, (void *)h_CreateWindowExA);
                    break;
                }
        }
    logf(g_cwSlot ? "license: the game will wait with its window until the user has chosen"
                  : "license: the game's window function was not found - the game starts behind the dialog");
}
static void release_game(void) {
    if (g_cwSlot) set_slot(g_cwSlot, (void *)g_cwOrig);            // back to Windows' own function
    if (g_licDone) SetEvent(g_licDone);
}

static DWORD WINAPI license_thread(LPVOID unused) {
    (void)unused;
    logf("license: asking the user to accept the MIT License");
    if (g_licOlder) logf("license: an acceptance of an earlier license text was found - the window says that the license has changed");
    int r = license_dialog();
    if (r == 1) {
        if (ini_set(L"LicenseAccepted", W3CF_LICENSE_ID)) logf("license: accepted (saved in W3CrashFix.ini; the dialog will not be shown again unless the license changes)");
        else logf("license: accepted, but W3CrashFix.ini could not be written (error %lu) - the dialog will return", GetLastError());
        g_accepted = 1;
        if (g_fixInstalled) {
            g_fixActive = 1;
            logf("fix: descriptor page recycling fix ACTIVE (w3+0x%X detoured)", g_at[F_ENSURE]);
        }
        if (g_dredWanted && g_cwSlot) dred_switch_on((LPVOID)1);    // the game is still held before its window: see dred_switch_on
        start_work();
    } else if (r == 3) {
        wchar_t to[MAX_PATH]; lstrcpyW(to, g_dir); lstrcatW(to, L"W3CrashFix.removed");
        DeleteFileW(g_ini);                                         // the settings go too: an earlier acceptance, the report id
        char msg[900], self8[MAX_PATH * 3], to8[MAX_PATH * 3];
        WideCharToMultiByte(CP_ACP, 0, g_self, -1, self8, sizeof self8, NULL, NULL);
        WideCharToMultiByte(CP_ACP, 0, to, -1, to8, sizeof to8, NULL, NULL);
        if (MoveFileExW(g_self, to, MOVEFILE_REPLACE_EXISTING)) {   // Windows allows a rename of a loaded file, but no delete and no Recycle Bin
            logf("license: declined - the mod's file was renamed to W3CrashFix.removed and will not load again");
            snprintf(msg, sizeof msg, "W3CrashFix is switched off and will not load again.\n\nIts file was renamed to:\n%s\n\nYou can delete that file.", to8);
        } else {
            logf("license: declined - the mod's file could not be renamed (error %lu); the fix stays off for this session", GetLastError());
            snprintf(msg, sizeof msg, "W3CrashFix is switched off for this session, but its file could not be renamed.\n\nTo remove the mod, close the game and delete:\n%s", self8);
        }
        MessageBoxA(NULL, msg, g_licTitle, MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
    } else logf("license: the dialog ended without a choice - the fix stays off for this session");   // only if it could not be shown
    release_game();
    return 0;
}
#endif

// Exported by the .asi build (see w3crashfix_asi.def) so that only this one symbol is public.
const char *W3CrashFixVersion(void) { return "W3CrashFix " W3CF_VERSION " for witcher3.exe 5.0.16.41566 (DX12)"; }

static HANDLE open_log(const wchar_t *path) {
    return CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

// W3CrashFix-send-log.html (Onno, 19 Nov 2026: "unmodded games without crashes require a way to submit .activity logs",
// "so do modded games", "go with 2"). The report page only appears after a crash, so a player whose game did NOT crash
// had no way to send a log. This page is written at every game start next to the mod and is never opened by the mod:
// the player opens it from the game's bin\x64_dx12 folder (README and Nexus say so). It has the same Send button as the
// report page, for W3CrashFix.activity.log. On a game build the mod doesn't support (for example the Epic version) it
// says that this log is especially helpful: its "diag:" lines show how that build differs. Nothing is sent by the mod.
static void write_send_page(void) {
    wchar_t path[MAX_PATH];
    HANDLE f = create_in_dir(L"W3CrashFix-send-log.html", path);
    if (f == INVALID_HANDLE_VALUE) { logf("send page: could not write W3CrashFix-send-log.html (%lu)", GetLastError()); return; }
    char dir8[3 * MAX_PATH], dirEsc[4 * MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, g_dir, -1, dir8, sizeof dir8, NULL, NULL);
    html_esc(dir8, dirEsc, sizeof dirEsc);
    put(f, "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><title>Send your W3CrashFix log</title><style>"
           "body{font:16px/1.5 Segoe UI,Arial,sans-serif;max-width:860px;margin:32px 40px;padding:0 16px;box-sizing:border-box;background:#16181c;color:#e8e6e3}"
           "h1{font-size:26px;margin:0 0 4px}a{color:#8ab4f8}"
           ".b{display:inline-block;margin:6px 10px 6px 0;padding:10px 18px;border-radius:6px;border:0;background:#c8102e;color:#fff;font:inherit;font-weight:600;text-decoration:none;cursor:pointer}"
           ".g{background:#30363d;padding:6px 12px;font-weight:400}code{background:#0d0e11;padding:2px 5px;border-radius:4px}.s{color:#9aa0a6;font-size:14px}li{margin:4px 0}"
           ".o{background:#3a2a00;border:1px solid #b8860b;border-radius:6px;padding:10px 14px}"
           // Send button always in view (Onno, 20 Nov 2026: "the send button should always be visible. i had to scroll down"): its line sticks to the bottom of the window until the page is scrolled past it
           ".sb{position:sticky;bottom:0;margin:0;padding:10px 0;background:#16181c;border-top:1px solid #2c3036;z-index:1}"
           "</style></head><body><h1>Send your W3CrashFix log</h1>"
           "<p><b>Every log is welcome, also from a game that did not crash</b>, with or without other mods. A session without a crash "
           "shows how the game behaves when all goes well, and can be compared with sessions that crashed. "
           "Logs of both, with and without mods, from the same PC are especially helpful.</p>");
    if (!g_buildOk)
        put(f, "<p class=\"o\"><b>Your log is especially helpful.</b> W3CrashFix does not support this build of the game (for example the "
               "Epic version), so the fix is off. The log shows how this build differs, which is what is needed to make the fix work on it.</p>");
    put(f, "<p class=\"s\">This page is a file on your own PC, not a website: look at the address bar, it starts with <code>file:</code>. "
           "The mod itself never connects to the internet. Only your browser does, and only when you click the button.</p>"
           "<p><b>The file to send:</b><br><code>");
    put(f, dirEsc);
    put(f, "W3CrashFix.activity.log</code><br><span class=\"s\">It holds the session that is running now, or the last one if the game is closed. "
           "Best sent after you close the game and before you start it again: at the next start it is renamed to "
           "<code>W3CrashFix.activity.previous.log</code> (you can send that one instead), and the start after that replaces it.</span></p>"
           "<p class=\"sb\"><a class=\"b\" target=\"_blank\" rel=\"noopener\" href=\"" W3CF_UPLOAD_URL "\" data-p=\"");
    put(f, dirEsc);
    put(f, "W3CrashFix.activity.log\" onclick=\"cp(this)\">Send the activity log</a> <span class=\"s\" id=\"c\"></span></p>"
           "<p><b>How it works</b></p><ol>"
           "<li>Click the button. A pCloud page opens, and the log's path is copied. If the page shows a cookie notice, answer it first; it can cover the buttons.</li>"
           "<li>On that page, click <b>Click Here to Add Files</b>. In the window that opens, press Ctrl+V and then Enter.</li>"
           "<li>Type any name you like. A made-up one is fine.</li>"
           "<li>Click Upload. When you see &quot;Files are uploaded!&quot;, you are done and can close that tab.</li></ol>"
           "<p class=\"s\">pCloud then offers you an account. You don't need one.</p>"
           // Onno, 20 Nov 2026: "inform the user that accidentally uploading a wrong file is not a big problem"
           "<p class=\"s\">Sent the wrong file by mistake? No problem: anything that isn't a W3CrashFix log or crash dump is deleted. You can just send the right file afterwards.</p>"
           "<p class=\"s\">Ctrl+V pasted nothing? <button class=\"b g\" data-p=\"");
    put(f, dirEsc);
    put(f, "W3CrashFix.activity.log\" onclick=\"cp(this)\">Copy the file's path</button> and paste it again into the &quot;File name&quot; box.</p>"
           "<p class=\"s\">Did the game crash? Then send the crash log instead: W3CrashFix opens a report page for it after the crash.</p>"
           "<p class=\"s\">What is sent: only the file you pick, and the name you type on the upload page. The log is technical data, which can "
           "include the folder the game is installed in and the folder names of your in-game mods. Only the mod's author can see uploaded files. "
           "The &quot;report id&quot; in it is a random number made on your PC, so that several logs from the same machine/installation can be "
           "recognised; it says nothing about you. A log sent this way can't be answered. For questions, use the "
           "<a target=\"_blank\" rel=\"noopener\" href=\"https://www.nexusmods.com/witcher3/mods/15624?tab=posts\">posts on Nexus Mods</a> or the "
           "<a target=\"_blank\" rel=\"noopener\" href=\"https://github.com/Havoc104/W3CrashFix/issues\">issues on GitHub</a>.</p>"
           "<p class=\"s\">W3CrashFix writes this page at every game start; the installer's Uninstall removes it.</p>"
           "<script>function cp(b){var t=b.getAttribute('data-p'),a=document.createElement('textarea');"
           "a.value=t;document.body.appendChild(a);a.select();try{document.execCommand('copy')}catch(e){}document.body.removeChild(a);"
           "if(navigator.clipboard&&navigator.clipboard.writeText){navigator.clipboard.writeText(t).catch(function(){})}"
           "if(b.tagName=='A'){document.getElementById('c').textContent='The path is copied: paste it with Ctrl+V on the upload page.'}"
           "else{b.textContent='Path copied'}}</script></body></html>");
    CloseHandle(f);
    logf("send page: W3CrashFix-send-log.html written (open it to send this log, also without a crash)");
}

// Everything the mod does besides the fix itself: crash report, heap statistics. Runs only after the license is accepted.
static void start_work(void) {
    char opv[8]; ini_get(L"OpenReportPage", opv, sizeof opv);
    if (!opv[0]) ini_set(L"OpenReportPage", "1");        // make the setting visible to anyone who opens the file
    char gcd[8]; ini_get(L"GpuCrashDetails", gcd, sizeof gcd);
    if (!gcd[0]) ini_set(L"GpuCrashDetails", "1");       // the same, for the GPU details (on by default, see load_settings)
    if (g_dredWanted) {                                  // before the game creates its graphics device; init waits for that
        HANDLE g = CreateThread(NULL, 0, dred_switch_on, NULL, 0, NULL);
        if (g) CloseHandle(g);
    }
    char cdv[8]; ini_get(L"CrashDump", cdv, sizeof cdv);
    if (!cdv[0]) ini_set(L"CrashDump", "0");             // visible, off by default (see "crash dump")
    if (g_dumpWanted) {                                  // dbghelp.dll is loaded now, never during a crash
        HANDLE g = CreateThread(NULL, 0, dump_load, NULL, 0, NULL);
        if (g) CloseHandle(g);
    }
    ini_shorten_notes();
    load_report_id();
    ini_note_settings();
    scan_mods();
    write_send_page();
    guard_filter(1);
    AddVectoredExceptionHandler(1, crash_notice);
#ifdef W3_CRASHTEST
    logf("TEST BUILD - crash keys are enabled: Ctrl+Alt+Shift+F7 = plain crash, Ctrl+Alt+Shift+F8 = descriptor heap crash, Ctrl+Alt+Shift+F9 = GPU crash (device removed)");
    HANDLE k = CreateThread(NULL, 0, crash_keys, NULL, 0, NULL);
    if (k) CloseHandle(k);
#endif
    HANDLE t = CreateThread(NULL, 0, init, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        g_t0 = GetTickCount();
        wchar_t p[MAX_PATH], prev[MAX_PATH];
        GetModuleFileNameW(h, p, MAX_PATH);
#ifdef W3_LICENSE
        g_hmod = h;
        lstrcpyW(g_self, p);
#endif
        wchar_t *s = wcsrchr(p, L'\\'); if (s) s[1] = 0;
        lstrcpyW(g_dir, p);
        lstrcpyW(prev, p);
        // The activity log. (Up to 1.5.0 it was W3CrashFix.log, which was easy to mix up with the crash log.)
        wchar_t old[MAX_PATH], oldPrev[MAX_PATH];
        lstrcpyW(old, p); lstrcatW(old, L"W3CrashFix.log");
        lstrcpyW(oldPrev, p); lstrcatW(oldPrev, L"W3CrashFix.previous.log");
        lstrcatW(p, L"W3CrashFix.activity.log");
        lstrcatW(prev, L"W3CrashFix.activity.previous.log");
#ifdef W3_ASI
        const char *flavor = "ASI plugin";
#else
        const char *flavor = "dinput8 proxy";
#endif
        // Only one copy may patch the game: dinput8.dll and the .asi build installed together would
        // otherwise hook everything twice. The second copy to load stays passive (and leaves the log alone).
        wchar_t name[64];
        wsprintfW(name, L"Local\\W3CrashFix-%lu", GetCurrentProcessId());
        HANDLE once = CreateMutexW(NULL, FALSE, name);
        if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
            g_log = open_log(p);
            logf("==== W3CrashFix %s (%s) loaded, pid %lu: another copy is already active - this one does nothing ====",
                 W3CF_VERSION, flavor, GetCurrentProcessId());
            return TRUE;   // keep the mutex handle open for the process lifetime
        }
        // One log per game session; the previous session's log is kept (useful after a crash).
        // Logs under the old names: the last one becomes this session's "previous" log if there is none yet; the
        // rest is removed, so that no stale log is left to be read by mistake.
        int hadPrev = MoveFileExW(p, prev, MOVEFILE_REPLACE_EXISTING);
        if (!hadPrev) MoveFileExW(old, prev, MOVEFILE_REPLACE_EXISTING);
        DeleteFileW(old); DeleteFileW(oldPrev);
        g_log = open_log(p);
        logf("==== W3CrashFix %s (%s) loaded, pid %lu ====", W3CF_VERSION, flavor, GetCurrentProcessId());
        g_flavor = flavor;
        load_settings();
#ifdef W3_LICENSE
        // The license: accepted for this version in an earlier session (W3CrashFix.ini), or asked in a dialog.
        // The dialog is only for the game itself; in any other program the mod never patches anything anyway.
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        int ask = wcsstr(exe, L"witcher3.exe") && !accepted_this_license();
        g_accepted = !ask;
        // The detour goes in now, before any game code runs: no thread can be inside the patched function. While
        // the license is not accepted it behaves exactly like the game's own code (see fixed_ensure).
        install_fix();
#ifdef W3_LICENSE_TEST
        // Build tests only (never in a release, see build.ps1): the stand-in "game" of the tests is not a supported
        // build, and the dialog must still be testable.
        logf("license dialog test variant: the dialog is also shown on an unsupported build");
#else
        // On a game build the mod doesn't support, the fix is off anyway: there is nothing to accept, so no dialog.
        if (ask && !g_buildOk) { ask = 0; g_accepted = 1; }
#endif
        if (!ask) start_work();
        else {
            hold_game();
            HANDLE t = CreateThread(NULL, 0, license_thread, NULL, 0, NULL);   // runs once DllMain has returned
            if (t) CloseHandle(t); else release_game();
        }
#else
        install_fix();   // before any game code runs: no thread can be inside the patched function
        start_work();
#endif
    }
    return TRUE;
}
