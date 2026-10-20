// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "GadgetScanner.h"

#include <QString>
#include <QStringList>

bool BuildGadgetIndexDatabase(
	const QString& peFilePath,
	const QString& indexPath,
	const QString& sha256Hex,
	QString& errorOut);

bool QueryGadgetIndexDatabase(
	const QString& indexPath,
	uint64_t imageBase,
	const QStringList& queryParts,
	volatile bool* cancelFlag,
	int maxHitsPerDll,
	GadgetFileResult& resultOut,
	QString& errorOut);
