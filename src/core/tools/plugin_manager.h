// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#define ADD_FUNCTION_TO_PLUGIN(type, address)                                                      \
    func = GetDllFunction<PluginDefinitions::meta_add_function>(*plugin, "yuzupluginset_" #type);  \
    if (func)                                                                                      \
        func((void*)((PluginDefinitions::type*)address));

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include "common/common_types.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

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

struct Plugin {
    bool ready{false};
    std::atomic_bool processedMainLoop{false};
    std::atomic_bool encounteredVsync{false};
    std::mutex pluginMutex;
    std::condition_variable pluginCv;
    std::unique_ptr<std::thread> pluginThread;
    bool pluginAvailable;
    Tools::PluginManager* pluginManager;
    Core::System* system;
#ifdef _WIN32
    HMODULE sharedLibHandle;
#endif
#if defined(__linux__) || defined(__APPLE__)
    void* sharedLibHandle;
#endif
};

namespace Tools {

/**
 * This class allows the user to enable plugins that give a DLL access to the game. This can enable
 * the user to attach separate programs that have additional control over the emulator without
 * compiling it into the emulator itself
 */
class PluginManager {
public:
    explicit PluginManager(Core::Timing::CoreTiming& core_timing_, Core::Memory::Memory& memory_,
                           Core::System& system_);
    ~PluginManager();

    // Enables or disables the entire plugin manager.
    void SetActive(bool active);

    // Returns whether or not the plugin manager is active.
    bool IsActive() const;

    void ProcessScript(std::shared_ptr<Plugin> plugin);
    void ProcessScriptFromIdle();
    void ProcessScriptFromVsync();

    /*
        // Removes all entries from the freezer.
        void Clear();

        // Freezes a value to its current memory address. The value the memory is kept at will be
       the
        // value that is read during this function. Width can be 1, 2, 4, or 8 (in bytes).
        u64 Freeze(VAddr address, u32 width);

        // Unfreezes the memory value at address. If the address isn't frozen, this is a no-op.
        void Unfreeze(VAddr address);

        // Returns whether or not the address is frozen.
        bool IsFrozen(VAddr address) const;

        // Sets the value that address should be frozen to. This doesn't change the width set by
       using
        // Freeze(). If the value isn't frozen, this will not freeze it and is thus a no-op.
        void SetFrozenValue(VAddr address, u64 value);

        // Returns the entry corresponding to the address if the address is frozen, otherwise
        // std::nullopt.
        std::optional<Entry> GetEntry(VAddr address) const;

        // Returns all the entries in the freezer, an empty vector means nothing is frozen.
        std::vector<Entry> GetEntries() const;
    */
private:
    /*
        void FrameCallback(u64 userdata, s64 cycles_late);
        void FillEntryReads();

        mutable std::mutex entries_mutex;
        std::vector<Entry> entries;
    */

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

    bool LoadPlugin(std::string path);
    void ConnectAllDllFunctions(std::shared_ptr<Plugin> plugin);

    void PluginThreadExecuter(std::shared_ptr<Plugin> plugin);

    std::atomic_bool active{false};

    std::vector<std::shared_ptr<Plugin>> plugins;

    Core::Timing::CoreTiming& core_timing;
    Core::Memory::Memory& memory;
    Core::System& system;
};

} // namespace Tools
