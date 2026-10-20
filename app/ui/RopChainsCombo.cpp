// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RopChainsCombo.h"

RopChainsCombo::RopChainsCombo(QWidget* parent)
	: QComboBox(parent)
{
}

void RopChainsCombo::showPopup()
{
	emit refreshRequested();
	QComboBox::showPopup();
}
