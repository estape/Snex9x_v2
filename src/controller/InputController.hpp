#pragma once

#include "../model/ConfigModel.hpp"
#include "../model/EmulatorModel.hpp"
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include <mutex>
#include <json.hpp>

namespace snex9x::controller {

struct ConnectedDeviceInfo {
    std::string id;         // "keyboard", "gamepad_0", "gamepad_1", ...
    std::string type;       // "keyboard", "gamepad"
    std::string name;       // e.g. "Xbox Wireless Controller"
    SDL_JoystickID instanceId = 0;
};

struct UiGamepadNavState {
    bool up = false;
    bool down = false;
    bool pageUp = false;
    bool pageDown = false;
    bool south = false; // DualShock 4 X button (Launch)
    bool west = false;  // DualShock 4 Square button (Fullscreen)
    bool east = false;  // DualShock 4 Circle button (Back/Close)
};

class InputController {
public:
    InputController(model::ConfigModel& configModel, model::EmulatorModel& emulatorModel);
    ~InputController();

    bool Initialize();
    void Shutdown();

    // Polls SDL3 events/gamepads and Win32/WebView keyboard state, reporting 4-player inputs to EmulatorModel
    void PollAndReportInputs(bool windowFocused = true);

    // Polls connected gamepads for library UI navigation inputs (D-Pad, Left Stick, South/X, West/Square, East/Circle)
    UiGamepadNavState PollUiGamepadNavigationState();

    // Returns list of available devices ("none", "keyboard", and all connected SDL3 gamepads)
    std::vector<ConnectedDeviceInfo> GetConnectedDevices();
    nlohmann::json GetConnectedDevicesJson();

    // Updates WebView keyboard state (fallback/complement to Win32 GetAsyncKeyState)
    void SetWebKeyState(const std::string& code, bool pressed);

    // Checks if any button is currently pressed on a connected gamepad (for interactive binding modal)
    std::string PollFirstPressedGamepadButton(const std::string& deviceId);

    // Triggers short haptic rumble test on the specified player's gamepad
    bool TriggerPlayerRumble(int playerIndex1Based, float intensity = 0.65f, uint32_t durationMs = 220);

private:
    void RefreshGamepads();
    bool IsKeyboardKeyPressed(const std::string& webKeyCode, bool windowFocused) const;
    bool IsGamepadInputPressed(SDL_Gamepad* pad, const std::string& buttonCode, float deadzone) const;

    model::ConfigModel& m_config;
    model::EmulatorModel& m_emulator;
    mutable std::mutex m_mutex;

    bool m_initialized = false;
    std::vector<SDL_Gamepad*> m_openGamepads;
    std::vector<SDL_JoystickID> m_gamepadIds;
    std::unordered_set<std::string> m_pressedWebKeys;
};

} // namespace snex9x::controller

