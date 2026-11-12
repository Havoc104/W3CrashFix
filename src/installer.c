// W3CrashFix installer for The Witcher 3: Wild Hunt Remastered (DX12).
// Installs / removes either version of W3CrashFix; both are embedded as resources:
//   standalone  -> bin\x64_dx12\dinput8.dll
//   ASI plugin  -> bin\x64_dx12\W3CrashFix.asi (needs an ASI loader; the installer only checks for one and
//                  links to Ultimate ASI Loader - the loader is a third-party project and is not included)
// Copyright (c) 2026 Onno Struijk (Havoc104) and Claude. MIT License, see LICENSE.
//
// Command line (for scripts and tests):
//   W3CrashFix-Installer.exe --install-standalone | --install-asi | --uninstall  "<game folder>"
//   exit code = 0 or a Win32 error code
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define W3CF_VERSION   L"1.2.0-5.0.16"
#define GAME_NAME      L"The Witcher 3: Wild Hunt Remastered"
#define GAME_VER       L"5.0.16.41566"
#define UAL_URL        L"https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest"
#define KOFI_URL       L"https://ko-fi.com/havoc104/tip"
#define IDR_STANDALONE 101
#define IDR_ASI        102

enum { ID_PATH = 10, ID_BROWSE, ID_STATUS, ID_INSTALL, ID_UNINSTALL, ID_CLOSE, ID_STANDALONE, ID_ASI, ID_GETUAL, ID_KOFI };
static HWND g_wnd, g_path, g_status, g_install, g_uninstall, g_rStandalone, g_rAsi, g_getUal;
static HFONT g_font, g_bold;
static int g_dpi = 96, g_userPicked;   // g_userPicked: user chose a version, stop auto-selecting

// ---------------------------------------------------------------- helpers
static int S(int v) { return MulDiv(v, g_dpi, 96); }
static int exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

// game folder (contains bin\x64_dx12\witcher3.exe) from whatever the user picked: game root, bin, x64_dx12 or the exe
static int normalize(const wchar_t *in, wchar_t *root) {
    wchar_t p[MAX_PATH], t[MAX_PATH];
    while (*in == L' ' || *in == L'"') in++;
    lstrcpynW(p, in, MAX_PATH);
    for (size_t n = wcslen(p); n && (p[n - 1] == L'\\' || p[n - 1] == L'/' || p[n - 1] == L' ' || p[n - 1] == L'"'); n--) p[n - 1] = 0;
    for (int up = 0; up < 4 && p[0]; up++) {   // exe -> x64_dx12 -> bin -> game folder
        swprintf(t, MAX_PATH, L"%ls\\bin\\x64_dx12\\witcher3.exe", p);
        if (exists(t)) { lstrcpyW(root, p); return 1; }
        wchar_t *s = wcsrchr(p, L'\\');
        if (!s) break;
        *s = 0;
    }
    return 0;
}

static void bin_file(const wchar_t *root, const wchar_t *name, wchar_t *out) {
    swprintf(out, MAX_PATH, L"%ls\\bin\\x64_dx12\\%ls", root, name);
}

// "...\\\\..." JSON/VDF string at a -> plain wide path
static void unescape_path(const char *a, wchar_t *out) {
    char tmp[MAX_PATH * 2]; int j = 0;
    for (const char *s = a; *s && j < (int)sizeof tmp - 1; s++) { if (s[0] == '\\' && s[1] == '\\') s++; tmp[j++] = *s; }
    tmp[j] = 0;
    MultiByteToWideChar(CP_UTF8, 0, tmp, -1, out, MAX_PATH);
}

// ---------------------------------------------------------------- finding the game
static int find_steam(wchar_t *root) {
    wchar_t steam[MAX_PATH], cand[MAX_PATH]; DWORD sz = sizeof steam;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, NULL, steam, &sz)) return 0;
    for (wchar_t *c = steam; *c; c++) if (*c == L'/') *c = L'\\';
    swprintf(cand, MAX_PATH, L"%ls\\steamapps\\common\\The Witcher 3", steam);
    if (normalize(cand, root)) return 1;
    wchar_t vdf[MAX_PATH]; swprintf(vdf, MAX_PATH, L"%ls\\steamapps\\libraryfolders.vdf", steam);
    FILE *f = _wfopen(vdf, L"rb");
    if (!f) return 0;
    char line[1024];
    while (fgets(line, sizeof line, f)) {                    // lines like:  "path"  "D:\\SteamLibrary"
        char *k = strstr(line, "\"path\""); if (!k) continue;
        char *a = strchr(k + 6, '"'); if (!a) continue;
        char *b = strchr(a + 1, '"'); if (!b) continue;
        *b = 0;
        wchar_t lib[MAX_PATH]; unescape_path(a + 1, lib);
        swprintf(cand, MAX_PATH, L"%ls\\steamapps\\common\\The Witcher 3", lib);
        if (normalize(cand, root)) { fclose(f); return 1; }
    }
    fclose(f);
    return 0;
}

static int find_gog(wchar_t *root) {
    static const wchar_t *ids[] = { L"1495134320", L"1207664663", L"1207664643" };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        wchar_t key[128], p[MAX_PATH]; DWORD sz = sizeof p;
        swprintf(key, 128, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games\\%ls", ids[i]);
        if (!RegGetValueW(HKEY_LOCAL_MACHINE, key, L"path", RRF_RT_REG_SZ, NULL, p, &sz) && normalize(p, root)) return 1;
    }
    return 0;
}

static int find_epic(wchar_t *root) {
    const wchar_t *dir = L"C:\\ProgramData\\Epic\\EpicGamesLauncher\\Data\\Manifests";
    wchar_t pat[MAX_PATH]; swprintf(pat, MAX_PATH, L"%ls\\*.item", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int found = 0;
    static char buf[65536];
    do {
        wchar_t fp[MAX_PATH]; swprintf(fp, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        FILE *f = _wfopen(fp, L"rb"); if (!f) continue;
        size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f);
        if (!strstr(buf, "Witcher 3")) continue;
        char *k = strstr(buf, "\"InstallLocation\""); if (!k) continue;
        char *a = strchr(k + 17, '"'); if (!a) continue;
        char *b = strchr(a + 1, '"'); if (!b) continue;
        *b = 0;
        wchar_t loc[MAX_PATH]; unescape_path(a + 1, loc);
        if (normalize(loc, root)) { found = 1; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

static int find_game(wchar_t *root) { return find_steam(root) || find_gog(root) || find_epic(root); }

// ---------------------------------------------------------------- state of a game folder
static int file_contains(const wchar_t *path, const char *a, const char *b) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD size = GetFileSize(f, NULL), got = 0;
    char *buf = (size && size < (64u << 20)) ? HeapAlloc(GetProcessHeap(), 0, size) : NULL;
    int hit = 0;
    if (buf && ReadFile(f, buf, size, &got, NULL)) {
        size_t la = strlen(a), lb = b ? strlen(b) : 0;
        for (DWORD i = 0; i < got && !hit; i++)
            hit = (i + la <= got && !memcmp(buf + i, a, la)) || (b && i + lb <= got && !memcmp(buf + i, b, lb));
    }
    if (buf) HeapFree(GetProcessHeap(), 0, buf);
    CloseHandle(f);
    return hit;
}
static int ours(const wchar_t *path) { return file_contains(path, "W3CrashFix", "W3HeapProbe"); }

// 0 = no dinput8.dll, 1 = our standalone, 2 = someone else's (an ASI loader or another mod)
static int dinput_state(const wchar_t *root) {
    wchar_t d[MAX_PATH]; bin_file(root, L"dinput8.dll", d);
    if (!exists(d)) return 0;
    return ours(d) ? 1 : 2;
}
static int asi_installed(const wchar_t *root) { wchar_t d[MAX_PATH]; bin_file(root, L"W3CrashFix.asi", d); return exists(d); }

// Ultimate ASI Loader can be installed under many dll names; out = the name found
static int find_asi_loader(const wchar_t *root, wchar_t *out) {
    static const wchar_t *names[] = { L"dinput8.dll", L"winmm.dll", L"version.dll", L"dsound.dll", L"winhttp.dll",
        L"wininet.dll", L"dinput.dll", L"xinput1_3.dll", L"xinput1_4.dll", L"xinput9_1_0.dll", L"d3d9.dll",
        L"d3d10.dll", L"d3d11.dll", L"d3d12.dll", L"dxgi.dll", L"ddraw.dll", L"msacm32.dll", L"msvfw32.dll", L"wintrust.dll" };
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
        wchar_t p[MAX_PATH]; bin_file(root, names[i], p);
        if (exists(p) && file_contains(p, "Ultimate ASI Loader", "Ultimate-ASI-Loader")) { lstrcpyW(out, names[i]); return 1; }
    }
    return 0;
}

static int game_version(const wchar_t *root, wchar_t *out, size_t n) {   // 1 = exactly the supported version
    wchar_t exe[MAX_PATH]; bin_file(root, L"witcher3.exe", exe);
    DWORD h, sz = GetFileVersionInfoSizeW(exe, &h);
    lstrcpynW(out, L"unknown", (int)n);
    if (!sz) return 0;
    void *buf = HeapAlloc(GetProcessHeap(), 0, sz);
    VS_FIXEDFILEINFO *fi; UINT len; int ok = 0;
    if (buf && GetFileVersionInfoW(exe, 0, sz, buf) && VerQueryValueW(buf, L"\\", (void **)&fi, &len)) {
        swprintf(out, n, L"%u.%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
                 HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
        ok = !wcscmp(out, GAME_VER);
    }
    if (buf) HeapFree(GetProcessHeap(), 0, buf);
    return ok;
}

// A running exe is locked by Windows: opening it with write access then fails with a sharing violation.
// (No process enumeration needed.)
static int game_running(const wchar_t *root) {
    wchar_t exe[MAX_PATH]; bin_file(root, L"witcher3.exe", exe);
    HANDLE f = CreateFileW(exe, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { CloseHandle(f); return 0; }
    return GetLastError() == ERROR_SHARING_VIOLATION;
}

// ---------------------------------------------------------------- install / uninstall
// All return 0 or a Win32 error (ERROR_ACCESS_DENIED -> the UI retries elevated).
static DWORD write_resource(int id, const wchar_t *root, const wchar_t *name) {
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    const void *data = g ? LockResource(g) : NULL;
    DWORD size = r ? SizeofResource(NULL, r) : 0;
    if (!data || !size) return ERROR_RESOURCE_DATA_NOT_FOUND;
    wchar_t dst[MAX_PATH], tmp[MAX_PATH], tmpName[64];
    swprintf(tmpName, 64, L"%ls.w3cf-new", name);
    bin_file(root, name, dst);
    bin_file(root, tmpName, tmp);
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD w = 0; BOOL ok = WriteFile(f, data, size, &w, NULL) && w == size;
    DWORD err = ok ? 0 : GetLastError();
    CloseHandle(f);
    if (!ok) { DeleteFileW(tmp); return err ? err : ERROR_WRITE_FAULT; }
    if (!MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING)) { err = GetLastError(); DeleteFileW(tmp); return err; }
    return 0;
}
static DWORD remove_ours(const wchar_t *root, const wchar_t *name) {
    wchar_t p[MAX_PATH]; bin_file(root, name, p);
    if (!exists(p) || !ours(p)) return 0;   // never delete someone else's file
    return DeleteFileW(p) ? 0 : GetLastError();
}
static DWORD install_standalone(const wchar_t *root) {
    if (dinput_state(root) == 2) return ERROR_ALREADY_EXISTS;   // someone else's dinput8.dll: don't overwrite
    DWORD e = write_resource(IDR_STANDALONE, root, L"dinput8.dll");
    return e ? e : remove_ours(root, L"W3CrashFix.asi");        // one version at a time
}
static DWORD install_asi(const wchar_t *root) {
    DWORD e = write_resource(IDR_ASI, root, L"W3CrashFix.asi");
    return e ? e : remove_ours(root, L"dinput8.dll");           // our dinput8.dll would block the loader's name
}
static DWORD uninstall_all(const wchar_t *root) {
    DWORD a = remove_ours(root, L"dinput8.dll"), b = remove_ours(root, L"W3CrashFix.asi");
    return a ? a : b;
}

// ---------------------------------------------------------------- UI
static void msg(const wchar_t *text, UINT icon) { MessageBoxW(g_wnd, text, L"W3CrashFix " W3CF_VERSION, MB_OK | icon); }
static int want_asi(void) { return SendMessageW(g_rAsi, BM_GETCHECK, 0, 0) == BST_CHECKED; }
static void set_choice(int asi) {
    SendMessageW(g_rAsi, BM_SETCHECK, asi ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_rStandalone, BM_SETCHECK, asi ? BST_UNCHECKED : BST_CHECKED, 0);
}
static int current_root(wchar_t *root) {
    wchar_t p[MAX_PATH]; GetWindowTextW(g_path, p, MAX_PATH);
    return normalize(p, root);
}

static void refresh(void) {
    wchar_t root[MAX_PATH], ver[64], loader[64] = L"", s[1400];
    int canInstall = 0, canUninstall = 0, showUal = 0;
    if (!current_root(root)) {
        swprintf(s, 1400, L"%ls was not found in this folder.\r\nClick Browse and select the game folder "
                          L"(the one that contains the \"bin\" folder).", GAME_NAME);
        EnableWindow(g_rStandalone, FALSE); EnableWindow(g_rAsi, FALSE);
    } else {
        EnableWindow(g_rStandalone, TRUE); EnableWindow(g_rAsi, TRUE);
        int vok = game_version(root, ver, 64), di = dinput_state(root), asi = asi_installed(root);
        int hasLoader = find_asi_loader(root, loader);
        if (!g_userPicked) set_choice(hasLoader || asi);   // sensible default
        canUninstall = di == 1 || asi;

        wchar_t a[300], b[300], c[600];
        if (vok) swprintf(a, 300, L"Game version: %ls (supported).", ver);
        else swprintf(a, 300, L"Game version: %ls. This release is made for %ls; on other versions the fix stays "
                              L"inactive. Check for a newer W3CrashFix release.", ver, GAME_VER);
        if (di == 1) swprintf(b, 300, L"Installed now: W3CrashFix standalone (dinput8.dll).");
        else if (asi) swprintf(b, 300, L"Installed now: W3CrashFix ASI plugin (W3CrashFix.asi).");
        else swprintf(b, 300, L"Installed now: nothing.");

        if (!want_asi()) {
            if (di == 2 && hasLoader)
                swprintf(c, 600, L"An ASI loader (Ultimate ASI Loader) is installed as dinput8.dll, so the standalone "
                                 L"version can't be used. Choose the ASI plugin instead.");
            else if (di == 2)
                swprintf(c, 600, L"Another mod's dinput8.dll is in bin\\x64_dx12, so the standalone version can't be "
                                 L"installed (it would overwrite that file). Choose the ASI plugin instead.");
            else { swprintf(c, 600, L"Will install: bin\\x64_dx12\\dinput8.dll."); canInstall = 1; }
        } else if (hasLoader) {
            swprintf(c, 600, L"ASI loader found (Ultimate ASI Loader as %ls).\r\nWill install: bin\\x64_dx12\\W3CrashFix.asi.", loader);
            canInstall = 1;
        } else {
            swprintf(c, 600, L"No ASI loader found. The ASI plugin needs one: download Ultimate ASI Loader (x64), "
                             L"put its dinput8.dll in bin\\x64_dx12, then come back here. Or choose the standalone version.");
            showUal = 1;
        }
        swprintf(s, 1400, L"%ls\r\n%ls\r\n\r\n%ls", a, b, c);
    }
    SetWindowTextW(g_status, s);
    EnableWindow(g_install, canInstall);
    EnableWindow(g_uninstall, canUninstall);
    ShowWindow(g_getUal, showUal ? SW_SHOW : SW_HIDE);
}

static void action(int install) {
    wchar_t root[MAX_PATH], ver[64];
    if (!current_root(root)) return;
    if (game_running(root)) { msg(L"Please close the game first.", MB_ICONWARNING); return; }
    int asi = want_asi();
    if (install && !game_version(root, ver, 64)) {
        wchar_t q[512];
        swprintf(q, 512, L"Your game version is %ls, but this release is made for %ls.\n\n"
                         L"The fix checks the game code and stays inactive (harmless) on other versions.\nInstall anyway?", ver, GAME_VER);
        if (MessageBoxW(g_wnd, q, L"W3CrashFix " W3CF_VERSION, MB_YESNO | MB_ICONWARNING) != IDYES) return;
    }
    DWORD e = !install ? uninstall_all(root) : asi ? install_asi(root) : install_standalone(root);
    if (e == ERROR_ACCESS_DENIED) {
        msg(L"Windows doesn't allow this program to write to the game folder.\n\n"
            L"Close the installer, right-click W3CrashFix-Installer.exe, choose \"Run as administrator\" and try again.",
            MB_ICONWARNING);
        refresh();
        return;
    }
    if (e) {
        wchar_t m[512], sys[256] = L"";
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, sys, 256, NULL);
        swprintf(m, 512, L"%ls failed (error %lu): %ls", install ? L"Installation" : L"Uninstall", e, sys);
        msg(m, MB_ICONERROR);
    } else if (install) {
        msg(L"W3CrashFix is installed.\n\nStart the game in DirectX 12 mode and load a save. "
            L"bin\\x64_dx12\\W3CrashFix.log should then contain a line with \"ACTIVE\".\n\n"
            L"W3CrashFix is free, but making and maintaining it costs time and money. Consider buying me a coffee "
            L"by clicking the button on the bottom left of the installer. For every cup of coffee I receive, "
            L"my cat gets a treat and some extra pets! \U0001F638", MB_ICONINFORMATION);
    } else msg(L"W3CrashFix has been removed.", MB_ICONINFORMATION);
    refresh();
}

static void browse(void) {
    BROWSEINFOW bi = { g_wnd, NULL, NULL, L"Select the folder of " GAME_NAME L" (it contains the \"bin\" folder):",
                       BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE };
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&bi);
    if (!id) return;
    wchar_t p[MAX_PATH], root[MAX_PATH];
    if (SHGetPathFromIDListW(id, p)) SetWindowTextW(g_path, normalize(p, root) ? root : p);
    CoTaskMemFree(id);
    refresh();
}

static HWND ctl(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(!wcscmp(cls, L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             S(x), S(y), S(w), S(h), g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_wnd = h;
        HWND t = ctl(L"STATIC", L"W3CrashFix " W3CF_VERSION, 0, 16, 14, 528, 24, 0);
        SendMessageW(t, WM_SETFONT, (WPARAM)g_bold, TRUE);
        ctl(L"STATIC", L"Crash fix for " GAME_NAME L" (DirectX 12).", 0, 16, 40, 528, 20, 0);
        ctl(L"STATIC", L"Game folder:", 0, 16, 70, 528, 18, 0);
        g_path = ctl(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 16, 90, 432, 24, ID_PATH);
        ctl(L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 456, 89, 88, 26, ID_BROWSE);
        ctl(L"STATIC", L"Version:", 0, 16, 128, 70, 20, 0);
        g_rStandalone = ctl(L"BUTTON", L"Standalone (dinput8.dll)", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 90, 126, 200, 22, ID_STANDALONE);
        g_rAsi = ctl(L"BUTTON", L"ASI plugin (needs an ASI loader)", BS_AUTORADIOBUTTON, 300, 126, 244, 22, ID_ASI);
        g_status = ctl(L"STATIC", L"", 0, 16, 160, 528, 112, ID_STATUS);
        g_getUal = ctl(L"BUTTON", L"Get Ultimate ASI Loader...", BS_PUSHBUTTON | WS_TABSTOP, 16, 278, 190, 28, ID_GETUAL);
        ctl(L"BUTTON", L"Buy me a coffee", BS_PUSHBUTTON | WS_TABSTOP, 16, 322, 150, 28, ID_KOFI);
        g_install = ctl(L"BUTTON", L"Install", BS_DEFPUSHBUTTON | WS_TABSTOP, 264, 322, 88, 28, ID_INSTALL);
        g_uninstall = ctl(L"BUTTON", L"Uninstall", BS_PUSHBUTTON | WS_TABSTOP, 360, 322, 88, 28, ID_UNINSTALL);
        ctl(L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, 456, 322, 88, 28, ID_CLOSE);
        wchar_t root[MAX_PATH];
        if (find_game(root)) SetWindowTextW(g_path, root);
        refresh();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_PATH: if (HIWORD(w) == EN_CHANGE) { g_userPicked = 0; refresh(); } return 0;
        case ID_BROWSE: browse(); return 0;
        case ID_STANDALONE: case ID_ASI: if (HIWORD(w) == BN_CLICKED) { g_userPicked = 1; refresh(); } return 0;
        case ID_GETUAL: ShellExecuteW(h, L"open", UAL_URL, NULL, NULL, SW_SHOWNORMAL); return 0;
        case ID_KOFI: ShellExecuteW(h, L"open", KOFI_URL, NULL, NULL, SW_SHOWNORMAL); return 0;
        case ID_INSTALL: action(1); return 0;
        case ID_UNINSTALL: action(0); return 0;
        case ID_CLOSE: case IDCANCEL: DestroyWindow(h); return 0;
        }
        break;
    case WM_ACTIVATE:   // re-check when the user comes back (e.g. after installing the ASI loader)
        if (LOWORD(w) != WA_INACTIVE && g_status) refresh();
        break;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev; (void)cmd;
    int argc; wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 3 && !wcsncmp(argv[1], L"--", 2)) {   // scripted / elevated mode
        wchar_t root[MAX_PATH];
        if (!normalize(argv[2], root)) return ERROR_PATH_NOT_FOUND;
        if (!wcscmp(argv[1], L"--install-standalone")) return (int)install_standalone(root);
        if (!wcscmp(argv[1], L"--install-asi")) return (int)install_asi(root);
        if (!wcscmp(argv[1], L"--uninstall")) return (int)uninstall_all(root);
        return ERROR_INVALID_PARAMETER;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    HDC dc = GetDC(NULL); g_dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc);
    NONCLIENTMETRICSW nm = { sizeof nm };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof nm, &nm, 0);
    g_font = CreateFontIndirectW(&nm.lfMessageFont);
    LOGFONTW b = nm.lfMessageFont; b.lfWeight = FW_BOLD; b.lfHeight = b.lfHeight * 4 / 3;
    g_bold = CreateFontIndirectW(&b);

    WNDCLASSW wc = { 0, wndproc, 0, 0, inst, LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION), LoadCursorW(NULL, (LPCWSTR)IDC_ARROW),
                     GetSysColorBrush(COLOR_WINDOW), NULL, L"W3CrashFixInstaller" };
    RegisterClassW(&wc);
    RECT r = { 0, 0, S(560), S(366) };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    HWND h = CreateWindowW(L"W3CrashFixInstaller", L"W3CrashFix " W3CF_VERSION L" installer", style,
                           CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    ShowWindow(h, show);
    MSG mv;
    while (GetMessageW(&mv, NULL, 0, 0) > 0)
        if (!IsDialogMessageW(h, &mv)) { TranslateMessage(&mv); DispatchMessageW(&mv); }
    return 0;
}
