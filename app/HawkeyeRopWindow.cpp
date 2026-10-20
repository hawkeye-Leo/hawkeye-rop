// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HawkeyeRopWindow.h"

#include "core/GadgetCache.h"
#include "core/GadgetResultFormat.h"
#include "core/GadgetIndexRules.h"
#include "core/InstrMatch.h"
#include "process/ProcessModuleEnum.h"
#include "scan/GadgetScanWorker.h"
#include "CmdLineScanGlow.h"
#include "ConsoleBorderGlow.h"
#include "HawkeyeRopChrome.h"
#include "HawkeyeTitleBar.h"
#include "RopChainsCombo.h"
#include "RopResultHighlighter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPalette>
#include <QSet>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTextCursor>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace
{
constexpr int kMaxProbeHistory = 32;
constexpr int kResizeBorderPx = 6;

int CountHitsInFileResults(const QVector<GadgetFileResult>& files)
{
	int hits = 0;
	for (const GadgetFileResult& file : files)
	{
		for (const GadgetSectionHits& section : file.sections)
		{
			hits += section.hits.size();
		}
	}
	return hits;
}
}

HawkeyeRopWindow::HawkeyeRopWindow(QWidget* parent)
	: QMainWindow(parent)
{
	ui.setupUi(this);
	setMinimumSize(480, 320);
	ui.toolGridLayout->setColumnStretch(0, 0);
	ui.toolGridLayout->setColumnStretch(1, 1);
	ui.toolGridLayout->setColumnStretch(2, 0);
	ui.toolGridLayout->setColumnMinimumWidth(2, 196);

	HawkeyeTitleBar::attach(this, HawkeyeTitleBar::MinMaxClose);
	HawkeyeRopChrome::apply(this);

	if (auto* titleBar = qobject_cast<HawkeyeTitleBar*>(menuWidget()))
	{
		titleBar->setTitle(QStringLiteral("Hawkeye ROP"));
		HawkeyeRopChrome::applyTitleBar(titleBar);
	}

	QFont mono(QStringLiteral("Consolas"));
	mono.setStyleHint(QFont::Monospace);
	ui.plainTextResults->setFont(mono);

	QPalette consolePalette = ui.plainTextResults->palette();
	consolePalette.setColor(QPalette::Text, QColor(QStringLiteral("#222222")));
	consolePalette.setColor(QPalette::Base, QColor(QStringLiteral("#fafcfa")));
	ui.plainTextResults->setPalette(consolePalette);
	new ConsoleBorderGlow(ui.plainTextResults);
	new RopResultHighlighter(ui.plainTextResults->document());
	new CmdLineScanGlow(ui.lineEditTargetsDir);

	ui.comboProbeChain->setDuplicatesEnabled(false);
	if (QLineEdit* probeEdit = ui.comboProbeChain->lineEdit())
	{
		probeEdit->setPlaceholderText(QStringLiteral("pop rcx; ret"));
		probeEdit->setToolTip(
			QStringLiteral(
				"Instructions separated by ;\n"
				";; = 1 arbitrary instruction, ;;; = 2, ;;;; = 3 (max 3 slots)\n"
				"Enter or Run probe"));
		new CmdLineScanGlow(probeEdit);
		connect(probeEdit, &QLineEdit::returnPressed, this, &HawkeyeRopWindow::onProbeRunClicked);
	}

	if (auto* chainsCombo = qobject_cast<RopChainsCombo*>(ui.comboBoxChains))
	{
		connect(chainsCombo, &RopChainsCombo::refreshRequested, this, &HawkeyeRopWindow::refreshChainsCombo);
	}
	connect(
		ui.comboBoxChains,
		QOverload<int>::of(&QComboBox::currentIndexChanged),
		this,
		&HawkeyeRopWindow::onBatchProfileChanged);

	loadSettings();
	loadProbeHistory();

	if (!isMaximized())
	{
		QSettings settings(
			QSettings::IniFormat,
			QSettings::UserScope,
			QStringLiteral("Hawkeye"),
			QStringLiteral("Rop"));
		const QByteArray geom = settings.value(QStringLiteral("windowGeometry")).toByteArray();
		if (!geom.isEmpty())
		{
			restoreGeometry(geom);
		}
		else
		{
			resize(880, 600);
		}
	}
	ui.lineEditTargetsDir->setText(defaultTargetsDir());
	updateTargetSourceUi();
	refreshChainsCombo();
	showBatchProfilePreview();

	ui.checkScanProcessModules->setToolTip(
		QStringLiteral(
			"When checked, batch/probe scan DLL files loaded in the chosen process "
			"(from disk paths). VA uses the module base in that process.\n"
			"Requires permission to open the process (admin may be needed)."));
	connect(
		ui.checkScanProcessModules,
		&QCheckBox::toggled,
		this,
		&HawkeyeRopWindow::onProcessSourceToggled);

	connect(ui.pushButtonBrowseDir, &QPushButton::clicked, this, &HawkeyeRopWindow::onBrowseTargetsDir);
	connect(ui.pushButtonScan, &QPushButton::clicked, this, &HawkeyeRopWindow::onScanClicked);
	connect(ui.pushButtonProbeRun, &QPushButton::clicked, this, &HawkeyeRopWindow::onProbeRunClicked);
	connect(ui.pushButtonCancel, &QPushButton::clicked, this, &HawkeyeRopWindow::onCancelClicked);
}

HawkeyeRopWindow::~HawkeyeRopWindow()
{
	if (!isMaximized())
	{
		QSettings settings(
			QSettings::IniFormat,
			QSettings::UserScope,
			QStringLiteral("Hawkeye"),
			QStringLiteral("Rop"));
		settings.setValue(QStringLiteral("windowGeometry"), saveGeometry());
	}
	saveSettings();
	if (m_worker != nullptr)
	{
		m_worker->requestCancel();
		m_worker->wait(3000);
	}
}

void HawkeyeRopWindow::showEvent(QShowEvent* event)
{
	QMainWindow::showEvent(event);
	refreshChainsCombo();
}

#if defined(Q_OS_WIN)
bool HawkeyeRopWindow::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
	if (eventType != "windows_generic_MSG" || isMaximized())
	{
		return QMainWindow::nativeEvent(eventType, message, result);
	}

	const MSG* msg = static_cast<MSG*>(message);
	if (msg->message != WM_NCHITTEST)
	{
		return QMainWindow::nativeEvent(eventType, message, result);
	}

	RECT rc = {};
	GetWindowRect(reinterpret_cast<HWND>(winId()), &rc);
	const POINT pt = { GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam) };
	const int border = kResizeBorderPx;
	const bool left = pt.x < rc.left + border;
	const bool right = pt.x >= rc.right - border;
	const bool top = pt.y < rc.top + border;
	const bool bottom = pt.y >= rc.bottom - border;

	if (top && left)
	{
		*result = HTTOPLEFT;
		return true;
	}
	if (top && right)
	{
		*result = HTTOPRIGHT;
		return true;
	}
	if (bottom && left)
	{
		*result = HTBOTTOMLEFT;
		return true;
	}
	if (bottom && right)
	{
		*result = HTBOTTOMRIGHT;
		return true;
	}
	if (left)
	{
		*result = HTLEFT;
		return true;
	}
	if (right)
	{
		*result = HTRIGHT;
		return true;
	}
	if (top)
	{
		*result = HTTOP;
		return true;
	}
	if (bottom)
	{
		*result = HTBOTTOM;
		return true;
	}

	return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

QString HawkeyeRopWindow::defaultTargetsDir() const
{
	return QCoreApplication::applicationDirPath() + QStringLiteral("/targets");
}

QString HawkeyeRopWindow::chainsConfigDir() const
{
	return QCoreApplication::applicationDirPath() + QStringLiteral("/chains");
}

QString HawkeyeRopWindow::selectedChainsFilePath() const
{
	const QString name = ui.comboBoxChains->currentText().trimmed();
	if (name.isEmpty() || name.startsWith(QLatin1Char('(')))
	{
		return QString();
	}
	return QDir(chainsConfigDir()).absoluteFilePath(name);
}

QString HawkeyeRopWindow::currentProbeChainText() const
{
	return ui.comboProbeChain->currentText().trimmed();
}

void HawkeyeRopWindow::loadProbeHistory()
{
	QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Hawkeye"), QStringLiteral("Rop"));
	const QStringList history =
		settings.value(QStringLiteral("probeHistory")).toStringList();
	ui.comboProbeChain->clear();
	for (const QString& entry : history)
	{
		if (!entry.trimmed().isEmpty())
		{
			ui.comboProbeChain->addItem(entry.trimmed());
		}
	}

	const QString lastProbe = settings.value(QStringLiteral("lastProbeChain")).toString().trimmed();
	if (!lastProbe.isEmpty())
	{
		ui.comboProbeChain->setCurrentText(lastProbe);
	}
	else if (ui.comboProbeChain->count() > 0)
	{
		ui.comboProbeChain->setCurrentIndex(0);
	}

	ui.spinProbeMaxPerDll->setValue(settings.value(QStringLiteral("probeMaxPerDll"), 3).toInt());
}

void HawkeyeRopWindow::rememberProbeChain(const QString& chain)
{
	const QString trimmed = chain.trimmed();
	if (trimmed.isEmpty())
	{
		return;
	}

	QStringList history;
	for (int i = 0; i < ui.comboProbeChain->count(); ++i)
	{
		const QString item = ui.comboProbeChain->itemText(i).trimmed();
		if (!item.isEmpty() && item.compare(trimmed, Qt::CaseInsensitive) != 0)
		{
			history.push_back(item);
		}
	}
	history.prepend(trimmed);
	while (history.size() > kMaxProbeHistory)
	{
		history.removeLast();
	}

	{
		const QSignalBlocker blocker(ui.comboProbeChain);
		ui.comboProbeChain->clear();
		ui.comboProbeChain->addItems(history);
	}
	ui.comboProbeChain->setCurrentText(trimmed);

	QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Hawkeye"), QStringLiteral("Rop"));
	settings.setValue(QStringLiteral("probeHistory"), history);
	settings.setValue(QStringLiteral("lastProbeChain"), trimmed);
}

void HawkeyeRopWindow::onBatchProfileChanged(int index)
{
	if (index < 0)
	{
		return;
	}
	if (m_worker != nullptr && m_worker->isRunning())
	{
		return;
	}

	saveSettings();
	showBatchProfilePreview();
}

void HawkeyeRopWindow::showBatchProfilePreview()
{
	const QString profileName = ui.comboBoxChains->currentText().trimmed();
	if (profileName.isEmpty() || profileName.startsWith(QLatin1Char('(')))
	{
		ui.plainTextResults->clear();
		ui.plainTextResults->setPlainText(
			QStringLiteral("Add .chains files to the chains folder, then pick a batch profile."));
		return;
	}

	const QString configPath = selectedChainsFilePath();
	GadgetConfigDocument config {};
	QString configError;
	if (!LoadGadgetConfigFile(configPath, config, configError))
	{
		ui.plainTextResults->clear();
		ui.plainTextResults->setPlainText(FormatConfigProfilePreviewError(profileName, configError));
		return;
	}

	ui.plainTextResults->clear();
	ui.plainTextResults->setPlainText(FormatConfigProfilePreview(profileName, config));
}

void HawkeyeRopWindow::refreshChainsCombo()
{
	const QString previous = ui.comboBoxChains->currentText();
	const QSignalBlocker blocker(ui.comboBoxChains);
	ui.comboBoxChains->clear();

	const QStringList files =
		QDir(chainsConfigDir()).entryList({ QStringLiteral("*.chains") }, QDir::Files, QDir::Name);
	if (files.isEmpty())
	{
		ui.comboBoxChains->addItem(QStringLiteral("(no .chains in chains folder)"));
		ui.comboBoxChains->setEnabled(false);
		ui.pushButtonScan->setEnabled(false);
	}
	else
	{
		QSet<QString> seenLower;
		QStringList uniqueFiles;
		for (const QString& file : files)
		{
			const QString key = file.toLower();
			if (seenLower.contains(key))
			{
				continue;
			}
			seenLower.insert(key);
			uniqueFiles.push_back(file);
		}

		ui.comboBoxChains->setEnabled(true);
		ui.comboBoxChains->addItems(uniqueFiles);

		const int prevIndex = ui.comboBoxChains->findText(previous);
		if (prevIndex >= 0)
		{
			ui.comboBoxChains->setCurrentIndex(prevIndex);
		}
		else
		{
			QSettings settings(
				QSettings::IniFormat,
				QSettings::UserScope,
				QStringLiteral("Hawkeye"),
				QStringLiteral("Rop"));
			const QString saved = settings.value(QStringLiteral("lastChainsFile")).toString();
			const int savedIndex = ui.comboBoxChains->findText(saved);
			if (savedIndex >= 0)
			{
				ui.comboBoxChains->setCurrentIndex(savedIndex);
			}
		}
	}

	const bool idle = m_worker == nullptr || !m_worker->isRunning();
	ui.pushButtonScan->setEnabled(idle && ui.comboBoxChains->isEnabled());
	ui.pushButtonProbeRun->setEnabled(idle);
}

void HawkeyeRopWindow::loadSettings()
{
	QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Hawkeye"), QStringLiteral("Rop"));
	if (settings.contains(QStringLiteral("targetsDir")))
	{
		ui.lineEditTargetsDir->setText(settings.value(QStringLiteral("targetsDir")).toString());
	}
	ui.checkScanProcessModules->setChecked(settings.value(QStringLiteral("scanProcessModules"), false).toBool());
	ui.lineEditProcessPid->setText(settings.value(QStringLiteral("processPid")).toString());

	const QString savedCacheDir = settings.value(QStringLiteral("gadgetCacheDir")).toString();
	SetGadgetCacheRootForSettings(savedCacheDir);
	if (settings.contains(QStringLiteral("gadgetCacheDir")))
	{
		settings.remove(QStringLiteral("gadgetCacheDir"));
	}
}

void HawkeyeRopWindow::saveSettings()
{
	QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Hawkeye"), QStringLiteral("Rop"));
	settings.setValue(QStringLiteral("targetsDir"), ui.lineEditTargetsDir->text().trimmed());
	const QString chainsName = ui.comboBoxChains->currentText().trimmed();
	if (!chainsName.isEmpty() && !chainsName.startsWith(QLatin1Char('(')))
	{
		settings.setValue(QStringLiteral("lastChainsFile"), chainsName);
	}
	settings.setValue(QStringLiteral("lastProbeChain"), currentProbeChainText());
	settings.setValue(QStringLiteral("probeMaxPerDll"), ui.spinProbeMaxPerDll->value());
	settings.setValue(QStringLiteral("scanProcessModules"), ui.checkScanProcessModules->isChecked());
	settings.setValue(QStringLiteral("processPid"), ui.lineEditProcessPid->text().trimmed());
}

void HawkeyeRopWindow::onBrowseTargetsDir()
{
	const QString dir = QFileDialog::getExistingDirectory(
		this,
		QStringLiteral("Select targets directory"),
		ui.lineEditTargetsDir->text());
	if (!dir.isEmpty())
	{
		ui.lineEditTargetsDir->setText(dir);
	}
}

void HawkeyeRopWindow::onProcessSourceToggled(bool /*enabled*/)
{
	updateTargetSourceUi();
	saveSettings();
}

void HawkeyeRopWindow::updateTargetSourceUi()
{
	const bool scanning = m_worker != nullptr && m_worker->isRunning();
	const bool processMode = ui.checkScanProcessModules->isChecked();

	ui.labelProcessPid->setEnabled(processMode && !scanning);
	ui.lineEditProcessPid->setEnabled(processMode && !scanning);
	ui.lineEditTargetsDir->setEnabled(!processMode && !scanning);
	ui.pushButtonBrowseDir->setEnabled(!processMode && !scanning);
}

bool HawkeyeRopWindow::buildScanRequest(GadgetScanRequest& requestOut, QString& errorOut) const
{
	requestOut = GadgetScanRequest {};
	requestOut.targetsDir = ui.lineEditTargetsDir->text().trimmed();

	if (ui.checkScanProcessModules->isChecked())
	{
		requestOut.useProcessModules = true;
		bool pidOk = false;
		const quint32 pidValue = ui.lineEditProcessPid->text().trimmed().toUInt(&pidOk);
		if (!pidOk)
		{
			errorOut = QStringLiteral("Enter a numeric process PID.");
			return false;
		}
		requestOut.processId = static_cast<quint32>(pidValue);
		if (!RopProcessModules::isPlausibleUserPid(requestOut.processId))
		{
			errorOut = QStringLiteral(
				"Enter a valid process PID (user processes are usually > 4 and a multiple of 4).");
			return false;
		}
		return true;
	}

	if (requestOut.targetsDir.isEmpty())
	{
		errorOut = QStringLiteral("Select a targets directory or enable process module scan.");
		return false;
	}

	return true;
}

QString HawkeyeRopWindow::scanTargetBanner() const
{
	if (m_lastScanUsedProcessModules)
	{
		return QStringLiteral("=== Target: PID %1 (loaded DLLs on disk) ===\n\n")
			.arg(m_lastScanProcessId);
	}

	return QStringLiteral("=== Target: %1 ===\n\n").arg(ui.lineEditTargetsDir->text().trimmed());
}

void HawkeyeRopWindow::appendScanResultText(const QString& text)
{
	ui.plainTextResults->appendPlainText(text);
	QTextCursor cursor = ui.plainTextResults->textCursor();
	cursor.movePosition(QTextCursor::End);
	ui.plainTextResults->setTextCursor(cursor);
}

void HawkeyeRopWindow::setScanning(bool active)
{
	ui.pushButtonScan->setEnabled(!active && ui.comboBoxChains->isEnabled());
	ui.pushButtonProbeRun->setEnabled(!active);
	ui.pushButtonCancel->setEnabled(active);
	ui.comboBoxChains->setEnabled(!active && ui.comboBoxChains->count() > 0
		&& !ui.comboBoxChains->currentText().startsWith(QLatin1Char('(')));
	ui.comboProbeChain->setEnabled(!active);
	ui.spinProbeMaxPerDll->setEnabled(!active);
	ui.checkScanProcessModules->setEnabled(!active);
	updateTargetSourceUi();
}

void HawkeyeRopWindow::startWorker(GadgetScanWorker* worker, bool probeMode)
{
	if (probeMode)
	{
		connect(
			worker,
			&GadgetScanWorker::scanFinished,
			this,
			&HawkeyeRopWindow::onProbeScanFinished,
			Qt::QueuedConnection);
	}
	else
	{
		connect(
			worker,
			&GadgetScanWorker::configScanFinished,
			this,
			&HawkeyeRopWindow::onConfigScanFinished,
			Qt::QueuedConnection);
		connect(
			worker,
			&GadgetScanWorker::scanChainStarted,
			this,
			&HawkeyeRopWindow::onScanChainStarted,
			Qt::QueuedConnection);
	}
	connect(
		worker,
		&GadgetScanWorker::scanFailed,
		this,
		&HawkeyeRopWindow::onScanFailed,
		Qt::QueuedConnection);
	connect(
		worker,
		&GadgetScanWorker::scanProgress,
		this,
		&HawkeyeRopWindow::onScanProgress,
		Qt::QueuedConnection);
	connect(worker, &GadgetScanWorker::finished, worker, &QObject::deleteLater);

	setScanning(true);
	worker->start();
}

void HawkeyeRopWindow::onScanClicked()
{
	if (m_worker != nullptr && m_worker->isRunning())
	{
		return;
	}

	const QString configPath = selectedChainsFilePath();
	if (configPath.isEmpty() || !QFileInfo::exists(configPath))
	{
		QMessageBox::warning(
			this,
			QStringLiteral("Hawkeye ROP"),
			QStringLiteral("Put .chains files in the chains folder next to HawkeyeRop.exe"));
		return;
	}

	GadgetScanRequest request {};
	QString requestError;
	if (!buildScanRequest(request, requestError))
	{
		QMessageBox::warning(this, QStringLiteral("Hawkeye ROP"), requestError);
		return;
	}

	saveSettings();
	m_lastScanUsedProcessModules = request.useProcessModules;
	m_lastScanProcessId = request.processId;

	ui.plainTextResults->clear();
	appendScanResultText(scanTargetBanner());
	appendScanResultText(QStringLiteral("Batch scan in progress — each module appears when done.\n"));

	m_worker = new GadgetScanWorker(this);
	m_worker->configureConfigFile(request, configPath);
	startWorker(m_worker, false);
}

void HawkeyeRopWindow::onProbeRunClicked()
{
	if (m_worker != nullptr && m_worker->isRunning())
	{
		return;
	}

	const QString chain = currentProbeChainText();
	QString chainError;
	if (!ValidateGadgetChainQuery(chain, chainError))
	{
		QMessageBox::warning(this, QStringLiteral("Hawkeye ROP"), chainError);
		return;
	}

	GadgetScanRequest request {};
	QString requestError;
	if (!buildScanRequest(request, requestError))
	{
		QMessageBox::warning(this, QStringLiteral("Hawkeye ROP"), requestError);
		return;
	}

	const QString normalizedChain = FormatNormalizedGadgetChainQuery(chain);

	saveSettings();
	rememberProbeChain(normalizedChain);
	ui.comboProbeChain->setCurrentText(normalizedChain);

	m_activeProbeChain = normalizedChain;
	m_activeProbeMaxPerDll = ui.spinProbeMaxPerDll->value();
	m_lastScanUsedProcessModules = request.useProcessModules;
	m_lastScanProcessId = request.processId;

	ui.plainTextResults->clear();
	appendScanResultText(scanTargetBanner());
	appendScanResultText(QStringLiteral("Probe input:     %1\n").arg(chain));
	appendScanResultText(QStringLiteral("Resolved chain:  %1\n").arg(normalizedChain));
	appendScanResultText(
		QStringLiteral("=== Probe (max %1 per DLL) ===\n\n").arg(m_activeProbeMaxPerDll));
	appendScanResultText(
		QStringLiteral("Index cache: %1 (rules v%2, ret=0xC3, max %3 insns before ret)\n\n")
			.arg(GadgetCacheRoot())
			.arg(kGadgetIndexRulesVersion)
			.arg(kMaxInsnsBeforeRet));

	m_worker = new GadgetScanWorker(this);
	m_worker->configureProbe(request, normalizedChain, m_activeProbeMaxPerDll);
	startWorker(m_worker, true);
}

void HawkeyeRopWindow::onScanChainStarted(const QString& chainQuery, int maxPerDll)
{
	appendScanResultText(
		QStringLiteral("\n=== [%1] (max %2 per DLL) ===\n\n").arg(chainQuery).arg(maxPerDll));
}

void HawkeyeRopWindow::onScanProgress(int current, int total, const GadgetFileResult& fileResult)
{
	const QString prefix =
		QStringLiteral("[%1/%2] ").arg(current).arg(total);
	appendScanResultText(prefix + FormatSingleFileScanBlock(fileResult));
}

void HawkeyeRopWindow::onCancelClicked()
{
	if (m_worker != nullptr)
	{
		m_worker->requestCancel();
	}
}

void HawkeyeRopWindow::onConfigScanFinished(const QVector<GadgetChainBatchResult>& /*results*/)
{
	setScanning(false);
	m_worker = nullptr;
	appendScanResultText(QStringLiteral("\n=== Batch scan finished ===\n"));
	refreshChainsCombo();
}

void HawkeyeRopWindow::onProbeScanFinished(const QVector<GadgetFileResult>& results)
{
	setScanning(false);
	m_worker = nullptr;

	const int hits = CountHitsInFileResults(results);
	int modulesScanned = 0;
	for (const GadgetFileResult& file : results)
	{
		if (!file.filePath.isEmpty())
		{
			++modulesScanned;
		}
	}
	appendScanResultText(
		QStringLiteral("\n=== Probe finished: %1 hit(s), %2 module(s) ===\n")
			.arg(hits)
			.arg(modulesScanned));

	refreshChainsCombo();
}

void HawkeyeRopWindow::onScanFailed(const QString& message)
{
	setScanning(false);
	m_worker = nullptr;
	ui.plainTextResults->setPlainText(message);
	QMessageBox::warning(this, QStringLiteral("Hawkeye ROP"), message);
	refreshChainsCombo();
}
