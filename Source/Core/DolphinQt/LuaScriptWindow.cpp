// Copyright 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/LuaScriptWindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"

#include "Core/Core.h"
#include "Core/Lua/Lua.h"
#include "Core/System.h"

#include "DolphinQt/QtUtils/ModalMessageBox.h"
#include "DolphinQt/Settings.h"

LuaScriptWindow::LuaScriptWindow(Core::System& system, QWidget* parent)
    : QDialog(parent), m_system(system)
{
  setWindowTitle(tr("Lua Scripts"));

  CreateWidgets();
  ConnectWidgets();

  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this,
          &LuaScriptWindow::OnStateChanged);

  RefreshScriptList();
  UpdateButtons();
}

void LuaScriptWindow::CreateWidgets()
{
  m_script_list = new QListWidget;
  m_script_list->setSelectionMode(QAbstractItemView::SingleSelection);

  m_refresh_button = new QPushButton(tr("Refresh"));
  m_start_button = new QPushButton(tr("Start"));
  m_stop_button = new QPushButton(tr("Stop"));

  auto* button_layout = new QHBoxLayout;
  button_layout->addWidget(m_refresh_button);
  button_layout->addStretch();
  button_layout->addWidget(m_start_button);
  button_layout->addWidget(m_stop_button);

  auto* main_layout = new QVBoxLayout;
  main_layout->addWidget(
      new QLabel(tr("Scripts are loaded from the Sys/Scripts folder. Names starting with '_' are "
                    "launched automatically when a game starts.")));
  main_layout->addWidget(m_script_list);
  main_layout->addLayout(button_layout);

  setLayout(main_layout);
  resize(450, 300);
}

void LuaScriptWindow::ConnectWidgets()
{
  connect(m_refresh_button, &QPushButton::clicked, this, &LuaScriptWindow::RefreshScriptList);
  connect(m_start_button, &QPushButton::clicked, this, &LuaScriptWindow::OnStart);
  connect(m_stop_button, &QPushButton::clicked, this, &LuaScriptWindow::OnStop);
  connect(m_script_list, &QListWidget::itemDoubleClicked, this, &LuaScriptWindow::OnStart);
  connect(m_script_list, &QListWidget::itemSelectionChanged, this, &LuaScriptWindow::UpdateButtons);
}

void LuaScriptWindow::showEvent(QShowEvent* event)
{
  RefreshScriptList();
  UpdateButtons();
  QDialog::showEvent(event);
}

void LuaScriptWindow::RefreshScriptList()
{
  // Remember the selection so a refresh doesn't lose the user's place.
  QString previous;
  if (QListWidgetItem* item = m_script_list->currentItem())
    previous = item->data(Qt::UserRole).toString();

  m_script_list->clear();

  for (const std::string& path : Common::DoFileSearch(Lua::GetScriptsDirectory(), ".lua"))
  {
    std::string name;
    Common::SplitPath(path, nullptr, &name, nullptr);

    // Files prefixed with '_' are auto-launched by the engine; don't list them.
    if (name.empty() || name.front() == '_')
      continue;

    const std::string file_name = name + ".lua";
    const QString q_file_name = QString::fromStdString(file_name);

    QString label = q_file_name;
    if (Lua::IsScriptRunning(file_name))
      label += tr(" (running)");

    auto* item = new QListWidgetItem(label);
    item->setData(Qt::UserRole, q_file_name);
    m_script_list->addItem(item);

    if (q_file_name == previous)
      m_script_list->setCurrentItem(item);
  }

  UpdateButtons();
}

void LuaScriptWindow::OnStart()
{
  if (Core::GetState(m_system) != Core::State::Running &&
      Core::GetState(m_system) != Core::State::Paused)
  {
    ModalMessageBox::warning(this, tr("Lua Scripts"),
                             tr("A game must be running to start a script."));
    return;
  }

  QListWidgetItem* item = m_script_list->currentItem();
  if (item == nullptr)
  {
    ModalMessageBox::warning(this, tr("Lua Scripts"), tr("No script selected."));
    return;
  }

  const std::string file_name = item->data(Qt::UserRole).toString().toStdString();

  if (!File::Exists(Lua::GetScriptsDirectory() + file_name))
  {
    ModalMessageBox::warning(this, tr("Lua Scripts"),
                             tr("The selected script no longer exists."));
    RefreshScriptList();
    return;
  }

  if (Lua::IsScriptRunning(file_name))
  {
    ModalMessageBox::information(this, tr("Lua Scripts"), tr("That script is already running."));
    return;
  }

  Lua::LoadScript(file_name);
  RefreshScriptList();
}

void LuaScriptWindow::OnStop()
{
  QListWidgetItem* item = m_script_list->currentItem();
  if (item == nullptr)
    return;

  const std::string file_name = item->data(Qt::UserRole).toString().toStdString();

  if (!Lua::IsScriptRunning(file_name))
  {
    ModalMessageBox::information(this, tr("Lua Scripts"), tr("That script is not running."));
    return;
  }

  Lua::TerminateScript(file_name);
  RefreshScriptList();
}

void LuaScriptWindow::OnStateChanged(Core::State)
{
  // Scripts are torn down when emulation stops, so refresh the "(running)"
  // markers and re-evaluate which buttons should be enabled.
  RefreshScriptList();
  UpdateButtons();
}

void LuaScriptWindow::UpdateButtons()
{
  const Core::State state = Core::GetState(m_system);
  const bool game_running =
      state == Core::State::Running || state == Core::State::Paused;
  const bool has_selection = m_script_list->currentItem() != nullptr;

  m_start_button->setEnabled(game_running && has_selection);
  m_stop_button->setEnabled(game_running && has_selection);
}
