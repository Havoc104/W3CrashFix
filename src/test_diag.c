// Test for the unsupported-build diagnostic. Built as a program NAMED witcher3.exe that is not the game:
// it carries four of the five function patterns at "wrong" addresses and only the first 12 bytes of the fifth,
// then loads the mod (path given as the first argument). The mod must not patch anything and must log where it
// found each pattern. Run by build.ps1; the log is checked there.
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <d3d12.h>

#define CODE __attribute__((section(".text"), used))
CODE static const unsigned char pad0[64] = {0xcc};
CODE static const unsigned char f_ensure[]     = {0x40,0x53,0x48,0x83,0xec,0x20,0x03,0x11,0x48,0x8b,0xd9,0x3b,0x91,0x30,0x01,0x00,0x00,0x0f,0x8e,0x8c,0x00,0x00,0x00,0x48, 0xc3};
CODE static const unsigned char f_forcenew[]   = {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0xb9,0xe8,0x00,0x00,0x00,0x48,0x8b,0xd9,0x48,0x8b,0x47,0x10, 0xc3};
CODE static const unsigned char f_invalidate[] = {0x48,0x81,0xc1,0x58,0x01,0x00,0x00,0x41,0xb8,0x04,0x00,0x00,0x00,0x45,0x33,0xc9,0x48,0x8b,0x41,0xf8,0x48,0x85,0xc0,0x74, 0xc3};
CODE static const unsigned char f_createpage[] = {0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x8b,0x81,0x30,0x01,0x00,0x00,0x4c,0x8d,0x4c,0x24,0x40,0x89,0x44,0x24, 0xc3};
// vecgrow: same first 12 bytes, different body -> "starts the same way"
CODE static const unsigned char f_vecgrow[]    = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89, 0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
                                                  0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90, 0xc3};

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) { puts("usage: witcher3.exe <path to W3CrashFix.asi or dinput8.dll>"); return 2; }
    volatile const unsigned char *keep[] = {pad0, f_ensure, f_forcenew, f_invalidate, f_createpage, f_vecgrow}; (void)keep;
    HMODULE m = LoadLibraryW(argv[1]);
    if (!m) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    Sleep(300);
    puts("loaded");
    // Optional second part (argument "heap"): create a D3D12 device and one shader-visible heap after the mod has
    // hooked the device, so the log gets a "create ... caller=witcher3.exe+0x..." line naming this program.
    if (argc > 2 && !wcscmp(argv[2], L"heap")) {
        ID3D12Device *dev = NULL;
        HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
        if (FAILED(hr)) { printf("no D3D12 device 0x%08lX\n", (unsigned long)hr); return 3; }
        Sleep(5000);                                           // the mod waits 2 s after D3D12Core.dll appears
        D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
        ID3D12DescriptorHeap *heap = NULL;
        hr = ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&heap);
        printf("heap 0x%08lX\n", (unsigned long)hr);
        if (heap) ID3D12DescriptorHeap_Release(heap);
    }
    return 0;
}
