// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/core_timing_util.h"
#include "core/cpu_manager.h"
#include "core/hardware_properties.h"
#include "core/hle/kernel/process.h"
#include "core/loader/loader.h"
#include "core/memory.h"
#include "core/settings.h"
#include "core/tools/plugin_definitions.h"
#include "core/tools/plugin_manager.h"

namespace Tools {
namespace {
/*
constexpr s64 MEMORY_FREEZER_TICKS = static_cast<s64>(1000000000 / 60);

u64 MemoryReadWidth(Core::Memory::Memory& memory, u32 width, VAddr addr) {
    switch (width) {
    case 1:
        return memory.Read8(addr);
    case 2:
        return memory.Read16(addr);
    case 4:
        return memory.Read32(addr);
    case 8:
        return memory.Read64(addr);
    default:
        UNREACHABLE();
        return 0;
    }
}

void MemoryWriteWidth(Core::Memory::Memory& memory, u32 width, VAddr addr, u64 value) {
    switch (width) {
    case 1:
        memory.Write8(addr, static_cast<u8>(value));
        break;
    case 2:
        memory.Write16(addr, static_cast<u16>(value));
        break;
    case 4:
        memory.Write32(addr, static_cast<u32>(value));
        break;
    case 8:
        memory.Write64(addr, value);
        break;
    default:
        UNREACHABLE();
    }
}
*/

} // Anonymous namespace

PluginManager::PluginManager(Core::Timing::CoreTiming& core_timing_, Core::Memory::Memory& memory_,
                             Core::System& system_)
    : core_timing{core_timing_}, memory{memory_}, system{system_} {
    /*
event = Core::Timing::CreateEvent(
    "MemoryFreezer::FrameCallback",
    [this](u64 userdata, s64 ns_late) { FrameCallback(userdata, ns_late); });

core_timing.ScheduleEvent(MEMORY_FREEZER_TICKS, event);
*/
}

PluginManager::~PluginManager() {
    /*
    core_timing.UnscheduleEvent(event, 0);
    */
}

void PluginManager::SetActive(bool active) {
    if (!this->active.exchange(active)) {
        /*
        FillEntryReads();
        core_timing.ScheduleEvent(MEMORY_FREEZER_TICKS, event);
        LOG_DEBUG(Common_Memory, "Memory freezer activated!");
        */
    } else {
        /*
        LOG_DEBUG(Common_Memory, "Memory freezer deactivated!");
        */
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

        if (plugin->processedMainLoop.load(std::memory_order_relaxed)) {
            // This main loop is done
            plugin->processedMainLoop = false;
            plugin->encounteredVsync = false;

            // pluginThread->join();
        }
    }
}

void PluginManager::ProcessScriptFromIdle() {
    for (auto& plugin : plugins) {
        if (!plugin->encounteredVsync.load(std::memory_order_relaxed)) {
            // Create thread for every main loop function
            plugin->encounteredVsync = false;
            plugin->processedMainLoop = false;

            if (!plugin->pluginThread->joinable()) {
                // Start for first time
                // If this plugin is disabled, the main loop has to be finshed, inform plugin
                //     it must close somehow
                plugin->pluginThread = std::make_unique<std::thread>(
                    &PluginManager::PluginThreadExecuter, this, plugin);
            }

            ProcessScript(plugin);
        }
    }
}

void PluginManager::ProcessScriptFromVsync() {
    for (auto& plugin : plugins) {
        if (plugin->encounteredVsync.load(std::memory_order_relaxed)) {
            // Continue thread from the vsync event
            plugin->encounteredVsync = false;

            ProcessScript(plugin);
        }
    }
}

void PluginManager::PluginThreadExecuter(std::shared_ptr<Plugin> plugin) {
    while (true) {
        std::unique_lock<std::mutex> lk(plugin->pluginMutex);
        plugin->pluginCv.wait(lk, [this, plugin] { return plugin->ready; });
        plugin->ready = false;
        // Call shared lib main loop function, should already be loaded
        // Could be instead obtained once, TODO
        GetDllFunction<PluginDefinitions::meta_handle_main_loop>(*plugin, "handleMainLoop")();

        // Once the end of this function is reached, the main loop must have completed
        lk.unlock();
        plugin->processedMainLoop = true;
        plugin->pluginCv.notify_one();
    }
}

std::string PluginManager::GetLastDllError() {
#ifdef _WIN32
    // Get the error message, if any.
    DWORD errorMessageID = ::GetLastError();
    if (errorMessageID == 0)
        return std::string(); // No error message has been recorded

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 (LPSTR)&messageBuffer, 0, NULL);

    std::string message(messageBuffer, size);

    // Free the buffer.
    LocalFree(messageBuffer);

    return message;
#endif
#if defined(__linux__) || defined(__APPLE__)
    return std::string(dlerror());
#endif
}

bool PluginManager::LoadPlugin(std::string path) {
    std::shared_ptr<Plugin> plugin = std::make_shared<Plugin>();

#ifdef _WIN32
    plugin->sharedLibHandle = LoadLibraryEx(path.c_str(), NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#endif
#if defined(__linux__) || defined(__APPLE__)
    plugin->sharedLibHandle = (void*)dlopen(path.c_str(), RTLD_LAZY);
#endif

    if (!plugin->sharedLibHandle) {
        // GetLastDllError();
        return false;
    }

    PluginDefinitions::meta_getplugininterfaceversion* pluginVersion =
        GetDllFunction<PluginDefinitions::meta_getplugininterfaceversion>(
            *plugin, "get_plugin_interface_version");

    if (!pluginVersion || pluginVersion() > PLUGIN_INTERFACE_VERSION) {
        // The plugin is not compatible with this version of Yuzu
        return false;
    }

    PluginDefinitions::meta_setup_plugin* setup =
        GetDllFunction<PluginDefinitions::meta_setup_plugin>(*plugin, "startPlugin");

    PluginDefinitions::meta_handle_main_loop* mainLoop =
        GetDllFunction<PluginDefinitions::meta_handle_main_loop>(*plugin, "handleMainLoop");

    if (!setup || !mainLoop) {
        // Neccessary functions not avalible
        return false;
    }

    plugin->system = &system;

    setup(plugin.get());

    plugins.push_back(plugin);
}

void PluginManager::ConnectAllDllFunctions(std::shared_ptr<Plugin> plugin) {
    // For every function in plugin_definitions.hpp
    // Can use lambdas
    PluginDefinitions::meta_add_function* func;

    ADD_FUNCTION_TO_PLUGIN(meta_free, [](void* ptr) -> void {
        // The plugin might have a different allocater
        free(ptr);
    })

    ADD_FUNCTION_TO_PLUGIN(emu_frameadvance, [](void* pluginInstance) -> void {
        Plugin* self = (Plugin*)pluginInstance;

        // Notify main thread a vsync event is now being waited for
        self->encounteredVsync = true;
        self->pluginCv.notify_one();

        // Block until main thread has reached vsync
        std::unique_lock<std::mutex> lk(self->pluginMutex);
        self->pluginCv.wait(lk, [self] { return self->ready; });
        self->ready = false;
        // Once this is done, execution will resume as normal
    });

    ADD_FUNCTION_TO_PLUGIN(emu_pause, [](void* pluginInstance) -> void {
        Plugin* self = (Plugin*)pluginInstance;
        self->system->Pause();
    })

    ADD_FUNCTION_TO_PLUGIN(emu_unpause, [](void* pluginInstance) -> void {
        Plugin* self = (Plugin*)pluginInstance;
        self->system->Run();
    })
    // ADD_FUNCTION_TO_PLUGIN(emu_message)
    // ADD_FUNCTION_TO_PLUGIN(emu_framecount)
    ADD_FUNCTION_TO_PLUGIN(emu_emulating, [](void* pluginInstance) -> uint8_t {
        Plugin* self = (Plugin*)pluginInstance;
        return self->system->CurrentProcess()->GetStatus() == Kernel::ProcessStatus::Running;
    })
    ADD_FUNCTION_TO_PLUGIN(emu_romname, [](void* pluginInstance) -> char* {
        Plugin* self = (Plugin*)pluginInstance;
        std::string name;
        if (self->system->GetGameName(name) == Loader::ResultStatus::Success) {
            return GetAllocatedString(name);
        } else {
            return NULL;
        }
    })
    ADD_FUNCTION_TO_PLUGIN(emu_getprogramid, [](void* pluginInstance) -> uint64_t {
        Plugin* self = (Plugin*)pluginInstance;
        uint64_t id;
        if (self->system->GetAppLoader().ReadProgramId(id) != Loader::ResultStatus::Success) {
            return NULL;
        } else {
            return id;
        }
    })
    /*
    ADD_FUNCTION_TO_PLUGIN(emu_getprocessid)
    ADD_FUNCTION_TO_PLUGIN(emu_getheapstart)
    ADD_FUNCTION_TO_PLUGIN(emu_getmainstart)
    ADD_FUNCTION_TO_PLUGIN(emu_log)
    ADD_FUNCTION_TO_PLUGIN(joypad_getnumjoypads)
    ADD_FUNCTION_TO_PLUGIN(joypad_setnumjoypads)
    ADD_FUNCTION_TO_PLUGIN(joypad_addjoypad)
    ADD_FUNCTION_TO_PLUGIN(emu_getscreenjpeg)
    ADD_FUNCTION_TO_PLUGIN(joypad_readjoystick)
    ADD_FUNCTION_TO_PLUGIN(joypad_read)
    ADD_FUNCTION_TO_PLUGIN(rom_readbytes)
    */
}

/*
void Freezer::Clear() {
    std::lock_guard lock{entries_mutex};

    LOG_DEBUG(Common_Memory, "Clearing all frozen memory values.");

    entries.clear();
}

u64 Freezer::Freeze(VAddr address, u32 width) {
    std::lock_guard lock{entries_mutex};

    const auto current_value = MemoryReadWidth(memory, width, address);
    entries.push_back({address, width, current_value});

    LOG_DEBUG(Common_Memory,
              "Freezing memory for address={:016X}, width={:02X}, current_value={:016X}", address,
              width, current_value);

    return current_value;
}

void Freezer::Unfreeze(VAddr address) {
    std::lock_guard lock{entries_mutex};

    LOG_DEBUG(Common_Memory, "Unfreezing memory for address={:016X}", address);

    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
                       [&address](const Entry& entry) { return entry.address == address; }),
        entries.end());
}

bool Freezer::IsFrozen(VAddr address) const {
    std::lock_guard lock{entries_mutex};

    return std::find_if(entries.begin(), entries.end(), [&address](const Entry& entry) {
               return entry.address == address;
           }) != entries.end();
}

void Freezer::SetFrozenValue(VAddr address, u64 value) {
    std::lock_guard lock{entries_mutex};

    const auto iter = std::find_if(entries.begin(), entries.end(), [&address](const Entry& entry) {
        return entry.address == address;
    });

    if (iter == entries.end()) {
        LOG_ERROR(Common_Memory,
                  "Tried to set freeze value for address={:016X} that is not frozen!", address);
        return;
    }

    LOG_DEBUG(Common_Memory,
              "Manually overridden freeze value for address={:016X}, width={:02X} to value={:016X}",
              iter->address, iter->width, value);
    iter->value = value;
}

std::optional<Freezer::Entry> Freezer::GetEntry(VAddr address) const {
    std::lock_guard lock{entries_mutex};

    const auto iter = std::find_if(entries.begin(), entries.end(), [&address](const Entry& entry) {
        return entry.address == address;
    });

    if (iter == entries.end()) {
        return std::nullopt;
    }

    return *iter;
}

std::vector<Freezer::Entry> Freezer::GetEntries() const {
    std::lock_guard lock{entries_mutex};

    return entries;
}

void Freezer::FrameCallback(u64 userdata, s64 ns_late) {
    if (!IsActive()) {
        LOG_DEBUG(Common_Memory, "Memory freezer has been deactivated, ending callback events.");
        return;
    }

    std::lock_guard lock{entries_mutex};

    for (const auto& entry : entries) {
        LOG_DEBUG(Common_Memory,
                  "Enforcing memory freeze at address={:016X}, value={:016X}, width={:02X}",
                  entry.address, entry.value, entry.width);
        MemoryWriteWidth(memory, entry.width, entry.address, entry.value);
    }

    core_timing.ScheduleEvent(MEMORY_FREEZER_TICKS - ns_late, event);
}

void Freezer::FillEntryReads() {
    std::lock_guard lock{entries_mutex};

    LOG_DEBUG(Common_Memory, "Updating memory freeze entries to current values.");

    for (auto& entry : entries) {
        entry.value = MemoryReadWidth(memory, entry.width, entry.address);
    }
}
*/

} // namespace Tools
