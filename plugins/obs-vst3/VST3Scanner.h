/******************************************************************************
    Copyright (C) 2025-2026 pkv <pkv@obsproject.com>
    This file is part of obs-vst3.
    It uses the Steinberg VST3 SDK, which is licensed under MIT license.
    See https://github.com/steinbergmedia/vst3sdk for details.
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

#include "public.sdk/source/vst/moduleinfo/moduleinfo.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef kVstAudioEffectClass
#define kVstAudioEffectClass "Audio Module Class"
#endif

struct VST3ClassInfo {
	std::string name;
	std::string id;
	std::string path;
	std::string pluginName;
	std::uint64_t fileSize = 0;
	std::string sha256;
	bool discardable; // Optional but requested to us by Steinberg: the classes need to be reloaded from the module at each host startup and can not be cached (ex: Waves plugins).
};

bool vst3_is_valid_class_id(std::string_view classId) noexcept;
std::string vst3_sanitize_display_text(std::string text);
bool vst3_validate_and_sanitize_class_info(VST3ClassInfo &entry);

class VST3Scanner {
public:
	std::vector<std::string> getDefaultSearchPaths() const;
	std::vector<std::string> getModulePaths();
	bool isAllowedModulePath(const std::string &modulePath) const;
	std::string getNameById(const std::string &class_id) const;
	std::string getPathById(const std::string &class_id) const;
	bool hasCurrentFingerprint(const std::string &class_id) const;
	bool moduleHasMultipleClasses(const std::string &bundlePath) const;
	bool addModuleClasses(const std::string &bundlePath);
	bool scanModule(const std::string &bundlePath);
	bool scanForVST3Plugins();
	std::vector<VST3ClassInfo> pluginList;
	void sort();
	std::unordered_map<std::string, size_t> classCount;

private:
	std::unordered_set<std::string> getVST3Paths();
	bool tryReadModuleInfo(const std::string &bundlePath);
	bool loadFromModuleInfo(const Steinberg::ModuleInfo &info, const std::string &bundlePath);
};
