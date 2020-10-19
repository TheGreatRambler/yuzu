// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <QDialog>
#include <QFileSystemWatcher>

class QHBoxLayout;
class QListWidget;
class QVBoxLayout;
class QPushButton;

class PluginDialog : public QDialog {
    Q_OBJECT

public:
    explicit PluginDialog(QWidget* parent);
    ~PluginDialog() override;

private:
    void pluginEnabledOrDisabled(QListWidgetItem* changed);

    void updateAvailablePlugins();

    const QString plugins_path = QCoreApplication::applicationDirPath() + tr("/yuzu_plugins");

    QListWidget* plugin_list;
    QFileSystemWatcher filesystem_watcher;

    QVBoxLayout* main_layout;
};
