// Test harness for the W3CrashFix proxy: loads build\dinput8.dll like the game would,
// checks DirectInput forwarding, then exercises CreateDescriptorHeap / Release / failure.
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>

DEFINE_GUID(IID_IDirectInput8W_, 0xBF798031, 0x483A, 0x4DA2, 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00);
typedef HRESULT (WINAPI *DI8C)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
typedef HRESULT (WINAPI *CD)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);

int main(int argc, char **argv) {
    // optional: load several copies (e.g. build\dinput8.dll build\W3CrashFix.asi) to test the single-instance guard
    HRESULT hr;
    for (int i = 1; i < (argc > 1 ? argc : 2); i++) {
        const char *path = argc > 1 ? argv[i] : "build\\dinput8.dll";
        HMODULE p = LoadLibraryA(path);
        if (!p) { printf("load %s failed %lu\n", path, GetLastError()); return 1; }
        DI8C di8 = (DI8C)GetProcAddress(p, "DirectInput8Create");
        printf("%s loaded, exports DirectInput8Create: %s\n", path, di8 ? "yes" : "no");
        if (di8) {
            IUnknown *di = NULL;
            hr = di8(GetModuleHandleW(NULL), 0x0800, &IID_IDirectInput8W_, (void **)&di, NULL);
            printf("DirectInput8Create via proxy: 0x%08lX %s\n", (unsigned long)hr, di ? "(got interface)" : "");
            if (di) IUnknown_Release(di);
        }
    }

    HMODULE d = LoadLibraryW(L"d3d12.dll");
    ID3D12Device *dev = NULL;
    hr = ((CD)GetProcAddress(d, "D3D12CreateDevice"))(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
    printf("device: 0x%08lX\n", (unsigned long)hr);
    if (!dev) return 1;
    Sleep(4000);   // let the probe hook

    ID3D12DescriptorHeap *h[6] = {0};
    D3D12_DESCRIPTOR_HEAP_DESC desc = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
    for (int i = 0; i < 5; i++) ID3D12Device_CreateDescriptorHeap(dev, &desc, &IID_ID3D12DescriptorHeap, (void **)&h[i]);
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    ID3D12Device_CreateDescriptorHeap(dev, &desc, &IID_ID3D12DescriptorHeap, (void **)&h[5]);
    ID3D12DescriptorHeap_Release(h[0]);
    ID3D12DescriptorHeap_Release(h[1]);
    // forced failure: sampler shader-visible heaps are limited to 2048 descriptors
    desc.NumDescriptors = 100000;
    ID3D12DescriptorHeap *bad = NULL;
    hr = ID3D12Device_CreateDescriptorHeap(dev, &desc, &IID_ID3D12DescriptorHeap, (void **)&bad);
    printf("forced failure returned 0x%08lX\n", (unsigned long)hr);
    Sleep(500);
    return 0;
}
