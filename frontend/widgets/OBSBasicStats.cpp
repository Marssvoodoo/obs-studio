#include "OBSBasicStats.hpp"

#include <widgets/OBSBasic.hpp>
#include <utility/NativeTelemetryGuard.hpp>

#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cstring>
#include <utility>

#ifdef _WIN32
#include <util/windows/ComPtr.hpp>

#include <dxgi1_2.h>
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

#include "moc_OBSBasicStats.cpp"

#define TIMER_INTERVAL 2000
#define REC_TIME_LEFT_INTERVAL 30000

/* ---------------------------------------------------------------------------
 * NVML client — dynamic-load NVIDIA Management Library to surface GPU util,
 * VRAM, and temperature in the Stats panel. NVML ships with the NVIDIA driver
 * so no extra dependency at link time. Silently no-ops on non-NVIDIA systems
 * and on macOS (where NVML is unavailable).
 * ------------------------------------------------------------------------- */

namespace {

typedef int nvmlReturn_t;
typedef void *nvmlDevice_t;
struct nvmlUtilization_t {
	unsigned int gpu;
	unsigned int memory;
};
struct nvmlMemory_t {
	unsigned long long total;
	unsigned long long free;
	unsigned long long used;
};
constexpr int NVML_SUCCESS_OK = 0;
constexpr int NVML_TEMPERATURE_GPU_SENSOR = 0;
#ifdef _WIN32
constexpr int CUDA_SUCCESS_OK = 0;

static bool selectedAdapterLuid(LUID &luid)
{
	obs_video_info videoInfo{};
	if (!obs_get_video_info(&videoInfo)) {
		return false;
	}

	ComPtr<IDXGIFactory1> factory;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
		return false;
	}

	ComPtr<IDXGIAdapter1> adapter;
	if (factory->EnumAdapters1(videoInfo.adapter, adapter.Assign()) != S_OK) {
		return false;
	}

	DXGI_ADAPTER_DESC1 description{};
	if (FAILED(adapter->GetDesc1(&description))) {
		return false;
	}

	luid = description.AdapterLuid;
	return true;
}

static bool cudaPciBusIdForLuid(const LUID &selectedLuid, char *pciBusId, int pciBusIdSize)
{
	HMODULE cuda = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!cuda) {
		return false;
	}

	using CUdevice = int;
	using CuInit = int(WINAPI *)(unsigned int);
	using CuDeviceGetCount = int(WINAPI *)(int *);
	using CuDeviceGet = int(WINAPI *)(CUdevice *, int);
	using CuDeviceGetLuid = int(WINAPI *)(char *, unsigned int *, CUdevice);
	using CuDeviceGetPciBusId = int(WINAPI *)(char *, int, CUdevice);

	auto cuInit = reinterpret_cast<CuInit>(GetProcAddress(cuda, "cuInit"));
	auto cuDeviceGetCount = reinterpret_cast<CuDeviceGetCount>(GetProcAddress(cuda, "cuDeviceGetCount"));
	auto cuDeviceGet = reinterpret_cast<CuDeviceGet>(GetProcAddress(cuda, "cuDeviceGet"));
	auto cuDeviceGetLuid = reinterpret_cast<CuDeviceGetLuid>(GetProcAddress(cuda, "cuDeviceGetLuid"));
	auto cuDeviceGetPciBusId = reinterpret_cast<CuDeviceGetPciBusId>(GetProcAddress(cuda, "cuDeviceGetPCIBusId"));

	bool matched = false;
	if (cuInit && cuDeviceGetCount && cuDeviceGet && cuDeviceGetLuid && cuDeviceGetPciBusId &&
	    cuInit(0) == CUDA_SUCCESS_OK) {
		int deviceCount = 0;
		if (cuDeviceGetCount(&deviceCount) == CUDA_SUCCESS_OK) {
			for (int index = 0; index < deviceCount; ++index) {
				CUdevice cudaDevice = 0;
				char cudaLuid[sizeof(LUID)]{};
				unsigned int nodeMask = 0;
				if (cuDeviceGet(&cudaDevice, index) != CUDA_SUCCESS_OK ||
				    cuDeviceGetLuid(cudaLuid, &nodeMask, cudaDevice) != CUDA_SUCCESS_OK ||
				    std::memcmp(cudaLuid, &selectedLuid, sizeof(selectedLuid)) != 0) {
					continue;
				}

				matched = cuDeviceGetPciBusId(pciBusId, pciBusIdSize, cudaDevice) == CUDA_SUCCESS_OK;
				break;
			}
		}
	}

	FreeLibrary(cuda);
	return matched;
}
#endif

class NVMLClient {
public:
	NVMLClient()
	{
#ifdef _WIN32
		call([&] {
			lib = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			return lib != nullptr;
		});
#elif defined(__linux__)
		lib = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
#endif
		if (!lib) {
			return;
		}

		auto sym = [&](const char *name) -> void * {
#ifdef _WIN32
			return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(lib), name));
#elif defined(__linux__)
			return dlsym(lib, name);
#else
			(void)name;
			return nullptr;
#endif
		};

		init = reinterpret_cast<decltype(init)>(sym("nvmlInit_v2"));
		shutdown_ = reinterpret_cast<decltype(shutdown_)>(sym("nvmlShutdown"));
#ifdef _WIN32
		getHandleByPciBusId =
			reinterpret_cast<decltype(getHandleByPciBusId)>(sym("nvmlDeviceGetHandleByPciBusId_v2"));
#elif defined(__linux__)
		getHandleByIndex = reinterpret_cast<decltype(getHandleByIndex)>(sym("nvmlDeviceGetHandleByIndex_v2"));
#endif
		getUtil = reinterpret_cast<decltype(getUtil)>(sym("nvmlDeviceGetUtilizationRates"));
		getMemory = reinterpret_cast<decltype(getMemory)>(sym("nvmlDeviceGetMemoryInfo"));
		getTemp = reinterpret_cast<decltype(getTemp)>(sym("nvmlDeviceGetTemperature"));

		if (!init || !shutdown_ || !getUtil || !getMemory || !getTemp) {
			return;
		}
#ifdef _WIN32
		if (!getHandleByPciBusId) {
			return;
		}
#elif defined(__linux__)
		if (!getHandleByIndex) {
			return;
		}
#endif
		if (!call([&] { return init() == NVML_SUCCESS_OK; })) {
			return;
		}
		initialized = true;
		if (!call([&] { return resolveSelectedDevice(); })) {
			call([&] { return shutdown_() == NVML_SUCCESS_OK; });
			initialized = false;
			return;
		}
		ok = true;
	}

	~NVMLClient()
	{
		if (initialized && shutdown_) {
			call([&] { return shutdown_() == NVML_SUCCESS_OK; });
		}
		// After a driver fault, even shutdown or DLL detach can fault again.
		// Retain the library until process exit instead of re-entering it.
		if (guard.disabled()) {
			return;
		}
#ifdef _WIN32
		if (lib) {
			call([&] { return FreeLibrary(static_cast<HMODULE>(lib)) != FALSE; });
		}
#elif defined(__linux__)
		if (lib) {
			dlclose(lib);
		}
#endif
	}

	bool available() const { return ok && !guard.disabled(); }

	bool sampleUtilization(unsigned int &gpu_pct)
	{
		if (!available()) {
			return false;
		}
		nvmlUtilization_t u{};
		if (!call([&] { return getUtil(device, &u) == NVML_SUCCESS_OK; })) {
			return false;
		}
		gpu_pct = u.gpu;
		return true;
	}

	bool sampleMemory(unsigned long long &used_bytes, unsigned long long &total_bytes)
	{
		if (!available()) {
			return false;
		}
		nvmlMemory_t m{};
		if (!call([&] { return getMemory(device, &m) == NVML_SUCCESS_OK; })) {
			return false;
		}
		used_bytes = m.used;
		total_bytes = m.total;
		return true;
	}

	bool sampleTemperature(unsigned int &celsius)
	{
		if (!available()) {
			return false;
		}
		unsigned int t = 0;
		if (!call([&] { return getTemp(device, NVML_TEMPERATURE_GPU_SENSOR, &t) == NVML_SUCCESS_OK; })) {
			return false;
		}
		celsius = t;
		return true;
	}

private:
	template<typename Operation> bool call(Operation &&operation)
	{
		const bool alreadyDisabled = guard.disabled();
		const bool result = guard.call(std::forward<Operation>(operation));
		if (!alreadyDisabled && guard.disabled()) {
			blog(LOG_WARNING, "[GPU Stats] NVIDIA telemetry disabled after native fault 0x%08lx",
			     static_cast<unsigned long>(guard.faultCode()));
		}
		return result;
	}

	bool resolveSelectedDevice()
	{
#ifdef _WIN32
		LUID selectedLuid{};
		char pciBusId[32]{};
		if (!selectedAdapterLuid(selectedLuid) ||
		    !cudaPciBusIdForLuid(selectedLuid, pciBusId, static_cast<int>(sizeof(pciBusId)))) {
			return false;
		}
		return getHandleByPciBusId(pciBusId, &device) == NVML_SUCCESS_OK;
#elif defined(__linux__)
		obs_video_info videoInfo{};
		const unsigned int adapter = obs_get_video_info(&videoInfo) ? videoInfo.adapter : 0;
		return getHandleByIndex(adapter, &device) == NVML_SUCCESS_OK;
#else
		return false;
#endif
	}

	NativeTelemetryGuard guard;
	void *lib = nullptr;
	bool ok = false;
	bool initialized = false;
	nvmlDevice_t device = nullptr;

	nvmlReturn_t (*init)() = nullptr;
	nvmlReturn_t (*shutdown_)() = nullptr;
#ifdef _WIN32
	nvmlReturn_t (*getHandleByPciBusId)(const char *, nvmlDevice_t *) = nullptr;
#elif defined(__linux__)
	nvmlReturn_t (*getHandleByIndex)(unsigned int, nvmlDevice_t *) = nullptr;
#endif
	nvmlReturn_t (*getUtil)(nvmlDevice_t, nvmlUtilization_t *) = nullptr;
	nvmlReturn_t (*getMemory)(nvmlDevice_t, nvmlMemory_t *) = nullptr;
	nvmlReturn_t (*getTemp)(nvmlDevice_t, int, unsigned int *) = nullptr;
};

NVMLClient &nvml()
{
	static NVMLClient instance;
	return instance;
}

} // namespace

void OBSBasicStats::OBSFrontendEvent(enum obs_frontend_event event, void *ptr)
{
	OBSBasicStats *stats = static_cast<OBSBasicStats *>(ptr);

	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		stats->StartRecTimeLeft();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		stats->ResetRecTimeLeft();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		// This is only reached when the non-closable (dock) stats
		// window is being cleaned up. The closable stats window is
		// already gone by this point as it's deleted on close.
		obs_frontend_remove_event_callback(OBSFrontendEvent, stats);
		break;
	default:
		break;
	}
}

static QString MakeTimeLeftText(int hours, int minutes)
{
	return QTStr("Basic.Stats.DiskFullIn.Text").arg(QString::number(hours), QString::number(minutes));
}

static QString MakeMissedFramesText(uint32_t total_lagged, uint32_t total_rendered, long double num)
{
	return QString("%1 / %2 (%3%)")
		.arg(QString::number(total_lagged), QString::number(total_rendered), QString::number(num, 'f', 1));
}

OBSBasicStats::OBSBasicStats(QWidget *parent, bool closable)
	: QFrame(parent),
	  cpu_info(os_cpu_usage_info_start()),
	  timer(this),
	  recTimeLeft(this)
{
	QVBoxLayout *mainLayout = new QVBoxLayout();
	QGridLayout *topLayout = new QGridLayout();
	outputLayout = new QGridLayout();

	bitrates.reserve(REC_TIME_LEFT_INTERVAL / TIMER_INTERVAL);

	int row = 0;

	auto newStatBare = [&](QString name, QWidget *label, int col, const QString &configKey = QString(),
			       const QString &displayName = QString()) {
		QLabel *typeLabel = new QLabel(name, this);
		topLayout->addWidget(typeLabel, row, col);
		topLayout->addWidget(label, row++, col + 1);
		QLabel *valueLabel = qobject_cast<QLabel *>(label);
		if (!configKey.isEmpty() && valueLabel) {
			RegisterStatRow(typeLabel, valueLabel, configKey, displayName.isEmpty() ? name : displayName);
		}
	};

	auto newStat = [&](const char *strLoc, QWidget *label, int col, const QString &configKey = QString(),
			   const QString &displayName = QString()) {
		std::string str = "Basic.Stats.";
		str += strLoc;
		newStatBare(QTStr(str.c_str()), label, col, configKey, displayName);
	};

	/* --------------------------------------------- */

	cpuUsage = new QLabel(this);
	hddSpace = new QLabel(this);
	recordTimeLeft = new QLabel(this);
	memUsage = new QLabel(this);

	QString str = MakeTimeLeftText(99999, 59);
	int textWidth = recordTimeLeft->fontMetrics().boundingRect(str).width();
	recordTimeLeft->setMinimumWidth(textWidth);

	newStat("CPUUsage", cpuUsage, 0, "show_cpu", QStringLiteral("CPU Usage"));
	newStat("HDDSpaceAvailable", hddSpace, 0, "show_hdd", QStringLiteral("Disk Space Available"));
	newStat("DiskFullIn", recordTimeLeft, 0, "show_disk_full", QStringLiteral("Time Until Disk Full"));
	newStat("MemoryUsage", memUsage, 0, "show_mem", QStringLiteral("Memory Usage"));

	/* NVIDIA-only via NVML. Labels are hardcoded English because this fork
	 * intentionally avoids adding new .ini i18n keys. */
	gpuUsage = new QLabel(this);
	vramUsage = new QLabel(this);
	gpuTemp = new QLabel(this);
	newStatBare(QStringLiteral("GPU Usage:"), gpuUsage, 0, "show_gpu", QStringLiteral("GPU Usage"));
	newStatBare(QStringLiteral("VRAM:"), vramUsage, 0, "show_vram", QStringLiteral("VRAM"));
	newStatBare(QStringLiteral("GPU Temp:"), gpuTemp, 0, "show_gpu_temp", QStringLiteral("GPU Temperature"));

	fps = new QLabel(this);
	renderTime = new QLabel(this);
	skippedFrames = new QLabel(this);
	missedFrames = new QLabel(this);

	str = MakeMissedFramesText(999999, 999999, 99.99);
	textWidth = missedFrames->fontMetrics().boundingRect(str).width();
	missedFrames->setMinimumWidth(textWidth);

	row = 0;

	newStatBare("FPS", fps, 2, "show_fps", QStringLiteral("FPS"));
	newStat("AverageTimeToRender", renderTime, 2, "show_render_time", QStringLiteral("Average Time To Render"));
	newStat("MissedFrames", missedFrames, 2, "show_missed", QStringLiteral("Missed Frames"));
	newStat("SkippedFrames", skippedFrames, 2, "show_skipped", QStringLiteral("Skipped Frames"));

	/* --------------------------------------------- */
	QPushButton *closeButton = nullptr;
	if (closable) {
		closeButton = new QPushButton(QTStr("Close"));
	}
	QPushButton *resetButton = new QPushButton(QTStr("Reset"));
	QPushButton *configureButton = new QPushButton(QStringLiteral("Configure…"));
	QHBoxLayout *buttonLayout = new QHBoxLayout;
	buttonLayout->addStretch();
	buttonLayout->addWidget(configureButton);
	buttonLayout->addWidget(resetButton);
	if (closable) {
		buttonLayout->addWidget(closeButton);
	}

	/* --------------------------------------------- */

	int col = 0;
	auto addOutputCol = [&](const char *loc) {
		QLabel *label = new QLabel(QTStr(loc), this);
		label->setStyleSheet("font-weight: bold");
		outputLayout->addWidget(label, 0, col++);
	};

	addOutputCol("Basic.Settings.Output");
	addOutputCol("Basic.Stats.Status");
	addOutputCol("Basic.Stats.DroppedFrames");
	addOutputCol("Basic.Stats.MegabytesSent");
	addOutputCol("Basic.Stats.Bitrate");

	/* --------------------------------------------- */

	AddOutputLabels(QTStr("Basic.Stats.Output.Stream"));
	AddOutputLabels(QTStr("Basic.Stats.Output.Recording"));

	/* --------------------------------------------- */

	QVBoxLayout *outputContainerLayout = new QVBoxLayout();
	outputContainerLayout->addLayout(outputLayout);
	outputContainerLayout->addStretch();

	QWidget *widget = new QWidget(this);
	widget->setLayout(outputContainerLayout);

	QScrollArea *scrollArea = new QScrollArea(this);
	scrollArea->setWidget(widget);
	scrollArea->setWidgetResizable(true);

	/* --------------------------------------------- */

	mainLayout->addLayout(topLayout);
	mainLayout->addWidget(scrollArea);
	mainLayout->addLayout(buttonLayout);
	setLayout(mainLayout);

	/* --------------------------------------------- */
	if (closable) {
		connect(closeButton, &QPushButton::clicked, this, [this]() { close(); });
	}
	connect(resetButton, &QPushButton::clicked, this, [this]() { Reset(); });
	connect(configureButton, &QPushButton::clicked, this, &OBSBasicStats::Configure);

	ApplyStatVisibility();

	delete shortcutFilter;
	shortcutFilter = CreateShortcutFilter();
	installEventFilter(shortcutFilter);

	resize(800, 280);

	setWindowTitle(QTStr("Basic.Stats"));
#ifndef __APPLE__
	setWindowIcon(QIcon::fromTheme("obs", QIcon(":/res/images/obs.png")));
#endif

	setWindowModality(Qt::NonModal);
	setAttribute(Qt::WA_DeleteOnClose, true);

	QObject::connect(&timer, &QTimer::timeout, this, &OBSBasicStats::Update);
	timer.setInterval(TIMER_INTERVAL);

	if (isVisible()) {
		timer.start();
	}

	Update();

	QObject::connect(&recTimeLeft, &QTimer::timeout, this, &OBSBasicStats::RecordingTimeLeft);
	recTimeLeft.setInterval(REC_TIME_LEFT_INTERVAL);

	OBSBasic *main = OBSBasic::Get();

	const char *geometry = config_get_string(main->Config(), "Stats", "geometry");
	if (geometry != NULL) {
		QByteArray byteArray = QByteArray::fromBase64(QByteArray(geometry));
		restoreGeometry(byteArray);

		QRect windowGeometry = normalGeometry();
		if (!WindowPositionValid(windowGeometry)) {
			QRect rect = QGuiApplication::primaryScreen()->geometry();
			setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size(), rect));
		}
	}

	obs_frontend_add_event_callback(OBSFrontendEvent, this);

	if (obs_frontend_recording_active()) {
		StartRecTimeLeft();
	}
}

void OBSBasicStats::closeEvent(QCloseEvent *event)
{
	OBSBasic *main = OBSBasic::Get();
	if (isVisible()) {
		config_set_string(main->Config(), "Stats", "geometry", saveGeometry().toBase64().constData());
		config_save_safe(main->Config(), "tmp", nullptr);
	}

	// This code is only reached when the non-dockable stats window is
	// manually closed or OBS is exiting.
	obs_frontend_remove_event_callback(OBSFrontendEvent, this);

	QWidget::closeEvent(event);
}

OBSBasicStats::~OBSBasicStats()
{
	delete shortcutFilter;
	os_cpu_usage_info_destroy(cpu_info);
}

void OBSBasicStats::AddOutputLabels(QString name)
{
	OutputLabels ol;
	ol.name = new QLabel(name, this);
	ol.status = new QLabel(this);
	ol.droppedFrames = new QLabel(this);
	ol.megabytesSent = new QLabel(this);
	ol.bitrate = new QLabel(this);

	int col = 0;
	int row = outputLabels.size() + 1;
	outputLayout->addWidget(ol.name, row, col++);
	outputLayout->addWidget(ol.status, row, col++);
	outputLayout->addWidget(ol.droppedFrames, row, col++);
	outputLayout->addWidget(ol.megabytesSent, row, col++);
	outputLayout->addWidget(ol.bitrate, row, col++);
	outputLabels.push_back(ol);
}

static uint32_t first_encoded = 0xFFFFFFFF;
static uint32_t first_skipped = 0xFFFFFFFF;
static uint32_t first_rendered = 0xFFFFFFFF;
static uint32_t first_lagged = 0xFFFFFFFF;

void OBSBasicStats::InitializeValues()
{
	video_t *video = obs_get_video();
	first_encoded = video_output_get_total_frames(video);
	first_skipped = video_output_get_skipped_frames(video);
	first_rendered = obs_get_total_frames();
	first_lagged = obs_get_lagged_frames();
}

void OBSBasicStats::Update()
{
	OBSBasic *main = OBSBasic::Get();

	/* TODO: Un-hardcode */

	struct obs_video_info ovi = {};
	obs_get_video_info(&ovi);

	OBSOutputAutoRelease strOutput = obs_frontend_get_streaming_output();
	OBSOutputAutoRelease recOutput = obs_frontend_get_recording_output();

	if (!strOutput && !recOutput) {
		return;
	}

	/* ------------------------------------------- */
	/* general usage                               */

	double curFPS = obs_get_active_fps();
	double obsFPS = (double)ovi.fps_num / (double)ovi.fps_den;

	QString str = QString::number(curFPS, 'f', 2);
	fps->setText(str);

	if (curFPS < (obsFPS * 0.8)) {
		setClasses(fps, "text-danger");
	} else if (curFPS < (obsFPS * 0.95)) {
		setClasses(fps, "text-warning");
	} else {
		setClasses(fps, "");
	}

	/* ------------------ */

	double usage = os_cpu_usage_info_query(cpu_info);
	str = QString::number(usage, 'g', 2) + QStringLiteral("%");
	cpuUsage->setText(str);

	/* ------------------ */

	const char *path = main->GetCurrentOutputPath();

#define MBYTE (1024ULL * 1024ULL)
#define GBYTE (1024ULL * 1024ULL * 1024ULL)
#define TBYTE (1024ULL * 1024ULL * 1024ULL * 1024ULL)
	num_bytes = os_get_free_disk_space(path);
	QString abrv = QStringLiteral(" MB");
	long double num;

	num = (long double)num_bytes / (1024.0l * 1024.0l);
	if (num_bytes > TBYTE) {
		num /= 1024.0l * 1024.0l;
		abrv = QStringLiteral(" TB");
	} else if (num_bytes > GBYTE) {
		num /= 1024.0l;
		abrv = QStringLiteral(" GB");
	}

	str = QString::number(num, 'f', 1) + abrv;
	hddSpace->setText(str);

	if (num_bytes < GBYTE) {
		setClasses(hddSpace, "text-danger");
	} else if (num_bytes < (5 * GBYTE)) {
		setClasses(hddSpace, "text-warning");
	} else {
		setClasses(hddSpace, "");
	}

	/* ------------------ */

	num = (long double)os_get_proc_resident_size() / (1024.0l * 1024.0l);

	str = QString::number(num, 'f', 1) + QStringLiteral(" MB");
	memUsage->setText(str);

	/* ------------------ */
	/* GPU stats via NVML (NVIDIA only). Missing = "—". */

	NVMLClient &n = nvml();
	if (n.available()) {
		unsigned int gpuPct = 0;
		unsigned long long vramUsed = 0, vramTotal = 0;
		unsigned int tempC = 0;

		if (n.sampleUtilization(gpuPct)) {
			gpuUsage->setText(QString::number(gpuPct) + QStringLiteral("%"));
		} else {
			gpuUsage->setText(QStringLiteral("—"));
		}
		setClasses(gpuUsage, "");

		if (n.sampleMemory(vramUsed, vramTotal) && vramTotal > 0) {
			double usedGB = (double)vramUsed / (1024.0 * 1024.0 * 1024.0);
			double totalGB = (double)vramTotal / (1024.0 * 1024.0 * 1024.0);
			double ratio = (double)vramUsed / (double)vramTotal;
			vramUsage->setText(
				QString("%1 / %2 GB")
					.arg(QString::number(usedGB, 'f', 1), QString::number(totalGB, 'f', 1)));
			if (ratio > 0.95) {
				setClasses(vramUsage, "text-danger");
			} else if (ratio > 0.85) {
				setClasses(vramUsage, "text-warning");
			} else {
				setClasses(vramUsage, "");
			}
		} else {
			vramUsage->setText(QStringLiteral("—"));
			setClasses(vramUsage, "");
		}

		if (n.sampleTemperature(tempC)) {
			gpuTemp->setText(QString::number(tempC) + QStringLiteral(" °C"));
			if (tempC > 85) {
				setClasses(gpuTemp, "text-danger");
			} else if (tempC > 80) {
				setClasses(gpuTemp, "text-warning");
			} else {
				setClasses(gpuTemp, "");
			}
		} else {
			gpuTemp->setText(QStringLiteral("—"));
			setClasses(gpuTemp, "");
		}
	} else {
		gpuUsage->setText(QStringLiteral("—"));
		vramUsage->setText(QStringLiteral("—"));
		gpuTemp->setText(QStringLiteral("—"));
	}

	/* ------------------ */

	num = (long double)obs_get_average_frame_time_ns() / 1000000.0l;

	str = QString::number(num, 'f', 1) + QStringLiteral(" ms");
	renderTime->setText(str);

	long double fpsFrameTime = (long double)ovi.fps_den * 1000.0l / (long double)ovi.fps_num;

	if (num > fpsFrameTime) {
		setClasses(renderTime, "text-danger");
	} else if (num > fpsFrameTime * 0.75l) {
		setClasses(renderTime, "text-warning");
	} else {
		setClasses(renderTime, "");
	}

	/* ------------------ */

	video_t *video = obs_get_video();
	uint32_t total_encoded = video_output_get_total_frames(video);
	uint32_t total_skipped = video_output_get_skipped_frames(video);

	if (total_encoded < first_encoded || total_skipped < first_skipped) {
		first_encoded = total_encoded;
		first_skipped = total_skipped;
	}
	total_encoded -= first_encoded;
	total_skipped -= first_skipped;

	num = total_encoded ? (long double)total_skipped / (long double)total_encoded : 0.0l;
	num *= 100.0l;

	str = QString("%1 / %2 (%3%)")
		      .arg(QString::number(total_skipped), QString::number(total_encoded),
			   QString::number(num, 'f', 1));
	skippedFrames->setText(str);

	if (num > 5.0l) {
		setClasses(skippedFrames, "text-danger");
	} else if (num > 1.0l) {
		setClasses(skippedFrames, "text-warning");
	} else {
		setClasses(skippedFrames, "");
	}

	/* ------------------ */

	uint32_t total_rendered = obs_get_total_frames();
	uint32_t total_lagged = obs_get_lagged_frames();

	if (total_rendered < first_rendered || total_lagged < first_lagged) {
		first_rendered = total_rendered;
		first_lagged = total_lagged;
	}
	total_rendered -= first_rendered;
	total_lagged -= first_lagged;

	num = total_rendered ? (long double)total_lagged / (long double)total_rendered : 0.0l;
	num *= 100.0l;

	str = MakeMissedFramesText(total_lagged, total_rendered, num);
	missedFrames->setText(str);

	if (num > 5.0l) {
		setClasses(missedFrames, "text-danger");
	} else if (num > 1.0l) {
		setClasses(missedFrames, "text-warning");
	} else {
		setClasses(missedFrames, "");
	}

	/* ------------------------------------------- */
	/* recording/streaming stats                   */

	outputLabels[0].Update(strOutput, false);
	outputLabels[1].Update(recOutput, true);

	if (obs_output_active(recOutput)) {
		long double kbps = outputLabels[1].kbps;
		bitrates.push_back(kbps);
	}
}

void OBSBasicStats::StartRecTimeLeft()
{
	if (recTimeLeft.isActive()) {
		ResetRecTimeLeft();
	}

	recordTimeLeft->setText(QTStr("Calculating"));
	recTimeLeft.start();
}

void OBSBasicStats::ResetRecTimeLeft()
{
	if (recTimeLeft.isActive()) {
		bitrates.clear();
		recTimeLeft.stop();
		recordTimeLeft->setText(QTStr(""));
	}
}

void OBSBasicStats::RecordingTimeLeft()
{
	if (bitrates.empty()) {
		return;
	}

	long double averageBitrate = accumulate(bitrates.begin(), bitrates.end(), 0.0) / (long double)bitrates.size();
	if (averageBitrate == 0) {
		return;
	}

	long double bytesPerSec = (averageBitrate / 8.0l) * 1000.0l;
	long double secondsUntilFull = (long double)num_bytes / bytesPerSec;

	bitrates.clear();

	int totalMinutes = (int)secondsUntilFull / 60;
	int minutes = totalMinutes % 60;
	int hours = totalMinutes / 60;

	QString text = MakeTimeLeftText(hours, minutes);
	recordTimeLeft->setText(text);
	recordTimeLeft->setMinimumWidth(recordTimeLeft->width());
}

void OBSBasicStats::Reset()
{
	timer.start();

	first_encoded = 0xFFFFFFFF;
	first_skipped = 0xFFFFFFFF;
	first_rendered = 0xFFFFFFFF;
	first_lagged = 0xFFFFFFFF;

	OBSOutputAutoRelease strOutput = obs_frontend_get_streaming_output();
	OBSOutputAutoRelease recOutput = obs_frontend_get_recording_output();

	outputLabels[0].Reset(strOutput);
	outputLabels[1].Reset(recOutput);
	Update();
}

void OBSBasicStats::OutputLabels::Update(obs_output_t *output, bool rec)
{
	uint64_t totalBytes = output ? obs_output_get_total_bytes(output) : 0;
	uint64_t curTime = os_gettime_ns();
	uint64_t bytesSent = totalBytes;

	if (bytesSent < lastBytesSent) {
		bytesSent = 0;
	}
	if (bytesSent == 0) {
		lastBytesSent = 0;
	}

	uint64_t bitsBetween = (bytesSent - lastBytesSent) * 8;
	long double timePassed = (long double)(curTime - lastBytesSentTime) / 1000000000.0l;
	kbps = (long double)bitsBetween / timePassed / 1000.0l;

	if (timePassed < 0.01l) {
		kbps = 0.0l;
	}

	QString str = QTStr("Basic.Stats.Status.Inactive");
	QString styling;
	bool active = output ? obs_output_active(output) : false;
	if (rec) {
		if (active) {
			str = QTStr("Basic.Stats.Status.Recording");
		}
	} else {
		if (active) {
			bool reconnecting = output ? obs_output_reconnecting(output) : false;

			if (reconnecting) {
				str = QTStr("Basic.Stats.Status.Reconnecting");
				styling = "text-danger";
			} else {
				str = QTStr("Basic.Stats.Status.Live");
				styling = "text-success";
			}
		}
	}

	status->setText(str);
	setClasses(status, styling);

	long double num = (long double)totalBytes / (1024.0l * 1024.0l);
	const char *unit = "MiB";
	if (num > 1024) {
		num /= 1024;
		unit = "GiB";
	}
	megabytesSent->setText(QString("%1 %2").arg(num, 0, 'f', 1).arg(unit));

	num = kbps;
	unit = "kb/s";
	if (num >= 10'000) {
		num /= 1000;
		unit = "Mb/s";
	}
	bitrate->setText(QString("%1 %2").arg(num, 0, 'f', 0).arg(unit));

	if (!rec) {
		int total = output ? obs_output_get_total_frames(output) : 0;
		int dropped = output ? obs_output_get_frames_dropped(output) : 0;

		if (total < first_total || dropped < first_dropped) {
			first_total = 0;
			first_dropped = 0;
		}

		total -= first_total;
		dropped -= first_dropped;

		num = total ? (long double)dropped / (long double)total * 100.0l : 0.0l;

		str = QString("%1 / %2 (%3%)")
			      .arg(QString::number(dropped), QString::number(total), QString::number(num, 'f', 1));
		droppedFrames->setText(str);

		if (num > 5.0l) {
			setClasses(droppedFrames, "text-danger");
		} else if (num > 1.0l) {
			setClasses(droppedFrames, "text-warning");
		} else {
			setClasses(droppedFrames, "");
		}
	}

	lastBytesSent = bytesSent;
	lastBytesSentTime = curTime;
}

void OBSBasicStats::OutputLabels::Reset(obs_output_t *output)
{
	if (!output) {
		return;
	}

	first_total = obs_output_get_total_frames(output);
	first_dropped = obs_output_get_frames_dropped(output);
}

void OBSBasicStats::showEvent(QShowEvent *)
{
	timer.start(TIMER_INTERVAL);
}

void OBSBasicStats::hideEvent(QHideEvent *)
{
	timer.stop();
}

/* ---------------------------------------------------------------------------
 * Customizable rows — persist per-row show/hide in config under [Stats]/show_*.
 * Default is true (visible) so a fresh config preserves the existing UX.
 * ------------------------------------------------------------------------- */

void OBSBasicStats::RegisterStatRow(QLabel *name, QLabel *value, const QString &configKey, const QString &displayName)
{
	StatRow row;
	row.name = name;
	row.value = value;
	row.configKey = configKey;
	row.displayName = displayName;
	statRows.append(row);
}

void OBSBasicStats::ApplyStatVisibility()
{
	OBSBasic *main = OBSBasic::Get();
	config_t *cfg = main->Config();
	for (const StatRow &row : statRows) {
		QByteArray keyBytes = row.configKey.toUtf8();
		config_set_default_bool(cfg, "Stats", keyBytes.constData(), true);
		bool visible = config_get_bool(cfg, "Stats", keyBytes.constData());
		if (row.name) {
			row.name->setVisible(visible);
		}
		if (row.value) {
			row.value->setVisible(visible);
		}
	}
}

void OBSBasicStats::Configure()
{
	QDialog dlg(this);
	dlg.setWindowTitle(QStringLiteral("Customize Stats"));

	QVBoxLayout *layout = new QVBoxLayout(&dlg);
	QLabel *header = new QLabel(QStringLiteral("Show these rows in the Stats panel:"), &dlg);
	layout->addWidget(header);

	OBSBasic *main = OBSBasic::Get();
	config_t *cfg = main->Config();
	QList<QCheckBox *> boxes;
	boxes.reserve(statRows.size());

	for (const StatRow &row : statRows) {
		QCheckBox *cb = new QCheckBox(row.displayName, &dlg);
		QByteArray keyBytes = row.configKey.toUtf8();
		config_set_default_bool(cfg, "Stats", keyBytes.constData(), true);
		cb->setChecked(config_get_bool(cfg, "Stats", keyBytes.constData()));
		cb->setProperty("configKey", row.configKey);
		layout->addWidget(cb);
		boxes.append(cb);
	}

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	if (dlg.exec() == QDialog::Accepted) {
		for (QCheckBox *cb : boxes) {
			QByteArray keyBytes = cb->property("configKey").toString().toUtf8();
			config_set_bool(cfg, "Stats", keyBytes.constData(), cb->isChecked());
		}
		config_save_safe(cfg, "tmp", nullptr);
		ApplyStatVisibility();
	}
}
