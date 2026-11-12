// movement.cpp — CS:GO internal movement + ImGui (D3D9) menu, x86
// offsets: frk1/hazedumper csgo.hpp 2023-09-05
// launch csgo with -insecure, on VAC servers this is a ban
#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <d3d9.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

#define USE_MOUSE_XOR 1

namespace off {
    constexpr uintptr_t m_fFlags      = 0x104;
    constexpr uintptr_t m_vecVelocity = 0x114;
    constexpr uintptr_t m_MoveType    = 0x25C;
    constexpr uintptr_t m_lifeState   = 0x25F;
    // client.dll
    constexpr uintptr_t dwLocalPlayer    = 0xDEB99C;
    constexpr uintptr_t dwEntityList     = 0x4E0102C;
    constexpr uintptr_t dwForceJump      = 0x52BCD88;
    constexpr uintptr_t dwForceLeft      = 0x322EE50;
    constexpr uintptr_t dwForceRight     = 0x322EE5C;
    constexpr uintptr_t dwMouseEnable    = 0x523A260;
    constexpr uintptr_t dwMouseEnablePtr = 0x523A230;
    // engine.dll
    constexpr uintptr_t dwClientState                = 0x59F19C;
    constexpr uintptr_t dwClientState_GetLocalPlayer = 0x180;
    constexpr uintptr_t dwClientState_ViewAngles     = 0x4D90;
    // shaderapidx9.dll
    constexpr uintptr_t dwppDirect3DDevice9 = 0xA62C0;

    constexpr int FL_ONGROUND = 1 << 0;
    constexpr int MOVETYPE_NOCLIP = 8, MOVETYPE_LADDER = 9, MOVETYPE_OBSERVER = 10;
    constexpr int VT_RESET = 16, VT_PRESENT = 17;
}

struct Cfg {
    bool  bhop = true, strafe = true, edgebug = true, surf = true;
    int   kEdge = 'X', kSurf = 'C';
    float strafeDz = 0.02f, ebFall = -6.0f, surfVz = 2.0f;
} cfg;

static uintptr_t g_client, g_engine;
static volatile bool g_open = false, g_unload = false;
static volatile long g_presentCalls = 0;
static const char* g_status = "";

// ---------- config json ----------
static const char* CFG_PATH = "csgo_mv.json";

static void SaveCfg() {
    FILE* f = fopen(CFG_PATH, "w");
    if (!f) { g_status = "save failed"; return; }
    fprintf(f, "{\n\"bhop\": %d,\n\"strafe\": %d,\n\"edgebug\": %d,\n\"surf\": %d,\n"
               "\"kEdge\": %d,\n\"kSurf\": %d,\n\"strafeDz\": %f,\n\"ebFall\": %f,\n\"surfVz\": %f\n}\n",
            cfg.bhop, cfg.strafe, cfg.edgebug, cfg.surf, cfg.kEdge, cfg.kSurf,
            cfg.strafeDz, cfg.ebFall, cfg.surfVz);
    fclose(f);
    g_status = "saved";
}

static double FindNum(const char* b, const char* k, double d) {
    char key[48]; snprintf(key, sizeof(key), "\"%s\":", k);
    const char* p = strstr(b, key);
    return p ? atof(p + strlen(key)) : d;
}

static void LoadCfg() {
    FILE* f = fopen(CFG_PATH, "r");
    if (!f) { g_status = "no config file"; return; }
    char buf[1024] = {}; fread(buf, 1, sizeof(buf) - 1, f); fclose(f);
    cfg.bhop    = FindNum(buf, "bhop", cfg.bhop) != 0;
    cfg.strafe  = FindNum(buf, "strafe", cfg.strafe) != 0;
    cfg.edgebug = FindNum(buf, "edgebug", cfg.edgebug) != 0;
    cfg.surf    = FindNum(buf, "surf", cfg.surf) != 0;
    cfg.kEdge   = (int)FindNum(buf, "kEdge", cfg.kEdge);
    cfg.kSurf   = (int)FindNum(buf, "kSurf", cfg.kSurf);
    cfg.strafeDz = (float)FindNum(buf, "strafeDz", cfg.strafeDz);
    cfg.ebFall   = (float)FindNum(buf, "ebFall", cfg.ebFall);
    cfg.surfVz   = (float)FindNum(buf, "surfVz", cfg.surfVz);
    g_status = "loaded";
}

// ---------- menu ----------
static int* g_binding = nullptr;

static const char* KeyName(int vk) {
    static char n[32];
    UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    if (!GetKeyNameTextA((LONG)(sc << 16), n, sizeof(n))) snprintf(n, sizeof(n), "VK%d", vk);
    return n;
}

static void KeyBind(const char* id, int* key) {
    char b[64];
    snprintf(b, sizeof(b), "[%s]##%s", g_binding == key ? "..." : KeyName(*key), id);
    if (ImGui::Button(b, ImVec2(80, 0))) g_binding = key;
}

static void PollBinding() {
    if (!g_binding) return;
    if (GetAsyncKeyState(VK_ESCAPE) & 1) { g_binding = nullptr; return; }
    for (int vk = 4; vk < 255; vk++) {
        if (vk == VK_INSERT || vk == VK_ESCAPE) continue;
        if (GetAsyncKeyState(vk) & 1) { *g_binding = vk; g_binding = nullptr; return; }
    }
}

static void DrawMenu() {
    PollBinding();
    ImGui::SetNextWindowSize(ImVec2(360, 260), ImGuiCond_FirstUseEver);
    ImGui::Begin("nuuu | csgo movement", nullptr, ImGuiWindowFlags_NoCollapse);
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("MOVEMENT")) {
            ImGui::Checkbox("Bhop (hold SPACE)", &cfg.bhop);
            ImGui::Checkbox("Autostrafe (air + SPACE)", &cfg.strafe);
            ImGui::Checkbox("Edgebug assist", &cfg.edgebug); ImGui::SameLine(); KeyBind("eb", &cfg.kEdge);
            ImGui::Checkbox("Pixelsurf assist", &cfg.surf);  ImGui::SameLine(); KeyBind("ps", &cfg.kSurf);
            ImGui::TextDisabled("edgebug / pixelsurf = hold bind");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("TUNE")) {
            ImGui::SliderFloat("strafe deadzone", &cfg.strafeDz, 0.0f, 0.5f, "%.3f");
            ImGui::SliderFloat("edgebug min fall vz", &cfg.ebFall, -50.0f, 0.0f, "%.1f");
            ImGui::SliderFloat("pixelsurf |vz| window", &cfg.surfVz, 0.1f, 10.0f, "%.1f");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("CONFIG")) {
            if (ImGui::Button("Save")) SaveCfg();
            ImGui::SameLine();
            if (ImGui::Button("Load")) LoadCfg();
            ImGui::TextDisabled("%s  (%s)", CFG_PATH, g_status);
            ImGui::Separator();
            if (ImGui::Button("Unload")) g_unload = true;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ---------- mouse unlock ----------
// cl_mouseenable is stored xor'd with its own pointer. not verified on this build
static void SetMouse(bool enable) {
#if USE_MOUSE_XOR
    uintptr_t ptr = g_client + off::dwMouseEnablePtr;
    *(int*)(g_client + off::dwMouseEnable) = (int)ptr ^ (enable ? 1 : 0);
#else
    (void)enable;
#endif
}

// ---------- d3d9 hooks (vtable swap on the game's device) ----------
using PresentFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using ResetFn   = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

static void**    g_vt;
static PresentFn oPresent;
static ResetFn   oReset;
static HWND      g_hwnd;
static WNDPROC   g_oldProc;
static bool      g_init;

static void VtSet(int idx, void* fn) {
    DWORD old;
    VirtualProtect(&g_vt[idx], sizeof(void*), PAGE_READWRITE, &old);
    g_vt[idx] = fn;
    VirtualProtect(&g_vt[idx], sizeof(void*), old, &old);
}

static LRESULT CALLBACK hkWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_open) {
        ImGui_ImplWin32_WndProcHandler(h, m, w, l);
        if (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) return TRUE;
        if (m == WM_KEYDOWN || m == WM_KEYUP || m == WM_CHAR || m == WM_SYSKEYDOWN || m == WM_SYSKEYUP) return TRUE;
    }
    return CallWindowProcA(g_oldProc, h, m, w, l);
}

static HRESULT WINAPI hkPresent(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND c, const RGNDATA* d) {
    InterlockedIncrement(&g_presentCalls);
    if (!g_init) {
        g_hwnd = FindWindowA("Valve001", nullptr);
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(g_hwnd);
        ImGui_ImplDX9_Init(dev);
        g_oldProc = (WNDPROC)SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
        g_init = true;
    }
    if (g_open) {
        ImGui::GetIO().MouseDrawCursor = true;
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawMenu();
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }
    return oPresent(dev, a, b, c, d);
}

static HRESULT WINAPI hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    if (g_init) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = oReset(dev, pp);
    if (g_init && SUCCEEDED(hr)) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

// ---------- movement ----------
template <class T> static T rd(uintptr_t a) { return *(T*)a; }

static uintptr_t LocalPlayer(uintptr_t cs) {
    // primary: direct pointer
    uintptr_t lp = rd<uintptr_t>(g_client + off::dwLocalPlayer);
    if (lp) return lp;
    // fallback: clientstate slot -> entity index is slot+1
    int slot = rd<int>(cs + off::dwClientState_GetLocalPlayer);
    if (slot < 0 || slot > 63) return 0;
    return rd<uintptr_t>(g_client + off::dwEntityList + (uintptr_t)(slot + 1) * 0x10);
}

// duck via scancode, default crouch = LCTRL. change wScan if rebound
static bool g_duckHeld = false;
static void Duck(bool down) {
    if (g_duckHeld == down) return;
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = 0x1D;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &in, sizeof(in));
    g_duckHeld = down;
}

static float NormYaw(float a) {
    while (a > 180.f) a -= 360.f;
    while (a < -180.f) a += 360.f;
    return a;
}

static DWORD WINAPI Main(LPVOID mod) {
    AllocConsole();
    FILE* cf; freopen_s(&cf, "CONOUT$", "w", stdout);
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[mv] loaded, waiting for modules\n");

    while (!(g_client = (uintptr_t)GetModuleHandleA("client.dll"))) Sleep(100);
    while (!(g_engine = (uintptr_t)GetModuleHandleA("engine.dll"))) Sleep(100);
    uintptr_t shader;
    while (!(shader = (uintptr_t)GetModuleHandleA("shaderapidx9.dll"))) Sleep(100);
    printf("[mv] client=%p engine=%p shaderapi=%p\n", (void*)g_client, (void*)g_engine, (void*)shader);

    IDirect3DDevice9* dev = nullptr;
    while (!(dev = *(IDirect3DDevice9**)(shader + off::dwppDirect3DDevice9))) Sleep(100);
    g_vt = *(void***)dev;
    oPresent = (PresentFn)g_vt[off::VT_PRESENT];
    oReset   = (ResetFn)g_vt[off::VT_RESET];
    VtSet(off::VT_PRESENT, (void*)hkPresent);
    VtSet(off::VT_RESET, (void*)hkReset);
    printf("[mv] device=%p, Present hooked | INSERT menu | END unload\n", (void*)dev);

    auto forceJump  = (int*)(g_client + off::dwForceJump);
    auto forceLeft  = (int*)(g_client + off::dwForceLeft);
    auto forceRight = (int*)(g_client + off::dwForceRight);

    float prevYaw = 0.f;
    bool surfTap = false;
    DWORD lastLog = 0;
    LoadCfg();

    while (!g_unload) {
        if (GetAsyncKeyState(VK_END) & 1) break;
        if (GetAsyncKeyState(VK_INSERT) & 1) {
            g_open = !g_open;
            g_binding = nullptr;
            SetMouse(!g_open);
            if (!g_open && g_init) ImGui::GetIO().MouseDrawCursor = false;
            printf("[mv] menu %s (present calls: %ld)\n", g_open ? "open" : "closed", g_presentCalls);
        }

        uintptr_t cs = rd<uintptr_t>(g_engine + off::dwClientState);
        uintptr_t lp = cs ? LocalPlayer(cs) : 0;

        DWORD now = GetTickCount();
        bool log = now - lastLog > 1000;
        if (log) lastLog = now;

        if (!lp) {
            if (log) printf("[mv] no local player (cs=%p) — not in a match or offsets don't match build\n", (void*)cs);
            Duck(false);
            Sleep(10);
            continue;
        }

        uint8_t life = rd<uint8_t>(lp + off::m_lifeState);
        int mt       = rd<int>(lp + off::m_MoveType);
        int flags    = rd<int>(lp + off::m_fFlags);
        float vz     = rd<float>(lp + off::m_vecVelocity + 8);
        float yaw    = rd<float>(cs + off::dwClientState_ViewAngles + 4);
        bool ground  = flags & off::FL_ONGROUND;
        bool space   = GetAsyncKeyState(VK_SPACE) & 0x8000;

        if (log)
            printf("[mv] lp=%p life=%u mt=%d flags=%d ground=%d vz=%.1f yaw=%.1f\n",
                   (void*)lp, life, mt, flags, ground, vz, yaw);

        if (life != 0 || g_open || mt == off::MOVETYPE_NOCLIP || mt == off::MOVETYPE_LADDER ||
            mt == off::MOVETYPE_OBSERVER) {
            Duck(false);
            Sleep(g_open ? 10 : 5);
            continue;
        }

        if (cfg.bhop && space && ground) *forceJump = 6;

        if (cfg.strafe && space && !ground) {
            float d = NormYaw(yaw - prevYaw);
            if (d > cfg.strafeDz)       { *forceLeft = 5;  *forceRight = 4; }  // yaw+ = turn left
            else if (d < -cfg.strafeDz) { *forceRight = 5; *forceLeft = 4;  }
            else                        { *forceLeft = 4;  *forceRight = 4; }
        } else if (cfg.strafe) {
            if (!(GetAsyncKeyState('A') & 0x8000)) *forceLeft = 4;
            if (!(GetAsyncKeyState('D') & 0x8000)) *forceRight = 4;
        }
        prevYaw = yaw;

        bool eb = cfg.edgebug && (GetAsyncKeyState(cfg.kEdge) & 0x8000);
        bool ps = cfg.surf    && (GetAsyncKeyState(cfg.kSurf) & 0x8000);
        if (eb && !ground && vz < cfg.ebFall) {
            Duck(true);
        } else if (ps && !ground && fabsf(vz) < cfg.surfVz) {
            surfTap = !surfTap;
            Duck(surfTap);
        } else {
            surfTap = false;
            Duck(false);
        }

        Sleep(1);
    }

    // teardown: unhook,
