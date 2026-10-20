// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ProcessModuleEnum.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Psapi.h>

#include <QFileInfo>
#include <QSet>

namespace
{
bool enableDebugPrivilege()
{
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
	{
		return false;
	}

	TOKEN_PRIVILEGES privileges = {};
	privileges.PrivilegeCount = 1;
	if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &privileges.Privileges[0].Luid))
	{
		CloseHandle(token);
		return false;
	}
	privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
	const DWORD adjustError = GetLastError();
	CloseHandle(token);
	return adjustError == ERROR_SUCCESS;
}

bool isDllPath(const QString& path)
{
	return QFileInfo(path).suffix().compare(QStringLiteral("dll"), Qt::CaseInsensitive) == 0;
}

bool isWinSxSPath(const QString& path)
{
	return path.contains(QStringLiteral("\\winsxs\\"), Qt::CaseInsensitive);
}
} // namespace

namespace RopProcessModules
{

bool isPlausibleUserPid(quint32 pid)
{
	return pid > 4 && pid < 0xFFFFFFFEu && (pid % 4) == 0;
}

bool buildScanTargetsFromProcess(quint32 pid, QVector<GadgetScanTarget>& out, QString& errorOut)
{
	out.clear();
	errorOut.clear();

	if (!isPlausibleUserPid(pid))
	{
		errorOut = QStringLiteral("invalid PID (use a running user process PID, typically > 4 and aligned to 4)");
		return false;
	}

	enableDebugPrivilege();

	HANDLE process =
		OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid));
	if (process == nullptr || process == INVALID_HANDLE_VALUE)
	{
		errorOut = QStringLiteral("OpenProcess failed for PID %1 (error %2). Try running Hawkeye ROP as administrator.")
			.arg(pid)
			.arg(static_cast<qulonglong>(GetLastError()));
		return false;
	}

	HMODULE modules[4096] = {};
	DWORD bytesNeeded = 0;
	if (!EnumProcessModulesEx(
			process,
			modules,
			sizeof(modules),
			&bytesNeeded,
			LIST_MODULES_ALL))
	{
		const DWORD err = GetLastError();
		CloseHandle(process);
		errorOut = QStringLiteral("EnumProcessModulesEx failed (error %1)").arg(static_cast<qulonglong>(err));
		return false;
	}

	const DWORD moduleCount = bytesNeeded / sizeof(HMODULE);
	QSet<QString> seenPaths;

	for (DWORD i = 0; i < moduleCount; ++i)
	{
		wchar_t pathBuffer[MAX_PATH + 512] = {};
		const DWORD pathLen = GetModuleFileNameExW(process, modules[i], pathBuffer, MAX_PATH + 512);
		if (pathLen == 0)
		{
			continue;
		}

		const QString path = QString::fromWCharArray(pathBuffer, static_cast<int>(pathLen));
		if (!isDllPath(path) || isWinSxSPath(path))
		{
			continue;
		}

		const QString key = path.toLower();
		if (seenPaths.contains(key))
		{
			continue;
		}
		seenPaths.insert(key);

		if (!QFileInfo::exists(path))
		{
			continue;
		}

		MODULEINFO moduleInfo = {};
		if (!GetModuleInformation(process, modules[i], &moduleInfo, sizeof(moduleInfo)))
		{
			continue;
		}

		GadgetScanTarget target {};
		target.filePath = path;
		target.loadedImageBase = reinterpret_cast<quint64>(moduleInfo.lpBaseOfDll);
		out.push_back(target);
	}

	CloseHandle(process);

	if (out.isEmpty())
	{
		errorOut = QStringLiteral("no on-disk DLL modules found for PID %1").arg(pid);
		return false;
	}

	return true;
}

} // namespace RopProcessModules
