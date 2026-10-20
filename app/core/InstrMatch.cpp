// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "InstrMatch.h"

#include "GadgetChainNorm.h"

#include <QRegularExpression>

namespace
{
QString SqueezeSpaces(QString s)
{
	s = s.simplified();
	return s;
}

QString CanonicalizeProbeInstructionToken(const QString& part)
{
	if (IsGadgetAnyInstructionSlot(part))
	{
		return part;
	}

	const QString indexed = NormalizeInstrForIndex(part);
	const QStringList words = indexed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (words.isEmpty())
	{
		return indexed;
	}

	const QString mnem = words[0];
	if (words.size() == 1)
	{
		return mnem;
	}

	// NormalizeInstrForIndex already uses spaces between operands; re-insert commas for display.
	const QStringList operands = words.mid(1);
	return mnem + QLatin1Char(' ') + operands.join(QStringLiteral(", "));
}
} // namespace

QString GadgetAnyInstructionSlotToken()
{
	return QStringLiteral("*");
}

bool IsGadgetAnyInstructionSlot(const QString& queryPart)
{
	return queryPart == GadgetAnyInstructionSlotToken();
}

bool ValidateGadgetChainQuery(const QString& query, QString& errorOut)
{
	const QStringList parts = ParseGadgetChainQuery(query);
	if (parts.isEmpty())
	{
		errorOut = QStringLiteral("empty gadget chain (use ; between instructions)");
		return false;
	}

	int anySlotCount = 0;
	for (const QString& part : parts)
	{
		if (IsGadgetAnyInstructionSlot(part))
		{
			++anySlotCount;
		}
	}

	constexpr int kMaxAnyInstructionSlots = 3;
	if (anySlotCount > kMaxAnyInstructionSlots)
	{
		errorOut = QStringLiteral(
			"at most %1 wildcard instruction slot(s) per chain "
			"(use consecutive semicolons: mov x; ret = 2 insns, mov x;; ret = 3 with one arbitrary middle)")
			.arg(kMaxAnyInstructionSlots);
		return false;
	}

	return true;
}

QString FormatNormalizedGadgetChainQuery(const QString& query)
{
	const QStringList parts = ParseGadgetChainQuery(query);
	QStringList normalizedParts;
	normalizedParts.reserve(parts.size());
	for (const QString& part : parts)
	{
		if (IsGadgetAnyInstructionSlot(part))
		{
			normalizedParts.append(part);
		}
		else
		{
			normalizedParts.append(CanonicalizeProbeInstructionToken(part));
		}
	}
	return normalizedParts.join(QStringLiteral("; "));
}

QStringList ParseGadgetChainQuery(const QString& query)
{
	QStringList parts;
	QString current;

	const auto flushCurrent = [&]() {
		const QString trimmed = SqueezeSpaces(current.trimmed());
		if (!trimmed.isEmpty())
		{
			parts.append(trimmed);
		}
		current.clear();
	};

	for (int i = 0; i < query.size();)
	{
		const QChar ch = query[i];
		if (ch == QLatin1Char(';'))
		{
			flushCurrent();

			int semiCount = 0;
			while (i < query.size())
			{
				while (i < query.size() && query[i].isSpace())
				{
					++i;
				}
				if (i >= query.size() || query[i] != QLatin1Char(';'))
				{
					break;
				}
				++semiCount;
				++i;
			}

			if (semiCount > 1)
			{
				for (int slot = 0; slot < semiCount - 1; ++slot)
				{
					parts.append(GadgetAnyInstructionSlotToken());
				}
			}
			continue;
		}

		current.append(ch);
		++i;
	}

	flushCurrent();
	return parts;
}

bool IsFarReturnDisasm(const QString& disasmLine)
{
	static const QRegularExpression kRetfRe(
		QStringLiteral("\\bretf\\b"),
		QRegularExpression::CaseInsensitiveOption);
	return kRetfRe.match(disasmLine).hasMatch();
}

QString NormalizeInstrToken(const QString& token)
{
	return NormalizeInstrForIndex(token);
}

bool InstrTokensMatch(const QString& queryToken, const QString& disasmLine)
{
	if (IsFarReturnDisasm(disasmLine))
	{
		return false;
	}

	const QString q = NormalizeInstrToken(queryToken);
	if (q.isEmpty())
	{
		return false;
	}

	const QString dNorm = NormalizeInstrToken(disasmLine);
	if (dNorm == q)
	{
		return true;
	}

	// Allow omitting ptr/size decorations on either side.
	if (dNorm.contains(q))
	{
		return true;
	}

	QString qCompact = q;
	qCompact.remove(QLatin1Char(' '));
	QString dCompact = dNorm;
	dCompact.remove(QLatin1Char(' '));
	if (!qCompact.isEmpty() && dCompact == qCompact)
	{
		return true;
	}

	return false;
}

bool MatchGadgetChainText(const QStringList& queryParts, const QString& gadgetChainText)
{
	const QString chainNorm = NormalizeGadgetChainText(gadgetChainText);
	QStringList chainParts;
	for (const QString& part : chainNorm.split(QLatin1Char(';'), Qt::SkipEmptyParts))
	{
		const QString trimmed = part.trimmed();
		if (!trimmed.isEmpty())
		{
			chainParts.append(trimmed);
		}
	}

	if (queryParts.size() != chainParts.size())
	{
		return false;
	}

	for (int i = 0; i < queryParts.size(); ++i)
	{
		if (IsGadgetAnyInstructionSlot(queryParts[i]))
		{
			continue;
		}
		if (!InstrTokensMatch(queryParts[i], chainParts[i]))
		{
			return false;
		}
	}
	return true;
}
