// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVector>

#include <cstdint>
#include <vector>

struct PeExecSection
{
	QString name;
	uint32_t rva = 0;
	uint32_t virtualSize = 0;
	uint32_t rawSize = 0;
	uint32_t rawOffset = 0;
};

struct PeImage64
{
	QString filePath;
	uint64_t imageBase = 0;
	QVector<PeExecSection> execSections;
	std::vector<uint8_t> bytes;
};

bool LoadPe64Image(const QString& filePath, PeImage64& out, QString& errorOut);
