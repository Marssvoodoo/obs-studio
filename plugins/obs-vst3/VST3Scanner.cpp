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

#include "VST3Scanner.h"

#include <PluginPathFingerprint.hpp>

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/moduleinfo/moduleinfoparser.h"

#include "util/bmem.h"
#include <util/platform.h>

#include <algorithm>
#include <cwctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <unordered_map>
#include <utility>

namespace {
constexpr int64_t MAX_MODULE_INFO_BYTES = 16LL * 1024LL * 1024LL;
constexpr size_t MAX_PLUGIN_CLASSES = 10000;

bool hasAsciiControlCharacters(std::string_view text) noexcept
{
	return std::any_of(text.begin(), text.end(), [](char value) {
		const auto byte = static_cast<unsigned char>(value);
		return byte < 0x20 || byte == 0x7f;
	});
}
} // namespace

bool vst3_is_valid_class_id(std::string_view classId) noexcept
{
	return classId.size() == 32 && std::all_of(classId.begin(), classId.end(), [](char value) {
		       return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
			      (value >= 'A' && value <= 'F');
	       });
}

std::string vst3_sanitize_display_text(std::string text)
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
	const auto last = text.find_last_not_of(' ');
	return text.substr(first, last - first + 1);
}

bool vst3_validate_and_sanitize_class_info(VST3ClassInfo &entry)
{
	entry.name = vst3_sanitize_display_text(std::move(entry.name));
	entry.pluginName = vst3_sanitize_display_text(std::move(entry.pluginName));
	return !entry.name.empty() && entry.name.size() <= 1024 && vst3_is_valid_class_id(entry.id) &&
	       !entry.path.empty() && entry.path.size() <= 32768 && !hasAsciiControlCharacters(entry.path) &&
	       !entry.pluginName.empty() && entry.pluginName.size() <= 1024;
}

std::vector<std::string> VST3Scanner::getDefaultSearchPaths() const
{
	std::vector<std::string> paths;

#ifdef _WIN32
	char *programFiles = std::getenv("ProgramFiles");
	if (programFiles) {
		paths.emplace_back(std::string(programFiles) + "\\Common Files\\VST3");
	}

	char *localAppData = std::getenv("LOCALAPPDATA");
	if (localAppData) {
		paths.emplace_back(std::string(localAppData) + "\\Programs\\Common\\VST3");
	}
#elif defined(__APPLE__)
	paths.emplace_back("/Library/Audio/Plug-Ins/VST3");
	if (const char *home = std::getenv("HOME")) {
		paths.emplace_back(std::string(home) + "/Library/Audio/Plug-Ins/VST3");
	}
#elif defined(__linux__)
	paths.emplace_back("/usr/lib/vst3");
	paths.emplace_back("/usr/local/lib/vst3");
	if (const char *home = std::getenv("HOME")) {
		paths.emplace_back(std::string(home) + "/.vst3");
	}
#endif
	return paths;
}

static bool pathComponentEquals(const std::filesystem::path &left, const std::filesystem::path &right)
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

static bool pathIsWithin(const std::filesystem::path &root, const std::filesystem::path &candidate)
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

static bool hasVst3Extension(const std::filesystem::path &path)
{
#ifdef _WIN32
	std::wstring extension = path.extension().native();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		       [](wchar_t c) { return std::towlower(c); });
	return extension == L".vst3";
#else
	return path.extension() == ".vst3";
#endif
}

bool VST3Scanner::isAllowedModulePath(const std::string &modulePath) const
{
	namespace fs = std::filesystem;
	if (modulePath.empty() || modulePath.size() > 32768 || hasAsciiControlCharacters(modulePath)) {
		return false;
	}

	std::error_code error;
	const fs::path candidate = fs::weakly_canonical(fs::u8path(modulePath), error);
	if (error) {
		return false;
	}
	const bool isFile = fs::is_regular_file(candidate, error);
	error.clear();
	const bool isBundle = fs::is_directory(candidate, error) && hasVst3Extension(candidate);
	if (error || (!isFile && !isBundle)) {
		return false;
	}

	if (!hasVst3Extension(candidate)) {
		return false;
	}

	for (const auto &searchPath : getDefaultSearchPaths()) {
		const fs::path root = fs::weakly_canonical(fs::u8path(searchPath), error);
		if (!error && pathIsWithin(root, candidate)) {
			return true;
		}
		error.clear();
	}
	return false;
}

// this retrieves the location of all VST3s (files on windows and dirs on other OSes)
std::unordered_set<std::string> VST3Scanner::getVST3Paths()
{
	std::unordered_set<std::string> fsPaths;

	for (const auto &folder : getDefaultSearchPaths()) {
		try {
			if (!std::filesystem::exists(folder)) {
				continue;
			}

			for (const auto &entry : std::filesystem::recursive_directory_iterator(
				     folder, std::filesystem::directory_options::skip_permission_denied)) {
				std::error_code error;
				if (!entry.exists(error) || error) {
					continue;
				}

#if defined(_WIN32)
				const bool moduleCandidate = entry.is_regular_file(error) && !error &&
							     hasVst3Extension(entry.path());
#else
				const bool moduleCandidate = entry.is_directory(error) && !error &&
							     hasVst3Extension(entry.path());
#endif
				if (moduleCandidate) {
					const std::string modulePath = entry.path().u8string();
					if (isAllowedModulePath(modulePath)) {
						fsPaths.insert(modulePath);
					}
				}
			}
		} catch (const std::filesystem::filesystem_error &error) {
			const std::string message = vst3_sanitize_display_text(error.what());
			blog(LOG_WARNING, "[VST3 Scanner] Skipping unreadable path %s: %.1024s", folder.c_str(),
			     message.c_str());
		}
	}

	return fsPaths;
}

static std::string lowerAscii(std::string s) noexcept
{
	for (auto &c : s) {
		if (c >= 'A' && c <= 'Z') {
			c = static_cast<char>(c - 'A' + 'a');
		}
	}
	return s;
}

std::vector<std::string> VST3Scanner::getModulePaths()
{
	auto paths = getVST3Paths();
	std::vector<std::string> modules(paths.begin(), paths.end());
	std::sort(modules.begin(), modules.end(), [](const std::string &left, const std::string &right) {
		return lowerAscii(left) < lowerAscii(right);
	});
	return modules;
}

// Alphabetical sorting of vst3 classes; note that a VST3 plugin can have multiple classes (ex: LSP). The sorting is
// done across VST3s. In case of multiple classes, the VST3 name is appended.
void VST3Scanner::sort()
{
	pluginList.erase(
		std::remove_if(pluginList.begin(), pluginList.end(),
			       [](VST3ClassInfo &entry) { return !vst3_validate_and_sanitize_class_info(entry); }),
		pluginList.end());

	std::sort(pluginList.begin(), pluginList.end(), [](const VST3ClassInfo &a, const VST3ClassInfo &b) {
		const auto an = lowerAscii(a.name);
		const auto bn = lowerAscii(b.name);
		if (an != bn) {
			return an < bn;
		}

		const auto ap = lowerAscii(a.pluginName);
		const auto bp = lowerAscii(b.pluginName);
		if (ap != bp) {
			return ap < bp;
		}

		if (a.path != b.path) {
			return a.path < b.path;
		}

		return a.id < b.id;
	});

	pluginList.erase(std::unique(pluginList.begin(), pluginList.end(),
				     [](const VST3ClassInfo &a, const VST3ClassInfo &b) {
					     return a.id == b.id && a.path == b.path;
				     }),
			 pluginList.end());

	classCount.clear();
	for (const auto &plugin : pluginList) {
		++classCount[plugin.path];
	}
}

// try to load the moduleinfo.json when provided by VST3
bool VST3Scanner::tryReadModuleInfo(const std::string &bundlePath)
{
	namespace fs = std::filesystem;

	fs::path p(bundlePath);
	fs::path bundleRoot;

#ifdef _WIN32
	if (!fs::is_regular_file(p)) {
		return false;
	}

	// path is <bundleRoot>/Contents/Resources/moduleinfo.json
	bundleRoot = p.parent_path().parent_path().parent_path();
#else
	if (!fs::is_directory(p)) {
		return false;
	}

	bundleRoot = p;
#endif

	fs::path jsonPath = bundleRoot / "Contents" / "Resources" / "moduleinfo.json";

	if (!fs::exists(jsonPath) || !fs::is_regular_file(jsonPath)) {
		return false;
	}

	const std::string jsonPathUtf8 = jsonPath.u8string();
	const int64_t jsonSize = os_get_file_size(jsonPathUtf8.c_str());
	if (jsonSize <= 0 || jsonSize > MAX_MODULE_INFO_BYTES) {
		return false;
	}

	char *jsonBuf = os_quick_read_utf8_file(jsonPathUtf8.c_str());
	if (!jsonBuf) {
		return false;
	}

	std::string json(jsonBuf);
	bfree(jsonBuf);

	auto parsed = Steinberg::ModuleInfoLib::parseJson(json, nullptr);
	if (!parsed) {
		return false;
	}

	bool discardable = parsed.value().factoryInfo.flags & Steinberg::PFactoryInfo::kClassesDiscardable;

	if (discardable) {
		return addModuleClasses(bundlePath);
	}

	return loadFromModuleInfo(*parsed, bundleRoot.u8string());
}

// load classes from moduleinfo.json
bool VST3Scanner::loadFromModuleInfo(const Steinberg::ModuleInfo &info, const std::string &bundleRoot)
{
	const std::string pluginName = std::filesystem::u8path(bundleRoot).stem().u8string();
	size_t added = 0;
	bool discardable = info.factoryInfo.flags & Steinberg::PFactoryInfo::kClassesDiscardable;
	for (const auto &c : info.classes) {
		// We only accept audio effects, not MIDI nor instruments ...
		if (c.category != kVstAudioEffectClass) {
			continue;
		}

		VST3ClassInfo entry;
		entry.id = c.cid;
		entry.name = c.name;
		entry.path = bundleRoot;
		entry.pluginName = pluginName;
		entry.discardable = discardable;

		if (!vst3_validate_and_sanitize_class_info(entry) || pluginList.size() >= MAX_PLUGIN_CLASSES) {
			continue;
		}

		pluginList.push_back(std::move(entry));
		++classCount[bundleRoot];
		++added;
	}

	return added > 0;
}

// load classes directly from binary; very close to previous function, a pity that factorization is not possible.
bool VST3Scanner::addModuleClasses(const std::string &bundlePath)
{
	std::string error;
	const std::string pluginName = std::filesystem::u8path(bundlePath).stem().u8string();
	size_t added = 0;
	VST3::Hosting::Module::Ptr module;
	try {
		module = VST3::Hosting::Module::create(bundlePath, error);
	} catch (const std::exception &exception) {
		const std::string message = vst3_sanitize_display_text(exception.what());
		blog(LOG_ERROR, "[VST3 Scanner] Module threw while loading %s: %.1024s", bundlePath.c_str(),
		     message.c_str());
		return false;
	} catch (...) {
		blog(LOG_ERROR, "[VST3 Scanner] Module threw while loading %s", bundlePath.c_str());
		return false;
	}

	if (!module) {
		error = vst3_sanitize_display_text(std::move(error));
		blog(LOG_ERROR, "[VST3 Scanner] Module failed to load with error %.1024s", error.c_str());
		return false;
	}

	VST3::Hosting::PluginFactory factory = module->getFactory();
	bool discardable = factory.info().classesDiscardable();
	for (const auto &classInfo : factory.classInfos()) {
		if (classInfo.category() == kVstAudioEffectClass) {
			VST3ClassInfo entry;
			entry.id = classInfo.ID().toString();
			entry.name = classInfo.name();
			entry.pluginName = pluginName;
			entry.path = bundlePath;
			entry.discardable = discardable;

			if (!vst3_validate_and_sanitize_class_info(entry) || pluginList.size() >= MAX_PLUGIN_CLASSES) {
				continue;
			}

			pluginList.push_back(std::move(entry));
			++classCount[bundlePath];
			++added;
		}
	}

	return added > 0;
}

bool VST3Scanner::scanModule(const std::string &bundlePath)
{
	if (!isAllowedModulePath(bundlePath)) {
		return false;
	}

	const size_t firstAdded = pluginList.size();
	try {
		if (!(tryReadModuleInfo(bundlePath) || addModuleClasses(bundlePath))) {
			return false;
		}

		std::unordered_map<std::string, PluginPathFingerprint> fingerprints;
		for (size_t index = firstAdded; index < pluginList.size(); ++index) {
			auto [iterator, inserted] = fingerprints.try_emplace(pluginList[index].path);
			if (inserted && !plugin_path_fingerprint(pluginList[index].path, iterator->second)) {
				pluginList.resize(firstAdded);
				sort();
				return false;
			}
			pluginList[index].fileSize = iterator->second.size;
			pluginList[index].sha256 = iterator->second.sha256;
		}
		return true;
	} catch (const std::exception &error) {
		const std::string message = vst3_sanitize_display_text(error.what());
		blog(LOG_ERROR, "[VST3 Scanner] Skipping %s after an exception: %.1024s", bundlePath.c_str(),
		     message.c_str());
	} catch (...) {
		blog(LOG_ERROR, "[VST3 Scanner] Skipping %s after an unknown exception", bundlePath.c_str());
	}
	return false;
}

// scan done by fully loading the module, very costly in time so this is done on a separate thread
bool VST3Scanner::scanForVST3Plugins()
{
	pluginList.clear();
	classCount.clear();
	auto paths = getModulePaths();

	for (const auto &bundlePath : paths) {
		if (pluginList.size() >= MAX_PLUGIN_CLASSES) {
			blog(LOG_WARNING, "[VST3 Scanner] Class limit reached; remaining modules were skipped");
			break;
		}
		scanModule(bundlePath);
	}
	sort();
	return !pluginList.empty();
}

bool VST3Scanner::moduleHasMultipleClasses(const std::string &bundlePath) const
{
	auto it = classCount.find(bundlePath);
	return it != classCount.end() && it->second > 1;
}

std::string VST3Scanner::getNameById(const std::string &class_id) const
{
	for (const auto &c : pluginList) {
		if (c.id == class_id) {
			return c.name;
		}
	}
	return {};
}

std::string VST3Scanner::getPathById(const std::string &class_id) const
{
	for (const auto &c : pluginList) {
		if (c.id == class_id) {
			return c.path;
		}
	}
	return {};
}

bool VST3Scanner::hasCurrentFingerprint(const std::string &class_id) const
{
	for (const auto &entry : pluginList) {
		if (entry.id == class_id) {
			const PluginPathFingerprint expected{entry.fileSize, entry.sha256};
			return plugin_path_fingerprint_matches(entry.path, expected);
		}
	}
	return false;
}
