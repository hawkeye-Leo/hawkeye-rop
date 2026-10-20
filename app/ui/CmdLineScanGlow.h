// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>

class QLineEdit;

class CmdLineScanGlow : public QWidget
{
    Q_OBJECT

public:
    explicit CmdLineScanGlow(QLineEdit* targetLineEdit);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onTextChanged(const QString& text);
    void onTick();

private:
    void startScan();

    QLineEdit* m_lineEdit = nullptr;
    QTimer m_timer;
    QElapsedTimer m_scanClock;
    bool m_scanning = false;
    int m_lastTextLength = 0;

    static const int kScanDurationMs = 420;
    static const int kTickMs = 16;
};
