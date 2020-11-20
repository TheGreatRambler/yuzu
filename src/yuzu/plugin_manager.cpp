// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

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
#include "common/logging/log.h"
#include "core/core.h"
#include "core/tools/plugin_manager.h"
#include "yuzu/plugin_manager.h"
#include "yuzu/uisettings.h"

PluginDialog::PluginDialog(QWidget* parent) : QDialog(parent) {
    plugins_path = QCoreApplication::applicationDirPath() + QStringLiteral("/yuzu_plugins/");

    Core::System::GetInstance().PluginManager().SetPluginCallback(
        std::bind(&PluginDialog::updateAvailablePlugins, this));

    main_layout = new QVBoxLayout();

    plugin_list = new QListWidget(this);
    plugin_list->setObjectName(QStringLiteral("PluginList"));
    updateAvailablePlugins();

    refresh_button = new QPushButton(QStringLiteral("Refresh list"), this);
    refresh_button->setObjectName(QStringLiteral("RefreshButton"));

    filesystem_watcher = new QFileSystemWatcher(this);
    filesystem_watcher->addPath(plugins_path);

    QObject::connect(refresh_button, &QPushButton::clicked, this, [this]() {
        // filesystem_watcher->addPath(plugins_path);
        updateAvailablePlugins();
    });

    QObject::connect(filesystem_watcher, &QFileSystemWatcher::directoryChanged, this,
                     [this] { updateAvailablePlugins(); });
    QObject::connect(plugin_list, &QListWidget::itemChanged, this,
                     &PluginDialog::pluginEnabledOrDisabled);

    main_layout->addWidget(plugin_list);
    main_layout->addWidget(refresh_button);

    setLayout(main_layout);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(QStringLiteral("Plugin Manager"));
}

PluginDialog::~PluginDialog() = default;

void PluginDialog::SignalClose() {
    Core::System::GetInstance().PluginManager().SetPluginCallback(nullptr);
}

void PluginDialog::pluginEnabledOrDisabled(QListWidgetItem* changed) {
    bool checked = changed->checkState() == Qt::Checked;
    std::string path = QString(plugins_path + changed->text()).toStdString();

    if (checked) {
        if (!Core::System::GetInstance().PluginManager().LoadPlugin(path)) {
            // Error
            std::string lastError =
                Core::System::GetInstance().PluginManager().GetLastErrorString();
            std::string message = "Plugin " + path + " was not loaded with error: " + lastError;

            LOG_ERROR(Plugin, message.c_str());

            QMessageBox msgBox;
            msgBox.setWindowTitle(QStringLiteral("Plugin Manager"));
            msgBox.setText(QString::fromStdString(message));
            msgBox.setIcon(QMessageBox::Warning);
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setDefaultButton(QMessageBox::Ok);
            msgBox.exec();

            changed->setCheckState(Qt::Unchecked);
        } else {
            LOG_INFO(Plugin, (changed->text().toStdString() + " successfully loaded").c_str());
        }

    } else {
        Core::System::GetInstance().PluginManager().RemovePlugin(path);
        LOG_INFO(Plugin, (changed->text().toStdString() + " successfully removed").c_str());
    }
}

void PluginDialog::updateAvailablePlugins() {
    static QString required_prefix = QStringLiteral("plugin_");
    if (QDir(plugins_path).exists()) {
        plugin_list->clear();

        QDirIterator plugins(plugins_path,
                             QStringList() << QStringLiteral("*.dll") << QStringLiteral("*.so"),
                             QDir::Files, QDirIterator::Subdirectories);
        while (plugins.hasNext()) {
            QString available_path = plugins.next();
            QString name = available_path.replace(plugins_path, QStringLiteral(""));

            if (name.startsWith(required_prefix)) {
                LOG_INFO(Plugin, (name.toStdString() + " starts with " +
                                  required_prefix.toStdString() + ", is plugin")
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
                QString::fromStdString(loadedPlugin).replace(plugins_path, QStringLiteral("")),
                Qt::MatchExactly);
            if (!foundItems.empty()) {
                // Only one plugin should match the criteria
                foundItems[0]->setCheckState(Qt::Checked);
            }
        }
    }
}