/******************************************************************************
    Copyright (C) 2026 OBS contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "VST3Cache.h"

#include "VST3Scanner.h"

#include <obs-data.h>
#include <util/platform.h>

#include <string>
#include <unordered_set>

namespace {
constexpr int64_t MAX_CACHE_BYTES = 16LL * 1024LL * 1024LL;
constexpr size_t MAX_CACHE_ENTRIES = 10000;
} // namespace

bool vst3_list_load_json(VST3Scanner &scanner, const char *path, bool safeBackup, bool includeDiscardable)
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
	if (!root) {
		return false;
	}

	if (obs_data_get_int(root, "version") != 1) {
		obs_data_release(root);
		return false;
	}

	obs_data_array_t *plugins = obs_data_get_array(root, "plugins");
	if (!plugins) {
		obs_data_release(root);
		return false;
	}

	const size_t count = obs_data_array_count(plugins);
	if (count > MAX_CACHE_ENTRIES) {
		obs_data_array_release(plugins);
		obs_data_release(root);
		return false;
	}

	VST3Scanner loaded;
	std::unordered_set<std::string> seen;
	bool rejectedEntry = false;
	seen.reserve(count);
	for (size_t index = 0; index < count; ++index) {
		obs_data_t *object = obs_data_array_item(plugins, index);
		if (!object) {
			rejectedEntry = true;
			continue;
		}
		VST3ClassInfo entry;
		entry.name = obs_data_get_string(object, "name");
		entry.id = obs_data_get_string(object, "id");
		entry.path = obs_data_get_string(object, "path");
		entry.pluginName = obs_data_get_string(object, "pluginName");
		entry.discardable = obs_data_get_bool(object, "discardable");
		obs_data_release(object);

		if ((!includeDiscardable && entry.discardable) || !vst3_validate_and_sanitize_class_info(entry) ||
		    !loaded.isAllowedModulePath(entry.path)) {
			rejectedEntry = true;
			continue;
		}

		std::string identity = entry.id;
		identity.push_back('\0');
		identity.append(entry.path);
		if (!seen.emplace(std::move(identity)).second) {
			continue;
		}

		loaded.pluginList.emplace_back(std::move(entry));
	}

	obs_data_array_release(plugins);
	obs_data_release(root);
	if (includeDiscardable && rejectedEntry) {
		return false;
	}

	loaded.sort();
	scanner = std::move(loaded);
	return true;
}

bool vst3_list_save_json(const VST3Scanner &scanner, const char *path, const char *backupExtension,
			 bool includeDiscardable)
{
	if (!path || scanner.pluginList.size() > MAX_CACHE_ENTRIES) {
		return false;
	}

	obs_data_t *root = obs_data_create();
	obs_data_array_t *plugins = obs_data_array_create();
	for (const auto &sourceEntry : scanner.pluginList) {
		VST3ClassInfo entry = sourceEntry;
		if ((!includeDiscardable && entry.discardable) || !vst3_validate_and_sanitize_class_info(entry) ||
		    !scanner.isAllowedModulePath(entry.path)) {
			continue;
		}

		obs_data_t *object = obs_data_create();
		obs_data_set_string(object, "name", entry.name.c_str());
		obs_data_set_string(object, "id", entry.id.c_str());
		obs_data_set_string(object, "path", entry.path.c_str());
		obs_data_set_string(object, "pluginName", entry.pluginName.c_str());
		obs_data_set_bool(object, "discardable", entry.discardable);
		obs_data_array_push_back(plugins, object);
		obs_data_release(object);
	}

	obs_data_set_int(root, "version", 1);
	obs_data_set_array(root, "plugins", plugins);
	obs_data_array_release(plugins);

	const bool saved = obs_data_save_json_safe(root, path, "tmp", backupExtension);
	obs_data_release(root);
	return saved;
}
