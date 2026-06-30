// Copyright 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Lua scripting support.
//
// Originally written by dragonbane0 for the wxWidgets-based Dolphin
// (https://github.com/dragonbane0/dolphin) and refined in
// https://github.com/SwareJonge/Dolphin-Lua-Core. Modernized here for the
// current Dolphin codebase (Core::System, DolphinQt, Lua 5.3.1).
//
// This header is intentionally free of any <lua.hpp> include so that UI code
// (DolphinQt) and the input pipeline can drive scripts without pulling in the
// Lua C headers. All interaction with the Lua C API lives in Lua.cpp.

#pragma once

#include <string>

struct GCPadStatus;

namespace Lua
{
// Initializes the scripting subsystem and auto-launches any script in the
// Sys/Scripts directory whose file name starts with '_'. Called when emulation
// starts.
void Init();

// Terminates every running script and tears down the subsystem. Called when
// emulation stops.
void Shutdown();

// Queues a script (by file name, relative to Sys/Scripts) to be started on the
// next input frame.
void LoadScript(const std::string& file_name);

// Requests that a running script be cancelled. The onScriptCancel callback runs
// before the Lua state is closed.
void TerminateScript(const std::string& file_name);

// Returns true if the named script is currently loaded/running.
bool IsScriptRunning(const std::string& file_name);

// Absolute path of the directory scripts are loaded from (Sys/Scripts).
std::string GetScriptsDirectory();

// Runs one update tick of every loaded script. Called once per polled
// GameCube input frame from the CPU thread; scripts may mutate pad_status to
// inject input.
void UpdateScripts(GCPadStatus* pad_status);
}  // namespace Lua
