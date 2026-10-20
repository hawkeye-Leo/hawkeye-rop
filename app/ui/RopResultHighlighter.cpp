// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RopResultHighlighter.h"

#include "HawkeyeStyle.h"

RopResultHighlighter::RopResultHighlighter(QTextDocument* document)
	: QSyntaxHighlighter(document)
{
	m_chainHeaderFormat.setForeground(HawkeyeStyle::kMnemonic);
	m_chainHeaderFormat.setFontWeight(QFont::DemiBold);
}

void RopResultHighlighter::highlightBlock(const QString& text)
{
	const QString trimmed = text.trimmed();
	if (trimmed.startsWith(QStringLiteral("===")) && trimmed.endsWith(QStringLiteral("===")))
	{
		setFormat(0, text.length(), m_chainHeaderFormat);
	}
}
