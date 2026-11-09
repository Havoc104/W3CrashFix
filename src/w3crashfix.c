// W3CrashFix - in-memory crash fixes for The Witcher 3: Wild Hunt next-gen, DX12, witcher3.exe 5.0.16.41566.
// Copyright (c) 2026 Havoc (Onno Struijk) and Claude. MIT License, see LICENSE.
//
// Fix 1 (THE FIX below): descriptor-page leak that ends in a crash at witcher3.exe+0x280ad3d.
// Fix 2 (FIX 2 below):   stale handle ids after fast travel that crash at witcher3.exe+0x1eecf31 / +0x1eece9d.
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
#include "idstub.h"

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
static volatile LONG64 g_rescued, g_retiredNormal;   // fix statistics
static unsigned char *g_idstubs;                      // fix 2 stubs; their counters are the statistics
static void fix2_stats(void);                          // defined after the fix-2 tables

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
    fix2_stats();
}

// ---------------------------------------------------------------- hooks
typedef HRESULT (STDMETHODCALLTYPE *CDH_t)(ID3D12Device *, const D3D12_DESCRIPTOR_HEAP_DESC *, REFIID, void **);
typedef ULONG (STDMETHODCALLTYPE *Rel_t)(ID3D12DescriptorHeap *);
static CDH_t o_CreateDescriptorHeap;
static Rel_t o_HeapRelease;

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
    void *caller = __builtin_return_address(0);
    if (SUCCEEDED(hr) && pp && *pp) {
        AcquireSRWLockExclusive(&g_lk);
        tab_add(*pp, t, sv, (int)d->NumDescriptors);
        g_live[t][sv]++; g_liveDesc[t][sv] += d->NumDescriptors; g_created[t][sv]++;
        long long live = g_live[t][sv], desc = g_liveDesc[t][sv], made = g_created[t][sv];
        ReleaseSRWLockExclusive(&g_lk);
        if (sv) logf("create %s n=%u sv -> live %lld heaps / %lld desc (total created %lld) caller=%p",
                     tname(t), d->NumDescriptors, live, desc, made, caller);
    } else {
        g_failed++;
        HRESULT rr = ID3D12Device_GetDeviceRemovedReason(self);
        logf("!!!! CreateDescriptorHeap FAILED hr=0x%08lX type=%s n=%u flags=%u caller=%p deviceRemovedReason=0x%08lX",
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
static const struct { unsigned rva; const char *hex; } g_sig[] = {
    {RVA_ENSURE,     "40534883ec200311488bd93b91300100000f8e8c00000048"},
    {RVA_FORCENEW,   "48895c2408574883ec20488db9e8000000488bd9488b4710"},
    {RVA_INVALIDATE, "4881c15801000041b8040000004533c9488b41f84885c074"},
    {RVA_CREATEPAGE, "48895c2410574883ec308b81300100004c8d4c2440894424"},
    {RVA_VECGROW,    "48895c241048896c24184889742420574883ec304c8b4118"},
};
static unsigned char *g_base;
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
    if (F32(a, A_USED) + n <= F32(a, A_CAP)) return;
    void *old = FPTR(a, A_CUR);
    if (old) {
        if (F32(old, P_PENDING) == 0 && F32(old, P_FRAMEUSES) == 0) {
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

static void install_fix(void) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!wcsstr(exe, L"witcher3.exe")) { logf("fix: not witcher3.exe - disabled"); return; }
    g_base = (unsigned char *)GetModuleHandleW(NULL);
    for (unsigned i = 0; i < sizeof g_sig / sizeof g_sig[0]; i++)
        if (!hexmatch(g_base + g_sig[i].rva, g_sig[i].hex)) {
            logf("fix: code at w3+0x%X does not match 5.0.16 - game was updated? FIX DISABLED (probe still logs)", g_sig[i].rva);
            return;
        }
    unsigned char jmp[12] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};   // mov rax, imm64 ; jmp rax
    void *target = (void *)fixed_ensure;
    memcpy(jmp + 2, &target, 8);
    DWORD old;
    if (VirtualProtect(g_base + RVA_ENSURE, sizeof jmp, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy(g_base + RVA_ENSURE, jmp, sizeof jmp);
        VirtualProtect(g_base + RVA_ENSURE, sizeof jmp, old, &old);
        FlushInstructionCache(GetCurrentProcess(), g_base + RVA_ENSURE, sizeof jmp);
        logf("fix: descriptor page recycling fix ACTIVE (w3+0x%X detoured)", RVA_ENSURE);
    } else logf("fix: VirtualProtect failed %lu - FIX DISABLED", GetLastError());
}

// ---------------------------------------------------------------- FIX 2: invalid ids in a 1-based handle table
// A game object (vtable w3+0x3828918) keeps a fixed table of 0x1c0-byte entries at +0x21B8C0, addressed by a
// 1-based id: entry = obj + (id - 1 + 0x1345) * 0x1c0. Its methods only reject id == -1. After fast travel a
// caller (w3+0x1d27043) passes stale ids (seen: 0 and 0x40666AEE) and the lookup reads far outside the table
// (crashes at w3+0x1eecf31, w3+0x1eece9d). Fix: route the 9 id-taking methods through a stub that maps any id
// outside 1..ID_MAX to -1, so each method takes its own "no such object" early return. The table must end before
// the field at +0x663391C, which bounds it to 0x6418000 bytes = 234276 entries.
enum { RVA_ID_VTBL = 0x3828918, ID_MAX = 234276 };
// reg = where the 1-based table id arrives. Slots 17 and 18 take it in r8d; their edx is a separate
// 0-based index (edx * 0x280), so id 0 in edx is valid there and must not be touched.
static const struct { int slot; unsigned rva; int reg; const char *hex; } g_idm[] = {
    {4,  0x1eed2e0, ID_REG_EDX, "4c8944241855565741544155415641574883ec70488d6c24"},
    {7,  0x1eecf70, ID_REG_EDX, "83faff0f849f010000574883ec30418bf94c8bd14183f8ff"},
    {9,  0x1eecf10, ID_REG_EDX, "458bd0458bd983faff74484183faff7442ffca4881c24513"},
    {10, 0x1eece70, ID_REG_EDX, "458bd98bc2458bd083faff747e4183faff74788b54243085"},
    {11, 0x1eecde0, ID_REG_EDX, "4883ec28418bc183faff746a4183f8ff746485c07460448b"},
    {12, 0x1eecd30, ID_REG_EDX, "4883ec38418bc183faff0f848c0000004183f8ff0f848200"},
    {13, 0x1eecbf0, ID_REG_EDX, "83faff0f842501000048895c2420574883ec20418bd9488b"},
    {17, 0x1eecb40, ID_REG_R8D, "4183f8ff0f8498000000574883ec20498bf94c8bc983faff"},
    {18, 0x1eeca80, ID_REG_R8D, "4183f8ff0f84ac000000534881ecd0000000488bd983faff"},
};
#define NIDM (sizeof g_idm / sizeof g_idm[0])

static void fix2_stats(void) {
    if (!g_idstubs) return;
    char buf[700]; int n = 0;
    long long z = 0, o = 0; unsigned mx = 0;
    for (unsigned i = 0; i < NIDM; i++) {
        unsigned char *t = g_idstubs + i * ID_STUB_SIZE;
        id_stub_note_max(t);
        long long zi = *(volatile LONG64 *)(t + ID_ZERO), oi = *(volatile LONG64 *)(t + ID_OTHER);
        unsigned mi = *(unsigned *)(t + ID_MAXSEEN);
        z += zi; o += oi; if (mi > mx) mx = mi;
        if (zi || oi) n += snprintf(buf + n, sizeof buf - n, " s%d:%lld/%lld", g_idm[i].slot, zi, oi);
    }
    if (n == 0) snprintf(buf, sizeof buf, " none");
    logf("fix2: blocked id==0 %lld, out-of-range %lld (largest 0x%X) | per slot zero/out:%s", z, o, mx, buf);
}

static void install_fix2(void) {
    for (unsigned i = 0; i < NIDM; i++) {
        void **slot = (void **)(g_base + RVA_ID_VTBL) + g_idm[i].slot;
        if (*slot != g_base + g_idm[i].rva || !hexmatch(g_base + g_idm[i].rva, g_idm[i].hex)) {
            logf("fix2: vtable slot %d / code does not match 5.0.16 - FIX 2 DISABLED", g_idm[i].slot);
            return;
        }
    }
    unsigned char *mem = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) { logf("fix2: VirtualAlloc failed - FIX 2 DISABLED"); return; }
    for (unsigned i = 0; i < NIDM; i++) {
        unsigned char *t = mem + i * ID_STUB_SIZE;
        build_id_stub(t, g_base + g_idm[i].rva, ID_MAX, g_idm[i].reg);
        void **slot = (void **)(g_base + RVA_ID_VTBL) + g_idm[i].slot;
        void *dummy;
        if (!patch(slot, t, &dummy)) { logf("fix2: VirtualProtect failed on slot %d", g_idm[i].slot); return; }
    }
    FlushInstructionCache(GetCurrentProcess(), mem, 0x1000);
    g_idstubs = mem;
    logf("fix2: invalid-id guard ACTIVE (vtable w3+0x%X, %u methods, ids 1..%u)", RVA_ID_VTBL, (unsigned)NIDM, (unsigned)ID_MAX);
}

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

    for (;;) { Sleep(30000); summary("30s"); }
    return 0;
}

// Exported by the .asi build (see w3crashfix_asi.def) so that only this one symbol is public.
// <mod version>-<game version it is built for>; the game part changes whenever the fix is ported to a new patch
#define W3CF_VERSION "1.0.0-5.0.16"
const char *W3CrashFixVersion(void) { return "W3CrashFix " W3CF_VERSION " for witcher3.exe 5.0.16.41566 (DX12)"; }

static HANDLE open_log(const wchar_t *path) {
    return CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        g_t0 = GetTickCount();
        wchar_t p[MAX_PATH], prev[MAX_PATH];
        GetModuleFileNameW(h, p, MAX_PATH);
        wchar_t *s = wcsrchr(p, L'\\'); if (s) s[1] = 0;
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
        install_fix();   // before any game code runs: no thread can be inside the patched function
        if (g_base) install_fix2();
        HANDLE t = CreateThread(NULL, 0, init, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
