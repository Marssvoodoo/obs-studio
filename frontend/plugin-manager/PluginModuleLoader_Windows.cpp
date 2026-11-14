/******************************************************************************
    Copyright (C) 2026 by FiniteSingularity <finitesingularityttv@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "PluginModuleLoader.hpp"
#include "PluginManager.hpp"

#include <obs.h>
#include <util/dstr.h>
#include <util/platform.h>

#include <string>
#include <string_view>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ShlObj.h>
#undef WIN32_LEAN_AND_MEAN

// Windows Third-Party Plugin Locations
//
// * User Path: <Environment Variable>
//     * Binary: <Environment Variable>/<Plugin>/<Plugin>.dll
//     * Data: <Environment Variable>/<Plugin>/data
//
// * Root Path: C:/ProgramData
//     * Binary: <Root Path>/obs-studio/plugins/<Plugin>/<Plugin>.dll
//     * Data: <Root Path>/obs-studio/plugins/<Plugin>/data
//
// * Portable Root Path: <OBS Binary Location>
//     * Binary: <Portable Root Path>/../../plugins/<Plugin>/<Plugin>.dll
//     * Data: <Portable Root Path>/../../plugins/<Plugin>/data
//
// * Legacy User Path: <Environment Variable> + <Environment Data Variable>
//     * Binary: <Environment Variable>/<Plugin>.dll
//     * Data: <Environment Data Variable>/<Plugin>/data
//
// * Legacy Root Path: <OBS Binary Location>
//     * Binary: <Legacy Root Path>/../../obs-plugins/<Plugin>.dll
//     * Data: <Legacy Root Path>/../../data/obs-plugins/<Plugin>
//
// * Legacy Portable Path: <OBS Binary Location>
//     * Binary: <Legacy Root Path>/../../obs-plugins/<Plugin>.dll
//     * Data: <Legacy Root Path>/../../data/obs-plugins/<Plugin>
//
// * Legacy System Path: C:/ProgramData
//     * Binary: <Legacy System Path>/obs-studio/plugins/<Plugin>/bin/<Plugin>.dll
//     * Data: <Legacy System Path>/obs-studio/plugins/<Plugin>/data
//

using State = OBS::PluginManager::State;
using ModuleType = obs_runtime_module_type;

constexpr std::string_view kPluginPathSuffix{"obs-studio/plugins/%module%"};
constexpr std::string_view kUserPluginPathSuffix{"/%module%"};
constexpr std::string_view kPortablePluginPath{"../../plugins/%module%"};

constexpr std::string_view kLegacyPortableBinaryPath{"../../obs-plugins/64bit/"};
constexpr std::string_view kLegacyPortableDataPath{"../../data/obs-plugins/%module%"};

namespace {
State loadPluginsFromPath(const std::string &pathString, ModuleType type, ModuleList &failedModules)
{
	std::string binaryPath{pathString};
	std::string dataPath{pathString};

	switch (type) {
	case OBS_MODULE_TYPE_LEGACY_PLUGIN: {
		binaryPath.append("/bin/64bit");
		dataPath.append("/data");
		break;
	}
	default: {
		dataPath.append("/data");
		break;
	}
	}

	ModuleLoadInfo info = {0};
	info.path_info.binary = binaryPath.c_str();
	info.path_info.data = dataPath.c_str();
	info.type = type;
	info.name = nullptr;

	int failedModuleCount = loadPluginsByInfo(info, failedModules);

	State result = (failedModuleCount > 0) ? State::PartialFailure : State::Success;

	return result;
}

/* Fork: also search the stock OBS Studio installation's legacy plugin folder, so third-party plugins installed for
 * stock OBS are available to this build. The folder is resolved through the shell instead of an environment
 * variable so it cannot be redirected. Core modules found there are skipped by libobs as legacy core modules. */
State loadProgramFilesLegacyPlugins(ModuleList &failedModules)
{
	PWSTR programFilesWide = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &programFilesWide)) || !programFilesWide) {
		return State::Success;
	}

	std::string programFiles{};
	int length = WideCharToMultiByte(CP_UTF8, 0, programFilesWide, -1, nullptr, 0, nullptr, nullptr);
	if (length > 1) {
		programFiles.resize(static_cast<size_t>(length - 1));
		WideCharToMultiByte(CP_UTF8, 0, programFilesWide, -1, programFiles.data(), length, nullptr, nullptr);
	}
	CoTaskMemFree(programFilesWide);

	if (programFiles.empty()) {
		return State::Success;
	}

	std::string binaryPath = programFiles + "/obs-studio/obs-plugins/64bit";
	std::string dataPath = programFiles + "/obs-studio/data/obs-plugins/%module%";

	ModuleLoadInfo info = {0};
	info.path_info.binary = binaryPath.c_str();
	info.path_info.data = dataPath.c_str();
	info.type = OBS_MODULE_TYPE_LEGACY_PLUGIN;
	info.name = nullptr;

	int failedModuleCount = loadPluginsByInfo(info, failedModules);

	return (failedModuleCount > 0) ? State::PartialFailure : State::Success;
}
} // namespace

namespace OBS {
State PluginManager::loadPlugins(bool usePortableMode)
{
	State userPluginState = State::Failure;
	std::string userPluginPath = getEnvironmentVariable(kPathVariable);

	/* With the pre-33.0 data variable present, OBS_PLUGINS_PATH is half of the old legacy pair (handled in
	 * loadLegacyPlugins), not a 33.0-layout plugin root. */
	if (!getEnvironmentVariable(kPre33DataPathVariable).empty()) {
		userPluginPath.clear();
	}

	if (!userPluginPath.empty() && !isSafePluginPath(userPluginPath)) {
		blog(LOG_WARNING, "%s rejected (unsafe path)", kPathVariable.data());
		userPluginState = State::Success;
	} else if (!userPluginPath.empty()) {
		userPluginPath.append(kUserPluginPathSuffix);

		userPluginState = loadPluginsFromPath(userPluginPath, OBS_MODULE_TYPE_PLUGIN, failedModules_);
	} else {
		userPluginState = State::Success;
	}

	State libraryPluginState = State::Failure;
	std::string appLibraryPath{};

	if (!usePortableMode) {
		appLibraryPath.resize(MAX_PATH);
		int bufferSize = os_get_program_data_path(appLibraryPath.data(), appLibraryPath.size(),
							  kPluginPathSuffix.data());
		if (bufferSize > 0) {
			appLibraryPath.resize(bufferSize);
		}
	} else {
		appLibraryPath = kPortablePluginPath;
	}

	libraryPluginState = loadPluginsFromPath(appLibraryPath, OBS_MODULE_TYPE_PLUGIN, failedModules_);

	return (userPluginState && libraryPluginState) ? State::Success : State::PartialFailure;
}

State PluginManager::loadLegacyPlugins(bool usePortableMode)
{
	State userPluginState = State::Failure;

	std::string userPluginPath = getEnvironmentVariable(kLegacyBinaryPathVariable);
	std::string userDataPath = getEnvironmentVariable(kLegacyDataPathVariable);

	if (userPluginPath.empty() && userDataPath.empty()) {
		userPluginPath = getEnvironmentVariable(kPathVariable);
		userDataPath = getEnvironmentVariable(kPre33DataPathVariable);
	}

	if (!userPluginPath.empty() && !userDataPath.empty() &&
	    (!isSafePluginPath(userPluginPath) || !isSafePluginPath(userDataPath))) {
		blog(LOG_WARNING, "%s rejected (unsafe path)", kLegacyBinaryPathVariable.data());
		userPluginState = State::Success;
	} else if (!userPluginPath.empty() && !userDataPath.empty()) {
		userDataPath.append(kUserPluginPathSuffix);

		ModuleLoadInfo info = {0};
		info.path_info.binary = userPluginPath.c_str();
		info.path_info.data = userDataPath.c_str();
		info.type = OBS_MODULE_TYPE_LEGACY_PLUGIN;
		info.name = nullptr;

		int failedModuleCount = loadPluginsByInfo(info, failedModules_);

		userPluginState = (failedModuleCount > 0) ? State::PartialFailure : State::Success;
	} else {
		userPluginState = State::Success;
	}

	State portablePluginState = State::Failure;

	{
		ModuleLoadInfo info = {0};
		info.path_info.binary = kLegacyPortableBinaryPath.data();
		info.path_info.data = kLegacyPortableDataPath.data();
		info.type = OBS_MODULE_TYPE_LEGACY_PLUGIN;
		info.name = nullptr;

		int failedModuleCount = loadPluginsByInfo(info, failedModules_);

		portablePluginState = (failedModuleCount > 0) ? State::PartialFailure : State::Success;
	}

	State libraryPluginState = State::Failure;

	if (!usePortableMode) {
		std::string appLibraryPath{};
		appLibraryPath.resize(MAX_PATH);
		int bufferSize = os_get_program_data_path(appLibraryPath.data(), appLibraryPath.size(),
							  kPluginPathSuffix.data());
		if (bufferSize > 0) {
			appLibraryPath.resize(bufferSize);
			libraryPluginState =
				loadPluginsFromPath(appLibraryPath, OBS_MODULE_TYPE_LEGACY_PLUGIN, failedModules_);
		}
	} else {
		libraryPluginState = State::Success;
	}

	State programFilesPluginState = loadProgramFilesLegacyPlugins(failedModules_);

	return (userPluginState && portablePluginState && libraryPluginState && programFilesPluginState)
		       ? State::Success
		       : State::PartialFailure;
}
} // namespace OBS
