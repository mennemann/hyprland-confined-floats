#define WLR_USE_UNSTABLE

#include <algorithm>
#include <hyprland/src/config/lua/bindings/LuaBindingsInternal.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleEffectContainer.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/layout/target/WindowTarget.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include "globals.hpp"

inline CFunctionHook* g_pSetPositionGlobalHook = nullptr;

typedef void (*origSetPositionGlobal)(void*, const Layout::STargetBox&);

static void callOriginal(void* thisptr, const Layout::STargetBox& box) {
    (*(origSetPositionGlobal)g_pSetPositionGlobalHook->m_original)(thisptr, box);
}

static void hkSetPositionGlobal(void* thisptr, const Layout::STargetBox& box) {
    const auto TARGET = rc<Layout::CWindowTarget*>(thisptr);

    if (!TARGET->floating())
        return callOriginal(thisptr, box);

    const auto WINDOW = TARGET->window();
    if (!WINDOW || !WINDOW->m_ruleApplicator)
        return callOriginal(thisptr, box);

    if (!WINDOW->m_ruleApplicator->m_otherProps.props.contains(config.confineRuleIdx))
        return callOriginal(thisptr, box);

    const auto MONITOR = WINDOW->m_monitor.lock();
    if (!MONITOR)
        return callOriginal(thisptr, box);

    const CBox WORKAREA = MONITOR->logicalBoxMinusReserved();
    Layout::STargetBox clamped = box;

    if (box.logicalBox.w >= WORKAREA.w)
        clamped.logicalBox.x = WORKAREA.x;
    else
        clamped.logicalBox.x = std::clamp(box.logicalBox.x, WORKAREA.x, WORKAREA.x + WORKAREA.w - box.logicalBox.w);

    if (box.logicalBox.h >= WORKAREA.h)
        clamped.logicalBox.y = WORKAREA.y;
    else
        clamped.logicalBox.y = std::clamp(box.logicalBox.y, WORKAREA.y, WORKAREA.y + WORKAREA.h - box.logicalBox.h);

    callOriginal(thisptr, clamped);
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[confined-floats] Version mismatch (headers vs running hyprland)",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[confined-floats] Version mismatch");
    }

    config.confineRuleIdx = Desktop::Rule::windowEffects()->registerEffect("confined-floats:confine");

    HyprlandAPI::addLuaFunction(PHANDLE, "confined_floats", "is_loaded",
                                [](lua_State* L) -> int { lua_pushboolean(L, 1); return 1; });

    static const auto METHODS = HyprlandAPI::findFunctionsByName(PHANDLE, "setPositionGlobal");
    bool found = false;
    for (auto& fn : METHODS) {
        if (fn.demangled.contains("CWindowTarget") && !fn.demangled.contains("CWindowGroupTarget")) {
            g_pSetPositionGlobalHook = HyprlandAPI::createFunctionHook(PHANDLE, fn.address, (void*)&hkSetPositionGlobal);
            if (g_pSetPositionGlobalHook)
                found = g_pSetPositionGlobalHook->hook();
            break;
        }
    }

    if (!found) {
        HyprlandAPI::addNotification(PHANDLE, "[confined-floats] Could not find/hook CWindowTarget::setPositionGlobal",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[confined-floats] Could not find/hook CWindowTarget::setPositionGlobal");
    }

    HyprlandAPI::addNotification(PHANDLE, "[confined-floats] Initialized successfully!", CHyprColor{0.2, 1.0, 0.2, 1.0}, 5000);

    return {"confined-floats", "Prevent floating windows from moving off-screen", "Marcin Mennemann", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (g_pSetPositionGlobalHook)
        g_pSetPositionGlobalHook->unhook();

    Desktop::Rule::windowEffects()->unregisterEffect(config.confineRuleIdx);
}
