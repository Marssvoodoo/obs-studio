/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>
                          Zachary Lund <admin@computerquip.com>
                          Philippe Groarke <philippe.groarke@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "OBSBasic.hpp"

#include <qt-wrappers.hpp>

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr char CUSTOM_DOCK_LAYOUTS_KEY[] = "CustomDockLayouts";
constexpr qsizetype MAX_CUSTOM_DOCK_LAYOUTS = 64;
constexpr qsizetype MAX_DOCK_STATE_BYTES = 4 * 1024 * 1024;

struct DockLayoutRecord {
	QString name;
	QByteArray state;
};

bool ValidDockLayoutName(const QString &name)
{
	if (name.isEmpty() || name.size() > 64)
		return false;

	for (const QChar character : name) {
		if (character.isNull() || character.category() == QChar::Other_Control)
			return false;
	}
	return true;
}

QList<DockLayoutRecord> LoadCustomDockLayouts()
{
	QList<DockLayoutRecord> layouts;
	const char *stored = config_get_string(App()->GetUserConfig(), "BasicWindow", CUSTOM_DOCK_LAYOUTS_KEY);
	if (!stored || !*stored)
		return layouts;

	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(QByteArray(stored), &error);
	if (error.error != QJsonParseError::NoError || !document.isArray())
		return layouts;

	const QJsonArray array = document.array();
	for (const QJsonValue &value : array) {
		if (layouts.size() >= MAX_CUSTOM_DOCK_LAYOUTS || !value.isObject())
			break;

		const QJsonObject object = value.toObject();
		const QString name = object.value(QStringLiteral("name")).toString().simplified();
		const QByteArray state =
			QByteArray::fromBase64(object.value(QStringLiteral("state")).toString().toLatin1());
		if (!ValidDockLayoutName(name) || state.isEmpty() || state.size() > MAX_DOCK_STATE_BYTES)
			continue;

		const auto duplicate =
			std::find_if(layouts.cbegin(), layouts.cend(), [&name](const DockLayoutRecord &item) {
				return item.name.compare(name, Qt::CaseInsensitive) == 0;
			});
		if (duplicate == layouts.cend())
			layouts.push_back({name, state});
	}
	return layouts;
}

void SaveCustomDockLayouts(const QList<DockLayoutRecord> &layouts)
{
	QJsonArray array;
	for (const DockLayoutRecord &layout : layouts) {
		if (array.size() >= MAX_CUSTOM_DOCK_LAYOUTS || !ValidDockLayoutName(layout.name) ||
		    layout.state.isEmpty() || layout.state.size() > MAX_DOCK_STATE_BYTES) {
			continue;
		}

		QJsonObject object;
		object.insert(QStringLiteral("name"), layout.name);
		object.insert(QStringLiteral("state"), QString::fromLatin1(layout.state.toBase64()));
		array.push_back(object);
	}

	const QByteArray json = QJsonDocument(array).toJson(QJsonDocument::Compact);
	config_set_string(App()->GetUserConfig(), "BasicWindow", CUSTOM_DOCK_LAYOUTS_KEY, json.constData());
	config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
}
} // namespace

void setupDockAction(QDockWidget *dock)
{
	QAction *action = dock->toggleViewAction();

	auto neverDisable = [action]() {
		QSignalBlocker block(action);
		action->setEnabled(true);
	};

	auto newToggleView = [dock](bool check) {
		QSignalBlocker block(dock);
		dock->setVisible(check);
	};

	// Replace the slot connected by default
	QObject::disconnect(action, &QAction::triggered, nullptr, 0);
	QObject::connect(action, &QAction::triggered, dock, newToggleView);

	// Make the action unable to be disabled
	QObject::connect(action, &QAction::enabledChanged, action, neverDisable);
}

void OBSBasic::on_resetDocks_triggered(bool force)
{
#ifdef BROWSER_AVAILABLE
	if ((extraDocks.size() || extraCustomDocks.size() || extraBrowserDocks.size()) && !force)
#else
	if ((extraDocks.size() || extraCustomDocks.size()) && !force)
#endif
	{
		QMessageBox::StandardButton button =
			OBSMessageBox::question(this, QTStr("ResetUIWarning.Title"), QTStr("ResetUIWarning.Text"));

		if (button == QMessageBox::No)
			return;
	}

#define RESET_DOCKLIST(dockList)                                                                               \
	for (int i = dockList.size() - 1; i >= 0; i--) {                                                       \
		dockList[i]->setVisible(true);                                                                 \
		dockList[i]->setFloating(true);                                                                \
		dockList[i]->move(frameGeometry().topLeft() + rect().center() - dockList[i]->rect().center()); \
		dockList[i]->setVisible(false);                                                                \
	}

	RESET_DOCKLIST(extraDocks)
	RESET_DOCKLIST(extraCustomDocks)
#ifdef BROWSER_AVAILABLE
	RESET_DOCKLIST(extraBrowserDocks)
#endif
#undef RESET_DOCKLIST

	restoreState(startingDockLayout);
	ui->sideDocks->setChecked(true);

	int cx = width();
	int bottomDocksHeight = height();

	bottomDocksHeight = bottomDocksHeight * 225 / 1000;

	ui->scenesDock->setVisible(true);
	ui->sourcesDock->setVisible(true);
	ui->mixerDock->setVisible(true);
	ui->transitionsDock->setVisible(true);
	previewDock->setVisible(true);
	controlsDock->setVisible(true);
	statsDock->setVisible(false);
	statsDock->setFloating(true);

	QList<QDockWidget *> bottomDocks{ui->mixerDock, ui->transitionsDock, controlsDock};

	resizeDocks(bottomDocks, {bottomDocksHeight, bottomDocksHeight, bottomDocksHeight}, Qt::Vertical);
	resizeDocks(bottomDocks, {cx * 45 / 100, cx * 14 / 100, cx * 16 / 100}, Qt::Horizontal);

	int sideDockWidth = std::min(width() * 30 / 100, 280);
	resizeDocks({ui->scenesDock, ui->sourcesDock}, {sideDockWidth, sideDockWidth}, Qt::Horizontal);

	activateWindow();
}

void OBSBasic::on_applyStandardDockLayout_triggered()
{
	on_resetDocks_triggered(false);
}

void OBSBasic::on_applyBalancedDockLayout_triggered()
{
	QList<QDockWidget *> rightColumn;
	QDockWidget *preferredRightDock = nullptr;
	int preferredRightX = 0;
	auto rememberVisible = [&rightColumn, &preferredRightDock, &preferredRightX](QDockWidget *dock) {
		if (!dock || !dock->isVisible())
			return;

		rightColumn.push_back(dock);
		if (!dock->visibleRegion().isEmpty()) {
			const int dockX = dock->mapToGlobal(dock->rect().center()).x();
			if (!preferredRightDock || dockX > preferredRightX) {
				preferredRightDock = dock;
				preferredRightX = dockX;
			}
		}
	};

	for (const auto &dock : extraDocks)
		rememberVisible(dock.get());
	for (const auto &dock : extraCustomDocks)
		rememberVisible(dock.data());
#ifdef BROWSER_AVAILABLE
	for (const auto &dock : extraBrowserDocks)
		rememberVisible(dock.get());
#endif
	rememberVisible(statsDock);

	on_resetDocks_triggered(true);
	setDockCornersVertical(true);
	ui->sideDocks->setChecked(true);

	QDockWidget *rightAnchor = nullptr;
	for (QDockWidget *dock : rightColumn) {
		removeDockWidget(dock);
		addDockWidget(Qt::RightDockWidgetArea, dock);
		dock->setFloating(false);
		dock->setVisible(true);
		if (rightAnchor)
			tabifyDockWidget(rightAnchor, dock);
		else
			rightAnchor = dock;
	}
	if (preferredRightDock)
		preferredRightDock->raise();
	else if (rightAnchor)
		rightAnchor->raise();

	const int third = std::max(width() / 3, 280);
	resizeDocks({ui->scenesDock, ui->sourcesDock}, {third, third}, Qt::Horizontal);
	resizeDocks({ui->scenesDock, ui->sourcesDock}, {1, 1}, Qt::Vertical);

	QList<QDockWidget *> middleRow{ui->mixerDock, ui->transitionsDock, controlsDock};
	resizeDocks(middleRow, {1, 1, 1}, Qt::Horizontal);

	if (rightAnchor)
		resizeDocks({ui->scenesDock, previewDock, rightAnchor}, {third, third, third}, Qt::Horizontal);

	activateWindow();
}

void OBSBasic::on_applyFourTwoDockLayout_triggered()
{
	QList<QDockWidget *> auxiliaryDocks;
	QDockWidget *preferredDock = nullptr;
	int preferredX = 0;
	auto rememberVisible = [&auxiliaryDocks, &preferredDock, &preferredX](QDockWidget *dock) {
		if (!dock || !dock->isVisible())
			return;

		auxiliaryDocks.push_back(dock);
		if (!dock->visibleRegion().isEmpty()) {
			const int dockX = dock->mapToGlobal(dock->rect().center()).x();
			if (!preferredDock || dockX > preferredX) {
				preferredDock = dock;
				preferredX = dockX;
			}
		}
	};

	for (const auto &dock : extraDocks)
		rememberVisible(dock.get());
	for (const auto &dock : extraCustomDocks)
		rememberVisible(dock.data());
#ifdef BROWSER_AVAILABLE
	for (const auto &dock : extraBrowserDocks)
		rememberVisible(dock.get());
#endif

	on_resetDocks_triggered(true);
	setDockCornersVertical(true);
	ui->sideDocks->setChecked(true);

	const QList<QDockWidget *> primaryDocks{ui->scenesDock,      ui->sourcesDock,    ui->mixerDock,
						ui->transitionsDock, previewDock.data(), controlsDock.data(),
						statsDock.data()};
	for (QDockWidget *dock : primaryDocks)
		removeDockWidget(dock);

	addDockWidget(Qt::TopDockWidgetArea, previewDock);
	addDockWidget(Qt::BottomDockWidgetArea, ui->scenesDock);
	splitDockWidget(ui->scenesDock, ui->sourcesDock, Qt::Horizontal);
	splitDockWidget(ui->sourcesDock, ui->mixerDock, Qt::Horizontal);
	splitDockWidget(ui->mixerDock, ui->transitionsDock, Qt::Horizontal);
	addDockWidget(Qt::BottomDockWidgetArea, controlsDock);
	tabifyDockWidget(ui->transitionsDock, controlsDock);

	addDockWidget(Qt::RightDockWidgetArea, statsDock);

	for (QDockWidget *dock : primaryDocks) {
		dock->setFloating(false);
		dock->setVisible(true);
	}

	QDockWidget *rightTab = statsDock;
	for (QDockWidget *dock : auxiliaryDocks) {
		removeDockWidget(dock);
		addDockWidget(Qt::RightDockWidgetArea, dock);
		dock->setFloating(false);
		dock->setVisible(true);
		tabifyDockWidget(rightTab, dock);
		rightTab = dock;
	}
	if (preferredDock)
		preferredDock->raise();
	else
		statsDock->raise();

	resizeDocks({ui->scenesDock, ui->sourcesDock, ui->mixerDock, ui->transitionsDock}, {2, 2, 4, 2},
		    Qt::Horizontal);
	resizeDocks({previewDock, ui->scenesDock}, {4, 2}, Qt::Vertical);
	resizeDocks({previewDock, statsDock}, {4, 2}, Qt::Horizontal);
	controlsDock->raise();

	activateWindow();
}

void OBSBasic::SetupDockLayoutMenu()
{
	ui->dockLayoutsMenu->addSeparator();

	QAction *saveAction = ui->dockLayoutsMenu->addAction(QTStr("Basic.MainMenu.Docks.Layout.Save"));
	connect(saveAction, &QAction::triggered, this, &OBSBasic::SaveCustomDockLayout);

	customDockLayoutsMenu = ui->dockLayoutsMenu->addMenu(QTStr("Basic.MainMenu.Docks.Layout.Custom"));
	RefreshCustomDockLayoutsMenu();

	QAction *manageAction = ui->dockLayoutsMenu->addAction(QTStr("Basic.MainMenu.Docks.Layout.Manage"));
	connect(manageAction, &QAction::triggered, this, &OBSBasic::ManageCustomDockLayouts);
}

void OBSBasic::RefreshCustomDockLayoutsMenu()
{
	if (!customDockLayoutsMenu)
		return;

	customDockLayoutsMenu->clear();
	const QList<DockLayoutRecord> layouts = LoadCustomDockLayouts();
	if (layouts.isEmpty()) {
		QAction *emptyAction =
			customDockLayoutsMenu->addAction(QTStr("Basic.MainMenu.Docks.Layout.Custom.Empty"));
		emptyAction->setEnabled(false);
		return;
	}

	for (const DockLayoutRecord &layout : layouts) {
		QAction *action = customDockLayoutsMenu->addAction(layout.name);
		action->setData(layout.name);
		connect(action, &QAction::triggered, this,
			[this, action]() { ApplyCustomDockLayout(action->data().toString()); });
	}
}

void OBSBasic::SaveCustomDockLayout()
{
	bool accepted = false;
	QString name = QInputDialog::getText(this, QTStr("Basic.MainMenu.Docks.Layout.Save.Title"),
					     QTStr("Basic.MainMenu.Docks.Layout.Save.Name"), QLineEdit::Normal, {},
					     &accepted)
			       .simplified();
	if (!accepted)
		return;
	if (!ValidDockLayoutName(name)) {
		OBSMessageBox::warning(this, QTStr("Basic.MainMenu.Docks.Layout.Save.Title"),
				       QTStr("Basic.MainMenu.Docks.Layout.Save.Invalid"));
		return;
	}

	QList<DockLayoutRecord> layouts = LoadCustomDockLayouts();
	auto existing = std::find_if(layouts.begin(), layouts.end(), [&name](const DockLayoutRecord &layout) {
		return layout.name.compare(name, Qt::CaseInsensitive) == 0;
	});
	if (existing != layouts.end()) {
		const auto choice = OBSMessageBox::question(this,
							    QTStr("Basic.MainMenu.Docks.Layout.Save.Replace.Title"),
							    QTStr("Basic.MainMenu.Docks.Layout.Save.Replace.Text"));
		if (choice != QMessageBox::Yes)
			return;
		existing->name = name;
		existing->state = saveState();
	} else {
		layouts.push_back({name, saveState()});
	}

	SaveCustomDockLayouts(layouts);
	RefreshCustomDockLayoutsMenu();
}

void OBSBasic::ApplyCustomDockLayout(const QString &name)
{
	const QList<DockLayoutRecord> layouts = LoadCustomDockLayouts();
	const auto layout = std::find_if(layouts.cbegin(), layouts.cend(),
					 [&name](const DockLayoutRecord &item) { return item.name == name; });
	if (layout == layouts.cend())
		return;

	const QByteArray previousState = saveState();
	if (!restoreState(layout->state)) {
		restoreState(previousState);
		OBSMessageBox::warning(this, QTStr("Basic.MainMenu.Docks.Layout.RestoreFailed.Title"),
				       QTStr("Basic.MainMenu.Docks.Layout.RestoreFailed.Text"));
		return;
	}

	config_set_string(App()->GetUserConfig(), "BasicWindow", "DockState", layout->state.toBase64().constData());
	config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
	activateWindow();
}

void OBSBasic::ManageCustomDockLayouts()
{
	QDialog dialog(this);
	dialog.setWindowTitle(QTStr("Basic.MainMenu.Docks.Layout.Manager.Title"));
	dialog.resize(480, 360);

	auto *layout = new QVBoxLayout(&dialog);
	auto *list = new QListWidget(&dialog);
	list->setSelectionMode(QAbstractItemView::SingleSelection);
	layout->addWidget(list);

	auto *buttonLayout = new QHBoxLayout();
	auto *applyButton = new QPushButton(QTStr("Basic.MainMenu.Docks.Layout.Manager.Apply"), &dialog);
	auto *renameButton = new QPushButton(QTStr("Basic.MainMenu.Docks.Layout.Manager.Rename"), &dialog);
	auto *deleteButton = new QPushButton(QTStr("Basic.MainMenu.Docks.Layout.Manager.Delete"), &dialog);
	buttonLayout->addWidget(applyButton);
	buttonLayout->addWidget(renameButton);
	buttonLayout->addWidget(deleteButton);
	buttonLayout->addStretch();
	layout->addLayout(buttonLayout);

	auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	layout->addWidget(buttonBox);
	connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	QList<DockLayoutRecord> layouts = LoadCustomDockLayouts();
	auto refreshList = [&]() {
		list->clear();
		for (const DockLayoutRecord &record : layouts)
			list->addItem(record.name);
		const bool hasLayouts = !layouts.isEmpty();
		applyButton->setEnabled(hasLayouts);
		renameButton->setEnabled(hasLayouts);
		deleteButton->setEnabled(hasLayouts);
		if (hasLayouts)
			list->setCurrentRow(0);
	};
	refreshList();

	connect(list, &QListWidget::itemDoubleClicked, &dialog, [this, &dialog](QListWidgetItem *item) {
		ApplyCustomDockLayout(item->text());
		dialog.accept();
	});
	connect(applyButton, &QPushButton::clicked, &dialog, [this, &dialog, list]() {
		if (QListWidgetItem *item = list->currentItem()) {
			ApplyCustomDockLayout(item->text());
			dialog.accept();
		}
	});
	connect(renameButton, &QPushButton::clicked, &dialog, [&, list]() {
		const int row = list->currentRow();
		if (row < 0 || row >= layouts.size())
			return;
		bool accepted = false;
		QString name = QInputDialog::getText(&dialog, QTStr("Basic.MainMenu.Docks.Layout.Manager.Rename"),
						     QTStr("Basic.MainMenu.Docks.Layout.Save.Name"), QLineEdit::Normal,
						     layouts[row].name, &accepted)
				       .simplified();
		if (!accepted)
			return;
		bool duplicate = false;
		for (qsizetype index = 0; index < layouts.size(); ++index) {
			if (index != row && layouts[index].name.compare(name, Qt::CaseInsensitive) == 0) {
				duplicate = true;
				break;
			}
		}
		if (!ValidDockLayoutName(name) || duplicate) {
			OBSMessageBox::warning(&dialog, QTStr("Basic.MainMenu.Docks.Layout.Save.Title"),
					       QTStr("Basic.MainMenu.Docks.Layout.Save.Invalid"));
			return;
		}
		layouts[row].name = name;
		SaveCustomDockLayouts(layouts);
		RefreshCustomDockLayoutsMenu();
		refreshList();
		list->setCurrentRow(row);
	});
	connect(deleteButton, &QPushButton::clicked, &dialog, [&, list]() {
		const int row = list->currentRow();
		if (row < 0 || row >= layouts.size())
			return;
		const auto choice = OBSMessageBox::question(&dialog,
							    QTStr("Basic.MainMenu.Docks.Layout.Manager.Delete.Title"),
							    QTStr("Basic.MainMenu.Docks.Layout.Manager.Delete.Text"));
		if (choice != QMessageBox::Yes)
			return;
		layouts.removeAt(row);
		SaveCustomDockLayouts(layouts);
		RefreshCustomDockLayoutsMenu();
		refreshList();
		if (!layouts.isEmpty())
			list->setCurrentRow(std::min(row, static_cast<int>(layouts.size() - 1)));
	});

	dialog.exec();
}

void OBSBasic::on_lockDocks_toggled(bool lock)
{
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	QDockWidget::DockWidgetFeatures mainFeatures = features;
	mainFeatures &= ~QDockWidget::QDockWidget::DockWidgetClosable;

	ui->scenesDock->setFeatures(mainFeatures);
	ui->sourcesDock->setFeatures(mainFeatures);
	ui->mixerDock->setFeatures(mainFeatures);
	ui->transitionsDock->setFeatures(mainFeatures);
	controlsDock->setFeatures(mainFeatures);
	previewDock->setFeatures(features);
	statsDock->setFeatures(features);

	for (int i = extraDocks.size() - 1; i >= 0; i--)
		extraDocks[i]->setFeatures(features);

	for (int i = extraCustomDocks.size() - 1; i >= 0; i--)
		extraCustomDocks[i]->setFeatures(features);

#ifdef BROWSER_AVAILABLE
	for (int i = extraBrowserDocks.size() - 1; i >= 0; i--)
		extraBrowserDocks[i]->setFeatures(features);
#endif
}

void OBSBasic::on_sideDocks_toggled(bool side)
{
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "SideDocks", side);

	setDockCornersVertical(side);
}

void OBSBasic::AddDockWidget(QDockWidget *dock, Qt::DockWidgetArea area, bool extraBrowser)
{
	if (dock->objectName().isEmpty())
		return;

	bool lock = ui->lockDocks->isChecked();
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	setupDockAction(dock);
	dock->setFeatures(features);
	addDockWidget(area, dock);

#ifdef BROWSER_AVAILABLE
	if (extraBrowser && extraBrowserMenuDocksSeparator.isNull())
		extraBrowserMenuDocksSeparator = ui->menuDocks->addSeparator();

	if (!extraBrowser && !extraBrowserMenuDocksSeparator.isNull())
		ui->menuDocks->insertAction(extraBrowserMenuDocksSeparator, dock->toggleViewAction());
	else
		ui->menuDocks->addAction(dock->toggleViewAction());

	if (extraBrowser)
		return;
#else
	UNUSED_PARAMETER(extraBrowser);

	ui->menuDocks->addAction(dock->toggleViewAction());
#endif

	extraDockNames.push_back(dock->objectName());
	extraDocks.push_back(std::shared_ptr<QDockWidget>(dock));
}

void OBSBasic::RemoveDockWidget(const QString &name)
{
	if (extraDockNames.contains(name)) {
		int idx = extraDockNames.indexOf(name);
		extraDockNames.removeAt(idx);
		extraDocks[idx].reset();
		extraDocks.removeAt(idx);
	} else if (extraCustomDockNames.contains(name)) {
		int idx = extraCustomDockNames.indexOf(name);
		extraCustomDockNames.removeAt(idx);
		removeDockWidget(extraCustomDocks[idx]);
		extraCustomDocks.removeAt(idx);
	}
}

bool OBSBasic::IsDockObjectNameUsed(const QString &name)
{
	QStringList list;
	list << "scenesDock"
	     << "sourcesDock"
	     << "mixerDock"
	     << "transitionsDock"
	     << "controlsDock"
	     << "previewDock"
	     << "statsDock";
	list << extraDockNames;
	list << extraCustomDockNames;

	return list.contains(name);
}

void OBSBasic::AddCustomDockWidget(QDockWidget *dock)
{
	// Prevent the object name from being changed
	connect(dock, &QObject::objectNameChanged, this, &OBSBasic::RepairCustomExtraDockName);

	bool lock = ui->lockDocks->isChecked();
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	dock->setFeatures(features);
	addDockWidget(Qt::RightDockWidgetArea, dock);

	extraCustomDockNames.push_back(dock->objectName());
	extraCustomDocks.push_back(dock);
}

void OBSBasic::setDockCornersVertical(bool vertical)
{
	if (vertical) {
		setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
		setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
		setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
		setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
	} else {
		setCorner(Qt::TopLeftCorner, Qt::TopDockWidgetArea);
		setCorner(Qt::TopRightCorner, Qt::TopDockWidgetArea);
		setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
		setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
	}
}

void OBSBasic::RepairCustomExtraDockName()
{
	QDockWidget *dock = reinterpret_cast<QDockWidget *>(sender());
	int idx = extraCustomDocks.indexOf(dock);
	QSignalBlocker block(dock);

	if (idx == -1) {
		blog(LOG_WARNING, "A custom dock got its object name changed");
		return;
	}

	blog(LOG_WARNING, "The custom dock '%s' got its object name restored", QT_TO_UTF8(extraCustomDockNames[idx]));

	dock->setObjectName(extraCustomDockNames[idx]);
}
