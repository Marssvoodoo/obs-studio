/******************************************************************************
    Copyright (C) 2026 OBS contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
******************************************************************************/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "VST3Cache.h"
#include "VST3Scanner.h"

#include <obs-data.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
constexpr DWORD MODULE_SCAN_TIMEOUT_MS = 15000;
constexpr SIZE_T MODULE_SCAN_MEMORY_LIMIT = 768ULL * 1024ULL * 1024ULL;
constexpr size_t MAX_PLUGIN_CLASSES = 10000;

struct VST3AuditResult {
	std::string name;
	std::string path;
	std::string status;
	std::string reason;
};

struct WorkerResult {
	bool launched = false;
	bool finished = false;
	bool timedOut = false;
	DWORD exitCode = ERROR_INVALID_STATE;
};

class WinHandle {
public:
	explicit WinHandle(HANDLE value_ = nullptr) : value(value_) {}
	~WinHandle()
	{
		if (value) {
			CloseHandle(value);
		}
	}

	WinHandle(const WinHandle &) = delete;
	WinHandle &operator=(const WinHandle &) = delete;
	HANDLE get() const { return value; }

private:
	HANDLE value;
};

bool validOutputPath(const std::filesystem::path &path)
{
	std::error_code error;
	const std::wstring filename = path.filename().native();
	if (path.empty() || path.extension() != L".json" || filename.size() > 255 ||
	    filename.rfind(L".vst3-scan-", 0) != 0) {
		return false;
	}
	if (std::filesystem::exists(path, error) || error) {
		return false;
	}

	const auto parent = std::filesystem::weakly_canonical(path.parent_path(), error);
	return !error && std::filesystem::is_directory(parent, error) && !error;
}

std::string pathIdentity(const std::string &path)
{
	std::string identity = path;
	std::transform(identity.begin(), identity.end(), identity.begin(),
		       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	return identity;
}

bool sameParent(const std::vector<std::filesystem::path> &paths)
{
	if (paths.empty())
		return false;
	std::error_code error;
	const auto expected = std::filesystem::weakly_canonical(paths.front().parent_path(), error);
	if (error || !std::filesystem::is_directory(expected, error) || error)
		return false;
	for (const auto &path : paths) {
		error.clear();
		const auto parent = std::filesystem::weakly_canonical(path.parent_path(), error);
		if (error || parent != expected)
			return false;
	}
	return true;
}

bool validManagerPaths(const std::filesystem::path &cachePath, const std::filesystem::path &auditPath,
		       const std::filesystem::path &statusPath, const std::filesystem::path &stopPath)
{
	return cachePath.filename() == L"vst3list.json" && auditPath.filename() == L"vst3scan-results.json" &&
	       statusPath.filename() == L"vst3scan-status.json" && stopPath.filename() == L"vst3scan.stop" &&
	       sameParent({cachePath, auditPath, statusPath, stopPath});
}

bool stopRequested(const std::filesystem::path &stopPath)
{
	std::error_code error;
	return std::filesystem::is_regular_file(stopPath, error) && !error;
}

bool saveAudit(const std::filesystem::path &path, const std::vector<VST3AuditResult> &results)
{
	obs_data_t *root = obs_data_create();
	obs_data_array_t *array = obs_data_array_create();
	for (const auto &result : results) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "name", vst3_sanitize_display_text(result.name).c_str());
		obs_data_set_string(item, "vendor", "");
		obs_data_set_string(item, "path", result.path.c_str());
		obs_data_set_string(item, "status", result.status.c_str());
		obs_data_set_string(item, "reason", vst3_sanitize_display_text(result.reason).c_str());
		obs_data_array_push_back(array, item);
		obs_data_release(item);
	}
	obs_data_set_int(root, "version", 1);
	obs_data_set_string(root, "format", "VST3");
	obs_data_set_array(root, "results", array);
	obs_data_array_release(array);
	const bool saved = obs_data_save_json_safe(root, path.u8string().c_str(), "tmp", "bak");
	obs_data_release(root);
	return saved;
}

bool loadAudit(const std::filesystem::path &path, std::vector<VST3AuditResult> &results)
{
	obs_data_t *root = obs_data_create_from_json_file_safe(path.u8string().c_str(), "bak");
	if (!root || obs_data_get_int(root, "version") != 1 ||
	    std::string(obs_data_get_string(root, "format")) != "VST3") {
		if (root)
			obs_data_release(root);
		return false;
	}

	obs_data_array_t *array = obs_data_get_array(root, "results");
	if (!array || obs_data_array_count(array) > MAX_PLUGIN_CLASSES) {
		if (array)
			obs_data_array_release(array);
		obs_data_release(root);
		return false;
	}

	VST3Scanner validator;
	std::vector<VST3AuditResult> loaded;
	for (size_t index = 0; index < obs_data_array_count(array); ++index) {
		obs_data_t *item = obs_data_array_item(array, index);
		if (!item)
			continue;
		VST3AuditResult result;
		result.name = vst3_sanitize_display_text(obs_data_get_string(item, "name"));
		result.path = obs_data_get_string(item, "path");
		result.status = obs_data_get_string(item, "status");
		result.reason = vst3_sanitize_display_text(obs_data_get_string(item, "reason"));
		obs_data_release(item);
		if (validator.isAllowedModulePath(result.path) &&
		    (result.status == "passed" || result.status == "failed" || result.status == "skipped")) {
			loaded.emplace_back(std::move(result));
		}
	}
	obs_data_array_release(array);
	obs_data_release(root);
	results = std::move(loaded);
	return true;
}

void countAudit(const std::vector<VST3AuditResult> &results, size_t &passed, size_t &failed, size_t &skipped)
{
	passed = failed = skipped = 0;
	for (const auto &result : results) {
		if (result.status == "passed")
			++passed;
		else if (result.status == "failed")
			++failed;
		else if (result.status == "skipped")
			++skipped;
	}
}

bool writeStatus(const std::filesystem::path &path, const char *state, const std::string &current, size_t completed,
		 size_t total, const std::vector<VST3AuditResult> &results, const char *message)
{
	size_t passed = 0;
	size_t failed = 0;
	size_t skipped = 0;
	countAudit(results, passed, failed, skipped);
	obs_data_t *root = obs_data_create();
	obs_data_set_int(root, "version", 1);
	obs_data_set_string(root, "format", "VST3");
	obs_data_set_string(root, "state", state);
	obs_data_set_string(root, "current", vst3_sanitize_display_text(current).c_str());
	obs_data_set_int(root, "completed", static_cast<long long>(completed));
	obs_data_set_int(root, "total", static_cast<long long>(total));
	obs_data_set_int(root, "passed", static_cast<long long>(passed));
	obs_data_set_int(root, "failed", static_cast<long long>(failed));
	obs_data_set_int(root, "skipped", static_cast<long long>(skipped));
	obs_data_set_string(root, "message", message ? message : "");
	const bool saved = obs_data_save_json_safe(root, path.u8string().c_str(), "tmp", nullptr);
	obs_data_release(root);
	return saved;
}

void removeOutput(const std::filesystem::path &path)
{
	std::error_code error;
	std::filesystem::remove(path, error);
	error.clear();
	std::filesystem::remove(path.string() + ".tmp", error);
	error.clear();
	std::filesystem::remove(path.string() + ".bak", error);
}

std::filesystem::path currentExecutable()
{
	std::vector<wchar_t> buffer(32768);
	const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
	if (size == 0 || size >= buffer.size()) {
		return {};
	}
	return std::filesystem::path(std::wstring(buffer.data(), size));
}

std::filesystem::path workerOutputPath(const std::filesystem::path &outputPath, size_t index)
{
	std::wstring filename = L".vst3-scan-worker-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
				std::to_wstring(index) + L"-" + std::to_wstring(GetTickCount64()) + L".json";
	return outputPath.parent_path() / filename;
}

std::wstring quoted(const std::wstring &value)
{
	return L"\"" + value + L"\"";
}

WorkerResult runModuleWorker(const std::filesystem::path &executable, const std::filesystem::path &modulePath,
			     const std::filesystem::path &outputPath)
{
	WorkerResult result;
	WinHandle job(CreateJobObjectW(nullptr, nullptr));
	if (!job.get())
		return result;

	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
	limits.ProcessMemoryLimit = MODULE_SCAN_MEMORY_LIMIT;
	if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
		return result;
	}

	std::wstring commandLine = quoted(executable.native()) + L" --scan-module " + quoted(modulePath.native()) +
				   L" " + quoted(outputPath.native());
	std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
	commandBuffer.push_back(L'\0');

	STARTUPINFOW startupInfo = {};
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_FORCEOFFFEEDBACK;
	PROCESS_INFORMATION processInfo = {};
	if (!CreateProcessW(executable.c_str(), commandBuffer.data(), nullptr, nullptr, FALSE,
			    CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE, nullptr, nullptr,
			    &startupInfo, &processInfo)) {
		return result;
	}
	result.launched = true;

	WinHandle process(processInfo.hProcess);
	WinHandle thread(processInfo.hThread);
	if (!AssignProcessToJobObject(job.get(), process.get())) {
		TerminateProcess(process.get(), ERROR_ACCESS_DENIED);
		return result;
	}
	if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
		TerminateJobObject(job.get(), ERROR_INVALID_STATE);
		return result;
	}

	const DWORD waitResult = WaitForSingleObject(process.get(), MODULE_SCAN_TIMEOUT_MS);
	if (waitResult != WAIT_OBJECT_0) {
		result.timedOut = waitResult == WAIT_TIMEOUT;
		TerminateJobObject(job.get(), result.timedOut ? WAIT_TIMEOUT : ERROR_INVALID_STATE);
		WaitForSingleObject(process.get(), 2000);
		return result;
	}

	result.finished = GetExitCodeProcess(process.get(), &result.exitCode) != FALSE;
	return result;
}

int scanSingleModule(const std::filesystem::path &modulePath, const std::filesystem::path &outputPath)
{
	if (!validOutputPath(outputPath)) {
		return 3;
	}

	VST3Scanner scanner;
	const std::string moduleUtf8 = modulePath.u8string();
	if (!scanner.isAllowedModulePath(moduleUtf8)) {
		return 6;
	}

	scanner.scanModule(moduleUtf8);
	scanner.sort();
	const std::string outputUtf8 = outputPath.u8string();
	return vst3_list_save_json(scanner, outputUtf8.c_str(), nullptr, true) ? 0 : 5;
}

int scanAllModules(const std::filesystem::path &outputPath)
{
	if (!validOutputPath(outputPath)) {
		return 3;
	}

	const std::filesystem::path executable = currentExecutable();
	if (executable.empty()) {
		return 7;
	}

	VST3Scanner aggregate;
	const std::string outputUtf8 = outputPath.u8string();
	if (!vst3_list_save_json(aggregate, outputUtf8.c_str(), nullptr, true)) {
		return 5;
	}

	const auto modules = aggregate.getModulePaths();
	size_t failed = 0;
	for (size_t index = 0; index < modules.size(); ++index) {
		const std::filesystem::path modulePath = std::filesystem::u8path(modules[index]);
		const std::filesystem::path workerPath = workerOutputPath(outputPath, index);
		removeOutput(workerPath);

		VST3Scanner moduleResult;
		const WorkerResult worker = runModuleWorker(executable, modulePath, workerPath);
		const bool workerFinished = worker.finished && worker.exitCode == 0;
		const std::string workerUtf8 = workerPath.u8string();
		const bool resultLoaded = workerFinished &&
					  vst3_list_load_json(moduleResult, workerUtf8.c_str(), false, true);
		if (!resultLoaded) {
			++failed;
		} else {
			for (auto &entry : moduleResult.pluginList) {
				if (aggregate.pluginList.size() >= MAX_PLUGIN_CLASSES) {
					break;
				}
				aggregate.pluginList.emplace_back(std::move(entry));
			}
			aggregate.sort();
			if (!vst3_list_save_json(aggregate, outputUtf8.c_str(), nullptr, true)) {
				removeOutput(workerPath);
				return 5;
			}
		}
		removeOutput(workerPath);

		if ((index + 1) % 25 == 0 || index + 1 == modules.size()) {
			std::fwprintf(stderr, L"VST3 scan: %zu/%zu modules, %zu classes, %zu skipped\n", index + 1,
				      modules.size(), aggregate.pluginList.size(), failed);
		}
		if (aggregate.pluginList.size() >= MAX_PLUGIN_CLASSES) {
			break;
		}
	}

	aggregate.sort();
	return vst3_list_save_json(aggregate, outputUtf8.c_str(), nullptr, true) ? 0 : 5;
}

void updateAudit(std::vector<VST3AuditResult> &results, VST3AuditResult result)
{
	const std::string identity = pathIdentity(result.path);
	const auto existing = std::find_if(results.begin(), results.end(), [&identity](const VST3AuditResult &item) {
		return pathIdentity(item.path) == identity;
	});
	if (existing == results.end())
		results.emplace_back(std::move(result));
	else
		*existing = std::move(result);
}

void removeModuleClasses(VST3Scanner &scanner, const std::string &path)
{
	const std::string identity = pathIdentity(path);
	scanner.pluginList.erase(std::remove_if(scanner.pluginList.begin(), scanner.pluginList.end(),
						[&identity](const VST3ClassInfo &entry) {
							return pathIdentity(entry.path) == identity;
						}),
				 scanner.pluginList.end());
	scanner.classCount.erase(path);
}

void rebuildClassCounts(VST3Scanner &scanner)
{
	scanner.classCount.clear();
	for (const auto &entry : scanner.pluginList)
		++scanner.classCount[entry.path];
}

VST3AuditResult failedWorkerResult(const std::string &path, const WorkerResult &worker)
{
	VST3AuditResult result;
	result.name = std::filesystem::u8path(path).stem().u8string();
	result.path = path;
	result.status = "failed";
	if (worker.timedOut) {
		result.reason = "Validation timed out after 15 seconds.";
	} else if (!worker.launched) {
		result.reason = "The isolated scanner process could not be started.";
	} else if (!worker.finished) {
		result.reason = "The isolated scanner process ended unexpectedly.";
	} else {
		char buffer[96];
		std::snprintf(buffer, sizeof(buffer), "The isolated scanner exited with code 0x%08lX.",
			      static_cast<unsigned long>(worker.exitCode));
		result.reason = buffer;
	}
	return result;
}

VST3AuditResult passedModuleResult(const std::string &path, const VST3Scanner &scanner)
{
	VST3AuditResult result;
	result.name = std::filesystem::u8path(path).stem().u8string();
	result.path = path;
	result.status = "passed";
	if (scanner.pluginList.size() == 1)
		result.name = scanner.pluginList.front().name;
	result.reason = "Validated " + std::to_string(scanner.pluginList.size()) + " audio effect class" +
			(scanner.pluginList.size() == 1 ? "." : "es.");
	return result;
}

int scanManaged(const std::filesystem::path &cachePath, const std::filesystem::path &auditPath,
		const std::filesystem::path &statusPath, const std::filesystem::path &stopPath, bool skipFailed,
		bool rescanAll)
{
	if (!validManagerPaths(cachePath, auditPath, statusPath, stopPath))
		return 3;
	const std::filesystem::path executable = currentExecutable();
	if (executable.empty())
		return 7;

	VST3Scanner discovery;
	const std::vector<std::string> modules = discovery.getModulePaths();
	std::unordered_map<std::string, bool> activeModules;
	for (const auto &module : modules)
		activeModules.emplace(pathIdentity(module), true);

	VST3Scanner aggregate;
	vst3_list_load_json(aggregate, cachePath.u8string().c_str(), true, true);
	aggregate.pluginList.erase(std::remove_if(aggregate.pluginList.begin(), aggregate.pluginList.end(),
						  [&activeModules](const VST3ClassInfo &entry) {
							  return activeModules.find(pathIdentity(entry.path)) ==
								 activeModules.end();
						  }),
				   aggregate.pluginList.end());
	rebuildClassCounts(aggregate);

	std::vector<VST3AuditResult> previous;
	loadAudit(auditPath, previous);
	std::unordered_map<std::string, VST3AuditResult> previousByPath;
	for (auto &result : previous)
		previousByPath.emplace(pathIdentity(result.path), std::move(result));

	std::vector<VST3AuditResult> results;
	results.reserve(modules.size());
	for (const auto &module : modules) {
		const auto prior = previousByPath.find(pathIdentity(module));
		if (prior != previousByPath.end())
			results.push_back(prior->second);
	}

	removeOutput(stopPath);
	if (!vst3_list_save_json(aggregate, cachePath.u8string().c_str(), "bak", true) ||
	    !saveAudit(auditPath, results) ||
	    !writeStatus(statusPath, "running", {}, 0, modules.size(), results,
			 "Discovering and validating VST3 plug-ins.")) {
		return 5;
	}

	size_t completed = 0;
	bool stopped = false;
	for (size_t index = 0; index < modules.size(); ++index) {
		if (stopRequested(stopPath)) {
			stopped = true;
			break;
		}

		const std::string &module = modules[index];
		const auto prior = previousByPath.find(pathIdentity(module));
		if (skipFailed && !rescanAll && prior != previousByPath.end() &&
		    (prior->second.status == "failed" || prior->second.status == "skipped")) {
			VST3AuditResult skipped = prior->second;
			skipped.status = "skipped";
			if (skipped.reason.rfind("Previously failed: ", 0) != 0)
				skipped.reason = "Previously failed: " + skipped.reason;
			updateAudit(results, std::move(skipped));
			removeModuleClasses(aggregate, module);
		} else {
			const std::filesystem::path workerPath = workerOutputPath(cachePath, index);
			removeOutput(workerPath);
			const WorkerResult worker =
				runModuleWorker(executable, std::filesystem::u8path(module), workerPath);
			VST3Scanner moduleResult;
			const bool loaded =
				worker.finished && worker.exitCode == 0 &&
				vst3_list_load_json(moduleResult, workerPath.u8string().c_str(), false, true);
			removeModuleClasses(aggregate, module);
			if (loaded && !moduleResult.pluginList.empty()) {
				VST3AuditResult passed = passedModuleResult(module, moduleResult);
				for (auto &entry : moduleResult.pluginList) {
					if (aggregate.pluginList.size() >= MAX_PLUGIN_CLASSES)
						break;
					aggregate.pluginList.emplace_back(std::move(entry));
				}
				updateAudit(results, std::move(passed));
			} else if (loaded) {
				VST3AuditResult failed;
				failed.name = std::filesystem::u8path(module).stem().u8string();
				failed.path = module;
				failed.status = "failed";
				failed.reason = "No supported VST3 audio effect classes were found.";
				updateAudit(results, std::move(failed));
			} else {
				updateAudit(results, failedWorkerResult(module, worker));
			}
			removeOutput(workerPath);
		}

		++completed;
		rebuildClassCounts(aggregate);
		aggregate.sort();
		std::sort(results.begin(), results.end(),
			  [](const auto &left, const auto &right) { return left.name < right.name; });
		if (!vst3_list_save_json(aggregate, cachePath.u8string().c_str(), "bak", true) ||
		    !saveAudit(auditPath, results) ||
		    !writeStatus(statusPath, "running", module, completed, modules.size(), results,
				 "Validating each module in a separate process.")) {
			return 5;
		}
	}

	const char *state = stopped ? "stopped" : "complete";
	const char *message = stopped ? "Scan stopped. Completed results were preserved."
				      : "VST3 scan complete. Results were saved.";
	writeStatus(statusPath, state, {}, completed, modules.size(), results, message);
	removeOutput(stopPath);
	return 0;
}
} // namespace

int wmain(int argc, wchar_t **argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

	if (argc == 4 && std::wstring(argv[1]) == L"--scan-module") {
		try {
			return scanSingleModule(std::filesystem::path(argv[2]), std::filesystem::path(argv[3]));
		} catch (...) {
			return 4;
		}
	}

	if (argc == 8 && std::wstring(argv[1]) == L"--scan-manager") {
		try {
			return scanManaged(std::filesystem::path(argv[2]), std::filesystem::path(argv[3]),
					   std::filesystem::path(argv[4]), std::filesystem::path(argv[5]),
					   std::wstring(argv[6]) == L"1", std::wstring(argv[7]) == L"1");
		} catch (...) {
			return 4;
		}
	}

	if (argc != 2) {
		return 2;
	}

	const std::filesystem::path outputPath(argv[1]);
	try {
		return scanAllModules(outputPath);
	} catch (...) {
		return 4;
	}
}
