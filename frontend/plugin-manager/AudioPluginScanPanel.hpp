/*****************************************************************************
    Copyright (C) 2026 OBS contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
*****************************************************************************/

#pragma once

#include <QDateTime>
#include <QList>
#include <QProcess>
#include <QQueue>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTimer;

namespace OBS {

class AudioPluginScanPanel : public QWidget {
	Q_OBJECT

public:
	explicit AudioPluginScanPanel(QWidget *parent = nullptr);
	~AudioPluginScanPanel() override;

private:
	struct ScannerPaths {
		QString format;
		QString executable;
		QString cache;
		QString audit;
		QString status;
		QString stop;
	};

	QCheckBox *vst2Check = nullptr;
	QCheckBox *vst3Check = nullptr;
	QCheckBox *skipFailedCheck = nullptr;
	QListWidget *locationsList = nullptr;
	QLineEdit *filterEdit = nullptr;
	QTableWidget *resultsTable = nullptr;
	QLabel *currentPathLabel = nullptr;
	QLabel *summaryLabel = nullptr;
	QProgressBar *progressBar = nullptr;
	QPushButton *startButton = nullptr;
	QPushButton *stopButton = nullptr;
	QPushButton *rescanButton = nullptr;
	QProcess *scannerProcess = nullptr;
	QTimer *refreshTimer = nullptr;
	QQueue<ScannerPaths> pendingScanners;
	ScannerPaths activeScanner;
	QDateTime vst2AuditModified;
	QDateTime vst3AuditModified;
	bool scanning = false;
	bool rescanAll = false;
	bool stopping = false;

	ScannerPaths vst2Paths() const;
	ScannerPaths vst3Paths() const;
	void populateLocations();
	void startScan(bool fullRescan);
	void startNextScanner();
	void requestStop();
	void scannerFinished(int exitCode, QProcess::ExitStatus exitStatus);
	void refreshFromDisk(bool force = false);
	void refreshStatus(const ScannerPaths &paths);
	void rebuildResults();
	void applyFilter();
	void setScanningState(bool active);
	static bool writeStopFile(const QString &path);
};

} // namespace OBS
