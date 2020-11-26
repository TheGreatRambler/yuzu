// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QCheckBox>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/tools/plugin_manager.h"
#include "yuzu/plugin_manager.h"
#include "yuzu/uisettings.h"

PluginDialog::PluginDialog(QWidget* parent) : QDialog(parent) {
    // Normalize path
    plugins_path =
        QDir::fromNativeSeparators(
            QString::fromStdString(Common::FS::GetUserPath(Common::FS::UserPath::PluginDir)))
            .toStdString();
    Common::FS::CreateDir(plugins_path);

    Core::System::GetInstance().PluginManager().SetPluginCallback(
        std::bind(&PluginDialog::UpdateAvailablePlugins, this));

    main_layout = new QVBoxLayout();

    plugin_list = new QListWidget(this);
    plugin_list->setObjectName(QStringLiteral("PluginList"));
    UpdateAvailablePlugins();

    refresh_button = new QPushButton(QStringLiteral("Refresh list"), this);
    refresh_button->setObjectName(QStringLiteral("RefreshButton"));

    plugins_enabled = new QCheckBox(QStringLiteral("Enable plugins"), this);
    plugins_enabled->setObjectName(QStringLiteral("EnablePlugins"));
    plugins_enabled->setChecked(
        Core::System::GetInstance().PluginManager().IsActive() ? Qt::Checked : Qt::Unchecked);

    filesystem_watcher = new QFileSystemWatcher(this);
    filesystem_watcher->addPath(QString::fromStdString(plugins_path));

    QObject::connect(refresh_button, &QPushButton::clicked, this, [this]() {
        // Just in case your system doesn't support file monitering
        UpdateAvailablePlugins();
    });

    QObject::connect(plugins_enabled, &QCheckBox::clicked, this, [this]() {
        Core::System::GetInstance().PluginManager().SetActive(plugins_enabled->isChecked());
    });

    QObject::connect(filesystem_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        LOG_DEBUG(Plugin_Manager, "Directory update detected, refreshing list");
        UpdateAvailablePlugins();
    });
    QObject::connect(plugin_list, &QListWidget::itemChanged, this,
                     &PluginDialog::PluginEnabledOrDisabled);

    main_layout->addWidget(plugin_list);
    main_layout->addWidget(refresh_button);
    main_layout->addWidget(plugins_enabled);

    setLayout(main_layout);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(QStringLiteral("Plugin Manager"));
}

PluginDialog::~PluginDialog() = default;

void PluginDialog::SignalClose() {
    Core::System::GetInstance().PluginManager().SetPluginCallback(nullptr);
}

void PluginDialog::PluginEnabledOrDisabled(QListWidgetItem* changed) {
    bool checked = changed->checkState() == Qt::Checked;
    std::string plugin_name = changed->text().toStdString();
    std::string path = plugins_path + plugin_name;

    if (checked) {
        if (!Core::System::GetInstance().PluginManager().LoadPlugin(path, plugin_name)) {
            // Error
            std::string last_error =
                Core::System::GetInstance().PluginManager().GetLastErrorString();
            std::string message =
                "Plugin " + plugin_name + " was not loaded with error: " + last_error;

            LOG_ERROR(Plugin_Manager, message.c_str());

            QMessageBox msgBox;
            msgBox.setWindowTitle(QStringLiteral("Plugin Manager"));
            msgBox.setText(QString::fromStdString(message));
            msgBox.setIcon(QMessageBox::Warning);
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setDefaultButton(QMessageBox::Ok);
            msgBox.exec();

            changed->setCheckState(Qt::Unchecked);
        } else {
            LOG_INFO(Plugin_Manager, (plugin_name + " successfully loaded").c_str());
        }

    } else {
        Core::System::GetInstance().PluginManager().RemovePlugin(path);
        LOG_INFO(Plugin_Manager, (plugin_name + " successfully removed").c_str());
    }
}

void PluginDialog::UpdateAvailablePlugins() {
    static QString required_prefix = QStringLiteral("plugin_");
    if (Common::FS::Exists(plugins_path)) {
        plugin_list->clear();

        QDirIterator plugins(QString::fromStdString(plugins_path),
                             QStringList() << QStringLiteral("*.dll") << QStringLiteral("*.so")
                                           << QStringLiteral("*.dylib"),
                             QDir::Files, QDirIterator::Subdirectories);
        while (plugins.hasNext()) {
            QString available_path = QDir::fromNativeSeparators(plugins.next());
            QString name =
                available_path.replace(QString::fromStdString(plugins_path), QStringLiteral(""));

            if (name.startsWith(required_prefix)) {
                LOG_DEBUG(Plugin_Manager, (name.toStdString() + " starts with " +
                                           required_prefix.toStdString() + ", is a plugin")
                                              .c_str());
                QListWidgetItem* item = new QListWidgetItem(name);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Unchecked);
                plugin_list->addItem(item);
            }
        }

        auto const& loadedPlugins =
            Core::System::GetInstance().PluginManager().GetAllLoadedPlugins();
        for (auto const& loadedPlugin : loadedPlugins) {
            auto const& foundItems = plugin_list->findItems(
                QString::fromStdString(loadedPlugin)
                    .replace(QString::fromStdString(plugins_path), QStringLiteral("")),
                Qt::MatchExactly);
            if (!foundItems.empty()) {
                // Only one plugin should match the criteria
                foundItems[0]->setCheckState(Qt::Checked);
            }
        }
    } else {
        LOG_INFO(Plugin_Manager, ("Plugin path " + plugins_path + " does not exist").c_str());
    }
}