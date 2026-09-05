#include <VST3Cache.h>
#include <VST3Scanner.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <iostream>

namespace {
bool writeFile(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QIODevice::WriteOnly) &&
	       file.write(bytes) == bytes.size();
}

const QByteArray metadata = R"json({
  "Name":"SyntheticReview", "Version":"1.0.0",
  "Factory Info":{"Vendor":"OBS regression fixture","URL":"","E-Mail":"",
    "Flags":{"Classes Discardable":false,"Unicode":true}},
  "Classes":[{"CID":"0123456789ABCDEF0123456789ABCDEF","Category":"Audio Module Class",
    "Name":"Synthetic effect","Vendor":"OBS regression fixture","Version":"1.0.0",
    "SDKVersion":"VST 3.7.12","Class Flags":0,"Cardinality":2147483647}]
})json";

bool rejectModule(const QString &scanner, const QString &root, const QString &module, const QString &label)
{
	QProcessEnvironment environment;
	for (const auto &key : {"SystemRoot", "WINDIR", "PATH"}) {
		environment.insert(QString::fromLatin1(key), qEnvironmentVariable(key));
	}
	for (const auto &key : {"TEMP", "TMP", "USERPROFILE", "APPDATA"}) {
		environment.insert(QString::fromLatin1(key), root);
	}
	// Do not inherit alternate-case ProgramFiles keys or real plug-in roots.
	QDir().mkpath(root + "/empty-program-files");
	QDir().mkpath(root + "/local");
	for (const auto &key : {"ProgramFiles", "PROGRAMFILES"}) {
		environment.insert(QString::fromLatin1(key), root + "/empty-program-files");
	}
	for (const auto &key : {"LOCALAPPDATA", "LocalAppData"}) {
		environment.insert(QString::fromLatin1(key), root + "/local");
	}

	const QString output = root + "/.vst3-scan-" + label + ".json";
	QProcess process;
	process.setProcessEnvironment(environment);
	process.setWorkingDirectory(QFileInfo(scanner).absolutePath());
	process.start(scanner, {"--scan-module", module, output});
	if (!process.waitForStarted(3000) || !process.waitForFinished(20000)) {
		process.kill();
		process.waitForFinished(3000);
		std::cerr << label.toStdString() << ": scanner did not complete\n";
		return false;
	}
	QFile file(output);
	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
	    !file.open(QIODevice::ReadOnly)) {
		std::cerr << label.toStdString() << ": scanner protocol failed\n";
		return false;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	const auto plugins = document.object().value("plugins");
	const bool rejected = plugins.isArray() && plugins.toArray().isEmpty();
	std::cout << label.toStdString() << ": entries=" << plugins.toArray().size() << " (expected 0)\n";
	return rejected;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (app.arguments().size() != 3) {
		return 2;
	}
	QTemporaryDir temporary(QDir::tempPath() + "/obs-vst3-regression-XXXXXX");
	if (!temporary.isValid()) {
		return 2;
	}
	const QString root = temporary.path();
	const QString plugins = root + "/local/Programs/Common/VST3/";
	const QString binary = plugins + "MetadataOnly.vst3/Contents/x86_64-win/MetadataOnly.vst3";
	const QString moduleInfo = plugins + "MetadataOnly.vst3/Contents/Resources/moduleinfo.json";
	const QString factoryBinary = plugins + "FactoryOnly.vst3/Contents/x86_64-win/FactoryOnly.vst3";
	if (!writeFile(binary, "Synthetic plain text, not a Windows DLL") || !writeFile(moduleInfo, metadata) ||
	    !QDir().mkpath(QFileInfo(factoryBinary).absolutePath()) ||
	    !QFile::copy(app.arguments()[2], factoryBinary)) {
		return 2;
	}

	int failures = 0;
	failures += !rejectModule(app.arguments()[1], root, binary, "invalid-binary-valid-metadata");
	failures += !rejectModule(app.arguments()[1], root, factoryBinary, "uninitializable-class");
	const QString legacy = root + "/legacy-cache.json";
	if (!writeFile(legacy, R"({"version":2,"plugins":[]})")) {
		return 2;
	}
	VST3Scanner loaded;
	const bool legacyAccepted = vst3_list_load_json(loaded, legacy.toUtf8().constData(), false, true);
	std::cout << "Metadata-only cache version accepted=" << legacyAccepted << " (expected 0)\n";
	failures += legacyAccepted;
	const QString current = root + "/current-cache.json";
	VST3Scanner empty;
	failures += !vst3_list_save_json(empty, current.toUtf8().constData(), nullptr, true) ||
		    !vst3_list_load_json(loaded, current.toUtf8().constData(), false, true);
	return failures ? 1 : 0;
}
