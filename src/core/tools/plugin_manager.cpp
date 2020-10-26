// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifndef __STRINGIFY
#define __STRINGIFY(s) #s
#endif

#include <QColor>
#include <QFile>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QRectF>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/core_timing_util.h"
#include "core/cpu_manager.h"
#include "core/hardware_properties.h"
#include "core/hle/kernel/memory/page_table.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/hid/controllers/keyboard.h"
#include "core/hle/service/hid/controllers/mouse.h"
#include "core/hle/service/hid/controllers/npad.h"
#include "core/hle/service/hid/controllers/touchscreen.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/vi/vi.h"
#include "core/loader/loader.h"
#include "core/memory.h"
#include "core/settings.h"
#include "core/tools/plugin_definitions.h"
#include "core/tools/plugin_manager.h"
#include "video_core/renderer_base.h"

namespace Tools {
PluginManager::PluginManager(Core::System& system_)
    : system{system_}, core_timing{system.CoreTiming()}, memory{system.Memory()} {}

PluginManager::~PluginManager() {}

void PluginManager::SetActive(bool active) {
    if (!this->active.exchange(active)) {
        loaded_plugins.clear();
    }
}

bool PluginManager::IsActive() const {
    return active.load(std::memory_order_relaxed);
}

void PluginManager::ProcessScript(std::shared_ptr<Plugin> plugin) {
    {
        std::lock_guard<std::mutex> lk(plugin->pluginMutex);
        plugin->ready = true;
    }
    plugin->pluginCv.notify_one();

    {
        // Gain back control
        std::unique_lock<std::mutex> lk(plugin->pluginMutex);
        plugin->pluginCv.wait(
            lk, [plugin] { return plugin->processedMainLoop || plugin->encounteredVsync; });

        if (plugin->processedMainLoop.load(std::memory_order_relaxed) &&
            loaded_plugins.find(plugin->path) == loaded_plugins.end()) {
            plugin->hasStopped = true;
            temp_plugins_to_remove.push_back(plugin);
        }
    }
}

void PluginManager::ProcessScriptFromVsync() {
    temp_plugins_to_remove.clear();

    for (auto& plugin : plugins) {
        if (!plugin->pluginThread->joinable()) {
            plugin->pluginThread =
                std::make_unique<std::thread>(&PluginManager::PluginThreadExecuter, this, plugin);
        }

        if (plugin->encounteredVsync.load(std::memory_order_relaxed)) {
            // Continue thread from the vsync event
            plugin->encounteredVsync = false;

            do {
                ProcessScript(plugin);
            } while (plugin->processedMainLoop.load(std::memory_order_relaxed) &&
                     !plugin->hasStopped);
        }
    }

    for (auto& plugin : temp_plugins_to_remove) {
        GetDllFunction<PluginDefinitions::meta_handle_close>(*plugin, "onClose")();
        plugin->pluginThread->join();

#ifdef _WIN32
        FreeLibrary(plugin->sharedLibHandle);
#endif
#if defined(__linux__) || defined(__APPLE__)
        dlclose(plugin->sharedLibHandle);
#endif

        // The plugin will now be freed
        plugins.erase(std::find(plugins.begin(), plugins.end(), plugin));

        if (plugin_list_update_callback) {
            plugin_list_update_callback();
        }
    }
}

void PluginManager::PluginThreadExecuter(std::shared_ptr<Plugin> plugin) {
    while (true) {
        std::unique_lock<std::mutex> lk(plugin->pluginMutex);
        plugin->pluginCv.wait(lk, [this, plugin] { return plugin->ready; });
        plugin->ready = false;

        if (plugin->hasStopped) {
            plugin->processedMainLoop = true;
            return;
        }

        // Call shared lib main loop function, should already be loaded
        // Could be instead obtained once, TODO
        GetDllFunction<PluginDefinitions::meta_handle_main_loop>(*plugin, "onMainLoop")();

        // Once the end of this function is reached, the main loop must have completed
        lk.unlock();
        plugin->processedMainLoop = true;
        plugin->encounteredVsync = false;
        plugin->pluginCv.notify_one();
    }
}

std::string PluginManager::GetLastDllError() {
#ifdef _WIN32
    DWORD errorMessageID = GetLastError();
    if (errorMessageID == 0)
        return "";

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 (LPSTR)&messageBuffer, 0, NULL);
    std::string message(messageBuffer, size);
    LocalFree(messageBuffer);

    return message;
#endif
#if defined(__linux__) || defined(__APPLE__)
    return std::string(dlerror());
#endif
}

bool PluginManager::LoadPlugin(std::string path) {
    if (!IsPluginLoaded(path)) {
        std::shared_ptr<Plugin> plugin = std::make_shared<Plugin>();
        plugin->path = path;

#ifdef _WIN32
        plugin->sharedLibHandle =
            LoadLibraryEx(path.c_str(), NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#endif
#if defined(__linux__) || defined(__APPLE__)
        plugin->sharedLibHandle = (void*)dlopen(path.c_str(), RTLD_LAZY);
#endif

        if (!plugin->sharedLibHandle) {
            last_error = "DLL error: " + GetLastDllError();
            return false;
        }

        PluginDefinitions::meta_getplugininterfaceversion* pluginVersion =
            GetDllFunction<PluginDefinitions::meta_getplugininterfaceversion>(
                *plugin, "get_plugin_interface_version");

        if (!pluginVersion || pluginVersion() != PLUGIN_INTERFACE_VERSION) {
            // The plugin is not compatible with this version of Yuzu
            last_error = "Plugin version " + std::to_string(pluginVersion()) +
                         " is not compatible with Yuzu plugin version " +
                         std::to_string(PLUGIN_INTERFACE_VERSION);
            return false;
        }

        PluginDefinitions::meta_setup_plugin* setup =
            GetDllFunction<PluginDefinitions::meta_setup_plugin>(*plugin, "startPlugin");

        PluginDefinitions::meta_handle_main_loop* mainLoop =
            GetDllFunction<PluginDefinitions::meta_handle_main_loop>(*plugin, "handleMainLoop");

        if (!setup || !mainLoop) {
            last_error = "The DLL lacks necessary functions to run";
            return false;
        }

        loaded_plugins.insert(path);

        plugin->system = &system;
        plugin->hidAppletResource =
            system.ServiceManager().GetService<Service::HID::Hid>("hid")->GetAppletResource();

        setup(plugin.get());

        plugins.push_back(plugin);
    }

    return true;
}

void PluginManager::RegenerateGuiRendererIfNeeded() {
    LastDockedState thisDockedState =
        Settings::values.use_docked_mode ? LastDockedState::Docked : LastDockedState::Undocked;
    if (lastDockedState == LastDockedState::Neither || lastDockedState != thisDockedState) {
        lastDockedState = thisDockedState;
        delete guiPixmap;
        delete guiPainter;
        if (Settings::values.use_docked_mode) {
            guiPixmap = new QPixmap((int)Service::VI::DisplayResolution::DockedWidth,
                                    (int)Service::VI::DisplayResolution::DockedHeight);
        } else {
            guiPixmap = new QPixmap((int)Service::VI::DisplayResolution::UndockedWidth,
                                    (int)Service::VI::DisplayResolution::UndockedHeight);
        }
        guiPainter = new QPainter(guiPixmap);
        guiPainter->setWindow(guiPixmap->rect());
    }
}

void PluginManager::ConnectAllDllFunctions(std::shared_ptr<Plugin> plugin) {
    // For every function in plugin_definitions.hpp
    // Can use lambdas
    PluginDefinitions::meta_add_function* func;

    ADD_FUNCTION_TO_PLUGIN(meta_free, [](void* ptr) -> void {
        // The plugin might have a different allocater
        free(ptr);
    })

    ADD_FUNCTION_TO_PLUGIN(emu_frameadvance, [](void* ctx) -> void {
        Plugin* self = (Plugin*)ctx;

        // Notify main thread a vsync event is now being waited for
        self->encounteredVsync = true;
        self->encounteredVsync = false;
        self->pluginCv.notify_one();

        // Block until main thread has reached vsync
        std::unique_lock<std::mutex> lk(self->pluginMutex);
        self->pluginCv.wait(lk, [self] { return self->ready; });
        self->ready = false;
        // Once this is done, execution will resume as normal
    });

    ADD_FUNCTION_TO_PLUGIN(emu_pause, [](void* ctx) -> void {
        Plugin* self = (Plugin*)ctx;
        self->system->Pause();
    })

    ADD_FUNCTION_TO_PLUGIN(emu_unpause, [](void* ctx) -> void {
        Plugin* self = (Plugin*)ctx;
        self->system->Run();
    })
    ADD_FUNCTION_TO_PLUGIN(emu_framecount, [](void* ctx) -> int32_t {
        Plugin* self = (Plugin*)ctx;
        return self->system->Renderer().GetCurrentFrame();
    })
    ADD_FUNCTION_TO_PLUGIN(emu_fps, [](void* ctx) -> float {
        Plugin* self = (Plugin*)ctx;
        return self->system->Renderer().GetCurrentFPS();
    })
    ADD_FUNCTION_TO_PLUGIN(emu_emulating, [](void* ctx) -> uint8_t {
        Plugin* self = (Plugin*)ctx;
        return self->system->CurrentProcess()->GetStatus() == Kernel::ProcessStatus::Running;
    })
    ADD_FUNCTION_TO_PLUGIN(emu_romname, [](void* ctx) -> char* {
        Plugin* self = (Plugin*)ctx;
        std::string name;
        if (self->system->GetGameName(name) == Loader::ResultStatus::Success) {
            return GetAllocatedString(name);
        } else {
            return NULL;
        }
    })
    ADD_FUNCTION_TO_PLUGIN(emu_getprogramid, [](void* ctx) -> uint64_t {
        Plugin* self = (Plugin*)ctx;
        uint64_t id;
        if (self->system->GetAppLoader().ReadProgramId(id) != Loader::ResultStatus::Success) {
            return 0;
        } else {
            return id;
        }
    })
    ADD_FUNCTION_TO_PLUGIN(emu_getprocessid, [](void* ctx) -> uint64_t {
        Plugin* self = (Plugin*)ctx;
        if (self->system->CurrentProcess() == nullptr) {
            return 0;
        } else {
            return self->system->CurrentProcess()->GetProcessID();
        }
    })
    ADD_FUNCTION_TO_PLUGIN(emu_getheapstart, [](void* ctx) -> uint64_t {
        Plugin* self = (Plugin*)ctx;
        if (self->system->CurrentProcess() == nullptr) {
            return 0;
        } else {
            return self->system->CurrentProcess()->PageTable().GetHeapRegionStart();
        }
    })
    ADD_FUNCTION_TO_PLUGIN(emu_getheapsize, [](void* ctx) -> uint64_t {
        Plugin* self = (Plugin*)ctx;
        if (self->system->CurrentProcess() == nullptr) {
            return 0;
        } else {
            return self->system->CurrentProcess()->PageTable().GetHeapRegionSize();
        }
    })
    ADD_FUNCTION_TO_PLUGIN(emu_getmainstart, [](void* ctx) -> uint64_t {
        Plugin* self = (Plugin*)ctx;
        if (self->system->CurrentProcess() == nullptr) {
            return 0;
        } else {
            return self->system->CurrentProcess()->PageTable().GetAddressSpaceStart();
        }
    })
    ADD_FUNCTION_TO_PLUGIN(emu_getmainsize, [](void* ctx) -> uint64_t {
        Plugin* self = (Plugin*)ctx;
        if (self->system->CurrentProcess() == nullptr) {
            return 0;
        } else {
            return self->system->CurrentProcess()->PageTable().GetAddressSpaceSize();
        }
    })
    ADD_FUNCTION_TO_PLUGIN(emu_getstackstart, [](void* ctx) -> uint64_t {
        Plugin* self = (Plugin*)ctx;
        if (self->system->CurrentProcess() == nullptr) {
            return 0;
        } else {
            return self->system->CurrentProcess()->PageTable().GetStackRegionStart();
        }
    })
    ADD_FUNCTION_TO_PLUGIN(emu_getstacksize, [](void* ctx) -> uint64_t {
        Plugin* self = (Plugin*)ctx;
        if (self->system->CurrentProcess() == nullptr) {
            return 0;
        } else {
            return self->system->CurrentProcess()->PageTable().GetStackRegionSize();
        }
    })
    ADD_FUNCTION_TO_PLUGIN(
        emu_log, [](void* ctx, const char* logMessage, PluginDefinitions::LogLevel level) -> void {
            // TODO send to correct channels
            switch (level) {
            case PluginDefinitions::LogLevel::Info:
                LOG_INFO(Plugin, logMessage);
                break;
            case PluginDefinitions::LogLevel::Critical:
                LOG_CRITICAL(Plugin, logMessage);
                break;
            case PluginDefinitions::LogLevel::Debug:
                LOG_DEBUG(Plugin, logMessage);
                break;
            case PluginDefinitions::LogLevel::Warning:
                LOG_WARNING(Plugin, logMessage);
                break;
            case PluginDefinitions::LogLevel::Error:
                LOG_ERROR(Plugin, logMessage);
                break;
            }
        })
    ADD_FUNCTION_TO_PLUGIN(
        memory_readbyterange,
        [](void* ctx, uint64_t address, uint8_t* bytes, uint64_t length) -> uint8_t {
            Plugin* self = (Plugin*)ctx;
            Core::Memory::Memory& memoryInstance = self->system->Memory();
            if (memoryInstance.IsValidVirtualAddress(address) &&
                memoryInstance.IsValidVirtualAddress(address + length - 1)) {
                memoryInstance.ReadBlock(address, bytes, length);
                return true;
            } else {
                return false;
            }
        })
    ADD_FUNCTION_TO_PLUGIN(
        memory_writebyterange,
        [](void* ctx, uint64_t address, uint8_t* bytes, uint64_t length) -> uint8_t {
            Plugin* self = (Plugin*)ctx;
            Core::Memory::Memory& memoryInstance = self->system->Memory();
            if (memoryInstance.IsValidVirtualAddress(address) &&
                memoryInstance.IsValidVirtualAddress(address + length - 1)) {
                memoryInstance.WriteBlock(address, bytes, length);
                return true;
            } else {
                return false;
            }
        })
    ADD_FUNCTION_TO_PLUGIN(debugger_getclockticks, [](void* ctx) -> uint64_t {
        Plugin* self = (Plugin*)ctx;
        return self->system->CoreTiming().GetClockTicks();
    })
    ADD_FUNCTION_TO_PLUGIN(debugger_getcputicks, [](void* ctx) -> uint64_t {
        Plugin* self = (Plugin*)ctx;
        return self->system->CoreTiming().GetCPUTicks();
    })
    ADD_FUNCTION_TO_PLUGIN(
        joypad_read, [](void* ctx, PluginDefinitions::ControllerNumber player) -> uint64_t {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad =
                self->hidAppletResource->GetController<Service::HID::Controller_NPad>(
                    Service::HID::HidController::NPad);
            return npad.GetRawHandle((uint32_t)player).pad_states.raw;
        })
    ADD_FUNCTION_TO_PLUGIN(
        joypad_set,
        [](void* ctx, PluginDefinitions::ControllerNumber player, uint64_t input) -> void {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad =
                self->hidAppletResource->GetController<Service::HID::Controller_NPad>(
                    Service::HID::HidController::NPad);
            npad.GetRawHandle((uint32_t)player).pad_states.raw = input;
        })
    ADD_FUNCTION_TO_PLUGIN(
        joypad_readjoystick,
        [](void* ctx, PluginDefinitions::ControllerNumber player,
           PluginDefinitions::YuzuJoystickType type) -> int16_t {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad =
                self->hidAppletResource->GetController<Service::HID::Controller_NPad>(
                    Service::HID::HidController::NPad);
            npad.RequestPadStateUpdate((uint32_t)player);
            auto& handle = npad.GetRawHandle((uint32_t)player);
            switch (type) {
            case PluginDefinitions::YuzuJoystickType::LeftX:
                return handle.l_stick.x;
            case PluginDefinitions::YuzuJoystickType::LeftY:
                return handle.l_stick.y;
            case PluginDefinitions::YuzuJoystickType::RightX:
                return handle.r_stick.x;
            case PluginDefinitions::YuzuJoystickType::RightY:
                return handle.r_stick.y;
            }
        })
    ADD_FUNCTION_TO_PLUGIN(
        joypad_setjoystick,
        [](void* ctx, PluginDefinitions::ControllerNumber player,
           PluginDefinitions::YuzuJoystickType type, int16_t input) -> void {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad =
                self->hidAppletResource->GetController<Service::HID::Controller_NPad>(
                    Service::HID::HidController::NPad);
            auto& handle = npad.GetRawHandle((uint32_t)player);
            switch (type) {
            case PluginDefinitions::YuzuJoystickType::LeftX:
                handle.l_stick.x = input;
                break;
            case PluginDefinitions::YuzuJoystickType::LeftY:
                handle.l_stick.y = input;
                break;
            case PluginDefinitions::YuzuJoystickType::RightX:
                handle.r_stick.x = input;
                break;
            case PluginDefinitions::YuzuJoystickType::RightY:
                handle.r_stick.y = input;
                break;
            }
        })
    ADD_FUNCTION_TO_PLUGIN(
        joypad_readsixaxis,
        [](void* ctx, PluginDefinitions::ControllerNumber player,
           PluginDefinitions::SixAxisMotionTypes type) -> float {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad =
                self->hidAppletResource->GetController<Service::HID::Controller_NPad>(
                    Service::HID::HidController::NPad);
            npad.RequestMotionUpdate((uint32_t)player);
            auto& handle = npad.GetRawMotionHandle((uint32_t)player);
            switch (type) {
            case PluginDefinitions::SixAxisMotionTypes::AccelerationX:
                return handle.accel.x;
            case PluginDefinitions::SixAxisMotionTypes::AccelerationY:
                return handle.accel.y;
            case PluginDefinitions::SixAxisMotionTypes::AccelerationZ:
                return handle.accel.z;
            case PluginDefinitions::SixAxisMotionTypes::AngularVelocityX:
                return handle.gyro.x;
            case PluginDefinitions::SixAxisMotionTypes::AngularVelocityY:
                return handle.gyro.y;
            case PluginDefinitions::SixAxisMotionTypes::AngularVelocityZ:
                return handle.gyro.z;
            case PluginDefinitions::SixAxisMotionTypes::AngleX:
                return handle.rotation.x;
            case PluginDefinitions::SixAxisMotionTypes::AngleY:
                return handle.rotation.y;
            case PluginDefinitions::SixAxisMotionTypes::AngleZ:
                return handle.rotation.z;
            case PluginDefinitions::SixAxisMotionTypes::DirectionXX:
                return handle.orientation[0].x;
            case PluginDefinitions::SixAxisMotionTypes::DirectionXY:
                return handle.orientation[0].y;
            case PluginDefinitions::SixAxisMotionTypes::DirectionXZ:
                return handle.orientation[0].z;
            case PluginDefinitions::SixAxisMotionTypes::DirectionYX:
                return handle.orientation[1].x;
            case PluginDefinitions::SixAxisMotionTypes::DirectionYY:
                return handle.orientation[1].y;
            case PluginDefinitions::SixAxisMotionTypes::DirectionYZ:
                return handle.orientation[1].z;
            case PluginDefinitions::SixAxisMotionTypes::DirectionZX:
                return handle.orientation[2].x;
            case PluginDefinitions::SixAxisMotionTypes::DirectionZY:
                return handle.orientation[2].y;
            case PluginDefinitions::SixAxisMotionTypes::DirectionZZ:
                return handle.orientation[2].z;
            }
        })
    ADD_FUNCTION_TO_PLUGIN(
        joypad_setsixaxis,
        [](void* ctx, PluginDefinitions::ControllerNumber player,
           PluginDefinitions::SixAxisMotionTypes type, float input) -> void {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad =
                self->hidAppletResource->GetController<Service::HID::Controller_NPad>(
                    Service::HID::HidController::NPad);
            auto& handle = npad.GetRawMotionHandle((uint32_t)player);
            switch (type) {
            case PluginDefinitions::SixAxisMotionTypes::AccelerationX:
                handle.accel.x = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::AccelerationY:
                handle.accel.y = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::AccelerationZ:
                handle.accel.z = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::AngularVelocityX:
                handle.gyro.x = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::AngularVelocityY:
                handle.gyro.y = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::AngularVelocityZ:
                handle.gyro.z = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::AngleX:
                handle.rotation.x = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::AngleY:
                handle.rotation.y = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::AngleZ:
                handle.rotation.z = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::DirectionXX:
                handle.orientation[0].x = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::DirectionXY:
                handle.orientation[0].y = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::DirectionXZ:
                handle.orientation[0].z = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::DirectionYX:
                handle.orientation[1].x = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::DirectionYY:
                handle.orientation[1].y = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::DirectionYZ:
                handle.orientation[1].z = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::DirectionZX:
                handle.orientation[2].x = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::DirectionZY:
                handle.orientation[2].y = input;
                break;
            case PluginDefinitions::SixAxisMotionTypes::DirectionZZ:
                handle.orientation[2].z = input;
                break;
            }
        })
    ADD_FUNCTION_TO_PLUGIN(
        joypad_enablejoypad,
        [](void* ctx, PluginDefinitions::ControllerNumber player, uint8_t enable) -> void {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad =
                self->hidAppletResource->GetController<Service::HID::Controller_NPad>(
                    Service::HID::HidController::NPad);
            size_t index = (size_t)player;
            auto type = npad.MapSettingsTypeToNPad(Settings::values.players[index].controller_type);
            npad.UpdateControllerAt(type, (size_t)player, enable);
        })
    ADD_FUNCTION_TO_PLUGIN(joypad_removealljoypads, [](void* ctx) -> void {
        Plugin* self = (Plugin*)ctx;
        Service::HID::Controller_NPad& npad =
            self->hidAppletResource->GetController<Service::HID::Controller_NPad>(
                Service::HID::HidController::NPad);
        npad.DisconnectAllConnectedControllers();
    })
    ADD_FUNCTION_TO_PLUGIN(
        joypad_setjoypadtype,
        [](void* ctx, PluginDefinitions::ControllerNumber player,
           PluginDefinitions::ControllerType type) -> void {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad =
                self->hidAppletResource->GetController<Service::HID::Controller_NPad>(
                    Service::HID::HidController::NPad);
            size_t index = (size_t)player;
            using NpadType = Service::HID::Controller_NPad::NPadControllerType;
            npad.UpdateControllerAt((NpadType)type, index,
                                    Settings::values.players[index].connected);
        })
    ADD_FUNCTION_TO_PLUGIN(joypad_isjoypadconnected,
                           [](void* ctx, PluginDefinitions::ControllerNumber player) -> uint8_t {
                               return Settings::values.players[(size_t)player].connected;
                           })
    ADD_FUNCTION_TO_PLUGIN(input_requeststateupdate, [](void* ctx) -> void {
        Plugin* self = (Plugin*)ctx;
        Service::HID::Controller_NPad& npad =
            self->hidAppletResource->GetController<Service::HID::Controller_NPad>(
                Service::HID::HidController::NPad);
        for (uint32_t joypad = 0; joypad < Settings::values.players.size(); joypad++) {
            if (Settings::values.players[joypad].connected) {
                npad.RequestPadStateUpdate(joypad);
                npad.RequestMotionUpdate(joypad);
            }
        }
        Service::HID::Controller_Keyboard& keyboard =
            self->hidAppletResource->GetController<Service::HID::Controller_Keyboard>(
                Service::HID::HidController::Keyboard);
        keyboard.RequestKeyboardStateUpdate();
        Service::HID::Controller_Touchscreen& touchscreen =
            self->hidAppletResource->GetController<Service::HID::Controller_Touchscreen>(
                Service::HID::HidController::Touchscreen);
        touchscreen.RequestTouchscreenStateUpdate(self->system->CoreTiming());
        Service::HID::Controller_Mouse& mouse =
            self->hidAppletResource->GetController<Service::HID::Controller_Mouse>(
                Service::HID::HidController::Mouse);
        mouse.RequestMouseStateUpdate();
    })
    ADD_FUNCTION_TO_PLUGIN(input_enablekeyboard, [](void* ctx, uint8_t enable) -> void {
        Settings::values.keyboard_enabled = enable;
    })
    ADD_FUNCTION_TO_PLUGIN(input_enablemouse, [](void* ctx, uint8_t enable) -> void {
        Settings::values.mouse_enabled = enable;
    })
    ADD_FUNCTION_TO_PLUGIN(input_enabletouchscreen, [](void* ctx, uint8_t enable) -> void {
        Settings::values.touchscreen.enabled = enable;
    })
    ADD_FUNCTION_TO_PLUGIN(
        input_iskeypressed, [](void* ctx, PluginDefinitions::KeyboardValues key) -> uint8_t {
            Plugin* self = (Plugin*)ctx;
            uint8_t corrected_key = (uint8_t)key;
            Service::HID::Controller_Keyboard& keyboard =
                self->hidAppletResource->GetController<Service::HID::Controller_Keyboard>(
                    Service::HID::HidController::Keyboard);
            auto& handle = keyboard.GetRawHandle();
            return handle.key[corrected_key / 8] & (1ULL << (corrected_key % 8));
        })
    ADD_FUNCTION_TO_PLUGIN(
        input_setkeypressed,
        [](void* ctx, PluginDefinitions::KeyboardValues key, uint8_t ispressed) -> void {
            Plugin* self = (Plugin*)ctx;
            uint8_t corrected_key = (uint8_t)key;
            Service::HID::Controller_Keyboard& keyboard =
                self->hidAppletResource->GetController<Service::HID::Controller_Keyboard>(
                    Service::HID::HidController::Keyboard);
            auto& handle = keyboard.GetRawHandle();
            if (ispressed) {
                handle.key[corrected_key / 8] |= (1ULL << (corrected_key % 8));
            } else {
                handle.key[corrected_key / 8] &= ~(1ULL << (corrected_key % 8));
            }
        })
    ADD_FUNCTION_TO_PLUGIN(
        input_iskeymodifierpressed,
        [](void* ctx, PluginDefinitions::KeyboardModifiers modifier) -> uint8_t {
            Plugin* self = (Plugin*)ctx;
            uint8_t corrected_modifier = (uint8_t)modifier;
            Service::HID::Controller_Keyboard& keyboard =
                self->hidAppletResource->GetController<Service::HID::Controller_Keyboard>(
                    Service::HID::HidController::Keyboard);
            auto& handle = keyboard.GetRawHandle();
            return handle.modifier & BIT(corrected_modifier);
        })
    ADD_FUNCTION_TO_PLUGIN(
        input_setkeymodifierpressed,
        [](void* ctx, PluginDefinitions::KeyboardModifiers modifier, uint8_t ispressed) -> void {
            Plugin* self = (Plugin*)ctx;
            uint8_t corrected_modifier = (uint8_t)modifier;
            Service::HID::Controller_Keyboard& keyboard =
                self->hidAppletResource->GetController<Service::HID::Controller_Keyboard>(
                    Service::HID::HidController::Keyboard);
            auto& handle = keyboard.GetRawHandle();
            if (ispressed) {
                handle.modifier |= BIT(corrected_modifier);
            } else {
                handle.modifier &= ~BIT(corrected_modifier);
            }
        })
    ADD_FUNCTION_TO_PLUGIN(
        input_ismousepressed, [](void* ctx, PluginDefinitions::MouseButton button) -> uint8_t {
            Plugin* self = (Plugin*)ctx;
            uint8_t corrected_button = (uint8_t)button;
            Service::HID::Controller_Mouse& mouse =
                self->hidAppletResource->GetController<Service::HID::Controller_Mouse>(
                    Service::HID::HidController::Mouse);
            auto& handle = mouse.GetRawHandle();
            return handle.button & BIT(corrected_button);
        })
    ADD_FUNCTION_TO_PLUGIN(
        input_setmousepressed,
        [](void* ctx, PluginDefinitions::MouseButton button, uint8_t ispressed) -> void {
            Plugin* self = (Plugin*)ctx;
            uint8_t corrected_button = (uint8_t)button;
            Service::HID::Controller_Mouse& mouse =
                self->hidAppletResource->GetController<Service::HID::Controller_Mouse>(
                    Service::HID::HidController::Mouse);
            auto& handle = mouse.GetRawHandle();
            if (ispressed) {
                handle.button |= BIT(corrected_button);
            } else {
                handle.button &= ~BIT(corrected_button);
            }
        })
    ADD_FUNCTION_TO_PLUGIN(input_getnumtouches, [](void* ctx) -> uint8_t {
        Plugin* self = (Plugin*)ctx;
        Service::HID::Controller_Touchscreen& touchscreen =
            self->hidAppletResource->GetController<Service::HID::Controller_Touchscreen>(
                Service::HID::HidController::Touchscreen);
        auto& handle = touchscreen.GetRawHandle();
        return handle.entry_count;
    })
    ADD_FUNCTION_TO_PLUGIN(input_setnumtouches, [](void* ctx, uint8_t num) -> void {
        Plugin* self = (Plugin*)ctx;
        Service::HID::Controller_Touchscreen& touchscreen =
            self->hidAppletResource->GetController<Service::HID::Controller_Touchscreen>(
                Service::HID::HidController::Touchscreen);
        auto& handle = touchscreen.GetRawHandle();
        handle.entry_count = num;
    })
    ADD_FUNCTION_TO_PLUGIN(
        joypad_readtouch,
        [](void* ctx, uint8_t idx, PluginDefinitions::TouchTypes type) -> uint32_t {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_Touchscreen& touchscreen =
                self->hidAppletResource->GetController<Service::HID::Controller_Touchscreen>(
                    Service::HID::HidController::Touchscreen);
            auto& handle = touchscreen.GetRawHandle().states[idx];
            switch (type) {
            case PluginDefinitions::TouchTypes::X:
                return handle.x;
            case PluginDefinitions::TouchTypes::Y:
                return handle.y;
            case PluginDefinitions::TouchTypes::DiameterX:
                return handle.diameter_x;
            case PluginDefinitions::TouchTypes::DiameterY:
                return handle.diameter_y;
            case PluginDefinitions::TouchTypes::RotationAngle:
                return handle.rotation_angle;
            }
        })
    ADD_FUNCTION_TO_PLUGIN(
        joypad_settouch,
        [](void* ctx, uint8_t idx, PluginDefinitions::TouchTypes type, uint32_t val) -> void {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_Touchscreen& touchscreen =
                self->hidAppletResource->GetController<Service::HID::Controller_Touchscreen>(
                    Service::HID::HidController::Touchscreen);
            auto& handle = touchscreen.GetRawHandle().states[idx];
            switch (type) {
            case PluginDefinitions::TouchTypes::X:
                handle.x = val;
                break;
            case PluginDefinitions::TouchTypes::Y:
                handle.y = val;
                break;
            case PluginDefinitions::TouchTypes::DiameterX:
                handle.diameter_x = val;
                break;
            case PluginDefinitions::TouchTypes::DiameterY:
                handle.diameter_y = val;
                break;
            case PluginDefinitions::TouchTypes::RotationAngle:
                handle.rotation_angle = val;
                break;
            }
        })
    ADD_FUNCTION_TO_PLUGIN(
        joypad_movemouse, [](void* ctx, PluginDefinitions::MouseTypes type, int32_t val) -> void {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_Mouse& mouse =
                self->hidAppletResource->GetController<Service::HID::Controller_Mouse>(
                    Service::HID::HidController::Mouse);
            auto& handle = mouse.GetRawHandle();
            switch (type) {
            case PluginDefinitions::MouseTypes::X:
                handle.x = val;
                break;
            case PluginDefinitions::MouseTypes::Y:
                handle.y = val;
                break;
            case PluginDefinitions::MouseTypes::DeltaX:
                handle.delta_x = val;
                break;
            case PluginDefinitions::MouseTypes::DeltaY:
                handle.delta_y = val;
                break;
            case PluginDefinitions::MouseTypes::WheelX:
                handle.mouse_wheel_x = val;
                break;
            case PluginDefinitions::MouseTypes::WheelY:
                handle.mouse_wheel_y = val;
                break;
            }
        })
    ADD_FUNCTION_TO_PLUGIN(
        joypad_readmouse, [](void* ctx, PluginDefinitions::MouseTypes type) -> int32_t {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_Mouse& mouse =
                self->hidAppletResource->GetController<Service::HID::Controller_Mouse>(
                    Service::HID::HidController::Mouse);
            auto& handle = mouse.GetRawHandle();
            switch (type) {
            case PluginDefinitions::MouseTypes::X:
                return handle.x;
            case PluginDefinitions::MouseTypes::Y:
                return handle.y;
            case PluginDefinitions::MouseTypes::DeltaX:
                return handle.delta_x;
            case PluginDefinitions::MouseTypes::DeltaY:
                return handle.delta_y;
            case PluginDefinitions::MouseTypes::WheelX:
                return handle.mouse_wheel_x;
            case PluginDefinitions::MouseTypes::WheelY:
                return handle.mouse_wheel_y;
            }
        })
    ADD_FUNCTION_TO_PLUGIN(
        input_enableoutsideinput,
        [](void* ctx, PluginDefinitions::EnableInputType typetoenable, uint8_t enable) -> void {
            Plugin* self = (Plugin*)ctx;

            Service::HID::Controller_NPad& npad =
                self->hidAppletResource->GetController<Service::HID::Controller_NPad>(
                    Service::HID::HidController::NPad);
            Service::HID::Controller_Keyboard& keyboard =
                self->hidAppletResource->GetController<Service::HID::Controller_Keyboard>(
                    Service::HID::HidController::Keyboard);
            Service::HID::Controller_Touchscreen& touchscreen =
                self->hidAppletResource->GetController<Service::HID::Controller_Touchscreen>(
                    Service::HID::HidController::Touchscreen);
            Service::HID::Controller_Mouse& mouse =
                self->hidAppletResource->GetController<Service::HID::Controller_Mouse>(
                    Service::HID::HidController::Mouse);

            if (typetoenable == PluginDefinitions::EnableInputType::All) {
                for (uint32_t joypad = 0; joypad < Settings::values.players.size(); joypad++) {
                    npad.EnableOutsideInput(joypad, enable);
                }
                keyboard.EnableOutsideInput(enable);
                mouse.EnableOutsideInput(enable);
                touchscreen.EnableOutsideInput(enable);
            } else {
                switch (typetoenable) {
                case PluginDefinitions::EnableInputType::EnableController1:
                    npad.EnableOutsideInput(0, enable);
                    break;
                case PluginDefinitions::EnableInputType::EnableController2:
                    npad.EnableOutsideInput(1, enable);
                    break;
                case PluginDefinitions::EnableInputType::EnableController3:
                    npad.EnableOutsideInput(2, enable);
                    break;
                case PluginDefinitions::EnableInputType::EnableController4:
                    npad.EnableOutsideInput(3, enable);
                    break;
                case PluginDefinitions::EnableInputType::EnableController5:
                    npad.EnableOutsideInput(4, enable);
                    break;
                case PluginDefinitions::EnableInputType::EnableController6:
                    npad.EnableOutsideInput(5, enable);
                    break;
                case PluginDefinitions::EnableInputType::EnableController7:
                    npad.EnableOutsideInput(6, enable);
                    break;
                case PluginDefinitions::EnableInputType::EnableController8:
                    npad.EnableOutsideInput(7, enable);
                    break;
                case PluginDefinitions::EnableInputType::EnableControllerHandheld:
                    npad.EnableOutsideInput(8, enable);
                    break;
                case PluginDefinitions::EnableInputType::EnableKeyboard:
                    keyboard.EnableOutsideInput(enable);
                    break;
                case PluginDefinitions::EnableInputType::EnableMouse:
                    mouse.EnableOutsideInput(enable);
                    break;
                case PluginDefinitions::EnableInputType::EnableTouchscreen:
                    touchscreen.EnableOutsideInput(enable);
                    break;
                }
            }
        })
    ADD_FUNCTION_TO_PLUGIN(gui_getwidth, [](void* ctx) -> uint32_t {
        Plugin* self = (Plugin*)ctx;
        return self->system->Renderer().Settings().screenshot_framebuffer_layout.width;
    })
    ADD_FUNCTION_TO_PLUGIN(gui_getheight, [](void* ctx) -> uint32_t {
        Plugin* self = (Plugin*)ctx;
        return self->system->Renderer().Settings().screenshot_framebuffer_layout.height;
    })
    ADD_FUNCTION_TO_PLUGIN(gui_clearscreen, [](void* ctx) -> void {
        Plugin* self = (Plugin*)ctx;
        self->pluginManager->RegenerateGuiRendererIfNeeded();
        auto& painter = self->pluginManager->guiPainter;
        painter->fillRect(QRectF(0, 0, painter->window().width(), painter->window().height()),
                          Qt::GlobalColor::transparent);
    })
    ADD_FUNCTION_TO_PLUGIN(gui_render, [](void* ctx) -> void {
        Plugin* self = (Plugin*)ctx;
        self->pluginManager->RegenerateGuiRendererIfNeeded();
        self->pluginManager->RenderGui();
    })
    ADD_FUNCTION_TO_PLUGIN(gui_drawpixel,
                           [](void* ctx, uint32_t x, uint32_t y, uint8_t red, uint8_t green,
                              uint8_t blue, uint8_t alpha) -> void {
                               Plugin* self = (Plugin*)ctx;
                               self->pluginManager->RegenerateGuiRendererIfNeeded();
                               auto& painter = self->pluginManager->guiPainter;
                               painter->setPen(QColor(red, green, blue, alpha));
                               painter->drawPoint(x, y);
                           })
    ADD_FUNCTION_TO_PLUGIN(gui_savescreenshotas, [](void* ctx, const char* path) -> bool {
        Plugin* self = (Plugin*)ctx;
        if (self->pluginManager->screenshot_callback) {
            QFile file(path);
            file.open(QIODevice::WriteOnly);
            self->pluginManager->screenshot_callback().save(&file, "PNG");
            return true;
        } else {
            return false;
        }
    })
    ADD_FUNCTION_TO_PLUGIN(gui_drawimage,
                           [](void* ctx, int32_t dx, int32_t dy, const char* path, int32_t sx,
                              int32_t sy, int32_t sw, int32_t sh) -> void {
                               Plugin* self = (Plugin*)ctx;
                               self->pluginManager->RegenerateGuiRendererIfNeeded();
                               QImage image(path);
                               auto& painter = self->pluginManager->guiPainter;
                               painter->drawImage(dx, dy, image, sx, sy, sw, sh);
                           })
    ADD_FUNCTION_TO_PLUGIN(
        gui_popup, [](void* ctx, const char* title, const char* message, const char* type) -> void {
            Plugin* self = (Plugin*)ctx;
            QMessageBox msgBox;
            msgBox.setText(tr(title));
            msgBox.setInformativeText(tr(message));

            if (strcmp(type, "inform") == 0) {
                msgBox.msgBox(QMessageBox::Information);
            } else if (strcmp(type, "warn") == 0) {
                msgBox.msgBox(QMessageBox::Warning);
            } else if (strcmp(type, "critical") == 0) {
                msgBox.msgBox(QMessageBox::Critical);
            } else {
                msgBox.msgBox(QMessageBox::NoIcon);
            }

            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setDefaultButton(QMessageBox::Ok);
            msgBox.exec();
        })
    ADD_FUNCTION_TO_PLUGIN(gui_savescreenshotmemory, [](void* ctx, uint64_t* size) -> uint8_t* {
        Plugin* self = (Plugin*)ctx;
        if (self->pluginManager->screenshot_callback) {
            QFile file(path);
            file.open(QIODevice::WriteOnly);
            auto& image = self->pluginManager->screenshot_callback().convertToImage();
            size_t imgSize = image.sizeInBytes();
            uint8_t* imgBuf = malloc(imgSize);
            *size = imgSize;
            memcpy(imgBuf, image.bits(), imgSize);
            return imgBuf;
        } else {
            return NULL;
        }
    })
}
} // namespace Tools
