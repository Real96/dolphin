// Copyright 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>

class QListWidget;
class QPushButton;
class QShowEvent;

namespace Core
{
enum class State;
class System;
}  // namespace Core

// Small dialog that lists the .lua scripts found in Sys/Scripts and lets the
// user start/stop them while a game is running. The scripting engine itself
// lives in Core/Lua.
class LuaScriptWindow : public QDialog
{
  Q_OBJECT
public:
  explicit LuaScriptWindow(Core::System& system, QWidget* parent = nullptr);

protected:
  void showEvent(QShowEvent* event) override;

private:
  void CreateWidgets();
  void ConnectWidgets();

  void RefreshScriptList();
  void OnStart();
  void OnStop();
  void OnStateChanged(Core::State state);
  void UpdateButtons();

  Core::System& m_system;

  QListWidget* m_script_list = nullptr;
  QPushButton* m_refresh_button = nullptr;
  QPushButton* m_start_button = nullptr;
  QPushButton* m_stop_button = nullptr;
};
