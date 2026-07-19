/*****************************************************************************
Copyright (C) 2026 OBS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.
*****************************************************************************/

#pragma once

#include <string>
#include <vector>

struct VST2ScanResult {
	std::string name;
	std::string vendor;
	std::string path;
	std::string status;
	std::string reason;
};

std::vector<std::string> vst2_default_search_paths();
std::vector<std::string> vst2_discover_plugin_paths();
bool vst2_is_allowed_plugin_path(const std::string &path);
std::string vst2_sanitize_scan_text(std::string text);

bool vst2_scan_results_load(const char *path, std::vector<VST2ScanResult> &results, bool safeBackup);
bool vst2_scan_results_save(const char *path, const std::vector<VST2ScanResult> &results,
			    const char *backupExtension = nullptr);
