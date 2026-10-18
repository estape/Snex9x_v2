#include "InputController.hpp"
#include <windows.h>
#include <iostream>
#include <cmath>

using json = nlohmann::json;

namespace snex9x::controller {

static const std::vector<std::pair<std::string, model::SnesJoypadButton>> kSnesButtonMap = {
    {"Up", model::SNES_BTN_UP},
    {"Down", model::SNES_BTN_DOWN},
    {"Left", model::SNES_BTN_LEFT},
    {"Right", model::SNES_BTN_RIGHT},
    {"A", model::SNES_BTN_A},
    {"B", model::SNES_BTN_B},
    {"X", model::SNES_BTN_X},
    {"Y", model::SNES_BTN_Y},
    {"L", model::SNES_BTN_L},
    {"R", model::SNES_BTN_R},
    {"Start", model::SNES_BTN_START},
    {"Select", model::SNES_BTN_SELECT}
};

static int WebKeyCodeToVirtualKey(const std::string& code) {
    if (code == "ArrowUp") return VK_UP;
    if (code == "ArrowDown") return VK_DOWN;
    if (code == "ArrowLeft") return VK_LEFT;
    if (code == "ArrowRight") return VK_RIGHT;
    if (code == "Enter" || code == "NumpadEnter") return VK_RETURN;
    if (code == "ShiftRight") return VK_RSHIFT;
    if (code == "ShiftLeft") return VK_LSHIFT;
    if (code == "Space") return VK_SPACE;
    if (code == "Tab") return VK_TAB;
    if (code == "Backspace") return VK_BACK;
    if (code == "ControlLeft") return VK_LCONTROL;
    if (code == "ControlRight") return VK_RCONTROL;
    if (code == "AltLeft") return VK_LMENU;
    if (code == "AltRight") return VK_RMENU;
    if (code.size() == 4 && code.rfind("Key", 0) == 0) {
        char c = code[3];
        if (c >= 'A' && c <= 'Z') return static_cast<int>(c);
    }
    if (code.size() == 6 && code.rfind("Digit", 0) == 0) {
        char c = code[5];
        if (c >= '0' && c <= '9') return static_cast<int>(c);
    }
    if (code.size() == 7 && code.rfind("Numpad", 0) == 0) {
        char c = code[6];
        if (c >= '0' && c <= '9') return VK_NUMPAD0 + (c - '0');
    }
    return 0;
}

InputController::InputController(model::ConfigModel& configModel, model::EmulatorModel& emulatorModel)
    : m_config(configModel), m_emulator(emulatorModel) {}

InputController::~InputController() {
    Shutdown();
}

bool InputController::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return true;

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC | SDL_INIT_EVENTS)) {
        std::cerr << "[InputController] SDL_InitSubSystem failed: " << SDL_GetError() << "\n";
        return false;
    }

    m_initialized = true;
    RefreshGamepads();
    return true;
}

void InputController::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (SDL_Gamepad* pad : m_openGamepads) {
        if (pad) SDL_CloseGamepad(pad);
    }
    m_openGamepads.clear();
    m_gamepadIds.clear();
    m_initialized = false;
}

void InputController::RefreshGamepads() {
    SDL_PumpEvents();

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);

    // Check if gamepad list changed
    bool changed = (static_cast<size_t>(count) != m_gamepadIds.size());
    if (!changed && ids) {
        for (int i = 0; i < count; ++i) {
            if (m_gamepadIds[i] != ids[i]) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        for (SDL_Gamepad* pad : m_openGamepads) {
            if (pad) SDL_CloseGamepad(pad);
        }
        m_openGamepads.clear();
        m_gamepadIds.clear();

        if (ids && count > 0) {
            for (int i = 0; i < count; ++i) {
                SDL_Gamepad* pad = SDL_OpenGamepad(ids[i]);
                if (pad) {
                    m_openGamepads.push_back(pad);
                    m_gamepadIds.push_back(ids[i]);
                }
            }
        }
    }

    if (ids) {
        SDL_free(ids);
    }
}

std::vector<ConnectedDeviceInfo> InputController::GetConnectedDevices() {
    std::lock_guard<std::mutex> lock(m_mutex);
    RefreshGamepads();

    std::vector<ConnectedDeviceInfo> list;
    ConnectedDeviceInfo kb;
    kb.id = "keyboard";
    kb.type = "keyboard";
    kb.name = "Keyboard";
    kb.instanceId = 0;
    list.push_back(kb);

    for (size_t i = 0; i < m_openGamepads.size(); ++i) {
        SDL_Gamepad* pad = m_openGamepads[i];
        if (!pad) continue;
        ConnectedDeviceInfo info;
        info.id = "gamepad_" + std::to_string(i);
        info.type = "gamepad";
        const char* rawName = SDL_GetGamepadName(pad);
        info.name = rawName ? std::string(rawName) : ("Gamepad #" + std::to_string(i + 1));
        info.instanceId = m_gamepadIds[i];
        list.push_back(info);
    }
    return list;
}

json InputController::GetConnectedDevicesJson() {
    auto devices = GetConnectedDevices();
    json arr = json::array();
    for (const auto& d : devices) {
        arr.push_back({
            {"id", d.id},
            {"type", d.type},
            {"name", d.name},
            {"instanceId", d.instanceId}
        });
    }
    return arr;
}

void InputController::SetWebKeyState(const std::string& code, bool pressed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (pressed) {
        m_pressedWebKeys.insert(code);
    } else {
        m_pressedWebKeys.erase(code);
    }
}

bool InputController::IsKeyboardKeyPressed(const std::string& webKeyCode, bool windowFocused) const {
    if (m_pressedWebKeys.count(webKeyCode) > 0) {
        return true;
    }
    if (!windowFocused) return false;
    int vk = WebKeyCodeToVirtualKey(webKeyCode);
    if (vk != 0) {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
    return false;
}

bool InputController::IsGamepadInputPressed(SDL_Gamepad* pad, const std::string& buttonCode, float deadzone) const {
    if (!pad || buttonCode.empty()) return false;

    int16_t threshold = static_cast<int16_t>(std::clamp(deadzone, 0.05f, 0.90f) * 32767.0f);

    if (buttonCode == "dpup") {
        return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP) ||
               SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) < -threshold;
    }
    if (buttonCode == "dpdown") {
        return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ||
               SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) > threshold;
    }
    if (buttonCode == "dpleft") {
        return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ||
               SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) < -threshold;
    }
    if (buttonCode == "dpright") {
        return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ||
               SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) > threshold;
    }
    if (buttonCode == "south") return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH);
    if (buttonCode == "east") return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST);
    if (buttonCode == "west") return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST);
    if (buttonCode == "north") return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH);
    if (buttonCode == "leftshoulder") return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    if (buttonCode == "rightshoulder") return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    if (buttonCode == "lefttrigger") return SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > threshold;
    if (buttonCode == "righttrigger") return SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > threshold;
    if (buttonCode == "start") return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START);
    if (buttonCode == "back") return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
    if (buttonCode == "leftstick") return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK);
    if (buttonCode == "rightstick") return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);

    return false;
}

void InputController::PollAndReportInputs(bool windowFocused) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return;

    RefreshGamepads();
    auto bindings = m_config.GetPlayerBindings();

    for (size_t i = 0; i < bindings.size() && i < 4; ++i) {
        const auto& b = bindings[i];
        int playerNum = static_cast<int>(i + 1);

        if (b.deviceType == "none") {
            for (const auto& [name, snesBtn] : kSnesButtonMap) {
                m_emulator.ReportButtonState(playerNum, snesBtn, false);
            }
            continue;
        }

        SDL_Gamepad* assignedPad = nullptr;
        if (b.deviceType == "gamepad") {
            size_t padIdx = 0;
            if (b.deviceId.rfind("gamepad_", 0) == 0) {
                try {
                    padIdx = static_cast<size_t>(std::stoi(b.deviceId.substr(8)));
                } catch (...) {
                    padIdx = 0;
                }
            }
            if (padIdx < m_openGamepads.size()) {
                assignedPad = m_openGamepads[padIdx];
            }
        } else if (playerNum == 1 && !m_openGamepads.empty()) {
            // Convenience: Player 1 responds to both Keyboard and Gamepad 0 out of the box
            assignedPad = m_openGamepads[0];
        }

        for (const auto& [btnName, snesBtn] : kSnesButtonMap) {
            bool pressed = false;
            auto it = b.buttons.find(btnName);
            std::string mappedCode = (it != b.buttons.end()) ? it->second : "";

            if (b.deviceType == "keyboard" && !mappedCode.empty()) {
                pressed = IsKeyboardKeyPressed(mappedCode, windowFocused);
            }
            if (!pressed && assignedPad) {
                std::string padCode = mappedCode;
                if (b.deviceType == "keyboard") {
                    // Use standard SNES layout on Gamepad 0 when P1 is set to Keyboard
                    if (btnName == "Up") padCode = "dpup";
                    else if (btnName == "Down") padCode = "dpdown";
                    else if (btnName == "Left") padCode = "dpleft";
                    else if (btnName == "Right") padCode = "dpright";
                    else if (btnName == "A") padCode = "east";
                    else if (btnName == "B") padCode = "south";
                    else if (btnName == "X") padCode = "north";
                    else if (btnName == "Y") padCode = "west";
                    else if (btnName == "L") padCode = "leftshoulder";
                    else if (btnName == "R") padCode = "rightshoulder";
                    else if (btnName == "Start") padCode = "start";
                    else if (btnName == "Select") padCode = "back";
                }
                pressed = IsGamepadInputPressed(assignedPad, padCode, b.deadzone);
            }

            m_emulator.ReportButtonState(playerNum, snesBtn, pressed);
        }
    }
}

std::string InputController::PollFirstPressedGamepadButton(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RefreshGamepads();

    size_t targetIdx = 0;
    bool checkAll = true;
    if (deviceId.rfind("gamepad_", 0) == 0) {
        try {
            targetIdx = static_cast<size_t>(std::stoi(deviceId.substr(8)));
            checkAll = false;
        } catch (...) {}
    }

    static const std::vector<std::string> kCheckOrder = {
        "dpup", "dpdown", "dpleft", "dpright",
        "south", "east", "west", "north",
        "leftshoulder", "rightshoulder", "lefttrigger", "righttrigger",
        "start", "back", "leftstick", "rightstick"
    };

    for (size_t i = 0; i < m_openGamepads.size(); ++i) {
        if (!checkAll && i != targetIdx) continue;
        SDL_Gamepad* pad = m_openGamepads[i];
        if (!pad) continue;
        for (const auto& code : kCheckOrder) {
            if (IsGamepadInputPressed(pad, code, 0.35f)) {
                return code;
            }
        }
    }
    return "";
}

bool InputController::TriggerPlayerRumble(int playerIndex1Based, float intensity, uint32_t durationMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RefreshGamepads();
    if (m_openGamepads.empty()) return false;

    auto b = m_config.GetPlayerBinding(playerIndex1Based);
    size_t padIdx = 0;
    if (b.deviceId.rfind("gamepad_", 0) == 0) {
        try {
            padIdx = static_cast<size_t>(std::stoi(b.deviceId.substr(8)));
        } catch (...) {
            padIdx = 0;
        }
    }
    if (padIdx >= m_openGamepads.size() || !m_openGamepads[padIdx]) return false;

    uint16_t freq = static_cast<uint16_t>(std::clamp(intensity, 0.0f, 1.0f) * 65535.0f);
    return SDL_RumbleGamepad(m_openGamepads[padIdx], freq, freq, durationMs);
}

UiGamepadNavState InputController::PollUiGamepadNavigationState() {
    std::lock_guard<std::mutex> lock(m_mutex);
    UiGamepadNavState state{};
    if (!m_initialized) return state;

    RefreshGamepads();
    if (m_openGamepads.empty()) return state;

    const float deadzone = 0.35f;

    // Check connected gamepads (DualShock 4, Xbox, etc.)
    for (SDL_Gamepad* pad : m_openGamepads) {
        if (!pad) continue;

        if (IsGamepadInputPressed(pad, "dpup", deadzone)) state.up = true;
        if (IsGamepadInputPressed(pad, "dpdown", deadzone)) state.down = true;
        if (IsGamepadInputPressed(pad, "dpleft", deadzone) || IsGamepadInputPressed(pad, "leftshoulder", deadzone)) state.pageUp = true;
        if (IsGamepadInputPressed(pad, "dpright", deadzone) || IsGamepadInputPressed(pad, "rightshoulder", deadzone)) state.pageDown = true;
        if (IsGamepadInputPressed(pad, "south", deadzone)) state.south = true; // DualShock 4 X button
        if (IsGamepadInputPressed(pad, "west", deadzone)) state.west = true;   // DualShock 4 Square button
        if (IsGamepadInputPressed(pad, "east", deadzone)) state.east = true;   // DualShock 4 Circle button
    }

    return state;
}

} // namespace snex9x::controller

