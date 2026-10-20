// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GadgetDisasm.h"

#include "GadgetIndexRules.h"

#include <QRegularExpression>

extern "C" {
#include "bddisasm.h"
}

namespace
{
constexpr uint8_t kDefCode = ND_CODE_64;
constexpr uint8_t kDefData = ND_DATA_64;
}

bool DisassembleOne64(
	const uint8_t* code,
	size_t codeSize,
	uint64_t rip,
	QString& textOut,
	uint8_t& instrSizeOut)
{
	INSTRUX instrux {};
	const NDSTATUS st = NdDecodeEx(&instrux, code, codeSize, kDefCode, kDefData);
	if (!ND_SUCCESS(st))
	{
		return false;
	}

	char buffer[256] = {};
	const NDSTATUS textSt = NdToText(&instrux, rip, static_cast<uint32_t>(sizeof(buffer)), buffer);
	if (!ND_SUCCESS(textSt))
	{
		return false;
	}

	textOut = QString::fromLatin1(buffer).trimmed();
	instrSizeOut = instrux.Length;
	return instrSizeOut > 0;
}

bool IsFarReturnDisasmLine(const QString& disasmLine)
{
	static const QRegularExpression kRetfRe(
		QStringLiteral("\\bretf\\b"),
		QRegularExpression::CaseInsensitiveOption);
	return kRetfRe.match(disasmLine).hasMatch();
}

bool IsGadgetTailRetC3(const uint8_t* code, size_t codeSize, uint8_t& instrSizeOut)
{
	instrSizeOut = 0;
	if (codeSize == 0)
	{
		return false;
	}

	QString text;
	if (!DisassembleOne64(code, codeSize, 0, text, instrSizeOut))
	{
		return false;
	}
	if (IsFarReturnDisasmLine(text))
	{
		return false;
	}
	if (instrSizeOut != 1 || code[0] != kGadgetRetOpcodeC3)
	{
		return false;
	}

	// bddisasm labels single-byte 0xC3 as RETN, not RET.
	static const QRegularExpression kNearRetRe(
		QStringLiteral("^retn?\\b"),
		QRegularExpression::CaseInsensitiveOption);
	return kNearRetRe.match(text).hasMatch();
}
