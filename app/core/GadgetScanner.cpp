// Copyright (C) 2026 Hawkeye
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GadgetScanner.h"

#include "GadgetCache.h"
#include "GadgetIndexStore.h"
#include "InstrMatch.h"
#include "PeImage.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace
{
QStringList ListTargetPeFiles(const QString& directoryPath)
{
	QDir dir(directoryPath);
	const QStringList filters { QStringLiteral("*.dll"), QStringLiteral("*.exe") };
	return dir.entryList(filters, QDir::Files, QDir::Name);
}

GadgetFileResult ScanOneFile(
	const QString& filePath,
	const QStringList& queryParts,
	volatile bool* cancelFlag,
	int maxHitsPerDll,
	quint64 loadedImageBase)
{
	GadgetFileResult result {};
	result.filePath = QFileInfo(filePath).absoluteFilePath();

	QString indexDbPath;
	QString error;
	if (!EnsureGadgetIndexForPe(filePath, indexDbPath, error))
	{
		result.skipReason = error;
		return result;
	}

	uint64_t imageBase = loadedImageBase;
	if (imageBase == 0)
	{
		PeImage64 pe {};
		if (LoadPe64Image(filePath, pe, error))
		{
			imageBase = pe.imageBase;
		}
	}

	if (!QueryGadgetIndexDatabase(
			indexDbPath,
			imageBase,
			queryParts,
			cancelFlag,
			maxHitsPerDll,
			result,
			error))
	{
		result.skipReason = error;
	}

	return result;
}
} // namespace

QVector<GadgetScanTarget> BuildScanTargetsFromDirectory(const QString& directoryPath)
{
	QVector<GadgetScanTarget> targets;
	const QStringList files = ListTargetPeFiles(directoryPath);
	targets.reserve(files.size());
	for (const QString& name : files)
	{
		GadgetScanTarget target {};
		target.filePath = QDir(directoryPath).absoluteFilePath(name);
		targets.push_back(target);
	}
	return targets;
}

namespace
{
constexpr int kParallelScanMinTargets = 3;
constexpr int kParallelScanThreads = 4;

bool IsCancelled(volatile bool* cancelFlag)
{
	return cancelFlag != nullptr && *cancelFlag;
}

void ScanTargetsSequential(
	const QVector<GadgetScanTarget>& targets,
	const QStringList& queryParts,
	volatile bool* cancelFlag,
	int maxHitsPerDll,
	GadgetScanProgressFn progress,
	QVector<GadgetFileResult>& results)
{
	const int total = targets.size();
	for (int index = 0; index < total; ++index)
	{
		if (IsCancelled(cancelFlag))
		{
			break;
		}

		const GadgetScanTarget& target = targets[index];
		results[index] = ScanOneFile(
			target.filePath,
			queryParts,
			cancelFlag,
			maxHitsPerDll,
			target.loadedImageBase);
		if (progress)
		{
			progress(index + 1, total, results[index]);
		}
	}
}

void ScanTargetsParallel(
	const QVector<GadgetScanTarget>& targets,
	const QStringList& queryParts,
	volatile bool* cancelFlag,
	int maxHitsPerDll,
	GadgetScanProgressFn progress,
	QVector<GadgetFileResult>& results)
{
	const int total = targets.size();
	std::atomic<int> nextIndex { 0 };
	std::atomic<int> completed { 0 };

	const int threadCount = std::min(kParallelScanThreads, total);
	std::vector<std::thread> workers;
	workers.reserve(static_cast<size_t>(threadCount));

	for (int worker = 0; worker < threadCount; ++worker)
	{
		workers.emplace_back([&]() {
			for (;;)
			{
				if (IsCancelled(cancelFlag))
				{
					return;
				}

				const int index = nextIndex.fetch_add(1);
				if (index >= total)
				{
					return;
				}

				const GadgetScanTarget& target = targets[index];
				results[index] = ScanOneFile(
					target.filePath,
					queryParts,
					cancelFlag,
					maxHitsPerDll,
					target.loadedImageBase);

				if (progress)
				{
					const int done = completed.fetch_add(1) + 1;
					progress(done, total, results[index]);
				}
			}
		});
	}

	for (std::thread& worker : workers)
	{
		if (worker.joinable())
		{
			worker.join();
		}
	}
}
} // namespace

QVector<GadgetFileResult> ScanGadgetTargets(
	const QVector<GadgetScanTarget>& targets,
	const QString& chainQuery,
	volatile bool* cancelFlag,
	int maxHitsPerDll,
	GadgetScanProgressFn progress)
{
	QVector<GadgetFileResult> results;
	const QStringList queryParts = ParseGadgetChainQuery(chainQuery);
	if (queryParts.isEmpty())
	{
		return results;
	}

	if (targets.isEmpty())
	{
		return results;
	}

	results.resize(targets.size());
	if (targets.size() >= kParallelScanMinTargets)
	{
		ScanTargetsParallel(
			targets,
			queryParts,
			cancelFlag,
			maxHitsPerDll,
			progress,
			results);
	}
	else
	{
		ScanTargetsSequential(
			targets,
			queryParts,
			cancelFlag,
			maxHitsPerDll,
			progress,
			results);
	}

	return results;
}
