// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ChainConfig.h"
#include "GadgetScanner.h"

#include <QString>

QString FormatScanResults(const QVector<GadgetFileResult>& results);
QString FormatScanManifest(const QVector<GadgetFileResult>& files);
QString FormatFileScanStatusLine(const GadgetFileResult& file);
QString FormatSingleFileScanBlock(const GadgetFileResult& file);
QString FormatConfigScanResults(const QVector<GadgetChainBatchResult>& batch);
QString FormatProbeScanResults(
	const QString& chainQuery,
	int maxPerDll,
	const QVector<GadgetFileResult>& files);
QString FormatConfigProfilePreview(const QString& profileName, const GadgetConfigDocument& config);
QString FormatConfigProfilePreviewError(const QString& profileName, const QString& error);
