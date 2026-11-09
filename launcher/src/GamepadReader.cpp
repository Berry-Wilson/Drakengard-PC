#include "GamepadReader.h"

#include <GLFW/glfw3.h>

GamepadReader::GamepadReader()
{
    m_ready = glfwInit() == GLFW_TRUE;
}

GamepadReader::~GamepadReader()
{
    if (m_ready)
        glfwTerminate();
}

void GamepadReader::rescan()
{
    if (m_ready)
        glfwTerminate();
    m_ready = glfwInit() == GLFW_TRUE;
}

std::vector<std::string> GamepadReader::names() const
{
    std::vector<std::string> out;
    if (!m_ready)
        return out;
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid)
    {
        if (glfwJoystickIsGamepad(jid))
        {
            const char *name = glfwGetGamepadName(jid);
            out.emplace_back(name ? name : "Controller");
        }
    }
    return out;
}

std::vector<ps2x::input::GamepadState> GamepadReader::read() const
{
    std::vector<ps2x::input::GamepadState> out;
    if (!m_ready)
        return out;
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid)
    {
        GLFWgamepadstate state;
        if (!glfwJoystickIsGamepad(jid) || !glfwGetGamepadState(jid, &state))
            continue;
        ps2x::input::GamepadState pad;
        // ps2x::input numbers buttons and axes in GLFW's order.
        for (int b = 0; b < ps2x::input::kGamepadButtonCount; ++b)
            pad.buttons[b] = state.buttons[b] == GLFW_PRESS;
        for (int a = 0; a < ps2x::input::kGamepadAxisCount; ++a)
            pad.axes[a] = state.axes[a];
        out.push_back(pad);
    }
    return out;
}
