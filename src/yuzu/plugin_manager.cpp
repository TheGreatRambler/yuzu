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
#include "yuzu/plugin_manager.h"
#include "yuzu/uisettings.h"

PluginDialog::PluginDialog(QWidget* parent) : QDialog(parent) {
    plugin_list = new QListWidget(this);
    plugin_list->setObjectName(QStringLiteral("PluginList"));

    main_layout->addWidget(plugin_list);

    setLayout(main_layout);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(tr("Plugin Manager"));

    /*
    file_list = new QListWidget(this);

    for (const QString& file : files) {
        QListWidgetItem* item = new QListWidgetItem(QFileInfo(file).fileName(), file_list);
        item->setData(Qt::UserRole, file);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }

    file_list->setMinimumWidth((file_list->sizeHintForColumn(0) * 11) / 10);

    vbox_layout = new QVBoxLayout;

    hbox_layout = new QHBoxLayout;

    description = new QLabel(tr("Please confirm these are the files you wish to install."));

    update_description =
        new QLabel(tr("Installing an Update or DLC will overwrite the previously installed one."));

    buttons = new QDialogButtonBox;
    buttons->addButton(QDialogButtonBox::Cancel);
    buttons->addButton(tr("Install"), QDialogButtonBox::AcceptRole);

    connect(buttons, &QDialogButtonBox::accepted, this, &InstallDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &InstallDialog::reject);

    hbox_layout->addWidget(buttons);

    vbox_layout->addWidget(description);
    vbox_layout->addWidget(update_description);
    vbox_layout->addWidget(file_list);
    vbox_layout->addLayout(hbox_layout);

    setLayout(vbox_layout);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(tr("Install Files to NAND"));
    */
}

PluginDialog::~PluginDialog() = default;

void PluginDialog::updateAvailablePlugins() {
    QString plugins_path = QCoreApplication::applicationDirPath() + tr("/yuzu_plugins");
    if (QDir(plugins_path).exists()) {
        QDirIterator plugins(plugins_path, QStringList() << tr("*.dll"), QDir::Files,
                             QDirIterator::Subdirectories);
        while (plugins.hasNext()) {
            QString available_path = plugins.next();

            QListWidgetItem* item = new QListWidgetItem(available_path);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
            plugin_list->addItem(item);
        }
    }
}