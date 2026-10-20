// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "HawkeyeStyle.h"
#include "HawkeyeTitleBar.h"

#include <QColor>
#include <QWidget>

namespace HawkeyeRopChrome
{

inline QColor wineAccent()
{
	return QColor(QStringLiteral("#8e2452"));
}

inline QString wineButtonGradient()
{
	return QStringLiteral(
		"qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #a83266, stop:1 #6d1c40)");
}

inline QString wineButtonGradientHover()
{
	return QStringLiteral(
		"qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #c04078, stop:1 #8e2452)");
}

inline QString panelStyle()
{
	const QString grad = wineButtonGradient();
	const QString gradHover = wineButtonGradientHover();
	return QStringLiteral(
		"QFrame#ropToolPanel {"
		"  background-color: #fafcfa;"
		"  border: 1px solid #d4a8b8;"
		"  border-radius: 4px;"
		"}"
		"QFrame#ropToolPanel QLabel#labelTargets,"
		"QFrame#ropToolPanel QLabel#labelBatch,"
		"QFrame#ropToolPanel QLabel#labelProbe {"
		"  color: #8e2452;"
		"  font-family: 'Segoe UI', sans-serif;"
		"  font-size: 12px;"
		"  font-weight: 700;"
		"  min-width: 54px;"
		"  max-width: 54px;"
		"}"
		"QLabel#labelProcessPid {"
		"  min-width: 0;"
		"  max-width: 16777215;"
		"  font-size: 11px;"
		"  font-weight: 600;"
		"  color: #8e6a78;"
		"}"
		"QCheckBox#checkScanProcessModules {"
		"  font-size: 11px;"
		"  font-weight: 600;"
		"  color: #6d1c40;"
		"  spacing: 6px;"
		"}"
		"QLineEdit#lineEditProcessPid {"
		"  font-weight: 600;"
		"  font-size: 12px;"
		"  color: #6d1c40;"
		"  border: 1px solid #d4a8b8;"
		"  border-radius: 3px;"
		"  padding: 0 6px;"
		"  min-height: 30px;"
		"  max-height: 30px;"
		"  background: #ffffff;"
		"}"
		"QLabel#labelProbeMax {"
		"  color: #8e6a78;"
		"  font-size: 11px;"
		"  font-weight: 600;"
		"  min-width: 0;"
		"  max-width: 16777215;"
		"  padding: 0 2px 0 6px;"
		"}"
		"QLineEdit#lineEditTargetsDir,"
		"QLineEdit#lineEditProcessPid,"
		"QComboBox#comboProbeChain {"
		"  color: #4a1028;"
		"  font-weight: 600;"
		"  font-size: 12px;"
		"  min-height: 30px;"
		"  max-height: 30px;"
		"  padding: 2px 8px;"
		"  border: 1px solid #d4a8b8;"
		"  border-radius: 3px;"
		"  background: #ffffff;"
		"}"
		"QComboBox#comboProbeChain {"
		"  font-family: Consolas, 'Courier New', monospace;"
		"}"
		"QLineEdit#lineEditTargetsDir:focus,"
		"QComboBox#comboProbeChain:focus { border: 1px solid #8e2452; }"
		"QComboBox#comboProbeChain QAbstractItemView {"
		"  background: #ffffff;"
		"  color: #4a1028;"
		"  border: 1px solid #8e2452;"
		"  selection-background-color: #8e2452;"
		"  selection-color: #ffffff;"
		"}"
		"QSpinBox#spinProbeMaxPerDll {"
		"  font-weight: 600;"
		"  font-size: 12px;"
		"  color: #6d1c40;"
		"  border: 1px solid #d4a8b8;"
		"  border-radius: 3px;"
		"  padding: 0 4px;"
		"  min-height: 30px;"
		"  max-height: 30px;"
		"  background: #ffffff;"
		"}"
		"QComboBox#comboBoxChains {"
		"  color: #ffffff;"
		"  font-weight: 600;"
		"  font-size: 12px;"
		"  min-height: 30px;"
		"  max-height: 30px;"
		"  padding: 2px 10px;"
		"  border: 1px solid #6d1c40;"
		"  border-radius: 3px;"
		"  background: %1;"
		"}"
		"QComboBox#comboBoxChains:hover { background: %2; }"
		"QComboBox#comboBoxChains:focus { border: 1px solid #ffffff; }"
		"QComboBox#comboBoxChains::drop-down {"
		"  subcontrol-origin: padding;"
		"  subcontrol-position: top right;"
		"  width: 22px;"
		"  border: none;"
		"  background: transparent;"
		"}"
		"QComboBox#comboBoxChains QAbstractItemView {"
		"  background: #fff8fa;"
		"  color: #6d1c40;"
		"  border: 1px solid #8e2452;"
		"  padding: 2px;"
		"  outline: none;"
		"  selection-background-color: #8e2452;"
		"  selection-color: #ffffff;"
		"}"
		"QComboBox#comboBoxChains QAbstractItemView::item {"
		"  min-height: 26px;"
		"  padding: 4px 10px;"
		"}"
		"QComboBox#comboBoxChains QAbstractItemView::item:selected,"
		"QComboBox#comboBoxChains QAbstractItemView::item:hover {"
		"  background-color: #8e2452;"
		"  color: #ffffff;"
		"}"
		"QPlainTextEdit#plainTextResults {"
		"  background: #fafcfa;"
		"  border: none;"
		"  padding: 8px 10px;"
		"  selection-background-color: #e8b4c8;"
		"  selection-color: #4a1028;"
		"}"
		"QFrame#ropToolPanel QPushButton {"
		"  font-family: 'Segoe UI', sans-serif;"
		"  font-size: 12px;"
		"  font-weight: 700;"
		"  min-width: 72px;"
		"  min-height: 30px;"
		"  max-height: 30px;"
		"  padding: 0 12px;"
		"  border-radius: 3px;"
		"  color: #ffffff;"
		"  border: 1px solid #5a1630;"
		"  background: %1;"
		"}"
		"QFrame#ropToolPanel QPushButton:hover { background: %2; }"
		"QFrame#ropToolPanel QPushButton:pressed {"
		"  background: #5a1630;"
		"}"
		"QFrame#ropToolPanel QPushButton:disabled {"
		"  background: #e0e0e0;"
		"  color: #999999;"
		"  border: 1px solid #c8c8c8;"
		"}"
		"QPushButton#pushButtonScan,"
		"QPushButton#pushButtonProbeRun {"
		"  min-width: 84px;"
		"}")
		.arg(grad, gradHover);
}

inline void applyTitleBar(HawkeyeTitleBar* bar)
{
	if (bar != nullptr)
	{
		bar->setChromeAccent(wineAccent());
	}
}

inline void apply(QWidget* root)
{
	if (root == nullptr)
	{
		return;
	}
	HawkeyeStyle::applyChrome(root);
	root->setStyleSheet(root->styleSheet() + panelStyle());
}

} // namespace HawkeyeRopChrome
