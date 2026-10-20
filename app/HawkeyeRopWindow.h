// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_HawkeyeRop.h"

#include "core/ChainConfig.h"
#include "core/GadgetScanner.h"
#include "scan/GadgetScanWorker.h"

#include <QMainWindow>

class HawkeyeRopWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit HawkeyeRopWindow(QWidget* parent = nullptr);
	~HawkeyeRopWindow() override;

protected:
	void showEvent(QShowEvent* event) override;
#if defined(Q_OS_WIN)
	bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
#endif

private slots:
	void onBrowseTargetsDir();
	void refreshChainsCombo();
	void onScanClicked();
	void onProbeRunClicked();
	void onCancelClicked();
	void onConfigScanFinished(const QVector<GadgetChainBatchResult>& results);
	void onProbeScanFinished(const QVector<GadgetFileResult>& results);
	void onScanFailed(const QString& message);
	void onScanProgress(int current, int total, const GadgetFileResult& fileResult);
	void onScanChainStarted(const QString& chainQuery, int maxPerDll);
	void onBatchProfileChanged(int index);
	void onProcessSourceToggled(bool enabled);

private:
	void loadSettings();
	void saveSettings();
	void loadProbeHistory();
	void rememberProbeChain(const QString& chain);
	QString defaultTargetsDir() const;
	QString chainsConfigDir() const;
	QString selectedChainsFilePath() const;
	QString currentProbeChainText() const;
	void setScanning(bool active);
	void startWorker(GadgetScanWorker* worker, bool probeMode);
	void showBatchProfilePreview();
	void updateTargetSourceUi();
	bool buildScanRequest(GadgetScanRequest& requestOut, QString& errorOut) const;
	QString scanTargetBanner() const;
	void appendScanResultText(const QString& text);

	Ui::HawkeyeRopClass ui;
	GadgetScanWorker* m_worker = nullptr;
	QString m_activeProbeChain;
	int m_activeProbeMaxPerDll = 3;
	bool m_lastScanUsedProcessModules = false;
	quint32 m_lastScanProcessId = 0;
};
