// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/GadgetScanner.h"

#include <QtGlobal>

#include <QVector>

namespace RopProcessModules
{

bool isPlausibleUserPid(quint32 pid);

bool buildScanTargetsFromProcess(quint32 pid, QVector<GadgetScanTarget>& out, QString& errorOut);

} // namespace RopProcessModules
