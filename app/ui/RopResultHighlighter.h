// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class RopResultHighlighter : public QSyntaxHighlighter
{
public:
	explicit RopResultHighlighter(QTextDocument* document);

protected:
	void highlightBlock(const QString& text) override;

private:
	QTextCharFormat m_chainHeaderFormat;
};
