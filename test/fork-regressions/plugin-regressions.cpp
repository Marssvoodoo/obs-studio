#include <PluginPathFingerprint.hpp>
#include <VST3BufferLayout.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
bool writeFile(const std::filesystem::path &path, const std::string &contents)
{
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	return stream.good();
}

int checkFingerprints(const std::filesystem::path &root)
{
	const auto file = root / "plugin.dll";
	if (!writeFile(file, "first-content")) {
		return 1;
	}

	PluginPathFingerprint first;
	PluginPathFingerprint second;
	if (!plugin_path_fingerprint(file.u8string(), first) || first.sha256.size() != 64) {
		return 1;
	}
	if (!plugin_path_fingerprint_matches(file.u8string(), first)) {
		return 1;
	}
	if (!writeFile(file, "other-content") || !plugin_path_fingerprint(file.u8string(), second)) {
		return 1;
	}
	if (first.size != second.size || first.sha256 == second.sha256 ||
	    plugin_path_fingerprint_matches(file.u8string(), first)) {
		return 1;
	}

	const auto bundle = root / "Example.vst3";
	std::filesystem::create_directories(bundle / "Contents");
	if (!writeFile(bundle / "Contents" / "module.vst3", "binary") ||
	    !writeFile(bundle / "Contents" / "moduleinfo.json", "metadata")) {
		return 1;
	}
	if (!plugin_path_fingerprint(bundle.u8string(), first) || !plugin_path_fingerprint(bundle.u8string(), second) ||
	    first.sha256 != second.sha256) {
		return 1;
	}
	if (!writeFile(bundle / "Contents" / "moduleinfo.json", "changed!") ||
	    !plugin_path_fingerprint(bundle.u8string(), second) || first.sha256 == second.sha256) {
		return 1;
	}
	return 0;
}

int checkOutputLayout()
{
	size_t count = 0;
	if (!vst3_output_float_count(480, 2, count) || count != 960) {
		return 1;
	}
	if (vst3_output_channel_offset(480, 0) != 0 || vst3_output_channel_offset(480, 1) != 480) {
		return 1;
	}
	if (vst3_output_float_count(static_cast<size_t>(-1), 2, count)) {
		return 1;
	}
	return 0;
}
} // namespace

int main()
{
	const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	const auto root = std::filesystem::temp_directory_path() / ("obs-fork-regressions-" + unique);
	std::error_code error;
	std::filesystem::create_directories(root, error);
	if (error) {
		return 1;
	}

	const int failures = checkFingerprints(root) + checkOutputLayout();
	std::filesystem::remove_all(root, error);
	if (failures) {
		std::cerr << "OBS plug-in regression checks failed\n";
	}
	return failures ? 1 : 0;
}
