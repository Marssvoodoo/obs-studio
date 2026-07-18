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

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr DWORD MODULE_SCAN_TIMEOUT_MS = 15000;
constexpr SIZE_T MODULE_SCAN_MEMORY_LIMIT = 768ULL * 1024ULL * 1024ULL;
constexpr size_t MAX_PLUGIN_CLASSES = 10000;

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

bool runModuleWorker(const std::filesystem::path &executable, const std::filesystem::path &modulePath,
		     const std::filesystem::path &outputPath)
{
	WinHandle job(CreateJobObjectW(nullptr, nullptr));
	if (!job.get()) {
		return false;
	}

	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
	limits.ProcessMemoryLimit = MODULE_SCAN_MEMORY_LIMIT;
	if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
		return false;
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
		return false;
	}

	WinHandle process(processInfo.hProcess);
	WinHandle thread(processInfo.hThread);
	if (!AssignProcessToJobObject(job.get(), process.get())) {
		TerminateProcess(process.get(), ERROR_ACCESS_DENIED);
		return false;
	}
	if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
		TerminateJobObject(job.get(), ERROR_INVALID_STATE);
		return false;
	}

	const DWORD waitResult = WaitForSingleObject(process.get(), MODULE_SCAN_TIMEOUT_MS);
	if (waitResult != WAIT_OBJECT_0) {
		TerminateJobObject(job.get(), waitResult == WAIT_TIMEOUT ? WAIT_TIMEOUT : ERROR_INVALID_STATE);
		WaitForSingleObject(process.get(), 2000);
		return false;
	}

	DWORD exitCode = ERROR_INVALID_STATE;
	return GetExitCodeProcess(process.get(), &exitCode) && exitCode == 0;
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
		const bool workerFinished = runModuleWorker(executable, modulePath, workerPath);
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
