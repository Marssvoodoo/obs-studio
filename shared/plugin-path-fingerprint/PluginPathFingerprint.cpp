/******************************************************************************
    Copyright (C) 2026 OBS contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 or version 3 of the
    License, at your option.
******************************************************************************/

#include "PluginPathFingerprint.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <string_view>
#include <vector>

namespace {
constexpr std::string_view FILE_DOMAIN = "OBS_PLUGIN_FILE_V1";
constexpr std::string_view BUNDLE_DOMAIN = "OBS_PLUGIN_BUNDLE_V1";

QString pathToQString(const std::filesystem::path &path)
{
#ifdef _WIN32
	return QString::fromStdWString(path.native());
#else
	return QString::fromUtf8(path.u8string().c_str());
#endif
}

void addBytes(QCryptographicHash &hash, const void *data, size_t size)
{
	hash.addData(QByteArrayView(static_cast<const char *>(data), static_cast<qsizetype>(size)));
}

void addUint64(QCryptographicHash &hash, std::uint64_t value)
{
	std::array<unsigned char, 8> encoded{};
	for (size_t index = 0; index < encoded.size(); ++index) {
		encoded[encoded.size() - index - 1] = static_cast<unsigned char>(value >> (index * 8));
	}
	addBytes(hash, encoded.data(), encoded.size());
}

bool addFileContents(const std::filesystem::path &path, QCryptographicHash &hash, std::uint64_t &size)
{
	QFile file(pathToQString(path));
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	const qint64 signedSize = file.size();
	if (signedSize < 0) {
		return false;
	}
	size = static_cast<std::uint64_t>(signedSize);

	QByteArray buffer(1024 * 1024, Qt::Uninitialized);
	while (true) {
		const qint64 read = file.read(buffer.data(), buffer.size());
		if (read < 0) {
			return false;
		}
		if (read == 0) {
			break;
		}
		hash.addData(QByteArrayView(buffer.constData(), read));
	}
	return true;
}

bool fingerprintFile(const std::filesystem::path &path, PluginPathFingerprint &fingerprint)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	addBytes(hash, FILE_DOMAIN.data(), FILE_DOMAIN.size());
	std::uint64_t size = 0;
	if (!addFileContents(path, hash, size)) {
		return false;
	}
	fingerprint.size = size;
	fingerprint.sha256 = hash.result().toHex().toStdString();
	return true;
}

bool fingerprintBundle(const std::filesystem::path &root, PluginPathFingerprint &fingerprint)
{
	std::error_code error;
	std::vector<std::filesystem::path> files;
	std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::none, error);
	const std::filesystem::recursive_directory_iterator end;
	while (!error && iterator != end) {
		const auto status = iterator->symlink_status(error);
		if (error || std::filesystem::is_symlink(status)) {
			return false;
		}
		if (std::filesystem::is_regular_file(status)) {
			files.emplace_back(iterator->path());
		}
		iterator.increment(error);
	}
	if (error || files.empty()) {
		return false;
	}
	std::sort(files.begin(), files.end(), [&root](const auto &left, const auto &right) {
		return left.lexically_relative(root).generic_u8string() <
		       right.lexically_relative(root).generic_u8string();
	});

	QCryptographicHash hash(QCryptographicHash::Sha256);
	addBytes(hash, BUNDLE_DOMAIN.data(), BUNDLE_DOMAIN.size());
	addUint64(hash, files.size());
	std::uint64_t totalSize = 0;
	for (const auto &file : files) {
		const std::string relative = file.lexically_relative(root).generic_u8string();
		addUint64(hash, relative.size());
		addBytes(hash, relative.data(), relative.size());

		std::uint64_t fileSize = 0;
		QCryptographicHash fileHash(QCryptographicHash::Sha256);
		if (!addFileContents(file, fileHash, fileSize) ||
		    totalSize > std::numeric_limits<std::uint64_t>::max() - fileSize) {
			return false;
		}
		totalSize += fileSize;
		addUint64(hash, fileSize);
		const QByteArray digest = fileHash.result();
		addBytes(hash, digest.constData(), static_cast<size_t>(digest.size()));
	}
	fingerprint.size = totalSize;
	fingerprint.sha256 = hash.result().toHex().toStdString();
	return true;
}
} // namespace

bool plugin_path_fingerprint(const std::string &path, PluginPathFingerprint &fingerprint)
{
	fingerprint = {};
	std::error_code error;
	const auto canonical = std::filesystem::weakly_canonical(std::filesystem::u8path(path), error);
	if (error) {
		return false;
	}
	if (std::filesystem::is_regular_file(canonical, error) && !error) {
		return fingerprintFile(canonical, fingerprint);
	}
	error.clear();
	if (std::filesystem::is_directory(canonical, error) && !error) {
		return fingerprintBundle(canonical, fingerprint);
	}
	return false;
}

bool plugin_path_fingerprint_is_valid(const PluginPathFingerprint &fingerprint) noexcept
{
	return fingerprint.sha256.size() == 64 &&
	       std::all_of(fingerprint.sha256.begin(), fingerprint.sha256.end(),
			   [](char value) { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); });
}

bool plugin_path_fingerprint_matches(const std::string &path, const PluginPathFingerprint &expected)
{
	if (!plugin_path_fingerprint_is_valid(expected)) {
		return false;
	}
	PluginPathFingerprint current;
	return plugin_path_fingerprint(path, current) && current.size == expected.size &&
	       current.sha256 == expected.sha256;
}
