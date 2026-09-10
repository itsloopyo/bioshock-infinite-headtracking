#include "game_crosshair.h"

#include "aim_projection.h"
#include "build_profile.h"
#include "logging.h"
#include "hud_projection.h"
#include "minhook_util.h"
#include "ue3_object.h"

#include <windows.h>

#include <cmath>
#include <cstdint>

namespace BioShockInfiniteHeadTracking {
namespace {

using HudTick = void(__fastcall*)(void*, void*, float);
using FindWidget = void*(__thiscall*)(void*, const void*);
using GetPosition = int(__thiscall*)(void*, float*, float*);
using SetPosition = void(__thiscall*)(void*, float, float);
using ResolveDisplayCharacter = void*(__thiscall*)(void*, void*);
using GetWorldMatrix3D = void(__thiscall*)(void*, float*);
using GetMatrix3D = const float*(__thiscall*)(void*, bool);

HudTick g_original = nullptr;
FindWidget g_findWidget = nullptr;
GetPosition g_getPosition = nullptr;
SetPosition g_setPosition = nullptr;
ResolveDisplayCharacter g_resolveCharacter = nullptr;
GetWorldMatrix3D g_getWorldMatrix = nullptr;
GetMatrix3D g_getViewMatrix = nullptr;
GetMatrix3D g_getProjectionMatrix = nullptr;
std::uintptr_t g_widgetVtable = 0;
void* g_hud = nullptr;
void* g_movedWidget = nullptr;
std::uint32_t g_movedHandle = 0;
float g_cleanX = 0.0f;
float g_cleanY = 0.0f;

bool IsCrosshair(void* widget, std::uint32_t* handle) {
    std::uint32_t fields[13];
    if (!ue3::CopyBytes(fields, widget, sizeof(fields))) {
        return false;
    }
    if (fields[0] != g_widgetVtable || (fields[11] & 0x8f) != 8) {
        return false;
    }
    *handle = fields[12];
    return true;
}

void __fastcall TickDetour(void* hud, void* edx, float deltaTime) {
    // Restore before the HUD updates so its animations never read our previous offset.
    RestoreGameCrosshair();
    g_original(hud, edx, deltaTime);
    g_hud = hud;
}

}  // namespace

void RestoreGameCrosshair() {
    g_hud = nullptr;
    if (!g_movedWidget) {
        return;
    }
    std::uint32_t handle = 0;
    if (IsCrosshair(g_movedWidget, &handle) && handle == g_movedHandle) {
        g_setPosition(g_movedWidget, g_cleanX, g_cleanY);
    }
    g_movedWidget = nullptr;
}

void PositionGameCrosshair() {
    void* hud = g_hud;
    RestoreGameCrosshair();
    if (!hud || !ue3::IsObject(hud)) {
        return;
    }
    float ndcX = 0.0f;
    float ndcY = 0.0f;
    const AimProjection result = ProjectAim(&ndcX, &ndcY);
    if (result == AimProjection::Inactive || result == AimProjection::NoProjection) {
        return;
    }
    const auto* binding = static_cast<const std::uint8_t*>(hud) +
                          ActiveProfile().crosshair.offCrosshairBinding;
    void* widget = g_findWidget(hud, binding);
    std::uint32_t handle = 0;
    if (!widget || !IsCrosshair(widget, &handle)) {
        static bool reported = false;
        if (!reported) {
            Log::Line("[crosshair] HUD binding has no active stock crosshair display object.");
            reported = true;
        }
        return;
    }

    const auto* fields = static_cast<const std::uintptr_t*>(widget);
    void* movie = *reinterpret_cast<void**>(fields[10]);
    void* character = g_resolveCharacter(reinterpret_cast<void*>(handle), movie);
    if (!character) {
        return;
    }
    void* parent = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(character) + 0x20);
    const float* view = g_getViewMatrix(character, true);
    const float* projection = g_getProjectionMatrix(character, true);
    if (!parent || !view || !projection) {
        Log::Line("ERROR: stock crosshair has no HUD transform or projection.");
        return;
    }
    float worldMatrix[16];
    float parentMatrix[16];
    g_getWorldMatrix(character, worldMatrix);
    g_getWorldMatrix(parent, parentMatrix);
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    // Preserve the reticle's native depth, size and animation while moving its origin.
    if (!g_getPosition(widget, &g_cleanX, &g_cleanY) ||
        !std::isfinite(g_cleanX) || !std::isfinite(g_cleanY) ||
        !ProjectHudOffset(worldMatrix, parentMatrix, view, projection,
                          result == AimProjection::Ok ? ndcX : 4.0f,
                          result == AimProjection::Ok ? ndcY : 4.0f,
                          &offsetX, &offsetY)) {
        Log::Line("ERROR: stock crosshair position or HUD projection is invalid.");
        return;
    }
    const float x = g_cleanX + offsetX;
    const float y = g_cleanY + offsetY;
    g_setPosition(widget, x, y);
    g_movedWidget = widget;
    g_movedHandle = handle;

    static bool reported = false;
    if (!reported) {
        float actualX = 0.0f;
        float actualY = 0.0f;
        g_getPosition(widget, &actualX, &actualY);
        Log::Line("[crosshair] Stock widget %p: clean (%.2f, %.2f), "
                  "NDC (%.4f, %.4f), requested (%.2f, %.2f), read back (%.2f, %.2f).",
                  widget, g_cleanX, g_cleanY, ndcX, ndcY, x, y, actualX, actualY);
        reported = true;
    }
}

bool InstallGameCrosshairHook() {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("BioShockInfinite.exe"));
    const auto& layout = ActiveProfile().crosshair;
    g_findWidget = reinterpret_cast<FindWidget>(base + layout.rvaFindWidget);
    g_getPosition = reinterpret_cast<GetPosition>(base + layout.rvaGetPosition);
    g_setPosition = reinterpret_cast<SetPosition>(base + layout.rvaSetPosition);
    g_resolveCharacter = reinterpret_cast<ResolveDisplayCharacter>(base + layout.rvaResolveDisplayCharacter);
    g_getWorldMatrix = reinterpret_cast<GetWorldMatrix3D>(base + layout.rvaGetWorldMatrix3D);
    g_getViewMatrix = reinterpret_cast<GetMatrix3D>(base + layout.rvaGetViewMatrix3D);
    g_getProjectionMatrix = reinterpret_cast<GetMatrix3D>(base + layout.rvaGetProjectionMatrix3D);
    g_widgetVtable = base + layout.rvaWidgetVtable;
    const auto target = reinterpret_cast<void*>(base + layout.rvaHudTick);
    const MH_STATUS status = CreateAndEnableHook(target, reinterpret_cast<void*>(&TickDetour),
                                                 reinterpret_cast<void**>(&g_original));
    if (status != MH_OK) {
        Log::Line("ERROR: stock crosshair HUD hook at %p failed: %d", target, status);
        return false;
    }
    Log::Line("[crosshair] Stock crosshair HUD hook installed at %p.", target);
    return true;
}

}  // namespace BioShockInfiniteHeadTracking
