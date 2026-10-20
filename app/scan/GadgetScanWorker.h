// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/ChainConfig.h"
#include "core/GadgetScanner.h"

#include <QThread>

#include <QtGlobal>

struct GadgetScanRequest
{
	QString targetsDir;
	bool useProcessModules = false;
	quint32 processId = 0;
};

class GadgetScanWorker : public QThread
{
	Q_OBJECT

public:
	enum class Mode
	{
		SingleChain,
		ConfigFile
	};

	explicit GadgetScanWorker(QObject* parent = nullptr);

	void configureProbe(const GadgetScanRequest& request, const QString& chainQuery, int maxPerDll);
	void configureConfigFile(const GadgetScanRequest& request, const QString& configPath);
	void requestCancel();

signals:
	void scanProgress(int current, int total, const GadgetFileResult& fileResult);
	void scanChainStarted(const QString& chainQuery, int maxPerDll);
	void scanFinished(const QVector<GadgetFileResult>& results);
	void configScanFinished(const QVector<GadgetChainBatchResult>& results);
	void scanFailed(const QString& message);

protected:
	void run() override;

private:
	bool resolveScanTargets(QVector<GadgetScanTarget>& targets, QString& errorOut) const;

	Mode m_mode = Mode::SingleChain;
	GadgetScanRequest m_request;
	QString m_chainQuery;
	QString m_configPath;
	int m_maxPerDll = -1;
	volatile bool m_cancel = false;
};
