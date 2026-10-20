// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

// Per-instruction text for index storage and phase-2 matching (lowercase, no ptr, IDA-style hex).
QString NormalizeInstrForIndex(const QString& instrToken);

// Full gadget chain: normalize each instruction separated by ';', join with "; ".
QString NormalizeGadgetChainText(const QString& chainText);

int CountInstructionsInChainText(const QString& normalizedChainText);
