// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>

bool DisassembleOne64(
	const uint8_t* code,
	size_t codeSize,
	uint64_t rip,
	QString& textOut,
	uint8_t& instrSizeOut);

// Single-byte 0xC3 ret only (ROP gadget tail).
bool IsGadgetTailRetC3(const uint8_t* code, size_t codeSize, uint8_t& instrSizeOut);

bool IsFarReturnDisasmLine(const QString& disasmLine);
