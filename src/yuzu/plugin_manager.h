// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <QDialog>

class QHBoxLayout;
class QListWidget;
class QVBoxLayout;
class QPushButton;
class QListWidgetItem;

class PluginDialog : public QDialog {
    Q_OBJECT

public:
    explicit PluginDialog(QWidget* parent);
    ~PluginDialog() override;

    void SignalClose();

private:
    void PluginEnabledOrDisabled(QListWidgetItem* changed);

    void UpdateAvailablePlugins();

    QString plugins_path;

    QListWidget* plugin_list;
    QPushButton* refresh_button;
    QFileSystemWatcher* filesystem_watcher;

    QVBoxLayout* main_layout;
};
