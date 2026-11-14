// W3CrashFix - in-memory crash fix for The Witcher 3: Wild Hunt Remastered, DX12, witcher3.exe 5.0.16.41566.
// Copyright (c) 2026 Onno Struijk (Havoc104) and Claude. MIT License, see LICENSE.
//
// The fix (THE FIX below): descriptor-page leak that ends in a crash at witcher3.exe+0x280ad3d.
// Also logs descriptor heap statistics to W3CrashFix.log next to the module.
// Built as dinput8.dll (standalone proxy) or W3CrashFix.asi (for an ASI loader). Nothing on disk is modified;
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
static unsigned char *g_base;
static wchar_t g_dir[MAX_PATH];                      // folder of the mod, with trailing backslash
static int g_buildOk, g_fixInstalled;                // game code recognised / detour installed
static volatile LONG g_fixActive;                    // detour installed AND the license accepted: pages are rescued
// The license dialog (see "license dialog" below) is compiled in with -DW3_LICENSE, which build.ps1 always passes.
// Built without it, g_accepted is always 1 and the mod starts working right away.
static volatile LONG g_accepted = 1;                 // the user accepted the license (now or in an earlier session)
#ifdef W3_CRASHTEST
static volatile LONG g_testCrash;                    // 1 = real heap crash requested, 2 = done, 3 = plain crash
#endif
typedef void (*Invalidate_t)(unsigned char *alloc, void *page);
typedef void *(*CreatePage_t)(unsigned char *alloc);
typedef void (*VecGrow_t)(unsigned char *vec, long long elemSize);

#define F64(p, o) (*(long long *)((unsigned char *)(p) + (o)))
#define F32(p, o) (*(int *)((unsigned char *)(p) + (o)))
#define FPTR(p, o) (*(void **)((unsigned char *)(p) + (o)))

static void vec_push(unsigned char *v, void *x) {
    if (!(F64(v, V_CAPACITY) > F64(v, V_COUNT))) ((VecGrow_t)(g_base + RVA_VECGROW))(v, 8);
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
        ((Invalidate_t)(g_base + RVA_INVALIDATE))(a, page);   // drop cached tables pointing into it
    } else {
        page = ((CreatePage_t)(g_base + RVA_CREATEPAGE))(a);
    }
    FPTR(a, A_CUR) = page;
    F32(a, A_USED) = 0;
}

static int hexmatch(const unsigned char *p, const char *hex) {
    for (; hex[0] && hex[1]; hex += 2, p++) {
        unsigned v; char b[3] = {hex[0], hex[1], 0};
        v = (unsigned)strtoul(b, NULL, 16);
        if (*p != v) return 0;
    }
    return 1;
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

static void install_fix(void) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!wcsstr(exe, L"witcher3.exe")) { logf("fix: not witcher3.exe - disabled"); return; }
    g_base = (unsigned char *)GetModuleHandleW(NULL);
    DWORD image = ((IMAGE_NT_HEADERS64 *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew))->OptionalHeader.SizeOfImage;
    for (unsigned i = 0; i < sizeof g_sig / sizeof g_sig[0]; i++)
        if ((unsigned long long)g_sig[i].rva + 24 > image || !readable(g_base + g_sig[i].rva, 24) ||
            !hexmatch(g_base + g_sig[i].rva, g_sig[i].hex)) {
            logf("fix: code at w3+0x%X does not match the supported game build (Steam or GOG, 5.0.16) - FIX DISABLED (probe still logs)", g_sig[i].rva);
            diagnose();
            return;
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
    if (VirtualProtect(g_base + RVA_ENSURE, sizeof jmp, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy(g_base + RVA_ENSURE, jmp, sizeof jmp);
        VirtualProtect(g_base + RVA_ENSURE, sizeof jmp, old, &old);
        FlushInstructionCache(GetCurrentProcess(), g_base + RVA_ENSURE, sizeof jmp);
        g_fixInstalled = 1;
        if (g_accepted) {
            g_fixActive = 1;
            logf("fix: descriptor page recycling fix ACTIVE (w3+0x%X detoured)", RVA_ENSURE);
        } else logf("fix: ready, but switched off until the license is accepted (the game behaves as without the mod)");
    } else logf("fix: VirtualProtect failed %lu - FIX DISABLED", GetLastError());
}

// ---------------------------------------------------------------- crash report
// When the game crashes, the mod writes what crashed into the log, saves the session as W3CrashFix.crash.log with a
// short summary on top, and writes W3CrashFix-report.html next to it and opens that page. The page shows the summary
// and two ways to report it (a pre-filled GitHub issue, or copy the summary and open the Nexus page). Nothing is
// sent by the mod itself. An empty file named W3CrashFix.noreport next to the mod stops the page from opening.
#define RVA_CRASH 0x280ad3d
// <mod version>-<game version it is built for>; the game part changes whenever the fix is ported to a new patch
#define W3CF_VERSION "1.5.0-5.0.16"
static const char *g_flavor = "";
static char g_adapterName[128] = "unknown";
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter;
static volatile LONG g_crashed, g_noticed;
static char g_crSum[2048], g_crBody[2400], g_crEnc[8192], g_crEsc[4096], g_crBuf[65536];

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

static void url_enc(const char *s, char *out, int max) {
    int n = 0;
    for (; *s && n < max - 4; s++) {
        unsigned char ch = (unsigned char)*s;
        if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '-' || ch == '_' || ch == '.')
            out[n++] = (char)ch;
        else n += sprintf(out + n, "%%%02X", ch);
    }
    out[n] = 0;
}
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

    long long rva = g_base ? (long long)((unsigned char *)er->ExceptionAddress - g_base) : -1;
    const char *verdict =
        !g_buildOk ? "W3CrashFix does not support this build of the game, so it cannot tell which crash this is."
        : rva != RVA_CRASH ? "This is NOT the crash that W3CrashFix fixes (that one is at witcher3.exe+0x280AD3D). Something else crashed the game."
        : g_fixActive ? "This is the descriptor heap crash that W3CrashFix is made for, although the fix was active. That should not happen: please report it."
        : "This is the descriptor heap crash that W3CrashFix is made for. The fix was not active in this session.";
    const char *test = "";
#ifdef W3_CRASHTEST
    if (g_testCrash) test = "Note: this crash was triggered on purpose with a test key of this TEST BUILD.\r\n";
#endif
    snprintf(g_crSum, sizeof g_crSum,
             "W3CrashFix crash report\r\n"
             "Mod: %s (%s)\r\n"
             "Fix active: %s\r\n"
             "Crash: exception 0x%08lX (%s%s) at %s\r\n"
             "Session length: %lu min\r\n"
             "Graphics card: %s\r\n"
             "Descriptor heaps: %lld live + %lld sampler (peak %lld + %lld), %lld failed creations\r\n"
             "Fix: %lld pages rescued, %lld retired normally\r\n"
             "Verdict: %s\r\n%s",
             "W3CrashFix " W3CF_VERSION, g_flavor, g_fixActive ? "yes" : "no",
             (unsigned long)code, exc_name(code), detail, at,
             (unsigned long)((GetTickCount() - g_t0) / 60000), g_adapterName,
             g_live[0][1], g_live[1][1], g_peakSv[0], g_peakSv[1], g_failed,
             (long long)g_rescued, (long long)g_retiredNormal, verdict, test);

    // W3CrashFix.crash.log = the summary + this session's whole log. Only a newer crash overwrites it.
    wchar_t path[MAX_PATH], src[MAX_PATH];
    HANDLE f = create_in_dir(L"W3CrashFix.crash.log", path);
    if (f != INVALID_HANDLE_VALUE) {
        put(f, g_crSum); put(f, "\r\n---------------- full log of the crashed session ----------------\r\n");
        lstrcpyW(src, g_dir); lstrcatW(src, L"W3CrashFix.log");
        HANDLE in = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        if (in != INVALID_HANDLE_VALUE) {
            DWORD got, w;
            while (ReadFile(in, g_crBuf, sizeof g_crBuf, &got, NULL) && got) WriteFile(f, g_crBuf, got, &w, NULL);
            CloseHandle(in);
        }
        CloseHandle(f);
    }

    // W3CrashFix-report.html
    char title[320], dir8[3 * MAX_PATH], dirEsc[4 * MAX_PATH];
    f = create_in_dir(L"W3CrashFix-report.html", path);
    if (f == INVALID_HANDLE_VALUE) { logf("crash report: could not write the report page (%lu)", GetLastError()); return; }
    put(f, "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><title>W3CrashFix crash report</title><style>"
           "body{font:16px/1.5 Segoe UI,Arial,sans-serif;max-width:860px;margin:32px auto;padding:0 16px;background:#16181c;color:#e8e6e3}"
           "h1{font-size:26px;margin:0 0 4px}pre{background:#0d0e11;border:1px solid #333;border-radius:6px;padding:14px;white-space:pre-wrap;word-break:break-word}"
           ".b{display:inline-block;margin:6px 10px 6px 0;padding:10px 18px;border-radius:6px;border:0;background:#c8102e;color:#fff;font:inherit;font-weight:600;text-decoration:none;cursor:pointer}"
           ".g{background:#30363d}code{background:#0d0e11;padding:2px 5px;border-radius:4px}.s{color:#9aa0a6;font-size:14px}"
           "</style></head><body><h1>The Witcher 3 crashed</h1>"
           "<p>W3CrashFix noticed the crash and saved a report on your PC. <b>Nothing has been sent anywhere.</b> "
           "If you would like to help, you can send the summary below with one of the two buttons.</p><pre id=\"s\">");
    html_esc(g_crSum, g_crEsc, sizeof g_crEsc); put(f, g_crEsc);
    put(f, "</pre><p><a class=\"b\" target=\"_blank\" rel=\"noopener\" href=\"https://github.com/Havoc104/W3CrashFix/issues/new?title=");
    snprintf(title, sizeof title, "Crash report: 0x%08lX at %s", (unsigned long)code, at);
    url_enc(title, g_crEnc, sizeof g_crEnc); put(f, g_crEnc);
    put(f, "&amp;body=");
    snprintf(g_crBody, sizeof g_crBody, "%s\r\nPlease attach W3CrashFix.crash.log from the game's bin\\x64_dx12 folder: drag the file into this text box.\r\n", g_crSum);
    url_enc(g_crBody, g_crEnc, sizeof g_crEnc); put(f, g_crEnc);
    put(f, "\">Report on GitHub</a><button class=\"b g\" onclick=\"nexus()\">Copy summary and open Nexus Mods</button></p>"
           "<p class=\"s\">The GitHub button opens a new issue with the summary already filled in (you need a GitHub account). "
           "The Nexus button copies the summary and opens the mod's Posts tab, where you can paste it.</p>"
           "<p>The full log of the crashed session is saved here:<br><code>");
    WideCharToMultiByte(CP_UTF8, 0, g_dir, -1, dir8, sizeof dir8, NULL, NULL);
    html_esc(dir8, dirEsc, sizeof dirEsc); put(f, dirEsc);
    put(f, "W3CrashFix.crash.log</code><br>Please add that file to your report. It stays there until the game crashes again.</p>"
           "<p class=\"s\">The game's own crash reporter may also appear. That one reports to the game's developers; this page is only about the W3CrashFix mod. You can use both.</p>"
           "<p class=\"s\">Don't want this page to open after a crash? Create an empty file named <code>W3CrashFix.noreport</code> "
           "in that folder. The crash log is still saved.</p>"
           "<script>function nexus(){var t=document.getElementById('s').innerText,a=document.createElement('textarea');"
           "a.value=t;document.body.appendChild(a);a.select();try{document.execCommand('copy')}catch(e){}document.body.removeChild(a);"
           "if(navigator.clipboard&&navigator.clipboard.writeText){navigator.clipboard.writeText(t).catch(function(){})}"
           "window.open('https://www.nexusmods.com/witcher3/mods/15624?tab=posts','_blank')}</script></body></html>");
    CloseHandle(f);

    wchar_t off[MAX_PATH]; lstrcpyW(off, g_dir); lstrcatW(off, L"W3CrashFix.noreport");
    if (GetFileAttributesW(off) != INVALID_FILE_ATTRIBUTES) { logf("crash report: saved; the page was not opened because W3CrashFix.noreport exists"); return; }
    wchar_t cmd[MAX_PATH + 32]; wsprintfW(cmd, L"explorer.exe \"%s\"", path);
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        logf("crash report: saved and opened W3CrashFix-report.html");
    } else logf("crash report: saved, but the page could not be opened (%lu)", GetLastError());
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
// TEST BUILDS ONLY - never part of a release. Two key combinations crash the game on purpose, while the game window
// is in front:  Ctrl+Alt+Shift+F7 = plain crash inside the mod,  Ctrl+Alt+Shift+F8 = the real descriptor heap crash.
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
        snprintf(g_adapterName, sizeof g_adapterName, "%s (%llu MB)", name, (unsigned long long)(ad.DedicatedVideoMemory >> 20));
    }

    ID3D12Device *dev = NULL;
    // D3D12 devices are per-adapter singletons: this returns the game's own device (AddRef'd).
    HRESULT hr = create ? create((IUnknown *)best, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev) : E_FAIL;
    if (FAILED(hr) || !dev) { logf("D3D12CreateDevice failed 0x%08lX - not hooking", (unsigned long)hr); return 0; }

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

    for (;;) { Sleep(30000); summary("30s"); guard_filter(0); }
    return 0;
}

static void start_work(void);

#ifdef W3_LICENSE
// ---------------------------------------------------------------- license dialog
// At game start the mod shows a Windows dialog with the MIT License. Until the user accepts with OK, the mod does
// nothing: the fix is switched off, no hooks, no crash report. OK accepts: it writes the id of the license text into
// W3CrashFix.accepted next to the mod, and the dialog is not shown again unless the license changes (the installer
// writes the same file when the user accepts there). OK with the "I decline" box ticked renames the mod's own file to
// W3CrashFix.removed, so that it no longer loads; nothing is deleted and no other program is started.
// The dialog is only shown on a game build the mod supports: on any other build the fix is off anyway.
#include "w3cf_license.h"
#define ID_ACCEPT  101
#define ID_DECLINE 102
#define ID_INTRO   104
#define ID_LICENSE 105
#define ID_NOTE    106
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

// The acceptance is remembered per license text: W3CrashFix.accepted holds the id of the license it was given for
// (W3CF_LICENSE_ID). With another id, or an empty or unreadable file, the dialog is shown again.
static int accepted_this_license(void) {
    wchar_t path[MAX_PATH]; lstrcpyW(path, g_dir); lstrcatW(path, L"W3CrashFix.accepted");
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    char buf[64]; DWORD n = 0;
    if (!ReadFile(f, buf, sizeof buf - 1, &n, NULL)) n = 0;
    CloseHandle(f);
    while (n && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ')) n--;
    buf[n] = 0;
    return !strcmp(buf, W3CF_LICENSE_ID);
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
        SetDlgItemTextA(d, ID_LICENSE, g_licText);
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
    t->cdit = 6; t->cx = 300; t->cy = 254;
    WORD *p = (WORD *)(t + 1);
    *p++ = 0; *p++ = 0; *p++ = 0;                                  // no menu, default dialog class, title set later
    *p++ = 9;                                                      // font: 9 pt Segoe UI
    for (const wchar_t *f = L"Segoe UI"; *f; f++) *p++ = *f;
    *p++ = 0;
    p = dlg_item(p, SS_LEFT, 7, 7, 286, 64, ID_INTRO, 0x82, L"");
    p = dlg_item(p, ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_BORDER | WS_TABSTOP, 7, 74, 286, 118, ID_LICENSE, 0x81, L"");
    p = dlg_item(p, BS_AUTOCHECKBOX | WS_TABSTOP, 7, 198, 220, 12, ID_DECLINE, 0x80, L"I decline. Remove W3CrashFix from this game.");
    p = dlg_item(p, BS_PUSHBUTTON | WS_TABSTOP, 7, 218, 100, 15, ID_SOURCE, 0x80, L"View source code");
    p = dlg_item(p, BS_PUSHBUTTON | WS_TABSTOP, 193, 218, 100, 15, ID_ACCEPT, 0x80, L"OK (Apply this mod)");   // text changes with the decline box
    p = dlg_item(p, SS_RIGHT, 7, 238, 286, 10, ID_NOTE, 0x82, L"This window will not appear again unless the license changes.");
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
    int r = license_dialog();
    if (r == 1) {
        wchar_t path[MAX_PATH];
        HANDLE f = create_in_dir(L"W3CrashFix.accepted", path);
        if (f != INVALID_HANDLE_VALUE) { put(f, W3CF_LICENSE_ID); CloseHandle(f); logf("license: accepted (W3CrashFix.accepted written; the dialog will not be shown again unless the license changes)"); }
        else logf("license: accepted, but W3CrashFix.accepted could not be written (error %lu) - the dialog will return", GetLastError());
        g_accepted = 1;
        if (g_fixInstalled) {
            g_fixActive = 1;
            logf("fix: descriptor page recycling fix ACTIVE (w3+0x%X detoured)", RVA_ENSURE);
        }
        start_work();
    } else if (r == 3) {
        wchar_t to[MAX_PATH]; lstrcpyW(to, g_dir); lstrcatW(to, L"W3CrashFix.removed");
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

// Everything the mod does besides the fix itself: crash report, heap statistics. Runs only after the license is accepted.
static void start_work(void) {
    guard_filter(1);
    AddVectoredExceptionHandler(1, crash_notice);
#ifdef W3_CRASHTEST
    logf("TEST BUILD - crash keys are enabled: Ctrl+Alt+Shift+F7 = plain crash, Ctrl+Alt+Shift+F8 = descriptor heap crash");
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
        lstrcatW(p, L"W3CrashFix.log");
        lstrcatW(prev, L"W3CrashFix.previous.log");
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
        MoveFileExW(p, prev, MOVEFILE_REPLACE_EXISTING);
        g_log = open_log(p);
        logf("==== W3CrashFix %s (%s) loaded, pid %lu ====", W3CF_VERSION, flavor, GetCurrentProcessId());
        g_flavor = flavor;
#ifdef W3_LICENSE
        // The license: accepted for this version in an earlier session (W3CrashFix.accepted), or asked in a dialog.
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
