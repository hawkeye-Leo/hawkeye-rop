// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

// Bump when index semantics change (forces rebuild of cached databases).
constexpr int kGadgetIndexRulesVersion = 6;

// Max non-ret instructions before a trailing 0xC3 ret in a stored gadget.
constexpr int kMaxInsnsBeforeRet = 3;

constexpr quint8 kGadgetRetOpcodeC3 = 0xC3;
