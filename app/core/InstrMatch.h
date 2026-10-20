// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

// Between fixed instructions, N semicolons (spaces allowed) inserts N-1 arbitrary slots.
QStringList ParseGadgetChainQuery(const QString& query);
bool IsGadgetAnyInstructionSlot(const QString& queryPart);
bool ValidateGadgetChainQuery(const QString& query, QString& errorOut);
// Canonical index-style chain text for display and scan (after ParseGadgetChainQuery).
QString FormatNormalizedGadgetChainQuery(const QString& query);
QString NormalizeInstrToken(const QString& token);
bool IsFarReturnDisasm(const QString& disasmLine);
bool InstrTokensMatch(const QString& queryToken, const QString& disasmLine);

bool MatchGadgetChainText(const QStringList& queryParts, const QString& gadgetChainText);
