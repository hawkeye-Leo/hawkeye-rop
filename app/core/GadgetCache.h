// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <functional>

QString DefaultGadgetCacheRoot();
void SetGadgetCacheRootForSettings(const QString& path);
QString GadgetCacheRoot();

QString Sha256HexOfFile(const QString& filePath, QString& errorOut);

QString GadgetIndexDirectoryForPe(const QString& peFilePath, const QString& sha256Hex);

bool GadgetIndexCacheIsValid(const QString& indexDir, const QString& sha256Hex, int rulesVersion);

bool EnsureGadgetIndexForPe(
	const QString& peFilePath,
	QString& indexDbPathOut,
	QString& errorOut,
	const std::function<void(const QString& statusLine)>& status = nullptr);
