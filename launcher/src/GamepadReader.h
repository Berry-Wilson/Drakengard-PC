#pragma once

#include "runtime/input_bindings.h"

#include <string>
#include <vector>

// Reads controllers through GLFW, the library the game uses, so its gamepad mappings and button
// numbering match the game's. GLFW is used without a window: Qt owns the event loop, and
// glfwPollEvents() would steal Qt's thread messages, so newly connected controllers are found by
// rescan(), which restarts GLFW.
class GamepadReader
{
public:
    GamepadReader();
    ~GamepadReader();
    GamepadReader(const GamepadReader &) = delete;
    GamepadReader &operator=(const GamepadReader &) = delete;

    bool ready() const { return m_ready; }
    void rescan();
    std::vector<std::string> names() const;
    std::vector<ps2x::input::GamepadState> read() const;

private:
    bool m_ready = false;
};
