// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HawkeyeRopWindow.h"
#include "resource.h"

#include "core/ChainConfig.h"
#include "core/GadgetScanner.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QIcon>

#include <Windows.h>
#include <Shobjidl.h>

namespace
{
void applyNativeWindowIcon(QWidget* widget)
{
	if (widget == nullptr)
	{
		return;
	}

	const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
	if (hwnd == nullptr)
	{
		return;
	}

	const HINSTANCE instance = GetModuleHandleW(nullptr);
	const HICON bigIcon = static_cast<HICON>(LoadImageW(
		instance,
		MAKEINTRESOURCEW(IDI_ICON1),
		IMAGE_ICON,
		GetSystemMetrics(SM_CXICON),
		GetSystemMetrics(SM_CYICON),
		LR_DEFAULTCOLOR | LR_SHARED));
	const HICON smallIcon = static_cast<HICON>(LoadImageW(
		instance,
		MAKEINTRESOURCEW(IDI_ICON1),
		IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON),
		GetSystemMetrics(SM_CYSMICON),
		LR_DEFAULTCOLOR | LR_SHARED));

	if (bigIcon != nullptr)
	{
		SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
	}
	if (smallIcon != nullptr)
	{
		SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
	}
}
} // namespace

int main(int argc, char* argv[])
{
	qRegisterMetaType<GadgetFileResult>("GadgetFileResult");
	qRegisterMetaType<QVector<GadgetFileResult>>("QVector<GadgetFileResult>");
	qRegisterMetaType<GadgetChainBatchResult>("GadgetChainBatchResult");
	qRegisterMetaType<QVector<GadgetChainBatchResult>>("QVector<GadgetChainBatchResult>");

	SetCurrentProcessExplicitAppUserModelID(L"Hawkeye.HawkeyeRop");

	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
	QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
	QApplication app(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("Hawkeye ROP"));
	QCoreApplication::setOrganizationName(QStringLiteral("Hawkeye"));

	QFont uiFont(QStringLiteral("Segoe UI"), 9);
	app.setFont(uiFont);

	QIcon icon(QCoreApplication::applicationFilePath());
	if (icon.isNull())
	{
		icon = QIcon(QStringLiteral(":/HawkeyeRop/AppIcon.png"));
	}
	app.setWindowIcon(icon);

	HawkeyeRopWindow window;
	window.setWindowIcon(icon);
	window.show();
	applyNativeWindowIcon(&window);

	return app.exec();
}
