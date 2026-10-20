// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PeImage.h"

#include <Windows.h>

#include <QFile>

namespace
{
QString SectionNameFromHeader(const IMAGE_SECTION_HEADER& sec)
{
	char name[IMAGE_SIZEOF_SHORT_NAME + 1] = {};
	std::memcpy(name, sec.Name, IMAGE_SIZEOF_SHORT_NAME);
	return QString::fromLatin1(name).trimmed();
}

bool IsExecutableSection(const IMAGE_SECTION_HEADER& sec)
{
	const DWORD ch = sec.Characteristics;
	return (ch & IMAGE_SCN_MEM_EXECUTE) != 0 || (ch & IMAGE_SCN_CNT_CODE) != 0;
}
} // namespace

bool LoadPe64Image(const QString& filePath, PeImage64& out, QString& errorOut)
{
	out = PeImage64 {};
	out.filePath = filePath;

	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		errorOut = QStringLiteral("cannot open file");
		return false;
	}

	const QByteArray data = file.readAll();
	if (data.size() < static_cast<int>(sizeof(IMAGE_DOS_HEADER)))
	{
		errorOut = QStringLiteral("file too small for PE");
		return false;
	}

	out.bytes.assign(reinterpret_cast<const uint8_t*>(data.constData()),
		reinterpret_cast<const uint8_t*>(data.constData()) + data.size());
	const uint8_t* base = out.bytes.data();
	const size_t fileSize = out.bytes.size();

	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
	{
		errorOut = QStringLiteral("not a PE (DOS header)");
		return false;
	}
	if (dos->e_lfanew <= 0
		|| static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > fileSize)
	{
		errorOut = QStringLiteral("invalid PE headers");
		return false;
	}

	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE)
	{
		errorOut = QStringLiteral("not a PE (NT signature)");
		return false;
	}
	if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
	{
		errorOut = QStringLiteral("not AMD64");
		return false;
	}
	if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
	{
		errorOut = QStringLiteral("not PE32+");
		return false;
	}

	out.imageBase = nt->OptionalHeader.ImageBase;

	const WORD sectionCount = nt->FileHeader.NumberOfSections;
	const auto* section = IMAGE_FIRST_SECTION(nt);
	for (WORD i = 0; i < sectionCount; ++i)
	{
		if (!IsExecutableSection(section[i]))
		{
			continue;
		}
		if (section[i].SizeOfRawData == 0 || section[i].PointerToRawData == 0)
		{
			continue;
		}
		const size_t rawOff = section[i].PointerToRawData;
		const size_t rawSize = section[i].SizeOfRawData;
		if (rawOff >= fileSize || rawOff + rawSize > fileSize)
		{
			continue;
		}

		PeExecSection sec {};
		sec.name = SectionNameFromHeader(section[i]);
		sec.rva = section[i].VirtualAddress;
		sec.virtualSize = section[i].Misc.VirtualSize;
		sec.rawSize = static_cast<uint32_t>(rawSize);
		sec.rawOffset = static_cast<uint32_t>(rawOff);
		out.execSections.push_back(sec);
	}

	if (out.execSections.isEmpty())
	{
		errorOut = QStringLiteral("no executable sections with raw data");
		return false;
	}

	return true;
}
