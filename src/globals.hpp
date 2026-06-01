#pragma once

#include <hyprland/src/desktop/rule/windowRule/WindowRuleEffectContainer.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprutils/math/Box.hpp>
#include <vector>

inline HANDLE PHANDLE = nullptr;

struct SConfig {
    Desktop::Rule::CWindowRuleEffectContainer::storageType confineRuleIdx = 0;
};

inline SConfig config;

struct WorkAreaData {
    CBox bounds;
    std::vector<CBox> gaps;
};

inline WorkAreaData workArea;
