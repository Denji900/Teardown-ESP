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
#include <cfloat>
#include <iostream>
#include <fstream>
#include <sstream>
#include <array>
#include <map>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool  g_espBox = true;
bool  g_skeletonEsp = true;
bool  g_tracers = true;
bool  g_showJointBadges = false;

bool  g_aimbotEnabled = false;
bool  g_drawAimFov = true;
float g_aimFovRadius = 140.0f;
float g_headHeightOffset = 0.05f;
int   g_aimKey = VK_RBUTTON;
bool  g_waitingForKey = false;

std::string GetConfigPath() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string p(path);
    size_t pos = p.find_last_of("\\/");
    if (pos != std::string::npos) {
        return p.substr(0, pos) + "\\teardown_settings.json";
    }
    return "teardown_settings.json";
}

void SaveConfig() {
    std::ofstream file(GetConfigPath());
    if (file.is_open()) {
        file << "{\n";
        file << "  \"espBox\": " << (g_espBox ? "true" : "false") << ",\n";
        file << "  \"skeletonEsp\": " << (g_skeletonEsp ? "true" : "false") << ",\n";
        file << "  \"tracers\": " << (g_tracers ? "true" : "false") << ",\n";
        file << "  \"showJointBadges\": " << (g_showJointBadges ? "true" : "false") << ",\n";
        file << "  \"aimbotEnabled\": " << (g_aimbotEnabled ? "true" : "false") << ",\n";
        file << "  \"drawAimFov\": " << (g_drawAimFov ? "true" : "false") << ",\n";
        file << "  \"aimFovRadius\": " << g_aimFovRadius << ",\n";
        file << "  \"aimKey\": " << g_aimKey << "\n";
        file << "}\n";
        file.close();
    }
}

void LoadConfig() {
    std::ifstream file(GetConfigPath());
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("\"espBox\": true") != std::string::npos) g_espBox = true;
            if (line.find("\"espBox\": false") != std::string::npos) g_espBox = false;

            if (line.find("\"skeletonEsp\": true") != std::string::npos) g_skeletonEsp = true;
            if (line.find("\"skeletonEsp\": false") != std::string::npos) g_skeletonEsp = false;

            if (line.find("\"tracers\": true") != std::string::npos) g_tracers = true;
            if (line.find("\"tracers\": false") != std::string::npos) g_tracers = false;

            if (line.find("\"showJointBadges\": true") != std::string::npos) g_showJointBadges = true;
            if (line.find("\"showJointBadges\": false") != std::string::npos) g_showJointBadges = false;

            if (line.find("\"aimbotEnabled\": true") != std::string::npos) g_aimbotEnabled = true;
            if (line.find("\"aimbotEnabled\": false") != std::string::npos) g_aimbotEnabled = false;

            if (line.find("\"drawAimFov\": true") != std::string::npos) g_drawAimFov = true;
            if (line.find("\"drawAimFov\": false") != std::string::npos) g_drawAimFov = false;

            size_t pos;
            if ((pos = line.find("\"aimFovRadius\": ")) != std::string::npos) g_aimFovRadius = std::stof(line.substr(pos + 16));
            if ((pos = line.find("\"aimKey\": ")) != std::string::npos) g_aimKey = std::stoi(line.substr(pos + 10));
        }
        file.close();
    }
}

uintptr_t OFFSET_SCENE_MANAGER = 0x50;
uintptr_t OFFSET_CAMERA_MANAGER = 0x60;
uintptr_t OFFSET_CAM_POS = 0x1D0;
uintptr_t OFFSET_CAM_ROT = 0x1DC;

uintptr_t OFFSET_GLOBAL_FOV = 0x380;
uintptr_t OFFSET_GLOBAL_FOV_ALT = 0x320;
uintptr_t OFFSET_PLAYER_COUNT = 0xB8;
uintptr_t OFFSET_PLAYER_SLOT_ARRAY = 0xC0;
uintptr_t OFFSET_LOCAL_SLOT_INDEX = 0x168;

uintptr_t OFFSET_PLAYER_POS = 0x570;
uintptr_t OFFSET_PLAYER_VEL = 0x5FC;
uintptr_t OFFSET_PLAYER_PITCH = 0x700;
uintptr_t OFFSET_PLAYER_YAW = 0x704;
uintptr_t OFFSET_PLAYER_ROT = 0x70C;
uintptr_t OFFSET_PLAYER_HEALTH = 0x7C8;
uintptr_t OFFSET_DAMAGE_TIMER = 0x9E0;
uintptr_t OFFSET_PLAYER_TOOL = 0x990;
uintptr_t OFFSET_PLAYER_VEHICLE_PTR = 0x998;
uintptr_t OFFSET_PLAYER_HEAD_POS = 0x640;
uintptr_t OFFSET_PLAYER_CHARACTER_HANDLE = 0x2DC8;
uintptr_t OFFSET_PLAYER_RIG_HANDLE = 0x2DD4;
uintptr_t OFFSET_PLAYER_IS_AIMING = 0x2C8E;
uintptr_t OFFSET_PLAYER_IS_CROUCHING = 0x168;

uintptr_t OFFSET_NODE_TYPE = 0x08;
uintptr_t OFFSET_NODE_NEXT_SIBLING = 0x18;
uintptr_t OFFSET_NODE_FIRST_CHILD = 0x28;
uintptr_t OFFSET_NODE_WORLD_POS = 0x40;

uintptr_t OFFSET_LOCAL_ENTITY_COUNT = 0x208;
uintptr_t OFFSET_LOCAL_ENTITY_ARRAY = 0x210;
uintptr_t OFFSET_GLOBAL_ENTITY_COUNT = 0xA18;
uintptr_t OFFSET_GLOBAL_ENTITY_ARRAY = 0xA20;

struct Vec2 { float x, y; };

struct Vec3 {
    float x, y, z;
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    float Length() const { return std::sqrtf(x * x + y * y + z * z); }
    float Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
};

struct Vec4 { float x, y, z, w; };

struct LiveBone {
    Vec3 pos{};
    int32_t parentIdx = -1;
};

struct PlayerInfo {
    uintptr_t ptr = 0;
    Vec3 pos{};
    Vec3 headPos{};
    Vec4 rot{ 0, 0, 0, 1 };
    Vec3 velocity{};
    float health = 1.0f;
    float dist = 0.0f;
    bool isLocal = false;
    bool isCrouching = false;
    bool isAiming = false;
    bool isDriving = false;
    std::string activeTool = "";
    int32_t charHandle = 0;
    int32_t rigHandle = 0;
    std::array<LiveBone, 32> liveBones;
    size_t boneCount = 0;
};

struct SharedData {
    std::mutex mtx;
    bool attached = false;
    HANDLE hProcess = nullptr;
    uintptr_t globalContext = 0;
    uintptr_t localPlayerPtr = 0;
    Vec3 camPos{};
    Vec4 camRot{};
    float liveFovDeg = 0.0f;
    float dbgLivePlayerYaw = 0.0f;
    float dbgLivePlayerPitch = 0.0f;
    std::vector<PlayerInfo> players;
    uint64_t updateTick = 0;
} g_sharedData;

std::atomic<bool> g_workerRunning{ true };

std::string ReadTeardownString(HANDLE hProc, uintptr_t address) {
    char buf[32] = { 0 };
    if (!ReadProcessMemory(hProc, (LPCVOID)address, buf, 32, nullptr)) return "";

    if (buf[31] != 0) {
        uintptr_t ptr = 0;
        std::memcpy(&ptr, buf, sizeof(uintptr_t));
        if (ptr > 0x10000) {
            char extBuf[128] = { 0 };
            ReadProcessMemory(hProc, (LPCVOID)ptr, extBuf, 127, nullptr);
            return std::string(extBuf);
        }
    }
    buf[31] = '\0';
    return std::string(buf);
}

inline float HorizontalToVerticalFov(float hFovDeg, float aspect) {
    float hFovRad = hFovDeg * (M_PI / 180.0f);
    float vFovRad = 2.0f * std::atan(std::tan(hFovRad / 2.0f) / aspect);
    return vFovRad * (180.0f / M_PI);
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
    if (hFovDeg <= 0.5f) return false;
    Vec3 delta = worldPos - camPos;
    Vec4 invQuat = { -camQuat.x, -camQuat.y, -camQuat.z, camQuat.w };
    Vec3 view = RotateVectorByQuat(delta, invQuat);
    float zForward = -view.z;
    if (zForward <= 0.1f) return false;
    float aspect = screenW / screenH;
    float vFovDeg = HorizontalToVerticalFov(hFovDeg, aspect);
    float tanHalfVFov = std::tan((vFovDeg * M_PI / 180.0f) / 2.0f);
    float ndcX = view.x / (zForward * tanHalfVFov * aspect);
    float ndcY = view.y / (zForward * tanHalfVFov);

    outScreen.x = (screenW / 2.0f) + (ndcX * (screenW / 2.0f));
    outScreen.y = (screenH / 2.0f) - (ndcY * (screenH / 2.0f));
    outDist = zForward;
    return true;
}

inline float NormalizeAngle(float target, float current) {
    float diff = fmod(target - current, 2.0f * M_PI);
    if (diff > M_PI) diff -= 2.0f * M_PI;
    else if (diff < -M_PI) diff += 2.0f * M_PI;
    return diff;
}

void DrawSkeleton(ImDrawList* drawList, const PlayerInfo& player, const Vec3& camPos, const Vec4& camRot, float screenW, float screenH, float activeFov) {
    ImU32 boneCol = player.isLocal ? IM_COL32(100, 255, 100, 255) : IM_COL32(0, 255, 100, 255);

    if (player.boneCount >= 2) {
        std::array<Vec2, 32> screenJoints;
        std::array<bool, 32> valid = { false };

        for (size_t i = 0; i < player.boneCount; ++i) {
            if (i == 18 || i == 21 || i == 22) continue;

            float d = 0.0f;
            if (WorldToScreen(player.liveBones[i].pos, camPos, camRot, screenW, screenH, activeFov, screenJoints[i], d)) {
                valid[i] = true;

                if (g_showJointBadges) {
                    char badge[16];
                    snprintf(badge, sizeof(badge), "#%d", (int)i);
                    drawList->AddText({ screenJoints[i].x + 5.0f, screenJoints[i].y - 8.0f }, IM_COL32(255, 255, 100, 255), badge);
                }
            }
        }

        const std::pair<int, int> exactConnections[] = {
            { 16, 17 }, { 17, 19 }, { 19, 20 }, { 20, 23 },
            { 17, 11 }, { 11, 10 }, { 10, 9 }, { 9, 8 },
            { 17, 15 }, { 15, 14 }, { 14, 13 }, { 13, 12 },
            { 23, 3 }, { 3, 2 }, { 2, 1 }, { 1, 0 },
            { 23, 7 }, { 7, 6 }, { 6, 5 }, { 5, 4 }
        };

        for (const auto& conn : exactConnections) {
            int a = conn.first;
            int b = conn.second;

            if (a >= 0 && (size_t)a < player.boneCount && b >= 0 && (size_t)b < player.boneCount) {
                if (valid[a] && valid[b]) {
                    drawList->AddLine(
                        { screenJoints[a].x, screenJoints[a].y },
                        { screenJoints[b].x, screenJoints[b].y },
                        boneCol, 2.2f
                    );
                }
            }
        }
    }
}

class PatternScanner {
public:
    std::vector<BYTE> buffer;
    uintptr_t baseAddress = 0;

    bool Init(HANDLE hProcess, uintptr_t moduleBase, size_t moduleSize) {
        baseAddress = moduleBase;
        buffer.resize(moduleSize);
        SIZE_T bytesRead = 0;
        return ReadProcessMemory(hProcess, (LPCVOID)moduleBase, buffer.data(), moduleSize, &bytesRead) && bytesRead > 0;
    }

    uintptr_t FindPattern(const char* szSignature, uintptr_t startOffset = 0, size_t maxDistance = 0) {
        std::vector<int> patternBytes;
        char* start = const_cast<char*>(szSignature);
        char* end = start + strlen(szSignature);
        for (char* current = start; current < end; ++current) {
            if (*current == '?') {
                ++current;
                if (*current == '?') ++current;
                patternBytes.push_back(-1);
            }
            else if (*current != ' ') {
                patternBytes.push_back(strtol(current, &current, 16));
            }
        }

        size_t s = patternBytes.size();
        int* d = patternBytes.data();
        size_t searchEnd = maxDistance > 0 ? (startOffset + maxDistance) : (buffer.size() - s);
        if (searchEnd > buffer.size() - s) searchEnd = buffer.size() - s;

        for (size_t i = startOffset; i < searchEnd; ++i) {
            bool found = true;
            for (size_t j = 0; j < s; ++j) {
                if (buffer[i + j] != d[j] && d[j] != -1) {
                    found = false;
                    break;
                }
            }
            if (found) return i;
        }
        return 0;
    }

    int32_t ReadInt32(uintptr_t offset) {
        if (offset + 4 > buffer.size()) return 0;
        int32_t val = 0;
        std::memcpy(&val, &buffer[offset], sizeof(int32_t));
        return val;
    }
};

void PatchMemory(HANDLE hProcess, uintptr_t address, const std::vector<BYTE>& bytes) {
    DWORD oldProtect = 0;
    VirtualProtectEx(hProcess, (LPVOID)address, bytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect);
    WriteProcessMemory(hProcess, (LPVOID)address, bytes.data(), bytes.size(), nullptr);
    VirtualProtectEx(hProcess, (LPVOID)address, bytes.size(), oldProtect, &oldProtect);
}

void AimbotWorkerThread() {
    float smoothedBobPitch = 0.0f;
    float smoothedBobYaw = 0.0f;
    bool bobbingInitialized = false;
    uint64_t lastTick = 0;

    while (g_workerRunning) {
        if (!g_aimbotEnabled || !(GetAsyncKeyState(g_aimKey) & 0x8000)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            bobbingInitialized = false;
            continue;
        }

        HANDLE hProc;
        uintptr_t locPtr;
        Vec3 camPos;
        Vec4 camRot;
        float fov;
        float playerPitch, playerYaw;
        std::vector<PlayerInfo> currentPlayers;

        {
            std::lock_guard<std::mutex> lock(g_sharedData.mtx);
            if (!g_sharedData.attached || !g_sharedData.localPlayerPtr || g_sharedData.updateTick == lastTick) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            lastTick = g_sharedData.updateTick;

            hProc = g_sharedData.hProcess;
            locPtr = g_sharedData.localPlayerPtr;
            camPos = g_sharedData.camPos;
            camRot = g_sharedData.camRot;
            fov = g_sharedData.liveFovDeg;
            playerPitch = g_sharedData.dbgLivePlayerPitch;
            playerYaw = g_sharedData.dbgLivePlayerYaw;
            currentPlayers = g_sharedData.players;
        }

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        float midX = screenW / 2.0f;
        float midY = screenH / 2.0f;

        Vec3 camForward = RotateVectorByQuat({ 0.0f, 0.0f, -1.0f }, camRot);
        float camYaw = std::atan2(-camForward.x, -camForward.z);
        float camPitch = std::atan2(camForward.y, std::sqrtf(camForward.x * camForward.x + camForward.z * camForward.z));

        float rawBobPitch = camPitch - playerPitch;
        float rawBobYaw = NormalizeAngle(camYaw, playerYaw);

        if (!bobbingInitialized) {
            smoothedBobPitch = rawBobPitch;
            smoothedBobYaw = rawBobYaw;
            bobbingInitialized = true;
        }
        else {
            smoothedBobPitch = smoothedBobPitch + 0.10f * (rawBobPitch - smoothedBobPitch);
            smoothedBobYaw = smoothedBobYaw + 0.10f * NormalizeAngle(rawBobYaw, smoothedBobYaw);
        }

        Vec3 bestTargetHeadPos{};
        float bestWorldDist = FLT_MAX;
        bool foundTarget = false;

        for (const auto& player : currentPlayers) {
            if (player.isLocal || player.health <= 0.0f) continue;

            Vec3 targetHead = player.pos + Vec3{ 0.0f, (player.isCrouching ? 1.20f : 1.72f) + g_headHeightOffset, 0.0f };

            if (player.boneCount > 17) {
                float bDist = (player.liveBones[17].pos - player.pos).Length();
                if (bDist >= 0.5f && bDist <= 2.5f) {
                    targetHead = player.liveBones[17].pos + Vec3{ 0.0f, g_headHeightOffset, 0.0f };
                }
            }
            else if (!std::isnan(player.headPos.x) && player.headPos.Length() > 1.0f) {
                float hDist = (player.headPos - player.pos).Length();
                if (hDist >= 0.5f && hDist <= 2.5f) {
                    targetHead = player.headPos + Vec3{ 0.0f, g_headHeightOffset, 0.0f };
                }
            }

            Vec3 toTarget = targetHead - camPos;
            if (camForward.Dot(toTarget) <= 0.1f) continue;

            Vec2 headScreen{};
            float hDist = 0.0f;
            if (WorldToScreen(targetHead, camPos, camRot, (float)screenW, (float)screenH, fov, headScreen, hDist)) {
                float dx = headScreen.x - midX;
                float dy = headScreen.y - midY;
                float crosshairDist = std::sqrtf(dx * dx + dy * dy);

                if (crosshairDist <= g_aimFovRadius) {
                    float worldDist = toTarget.Length();
                    if (worldDist < bestWorldDist) {
                        bestWorldDist = worldDist;
                        bestTargetHeadPos = targetHead;
                        foundTarget = true;
                    }
                }
            }
        }

        if (foundTarget) {
            Vec3 delta = bestTargetHeadPos - camPos;
            float dist2D = std::sqrtf(delta.x * delta.x + delta.z * delta.z);

            float targetYaw = std::atan2(-delta.x, -delta.z);
            float targetPitch = std::atan2(delta.y, dist2D);

            float targetPlayerYaw = targetYaw - smoothedBobYaw;
            float targetPlayerPitch = targetPitch - smoothedBobPitch;

            float deltaYaw = NormalizeAngle(targetPlayerYaw, playerYaw);
            float deltaPitch = targetPlayerPitch - playerPitch;

            constexpr float AIM_DEADZONE = 0.0008f;
            if (std::abs(deltaYaw) > AIM_DEADZONE || std::abs(deltaPitch) > AIM_DEADZONE) {
                float compensatedYaw = playerYaw + deltaYaw;
                float compensatedPitch = std::clamp(playerPitch + deltaPitch, -1.55f, 1.55f);

                WriteProcessMemory(hProc, (LPVOID)(locPtr + OFFSET_PLAYER_PITCH), &compensatedPitch, sizeof(float), nullptr);
                WriteProcessMemory(hProc, (LPVOID)(locPtr + OFFSET_PLAYER_YAW), &compensatedYaw, sizeof(float), nullptr);
            }
        }
    }
}

void MemoryWorkerThread() {
    HANDLE hProcess = nullptr;
    DWORD pid = 0;
    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    uintptr_t globalContext = 0;

    uintptr_t healthWriteInstructionAddr = 0;
    std::vector<BYTE> originalHealthBytes;
    bool isPatched = false;

    std::map<uintptr_t, std::vector<std::pair<uintptr_t, int>>> boneAddressCache;

    auto Cleanup = [&]() {
        if (hProcess && healthWriteInstructionAddr && isPatched && !originalHealthBytes.empty()) {
            PatchMemory(hProcess, healthWriteInstructionAddr, originalHealthBytes);
            isPatched = false;
        }
        if (hProcess) {
            CloseHandle(hProcess);
            hProcess = nullptr;
        }
        pid = 0;
        moduleBase = 0;
        moduleSize = 0;
        globalContext = 0;
        healthWriteInstructionAddr = 0;
        originalHealthBytes.clear();
        boneAddressCache.clear();
        std::lock_guard<std::mutex> lock(g_sharedData.mtx);
        g_sharedData.attached = false;
        g_sharedData.hProcess = nullptr;
        g_sharedData.globalContext = 0;
        g_sharedData.localPlayerPtr = 0;
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
                            hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
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
                    PatternScanner scanner;
                    if (scanner.Init(hProcess, moduleBase, moduleSize)) {
                        auto RipOffsets = [&](uintptr_t addr, size_t len, int32_t minV, int32_t maxV) -> std::vector<int32_t> {
                            std::vector<int32_t> res;
                            if (!addr) return res;
                            for (size_t i = 0; i < len - 4; ++i) {
                                int32_t val = scanner.ReadInt32(addr + i);
                                if (val >= minV && val <= maxV) {
                                    if (std::find(res.begin(), res.end(), val) == res.end()) res.push_back(val);
                                }
                            }
                            return res;
                            };

                        uintptr_t sigCamTrans = scanner.FindPattern("48 89 5C 24 08 57 48 83 EC 40 49 8B F8 49");
                        if (sigCamTrans) {
                            uintptr_t globalCtxInst = scanner.FindPattern("48 8B 05 ? ? ? ?", sigCamTrans, 0x100);
                            if (globalCtxInst) {
                                int32_t relOffset = scanner.ReadInt32(globalCtxInst + 3);
                                uintptr_t gCtxPtr = moduleBase + globalCtxInst + 7 + relOffset;
                                ReadProcessMemory(hProcess, (LPCVOID)gCtxPtr, &globalContext, sizeof(uintptr_t), nullptr);
                            }
                            auto camMgr = RipOffsets(sigCamTrans, 100, 0x30, 0x90);
                            if (!camMgr.empty()) OFFSET_CAMERA_MANAGER = camMgr[0];

                            auto camPos = RipOffsets(sigCamTrans, 200, 0x100, 0x2A0);
                            if (!camPos.empty()) {
                                std::sort(camPos.begin(), camPos.end());
                                OFFSET_CAM_POS = camPos[0];
                                if (camPos.size() > 1) OFFSET_CAM_ROT = camPos[1];
                                else OFFSET_CAM_ROT = OFFSET_CAM_POS + 0xC;
                            }
                        }

                        uintptr_t sigBodyVel = scanner.FindPattern("40 53 48 83 EC 30 49 8B D8 48 8B CA 45 33 C0 33 D2 E8 ?? ?? ?? ?? 48 8B 0D");
                        if (sigBodyVel) {
                            auto scnMgr = RipOffsets(sigBodyVel, 100, 0x30, 0x80);
                            if (!scnMgr.empty()) OFFSET_SCENE_MANAGER = scnMgr[0];

                            auto gCnt = RipOffsets(sigBodyVel, 200, 0x800, 0xC00);
                            if (!gCnt.empty()) { OFFSET_GLOBAL_ENTITY_COUNT = gCnt[0]; OFFSET_GLOBAL_ENTITY_ARRAY = gCnt[0] + 8; }

                            auto lCnt = RipOffsets(sigBodyVel, 200, 0x100, 0x300);
                            if (!lCnt.empty()) { OFFSET_LOCAL_ENTITY_COUNT = lCnt[0]; OFFSET_LOCAL_ENTITY_ARRAY = lCnt[0] + 8; }
                        }

                        uintptr_t sigPlayerTrans = scanner.FindPattern("48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 4C 89 74 24 20 55 48 8B EC 48 83 EC 70 4D");
                        if (sigPlayerTrans) {
                            auto posOffs = RipOffsets(sigPlayerTrans, 150, 0x400, 0x800);
                            if (!posOffs.empty()) OFFSET_PLAYER_POS = *std::min_element(posOffs.begin(), posOffs.end());

                            auto rotOffs = RipOffsets(sigPlayerTrans, 250, 0x600, 0x900);
                            if (!rotOffs.empty()) {
                                OFFSET_PLAYER_ROT = *std::max_element(rotOffs.begin(), rotOffs.end());
                                OFFSET_PLAYER_PITCH = OFFSET_PLAYER_ROT - 0xC;
                                OFFSET_PLAYER_YAW = OFFSET_PLAYER_ROT - 0x8;
                            }
                        }

                        uintptr_t sigEyeTrans = scanner.FindPattern("48 89 5C 24 08 57 48 83 EC 60 49 8B F8 48 8B C2 48 8B D9 45 33 C0 33 D2 48 8B C8 E8 ?? ?? ?? ?? 8B D0 48 8B CB E8 ?? ?? ?? ?? 0F");
                        if (sigEyeTrans) {
                            auto eyeOffs = RipOffsets(sigEyeTrans, 200, 0x500, 0x800);
                            if (!eyeOffs.empty()) OFFSET_PLAYER_HEAD_POS = eyeOffs[0];
                        }

                        uintptr_t sigVelocity = scanner.FindPattern("48 89 5C 24 08 57 48 83 EC 30 48 8B C2 48");
                        if (sigVelocity) {
                            auto velOffs = RipOffsets(sigVelocity, 100, 0x400, 0x800);
                            if (!velOffs.empty()) OFFSET_PLAYER_VEL = velOffs[0];
                        }

                        uintptr_t sigTool = scanner.FindPattern("48 89 5C 24 08 57 48 81 EC B0 00 00 00 49 8B F8 48 8B C2");
                        if (sigTool) {
                            auto toolOffs = RipOffsets(sigTool, 100, 0x800, 0xC00);
                            if (!toolOffs.empty()) OFFSET_PLAYER_TOOL = toolOffs[0];
                        }

                        uintptr_t sigRig = scanner.FindPattern("48 89 5C 24 08 57 48 83 EC 20 48 8B C2 49 8B F8 48 8B D9 45 33 C0 48 8B C8 33 D2 E8 ?? ?? ?? ?? 8B D0 48 8B CB E8 ?? ?? ?? ?? 48 85 C0 74 18 8B");
                        if (sigRig) {
                            auto rigOffs = RipOffsets(sigRig, 100, 0x2000, 0x4000);
                            if (!rigOffs.empty()) {
                                OFFSET_PLAYER_RIG_HANDLE = rigOffs[0];
                                OFFSET_PLAYER_CHARACTER_HANDLE = rigOffs[0] - 0xC;
                            }
                        }

                        uintptr_t sigCrouch = scanner.FindPattern("48 89 5C 24 08 57 48 83 EC 20 48 8B C2 49 8B F8 48 8B D9 45 33 C0 48 8B C8 33 D2 E8 ?? ?? ?? ?? 8B D0 48 8B CB E8 ?? ?? ?? ?? 48 85 C0 74 1A F3 0F 10 88 68");
                        if (sigCrouch) {
                            auto crOffs = RipOffsets(sigCrouch, 100, 0x100, 0x400);
                            if (!crOffs.empty()) OFFSET_PLAYER_IS_CROUCHING = crOffs[0];
                        }

                        uintptr_t healthFn = scanner.FindPattern("48 89 5C 24 08 57 48 83 EC 40 48 8B DA 0F 29 74 24 30 48 8B F9");
                        if (healthFn) {
                            uintptr_t writeInst = scanner.FindPattern("F3 0F 11 80 ? ? 00 00", healthFn, 0x150);
                            if (writeInst) {
                                healthWriteInstructionAddr = moduleBase + writeInst;
                                OFFSET_PLAYER_HEALTH = scanner.ReadInt32(writeInst + 4);
                                originalHealthBytes.resize(8);
                                ReadProcessMemory(hProcess, (LPCVOID)healthWriteInstructionAddr, originalHealthBytes.data(), 8, nullptr);
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
        Vec4 localCamRot{ 0, 0, 0, 1 };
        float readBaseFov = 0.0f;
        uintptr_t foundLocalPlayerAddress = 0;
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

        uintptr_t sceneMgr = 0;
        int32_t gEntityCount = 0;
        uintptr_t gEntityArray = 0;
        int32_t lEntityCount = 0;
        uintptr_t lEntityArray = 0;

        if (ReadProcessMemory(hProcess, (LPCVOID)(globalContext + OFFSET_SCENE_MANAGER), &sceneMgr, sizeof(uintptr_t), nullptr) && sceneMgr) {
            ReadProcessMemory(hProcess, (LPCVOID)(sceneMgr + OFFSET_GLOBAL_ENTITY_COUNT), &gEntityCount, sizeof(int32_t), nullptr);
            ReadProcessMemory(hProcess, (LPCVOID)(sceneMgr + OFFSET_GLOBAL_ENTITY_ARRAY), &gEntityArray, sizeof(uintptr_t), nullptr);
            ReadProcessMemory(hProcess, (LPCVOID)(sceneMgr + OFFSET_LOCAL_ENTITY_COUNT), &lEntityCount, sizeof(int32_t), nullptr);
            ReadProcessMemory(hProcess, (LPCVOID)(sceneMgr + OFFSET_LOCAL_ENTITY_ARRAY), &lEntityArray, sizeof(uintptr_t), nullptr);
        }

        auto ResolveEntity = [&](int32_t handle) -> uintptr_t {
            if (handle == 0 || !sceneMgr) return 0;
            uintptr_t entityPtr = 0;
            if (handle > 0 && handle < gEntityCount && gEntityArray) {
                ReadProcessMemory(hProcess, (LPCVOID)(gEntityArray + (handle * sizeof(uintptr_t))), &entityPtr, sizeof(uintptr_t), nullptr);
            }
            else if (handle < 0 && (-handle) < lEntityCount && lEntityArray) {
                ReadProcessMemory(hProcess, (LPCVOID)(lEntityArray + ((-handle) * sizeof(uintptr_t))), &entityPtr, sizeof(uintptr_t), nullptr);
            }
            return entityPtr;
            };

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

                    bool isLocal = (i == localIndex);

                    Vec3 pPos{};
                    if (ReadProcessMemory(hProcess, (LPCVOID)(pPtr + OFFSET_PLAYER_POS), &pPos, sizeof(Vec3), nullptr)) {
                        if (std::isnan(pPos.x) || (pPos.x == 0.0f && pPos.y == 0.0f && pPos.z == 0.0f))
                            continue;

                        Vec3 pHeadPos{};
                        ReadProcessMemory(hProcess, (LPCVOID)(pPtr + OFFSET_PLAYER_HEAD_POS), &pHeadPos, sizeof(Vec3), nullptr);

                        Vec4 pBodyRot{ 0, 0, 0, 1 };
                        ReadProcessMemory(hProcess, (LPCVOID)(pPtr + OFFSET_PLAYER_ROT), &pBodyRot, sizeof(Vec4), nullptr);
                        if (pBodyRot.x == 0.0f && pBodyRot.y == 0.0f && pBodyRot.z == 0.0f && pBodyRot.w == 0.0f) {
                            pBodyRot.w = 1.0f;
                        }

                        Vec3 pVel{};
                        ReadProcessMemory(hProcess, (LPCVOID)(pPtr + OFFSET_PLAYER_VEL), &pVel, sizeof(Vec3), nullptr);

                        float pHealth = 1.0f;
                        ReadProcessMemory(hProcess, (LPCVOID)(pPtr + OFFSET_PLAYER_HEALTH), &pHealth, sizeof(float), nullptr);

                        uint8_t aimingByte = 0;
                        ReadProcessMemory(hProcess, (LPCVOID)(pPtr + OFFSET_PLAYER_IS_AIMING), &aimingByte, sizeof(uint8_t), nullptr);

                        float crouchFloat = 0.0f;
                        ReadProcessMemory(hProcess, (LPCVOID)(pPtr + OFFSET_PLAYER_IS_CROUCHING), &crouchFloat, sizeof(float), nullptr);

                        std::string activeToolName = "";
                        uintptr_t toolMgr = 0;
                        ReadProcessMemory(hProcess, (LPCVOID)(pPtr + OFFSET_PLAYER_TOOL), &toolMgr, sizeof(uintptr_t), nullptr);
                        if (toolMgr) {
                            uintptr_t activeToolPtr = 0;
                            ReadProcessMemory(hProcess, (LPCVOID)(toolMgr + 0x30), &activeToolPtr, sizeof(uintptr_t), nullptr);
                            if (activeToolPtr) {
                                activeToolName = ReadTeardownString(hProcess, activeToolPtr + 0x18);
                            }
                        }

                        uintptr_t vehicleComp = 0;
                        int32_t vehicleHandle = 0;
                        ReadProcessMemory(hProcess, (LPCVOID)(pPtr + OFFSET_PLAYER_VEHICLE_PTR), &vehicleComp, sizeof(uintptr_t), nullptr);
                        if (vehicleComp) {
                            ReadProcessMemory(hProcess, (LPCVOID)(vehicleComp + 0x0C), &vehicleHandle, sizeof(int32_t), nullptr);
                        }
                        bool isDriving = (vehicleComp != 0 && vehicleHandle != 0);

                        int32_t charHandle = 0;
                        int32_t rigHandle = 0;
                        ReadProcessMemory(hProcess, (LPCVOID)(pPtr + OFFSET_PLAYER_CHARACTER_HANDLE), &charHandle, sizeof(int32_t), nullptr);
                        ReadProcessMemory(hProcess, (LPCVOID)(pPtr + OFFSET_PLAYER_RIG_HANDLE), &rigHandle, sizeof(int32_t), nullptr);

                        uintptr_t charEntityPtr = 0;
                        int32_t activeHandle = (charHandle != 0) ? charHandle : rigHandle;
                        if (activeHandle != 0) {
                            charEntityPtr = ResolveEntity(activeHandle);
                        }

                        PlayerInfo pi;
                        pi.ptr = pPtr;
                        pi.pos = pPos;
                        pi.headPos = pHeadPos;
                        pi.rot = pBodyRot;
                        pi.velocity = pVel;
                        pi.health = pHealth;
                        pi.dist = (pPos - localCamPos).Length();
                        pi.isLocal = isLocal;
                        pi.isCrouching = (crouchFloat > 0.5f);
                        pi.isAiming = (aimingByte != 0);
                        pi.isDriving = isDriving;
                        pi.activeTool = activeToolName;
                        pi.charHandle = charHandle;
                        pi.rigHandle = rigHandle;
                        pi.boneCount = 0;

                        if (charEntityPtr) {
                            if (boneAddressCache.find(charEntityPtr) == boneAddressCache.end()) {
                                std::vector<std::pair<uintptr_t, int>> nodeStack;
                                uintptr_t rootChild = 0;
                                ReadProcessMemory(hProcess, (LPCVOID)(charEntityPtr + OFFSET_NODE_FIRST_CHILD), &rootChild, sizeof(uintptr_t), nullptr);

                                for (uintptr_t curr = rootChild; curr != 0; ) {
                                    nodeStack.push_back({ curr, -1 });
                                    uintptr_t nextSib = 0;
                                    ReadProcessMemory(hProcess, (LPCVOID)(curr + OFFSET_NODE_NEXT_SIBLING), &nextSib, sizeof(uintptr_t), nullptr);
                                    curr = nextSib;
                                }

                                std::vector<std::pair<uintptr_t, int>> newCacheList;

                                while (!nodeStack.empty() && pi.boneCount < 32) {
                                    auto current = nodeStack.back();
                                    nodeStack.pop_back();

                                    uintptr_t node = current.first;
                                    int parentIdx = current.second;
                                    if (!node) continue;

                                    uint16_t typeId = 0;
                                    ReadProcessMemory(hProcess, (LPCVOID)(node + OFFSET_NODE_TYPE), &typeId, sizeof(uint16_t), nullptr);

                                    int currentBoneIdx = parentIdx;

                                    if (typeId == 1 || typeId == 2 || typeId == 9) {
                                        Vec3 nodePos{};
                                        ReadProcessMemory(hProcess, (LPCVOID)(node + OFFSET_NODE_WORLD_POS), &nodePos, sizeof(Vec3), nullptr);

                                        if (!std::isnan(nodePos.x) && (nodePos - pPos).Length() <= 2.5f && nodePos.Length() > 1.0f) {
                                            currentBoneIdx = (int)pi.boneCount;
                                            pi.liveBones[pi.boneCount] = { nodePos, parentIdx };
                                            pi.boneCount++;
                                            newCacheList.push_back({ node, parentIdx });
                                        }
                                    }

                                    uintptr_t firstChild = 0;
                                    ReadProcessMemory(hProcess, (LPCVOID)(node + OFFSET_NODE_FIRST_CHILD), &firstChild, sizeof(uintptr_t), nullptr);
                                    for (uintptr_t child = firstChild; child != 0; ) {
                                        nodeStack.push_back({ child, currentBoneIdx });
                                        uintptr_t nextSib = 0;
                                        ReadProcessMemory(hProcess, (LPCVOID)(child + OFFSET_NODE_NEXT_SIBLING), &nextSib, sizeof(uintptr_t), nullptr);
                                        child = nextSib;
                                    }
                                }
                                boneAddressCache[charEntityPtr] = newCacheList;
                            }
                            else {
                                auto& cachedBones = boneAddressCache[charEntityPtr];
                                for (auto& cachedNode : cachedBones) {
                                    Vec3 nodePos;
                                    ReadProcessMemory(hProcess, (LPCVOID)(cachedNode.first + OFFSET_NODE_WORLD_POS), &nodePos, sizeof(Vec3), nullptr);
                                    if (pi.boneCount < 32) {
                                        pi.liveBones[pi.boneCount] = { nodePos, cachedNode.second };
                                        pi.boneCount++;
                                    }
                                }
                            }
                        }

                        if (pi.isLocal) foundLocalPlayerAddress = pPtr;
                        localPlayers.push_back(std::move(pi));
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(g_sharedData.mtx);
                g_sharedData.attached = true;
                g_sharedData.hProcess = hProcess;
                g_sharedData.globalContext = globalContext;
                g_sharedData.localPlayerPtr = foundLocalPlayerAddress;
                g_sharedData.camPos = localCamPos;
                g_sharedData.camRot = localCamRot;
                g_sharedData.liveFovDeg = readBaseFov;
                g_sharedData.players = std::move(localPlayers);
                g_sharedData.updateTick++;

                if (foundLocalPlayerAddress) {
                    ReadProcessMemory(hProcess, (LPCVOID)(foundLocalPlayerAddress + OFFSET_PLAYER_PITCH), &g_sharedData.dbgLivePlayerPitch, sizeof(float), nullptr);
                    ReadProcessMemory(hProcess, (LPCVOID)(foundLocalPlayerAddress + OFFSET_PLAYER_YAW), &g_sharedData.dbgLivePlayerYaw, sizeof(float), nullptr);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
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
    LoadConfig();

    HINSTANCE hInstance = GetModuleHandle(nullptr);

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"TeardownOverlay", nullptr };
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    g_hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"Teardown Overlay",
        WS_POPUP, 0, 0, screenW, screenH,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    SetLayeredWindowAttributes(g_hWnd, RGB(0, 0, 0), 255, LWA_ALPHA);
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(g_hWnd, &margins);

    if (!CreateDeviceD3D(g_hWnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(g_hWnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hWnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

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

    std::thread memoryWorker(MemoryWorkerThread);
    std::thread aimbotWorker(AimbotWorkerThread);

    bool menuOpen = true;
    bool wasInsertPressed = false;
    bool attached = false;

    Vec3 camPos{};
    Vec4 camRot{};
    float liveFovDeg = 0.0f;
    std::vector<PlayerInfo> snapshotPlayers;

    MSG msg{};
    while (g_workerRunning) {
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) g_workerRunning = false;
        }
        if (!g_workerRunning) break;

        bool isInsertPressed = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (isInsertPressed && !wasInsertPressed) {
            menuOpen = !menuOpen;
            LONG_PTR exStyle = GetWindowLongPtr(g_hWnd, GWL_EXSTYLE);
            if (menuOpen) SetWindowLongPtr(g_hWnd, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT);
            else SetWindowLongPtr(g_hWnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);
        }
        wasInsertPressed = isInsertPressed;

        {
            std::lock_guard<std::mutex> lock(g_sharedData.mtx);
            attached = g_sharedData.attached;
            snapshotPlayers = g_sharedData.players;
            camPos = g_sharedData.camPos;
            camRot = g_sharedData.camRot;
            liveFovDeg = (g_sharedData.liveFovDeg >= 40.0f) ? g_sharedData.liveFovDeg : 90.0f;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();

        float midX = screenW / 2.0f;
        float midY = screenH / 2.0f;

        for (const auto& player : snapshotPlayers) {
            if (player.isLocal) {
                if ((player.activeTool.find("rifle") != std::string::npos || player.activeTool.find("hunting") != std::string::npos) &&
                    (GetAsyncKeyState(VK_RBUTTON) & 0x8000)) {
                    liveFovDeg /= 4.0f;
                }
                break;
            }
        }

        if (g_drawAimFov && g_aimbotEnabled) {
            drawList->AddCircle({ midX, midY }, g_aimFovRadius, IM_COL32(255, 255, 255, 120), 64, 1.5f);
        }

        for (const auto& player : snapshotPlayers) {
            if (player.isLocal) continue;

            Vec2 screenPos{};
            float dist = 0.0f;

            if (WorldToScreen(player.pos + Vec3{ 0.0f, 0.9f, 0.0f }, camPos, camRot, (float)screenW, (float)screenH, liveFovDeg, screenPos, dist)) {
                if (g_tracers) {
                    drawList->AddLine({ midX, (float)screenH }, { screenPos.x, screenPos.y }, IM_COL32(0, 255, 120, 200), 1.5f);
                }

                if (g_skeletonEsp) {
                    DrawSkeleton(drawList, player, camPos, camRot, (float)screenW, (float)screenH, liveFovDeg);
                }

                if (g_espBox) {
                    Vec2 topScreen{}, botScreen{};
                    float d1 = 0.0f, d2 = 0.0f;
                    if (WorldToScreen(player.pos + Vec3{ 0.0f, 1.8f, 0.0f }, camPos, camRot, (float)screenW, (float)screenH, liveFovDeg, topScreen, d1) &&
                        WorldToScreen(player.pos, camPos, camRot, (float)screenW, (float)screenH, liveFovDeg, botScreen, d2)) {

                        float boxH = std::abs(botScreen.y - topScreen.y);
                        float boxW = boxH * 0.45f;
                        ImU32 col = IM_COL32(0, 255, 200, 255);

                        drawList->AddRect(
                            { topScreen.x - (boxW / 2.0f), topScreen.y },
                            { topScreen.x + (boxW / 2.0f), botScreen.y },
                            col, 0.0f, 0, 2.0f
                        );

                        float barW = 4.0f;
                        float barH = boxH * std::clamp(player.health, 0.0f, 1.0f);
                        float barX = topScreen.x - (boxW / 2.0f) - 7.0f;
                        drawList->AddRectFilled({ barX, botScreen.y - boxH }, { barX + barW, botScreen.y }, IM_COL32(40, 40, 40, 200));
                        drawList->AddRectFilled({ barX, botScreen.y - barH }, { barX + barW, botScreen.y }, IM_COL32(0, 255, 100, 255));
                    }
                }
            }
        }

        if (menuOpen) {
            ImGui::SetNextWindowSize({ 540, 580 }, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("DENJI900's Teardown Menu [INSERT to Hide]", nullptr, ImGuiWindowFlags_NoCollapse)) {
                ImGui::TextColored(attached ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1),
                    attached ? "[V] Attached to Teardown.exe" : "[-] Searching for Teardown.exe...");

                ImGui::SameLine(ImGui::GetWindowWidth() - 120);
                if (ImGui::Button("Save Settings")) {
                    SaveConfig();
                }

                ImGui::Separator();

                if (ImGui::CollapsingHeader("AIMBOT SETTINGS", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enable Aimbot", &g_aimbotEnabled);

                    if (g_waitingForKey) {
                        ImGui::Button("[ Press Any Key... ]", ImVec2(200, 0));

                        static bool keysReleased = false;
                        if (!keysReleased) {
                            keysReleased = true;
                            for (int k = 1; k < 256; ++k) {
                                if (GetAsyncKeyState(k) & 0x8000) {
                                    keysReleased = false;
                                }
                            }
                        }
                        else {
                            for (int k = 1; k < 256; ++k) {
                                if (k == VK_INSERT || k == VK_ESCAPE) continue;
                                if (GetAsyncKeyState(k) & 0x8000) {
                                    g_aimKey = k;
                                    g_waitingForKey = false;
                                    keysReleased = false;
                                    SaveConfig();
                                    break;
                                }
                            }
                        }
                    }
                    else {
                        char btnText[64];
                        snprintf(btnText, sizeof(btnText), "Bind Aim Key [ 0x%X ]", g_aimKey);
                        if (ImGui::Button(btnText, ImVec2(200, 0))) {
                            g_waitingForKey = true;
                        }
                    }

                    ImGui::Checkbox("Draw FOV Circle", &g_drawAimFov);
                    ImGui::SliderFloat("FOV Radius", &g_aimFovRadius, 30.0f, 500.0f, "%.0f px");
                }

                if (ImGui::CollapsingHeader("VISUAL OVERLAYS", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Box ESP", &g_espBox);
                    ImGui::Checkbox("Skeleton ESP", &g_skeletonEsp);
                    ImGui::Checkbox("Show Joint ID Badges (#0, #1...)", &g_showJointBadges);
                    ImGui::Checkbox("Tracers", &g_tracers);
                }
            }
            ImGui::End();
        }

        ImGui::Render();
        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(0, 0);
    }

    g_workerRunning = false;
    memoryWorker.join();
    aimbotWorker.join();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(g_hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}
