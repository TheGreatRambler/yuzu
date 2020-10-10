#pragma once

#define PLUGIN_INTERFACE_VERSION 0

#define BIT(n) (1U << (n))

#include <cstdint>
#include <cstring>

namespace PluginDefinitions {

enum class LogLevel : uint8_t {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
};

enum class EnableInputType : uint16_t {
    None = 0,
    EnableController1 = BIT(0),
    EnableController2 = BIT(1),
    EnableController3 = BIT(2),
    EnableController4 = BIT(3),
    EnableController5 = BIT(4),
    EnableController6 = BIT(5),
    EnableController7 = BIT(6),
    EnableController8 = BIT(7),
    EnableControllerHandheld = BIT(8),
    EnableTouchpad = BIT(9),
    EnableMouseKeyboard = BIT(10),
    All = BIT(11),
};

enum class YuzuJoystickType : uint8_t {
    LeftX = 0,
    LeftY = 1,
    RightX = 2,
    RightY = 3,
};

// Lifted from settings.h
enum class ButtonValues : uint8_t {
    A,
    B,
    X,
    Y,
    LStick,
    RStick,
    L,
    R,
    ZL,
    ZR,
    Plus,
    Minus,

    DLeft,
    DUp,
    DRight,
    DDown,

    LStick_Left,
    LStick_Up,
    LStick_Right,
    LStick_Down,

    RStick_Left,
    RStick_Up,
    RStick_Right,
    RStick_Down,

    SL,
    SR,

    Home,
    Screenshot,

    NumButtons,
};

enum class ControllerType : uint8_t {
    ProController,
    DualJoycon,
    RightJoycon,
    LeftJoycon,
};

enum class ControllerNumber : uint8_t {
    Controller1,
    Controller2,
    Controller3,
    Controller4,
    Controller5,
    Controller6,
    Controller7,
    Controller8,
    Handheld,
    Unknown,
};

enum class KeyboardValues : uint8_t {
    None,
    Error,

    A = 4,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    N1,
    N2,
    N3,
    N4,
    N5,
    N6,
    N7,
    N8,
    N9,
    N0,
    Enter,
    Escape,
    Backspace,
    Tab,
    Space,
    Minus,
    Equal,
    LeftBrace,
    RightBrace,
    Backslash,
    Tilde,
    Semicolon,
    Apostrophe,
    Grave,
    Comma,
    Dot,
    Slash,
    CapsLockKey,

    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    SystemRequest,
    ScrollLockKey,
    Pause,
    Insert,
    Home,
    PageUp,
    Delete,
    End,
    PageDown,
    Right,
    Left,
    Down,
    Up,

    NumLockKey,
    KPSlash,
    KPAsterisk,
    KPMinus,
    KPPlus,
    KPEnter,
    KP1,
    KP2,
    KP3,
    KP4,
    KP5,
    KP6,
    KP7,
    KP8,
    KP9,
    KP0,
    KPDot,

    Key102,
    Compose,
    Power,
    KPEqual,

    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,

    Open,
    Help,
    Properties,
    Front,
    Stop,
    Repeat,
    Undo,
    Cut,
    Copy,
    Paste,
    Find,
    Mute,
    VolumeUp,
    VolumeDown,
    CapsLockActive,
    NumLockActive,
    ScrollLockActive,
    KPComma,

    KPLeftParenthesis,
    KPRightParenthesis,

    LeftControlKey = 0xE0,
    LeftShiftKey,
    LeftAltKey,
    LeftMetaKey,
    RightControlKey,
    RightShiftKey,
    RightAltKey,
    RightMetaKey,

    MediaPlayPause,
    MediaStopCD,
    MediaPrevious,
    MediaNext,
    MediaEject,
    MediaVolumeUp,
    MediaVolumeDown,
    MediaMute,
    MediaWebsite,
    MediaBack,
    MediaForward,
    MediaStop,
    MediaFind,
    MediaScrollUp,
    MediaScrollDown,
    MediaEdit,
    MediaSleep,
    MediaCoffee,
    MediaRefresh,
    MediaCalculator,

    NumKeyboardKeys,
};

enum class KeyboardModifiers : uint8_t {
    LeftControl,
    LeftShift,
    LeftAlt,
    LeftMeta,
    RightControl,
    RightShift,
    RightAlt,
    RightMeta,
    CapsLock,
    ScrollLock,
    NumLock,

    NumKeyboardMods,
};

enum class MouseButton : int32_t {
    Left,
    Right,
    Middle,
    Forward,
    Back,
};

enum class TouchTypes : uint8_t {
    X,
    Y,
    DiameterX,
    DiameterY,
    RotationAngle,
};

enum class SixAxisMotionTypes : uint8_t {
    AccelerationX,
    AccelerationY,
    AccelerationZ,
    AngularVelocityX,
    AngularVelocityY,
    AngularVelocityZ,
    AngleX,
    AngleY,
    AngleZ,
    DirectionXX,
    DirectionXY,
    DirectionXZ,
    DirectionYX,
    DirectionYY,
    DirectionYZ,
    DirectionZX,
    DirectionZY,
    DirectionZZ,
};

// NOTE: Every time a char string is returned, it must be freed by the DLL

typedef void(meta_setup_plugin)(void*);
typedef void(meta_handle_main_loop)();
typedef void(meta_add_function)(void*);
typedef uint64_t(meta_getplugininterfaceversion)();

// Memory passed to the DLL that is allocated must be freed with this function
typedef void(meta_free)(void*);

// Emu library

// emu.poweron() ignored
// emu.softreset() ignored
typedef void(emu_speedmode)(void* ctx, const char* mode);
typedef void(emu_frameadvance)(void* ctx);
typedef void(emu_pause)(void* ctx);
typedef void(emu_unpause)(void* ctx);
// emu.exec_count(int count, function func) ignored
// emu.exec_time(int time, function func) ignored
// emu.setrenderplanes(bool sprites, bool background) ignored
typedef void(emu_message)(void* ctx, const char* mode);
typedef int(emu_framecount)(void* ctx);
// int emu.lagcount() ignored
// bool emu.lagged()
// emu.setlagflag(bool value) ignored
typedef uint8_t(emu_emulating)(void* ctx);
typedef uint8_t(emu_paused)(void* ctx);
// bool emu.readonly() ignored
// emu.setreadonly(bool state) ignored
typedef char*(emu_getdir)(void* ctx);
typedef void(emu_loadrom)(void* ctx, const char* filename);
// emu.registerbefore(function func) handled outside of Yuzu
// emu.registerafter(function func) handled outside of Yuzu
// emu.registerexit(function func) handled outside of Yuzu
// bool emu.addgamegenie(string str) ignored
// bool emu.delgamegenie(string str) ignored
typedef void(emu_print)(void* ctx, uint8_t mode);
typedef uint8_t*(emu_getscreenframebuffer)(void* ctx, uint64_t* size);
typedef uint8_t*(emu_getscreenjpeg)(void* ctx, uint64_t* size);

typedef char*(emu_romname)(void* ctx);
typedef uint64_t(emu_getprogramid)(void* ctx);
typedef uint64_t(emu_getprocessid)(void* ctx);
typedef uint64_t(emu_getheapstart)(void* ctx);
typedef uint64_t(emu_getheapsize)(void* ctx);
typedef uint64_t(emu_getmainstart)(void* ctx);
typedef uint64_t(emu_getmainsize)(void* ctx);
typedef uint64_t(emu_getstackstart)(void* ctx);
typedef uint64_t(emu_getstacksize)(void* ctx);

typedef void(emu_log)(void* ctx, const char* logmessage, LogLevel level);

// ROM Library cannot be implemented, use IPS or IPSwitch

// Memory Library

typedef uint8_t(memory_readbyterange)(void* ctx, uint64_t address, uint8_t* bytes, uint64_t length);
typedef uint8_t(memory_writebyterange)(void* ctx, uint64_t address, uint8_t* bytes,
                                       uint64_t length);
// memory.readword(int addressLow, [int addressHigh]) ignored
// memory.readwordunsigned(int addressLow, [int addressHigh]) ignored
// memory.readwordsigned(int addressLow, [int addressHigh]) ignored
// int memory.getregister(cpuregistername) ignored, believe the name is different
// memory.setregister(string cpuregistername, int value) ignored, same as above
// memory.register(int address, [int size,] function func) ignored
// memory.registerwrite(int address, [int size,] function func) ignored
// memory.registerexec(int address, [int size,] function func) ignored, talk to Shadow
// memory.registerrun(int address, [int size,] function func) ignored, same as above
// memory.registerexecute(int address, [int size,] function func) ignored, same as above

// Debugger Library

// typedef void(debugger_hitbreakpoint)(void* ctx);
typedef uint64_t(debugger_getclockticks)(void* ctx);
typedef uint64_t(debugger_getcputicks)(void* ctx);
// typedef void(debugger_resetcyclescount)(void* ctx);
// typedef void(debugger_resetinstructionscount)(void* ctx);

// Joypad Library (Modified, based on libnx standards)

typedef uint64_t(joypad_read)(void* ctx, ControllerNumber player);
// table joypad.getdown(int player) ignored
// table joypad.readdown(int player) ignored
// table joypad.getup(int player) ignored
// table joypad.readup(int player) ignored
typedef void(joypad_set)(void* ctx, ControllerNumber player, uint64_t input);

typedef int16_t(joypad_readjoystick)(void* ctx, ControllerNumber player, YuzuJoystickType type);
typedef void(joypad_setjoystick)(void* ctx, ControllerNumber player, YuzuJoystickType type,
                                 int16_t val);

typedef float(joypad_readsixaxis)(void* ctx, ControllerNumber player, SixAxisMotionTypes type);
typedef void(joypad_setsixaxis)(void* ctx, ControllerNumber player, SixAxisMotionTypes type,
                                float val);

// Add controllers
typedef void(joypad_addjoypad)(void* ctx, ControllerType type);
// Remove all controllers
typedef void(joypad_removealljoypads)(void* ctx);
// Set controller type at index
typedef void(joypad_setjoypadtype)(void* ctx, ControllerNumber player, ControllerType type);
// Get number of controllers
typedef uint8_t(joypad_getnumjoypads)(void* ctx);

// Input Library

// table input.get() ignored
// table input.read() ignored
// string input.popup ignored

typedef void(input_enablekeyboard)(void* ctx, uint8_t enable);
typedef void(input_enablemouse)(void* ctx, uint8_t enable);
typedef void(input_enabletouchscreen)(void* ctx, uint8_t enable);

typedef uint8_t(input_iskeypressed)(void* ctx, KeyboardValues key);
typedef void(input_setkeypressed)(void* ctx, KeyboardValues key, uint8_t ispressed);

typedef uint8_t(input_iskeymodifierpressed)(void* ctx, KeyboardModifiers modifier);
typedef void(input_setkeymodifierpressed)(void* ctx, KeyboardModifiers modifier, uint8_t ispressed);

typedef uint8_t(input_ismousepressed)(void* ctx, MouseButton button);
typedef void(input_setmousepressed)(void* ctx, MouseButton button, uint8_t ispressed);

typedef uint8_t(input_getnumtouches)(void* ctx);
typedef void(input_setnumtouches)(void* ctx, uint8_t num);

typedef int32_t(joypad_readtouch)(void* ctx, TouchTypes type);
typedef void(joypad_settouch)(void* ctx, TouchTypes type, int32_t val);

// Enable certain kinds of input from Yuzu, all input types not explicitely enabled
// Are set manually by the plugin
typedef void(input_enableoutsideinput)(void* ctx, EnableInputType typeToEnable);

// Savestate Library implemented in dll

// Movie Library implemented in dll

// GUI Library (Most functions handled DLL side, even text handling)

typedef void(gui_drawpixel)(void* ctx, int x, int y, uint8_t alpha, uint8_t red, uint8_t green,
                            uint8_t blue);
// gui.getpixel(int x, int y) ignored
// gui.box(int x1, int y1, int x2, int y2 [, fillcolor [, outlinecolor]]))
// gui.drawbox(int x1, int y1, int x2, int y2 [, fillcolor [, outlinecolor]]))
// gui.rect(int x1, int y1, int x2, int y2 [, fillcolor [, outlinecolor]]))
// gui.drawrect(int x1, int y1, int x2, int y2 [, fillcolor [, outlinecolor]]))
// gui.text(int x, int y, string str [, textcolor [, backcolor]])
// gui.drawtext(int x, int y, string str [, textcolor [, backcolor]])
// gui.parsecolor(color) ignored
typedef char*(gui_savescreenshot)(void* ctx);
typedef void(gui_savescreenshotas)(void* ctx, const char* path);
typedef void(gui_drawimage)(void* ctx, int dx, int dy, const char* path, int sx, int sy, int sw,
                            int sh, float alphamul);
// gui.opacity(int alpha) ignored
// gui.transparency(int trans) ignored
// function gui.register(function func) ignored
typedef void(gui_popup)(void* ctx, const char* message, const char* type, const char* icon);

// Saves screenshot into byte array as raw framebuffer
typedef uint8_t*(gui_savescreenshotmemory)(void* ctx, uint64_t* size);

// Sound Library ignored

// Settings

// ALL SETTINGS
// from src\core\settings.h
/*
    // Audio
    std::string audio_device_id;
    std::string sink_id;
    bool audio_muted;
    Setting<bool> enable_audio_stretching;
    Setting<float> volume;

    // Core
    Setting<bool> use_multi_core;

    // Cpu
    CPUAccuracy cpu_accuracy;

    bool cpuopt_page_tables;
    bool cpuopt_block_linking;
    bool cpuopt_return_stack_buffer;
    bool cpuopt_fast_dispatcher;
    bool cpuopt_context_elimination;
    bool cpuopt_const_prop;
    bool cpuopt_misc_ir;
    bool cpuopt_reduce_misalign_checks;

    bool cpuopt_unsafe_unfuse_fma;
    bool cpuopt_unsafe_reduce_fp_error;

    // Renderer
    Setting<RendererBackend> renderer_backend;
    bool renderer_debug;
    Setting<int> vulkan_device;

    Setting<u16> resolution_factor = Setting(static_cast<u16>(1));
    Setting<int> aspect_ratio;
    Setting<int> max_anisotropy;
    Setting<bool> use_frame_limit;
    Setting<u16> frame_limit;
    Setting<bool> use_disk_shader_cache;
    Setting<GPUAccuracy> gpu_accuracy;
    Setting<bool> use_asynchronous_gpu_emulation;
    Setting<bool> use_vsync;
    Setting<bool> use_assembly_shaders;
    Setting<bool> use_asynchronous_shaders;
    Setting<bool> use_fast_gpu_time;

    Setting<float> bg_red;
    Setting<float> bg_green;
    Setting<float> bg_blue;

    // System
    Setting<std::optional<u32>> rng_seed;
    // Measured in seconds since epoch
    Setting<std::optional<std::chrono::seconds>> custom_rtc;
    // Set on game boot, reset on stop. Seconds difference between current time and `custom_rtc`
    std::chrono::seconds custom_rtc_differential;

    s32 current_user;
    Setting<s32> language_index;
    Setting<s32> region_index;
    Setting<s32> time_zone_index;
    Setting<s32> sound_index;

    // Controls
    // std::array<PlayerInput, 10> players;

    // bool mouse_enabled;
    std::string mouse_device;
    // MouseButtonsRaw mouse_buttons;

    // bool keyboard_enabled;
    // KeyboardKeysRaw keyboard_keys;
    // KeyboardModsRaw keyboard_mods;

    // bool debug_pad_enabled;
    // ButtonsRaw debug_pad_buttons;
    // AnalogsRaw debug_pad_analogs;

    std::string motion_device;
    TouchscreenInput touchscreen;
    std::atomic_bool is_device_reload_pending{true};
    std::string udp_input_address;
    u16 udp_input_port;
    u8 udp_pad_index;

    bool use_docked_mode;

    // Data Storage
    bool use_virtual_sd;
    bool gamecard_inserted;
    bool gamecard_current_game;
    std::string gamecard_path;

    // Debugging
    bool record_frame_times;
    bool use_gdbstub;
    u16 gdbstub_port;
    std::string program_args;
    bool dump_exefs;
    bool dump_nso;
    bool reporting_services;
    bool quest_flag;
    bool disable_macro_jit;

    // Misceallaneous
    std::string log_filter;
    bool use_dev_keys;

    // Services
    std::string bcat_backend;
    bool bcat_boxcat_local;

    // WebService
    bool enable_telemetry;
    std::string web_api_url;
    std::string yuzu_username;
    std::string yuzu_token;

    // Add-Ons
    std::map<u64, std::vector<std::string>> disabled_addons;
*/
} // namespace PluginDefinitions