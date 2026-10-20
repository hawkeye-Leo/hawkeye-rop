// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GadgetChainNorm.h"

#include <QRegularExpression>

namespace
{
QString SqueezeSpaces(QString s)
{
	return s.simplified();
}

QString StripPtrDecorations(QString s)
{
	static const QRegularExpression kPtrRe(
		QStringLiteral("\\b(?:byte|word|dword|qword|xmmword|ymmword|zmmword)\\s+ptr\\s+"),
		QRegularExpression::CaseInsensitiveOption);
	return s.remove(kPtrRe);
}

QString UnifyRetAlias(QString s)
{
	static const QRegularExpression kRetnRe(QStringLiteral("\\bretn\\b"), QRegularExpression::CaseInsensitiveOption);
	s.replace(kRetnRe, QStringLiteral("ret"));
	return s;
}

QString StripLeadingZerosHex(QString digits)
{
	digits = digits.toLower();
	while (digits.size() > 1 && digits.startsWith(QLatin1Char('0')))
	{
		digits.remove(0, 1);
	}
	return digits;
}

QString HexDigitsToIdaSuffix(const QString& digits)
{
	const QString trimmed = StripLeadingZerosHex(digits);
	if (trimmed.isEmpty())
	{
		return QStringLiteral("0h");
	}
	return trimmed + QStringLiteral("h");
}

QString NormalizeHexLiterals(QString s)
{
	{
		static const QRegularExpression k0x(
			QStringLiteral("\\b0x([0-9a-fA-F]+)\\b"),
			QRegularExpression::CaseInsensitiveOption);
		for (int pos = 0;;)
		{
			const QRegularExpressionMatch match = k0x.match(s, pos);
			if (!match.hasMatch())
			{
				break;
			}
			const QString replacement = HexDigitsToIdaSuffix(match.captured(1));
			s.replace(match.capturedStart(0), match.capturedLength(0), replacement);
			pos = match.capturedStart(0) + replacement.size();
		}
	}

	{
		static const QRegularExpression kH(
			QStringLiteral("\\b([0-9a-fA-F]+)h\\b"),
			QRegularExpression::CaseInsensitiveOption);
		for (int pos = 0;;)
		{
			const QRegularExpressionMatch match = kH.match(s, pos);
			if (!match.hasMatch())
			{
				break;
			}
			const QString replacement = HexDigitsToIdaSuffix(match.captured(1));
			s.replace(match.capturedStart(0), match.capturedLength(0), replacement);
			pos = match.capturedStart(0) + replacement.size();
		}
	}

	return s;
}

QString NormalizeBracketSpacing(QString s)
{
	static const QRegularExpression kBracket(
		QStringLiteral("\\[([^\\]]*)\\]"),
		QRegularExpression::CaseInsensitiveOption);

	int offset = 0;
	for (;;)
	{
		const QRegularExpressionMatch match = kBracket.match(s, offset);
		if (!match.hasMatch())
		{
			break;
		}
		QString inner = match.captured(1);
		inner.remove(QLatin1Char(' '));
		const QString replacement = QStringLiteral("[%1]").arg(inner);
		s.replace(match.capturedStart(0), match.capturedLength(0), replacement);
		offset = match.capturedStart(0) + replacement.size();
	}
	return s;
}
} // namespace

QString NormalizeInstrForIndex(const QString& instrToken)
{
	QString s = SqueezeSpaces(instrToken.trimmed()).toLower();
	s = StripPtrDecorations(s);
	s = UnifyRetAlias(s);
	s.replace(QLatin1Char(','), QLatin1Char(' '));
	s = SqueezeSpaces(s);
	s = NormalizeHexLiterals(s);
	s = NormalizeBracketSpacing(s);
	return s;
}

QString NormalizeGadgetChainText(const QString& chainText)
{
	QStringList parts;
	for (const QString& part : chainText.split(QLatin1Char(';'), Qt::SkipEmptyParts))
	{
		const QString trimmed = part.trimmed();
		if (!trimmed.isEmpty())
		{
			parts.append(NormalizeInstrForIndex(trimmed));
		}
	}
	return parts.join(QStringLiteral("; "));
}

int CountInstructionsInChainText(const QString& normalizedChainText)
{
	if (normalizedChainText.trimmed().isEmpty())
	{
		return 0;
	}
	int count = 0;
	for (const QString& part : normalizedChainText.split(QLatin1Char(';'), Qt::SkipEmptyParts))
	{
		if (!part.trimmed().isEmpty())
		{
			++count;
		}
	}
	return count;
}
