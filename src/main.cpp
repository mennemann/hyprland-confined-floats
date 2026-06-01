#define WLR_USE_UNSTABLE

#include <algorithm>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/lua/bindings/LuaBindingsInternal.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleEffectContainer.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/layout/target/WindowTarget.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <limits>
#include <set>

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

static bool workAreaDirty = true;

static void recomputeWorkArea() {
    workArea.gaps.clear();
    workArea.bounds = CBox{};

    std::vector<CBox> monitors;

    for (auto& m : g_pCompositor->m_monitors) {
        if (!m->m_output)
            continue;

        CBox WA = m->logicalBoxMinusReserved();
        if (WA.w <= 0 || WA.h <= 0)
            continue;

        double rx = std::round(WA.x + WA.w);
        double by = std::round(WA.y + WA.h);
        WA.x = std::round(WA.x);
        WA.y = std::round(WA.y);
        WA.w = rx - WA.x;
        WA.h = by - WA.y;

        monitors.push_back(WA);

        if (workArea.bounds.w == 0 && workArea.bounds.h == 0)
            workArea.bounds = WA;
        else {
            rx = std::max(workArea.bounds.x + workArea.bounds.w, WA.x + WA.w);
            by = std::max(workArea.bounds.y + workArea.bounds.h, WA.y + WA.h);
            workArea.bounds.x = std::min(workArea.bounds.x, WA.x);
            workArea.bounds.y = std::min(workArea.bounds.y, WA.y);
            workArea.bounds.w = rx - workArea.bounds.x;
            workArea.bounds.h = by - workArea.bounds.y;
        }
    }

    if (monitors.empty())
        return;

    std::set<double> xs;
    for (auto& wa : monitors) {
        xs.insert(wa.x);
        xs.insert(wa.x + wa.w);
    }

    if (xs.size() < 2)
        return;

    const double BIGY = workArea.bounds.y;
    const double BIGH = workArea.bounds.h;

    struct YRange {
        double y0, y1;
    };
    std::vector<YRange> covered;

    auto xit = xs.begin();
    for (auto next = std::next(xit); next != xs.end(); ++xit, ++next) {
        double x0 = *xit, x1 = *next;

        covered.clear();

        for (auto& wa : monitors) {
            if (wa.x <= x0 && x1 <= wa.x + wa.w)
                covered.push_back({wa.y, wa.y + wa.h});
        }

        std::sort(covered.begin(), covered.end(), [](auto& a, auto& b) { return a.y0 < b.y0; });

        double cursor = BIGY;
        for (size_t i = 0; i < covered.size(); ++i) {
            double cy0 = covered[i].y0;
            double cy1 = covered[i].y1;

            for (auto j = i + 1; j < covered.size() && covered[j].y0 <= cy1; ++j) {
                cy1 = std::max(cy1, covered[j].y1);
                i = j;
            }

            if (cursor < cy0) {
                CBox gap;
                gap.x = x0;
                gap.y = cursor;
                gap.w = x1 - x0;
                gap.h = cy0 - cursor;
                workArea.gaps.push_back(gap);
            }

            cursor = std::max(cursor, cy1);
        }

        if (cursor < BIGY + BIGH) {
            CBox gap;
            gap.x = x0;
            gap.y = cursor;
            gap.w = x1 - x0;
            gap.h = BIGY + BIGH - cursor;
            workArea.gaps.push_back(gap);
        }
    }

    workAreaDirty = false;
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

    if (workAreaDirty)
        recomputeWorkArea();

    if (workArea.bounds.w <= 0 || workArea.bounds.h <= 0)
        return callOriginal(thisptr, box);

    const auto MON = WINDOW->m_monitor.lock();

    Layout::STargetBox clamped = box;

    if (MON) {
        CBox WA = MON->logicalBoxMinusReserved();
        double maxW = WA.w * 0.99, maxH = WA.h * 0.99;
        if (clamped.logicalBox.w > maxW || clamped.logicalBox.h > maxH) {
            clamped.logicalBox.x += (clamped.logicalBox.w - std::min(clamped.logicalBox.w, maxW)) / 2.0;
            clamped.logicalBox.y += (clamped.logicalBox.h - std::min(clamped.logicalBox.h, maxH)) / 2.0;
            clamped.logicalBox.w = std::min(clamped.logicalBox.w, maxW);
            clamped.logicalBox.h = std::min(clamped.logicalBox.h, maxH);
        }
    }

    const double DESX = clamped.logicalBox.x;
    const double DESY = clamped.logicalBox.y;
    const double WINW = clamped.logicalBox.w;
    const double WINH = clamped.logicalBox.h;

    if (WINW >= workArea.bounds.w)
        clamped.logicalBox.x = workArea.bounds.x;
    else
        clamped.logicalBox.x = std::clamp(DESX, workArea.bounds.x, workArea.bounds.x + workArea.bounds.w - WINW);

    if (WINH >= workArea.bounds.h)
        clamped.logicalBox.y = workArea.bounds.y;
    else
        clamped.logicalBox.y = std::clamp(DESY, workArea.bounds.y, workArea.bounds.y + workArea.bounds.h - WINH);

    for (auto& gap : workArea.gaps) {
        CBox overlap = clamped.logicalBox.intersection(gap);
        if (overlap.w <= 0 || overlap.h <= 0)
            continue;

        double gx = gap.x, gy = gap.y, gw = gap.w, gh = gap.h;
        double bx = workArea.bounds.x, by = workArea.bounds.y;
        double bw = workArea.bounds.w, bh = workArea.bounds.h;

        double bestDist = std::numeric_limits<double>::max();
        double newX = clamped.logicalBox.x, newY = clamped.logicalBox.y;

        double nx = gx + gw;
        if (nx + WINW <= bx + bw + 1.0) {
            double d = nx - clamped.logicalBox.x;
            if (d < bestDist) {
                bestDist = d;
                newX = nx;
                newY = clamped.logicalBox.y;
            }
        }

        nx = gx - WINW;
        if (nx >= bx - 1.0) {
            double d = clamped.logicalBox.x - nx;
            if (d < bestDist) {
                bestDist = d;
                newX = nx;
                newY = clamped.logicalBox.y;
            }
        }

        double ny = gy + gh;
        if (ny + WINH <= by + bh + 1.0) {
            double d = ny - clamped.logicalBox.y;
            if (d < bestDist) {
                bestDist = d;
                newX = clamped.logicalBox.x;
                newY = ny;
            }
        }

        ny = gy - WINH;
        if (ny >= by - 1.0) {
            double d = clamped.logicalBox.y - ny;
            if (d < bestDist) {
                bestDist = d;
                newX = clamped.logicalBox.x;
                newY = ny;
            }
        }

        if (bestDist < std::numeric_limits<double>::max()) {
            clamped.logicalBox.x = newX;
            clamped.logicalBox.y = newY;
        }
    }

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

    static auto P1 = Event::bus()->m_events.monitor.added.listen([](PHLMONITOR) { workAreaDirty = true; });
    static auto P2 = Event::bus()->m_events.monitor.removed.listen([](PHLMONITOR) { workAreaDirty = true; });
    static auto P3 = Event::bus()->m_events.monitor.layoutChanged.listen([] { workAreaDirty = true; });
    static auto P4 = Event::bus()->m_events.config.reloaded.listen([] { workAreaDirty = true; });
    static auto P5 = Event::bus()->m_events.layer.opened.listen([](PHLLS) { workAreaDirty = true; });
    static auto P6 = Event::bus()->m_events.layer.closed.listen([](PHLLS) { workAreaDirty = true; });
    static auto P7 = Event::bus()->m_events.layer.updateRules.listen([](PHLLS) { workAreaDirty = true; });

    HyprlandAPI::addNotification(PHANDLE, "[confined-floats] Initialized successfully!", CHyprColor{0.2, 1.0, 0.2, 1.0}, 5000);

    return {"confined-floats", "Prevent floating windows from moving off-screen", "Marcin Mennemann", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (g_pSetPositionGlobalHook)
        g_pSetPositionGlobalHook->unhook();

    Desktop::Rule::windowEffects()->unregisterEffect(config.confineRuleIdx);
}
