/*****************************************************************************
Copyright (C) 2026 OBS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.
*****************************************************************************/

#include "VST2ScanCache.hpp"

#include <PluginPathFingerprint.hpp>

#include <obs-data.h>
#include <util/platform.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <string_view>
#include <unordered_set>

namespace {
constexpr int64_t MAX_CACHE_BYTES = 16LL * 1024LL * 1024LL;
constexpr size_t MAX_SCAN_RESULTS = 10000;

bool hasControlCharacters(std::string_view text)
{
	return std::any_of(text.begin(), text.end(), [](char value) {
		const auto byte = static_cast<unsigned char>(value);
		return byte < 0x20 || byte == 0x7f;
	});
}

bool pathComponentEquals(const std::filesystem::path &left, const std::filesystem::path &right)
{
#ifdef _WIN32
	const std::wstring leftValue = left.native();
	const std::wstring rightValue = right.native();
	return leftValue.size() == rightValue.size() &&
	       std::equal(leftValue.begin(), leftValue.end(), rightValue.begin(),
			  [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
#else
	return left == right;
#endif
}

bool pathIsWithin(const std::filesystem::path &root, const std::filesystem::path &candidate)
{
	auto rootPart = root.begin();
	auto candidatePart = candidate.begin();
	for (; rootPart != root.end(); ++rootPart, ++candidatePart) {
		if (candidatePart == candidate.end() || !pathComponentEquals(*rootPart, *candidatePart)) {
			return false;
		}
	}
	return true;
}

bool hasPluginExtension(const std::filesystem::path &path)
{
#ifdef _WIN32
	std::wstring extension = path.extension().native();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		       [](wchar_t value) { return std::towlower(value); });
	return extension == L".dll";
#elif defined(__APPLE__)
	return path.extension() == ".vst";
#else
	return path.extension() == ".so" || path.extension() == ".o";
#endif
}

std::string pathIdentity(const std::string &path)
{
#ifdef _WIN32
	std::string identity = path;
	std::transform(identity.begin(), identity.end(), identity.begin(),
		       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	return identity;
#else
	return path;
#endif
}

void appendEnvironmentPaths(std::vector<std::string> &paths)
{
	const char *value = std::getenv("VST_PATH");
	if (!value || !*value) {
		return;
	}

#ifdef _WIN32
	constexpr char separator = ';';
#else
	constexpr char separator = ':';
#endif
	std::string allPaths(value);
	size_t start = 0;
	while (start <= allPaths.size()) {
		const size_t end = allPaths.find(separator, start);
		std::string path = allPaths.substr(start, end == std::string::npos ? std::string::npos : end - start);
		if (!path.empty()) {
			paths.emplace_back(std::move(path));
		}
		if (end == std::string::npos) {
			break;
		}
		start = end + 1;
	}
}

bool validStatus(const std::string &status)
{
	return status == "passed" || status == "failed" || status == "skipped";
}

bool validateResult(VST2ScanResult &result)
{
	result.name = vst2_sanitize_scan_text(std::move(result.name));
	result.vendor = vst2_sanitize_scan_text(std::move(result.vendor));
	result.reason = vst2_sanitize_scan_text(std::move(result.reason));
	if (result.name.empty()) {
		result.name = std::filesystem::u8path(result.path).stem().u8string();
	}
	const PluginPathFingerprint fingerprint{result.fileSize, result.sha256};
	const bool validFingerprint = result.status != "passed" || plugin_path_fingerprint_is_valid(fingerprint);
	return result.name.size() <= 1024 && result.vendor.size() <= 1024 && result.reason.size() <= 1024 &&
	       result.path.size() <= 32768 && !hasControlCharacters(result.path) && validStatus(result.status) &&
	       validFingerprint && vst2_is_allowed_plugin_path(result.path);
}
} // namespace

std::vector<std::string> vst2_default_search_paths()
{
	std::vector<std::string> paths;
	appendEnvironmentPaths(paths);

#ifdef _WIN32
	if (const char *programFiles = std::getenv("ProgramFiles")) {
		paths.emplace_back(std::string(programFiles) + "\\Steinberg\\VstPlugins");
		paths.emplace_back(std::string(programFiles) + "\\VSTPlugins");
	}
	if (const char *commonProgramFiles = std::getenv("CommonProgramFiles")) {
		paths.emplace_back(std::string(commonProgramFiles) + "\\Steinberg\\Shared Components");
		paths.emplace_back(std::string(commonProgramFiles) + "\\VST2");
		paths.emplace_back(std::string(commonProgramFiles) + "\\Steinberg\\VST2");
		paths.emplace_back(std::string(commonProgramFiles) + "\\VSTPlugins");
	}
#elif defined(__APPLE__)
	paths.emplace_back("/Library/Audio/Plug-Ins/VST");
	if (const char *home = std::getenv("HOME")) {
		paths.emplace_back(std::string(home) + "/Library/Audio/Plug-Ins/VST");
	}
#else
	paths.insert(paths.end(),
		     {"/usr/lib/vst", "/usr/lib/lxvst", "/usr/lib/linux_vst", "/usr/lib64/vst", "/usr/lib64/lxvst",
		      "/usr/lib64/linux_vst", "/usr/local/lib/vst", "/usr/local/lib/lxvst", "/usr/local/lib/linux_vst",
		      "/usr/local/lib64/vst", "/usr/local/lib64/lxvst", "/usr/local/lib64/linux_vst"});
	if (const char *home = std::getenv("HOME")) {
		paths.emplace_back(std::string(home) + "/.vst");
		paths.emplace_back(std::string(home) + "/.lxvst");
	}
#endif

	std::unordered_set<std::string> seen;
	std::vector<std::string> unique;
	for (auto &path : paths) {
		if (!path.empty() && seen.emplace(pathIdentity(path)).second) {
			unique.emplace_back(std::move(path));
		}
	}
	return unique;
}

bool vst2_is_allowed_plugin_path(const std::string &path)
{
	if (path.empty() || path.size() > 32768 || hasControlCharacters(path)) {
		return false;
	}

	std::error_code error;
	const std::filesystem::path candidate = std::filesystem::weakly_canonical(std::filesystem::u8path(path), error);
	if (error || !std::filesystem::is_regular_file(candidate, error) || error || !hasPluginExtension(candidate)) {
		return false;
	}

	for (const auto &searchPath : vst2_default_search_paths()) {
		const std::filesystem::path root =
			std::filesystem::weakly_canonical(std::filesystem::u8path(searchPath), error);
		if (!error && pathIsWithin(root, candidate)) {
			return true;
		}
		error.clear();
	}
	return false;
}

std::vector<std::string> vst2_discover_plugin_paths()
{
	std::vector<std::string> plugins;
	std::unordered_set<std::string> seen;
	for (const auto &searchPath : vst2_default_search_paths()) {
		std::error_code error;
		const auto root = std::filesystem::weakly_canonical(std::filesystem::u8path(searchPath), error);
		if (error || !std::filesystem::is_directory(root, error) || error) {
			continue;
		}

		const auto options = std::filesystem::directory_options::skip_permission_denied;
		std::filesystem::recursive_directory_iterator iterator(root, options, error);
		const std::filesystem::recursive_directory_iterator end;
		while (!error && iterator != end && plugins.size() < MAX_SCAN_RESULTS) {
			const auto entry = *iterator;
			iterator.increment(error);
			if (error) {
				error.clear();
				continue;
			}
			if (!entry.is_regular_file(error) || error || !hasPluginExtension(entry.path())) {
				error.clear();
				continue;
			}
			const auto canonical = std::filesystem::weakly_canonical(entry.path(), error);
			if (error) {
				continue;
			}
			const std::string path = canonical.u8string();
			if (vst2_is_allowed_plugin_path(path) && seen.emplace(pathIdentity(path)).second) {
				plugins.emplace_back(path);
			}
		}
	}

	std::sort(plugins.begin(), plugins.end());
	return plugins;
}

std::string vst2_sanitize_scan_text(std::string text)
{
	for (char &value : text) {
		const auto byte = static_cast<unsigned char>(value);
		if (byte < 0x20 || byte == 0x7f) {
			value = ' ';
		}
	}
	const auto first = text.find_first_not_of(' ');
	if (first == std::string::npos) {
		return {};
	}
	return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

bool vst2_scan_results_load(const char *path, std::vector<VST2ScanResult> &results, bool safeBackup)
{
	if (!path) {
		return false;
	}
	const int64_t fileSize = os_get_file_size(path);
	const std::string backupPath = std::string(path) + ".bak";
	const int64_t backupSize = safeBackup ? os_get_file_size(backupPath.c_str()) : -1;
	if (fileSize > MAX_CACHE_BYTES || backupSize > MAX_CACHE_BYTES ||
	    (fileSize <= 0 && (!safeBackup || backupSize <= 0))) {
		return false;
	}

	obs_data_t *root = safeBackup ? obs_data_create_from_json_file_safe(path, "bak")
				      : obs_data_create_from_json_file(path);
	if (!root || obs_data_get_int(root, "version") != 2 ||
	    std::string(obs_data_get_string(root, "format")) != "VST2") {
		if (root)
			obs_data_release(root);
		return false;
	}

	obs_data_array_t *array = obs_data_get_array(root, "results");
	if (!array || obs_data_array_count(array) > MAX_SCAN_RESULTS) {
		if (array)
			obs_data_array_release(array);
		obs_data_release(root);
		return false;
	}

	std::vector<VST2ScanResult> loaded;
	std::unordered_set<std::string> seen;
	for (size_t index = 0; index < obs_data_array_count(array); ++index) {
		obs_data_t *item = obs_data_array_item(array, index);
		if (!item) {
			continue;
		}
		VST2ScanResult result;
		result.name = obs_data_get_string(item, "name");
		result.vendor = obs_data_get_string(item, "vendor");
		result.path = obs_data_get_string(item, "path");
		result.status = obs_data_get_string(item, "status");
		result.reason = obs_data_get_string(item, "reason");
		const long long storedSize = obs_data_get_int(item, "fileSize");
		if (storedSize >= 0) {
			result.fileSize = static_cast<std::uint64_t>(storedSize);
		}
		result.sha256 = obs_data_get_string(item, "sha256");
		obs_data_release(item);
		if (validateResult(result) && seen.emplace(pathIdentity(result.path)).second) {
			loaded.emplace_back(std::move(result));
		}
	}

	obs_data_array_release(array);
	obs_data_release(root);
	results = std::move(loaded);
	return true;
}

bool vst2_scan_results_save(const char *path, const std::vector<VST2ScanResult> &results, const char *backupExtension)
{
	if (!path || results.size() > MAX_SCAN_RESULTS) {
		return false;
	}

	obs_data_t *root = obs_data_create();
	obs_data_array_t *array = obs_data_array_create();
	std::unordered_set<std::string> seen;
	for (const auto &source : results) {
		VST2ScanResult result = source;
		if (!validateResult(result) || !seen.emplace(pathIdentity(result.path)).second) {
			continue;
		}
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "name", result.name.c_str());
		obs_data_set_string(item, "vendor", result.vendor.c_str());
		obs_data_set_string(item, "path", result.path.c_str());
		obs_data_set_string(item, "status", result.status.c_str());
		obs_data_set_string(item, "reason", result.reason.c_str());
		obs_data_set_int(item, "fileSize", static_cast<long long>(result.fileSize));
		obs_data_set_string(item, "sha256", result.sha256.c_str());
		obs_data_array_push_back(array, item);
		obs_data_release(item);
	}
	obs_data_set_int(root, "version", 2);
	obs_data_set_string(root, "format", "VST2");
	obs_data_set_array(root, "results", array);
	obs_data_array_release(array);
	const bool saved = obs_data_save_json_safe(root, path, "tmp", backupExtension);
	obs_data_release(root);
	return saved;
}

bool vst2_scan_result_passed(const std::vector<VST2ScanResult> &results, const std::string &pluginPath)
{
	if (!vst2_is_allowed_plugin_path(pluginPath)) {
		return false;
	}

	const std::string requestedIdentity = pathIdentity(pluginPath);
	return std::any_of(results.begin(), results.end(), [&](const VST2ScanResult &result) {
		const PluginPathFingerprint expected{result.fileSize, result.sha256};
		return result.status == "passed" && pathIdentity(result.path) == requestedIdentity &&
		       plugin_path_fingerprint_matches(pluginPath, expected);
	});
}
