/*****************************************************************************
Copyright (C) 2026 OBS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.
*****************************************************************************/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "VST2ScanCache.hpp"
#include "headers/vst-plugin-callbacks.hpp"

#include <PluginPathFingerprint.hpp>

#include <obs-data.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
constexpr DWORD MODULE_SCAN_TIMEOUT_MS = 15000;
constexpr SIZE_T MODULE_SCAN_MEMORY_LIMIT = 768ULL * 1024ULL * 1024ULL;

class WinHandle {
public:
	explicit WinHandle(HANDLE value_ = nullptr) : value(value_) {}
	~WinHandle()
	{
		if (value)
			CloseHandle(value);
	}

	WinHandle(const WinHandle &) = delete;
	WinHandle &operator=(const WinHandle &) = delete;
	HANDLE get() const { return value; }

private:
	HANDLE value;
};

struct WorkerResult {
	bool launched = false;
	bool finished = false;
	bool timedOut = false;
	DWORD exitCode = ERROR_INVALID_STATE;
};

std::wstring quoted(const std::wstring &value)
{
	return L"\"" + value + L"\"";
}

std::filesystem::path currentExecutable()
{
	std::vector<wchar_t> buffer(32768);
	const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
	if (size == 0 || size >= buffer.size())
		return {};
	return std::filesystem::path(std::wstring(buffer.data(), size));
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

bool validManagerPaths(const std::filesystem::path &resultsPath, const std::filesystem::path &statusPath,
		       const std::filesystem::path &stopPath)
{
	return resultsPath.filename() == L"vst2scan-results.json" && statusPath.filename() == L"vst2scan-status.json" &&
	       stopPath.filename() == L"vst2scan.stop" && sameParent({resultsPath, statusPath, stopPath});
}

bool validWorkerOutputPath(const std::filesystem::path &path)
{
	std::error_code error;
	const std::wstring filename = path.filename().native();
	if (path.extension() != L".json" || filename.rfind(L".vst2-scan-worker-", 0) != 0 ||
	    std::filesystem::exists(path, error) || error) {
		return false;
	}
	const auto parent = std::filesystem::weakly_canonical(path.parent_path(), error);
	return !error && std::filesystem::is_directory(parent, error) && !error;
}

std::filesystem::path workerOutputPath(const std::filesystem::path &resultsPath, size_t index)
{
	const std::wstring filename = L".vst2-scan-worker-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
				      std::to_wstring(index) + L"-" + std::to_wstring(GetTickCount64()) + L".json";
	return resultsPath.parent_path() / filename;
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

bool stopRequested(const std::filesystem::path &stopPath)
{
	std::error_code error;
	return std::filesystem::is_regular_file(stopPath, error) && !error;
}

void countResults(const std::vector<VST2ScanResult> &results, size_t &passed, size_t &failed, size_t &skipped)
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
		 size_t total, const std::vector<VST2ScanResult> &results, const char *message)
{
	obs_data_t *root = obs_data_create();
	size_t passed = 0;
	size_t failed = 0;
	size_t skipped = 0;
	countResults(results, passed, failed, skipped);
	obs_data_set_int(root, "version", 1);
	obs_data_set_string(root, "format", "VST2");
	obs_data_set_string(root, "state", state);
	obs_data_set_string(root, "current", vst2_sanitize_scan_text(current).c_str());
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

intptr_t vstHostCallback(AEffect *, int32_t opcode, int32_t, intptr_t, void *, float)
{
	switch (opcode) {
	case audioMasterVersion:
		return 2400;
	case audioMasterGetSampleRate:
		return 48000;
	case audioMasterGetBlockSize:
		return 1024;
	default:
		return 0;
	}
}

int scanSingleModule(const std::filesystem::path &modulePath, const std::filesystem::path &outputPath)
{
	if (!validWorkerOutputPath(outputPath))
		return 3;

	VST2ScanResult result;
	result.name = modulePath.stem().u8string();
	result.path = modulePath.u8string();
	result.status = "failed";

	if (!vst2_is_allowed_plugin_path(result.path)) {
		result.reason = "The file is outside the configured VST2 search locations.";
		return vst2_scan_results_save(outputPath.u8string().c_str(), {result}) ? 0 : 5;
	}

	SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
	HMODULE library = LoadLibraryExW(modulePath.c_str(), nullptr,
					 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (!library) {
		result.reason = "Windows could not load the module (error " + std::to_string(GetLastError()) + ").";
		return vst2_scan_results_save(outputPath.u8string().c_str(), {result}) ? 0 : 5;
	}

	FARPROC entryAddress = GetProcAddress(library, "VSTPluginMain");
	if (!entryAddress)
		entryAddress = GetProcAddress(library, "VstPluginMain()");
	if (!entryAddress)
		entryAddress = GetProcAddress(library, "main");
	if (!entryAddress) {
		result.reason = "No VST2 entry point was found.";
		FreeLibrary(library);
		return vst2_scan_results_save(outputPath.u8string().c_str(), {result}) ? 0 : 5;
	}

	AEffect *effect = nullptr;
	bool opened = false;
	try {
		const auto entry = reinterpret_cast<vstPluginMain>(entryAddress);
		effect = entry(vstHostCallback);
		if (!effect || effect->magic != kEffectMagic || !effect->dispatcher) {
			result.reason = "The module did not return a valid VST2 effect.";
		} else if ((effect->flags & effFlagsIsSynth) != 0) {
			result.reason = "Instrument plug-ins are not supported by the OBS VST2 audio filter.";
		} else if (effect->numInputs <= 0 || effect->numOutputs <= 0 || effect->numInputs > 256 ||
			   effect->numOutputs > 256) {
			result.reason = "The plug-in does not expose a supported audio input/output layout.";
		} else {
			effect->dispatcher(effect, effOpen, 0, 0, nullptr, 0.0f);
			opened = true;
			char effectName[256] = {};
			char vendorName[256] = {};
			effect->dispatcher(effect, effGetEffectName, 0, 0, effectName, 0.0f);
			effect->dispatcher(effect, effGetVendorString, 0, 0, vendorName, 0.0f);
			if (*effectName)
				result.name = effectName;
			if (*vendorName)
				result.vendor = vendorName;
			result.status = "passed";
			result.reason = "Validated in an isolated scanner process.";
		}
	} catch (const std::exception &error) {
		result.reason = std::string("Plug-in validation threw an exception: ") + error.what();
	} catch (...) {
		result.reason = "Plug-in validation threw an unknown exception.";
	}

	if (opened && effect && effect->dispatcher) {
		try {
			effect->dispatcher(effect, effClose, 0, 0, nullptr, 0.0f);
		} catch (...) {
			result.status = "failed";
			result.reason = "The plug-in failed while closing its validation instance.";
		}
	}
	FreeLibrary(library);
	if (result.status == "passed") {
		PluginPathFingerprint fingerprint;
		if (!plugin_path_fingerprint(result.path, fingerprint)) {
			result.status = "failed";
			result.reason = "The validated module could not be fingerprinted.";
		} else {
			result.fileSize = fingerprint.size;
			result.sha256 = std::move(fingerprint.sha256);
		}
	}
	return vst2_scan_results_save(outputPath.u8string().c_str(), {result}) ? 0 : 5;
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
	if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
		return result;

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

VST2ScanResult failedWorkerResult(const std::string &path, const WorkerResult &worker)
{
	VST2ScanResult result;
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

void updateResult(std::vector<VST2ScanResult> &results, VST2ScanResult result)
{
	const std::string identity = pathIdentity(result.path);
	const auto existing = std::find_if(results.begin(), results.end(), [&identity](const VST2ScanResult &item) {
		return pathIdentity(item.path) == identity;
	});
	if (existing == results.end())
		results.emplace_back(std::move(result));
	else
		*existing = std::move(result);
}

int scanAllModules(const std::filesystem::path &resultsPath, const std::filesystem::path &statusPath,
		   const std::filesystem::path &stopPath, bool skipFailed, bool rescanAll)
{
	if (!validManagerPaths(resultsPath, statusPath, stopPath))
		return 3;

	const std::filesystem::path executable = currentExecutable();
	if (executable.empty())
		return 7;

	std::vector<VST2ScanResult> previous;
	vst2_scan_results_load(resultsPath.u8string().c_str(), previous, true);
	const std::vector<std::string> modules = vst2_discover_plugin_paths();
	std::unordered_map<std::string, VST2ScanResult> previousByPath;
	for (auto &result : previous)
		previousByPath.emplace(pathIdentity(result.path), std::move(result));

	std::vector<VST2ScanResult> results;
	results.reserve(modules.size());
	for (const std::string &module : modules) {
		const auto previousResult = previousByPath.find(pathIdentity(module));
		if (previousResult != previousByPath.end())
			results.push_back(previousResult->second);
	}

	removeOutput(stopPath);
	writeStatus(statusPath, "running", {}, 0, modules.size(), results, "Discovering and validating VST2 plug-ins.");
	if (!vst2_scan_results_save(resultsPath.u8string().c_str(), results, "bak"))
		return 5;

	size_t completed = 0;
	bool stopped = false;
	for (size_t index = 0; index < modules.size(); ++index) {
		if (stopRequested(stopPath)) {
			stopped = true;
			break;
		}

		const std::string &module = modules[index];
		const auto previousResult = previousByPath.find(pathIdentity(module));
		if (skipFailed && !rescanAll && previousResult != previousByPath.end() &&
		    (previousResult->second.status == "failed" || previousResult->second.status == "skipped")) {
			VST2ScanResult skipped = previousResult->second;
			skipped.status = "skipped";
			if (skipped.reason.rfind("Previously failed: ", 0) != 0)
				skipped.reason = "Previously failed: " + skipped.reason;
			updateResult(results, std::move(skipped));
		} else {
			const std::filesystem::path workerPath = workerOutputPath(resultsPath, index);
			removeOutput(workerPath);
			const WorkerResult worker =
				runModuleWorker(executable, std::filesystem::u8path(module), workerPath);

			std::vector<VST2ScanResult> workerResults;
			const bool loaded =
				worker.finished && worker.exitCode == 0 &&
				vst2_scan_results_load(workerPath.u8string().c_str(), workerResults, false) &&
				workerResults.size() == 1;
			updateResult(results,
				     loaded ? std::move(workerResults.front()) : failedWorkerResult(module, worker));
			removeOutput(workerPath);
		}

		++completed;
		std::sort(results.begin(), results.end(),
			  [](const auto &left, const auto &right) { return left.name < right.name; });
		if (!vst2_scan_results_save(resultsPath.u8string().c_str(), results, "bak") ||
		    !writeStatus(statusPath, "running", module, completed, modules.size(), results,
				 "Validating each plug-in in a separate process.")) {
			return 5;
		}
	}

	const char *state = stopped ? "stopped" : "complete";
	const char *message = stopped ? "Scan stopped. Completed results were preserved."
				      : "VST2 scan complete. Results were saved.";
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

	if (argc == 7 && std::wstring(argv[1]) == L"--scan-manager") {
		try {
			return scanAllModules(std::filesystem::path(argv[2]), std::filesystem::path(argv[3]),
					      std::filesystem::path(argv[4]), std::wstring(argv[5]) == L"1",
					      std::wstring(argv[6]) == L"1");
		} catch (...) {
			return 4;
		}
	}

	return 2;
}
