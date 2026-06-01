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

#include <sstream>
#include <string>
#include <vector>

#include "globals.hpp"

struct Offsets {
    double top = 0, right = 0, bottom = 0, left = 0;
    bool topPct = false, rightPct = false, bottomPct = false, leftPct = false;
};

static Offsets parseOffsets(const std::string& raw) {
    Offsets o;
    if (raw.empty())
        return o;

    std::vector<std::string> parts;
    std::istringstream ss(raw);
    std::string tok;
    while (ss >> tok)
        parts.push_back(tok);

    if (parts.empty())
        return o;

    auto parse = [](const std::string& s, double& val, bool& pct) {
        if (s.ends_with('%')) {
            pct = true;
            val = std::stod(s.substr(0, s.size() - 1));
        } else {
            pct = false;
            val = std::stod(s);
        }
    };

    switch (parts.size()) {
        case 1:
            parse(parts[0], o.top, o.topPct);
            o.right = o.bottom = o.left = o.top;
            o.rightPct = o.bottomPct = o.leftPct = o.topPct;
            break;
        case 2:
            parse(parts[0], o.top, o.topPct);
            o.bottom = o.top;
            o.bottomPct = o.topPct;
            parse(parts[1], o.right, o.rightPct);
            o.left = o.right;
            o.leftPct = o.rightPct;
            break;
        case 3:
            parse(parts[0], o.top, o.topPct);
            parse(parts[1], o.right, o.rightPct);
            o.left = o.right;
            o.leftPct = o.rightPct;
            parse(parts[2], o.bottom, o.bottomPct);
            break;
        case 4:
            parse(parts[0], o.top, o.topPct);
            parse(parts[1], o.right, o.rightPct);
            parse(parts[2], o.bottom, o.bottomPct);
            parse(parts[3], o.left, o.leftPct);
            break;
    }
    return o;
}

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

    Offsets off;
    auto it = WINDOW->m_ruleApplicator->m_otherProps.props.find(config.confineRuleIdx);
    if (it != WINDOW->m_ruleApplicator->m_otherProps.props.end() && !it->second->effect.empty()) {
        try {
            off = parseOffsets(it->second->effect);
        } catch (...) {
        }
    }

    if (workAreaDirty)
        recomputeWorkArea();

    if (workArea.bounds.w <= 0 || workArea.bounds.h <= 0)
        return callOriginal(thisptr, box);

    Layout::STargetBox clamped = box;

    const double WINW = clamped.logicalBox.w;
    const double WINH = clamped.logicalBox.h;

    double lo = off.leftPct ? WINW * off.left / 100.0 : off.left;
    double ro = off.rightPct ? WINW * off.right / 100.0 : off.right;
    double to = off.topPct ? WINH * off.top / 100.0 : off.top;
    double bo = off.bottomPct ? WINH * off.bottom / 100.0 : off.bottom;

    CBox pseudo;
    pseudo.x = clamped.logicalBox.x - lo;
    pseudo.y = clamped.logicalBox.y - to;
    pseudo.w = clamped.logicalBox.w + lo + ro;
    pseudo.h = clamped.logicalBox.h + to + bo;

    const auto MON = WINDOW->m_monitor.lock();
    if (MON) {
        CBox WA = MON->logicalBoxMinusReserved();
        double maxW = WA.w * 0.99, maxH = WA.h * 0.99;
        if (pseudo.w > maxW || pseudo.h > maxH) {
            double oldW = pseudo.w, oldH = pseudo.h;
            pseudo.w = std::min(pseudo.w, maxW);
            pseudo.h = std::min(pseudo.h, maxH);
            pseudo.x += (oldW - pseudo.w) / 2.0;
            pseudo.y += (oldH - pseudo.h) / 2.0;
        }
    }

    if (pseudo.w >= workArea.bounds.w)
        pseudo.x = workArea.bounds.x;
    else
        pseudo.x = std::clamp(pseudo.x, workArea.bounds.x, workArea.bounds.x + workArea.bounds.w - pseudo.w);

    if (pseudo.h >= workArea.bounds.h)
        pseudo.y = workArea.bounds.y;
    else
        pseudo.y = std::clamp(pseudo.y, workArea.bounds.y, workArea.bounds.y + workArea.bounds.h - pseudo.h);

    for (auto& gap : workArea.gaps) {
        CBox overlap = pseudo.intersection(gap);
        if (overlap.w <= 0 || overlap.h <= 0)
            continue;

        double gx = gap.x, gy = gap.y, gw = gap.w, gh = gap.h;
        double bx = workArea.bounds.x, by = workArea.bounds.y;
        double bw = workArea.bounds.w, bh = workArea.bounds.h;

        double bestDist = std::numeric_limits<double>::max();
        double newX = pseudo.x, newY = pseudo.y;

        double nx = gx + gw;
        if (nx + pseudo.w <= bx + bw + 1.0) {
            double d = nx - pseudo.x;
            if (std::abs(d) < bestDist) {
                bestDist = std::abs(d);
                newX = nx;
                newY = pseudo.y;
            }
        }

        nx = gx - pseudo.w;
        if (nx >= bx - 1.0) {
            double d = pseudo.x - nx;
            if (std::abs(d) < bestDist) {
                bestDist = std::abs(d);
                newX = nx;
                newY = pseudo.y;
            }
        }

        double ny = gy + gh;
        if (ny + pseudo.h <= by + bh + 1.0) {
            double d = ny - pseudo.y;
            if (std::abs(d) < bestDist) {
                bestDist = std::abs(d);
                newX = pseudo.x;
                newY = ny;
            }
        }

        ny = gy - pseudo.h;
        if (ny >= by - 1.0) {
            double d = pseudo.y - ny;
            if (std::abs(d) < bestDist) {
                bestDist = std::abs(d);
                newX = pseudo.x;
                newY = ny;
            }
        }

        if (bestDist < std::numeric_limits<double>::max()) {
            pseudo.x = newX;
            pseudo.y = newY;
        }
    }

    clamped.logicalBox.x = pseudo.x + lo;
    clamped.logicalBox.y = pseudo.y + to;
    clamped.logicalBox.w = pseudo.w - lo - ro;
    clamped.logicalBox.h = pseudo.h - to - bo;

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
