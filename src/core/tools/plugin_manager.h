// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "common/common_types.h"
#include "core/tools/plugin_definitions.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

class QImage;
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
/**
 * This class allows the user to enable plugins that give a DLL access to the game. This can enable
 * the user to attach separate programs that have additional control over the emulator without
 * compiling it into the emulator itself
 */
class PluginManager {

public:
    struct Plugin {
        bool ready{false};
        std::string path;
        std::string plugin_name;
        std::atomic_bool processedMainLoop{true};
        std::atomic_bool encounteredVsync{false};
        bool hasStopped{false};
        std::mutex pluginMutex;
        std::condition_variable pluginCv;
        std::unique_ptr<std::thread> pluginThread{nullptr};
        Tools::PluginManager* pluginManager;
        std::shared_ptr<Service::HID::IAppletResource> hidAppletResource{nullptr};
        Core::System* system{nullptr};
        PluginDefinitions::meta_handle_main_loop* mainLoopFunction;
#ifdef _WIN32
        HMODULE sharedLibHandle;
#endif
#if defined(__linux__) || defined(__APPLE__)
        void* sharedLibHandle;
#endif
    };

    explicit PluginManager(Core::System& system_);
    ~PluginManager();

    // Enables or disables the entire plugin manager.
    void SetActive(bool active_);

    // Returns whether or not the plugin manager is active.
    bool IsActive() const;

    void ProcessScriptFromVsync();
    // Done during the course of the emulator (especially when a game is closed)
    void ProcessScriptFromMainLoop();

    bool LoadPlugin(std::string path, std::string name);

    void RemovePlugin(std::string path) {
        std::lock_guard lock{plugins_mutex};

        if (IsPluginLoaded(path)) {
            loaded_plugins.erase(std::find(loaded_plugins.begin(), loaded_plugins.end(), path));
        }
    }

    bool IsPluginLoaded(std::string path) {
        std::lock_guard lock{plugins_mutex};

        return loaded_plugins.count(path);
    }

    const std::set<std::string>& GetAllLoadedPlugins() {
        std::lock_guard lock{plugins_mutex};

        return loaded_plugins;
    }

    void SetPluginCallback(std::function<void()> func) {
        plugin_list_update_callback = func;
    }

    std::string GetLastErrorString() {
        std::string error = last_error;
        last_error = "";
        return error;
    }

    void SetRenderCallback(std::function<void(const QImage& pixmap)> callback) {
        render_callback = callback;
    }

    void SetScreenshotCallback(std::function<QImage()> callback) {
        screenshot_callback = callback;
    }

    void RegenerateGuiRendererIfNeeded();
    void RenderGui() {
        std::lock_guard lock{plugins_mutex};

        if (render_callback) {
            render_callback(*guiPixmap);
        }
    }

    QPainter* guiPainter;
    std::function<QImage()> screenshot_callback;

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

    std::string TrimString(std::string& str) {
        // Also removes periods
        static char const* removeChars = "\n\r\t. ";
        auto const& start = str.find_first_not_of(removeChars);
        auto const& end = str.find_last_not_of(removeChars);

        return start != std::string::npos ? str.substr(start, 1 + end - start) : std::string();
    }

    template <typename T>
    T* GetDllFunction(Plugin& plugin, std::string name) {
#ifdef _WIN32
        return (T*)GetProcAddress(plugin.sharedLibHandle, name.c_str());
#endif
#if defined(__linux__) || defined(__APPLE__)
        return (T*)dlsym(plugin.sharedLibHandle, name.c_str());
#endif
    }

    void ProcessScript(std::shared_ptr<Plugin> plugin);

    void ConnectAllDllFunctions(std::shared_ptr<Plugin> plugin);

    void PluginThreadExecuter(std::shared_ptr<Plugin> plugin);

    void EnsureHidAppletLoaded(std::shared_ptr<Plugin> plugin);

    void HandlePluginClosings();

    std::atomic_bool active{false};

    std::vector<std::shared_ptr<Plugin>> plugins;
    std::set<std::string> loaded_plugins;
    std::vector<std::shared_ptr<Plugin>> temp_plugins_to_remove;

    std::function<void()> plugin_list_update_callback{nullptr};

    std::string last_error;

    LastDockedState lastDockedState{LastDockedState::Neither};
    QImage* guiPixmap;
    std::function<void(const QImage& pixmap)> render_callback;

    mutable std::mutex plugins_mutex;

    std::unique_ptr<std::thread> plugin_main_loop_thread{nullptr};
    std::atomic_bool run_main_loop_thread{true};

    Core::System& system;
    Core::Timing::CoreTiming& core_timing;
    Core::Memory::Memory& memory;
};
} // namespace Tools
