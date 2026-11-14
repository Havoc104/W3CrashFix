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

// License dialog test (arguments "accept" / "decline"): find the mod's dialogs in this process and press their buttons.
// Control ids as in w3crashfix.c: 101 OK button, 102 "I decline" checkbox.
static HWND g_found;
static BOOL CALLBACK find_dialog(HWND w, LPARAM want) {
    DWORD pid = 0; wchar_t cls[16] = L"";
    GetWindowThreadProcessId(w, &pid);
    GetClassNameW(w, cls, 16);
    if (pid != GetCurrentProcessId() || wcscmp(cls, L"#32770") || !IsWindowVisible(w) || !IsWindowEnabled(w)) return TRUE;
    if (!GetDlgItem(w, (int)want)) return TRUE;
    g_found = w;
    return FALSE;
}
static int press(int id, int tick) {                 // waits up to 10 s for an enabled dialog with this button; tick = id of a checkbox to tick first, or 0
    for (int i = 0; i < 100; i++) {
        g_found = NULL;
        EnumWindows(find_dialog, id);
        if (g_found) {
            if (tick) SendMessageW(GetDlgItem(g_found, tick), BM_SETCHECK, BST_CHECKED, 0);
            PostMessageW(g_found, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)GetDlgItem(g_found, id));
            Sleep(300);
            return 1;
        }
        Sleep(100);
    }
    printf("no dialog with button %d\n", id);
    return 0;
}

static volatile LONG g_windowMade;
static DWORD WINAPI make_window(LPVOID unused) {
    (void)unused;
    HWND w = CreateWindowExA(0, "STATIC", "test", 0, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    g_windowMade = 1;
    if (w) DestroyWindow(w);
    return 0;
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) { puts("usage: witcher3.exe <path to W3CrashFix.asi or dinput8.dll>"); return 2; }
    volatile const unsigned char *keep[] = {pad0, f_ensure, f_forcenew, f_invalidate, f_createpage, f_vecgrow}; (void)keep;
    HMODULE m = LoadLibraryW(argv[1]);
    if (!m) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    Sleep(300);
    puts("loaded");
    if (argc > 2 && !wcscmp(argv[2], L"accept")) {           // press OK
        // The "game window": a second thread asks for a window right away. The mod must hold it until OK.
        HANDLE th = CreateThread(NULL, 0, make_window, NULL, 0, NULL);
        Sleep(1500);
        if (g_windowMade) { puts("the window was created while the dialog was still open"); return 5; }
        if (!press(101, 0)) return 4;
        if (WaitForSingleObject(th, 5000) != WAIT_OBJECT_0 || !g_windowMade) { puts("the window was not created after OK"); return 6; }
        Sleep(500);
        return 0;
    }
    if (argc > 2 && !wcscmp(argv[2], L"enter")) {            // the Enter key: ignored at first, OK after 3 seconds
        HWND dlg = NULL;
        for (int i = 0; i < 100 && !dlg; i++) { g_found = NULL; EnumWindows(find_dialog, 101); dlg = g_found; if (!dlg) Sleep(100); }
        if (!dlg) { puts("no dialog"); return 4; }
        PostMessageW(dlg, WM_COMMAND, IDOK, 0);
        Sleep(700);
        if (!IsWindow(dlg)) { puts("Enter closed the dialog right away"); return 7; }
        Sleep(3000);
        PostMessageW(dlg, WM_COMMAND, IDOK, 0);
        for (int i = 0; i < 30 && IsWindow(dlg); i++) Sleep(100);
        if (IsWindow(dlg)) { puts("Enter did not work after 3 seconds"); return 8; }
        Sleep(500);
        return 0;
    }
    if (argc > 2 && !wcscmp(argv[2], L"decline")) {          // tick "I decline", press OK, then "OK" in the final message
        if (!press(101, 102) || !press(IDCANCEL, 0)) return 4;   // the one button of an MB_OK box has id 2
        Sleep(500);
        return 0;
    }
    // Argument "crash": crash on purpose, so the mod's crash report (log line, crash log, report page) can be checked.
    if (argc > 2 && !wcscmp(argv[2], L"crash")) {
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);   // no Windows error dialog for this test
        Sleep(200);
        *(volatile int *)0 = 1;
    }
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
