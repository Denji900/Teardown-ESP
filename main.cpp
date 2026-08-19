#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

constexpr uintptr_t OFFSET_SCENE_MANAGER = 0x50;
constexpr uintptr_t OFFSET_CAMERA_MANAGER = 0x60;
constexpr uintptr_t OFFSET_CAM_POS = 0x1CC;
constexpr uintptr_t OFFSET_CAM_ROT = 0x1D8;

constexpr uintptr_t OFFSET_GLOBAL_FOV = 0x380;
constexpr uintptr_t OFFSET_GLOBAL_FOV_ALT = 0x320;

constexpr uintptr_t OFFSET_PLAYER_COUNT = 0xB8;
constexpr uintptr_t OFFSET_PLAYER_SLOT_ARRAY = 0xC0;
constexpr uintptr_t OFFSET_LOCAL_SLOT_INDEX = 0x168;
constexpr uintptr_t OFFSET_PLAYER_POS = 0x570;

struct Vec2 {
    float x, y;
};

struct Vec3 {
    float x, y, z;
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    float Length() const { return std::sqrt(x * x + y * y + z * z); }
};

struct Vec4 {
    float x, y, z, w;
};

struct PlayerInfo {
    uintptr_t ptr;
    Vec3 pos;
    float dist;
    bool isLocal;
    std::string name;
};

struct SharedData {
    std::mutex mtx;
    bool attached = false;
    Vec3 camPos{};
    Vec4 camRot{};
    float baseFovDeg = 0.0f;
    float liveFovDeg = 0.0f;
    std::vector<PlayerInfo> players;
} g_sharedData;

std::atomic<bool> g_workerRunning{ true };

inline float HorizontalToVerticalFov(float hFovDeg, float aspect) {
    constexpr float DEG2RAD = 3.1415926535f / 180.0f;
    constexpr float RAD2DEG = 180.0f / 3.1415926535f;
    float hFovRad = hFovDeg * DEG2RAD;
    float vFovRad = 2.0f * std::atan(std::tan(hFovRad / 2.0f) / aspect);
    return vFovRad * RAD2DEG;
}

inline Vec3 RotateVectorByQuat(const Vec3& v, const Vec4& q) {
    float qx = q.x, qy = q.y, qz = q.z, qw = q.w;
    float vx = v.x, vy = v.y, vz = v.z;

    float tx = 2.0f * (qy * vz - qz * vy);
    float ty = 2.0f * (qz * vx - qx * vz);
    float tz = 2.0f * (qx * vy - qy * vx);

    return {
        vx + qw * tx + (qy * tz - qz * ty),
        vy + qw * ty + (qz * tx - qx * tz),
        vz + qw * tz + (qx * ty - qy * tx)
    };
}

inline bool WorldToScreen(const Vec3& worldPos, const Vec3& camPos, const Vec4& camQuat,
    float screenW, float screenH, float hFovDeg, Vec2& outScreen, float& outDist) {
    if (hFovDeg <= 10.0f) return false;

    Vec3 delta = worldPos - camPos;
    Vec4 invQuat = { -camQuat.x, -camQuat.y, -camQuat.z, camQuat.w };
    Vec3 view = RotateVectorByQuat(delta, invQuat);

    float zForward = -view.z;
    if (zForward <= 0.3f) return false;

    float aspect = screenW / screenH;
    float vFovDeg = HorizontalToVerticalFov(hFovDeg, aspect);
    float tanHalfVFov = std::tan((vFovDeg * 3.1415926535f / 180.0f) / 2.0f);

    float ndcX = view.x / (zForward * tanHalfVFov * aspect);
    float ndcY = view.y / (zForward * tanHalfVFov);

    if (std::abs(ndcX) > 1.3f || std::abs(ndcY) > 1.3f) return false;

    outScreen.x = (screenW / 2.0f) + (ndcX * (screenW / 2.0f));
    outScreen.y = (screenH / 2.0f) - (ndcY * (screenH / 2.0f));
    outDist = zForward;

    return true;
}

void MemoryWorkerThread() {
    HANDLE hProcess = nullptr;
    DWORD pid = 0;
    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    uintptr_t globalContext = 0;

    auto Cleanup = [&]() {
        if (hProcess) {
            CloseHandle(hProcess);
            hProcess = nullptr;
        }
        pid = 0;
        moduleBase = 0;
        moduleSize = 0;
        globalContext = 0;
        std::lock_guard<std::mutex> lock(g_sharedData.mtx);
        g_sharedData.attached = false;
        g_sharedData.baseFovDeg = 0.0f;
        g_sharedData.liveFovDeg = 0.0f;
        };

    while (g_workerRunning) {
        if (!hProcess || !globalContext) {
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W entry{ sizeof(PROCESSENTRY32W) };
                if (Process32FirstW(snap, &entry)) {
                    do {
                        if (_wcsicmp(entry.szExeFile, L"Teardown.exe") == 0 ||
                            _wcsicmp(entry.szExeFile, L"teardown.exe") == 0) {
                            pid = entry.th32ProcessID;
                            hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
                            break;
                        }
                    } while (Process32NextW(snap, &entry));
                }
                CloseHandle(snap);
            }

            if (hProcess) {
                HANDLE modSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
                if (modSnap != INVALID_HANDLE_VALUE) {
                    MODULEENTRY32W mEntry{ sizeof(MODULEENTRY32W) };
                    if (Module32FirstW(modSnap, &mEntry)) {
                        do {
                            if (_wcsicmp(mEntry.szModule, L"Teardown.exe") == 0 ||
                                _wcsicmp(mEntry.szModule, L"teardown.exe") == 0) {
                                moduleBase = (uintptr_t)mEntry.modBaseAddr;
                                moduleSize = mEntry.modBaseSize;
                                break;
                            }
                        } while (Module32NextW(modSnap, &mEntry));
                    }
                    CloseHandle(modSnap);
                }

                if (moduleBase && moduleSize) {
                    std::vector<BYTE> memory(moduleSize);
                    SIZE_T bytesRead = 0;
                    if (ReadProcessMemory(hProcess, (LPCVOID)moduleBase, memory.data(), moduleSize, &bytesRead)) {
                        constexpr BYTE pattern[] = { 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x58, 0x60 };
                        constexpr char mask[] = "xxx????xxxx";
                        constexpr size_t patternSize = sizeof(pattern);
                        constexpr size_t maskSize = sizeof(mask) - 1;

                        for (size_t i = 0; i + patternSize <= bytesRead; ++i) {
                            bool found = true;
                            for (size_t j = 0; j < maskSize; ++j) {
                                if (mask[j] != '?' && memory[i + j] != pattern[j]) {
                                    found = false;
                                    break;
                                }
                            }
                            if (found) {
                                uintptr_t insAddr = moduleBase + i;
                                int32_t relOffset = 0;
                                std::memcpy(&relOffset, &memory[i + 3], sizeof(relOffset));
                                uintptr_t gCtxPtr = insAddr + 7 + relOffset;
                                ReadProcessMemory(hProcess, (LPCVOID)gCtxPtr, &globalContext, sizeof(uintptr_t), nullptr);
                                break;
                            }
                        }
                    }
                }
            }

            if (!globalContext) {
                Cleanup();
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }
        }

        Vec3 localCamPos{};
        Vec4 localCamRot{};
        float readBaseFov = 0.0f;
        std::vector<PlayerInfo> localPlayers;
        localPlayers.reserve(32);

        uintptr_t camManager = 0;
        if (ReadProcessMemory(hProcess, (LPCVOID)(globalContext + OFFSET_CAMERA_MANAGER), &camManager, sizeof(uintptr_t), nullptr) && camManager) {
            ReadProcessMemory(hProcess, (LPCVOID)(camManager + OFFSET_CAM_POS), &localCamPos, sizeof(Vec3), nullptr);
            ReadProcessMemory(hProcess, (LPCVOID)(camManager + OFFSET_CAM_ROT), &localCamRot, sizeof(Vec4), nullptr);
        }

        int32_t liveFovSetting = 0;
        if (ReadProcessMemory(hProcess, (LPCVOID)(globalContext + OFFSET_GLOBAL_FOV), &liveFovSetting, sizeof(int32_t), nullptr) &&
            liveFovSetting >= 40 && liveFovSetting <= 150) {
            readBaseFov = (float)liveFovSetting;
        }
        else if (ReadProcessMemory(hProcess, (LPCVOID)(globalContext + OFFSET_GLOBAL_FOV_ALT), &liveFovSetting, sizeof(int32_t), nullptr) &&
            liveFovSetting >= 40 && liveFovSetting <= 150) {
            readBaseFov = (float)liveFovSetting;
        }

        int32_t playerCount = 0;
        uintptr_t slotArray = 0;
        int32_t localIndex = -1;

        ReadProcessMemory(hProcess, (LPCVOID)(globalContext + OFFSET_PLAYER_COUNT), &playerCount, sizeof(int32_t), nullptr);
        ReadProcessMemory(hProcess, (LPCVOID)(globalContext + OFFSET_PLAYER_SLOT_ARRAY), &slotArray, sizeof(uintptr_t), nullptr);
        ReadProcessMemory(hProcess, (LPCVOID)(globalContext + OFFSET_LOCAL_SLOT_INDEX), &localIndex, sizeof(int32_t), nullptr);

        if (slotArray && playerCount > 0 && playerCount <= 32) {
            std::vector<uintptr_t> slots(playerCount);
            if (ReadProcessMemory(hProcess, (LPCVOID)slotArray, slots.data(), playerCount * sizeof(uintptr_t), nullptr)) {
                for (int i = 0; i < playerCount; ++i) {
                    uintptr_t pPtr = slots[i];
                    if (!pPtr) continue;

                    Vec3 pPos{};
                    if (ReadProcessMemory(hProcess, (LPCVOID)(pPtr + OFFSET_PLAYER_POS), &pPos, sizeof(Vec3), nullptr)) {
                        if (std::isnan(pPos.x) || (pPos.x == 0.0f && pPos.y == 0.0f && pPos.z == 0.0f))
                            continue;

                        float dist = (pPos - localCamPos).Length();
                        bool isLocal = (i == localIndex);
                        std::string pName = isLocal ? "Player 1 (You)" : ("Player " + std::to_string(i + 1));
                        localPlayers.push_back({ pPtr, pPos, dist, isLocal, pName });
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_sharedData.mtx);
            g_sharedData.attached = true;
            g_sharedData.camPos = localCamPos;
            g_sharedData.camRot = localCamRot;
            g_sharedData.baseFovDeg = readBaseFov;
            g_sharedData.liveFovDeg = readBaseFov;
            g_sharedData.players = std::move(localPlayers);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(6));
    }

    Cleanup();
}

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
HWND g_hWnd = nullptr;

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 0;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && g_pSwapChain != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int main() {
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"TeardownOverlay", nullptr };
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    g_hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED,
        wc.lpszClassName, L"Teardown Overlay",
        WS_POPUP, 0, 0, screenW, screenH,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(g_hWnd, &margins);
    SetLayeredWindowAttributes(g_hWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    if (!CreateDeviceD3D(g_hWnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(g_hWnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hWnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.95f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.24f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 1.00f, 0.40f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.16f, 0.16f, 0.24f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.24f, 0.36f, 1.00f);

    ImGui_ImplWin32_Init(g_hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    std::thread worker(MemoryWorkerThread);

    bool menuOpen = true;
    bool espBox = true;
    bool tracers = true;

    Vec3 camPos{};
    Vec4 camRot{};
    float liveFovDeg = 0.0f;
    std::vector<PlayerInfo> players;
    players.reserve(32);
    bool attached = false;

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        if (GetAsyncKeyState(VK_INSERT) & 1) {
            menuOpen = !menuOpen;
            LONG_PTR exStyle = GetWindowLongPtr(g_hWnd, GWL_EXSTYLE);
            if (menuOpen) {
                SetWindowLongPtr(g_hWnd, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT);
            }
            else {
                SetWindowLongPtr(g_hWnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_sharedData.mtx);
            attached = g_sharedData.attached;
            camPos = g_sharedData.camPos;
            camRot = g_sharedData.camRot;
            liveFovDeg = g_sharedData.liveFovDeg;
            players = g_sharedData.players;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        float midX = screenW / 2.0f;

        for (const auto& player : players) {
            Vec2 screenPos{};
            float dist = 0.0f;

            if (WorldToScreen(player.pos + Vec3{ 0.0f, 0.9f, 0.0f }, camPos, camRot, (float)screenW, (float)screenH, liveFovDeg, screenPos, dist)) {
                if (tracers && !player.isLocal) {
                    drawList->AddLine({ midX, (float)screenH }, { screenPos.x, screenPos.y }, IM_COL32(0, 255, 120, 200), 1.5f);
                }

                if (espBox) {
                    Vec2 topScreen{}, botScreen{};
                    float d1 = 0.0f, d2 = 0.0f;
                    if (WorldToScreen(player.pos + Vec3{ 0.0f, 1.8f, 0.0f }, camPos, camRot, (float)screenW, (float)screenH, liveFovDeg, topScreen, d1) &&
                        WorldToScreen(player.pos, camPos, camRot, (float)screenW, (float)screenH, liveFovDeg, botScreen, d2)) {

                        float boxH = std::abs(botScreen.y - topScreen.y);
                        float boxW = boxH * 0.45f;
                        ImU32 col = player.isLocal ? IM_COL32(0, 255, 60, 255) : IM_COL32(0, 255, 200, 255);

                        drawList->AddRect(
                            { topScreen.x - (boxW / 2.0f), topScreen.y },
                            { topScreen.x + (boxW / 2.0f), botScreen.y },
                            col, 0.0f, 0, 2.0f
                        );
                    }
                }
            }
        }

        if (menuOpen) {
            ImGui::SetNextWindowSize({ 460, 480 }, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Teardown ESP [INSERT to Hide]", nullptr, ImGuiWindowFlags_NoCollapse)) {
                ImGui::TextColored({ 0.0f, 1.0f, 0.4f, 1.0f }, "TEARDOWN ESP");
                ImGui::TextColored(attached ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1),
                    attached ? "[V] Attached to Teardown.exe" : "[-] Searching for Teardown.exe...");
                ImGui::Separator();

                if (ImGui::CollapsingHeader("VISUAL OVERLAYS", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("3D Bounding Box Outline", &espBox);
                    ImGui::Checkbox("Tracers", &tracers);
                }

                if (ImGui::CollapsingHeader("LIVE PLAYERS TABLE", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Text("Camera Pos: (%.2f, %.2f, %.2f)", camPos.x, camPos.y, camPos.z);

                    if (ImGui::BeginTable("PlayersTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Player");
                        ImGui::TableSetupColumn("X");
                        ImGui::TableSetupColumn("Y (Height)");
                        ImGui::TableSetupColumn("Z");
                        ImGui::TableSetupColumn("Distance");
                        ImGui::TableHeadersRow();

                        for (const auto& player : players) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", player.name.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%.2f", player.pos.x);
                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("%.2f", player.pos.y);
                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%.2f", player.pos.z);
                            ImGui::TableSetColumnIndex(4);
                            ImGui::Text("%.1fm", player.dist);
                        }
                        ImGui::EndTable();
                    }
                }
            }
            ImGui::End();
        }

        ImGui::Render();
        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    g_workerRunning = false;
    if (worker.joinable()) worker.join();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(g_hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}