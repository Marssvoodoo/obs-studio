/*****************************************************************************
    Copyright (C) 2026 OBS contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
*****************************************************************************/

#include "AudioPluginScanPanel.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "moc_AudioPluginScanPanel.cpp"

namespace OBS {
namespace {
constexpr qint64 MAX_SCAN_FILE_BYTES = 32LL * 1024LL * 1024LL;

QString appConfigPath(const char *relativePath)
{
	char path[4096] = {};
	if (GetAppConfigPath(path, sizeof(path), relativePath) <= 0)
		return {};
	return QString::fromUtf8(path);
}

QString scannerExecutable(const QString &name)
{
	return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

QStringList splitEnvironmentPaths(const QString &value)
{
#ifdef Q_OS_WIN
	return value.split(';', Qt::SkipEmptyParts);
#else
	return value.split(':', Qt::SkipEmptyParts);
#endif
}

struct ResultRow {
	QString format;
	QString name;
	QString vendor;
	QString status;
	QString reason;
	QString path;
};

QList<ResultRow> readAuditFile(const QString &path, const QString &expectedFormat)
{
	QList<ResultRow> results;
	QFile file(path);
	if (!file.exists() || file.size() <= 0 || file.size() > MAX_SCAN_FILE_BYTES || !file.open(QIODevice::ReadOnly))
		return results;

	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject())
		return results;
	const QJsonObject root = document.object();
	if (root.value(QStringLiteral("version")).toInt() != 1 ||
	    root.value(QStringLiteral("format")).toString() != expectedFormat) {
		return results;
	}

	const QJsonArray array = root.value(QStringLiteral("results")).toArray();
	for (const QJsonValue &value : array) {
		if (results.size() >= 10000 || !value.isObject())
			break;
		const QJsonObject item = value.toObject();
		ResultRow row;
		row.format = expectedFormat;
		row.name = item.value(QStringLiteral("name")).toString();
		row.vendor = item.value(QStringLiteral("vendor")).toString();
		row.status = item.value(QStringLiteral("status")).toString();
		row.reason = item.value(QStringLiteral("reason")).toString();
		row.path = item.value(QStringLiteral("path")).toString();
		if (!row.name.isEmpty() && !row.path.isEmpty() &&
		    (row.status == QStringLiteral("passed") || row.status == QStringLiteral("failed") ||
		     row.status == QStringLiteral("skipped"))) {
			results.push_back(std::move(row));
		}
	}
	return results;
}
} // namespace

AudioPluginScanPanel::AudioPluginScanPanel(QWidget *parent) : QWidget(parent)
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(24, 18, 24, 18);
	root->setSpacing(12);

	auto *title = new QLabel(QTStr("PluginManager.Audio.Title"), this);
	title->setProperty("class", "text-title");
	root->addWidget(title);

	auto *description = new QLabel(QTStr("PluginManager.Audio.Description"), this);
	description->setWordWrap(true);
	description->setProperty("class", "text-muted");
	root->addWidget(description);

	auto *formatRow = new QHBoxLayout();
	vst2Check = new QCheckBox(QTStr("PluginManager.Audio.VST2"), this);
	vst3Check = new QCheckBox(QTStr("PluginManager.Audio.VST3"), this);
	vst2Check->setChecked(true);
	vst3Check->setChecked(true);
	formatRow->addWidget(vst2Check);
	formatRow->addWidget(vst3Check);
	formatRow->addSpacing(20);
	skipFailedCheck = new QCheckBox(QTStr("PluginManager.Audio.SkipFailed"), this);
	const bool hasSkipSetting = config_has_user_value(App()->GetUserConfig(), "AudioPluginScanner", "SkipFailed");
	skipFailedCheck->setChecked(
		hasSkipSetting ? config_get_bool(App()->GetUserConfig(), "AudioPluginScanner", "SkipFailed") : true);
	formatRow->addWidget(skipFailedCheck);
	formatRow->addStretch();
	root->addLayout(formatRow);

	connect(skipFailedCheck, &QCheckBox::toggled, this, [](bool checked) {
		config_set_bool(App()->GetUserConfig(), "AudioPluginScanner", "SkipFailed", checked);
		config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
	});

	auto *locationsGroup = new QGroupBox(QTStr("PluginManager.Audio.Locations"), this);
	auto *locationsLayout = new QVBoxLayout(locationsGroup);
	locationsList = new QListWidget(locationsGroup);
	locationsList->setAlternatingRowColors(true);
	locationsList->setMaximumHeight(118);
	locationsLayout->addWidget(locationsList);
	root->addWidget(locationsGroup);
	populateLocations();

	auto *progressRow = new QHBoxLayout();
	currentPathLabel = new QLabel(QTStr("PluginManager.Audio.Idle"), this);
	currentPathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	currentPathLabel->setFixedHeight(currentPathLabel->fontMetrics().height() + 8);
	progressRow->addWidget(currentPathLabel, 3);
	progressBar = new QProgressBar(this);
	progressBar->setRange(0, 1);
	progressBar->setValue(0);
	progressBar->setTextVisible(true);
	progressRow->addWidget(progressBar, 2);
	root->addLayout(progressRow);

	summaryLabel = new QLabel(QTStr("PluginManager.Audio.NoResults"), this);
	summaryLabel->setMinimumHeight(summaryLabel->fontMetrics().height() + 8);
	root->addWidget(summaryLabel);

	filterEdit = new QLineEdit(this);
	filterEdit->setPlaceholderText(QTStr("PluginManager.Audio.Filter"));
	filterEdit->setClearButtonEnabled(true);
	root->addWidget(filterEdit);

	resultsTable = new QTableWidget(this);
	resultsTable->setColumnCount(5);
	resultsTable->setHorizontalHeaderLabels(
		{QTStr("PluginManager.Audio.Column.Format"), QTStr("PluginManager.Audio.Column.Plugin"),
		 QTStr("PluginManager.Audio.Column.Status"), QTStr("PluginManager.Audio.Column.Reason"),
		 QTStr("PluginManager.Audio.Column.Path")});
	resultsTable->setAlternatingRowColors(true);
	resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	resultsTable->setSortingEnabled(true);
	resultsTable->verticalHeader()->setVisible(false);
	QHeaderView *header = resultsTable->horizontalHeader();
	header->setSectionsMovable(true);
	header->setStretchLastSection(true);
	header->setSectionResizeMode(0, QHeaderView::Fixed);
	header->setSectionResizeMode(1, QHeaderView::Interactive);
	header->setSectionResizeMode(2, QHeaderView::Fixed);
	header->setSectionResizeMode(3, QHeaderView::Interactive);
	header->setSectionResizeMode(4, QHeaderView::Stretch);
	resultsTable->setColumnWidth(0, 72);
	resultsTable->setColumnWidth(1, 220);
	resultsTable->setColumnWidth(2, 92);
	resultsTable->setColumnWidth(3, 300);
	root->addWidget(resultsTable, 1);

	auto *buttonRow = new QHBoxLayout();
	startButton = new QPushButton(QTStr("PluginManager.Audio.Start"), this);
	stopButton = new QPushButton(QTStr("PluginManager.Audio.Stop"), this);
	rescanButton = new QPushButton(QTStr("PluginManager.Audio.RescanAll"), this);
	buttonRow->addWidget(startButton);
	buttonRow->addWidget(stopButton);
	buttonRow->addWidget(rescanButton);
	buttonRow->addStretch();
	root->addLayout(buttonRow);

	connect(startButton, &QPushButton::clicked, this, [this]() { startScan(false); });
	connect(stopButton, &QPushButton::clicked, this, &AudioPluginScanPanel::requestStop);
	connect(rescanButton, &QPushButton::clicked, this, [this]() { startScan(true); });
	connect(filterEdit, &QLineEdit::textChanged, this, &AudioPluginScanPanel::applyFilter);

	scannerProcess = new QProcess(this);
	scannerProcess->setProcessChannelMode(QProcess::MergedChannels);
	connect(scannerProcess, &QProcess::readyRead, scannerProcess, [this]() { scannerProcess->readAll(); });
	connect(scannerProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
		&AudioPluginScanPanel::scannerFinished);

	refreshTimer = new QTimer(this);
	refreshTimer->setInterval(350);
	connect(refreshTimer, &QTimer::timeout, this, [this]() { refreshFromDisk(false); });
	refreshTimer->start();
	setScanningState(false);
	refreshFromDisk(true);
}

AudioPluginScanPanel::~AudioPluginScanPanel()
{
	if (scannerProcess && scannerProcess->state() != QProcess::NotRunning) {
		writeStopFile(activeScanner.stop);
		scannerProcess->terminate();
		if (!scannerProcess->waitForFinished(2000)) {
			scannerProcess->kill();
			scannerProcess->waitForFinished(2000);
		}
	}
}

AudioPluginScanPanel::ScannerPaths AudioPluginScanPanel::vst2Paths() const
{
	const QString directory = appConfigPath("obs-studio/plugin_config/obs-vst");
	if (directory.isEmpty())
		return {QStringLiteral("VST2"), scannerExecutable(QStringLiteral("obs-vst2-scanner.exe"))};
	return {QStringLiteral("VST2"),
		scannerExecutable(QStringLiteral("obs-vst2-scanner.exe")),
		QDir(directory).filePath(QStringLiteral("vst2scan-results.json")),
		QDir(directory).filePath(QStringLiteral("vst2scan-results.json")),
		QDir(directory).filePath(QStringLiteral("vst2scan-status.json")),
		QDir(directory).filePath(QStringLiteral("vst2scan.stop"))};
}

AudioPluginScanPanel::ScannerPaths AudioPluginScanPanel::vst3Paths() const
{
	const QString directory = appConfigPath("obs-studio/plugin_config/obs-vst3");
	if (directory.isEmpty())
		return {QStringLiteral("VST3"), scannerExecutable(QStringLiteral("obs-vst3-scanner.exe"))};
	return {QStringLiteral("VST3"),
		scannerExecutable(QStringLiteral("obs-vst3-scanner.exe")),
		QDir(directory).filePath(QStringLiteral("vst3list.json")),
		QDir(directory).filePath(QStringLiteral("vst3scan-results.json")),
		QDir(directory).filePath(QStringLiteral("vst3scan-status.json")),
		QDir(directory).filePath(QStringLiteral("vst3scan.stop"))};
}

void AudioPluginScanPanel::populateLocations()
{
	locationsList->clear();
	QSet<QString> seen;
	auto addLocation = [&](const QString &format, QString path) {
		path = QDir::cleanPath(path.trimmed());
		if (path.isEmpty())
			return;
		const QString identity = format + QLatin1Char('|') + path.toCaseFolded();
		if (seen.contains(identity))
			return;
		seen.insert(identity);
		const bool available = QDir(path).exists();
		auto *item =
			new QListWidgetItem(QStringLiteral("[%1] %2 — %3")
						    .arg(format, path,
							 available ? QTStr("PluginManager.Audio.Location.Found")
								   : QTStr("PluginManager.Audio.Location.Missing")),
					    locationsList);
		item->setToolTip(path);
		if (!available)
			item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
	};

	for (const QString &path : splitEnvironmentPaths(qEnvironmentVariable("VST_PATH")))
		addLocation(QStringLiteral("VST2"), path);
	const QString programFiles = qEnvironmentVariable("ProgramFiles");
	const QString commonProgramFiles = qEnvironmentVariable("CommonProgramFiles");
	if (!programFiles.isEmpty()) {
		addLocation(QStringLiteral("VST2"), programFiles + QStringLiteral("/Steinberg/VstPlugins"));
		addLocation(QStringLiteral("VST2"), programFiles + QStringLiteral("/VSTPlugins"));
		addLocation(QStringLiteral("VST3"), programFiles + QStringLiteral("/Common Files/VST3"));
	}
	if (!commonProgramFiles.isEmpty()) {
		addLocation(QStringLiteral("VST2"),
			    commonProgramFiles + QStringLiteral("/Steinberg/Shared Components"));
		addLocation(QStringLiteral("VST2"), commonProgramFiles + QStringLiteral("/VST2"));
		addLocation(QStringLiteral("VST2"), commonProgramFiles + QStringLiteral("/Steinberg/VST2"));
		addLocation(QStringLiteral("VST2"), commonProgramFiles + QStringLiteral("/VSTPlugins"));
	}
	const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
	if (!localAppData.isEmpty())
		addLocation(QStringLiteral("VST3"), localAppData + QStringLiteral("/Programs/Common/VST3"));
}

void AudioPluginScanPanel::setScanningState(bool active)
{
	scanning = active;
	startButton->setEnabled(!active);
	rescanButton->setEnabled(!active);
	stopButton->setEnabled(active);
	vst2Check->setEnabled(!active);
	vst3Check->setEnabled(!active);
	skipFailedCheck->setEnabled(!active);
}

void AudioPluginScanPanel::startScan(bool fullRescan)
{
	if (scanning)
		return;
	pendingScanners.clear();
	if (vst2Check->isChecked())
		pendingScanners.enqueue(vst2Paths());
	if (vst3Check->isChecked())
		pendingScanners.enqueue(vst3Paths());
	if (pendingScanners.isEmpty()) {
		summaryLabel->setText(QTStr("PluginManager.Audio.SelectFormat"));
		return;
	}

	rescanAll = fullRescan;
	stopping = false;
	setScanningState(true);
	progressBar->setRange(0, 0);
	progressBar->setFormat(QTStr("PluginManager.Audio.Preparing"));
	startNextScanner();
}

void AudioPluginScanPanel::startNextScanner()
{
	if (pendingScanners.isEmpty()) {
		const bool wasStopped = stopping;
		setScanningState(false);
		stopping = false;
		activeScanner = {};
		currentPathLabel->setText(QTStr("PluginManager.Audio.Idle"));
		progressBar->setRange(0, 1);
		progressBar->setValue(1);
		progressBar->setFormat(
			QTStr(wasStopped ? "PluginManager.Audio.Stopped" : "PluginManager.Audio.Complete"));
		refreshFromDisk(true);
		return;
	}

	activeScanner = pendingScanners.dequeue();
	const QString directory = QFileInfo(activeScanner.audit).absolutePath();
	if (directory.isEmpty() || !QDir().mkpath(directory) || !QFileInfo::exists(activeScanner.executable)) {
		summaryLabel->setText(QTStr("PluginManager.Audio.ScannerMissing").arg(activeScanner.executable));
		QTimer::singleShot(0, this, &AudioPluginScanPanel::startNextScanner);
		return;
	}
	QFile::remove(activeScanner.stop);

	QStringList arguments{QStringLiteral("--scan-manager")};
	if (activeScanner.format == QStringLiteral("VST2")) {
		arguments << activeScanner.audit << activeScanner.status << activeScanner.stop;
	} else {
		arguments << activeScanner.cache << activeScanner.audit << activeScanner.status << activeScanner.stop;
	}
	arguments << (skipFailedCheck->isChecked() ? QStringLiteral("1") : QStringLiteral("0"))
		  << (rescanAll ? QStringLiteral("1") : QStringLiteral("0"));

	currentPathLabel->setText(QTStr("PluginManager.Audio.Starting").arg(activeScanner.format));
	currentPathLabel->setToolTip(activeScanner.executable);
	scannerProcess->start(activeScanner.executable, arguments, QIODevice::ReadOnly);
	if (!scannerProcess->waitForStarted(1500)) {
		summaryLabel->setText(QTStr("PluginManager.Audio.StartFailed").arg(activeScanner.format));
		QTimer::singleShot(0, this, &AudioPluginScanPanel::startNextScanner);
	}
}

bool AudioPluginScanPanel::writeStopFile(const QString &path)
{
	if (path.isEmpty())
		return false;
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	file.write("stop\n");
	return file.commit();
}

void AudioPluginScanPanel::requestStop()
{
	if (!scanning)
		return;
	stopping = true;
	pendingScanners.clear();
	writeStopFile(activeScanner.stop);
	writeStopFile(vst2Paths().stop);
	writeStopFile(vst3Paths().stop);
	currentPathLabel->setText(QTStr("PluginManager.Audio.Stopping"));
	progressBar->setFormat(QTStr("PluginManager.Audio.Stopping"));

	QTimer::singleShot(2500, this, [this]() {
		if (stopping && scannerProcess->state() != QProcess::NotRunning)
			scannerProcess->kill();
	});
}

void AudioPluginScanPanel::scannerFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
	refreshFromDisk(true);
	if (!stopping && (exitStatus != QProcess::NormalExit || exitCode != 0)) {
		summaryLabel->setText(
			QTStr("PluginManager.Audio.ScannerFailed").arg(activeScanner.format).arg(exitCode));
	}
	startNextScanner();
}

void AudioPluginScanPanel::refreshFromDisk(bool force)
{
	const ScannerPaths vst2 = vst2Paths();
	const ScannerPaths vst3 = vst3Paths();
	const QDateTime vst2Modified = QFileInfo(vst2.audit).lastModified();
	const QDateTime vst3Modified = QFileInfo(vst3.audit).lastModified();
	if (force || vst2Modified != vst2AuditModified || vst3Modified != vst3AuditModified) {
		vst2AuditModified = vst2Modified;
		vst3AuditModified = vst3Modified;
		rebuildResults();
	}
	if (scanning && !activeScanner.status.isEmpty())
		refreshStatus(activeScanner);
}

void AudioPluginScanPanel::refreshStatus(const ScannerPaths &paths)
{
	QFile file(paths.status);
	if (!file.exists() || file.size() <= 0 || file.size() > MAX_SCAN_FILE_BYTES || !file.open(QIODevice::ReadOnly))
		return;
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject())
		return;
	const QJsonObject root = document.object();
	if (root.value(QStringLiteral("version")).toInt() != 1 ||
	    root.value(QStringLiteral("format")).toString() != paths.format) {
		return;
	}

	const int completed = std::max(0, root.value(QStringLiteral("completed")).toInt());
	const int total = std::max(0, root.value(QStringLiteral("total")).toInt());
	progressBar->setRange(0, std::max(1, total));
	progressBar->setValue(std::min(completed, std::max(1, total)));
	progressBar->setFormat(QStringLiteral("%1  %2/%3").arg(paths.format).arg(completed).arg(total));

	const QString current = root.value(QStringLiteral("current")).toString();
	const QString currentText = current.isEmpty() ? QTStr("PluginManager.Audio.Scanning").arg(paths.format)
						      : current;
	const int labelWidth = std::max(240, currentPathLabel->width() - 12);
	currentPathLabel->setText(currentPathLabel->fontMetrics().elidedText(currentText, Qt::ElideMiddle, labelWidth));
	currentPathLabel->setToolTip(currentText);

	const int passed = std::max(0, root.value(QStringLiteral("passed")).toInt());
	const int failed = std::max(0, root.value(QStringLiteral("failed")).toInt());
	const int skipped = std::max(0, root.value(QStringLiteral("skipped")).toInt());
	const QString message = root.value(QStringLiteral("message")).toString();
	summaryLabel->setText(
		QTStr("PluginManager.Audio.Summary").arg(paths.format).arg(passed).arg(failed).arg(skipped).arg(message));
}

void AudioPluginScanPanel::rebuildResults()
{
	QList<ResultRow> rows = readAuditFile(vst2Paths().audit, QStringLiteral("VST2"));
	rows.append(readAuditFile(vst3Paths().audit, QStringLiteral("VST3")));
	std::sort(rows.begin(), rows.end(), [](const ResultRow &left, const ResultRow &right) {
		const int formatOrder = left.format.compare(right.format, Qt::CaseInsensitive);
		return formatOrder == 0 ? left.name.compare(right.name, Qt::CaseInsensitive) < 0 : formatOrder < 0;
	});

	resultsTable->setSortingEnabled(false);
	resultsTable->setRowCount(rows.size());
	int passed = 0;
	int failed = 0;
	int skipped = 0;
	for (qsizetype rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
		const ResultRow &row = rows[rowIndex];
		QString plugin = row.name;
		if (!row.vendor.isEmpty())
			plugin += QStringLiteral(" — ") + row.vendor;
		const QString statusText = row.status == QStringLiteral("passed") ? QTStr("PluginManager.Audio.Passed")
					   : row.status == QStringLiteral("failed")
						   ? QTStr("PluginManager.Audio.Failed")
						   : QTStr("PluginManager.Audio.Skipped");
		const QStringList values{row.format, plugin, statusText, row.reason, row.path};
		for (int column = 0; column < values.size(); ++column) {
			auto *item = new QTableWidgetItem(values[column]);
			item->setToolTip(values[column]);
			if (column == 2) {
				if (row.status == QStringLiteral("passed"))
					item->setForeground(QColor(QStringLiteral("#56B870")));
				else if (row.status == QStringLiteral("failed"))
					item->setForeground(QColor(QStringLiteral("#E05A5A")));
				else
					item->setForeground(QColor(QStringLiteral("#D6A64B")));
			}
			resultsTable->setItem(static_cast<int>(rowIndex), column, item);
		}
		passed += row.status == QStringLiteral("passed");
		failed += row.status == QStringLiteral("failed");
		skipped += row.status == QStringLiteral("skipped");
	}
	resultsTable->setSortingEnabled(true);
	applyFilter();

	if (!scanning) {
		summaryLabel->setText(
			rows.isEmpty()
				? QTStr("PluginManager.Audio.NoResults")
				: QTStr("PluginManager.Audio.SavedSummary").arg(passed).arg(failed).arg(skipped));
	}
}

void AudioPluginScanPanel::applyFilter()
{
	const QString filter = filterEdit->text().trimmed();
	for (int row = 0; row < resultsTable->rowCount(); ++row) {
		bool match = filter.isEmpty();
		for (int column = 0; !match && column < resultsTable->columnCount(); ++column) {
			if (const QTableWidgetItem *item = resultsTable->item(row, column))
				match = item->text().contains(filter, Qt::CaseInsensitive);
		}
		resultsTable->setRowHidden(row, !match);
	}
}

} // namespace OBS
