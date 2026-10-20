// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "PeImage.h"

#include <QString>
#include <QStringList>
#include <QMetaType>
#include <QVector>

#include <QtGlobal>

#include <functional>

struct GadgetHit
{
	uint32_t rva = 0;
	uint64_t va = 0;
	QString bytesHex;
	QString chainText;
};

struct GadgetSectionHits
{
	QString sectionName;
	QVector<GadgetHit> hits;
};

struct GadgetFileResult
{
	QString filePath;
	QString skipReason;
	QVector<GadgetSectionHits> sections;
};

struct GadgetScanTarget
{
	QString filePath;
	quint64 loadedImageBase = 0;
};

QVector<GadgetScanTarget> BuildScanTargetsFromDirectory(const QString& directoryPath);

using GadgetScanProgressFn =
	std::function<void(int current, int total, const GadgetFileResult& fileResult)>;

QVector<GadgetFileResult> ScanGadgetTargets(
	const QVector<GadgetScanTarget>& targets,
	const QString& chainQuery,
	volatile bool* cancelFlag,
	int maxHitsPerDll = -1,
	GadgetScanProgressFn progress = nullptr);

Q_DECLARE_METATYPE(GadgetFileResult)
Q_DECLARE_METATYPE(QVector<GadgetFileResult>)
