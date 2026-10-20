// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GadgetScanWorker.h"

#include "core/InstrMatch.h"
#include "process/ProcessModuleEnum.h"

#include <QDir>

void GadgetScanWorker::configureProbe(
	const GadgetScanRequest& request,
	const QString& chainQuery,
	int maxPerDll)
{
	m_mode = Mode::SingleChain;
	m_request = request;
	m_chainQuery = chainQuery;
	m_configPath.clear();
	m_maxPerDll = maxPerDll > 0 ? maxPerDll : 1;
	m_cancel = false;
}

void GadgetScanWorker::configureConfigFile(const GadgetScanRequest& request, const QString& configPath)
{
	m_mode = Mode::ConfigFile;
	m_request = request;
	m_configPath = configPath;
	m_chainQuery.clear();
	m_maxPerDll = -1;
	m_cancel = false;
}

GadgetScanWorker::GadgetScanWorker(QObject* parent)
	: QThread(parent)
{
}

void GadgetScanWorker::requestCancel()
{
	m_cancel = true;
}

bool GadgetScanWorker::resolveScanTargets(QVector<GadgetScanTarget>& targets, QString& errorOut) const
{
	if (m_request.useProcessModules)
	{
		return RopProcessModules::buildScanTargetsFromProcess(m_request.processId, targets, errorOut);
	}

	const QDir dir(m_request.targetsDir);
	if (!dir.exists())
	{
		errorOut = QStringLiteral("targets directory does not exist: %1").arg(m_request.targetsDir);
		return false;
	}

	targets = BuildScanTargetsFromDirectory(m_request.targetsDir);
	if (targets.isEmpty())
	{
		errorOut = QStringLiteral("no .exe / .dll in targets directory");
		return false;
	}

	return true;
}

void GadgetScanWorker::run()
{
	QVector<GadgetScanTarget> targets;
	QString targetError;
	if (!resolveScanTargets(targets, targetError))
	{
		emit scanFailed(targetError);
		return;
	}

	if (m_mode == Mode::SingleChain)
	{
		QString chainError;
		if (!ValidateGadgetChainQuery(m_chainQuery, chainError))
		{
			emit scanFailed(chainError);
			return;
		}

		const auto reportProgress = [this](int current, int total, const GadgetFileResult& fileResult) {
			emit scanProgress(current, total, fileResult);
		};

		const QVector<GadgetFileResult> results = ScanGadgetTargets(
			targets,
			m_chainQuery,
			&m_cancel,
			m_maxPerDll,
			reportProgress);
		emit scanFinished(results);
		return;
	}

	GadgetConfigDocument config {};
	QString configError;
	if (!LoadGadgetConfigFile(m_configPath, config, configError))
	{
		emit scanFailed(configError);
		return;
	}

	for (const GadgetChainSpec& spec : config.chains)
	{
		if (!ValidateGadgetChainQuery(spec.chainQuery, configError))
		{
			emit scanFailed(
				QStringLiteral("[%1]: %2").arg(spec.chainQuery, configError));
			return;
		}
	}

	const auto reportProgress = [this](int current, int total, const GadgetFileResult& fileResult) {
		emit scanProgress(current, total, fileResult);
	};

	const auto reportChainStarted = [this](const GadgetChainSpec& spec) {
		emit scanChainStarted(spec.chainQuery, spec.maxPerDll);
	};

	const QVector<GadgetChainBatchResult> batch = ScanTargetsFromConfig(
		targets,
		config,
		&m_cancel,
		reportProgress,
		reportChainStarted);
	emit configScanFinished(batch);
}
