// Copyright 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Lua/Lua.h"

#include <atomic>
#include <bit>
#include <cstring>
#include <list>
#include <mutex>
#include <string>
#include <vector>

#include <lua.hpp>

#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/MsgHandler.h"
#include "Common/StringUtil.h"

#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/Memmap.h"
#include "Core/Movie.h"
#include "Core/State.h"
#include "Core/System.h"

#include "InputCommon/GCPadStatus.h"
#include "VideoCommon/OnScreenDisplay.h"

namespace Lua
{
namespace
{
struct StateEvent
{
  bool do_save = false;
  bool use_slot = false;
  int slot_id = 0;
  std::string file_name;
};

struct LuaScript
{
  std::string file_name;
  lua_State* lua_state = nullptr;
  bool has_started = false;
  bool requested_termination = false;
  bool wants_savestate_callback = false;
};

// All scripts currently loaded. UpdateScripts runs on the CPU thread, while
// LoadScript/TerminateScript/IsScriptRunning are also called from the UI thread
// (the Lua Scripts dialog), so structural access is guarded by this mutex.
//
// The mutex is NOT re-entrant: helpers invoked from Lua during UpdateScripts
// (CancelScript, RequestSavestateCallback) deliberately touch s_script_list
// without locking, since UpdateScripts already holds the lock on the same thread.
std::list<LuaScript> s_script_list;
std::mutex s_script_list_mutex;

// Index of the script being updated, so wrapper functions invoked from Lua know
// which script called them. -1 when no script is currently executing.
int s_current_script_id = -1;

// Working copy of the local pad, mutated by the input-injection wrappers and
// written back to the SI device at the end of UpdateScripts.
GCPadStatus s_pad_local{};

// Savestate coordination. A script requests a save/load from the CPU thread;
// the actual State::Save/Load is deferred to the host thread (it is unsafe to
// tear down/rebuild emulation state from inside input polling). When the host
// finishes, the matching "done" flag is set and the next UpdateScripts fires the
// script's onStateSaved/onStateLoaded callback.
StateEvent s_state_event;
std::atomic<bool> s_in_state_operation{false};
std::atomic<bool> s_state_saved{false};
std::atomic<bool> s_state_loaded{false};

// Set/clear bits in the local pad's button field (explicit casts keep the
// integer-promotion result from tripping narrowing warnings).
void SetPadButton(int mask)
{
  s_pad_local.button = static_cast<u16>(s_pad_local.button | mask);
}
void ClearPadButton(int mask)
{
  s_pad_local.button = static_cast<u16>(s_pad_local.button & ~mask);
}

// Maps a Lua button name to its PAD_* mask (0 for an unknown name).
int ButtonMaskFromName(const std::string& name)
{
  if (name == "A")
    return PAD_BUTTON_A;
  if (name == "B")
    return PAD_BUTTON_B;
  if (name == "X")
    return PAD_BUTTON_X;
  if (name == "Y")
    return PAD_BUTTON_Y;
  if (name == "Z")
    return PAD_TRIGGER_Z;
  if (name == "L")
    return PAD_TRIGGER_L;
  if (name == "R")
    return PAD_TRIGGER_R;
  if (name == "Start")
    return PAD_BUTTON_START;
  if (name == "D-Up")
    return PAD_BUTTON_UP;
  if (name == "D-Down")
    return PAD_BUTTON_DOWN;
  if (name == "D-Left")
    return PAD_BUTTON_LEFT;
  if (name == "D-Right")
    return PAD_BUTTON_RIGHT;
  return 0;
}

// ---------------------------------------------------------------------------
// Pointer helpers
// ---------------------------------------------------------------------------

bool IsInMemArea(u32 pointer)
{
  return (pointer > 0x80000000 && pointer < 0x81800000) ||
         (pointer > 0x90000000 && pointer < 0x94000000) ||
         (pointer > 0x00000000 && pointer < 0x01800000) ||
         (pointer > 0x10000000 && pointer < 0x14000000);
}

u32 NormalizePointer(u32 pointer)
{
  if ((pointer > 0x80000000 && pointer < 0x81800000) ||
      (pointer > 0x90000000 && pointer < 0x94000000))
  {
    pointer -= 0x80000000;
  }
  return pointer;
}

u32 ReadPointer(u32 start_address, u32 offset)
{
  auto& system = Core::System::GetInstance();
  const u32 pointer = system.GetMemory().Read_U32(start_address) + offset;

  if (!IsInMemArea(pointer))
    return 0;

  return NormalizePointer(pointer);
}

// Walks a multi-level pointer described by the Lua arguments: argument 1 is the
// base address, arguments 2..n are successive offsets. Returns 0 if any hop
// leaves valid memory.
u32 ExecuteMultilevelLoop(lua_State* L)
{
  const int argc = lua_gettop(L);
  u32 pointer = static_cast<u32>(lua_tointeger(L, 1));

  for (int i = 2; i <= argc; ++i)
  {
    const u32 offset = static_cast<u32>(lua_tointeger(L, i));
    pointer = ReadPointer(pointer, offset);
    if (pointer == 0 || pointer == offset)
      return 0;
  }
  return pointer;
}

// ---------------------------------------------------------------------------
// Registered C functions (callable from Lua scripts)
// ---------------------------------------------------------------------------

int ReadValue8(lua_State* L)
{
  const int argc = lua_gettop(L);
  if (argc < 1)
    return 0;

  auto& memory = Core::System::GetInstance().GetMemory();
  u8 result = 0;
  if (argc < 2)
    result = memory.Read_U8(static_cast<u32>(lua_tointeger(L, 1)));
  else if (const u32 address = ExecuteMultilevelLoop(L); address != 0)
    result = memory.Read_U8(address);

  lua_pushinteger(L, result);
  return 1;
}

int ReadValue16(lua_State* L)
{
  const int argc = lua_gettop(L);
  if (argc < 1)
    return 0;

  auto& memory = Core::System::GetInstance().GetMemory();
  u16 result = 0;
  if (argc < 2)
    result = memory.Read_U16(static_cast<u32>(lua_tointeger(L, 1)));
  else if (const u32 address = ExecuteMultilevelLoop(L); address != 0)
    result = memory.Read_U16(address);

  lua_pushinteger(L, result);
  return 1;
}

int ReadValue32(lua_State* L)
{
  const int argc = lua_gettop(L);
  if (argc < 1)
    return 0;

  auto& memory = Core::System::GetInstance().GetMemory();
  u32 result = 0;
  if (argc < 2)
    result = memory.Read_U32(static_cast<u32>(lua_tointeger(L, 1)));
  else if (const u32 address = ExecuteMultilevelLoop(L); address != 0)
    result = memory.Read_U32(address);

  lua_pushinteger(L, result);
  return 1;
}

int ReadValueFloat(lua_State* L)
{
  const int argc = lua_gettop(L);
  if (argc < 1)
    return 0;

  auto& memory = Core::System::GetInstance().GetMemory();
  u32 raw = 0;
  if (argc < 2)
    raw = memory.Read_U32(static_cast<u32>(lua_tointeger(L, 1)));
  else if (const u32 address = ExecuteMultilevelLoop(L); address != 0)
    raw = memory.Read_U32(address);

  lua_pushnumber(L, std::bit_cast<float>(raw));
  return 1;
}

int ReadValueString(lua_State* L)
{
  const int argc = lua_gettop(L);
  if (argc < 2)
    return 0;

  const u32 address = static_cast<u32>(lua_tointeger(L, 1));
  const size_t count = static_cast<size_t>(lua_tointeger(L, 2));

  const std::string result = Core::System::GetInstance().GetMemory().GetString(address, count);

  lua_pushstring(L, result.c_str());
  return 1;
}

int WriteValue8(lua_State* L)
{
  auto& system = Core::System::GetInstance();
  if (system.GetMovie().IsPlayingInput())
    return 0;
  if (lua_gettop(L) < 2)
    return 0;

  const u32 address = static_cast<u32>(lua_tointeger(L, 1));
  const u8 value = static_cast<u8>(lua_tointeger(L, 2));
  system.GetMemory().Write_U8(value, address);
  return 0;
}

int WriteValue16(lua_State* L)
{
  auto& system = Core::System::GetInstance();
  if (system.GetMovie().IsPlayingInput())
    return 0;
  if (lua_gettop(L) < 2)
    return 0;

  const u32 address = static_cast<u32>(lua_tointeger(L, 1));
  const u16 value = static_cast<u16>(lua_tointeger(L, 2));
  system.GetMemory().Write_U16(value, address);
  return 0;
}

int WriteValue32(lua_State* L)
{
  auto& system = Core::System::GetInstance();
  if (system.GetMovie().IsPlayingInput())
    return 0;
  if (lua_gettop(L) < 2)
    return 0;

  const u32 address = static_cast<u32>(lua_tointeger(L, 1));
  const u32 value = static_cast<u32>(lua_tointeger(L, 2));
  system.GetMemory().Write_U32(value, address);
  return 0;
}

int WriteValueFloat(lua_State* L)
{
  auto& system = Core::System::GetInstance();
  if (system.GetMovie().IsPlayingInput())
    return 0;
  if (lua_gettop(L) < 2)
    return 0;

  const u32 address = static_cast<u32>(lua_tointeger(L, 1));
  const float value = static_cast<float>(lua_tonumber(L, 2));
  system.GetMemory().Write_U32(std::bit_cast<u32>(value), address);
  return 0;
}

int WriteValueString(lua_State* L)
{
  auto& system = Core::System::GetInstance();
  if (system.GetMovie().IsPlayingInput())
    return 0;
  if (lua_gettop(L) < 2)
    return 0;

  const u32 address = static_cast<u32>(lua_tointeger(L, 1));
  size_t length = 0;
  const char* value = lua_tolstring(L, 2, &length);
  if (value != nullptr && length > 0)
    system.GetMemory().CopyToEmu(address, value, length);
  return 0;
}

int GetPointerNormal(lua_State* L)
{
  const int argc = lua_gettop(L);
  if (argc < 1)
    return 0;

  u32 pointer = 0;
  if (argc < 2)
    pointer = ReadPointer(static_cast<u32>(lua_tointeger(L, 1)), 0x0);
  else
    pointer = ExecuteMultilevelLoop(L);

  lua_pushinteger(L, pointer);
  return 1;
}

int GetGameID(lua_State* L)
{
  lua_pushstring(L, SConfig::GetInstance().GetGameID().c_str());
  return 1;
}

int GetScriptsDir(lua_State* L)
{
  lua_pushstring(L, GetScriptsDirectory().c_str());
  return 1;
}

int PressButton(lua_State* L)
{
  if (Core::System::GetInstance().GetMovie().IsPlayingInput())
    return 0;
  if (lua_gettop(L) < 1)
    return 0;

  const std::string button = lua_tostring(L, 1);
  SetPadButton(ButtonMaskFromName(button));

  // Buttons with an associated analog value.
  if (button == "A")
    s_pad_local.analogA = 0xFF;
  else if (button == "B")
    s_pad_local.analogB = 0xFF;
  else if (button == "L")
    s_pad_local.triggerLeft = 255;
  else if (button == "R")
    s_pad_local.triggerRight = 255;
  return 0;
}

int ReleaseButton(lua_State* L)
{
  if (Core::System::GetInstance().GetMovie().IsPlayingInput())
    return 0;
  if (lua_gettop(L) < 1)
    return 0;

  const std::string button = lua_tostring(L, 1);
  ClearPadButton(ButtonMaskFromName(button));

  if (button == "A")
    s_pad_local.analogA = 0x00;
  else if (button == "B")
    s_pad_local.analogB = 0x00;
  else if (button == "L")
    s_pad_local.triggerLeft = 0;
  else if (button == "R")
    s_pad_local.triggerRight = 0;
  return 0;
}

int SetMainStickX(lua_State* L)
{
  if (Core::System::GetInstance().GetMovie().IsPlayingInput())
    return 0;
  if (lua_gettop(L) < 1)
    return 0;
  s_pad_local.stickX = static_cast<u8>(lua_tointeger(L, 1));
  return 0;
}

int SetMainStickY(lua_State* L)
{
  if (Core::System::GetInstance().GetMovie().IsPlayingInput())
    return 0;
  if (lua_gettop(L) < 1)
    return 0;
  s_pad_local.stickY = static_cast<u8>(lua_tointeger(L, 1));
  return 0;
}

int SetCStickX(lua_State* L)
{
  if (Core::System::GetInstance().GetMovie().IsPlayingInput())
    return 0;
  if (lua_gettop(L) < 1)
    return 0;
  s_pad_local.substickX = static_cast<u8>(lua_tointeger(L, 1));
  return 0;
}

int SetCStickY(lua_State* L)
{
  if (Core::System::GetInstance().GetMovie().IsPlayingInput())
    return 0;
  if (lua_gettop(L) < 1)
    return 0;
  s_pad_local.substickY = static_cast<u8>(lua_tointeger(L, 1));
  return 0;
}

// Marks the currently-executing script as awaiting a savestate callback.
void RequestSavestateCallback()
{
  int n = 0;
  for (auto& script : s_script_list)
  {
    if (s_current_script_id == n)
    {
      script.wants_savestate_callback = true;
      break;
    }
    ++n;
  }
}

// Queues the actual State::Save/Load onto the host thread.
void DispatchStateEvent()
{
  s_state_saved.store(false, std::memory_order_relaxed);
  s_state_loaded.store(false, std::memory_order_relaxed);
  s_in_state_operation.store(true, std::memory_order_relaxed);

  RequestSavestateCallback();

  const StateEvent event = s_state_event;
  Core::QueueHostJob(
      [event](Core::System& system) {
        if (event.do_save)
        {
          if (event.use_slot)
            State::Save(system, event.slot_id);
          else
            State::SaveAs(system, File::GetUserPath(D_STATESAVES_IDX) + event.file_name);
          s_state_saved.store(true, std::memory_order_release);
        }
        else
        {
          if (event.use_slot)
            State::Load(system, event.slot_id);
          else
            State::LoadAs(system, File::GetUserPath(D_STATESAVES_IDX) + event.file_name);
          s_state_loaded.store(true, std::memory_order_release);
        }
      },
      false);
}

int SaveState(lua_State* L)
{
  if (lua_gettop(L) < 2)
    return 0;

  s_state_event = StateEvent{};
  s_state_event.do_save = true;
  if (lua_toboolean(L, 1))
  {
    s_state_event.use_slot = true;
    s_state_event.slot_id = static_cast<int>(lua_tointeger(L, 2));
  }
  else
  {
    s_state_event.file_name = lua_tostring(L, 2);
  }

  DispatchStateEvent();
  return 0;
}

int LoadState(lua_State* L)
{
  if (Core::System::GetInstance().GetMovie().IsPlayingInput())
    return 0;
  if (lua_gettop(L) < 2)
    return 0;

  s_state_event = StateEvent{};
  s_state_event.do_save = false;
  if (lua_toboolean(L, 1))
  {
    s_state_event.use_slot = true;
    s_state_event.slot_id = static_cast<int>(lua_tointeger(L, 2));
  }
  else
  {
    s_state_event.file_name = lua_tostring(L, 2);
  }

  DispatchStateEvent();
  return 0;
}

int GetFrameCount(lua_State* L)
{
  lua_pushinteger(L,
                  static_cast<lua_Integer>(Core::System::GetInstance().GetMovie().GetCurrentFrame()));
  return 1;
}

int GetInputFrameCount(lua_State* L)
{
  lua_pushinteger(
      L, static_cast<lua_Integer>(Core::System::GetInstance().GetMovie().GetCurrentInputCount() + 1));
  return 1;
}

int SetScreenText(lua_State* L)
{
  if (lua_gettop(L) < 1)
    return 0;

  // A dedicated message type means each per-frame call replaces the previous
  // text in place instead of stacking up on screen.
  OSD::AddTypedMessage(OSD::MessageType::Script, lua_tostring(L, 1), OSD::Duration::NORMAL,
                       OSD::Color::GREEN);
  return 0;
}

int PauseEmulation(lua_State*)
{
  auto& system = Core::System::GetInstance();
  Core::SetState(system, Core::State::Paused);
  return 0;
}

// The old RAM-watch overlay this used to toggle no longer exists in Dolphin.
// Kept registered (as a no-op) so existing scripts that call it don't error.
int SetInfoDisplay(lua_State*)
{
  return 0;
}

int SetFrameAndAudioDump(lua_State* L)
{
  const bool enable = lua_toboolean(L, 1) != 0;
  Config::SetBaseOrCurrent(Config::MAIN_MOVIE_DUMP_FRAMES, enable);
  Config::SetBaseOrCurrent(Config::MAIN_DUMP_AUDIO, enable);
  return 0;
}

int MsgBox(lua_State* L)
{
  const int argc = lua_gettop(L);
  if (argc < 1)
    return 0;

  const char* text = lua_tostring(L, 1);
  int delay = 5000;  // Default: 5 seconds
  if (argc >= 2)
    delay = static_cast<int>(lua_tointeger(L, 2));

  Core::DisplayMessage(std::string("Lua Msg: ") + text, delay);
  return 0;
}

int CancelScript(lua_State*)
{
  int n = 0;
  for (auto& script : s_script_list)
  {
    if (s_current_script_id == n)
    {
      script.requested_termination = true;
      break;
    }
    ++n;
  }
  return 0;
}

void HandleLuaErrors(lua_State* L, int status)
{
  if (status != 0)
  {
    PanicAlertFmt("Lua Error: {}", lua_tostring(L, -1));
    lua_pop(L, 1);  // remove error message
  }
}

void RegisterGeneralLuaFunctions(lua_State* lua_state)
{
  lua_register(lua_state, "ReadValue8", ReadValue8);
  lua_register(lua_state, "ReadValue16", ReadValue16);
  lua_register(lua_state, "ReadValue32", ReadValue32);
  lua_register(lua_state, "ReadValueFloat", ReadValueFloat);
  lua_register(lua_state, "ReadValueString", ReadValueString);
  lua_register(lua_state, "GetPointerNormal", GetPointerNormal);

  lua_register(lua_state, "WriteValue8", WriteValue8);
  lua_register(lua_state, "WriteValue16", WriteValue16);
  lua_register(lua_state, "WriteValue32", WriteValue32);
  lua_register(lua_state, "WriteValueFloat", WriteValueFloat);
  lua_register(lua_state, "WriteValueString", WriteValueString);

  lua_register(lua_state, "GetGameID", GetGameID);
  lua_register(lua_state, "GetScriptsDir", GetScriptsDir);

  lua_register(lua_state, "PressButton", PressButton);
  lua_register(lua_state, "ReleaseButton", ReleaseButton);
  lua_register(lua_state, "SetMainStickX", SetMainStickX);
  lua_register(lua_state, "SetMainStickY", SetMainStickY);
  lua_register(lua_state, "SetCStickX", SetCStickX);
  lua_register(lua_state, "SetCStickY", SetCStickY);

  lua_register(lua_state, "SaveState", SaveState);
  lua_register(lua_state, "LoadState", LoadState);

  lua_register(lua_state, "GetFrameCount", GetFrameCount);
  lua_register(lua_state, "GetInputFrameCount", GetInputFrameCount);
  lua_register(lua_state, "MsgBox", MsgBox);

  lua_register(lua_state, "SetScreenText", SetScreenText);
  lua_register(lua_state, "PauseEmulation", PauseEmulation);
  lua_register(lua_state, "SetInfoDisplay", SetInfoDisplay);
  lua_register(lua_state, "SetFrameAndAudioDump", SetFrameAndAudioDump);
}

// Runs a global Lua function with no arguments, reporting and propagating
// errors. Returns the Lua status code (0 on success).
int CallGlobal(lua_State* lua_state, const char* function_name)
{
  lua_getglobal(lua_state, function_name);
  const int status = lua_pcall(lua_state, 0, LUA_MULTRET, 0);
  if (status != 0)
    HandleLuaErrors(lua_state, status);
  return status;
}
}  // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

std::string GetScriptsDirectory()
{
  return File::GetSysDirectory() + "Scripts" DIR_SEP;
}

void Init()
{
  std::memset(&s_pad_local, 0, sizeof(s_pad_local));
  s_in_state_operation.store(false, std::memory_order_relaxed);

  // Auto-launch every script whose file name starts with '_'.
  const std::string scripts_dir = GetScriptsDirectory();
  const std::vector<std::string> filenames = Common::DoFileSearch(scripts_dir, ".lua");

  for (const std::string& path : filenames)
  {
    std::string name;
    Common::SplitPath(path, nullptr, &name, nullptr);
    if (!name.empty() && name.front() == '_')
      LoadScript(name + ".lua");
  }
}

void Shutdown()
{
  std::lock_guard lock(s_script_list_mutex);
  for (LuaScript& script : s_script_list)
  {
    if (script.has_started)
      lua_close(script.lua_state);
  }
  s_script_list.clear();
  s_current_script_id = -1;
}

void LoadScript(const std::string& file_name)
{
  std::lock_guard lock(s_script_list_mutex);
  LuaScript script;
  script.file_name = file_name;
  s_script_list.push_back(std::move(script));
}

void TerminateScript(const std::string& file_name)
{
  std::lock_guard lock(s_script_list_mutex);
  for (LuaScript& script : s_script_list)
  {
    if (script.file_name == file_name)
    {
      script.requested_termination = true;
      break;
    }
  }
}

bool IsScriptRunning(const std::string& file_name)
{
  std::lock_guard lock(s_script_list_mutex);
  for (const LuaScript& script : s_script_list)
  {
    if (script.file_name == file_name)
      return true;
  }
  return false;
}

void UpdateScripts(GCPadStatus* pad_status)
{
  auto& system = Core::System::GetInstance();
  if (Core::GetState(system) != Core::State::Running)
    return;

  std::lock_guard lock(s_script_list_mutex);

  // Start from the real pad so unscripted buttons pass through.
  s_pad_local = *pad_status;

  int n = 0;
  auto it = s_script_list.begin();
  while (it != s_script_list.end())
  {
    int status = 0;
    s_current_script_id = n;

    if (!it->has_started)
    {
      // Start the script: new Lua state, standard libs, our C functions.
      it->lua_state = luaL_newstate();
      luaL_openlibs(it->lua_state);
      RegisterGeneralLuaFunctions(it->lua_state);
      lua_register(it->lua_state, "CancelScript", CancelScript);

      const std::string file = GetScriptsDirectory() + it->file_name;
      status = luaL_dofile(it->lua_state, file.c_str());
      if (status == 0)
        status = CallGlobal(it->lua_state, "onScriptStart");

      if (status != 0)
      {
        lua_close(it->lua_state);
        it = s_script_list.erase(it);
        --n;
      }
      else
      {
        it->has_started = true;
      }
    }
    else if (it->requested_termination)
    {
      CallGlobal(it->lua_state, "onScriptCancel");
      lua_close(it->lua_state);
      status = -1;
      it = s_script_list.erase(it);
      --n;
    }
    else
    {
      // Fire any pending savestate callback first so onScriptUpdate can react.
      if (it->wants_savestate_callback && s_in_state_operation.load(std::memory_order_acquire))
      {
        const char* callback = nullptr;
        if (s_state_saved.load(std::memory_order_acquire))
          callback = "onStateSaved";
        else if (s_state_loaded.load(std::memory_order_acquire))
          callback = "onStateLoaded";

        if (callback != nullptr)
        {
          it->wants_savestate_callback = false;
          status = CallGlobal(it->lua_state, callback);

          s_in_state_operation.store(false, std::memory_order_relaxed);
          s_state_saved.store(false, std::memory_order_relaxed);
          s_state_loaded.store(false, std::memory_order_relaxed);

          if (status != 0)
          {
            lua_close(it->lua_state);
            it = s_script_list.erase(it);
            --n;
          }
        }
      }

      if (status == 0)
      {
        status = CallGlobal(it->lua_state, "onScriptUpdate");
        if (status != 0)
        {
          lua_close(it->lua_state);
          it = s_script_list.erase(it);
          --n;
        }
      }
    }

    if (status == 0)
      ++it;
    ++n;
  }

  s_current_script_id = -1;

  // Send the (possibly script-modified) pad back to the SI device.
  *pad_status = s_pad_local;
}
}  // namespace Lua
