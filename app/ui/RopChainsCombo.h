// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QComboBox>

class RopChainsCombo : public QComboBox
{
	Q_OBJECT

public:
	explicit RopChainsCombo(QWidget* parent = nullptr);

signals:
	void refreshRequested();

protected:
	void showPopup() override;
};
