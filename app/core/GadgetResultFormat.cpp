// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GadgetResultFormat.h"

#include <QFileInfo>

namespace
{
constexpr int kAddrFieldWidth = 18; // 0x + 16 hex digits
constexpr int kBytesSlotCount = 8;  // reserve up to 8 byte cells (xx xx ...)
constexpr int kBytesFieldWidth = kBytesSlotCount * 3 - 1;

QString FormatAddressField(quint64 value)
{
	return QStringLiteral("0x%1").arg(value, 16, 16, QChar('0'));
}

QString FormatBytesField(const QString& bytesHex)
{
	QString bytes = bytesHex.trimmed().toLower();
	return bytes.leftJustified(kBytesFieldWidth, QLatin1Char(' '));
}

QString FormatHitLine(const GadgetHit& hit)
{
	return QStringLiteral("    %1  %2  %3  %4\n")
		.arg(FormatAddressField(hit.rva), -kAddrFieldWidth)
		.arg(FormatAddressField(hit.va), -kAddrFieldWidth)
		.arg(FormatBytesField(hit.bytesHex), -kBytesFieldWidth)
		.arg(hit.chainText);
}

void AppendSectionColumnHeader(QString& out)
{
	out.append(QStringLiteral("    %1  %2  %3  %4\n")
		.arg(QStringLiteral("RVA"), -kAddrFieldWidth)
		.arg(QStringLiteral("VA"), -kAddrFieldWidth)
		.arg(QStringLiteral("Bytes"), -kBytesFieldWidth)
		.arg(QStringLiteral("Chain")));
}

void AppendFileResults(QString& out, const GadgetFileResult& file)
{
	out.append(file.filePath);
	out.append(QLatin1Char('\n'));

	if (!file.skipReason.isEmpty())
	{
		out.append(QStringLiteral("  (skipped: %1)\n").arg(file.skipReason));
		return;
	}

	if (file.sections.isEmpty())
	{
		out.append(QStringLiteral("  (no matches)\n"));
		return;
	}

	for (const GadgetSectionHits& section : file.sections)
	{
		out.append(QStringLiteral("  %1:\n").arg(section.sectionName));
		AppendSectionColumnHeader(out);
		for (const GadgetHit& hit : section.hits)
		{
			out.append(FormatHitLine(hit));
		}
	}
}

QString formatSingleFileScanBlock(const GadgetFileResult& file)
{
	QString out;
	AppendFileResults(out, file);
	out.append(QLatin1Char('\n'));
	return out;
}
} // namespace

QString FormatSingleFileScanBlock(const GadgetFileResult& file)
{
	return formatSingleFileScanBlock(file);
}

QString FormatFileScanStatusLine(const GadgetFileResult& file)
{
	if (file.filePath.isEmpty())
	{
		return QStringLiteral("(not scanned)");
	}

	if (!file.skipReason.isEmpty())
	{
		return QStringLiteral("skipped — %1").arg(file.skipReason);
	}

	int hitCount = 0;
	for (const GadgetSectionHits& section : file.sections)
	{
		hitCount += section.hits.size();
	}

	if (hitCount == 0)
	{
		return QStringLiteral("no matches");
	}

	return QStringLiteral("%1 hit(s)").arg(hitCount);
}

QString FormatScanManifest(const QVector<GadgetFileResult>& files)
{
	QString out;
	int scannedCount = 0;
	for (const GadgetFileResult& file : files)
	{
		if (!file.filePath.isEmpty())
		{
			++scannedCount;
		}
	}

	out.append(QStringLiteral("=== Scanned modules (%1) ===\n").arg(scannedCount));

	int index = 1;
	for (const GadgetFileResult& file : files)
	{
		if (file.filePath.isEmpty())
		{
			continue;
		}

		const QString name = QFileInfo(file.filePath).fileName();
		out.append(QStringLiteral("  %1. %2  —  %3\n")
			.arg(index++, 3)
			.arg(name)
			.arg(FormatFileScanStatusLine(file)));
	}

	out.append(QLatin1Char('\n'));
	return out;
}

QString FormatScanResults(const QVector<GadgetFileResult>& results)
{
	QString out;
	for (const GadgetFileResult& file : results)
	{
		AppendFileResults(out, file);
	}
	return out;
}

QString FormatProbeScanResults(
	const QString& chainQuery,
	int maxPerDll,
	const QVector<GadgetFileResult>& files)
{
	QString out;
	out.append(QStringLiteral("=== Probe: [%1] (max %2 per DLL) ===\n\n")
		.arg(chainQuery)
		.arg(maxPerDll));

	if (files.isEmpty())
	{
		out.append(QStringLiteral("(no PE files scanned)\n"));
		return out;
	}

	for (const GadgetFileResult& file : files)
	{
		AppendFileResults(out, file);
	}
	return out;
}

QString FormatConfigScanResults(const QVector<GadgetChainBatchResult>& batch)
{
	QString out;
	for (int i = 0; i < batch.size(); ++i)
	{
		const GadgetChainBatchResult& chain = batch[i];
		if (i != 0)
		{
			out.append(QLatin1Char('\n'));
		}
		out.append(QStringLiteral("=== [%1] (max %2 per DLL) ===\n\n")
			.arg(chain.chainQuery)
			.arg(chain.maxPerDll));

		if (chain.files.isEmpty())
		{
			out.append(QStringLiteral("(no PE files scanned)\n"));
			continue;
		}

		for (const GadgetFileResult& file : chain.files)
		{
			AppendFileResults(out, file);
		}
	}
	return out;
}

QString FormatConfigProfilePreview(const QString& profileName, const GadgetConfigDocument& config)
{
	QString out;
	out.append(QStringLiteral("=== Profile: %1 ===\n\n").arg(profileName));

	if (!config.headerLine.isEmpty())
	{
		out.append(config.headerLine);
		out.append(QLatin1Char('\n'));
	}

	out.append(QStringLiteral("%1 rule(s) — max hits per DLL in [N]\n\n").arg(config.chains.size()));

	int index = 1;
	for (const GadgetChainSpec& spec : config.chains)
	{
		out.append(QStringLiteral("  %1. [%2]  [%3]\n")
			.arg(index++)
			.arg(spec.chainQuery)
			.arg(spec.maxPerDll));
	}

	out.append(QStringLiteral(
		"\n;; = one arbitrary instruction slot between fixed parts.\n"
		"Select another profile to replace this view. Run batch to scan targets.\n"));
	return out;
}

QString FormatConfigProfilePreviewError(const QString& profileName, const QString& error)
{
	return QStringLiteral("=== Profile: %1 ===\n\n%2\n").arg(profileName, error);
}
