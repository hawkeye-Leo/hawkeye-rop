// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChainConfig.h"

#include "InstrMatch.h"

#include <QFile>
#include <QRegularExpression>

bool LoadGadgetConfigFile(const QString& filePath, GadgetConfigDocument& out, QString& errorOut)
{
	out = GadgetConfigDocument {};

	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		errorOut = QStringLiteral("cannot open config: %1").arg(filePath);
		return false;
	}

	static const QRegularExpression kEntryRe(
		QStringLiteral("^\\[(.+)\\]\\[(\\d+)\\]\\s*$"));

	const QString content = QString::fromUtf8(file.readAll());
	const QStringList lines = content.split(QRegularExpression(QStringLiteral("\\r\\n|\\n|\\r")), Qt::KeepEmptyParts);
	int lineNumber = 0;
	for (const QString& rawLine : lines)
	{
		++lineNumber;
		QString line = rawLine.trimmed();
		if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
		{
			continue;
		}

		const QRegularExpressionMatch match = kEntryRe.match(line);
		if (!match.hasMatch())
		{
			if (out.headerLine.isEmpty() && out.chains.isEmpty())
			{
				out.headerLine = line;
				continue;
			}
			errorOut = QStringLiteral("invalid config line %1: %2").arg(lineNumber).arg(rawLine);
			return false;
		}

		const QString chain = match.captured(1).trimmed();
		QString chainError;
		if (!ValidateGadgetChainQuery(chain, chainError))
		{
			errorOut = QStringLiteral("line %1: %2").arg(lineNumber).arg(chainError);
			return false;
		}

		bool ok = false;
		const int maxPerDll = match.captured(2).toInt(&ok);
		if (!ok || maxPerDll < 1)
		{
			errorOut = QStringLiteral("invalid [N] on line %1 (need integer >= 1)").arg(lineNumber);
			return false;
		}

		GadgetChainSpec spec {};
		spec.chainQuery = chain;
		spec.maxPerDll = maxPerDll;
		out.chains.push_back(spec);
	}

	if (out.chains.isEmpty())
	{
		errorOut = QStringLiteral("config has no [chain][N] entries");
		return false;
	}

	return true;
}

QVector<GadgetChainBatchResult> ScanTargetsFromConfig(
	const QVector<GadgetScanTarget>& targets,
	const GadgetConfigDocument& config,
	volatile bool* cancelFlag,
	GadgetScanProgressFn progress,
	GadgetChainStartedFn chainStarted)
{
	QVector<GadgetChainBatchResult> batch;
	batch.reserve(config.chains.size());

	const int chainCount = config.chains.size();
	const int totalSteps = chainCount * targets.size();

	for (int chainIndex = 0; chainIndex < config.chains.size(); ++chainIndex)
	{
		if (cancelFlag != nullptr && *cancelFlag)
		{
			break;
		}

		const GadgetChainSpec& spec = config.chains[chainIndex];
		GadgetChainBatchResult one {};
		one.chainQuery = spec.chainQuery;
		one.maxPerDll = spec.maxPerDll;

		if (chainStarted)
		{
			chainStarted(spec);
		}

		const int baseStep = chainIndex * targets.size();
		GadgetScanProgressFn chainProgress;
		if (progress)
		{
			chainProgress = [progress, baseStep, totalSteps](
								int current,
								int /*perChainTotal*/,
								const GadgetFileResult& fileResult) {
				progress(baseStep + current, totalSteps, fileResult);
			};
		}

		one.files = ScanGadgetTargets(
			targets,
			spec.chainQuery,
			cancelFlag,
			spec.maxPerDll,
			chainProgress);
		batch.push_back(one);
	}

	return batch;
}
