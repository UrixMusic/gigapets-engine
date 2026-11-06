// input_settings.hpp - the controller and mouse settings (Settings) put into the input bridge (engine/input/input_bridge.hpp).
#pragma once
#include <algorithm>
#include "settings.hpp"
#include "../../engine/input/input_bridge.hpp"

namespace gp
{
    inline MouseRole mouseRoleOf(int v) { return MouseRole(std::clamp(v, 0, int(MouseRole::kCount) - 1)); }

    inline void applyInputSettings(const Settings &s)
    {
        InputBridge &b = inputBridge();
        b.controllerOn = s.controller;
        b.mouseOn = s.mouse;
        b.role[0] = mouseRoleOf(s.mouseLeft);
        b.role[1] = mouseRoleOf(s.mouseMiddle);
        b.role[2] = mouseRoleOf(s.mouseRight);
        b.wheelOn = s.mouseWheel;
    }
} // namespace gp
