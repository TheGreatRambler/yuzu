// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QDialog>

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
    void updateAvailablePlugins();

    QListWidget* plugin_list;

    QVBoxLayout* main_layout;
};
