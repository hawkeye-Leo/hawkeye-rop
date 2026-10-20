// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GadgetCache.h"

#include "GadgetIndexRules.h"
#include "GadgetIndexStore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace
{
QString g_cacheRootOverride;

bool IsObsoleteGadgetCacheRoot(const QString& path)
{
	const QString normalized = QDir::fromNativeSeparators(path.trimmed());
	if (normalized.isEmpty())
	{
		return false;
	}

	static const QRegularExpression kLegacyCacheRe(
		QStringLiteral("hawkeye\\s*lab[/\\\\]gadgets"),
		QRegularExpression::CaseInsensitiveOption);
	if (kLegacyCacheRe.match(normalized).hasMatch())
	{
		return true;
	}

	const QString exeDir = QDir::fromNativeSeparators(QCoreApplication::applicationDirPath());
	const QString oldNextToExe = QDir(exeDir).filePath(QStringLiteral("cache/gadgets"));
	return QDir::cleanPath(normalized).compare(QDir::cleanPath(oldNextToExe), Qt::CaseInsensitive) == 0;
}
} // namespace

QString DefaultGadgetCacheRoot()
{
	return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("gadgets"));
}

void SetGadgetCacheRootForSettings(const QString& path)
{
	QString trimmed = path.trimmed();
	if (IsObsoleteGadgetCacheRoot(trimmed))
	{
		trimmed.clear();
	}

	const QString def = DefaultGadgetCacheRoot();
	if (!trimmed.isEmpty()
		&& QDir::cleanPath(QDir::fromNativeSeparators(trimmed))
			.compare(QDir::cleanPath(def), Qt::CaseInsensitive) == 0)
	{
		trimmed.clear();
	}

	g_cacheRootOverride = trimmed;
}

QString GadgetCacheRoot()
{
	if (!g_cacheRootOverride.isEmpty())
	{
		return g_cacheRootOverride;
	}
	return DefaultGadgetCacheRoot();
}

QString Sha256HexOfFile(const QString& filePath, QString& errorOut)
{
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		errorOut = QStringLiteral("cannot open for hash: %1").arg(filePath);
		return {};
	}

	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file))
	{
		errorOut = QStringLiteral("hash read failed: %1").arg(filePath);
		return {};
	}
	return QString::fromLatin1(hash.result().toHex());
}

QString GadgetIndexDirectoryForPe(const QString& peFilePath, const QString& sha256Hex)
{
	const QString moduleName = QFileInfo(peFilePath).fileName();
	return QDir(GadgetCacheRoot()).filePath(QStringLiteral("%1/%2/r%3")
		.arg(moduleName, sha256Hex.toUpper())
		.arg(kGadgetIndexRulesVersion));
}

bool GadgetIndexCacheIsValid(const QString& indexDir, const QString& sha256Hex, int rulesVersion)
{
	const QString metaPath = QDir(indexDir).filePath(QStringLiteral("meta.json"));
	const QString dbPath = QDir(indexDir).filePath(QStringLiteral("gadgets.jsonl"));
	if (!QFileInfo::exists(metaPath) || !QFileInfo::exists(dbPath))
	{
		return false;
	}

	const QFileInfo dbInfo(dbPath);
	if (!dbInfo.isFile() || dbInfo.size() <= 0)
	{
		return false;
	}

	QFile metaFile(metaPath);
	if (!metaFile.open(QIODevice::ReadOnly))
	{
		return false;
	}
	const QJsonObject obj = QJsonDocument::fromJson(metaFile.readAll()).object();
	return obj.value(QStringLiteral("sha256")).toString().compare(sha256Hex, Qt::CaseInsensitive) == 0
		&& obj.value(QStringLiteral("rulesVersion")).toInt() == rulesVersion;
}

bool EnsureGadgetIndexForPe(
	const QString& peFilePath,
	QString& indexDbPathOut,
	QString& errorOut,
	const std::function<void(const QString& statusLine)>& status)
{
	indexDbPathOut.clear();
	QString sha256 = Sha256HexOfFile(peFilePath, errorOut);
	if (sha256.isEmpty())
	{
		return false;
	}

	const QString indexDir = GadgetIndexDirectoryForPe(peFilePath, sha256);
	indexDbPathOut = QDir(indexDir).filePath(QStringLiteral("gadgets.jsonl"));

	if (GadgetIndexCacheIsValid(indexDir, sha256, kGadgetIndexRulesVersion))
	{
		if (status)
		{
			status(QStringLiteral("index cache hit: %1").arg(QFileInfo(peFilePath).fileName()));
		}
		return true;
	}

	QDir().mkpath(indexDir);
	if (status)
	{
		status(QStringLiteral("building index: %1").arg(QFileInfo(peFilePath).fileName()));
	}

	if (!BuildGadgetIndexDatabase(peFilePath, indexDbPathOut, sha256, errorOut))
	{
		return false;
	}

	const QJsonObject meta {
		{ QStringLiteral("sha256"), sha256 },
		{ QStringLiteral("rulesVersion"), kGadgetIndexRulesVersion },
		{ QStringLiteral("sourcePath"), QFileInfo(peFilePath).absoluteFilePath() },
	};
	QFile metaOut(QDir(indexDir).filePath(QStringLiteral("meta.json")));
	if (metaOut.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		metaOut.write(QJsonDocument(meta).toJson(QJsonDocument::Indented));
	}

	return true;
}
