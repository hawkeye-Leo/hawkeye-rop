// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "GadgetScanner.h"

#include <QString>
#include <QVector>

#include <functional>

struct GadgetChainSpec
{
	QString chainQuery;
	int maxPerDll = 1;
};

struct GadgetConfigDocument
{
	QString headerLine;
	QVector<GadgetChainSpec> chains;
};

struct GadgetChainBatchResult
{
	QString chainQuery;
	int maxPerDll = 1;
	QVector<GadgetFileResult> files;
};

bool LoadGadgetConfigFile(const QString& filePath, GadgetConfigDocument& out, QString& errorOut);

using GadgetChainStartedFn = std::function<void(const GadgetChainSpec& spec)>;

QVector<GadgetChainBatchResult> ScanTargetsFromConfig(
	const QVector<GadgetScanTarget>& targets,
	const GadgetConfigDocument& config,
	volatile bool* cancelFlag,
	GadgetScanProgressFn progress = nullptr,
	GadgetChainStartedFn chainStarted = nullptr);

Q_DECLARE_METATYPE(GadgetChainBatchResult)
Q_DECLARE_METATYPE(QVector<GadgetChainBatchResult>)
