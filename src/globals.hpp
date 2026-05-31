#pragma once

#include <hyprland/src/desktop/rule/windowRule/WindowRuleEffectContainer.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

inline HANDLE PHANDLE = nullptr;

struct SConfig {
    Desktop::Rule::CWindowRuleEffectContainer::storageType confineRuleIdx = 0;
};

inline SConfig config;
