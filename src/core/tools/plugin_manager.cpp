// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifndef __STRINGIFY
#define __STRINGIFY(s) #s
#endif

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
#include "core/loader/loader.h"
#include "core/memory.h"
#include "core/settings.h"
#include "core/tools/plugin_definitions.h"
#include "core/tools/plugin_manager.h"

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
    // ADD_FUNCTION_TO_PLUGIN(emu_framecount)
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
    // clang-format off
    ADD_FUNCTION_TO_PLUGIN(emu_log, [](void* ctx,
        const char* logMessage, PluginDefinitions::LogLevel level) -> void {
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
        default:
            LOG_INFO(Plugin, logMessage);
            break;
        }
    })
    // clang-format on
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
    // clang-format off
    ADD_FUNCTION_TO_PLUGIN(joypad_read,
        [](void* ctx, PluginDefinitions::ControllerNumber player) -> uint64_t {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad = self->hidAppletResource->GetController<Service::HID::Controller_NPad>(Service::HID::HidController::NPad);
            return npad.GetRawHandle((uint32_t) player).pad_states.raw;
    })
    ADD_FUNCTION_TO_PLUGIN(joypad_set,
        [](void* ctx, PluginDefinitions::ControllerNumber player, uint64_t input) -> void {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad = self->hidAppletResource->GetController<Service::HID::Controller_NPad>(Service::HID::HidController::NPad);
            npad.GetRawHandle((uint32_t) player).pad_states.raw = input;
    })
    ADD_FUNCTION_TO_PLUGIN(joypad_readjoystick,
        [](void* ctx, PluginDefinitions::ControllerNumber player,
        PluginDefinitions::YuzuJoystickType type) -> int16_t {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad = self->hidAppletResource->GetController<Service::HID::Controller_NPad>(Service::HID::HidController::NPad);
            npad.RequestPadStateUpdate((uint32_t) player);
            auto& handle = npad.GetRawHandle((uint32_t) player);
            switch(type) {
                case PluginDefinitions::YuzuJoystickType::LeftX:
                    return handle.l_stick.x;
                case PluginDefinitions::YuzuJoystickType::LeftY:
                    return handle.l_stick.y;
                case PluginDefinitions::YuzuJoystickType::RightX:
                    return handle.r_stick.x;
                case PluginDefinitions::YuzuJoystickType::RightY:
                    return handle.r_stick.y;
                default:
                    UNREACHABLE();
                    break;
            }
    })
    ADD_FUNCTION_TO_PLUGIN(joypad_setjoystick,
        [](void* ctx, PluginDefinitions::ControllerNumber player,
        PluginDefinitions::YuzuJoystickType type, int16_t input) -> void {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad = self->hidAppletResource->GetController<Service::HID::Controller_NPad>(Service::HID::HidController::NPad);
            auto& handle = npad.GetRawHandle((uint32_t) player);
            switch(type) {
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
                default:
                    UNREACHABLE();
                    break;
            }
    })
    ADD_FUNCTION_TO_PLUGIN(joypad_readsixaxis,
        [](void* ctx, PluginDefinitions::ControllerNumber player,
        PluginDefinitions::SixAxisMotionTypes type) -> float {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad = self->hidAppletResource->GetController<Service::HID::Controller_NPad>(Service::HID::HidController::NPad);
            npad.RequestMotionUpdate((uint32_t) player);
            auto& handle = npad.GetRawMotionHandle((uint32_t) player);
            switch(type) {
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
                default:
                    UNREACHABLE();
                    break;
            }
    })
    ADD_FUNCTION_TO_PLUGIN(joypad_setsixaxis,
        [](void* ctx, PluginDefinitions::ControllerNumber player,
        PluginDefinitions::SixAxisMotionTypes type, float input) -> void {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad = self->hidAppletResource->GetController<Service::HID::Controller_NPad>(Service::HID::HidController::NPad);
            auto& handle = npad.GetRawMotionHandle((uint32_t) player);
            switch(type) {
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
                default:
                    UNREACHABLE();
                    break;
            }
    })
    ADD_FUNCTION_TO_PLUGIN(joypad_enablejoypad,
        [](void* ctx, PluginDefinitions::ControllerNumber player, uint8_t enable) -> void {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad =
                self->hidAppletResource->GetController<Service::HID::Controller_NPad>(Service::HID::HidController::NPad);
            size_t index = (size_t) player;
            auto type = npad.MapSettingsTypeToNPad(Settings::values.players[index].controller_type);
            npad.UpdateControllerAt(type, (size_t) player, enable);
    })
    ADD_FUNCTION_TO_PLUGIN(joypad_removealljoypads,
        [](void* ctx) -> void {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad =
                self->hidAppletResource->GetController<Service::HID::Controller_NPad>(Service::HID::HidController::NPad);
            npad.DisconnectAllConnectedControllers();
    })
    ADD_FUNCTION_TO_PLUGIN(joypad_setjoypadtype,
        [](void* ctx, PluginDefinitions::ControllerNumber player, PluginDefinitions::ControllerType type) -> void {
            Plugin* self = (Plugin*)ctx;
            Service::HID::Controller_NPad& npad =
                self->hidAppletResource->GetController<Service::HID::Controller_NPad>(Service::HID::HidController::NPad);
            size_t index = (size_t) player;
            using NpadType = Service::HID::Controller_NPad::NPadControllerType;
            npad.UpdateControllerAt((NpadType) type, index,
                Settings::values.players[index].connected);
    })
    ADD_FUNCTION_TO_PLUGIN(joypad_isjoypadconnected,
        [](void* ctx, PluginDefinitions::ControllerNumber player) -> uint8_t {
            return Settings::values.players[(size_t) player].connected;
    })
    // clang-format on
}
} // namespace Tools
