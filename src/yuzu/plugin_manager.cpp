// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QCoreApplication>
#include <QDirIterator>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>
#include "core/core.h"
#include "core/tools/plugin_manager.h"
#include "yuzu/plugin_manager.h"
#include "yuzu/uisettings.h"

PluginDialog::PluginDialog(QWidget* parent) : QDialog(parent) {
    setAttribute(Qt::WA_DeleteOnClose);

    Core::System::GetInstance().PluginManager().SetPluginCallback(
        std::bind(&PluginDialog::updateAvailablePlugins, this, std::placeholders::_1));

    plugin_list = new QListWidget(this);
    plugin_list->setObjectName(QStringLiteral("PluginList"));
    updateAvailablePlugins();

    main_layout->addWidget(plugin_list);

    setLayout(main_layout);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(tr("Plugin Manager"));

    filesystem_watcher.addPath(plugins_path);

    QObject::connect(&filesystem_watcher, &QFileSystemWatcher::directoryChanged, this,
                     [this] { updateAvailablePlugins(); });
    QObject::connect(plugin_list, &QListWidget::itemChanged, this,
                     &PluginDialog::pluginEnabledOrDisabled);
}

PluginDialog::~PluginDialog() {
    Core::System::GetInstance().PluginManager().SetPluginCallback(nullptr);
}

void PluginDialog::pluginEnabledOrDisabled(QListWidgetItem* changed) {
    bool checked = changed->checkState() == Qt::Checked;
    std::string path = changed->text().toStdString();

    if (checked) {
        Core::System::GetInstance().PluginManager().LoadPlugin(path);
    } else {
        Core::System::GetInstance().PluginManager().RemovePlugin(path);
    }
}

void PluginDialog::updateAvailablePlugins() {
    if (QDir(plugins_path).exists()) {
        plugin_list->clear();

        QDirIterator plugins(plugins_path, QStringList() << tr("*.dll"), QDir::Files,
                             QDirIterator::Subdirectories);
        while (plugins.hasNext()) {
            QString available_path = plugins.next();

            QListWidgetItem* item = new QListWidgetItem(available_path);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
            plugin_list->addItem(item);
        }

        for (auto const& loadedPlugin :
             Core::System::GetInstance().PluginManager().GetAllLoadedPlugins()) {
            plugin_list->findItems(QString::fromStdString(loadedPlugin), Qt::MatchExactly)[0]
                ->setCheckState(Qt::Checked);
        }
    }
}