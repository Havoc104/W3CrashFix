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
// sent by the mod itself. OpenReportPage=0 in W3CrashFix.ini stops the page from opening.
#define RVA_CRASH 0x280ad3d
// <mod version>-<game version it is built for>; the game part changes whenever the fix is ported to a new patch
#define W3CF_VERSION "1.6.1-5.0.16"
static const char *g_flavor = "";
static char g_adapterName[128] = "unknown";
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter;
static volatile LONG g_crashed, g_noticed;
static char g_crSum[3072], g_crEsc[6144], g_crBuf[65536];

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

// ---------------------------------------------------------------- settings: W3CrashFix.ini
// One readable file next to the mod instead of several small ones:
//   [W3CrashFix]
//   LicenseAccepted=MIT-2      the license text the user accepted (W3CF_LICENSE_ID); written by the mod or the installer
//   ReportId=0123456789ABCDEF  the random report id (see below)
//   OpenReportPage=1           0 = after a crash only the crash log is saved; no page and no folder window open
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
static char g_modNames[600] = "none";
static int g_modsFound;

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
        size_t len = g_modsFound ? strlen(g_modNames) : 0;
        if (len + strlen(n8) + 12 < sizeof g_modNames) snprintf(g_modNames + len, sizeof g_modNames - len, "%s%s", len ? ", " : "", n8);
        else if (len + 6 < sizeof g_modNames && strcmp(g_modNames + len - 5, ", ...")) lstrcatA(g_modNames, ", ...");
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
    if (!g_modsFound) logf("mods: no in-game mods found");
    else logf("mods: in-game mods found: %s (a crash report would name them)", g_modNames);
}

// The report id: a random number made on this PC the first time the mod works, kept as ReportId in W3CrashFix.ini
// and shown in the crash summary. Its only use: several crash reports from the same installation can be recognised
// as such. It is not derived from the hardware, Windows or the user's name; removing the line gives a new one, and
// Uninstall and a declined license remove the whole file.
static char g_reportId[20] = "none";
static void load_report_id(void) {
    char buf[64]; ini_get(L"ReportId", buf, sizeof buf);
    int ok = strlen(buf) == 16;
    for (int i = 0; ok && i < 16; i++) ok = (buf[i] >= '0' && buf[i] <= '9') || (buf[i] >= 'A' && buf[i] <= 'F');
    if (ok) { memcpy(g_reportId, buf, 17); logf("report id: %s (random, from W3CrashFix.ini)", g_reportId); return; }
    LARGE_INTEGER q; FILETIME ft; QueryPerformanceCounter(&q); GetSystemTimeAsFileTime(&ft);
    unsigned long long x = (unsigned long long)q.QuadPart ^ (((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime);
    x ^= ((unsigned long long)GetCurrentProcessId() << 32) ^ ((unsigned long long)GetCurrentThreadId() << 13) ^ (unsigned long long)(ULONG_PTR)&x;
    for (int i = 0; i < 2; i++) {                            // mix the bits (splitmix64)
        x += 0x9E3779B97F4A7C15ULL; x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL; x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL; x ^= x >> 31;
    }
    snprintf(g_reportId, sizeof g_reportId, "%016llX", x);
    if (ini_set(L"ReportId", g_reportId)) logf("report id: %s (random, newly made and saved in W3CrashFix.ini; remove the ReportId line there to get a new one)", g_reportId);
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
            if (cx > ww) cx = ww;
            if (cy > wh) cy = wh;
            if (IsZoomed(fw) || IsIconic(fw)) ShowWindowAsync(fw, SW_RESTORE);
            // HWND_TOPMOST = "always on top"; asynchronous, so the mod never waits for Explorer (rule 2 above)
            SetWindowPos(fw, HWND_TOPMOST, mi.rcWork.right - cx - 16, mi.rcWork.bottom - cy - 16, cx, cy, SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
            Sleep(500);                                  // let Explorer finish bringing it forward, then look
            // GetWindowLongW only reads the window's style; it does not send a message, so it cannot wait either
            int onTop = IsWindow(fw) && (GetWindowLongW(fw, GWL_EXSTYLE) & WS_EX_TOPMOST);
            logf("crash report: opened the folder \"" W3CF_REPORT_DIR "\" with W3CrashFix.crash.log highlighted (%s found after %lu ms, %d such window(s) were open before; made small, lower right; in front of the browser: %s, checked; always on top: %s, checked)",
                 reused ? "an already open window" : "new window", GetTickCount() - t0, fs.nOld,
                 !bw ? "no browser window to compare with" : (IsWindow(fw) && folder_in_front(fw, bw) ? "YES" : "NO"),
                 onTop ? "YES" : "NO (Windows did not allow it)");
        } else logf("crash report: opened the folder \"" W3CF_REPORT_DIR "\" with W3CrashFix.crash.log highlighted (its window was not found in time, so it keeps Explorer's own size and place)");
    } else logf("crash report: the folder window could not be opened (%lu)", GetLastError());
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
    if (ep->ContextRecord) { fmt_regs(ep->ContextRecord); logf("[CRASH] registers: %s", g_crRegs); }
    else lstrcpyA(g_crRegs, "not available");

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
    // The summary has to be useful on its own: many reports arrive without the log file attached.
    char mem[256]; vram(mem, sizeof mem);
    snprintf(g_crSum, sizeof g_crSum,
             "W3CrashFix crash report\r\n"
             "Mod: %s (%s)\r\n"
             "Report id: %s\r\n"
             "In-game mods: %s\r\n"
             "Game build supported: %s\r\n"
             "Fix active: %s\r\n"
             "Crash: exception 0x%08lX (%s%s) at %s\r\n"
             "Registers: %s\r\n"
             "Session length: %lu min\r\n"
             "Graphics card: %s\r\n"
             "Descriptor heaps: %lld live + %lld sampler (peak %lld + %lld), %lld failed creations\r\n"
             "Fix: %lld pages rescued, %lld retired normally\r\n"
             "Memory: %s\r\n"
             "Verdict: %s\r\n%s",
             "W3CrashFix " W3CF_VERSION, g_flavor, g_reportId, g_modNames, g_buildOk ? "yes" : "no", g_fixActive ? "yes" : "no",
             (unsigned long)code, exc_name(code), detail, at, g_crRegs,
             (unsigned long)((GetTickCount() - g_t0) / 60000), g_adapterName,
             g_live[0][1], g_live[1][1], g_peakSv[0], g_peakSv[1], g_failed,
             (long long)g_rescued, (long long)g_retiredNormal, mem, verdict, test);

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
        put(f, g_crSum); put(f, "\r\n---------------- full log of the crashed session ----------------\r\n");
        lstrcpyW(src, g_dir); lstrcatW(src, L"W3CrashFix.activity.log");
        HANDLE in = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        if (in != INVALID_HANDLE_VALUE) {
            DWORD got, w;
            while (ReadFile(in, g_crBuf, sizeof g_crBuf, &got, NULL) && got) WriteFile(f, g_crBuf, got, &w, NULL);
            CloseHandle(in);
        }
        CloseHandle(f);
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
           "</style></head><body><h1>The Witcher 3 crashed</h1>"
           "<p>W3CrashFix noticed the crash and saved a report on your PC. <b>Nothing has been sent anywhere.</b> "
           "If you would like to help, you can send the crash log to the mod's author with the button below.</p>"
           "<p class=\"s\">This page is a file on your own PC, not a website: look at the address bar, it starts with <code>file:</code>. "
           "The mod itself never connects to the internet. Only your browser does, and only when you click the button.</p><pre id=\"s\">");
    // The page shows only whether the fix was active and the verdict (Onno, 17 Nov 2026: "the less clutter the better");
    // the full summary stays at the top of the crash log, which is what gets sent.
    {
        char page[1024];
        snprintf(page, sizeof page, "Fix active: %s\r\nVerdict: %s\r\n%s", g_fixActive ? "yes" : "no", verdict, test);
        html_esc(page, g_crEsc, sizeof g_crEsc); put(f, g_crEsc);
    }
    put(f, "</pre><p><b>The file to send:</b><br><code>");
    put(f, dirEsc);
    put(f, W3CF_REPORT_DIR "\\W3CrashFix.crash.log</code><br><span class=\"s\">A small folder window has opened in the lower right corner that holds only this file. "
           "You don't need it: the button below also copies the file's path. It stays there until the game crashes again.</span></p>"
           "<p><a class=\"b\" target=\"_blank\" rel=\"noopener\" href=\"" W3CF_UPLOAD_URL "\" data-p=\"");
    put(f, dirEsc);
    put(f, W3CF_REPORT_DIR "\\W3CrashFix.crash.log\" onclick=\"cp(this)\">Send the crash log</a> <span class=\"s\" id=\"c\"></span></p>"
           "<p><b>How it works</b></p><ol>"
           "<li>Click the button. A pCloud page opens, and the crash log's path is copied. If the page shows a cookie notice, answer it first; it can cover the buttons.</li>"
           "<li>On that page, click <b>Click Here to Add Files</b>. In the window that opens, press Ctrl+V and then Enter. "
           "(Or drag the file from the small folder window onto the page.)</li>"
           "<li>Type any name you like. A made-up one is fine.</li>"
           "<li>Click Upload. When you see &quot;Files are uploaded!&quot;, you are done and can close that tab.</li></ol>"
           "<p class=\"s\">pCloud then offers you an account. You don't need one.</p>"
           "<p class=\"s\">Ctrl+V pasted nothing? <button class=\"b g\" data-p=\"");
    put(f, dirEsc);
    put(f, W3CF_REPORT_DIR "\\W3CrashFix.crash.log\" onclick=\"cp(this)\">Copy the file's path</button> and paste it again into the &quot;File name&quot; box.</p>"
           "<p>Thanks for sending your crash log. It helps find and fix crashes in W3CrashFix.</p>"
           "<p class=\"s\">What is sent: only the file you pick, and the name you type on the upload page. The file holds a summary "
           "and the mod's log of the crashed session: technical data, which can include the folder the game is installed in "
           "and the folder names of your in-game mods. "
           "Only the mod's author can see uploaded files. The &quot;Report id&quot; in the file is a random number made on your PC, so that several "
           "reports from the same installation can be recognised. It says nothing about you; remove the <code>ReportId</code> line from "
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
           // the only script on the page: puts the file's path on the clipboard when the Send button or the small button
           // is clicked. The Send link still opens the upload page as usual (nothing is stopped or redirected).
           "<script>function cp(b){var t=b.getAttribute('data-p'),a=document.createElement('textarea');"
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
            logf("fix: descriptor page recycling fix ACTIVE (w3+0x%X detoured)", RVA_ENSURE);
        }
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

// Everything the mod does besides the fix itself: crash report, heap statistics. Runs only after the license is accepted.
static void start_work(void) {
    char opv[8]; ini_get(L"OpenReportPage", opv, sizeof opv);
    if (!opv[0]) ini_set(L"OpenReportPage", "1");        // make the setting visible to anyone who opens the file
    load_report_id();
    scan_mods();
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
