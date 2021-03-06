// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/frontend/input.h"
#include "core/hle/service/hid/controllers/controller_base.h"
#include "core/settings.h"

namespace Service::HID {
class Controller_Keyboard final : public ControllerBase {
public:
    explicit Controller_Keyboard(Core::System& system);
    ~Controller_Keyboard() override;

    // Called when the controller is initialized
    void OnInit() override;

    // When the controller is released
    void OnRelease() override;

    // When the controller is requesting an update for the shared memory
    void OnUpdate(const Core::Timing::CoreTiming& core_timing, u8* data, std::size_t size) override;

    // Called when input devices should be loaded
    void OnLoadInputDevices() override;

    void RequestKeyboardStateUpdate();

    struct KeyboardState {
        s64_le sampling_number;
        s64_le sampling_number2;

        s32_le modifier;
        s32_le attribute;
        std::array<u8, 32> key;
    };
    static_assert(sizeof(KeyboardState) == 0x38, "KeyboardState is an invalid size");

    // Used to obtain a raw handle to a controller
    // Specifically for the plugin manager
    KeyboardState& GetRawHandle();

    // Enable input from user (as opposed to from a plugin) for this controller
    // Specifically for the plugin manager
    void EnableOutsideInput(bool enable);
    bool IsEnabledOutsideInput();

private:
    struct SharedMemory {
        CommonHeader header;
        std::array<KeyboardState, 17> pad_states;
        INSERT_PADDING_BYTES(0x28);
    };
    static_assert(sizeof(SharedMemory) == 0x400, "SharedMemory is an invalid size");
    SharedMemory shared_memory{};

    std::array<std::unique_ptr<Input::ButtonDevice>, Settings::NativeKeyboard::NumKeyboardKeys>
        keyboard_keys;
    std::array<std::unique_ptr<Input::ButtonDevice>, Settings::NativeKeyboard::NumKeyboardMods>
        keyboard_mods;
    bool outside_input_enabled{true};
};
} // namespace Service::HID
