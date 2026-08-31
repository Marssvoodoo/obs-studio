/******************************************************************************
    Copyright (C) 2026 OBS contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 or version 3 of the
    License, at your option.
******************************************************************************/

#pragma once

#include <cstdint>
#include <string>

struct PluginPathFingerprint {
	std::uint64_t size = 0;
	std::string sha256;
};

bool plugin_path_fingerprint(const std::string &path, PluginPathFingerprint &fingerprint);
bool plugin_path_fingerprint_is_valid(const PluginPathFingerprint &fingerprint) noexcept;
bool plugin_path_fingerprint_matches(const std::string &path, const PluginPathFingerprint &expected);
