// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GadgetIndexStore.h"

#include "GadgetDisasm.h"
#include "GadgetIndexRules.h"
#include "GadgetChainNorm.h"
#include "InstrMatch.h"
#include "PeImage.h"

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QVector>

namespace
{
struct DecodedStep
{
	size_t offsetInSection = 0;
	uint8_t size = 0;
	QString text;
};

QString FormatHexBytes(const uint8_t* data, size_t size)
{
	QString out;
	out.reserve(static_cast<int>(size * 3));
	for (size_t i = 0; i < size; ++i)
	{
		if (i != 0)
		{
			out.append(QLatin1Char(' '));
		}
		out.append(QStringLiteral("%1").arg(data[i], 2, 16, QLatin1Char('0')));
	}
	return out;
}

QString JoinNormalizedChainText(const QVector<DecodedStep>& steps, int fromIndex)
{
	QStringList parts;
	for (int i = fromIndex; i < steps.size(); ++i)
	{
		parts.append(NormalizeInstrForIndex(steps[i].text));
	}
	return parts.join(QStringLiteral("; "));
}

bool VerifyDecodedStepsSlice(
	const uint8_t* sectionBytes,
	size_t sectionSize,
	const QVector<DecodedStep>& steps,
	int fromIndex)
{
	if (fromIndex < 0 || fromIndex >= steps.size())
	{
		return false;
	}

	size_t pos = steps[fromIndex].offsetInSection;
	for (int i = fromIndex; i < steps.size(); ++i)
	{
		if (pos >= sectionSize)
		{
			return false;
		}

		const size_t remaining = sectionSize - pos;
		uint8_t instrSize = 0;
		QString text;
		if (!DisassembleOne64(sectionBytes + pos, remaining, 0, text, instrSize) || instrSize == 0)
		{
			return false;
		}
		if (instrSize != steps[i].size)
		{
			return false;
		}
		if (NormalizeInstrForIndex(text) != NormalizeInstrForIndex(steps[i].text))
		{
			return false;
		}
		pos += instrSize;
	}

	const size_t expectedEnd = steps.back().offsetInSection + steps.back().size;
	return pos == expectedEnd;
}

void EmitSuffixRecords(
	const PeExecSection& sec,
	const uint8_t* sectionBytes,
	size_t sectionSize,
	uint32_t sectionRva,
	const QVector<DecodedStep>& steps,
	QTextStream& out,
	QSet<QString>& seenKeys)
{
	for (int start = 0; start < steps.size(); ++start)
	{
		const int nonRetCount = steps.size() - 1 - start;
		if (nonRetCount <= 0)
		{
			continue;
		}
		if (nonRetCount > kMaxInsnsBeforeRet)
		{
			continue;
		}

		const size_t byteStart = steps[start].offsetInSection;
		const size_t byteEnd = steps.back().offsetInSection + steps.back().size;
		if (byteEnd > sectionSize || byteEnd <= byteStart)
		{
			continue;
		}

		if (!VerifyDecodedStepsSlice(sectionBytes, sectionSize, steps, start))
		{
			continue;
		}

		const uint32_t rva = sectionRva + static_cast<uint32_t>(byteStart);
		const QString key = QStringLiteral("%1:%2").arg(sec.name).arg(rva);
		if (seenKeys.contains(key))
		{
			continue;
		}
		seenKeys.insert(key);

		const QString chainNorm = JoinNormalizedChainText(steps, start);
		QJsonObject row {
			{ QStringLiteral("section"), sec.name.toLower() },
			{ QStringLiteral("rva"), static_cast<qint64>(rva) },
			{ QStringLiteral("chain"), chainNorm },
			{ QStringLiteral("insn_count"), CountInstructionsInChainText(chainNorm) },
			{ QStringLiteral("bytes"), FormatHexBytes(sectionBytes + byteStart, byteEnd - byteStart) },
		};
		out << QJsonDocument(row).toJson(QJsonDocument::Compact) << QLatin1Char('\n');
	}
}

void BuildSectionIndex(
	const PeExecSection& sec,
	const PeImage64& pe,
	QTextStream& out,
	QSet<QString>& seenKeys)
{
	if (sec.rawSize == 0)
	{
		return;
	}

	const uint8_t* sectionBytes = pe.bytes.data() + sec.rawOffset;
	const size_t sectionSize = sec.rawSize;

	for (size_t start = 0; start < sectionSize; ++start)
	{
		QVector<DecodedStep> steps;
		size_t pos = start;

		while (pos < sectionSize)
		{
			const size_t remaining = sectionSize - pos;
			uint8_t instrSize = 0;
			QString text;
			if (!DisassembleOne64(sectionBytes + pos, remaining, 0, text, instrSize))
			{
				break;
			}
			if (IsFarReturnDisasmLine(text))
			{
				break;
			}

			uint8_t retSize = 0;
			if (IsGadgetTailRetC3(sectionBytes + pos, remaining, retSize))
			{
				DecodedStep step {};
				step.offsetInSection = pos;
				step.size = retSize;
				step.text = text;
				steps.push_back(step);
				EmitSuffixRecords(
					sec,
					sectionBytes,
					sectionSize,
					sec.rva,
					steps,
					out,
					seenKeys);
				break;
			}

			if (steps.size() >= static_cast<size_t>(kMaxInsnsBeforeRet))
			{
				break;
			}

			DecodedStep step {};
			step.offsetInSection = pos;
			step.size = instrSize;
			step.text = text;
			steps.push_back(step);
			pos += instrSize;
		}
	}
}
} // namespace

bool BuildGadgetIndexDatabase(
	const QString& peFilePath,
	const QString& indexPath,
	const QString& /*sha256Hex*/,
	QString& errorOut)
{
	PeImage64 pe {};
	if (!LoadPe64Image(peFilePath, pe, errorOut))
	{
		return false;
	}

	QSaveFile outFile(indexPath);
	if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		errorOut = QStringLiteral("cannot write index: %1").arg(indexPath);
		return false;
	}

	QTextStream stream(&outFile);
	stream.setCodec("UTF-8");

	QSet<QString> seenKeys;
	for (const PeExecSection& sec : pe.execSections)
	{
		BuildSectionIndex(sec, pe, stream, seenKeys);
	}

	stream.flush();
	if (stream.status() != QTextStream::Ok)
	{
		errorOut = QStringLiteral("index stream write failed: %1").arg(indexPath);
		return false;
	}
	if (seenKeys.isEmpty())
	{
		errorOut = QStringLiteral("index produced no gadgets (check PE sections / disassembler)");
		return false;
	}

	if (!outFile.commit())
	{
		errorOut = QStringLiteral("index write failed: %1").arg(indexPath);
		return false;
	}

	return true;
}

bool QueryGadgetIndexDatabase(
	const QString& indexPath,
	uint64_t imageBase,
	const QStringList& queryParts,
	volatile bool* cancelFlag,
	int maxHitsPerDll,
	GadgetFileResult& resultOut,
	QString& errorOut)
{
	QFile file(indexPath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		errorOut = QStringLiteral("cannot open index: %1").arg(indexPath);
		return false;
	}

	QHash<QString, GadgetSectionHits> bySection;
	int remaining = maxHitsPerDll < 0 ? -1 : maxHitsPerDll;
	QSet<uint64_t> seenVa;

	QTextStream in(&file);
	while (!in.atEnd())
	{
		if (cancelFlag != nullptr && *cancelFlag)
		{
			break;
		}
		if (remaining == 0)
		{
			break;
		}

		const QJsonObject row = QJsonDocument::fromJson(in.readLine().toUtf8()).object();
		if (row.isEmpty())
		{
			continue;
		}

		const int insnCount = row.value(QStringLiteral("insn_count")).toInt(-1);
		if (insnCount >= 0 && insnCount != queryParts.size())
		{
			continue;
		}

		const QString chainText = row.value(QStringLiteral("chain")).toString();
		if (!MatchGadgetChainText(queryParts, chainText))
		{
			continue;
		}

		const QString sectionName = row.value(QStringLiteral("section")).toString();
		const uint32_t rva = static_cast<uint32_t>(row.value(QStringLiteral("rva")).toVariant().toULongLong());
		const uint64_t va = imageBase + rva;
		if (seenVa.contains(va))
		{
			continue;
		}
		seenVa.insert(va);

		GadgetHit hit {};
		hit.rva = rva;
		hit.va = va;
		hit.chainText = chainText;
		hit.bytesHex = row.value(QStringLiteral("bytes")).toString();

		GadgetSectionHits& sectionHits = bySection[sectionName];
		if (sectionHits.sectionName.isEmpty())
		{
			sectionHits.sectionName = sectionName;
		}
		sectionHits.hits.push_back(hit);

		if (remaining > 0)
		{
			--remaining;
		}
	}

	for (auto it = bySection.constBegin(); it != bySection.constEnd(); ++it)
	{
		if (!it.value().hits.isEmpty())
		{
			resultOut.sections.push_back(it.value());
		}
	}

	return true;
}
