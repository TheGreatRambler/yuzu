// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#define ADD_FUNCTION_TO_PLUGIN(type, address)                                                      \
    func = GetDllFunction<PluginDefinitions::meta_add_function>(*plugin, "yuzupluginset_" #type);  \
    if (func)                                                                                      \
        func((void*)((PluginDefinitions::type*)address));

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "common/common_types.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

class QPixmap;
class QPainter;

namespace Core::Timing {
class CoreTiming;
struct EventType;
} // namespace Core::Timing

namespace Core::Memory {
class Memory;
} // namespace Core::Memory

namespace Core {
class System;
} // namespace Core

namespace Kernel {
class Process;
} // namespace Kernel

namespace Kernel::Memory {
class PageTable;
} // namespace Kernel::Memory

namespace Loader {
enum class ResultStatus : u16;
} // namespace Loader

namespace Service::HID {
class IAppletResource;
} // namespace Service::HID

namespace Tools {
struct Plugin {
    bool ready{false};
    std::string path;
    std::atomic_bool processedMainLoop{false};
    std::atomic_bool encounteredVsync{false};
    bool hasStopped{false};
    std::mutex pluginMutex;
    std::condition_variable pluginCv;
    std::unique_ptr<std::thread> pluginThread;
    bool pluginAvailable;
    Tools::PluginManager* pluginManager;
    std::shared_ptr<Service::HID::IAppletResource> hidAppletResource;
    Core::System* system;
#ifdef _WIN32
    HMODULE sharedLibHandle;
#endif
#if defined(__linux__) || defined(__APPLE__)
    void* sharedLibHandle;
#endif
};

/**
 * This class allows the user to enable plugins that give a DLL access to the game. This can enable
 * the user to attach separate programs that have additional control over the emulator without
 * compiling it into the emulator itself
 */
class PluginManager {
public:
    explicit PluginManager(Core::System& system_);
    ~PluginManager();

    // Enables or disables the entire plugin manager.
    void SetActive(bool active);

    // Returns whether or not the plugin manager is active.
    bool IsActive() const;

    void ProcessScriptFromVsync();

    bool LoadPlugin(std::string path);
    void RemovePlugin(std::string path) {
        if (IsPluginLoaded(path)) {
            loaded_plugins.erase(std::find(loaded_plugins.begin(), loaded_plugins.end(), path));
        }
    }
    bool IsPluginLoaded(std::string path) {
        return loaded_plugins.count(path);
    }

    const std::set<std::string>& GetAllLoadedPlugins() {
        return loaded_plugins;
    }

    void SetPluginCallback(std::function<void()> func) {
        plugin_list_update_callback = func;
    }

    std::string GetLastErrorString() {
        return last_error;
        last_error = "";
    }

    QPixmap* GetPluginRender() {
        return guiPixmap;
    }

private:
    enum LastDockedState : uint8_t {
        Neither,
        Docked,
        Undocked,
    };

    std::string GetLastDllError();

    static char* GetAllocatedString(std::string& str) {
        // Get allocated version of a string that must be freed
        char* buf = (char*)malloc(str.size() + 1);
        std::copy(str.begin(), str.end(), buf);
        buf[str.size()] = '\0';
        return buf;
    }

    template <typename T>
    T* GetDllFunction(Plugin& plugin, std::string name) {
#ifdef _WIN32
        return (T*)GetProcAddress(plugin.sharedLibHandle, name.c_str());
#endif
#if defined(__linux__) || defined(__APPLE__)
        return dlsym(plugin.sharedLibHandle, name.c_str());
#endif
    }

    void ProcessScript(std::shared_ptr<Plugin> plugin);

    void ConnectAllDllFunctions(std::shared_ptr<Plugin> plugin);

    void PluginThreadExecuter(std::shared_ptr<Plugin> plugin);

    void RegenerateGuiRendererIfNeeded();

    std::atomic_bool active{false};

    std::vector<std::shared_ptr<Plugin>> plugins;
    std::set<std::string> loaded_plugins;
    std::vector<std::shared_ptr<Plugin>> temp_plugins_to_remove;

    std::function<void()> plugin_list_update_callback{nullptr};

    std::string last_error;

    LastDockedState lastDockedState{LastDockedState::Neither};
    QPixmap* guiPixmap;
    QPainter* guiPainter;

    Core::System& system;
    Core::Timing::CoreTiming& core_timing;
    Core::Memory::Memory& memory;
};
} // namespace Tools
