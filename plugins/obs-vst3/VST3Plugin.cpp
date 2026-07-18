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

// on linux Qt must be included before X11 headers
#include <QTimer>

#include <VST3Plugin.h>

#include "VST3EditorWindow.h"
#include "VST3HostApp.h"
#include "VST3Scanner.h"
#include "obs-vst3.h"
#if defined(__linux__)
#include "editor/linux/RunLoopImpl.h"
#endif

#include "public.sdk/source/common/memorystream.h"

#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Steinberg {
const FUID IPlugView::iid(0x5BC32507, 0xD06049EA, 0xA6151B52, 0x2B755B29);
const FUID IPlugViewContentScaleSupport::iid(0x65ED9690, 0x8AC44525, 0x8AADEF7A, 0x72EA703F);
const FUID IPlugFrame::iid(0x367FAF01, 0xAFA94693, 0x8D4DA2A0, 0xED0882A3);
#if defined(__linux__)
const FUID Linux::IRunLoop::iid(0x18C35366, 0x97764F1A, 0x9C5B8385, 0x7A871389);
#endif
} // namespace Steinberg

extern VST3HostApp *g_host_app;
#ifdef __linux__
extern RunLoopImpl *g_run_loop;
extern Display *g_display;
/* Fix for LSP plugins which crash with NVIDIA drivers. Switch LSP UI rendering from OpenGL(GLX) -> Cairo (software). */
static void ensure_lsp_cairo_backend()
{
	setenv("LSP_WS_LIB_GLXSURFACE", "off", 1);
	infovst3plugin("Workaround for LSP plugins on linux; LSP_WS_LIB_GLXSURFACE=off set (forcing Cairo backend)\n");
}
#endif

VST3Plugin::VST3Plugin()
{
	auto *drainTimer = new QTimer(this);
	drainTimer->setInterval(50);
	connect(drainTimer, &QTimer::timeout, this, &VST3Plugin::drainDspToGui);
	drainTimer->start();

	if (!hostContext) {
		hostContext = g_host_app;
	}

	componentContext = new VST3ComponentHolder(this);

#ifdef __linux__
	ensure_lsp_cairo_backend();
	display = g_display;
	XSync(display, False);
	runLoop = g_run_loop;
#endif
};

VST3Plugin::~VST3Plugin()
{
	try {
		if (view) {
			if (window) {
				hideEditor();
				view->removed();
				view->setFrame(nullptr);
			}
		}
	} catch (...) {
		warnvst3plugin("VST3 editor threw while closing");
	}
	view = nullptr;

	if (window) {
		delete window;
		window = nullptr;
	}

	processData.unprepare();
	processData = {};
	processSetup = {};
	processContext = {};

	plugProvider = nullptr;
	module = nullptr;

	inputAudioBusInfos.clear();
	outputAudioBusInfos.clear();
	numInputAudioBuses = 0;
	numOutputAudioBuses = 0;
	inputSpeakerArrangements.clear();
	outputSpeakerArrangements.clear();
	sampleRate = 0;
	maxBlockSize = 0;
	symbolicSampleSize = 0;
	realtime = false;

	path = "";
	name = "";

	if (componentContext) {
		delete componentContext;
	}
}

void VST3Plugin::deactivateComponent() const
{
	if (vstPlug) {
		try {
			vstPlug->setActive(false);
		} catch (...) {
			warnvst3plugin("VST3 component threw while deactivating");
		}
	}
}

bool VST3Plugin::activateComponent() const noexcept
{
	if (!vstPlug) {
		return false;
	}
	try {
		return vstPlug->setActive(true) == kResultTrue;
	} catch (...) {
		warnvst3plugin("VST3 component threw while activating");
		return false;
	}
}

uint32 VST3Plugin::latencySamplesSafe() const noexcept
{
	if (!audioEffect || sampleRate <= 0) {
		return 0;
	}

	try {
		const uint32 reported = audioEffect->getLatencySamples();
		const uint32 maximum = static_cast<uint32>(sampleRate) * 10U;
		if (reported > maximum) {
			warnvst3plugin("VST3 reported an excessive latency of %u samples; capped at %u", reported,
				       maximum);
			return maximum;
		}
		return reported;
	} catch (...) {
		warnvst3plugin("VST3 component threw while reporting latency");
		return 0;
	}
}

bool VST3Plugin::setBusActive(MediaType type, BusDirection direction, int which, bool active) const
{
	return vstPlug && vstPlug->activateBus(type, direction, which, active) == kResultTrue;
}

bool VST3Plugin::scanAudioBuses(SpeakerArrangement arr)
{
	int auxBusCount = 0;
	int mainBusCount = 0;
	numEnabledInputAudioBuses = 0;
	numEnabledOutputAudioBuses = 0;
	mainInputBusNumChannels = 0;
	mainOutputBusNumChannels = 0;
	sidechainNumChannels = 0;
	mainInputBusIndex = 0;
	mainOutputBusIndex = 0;
	auxBusIndex = 0;
	numInputAudioBuses = vstPlug->getBusCount(MediaTypes::kAudio, BusDirections::kInput);
	numOutputAudioBuses = vstPlug->getBusCount(MediaTypes::kAudio, BusDirections::kOutput);
	if (numInputAudioBuses <= 0 || numInputAudioBuses > 64 || numOutputAudioBuses <= 0 ||
	    numOutputAudioBuses > 64) {
		infovst3plugin("Unsupported VST3 bus counts: %d inputs, %d outputs", numInputAudioBuses,
			       numOutputAudioBuses);
		return false;
	}

	infovst3plugin("Input audio buses: %i\n Output audio buses: %i\n", numInputAudioBuses, numOutputAudioBuses);

	inputAudioBusInfos.clear();
	inputSpeakerArrangements.clear();
	outputAudioBusInfos.clear();
	outputSpeakerArrangements.clear();
	inputAudioBusInfos.reserve(numInputAudioBuses);
	outputAudioBusInfos.reserve(numOutputAudioBuses);
	inputSpeakerArrangements.resize(numInputAudioBuses, SpeakerArr::kEmpty);
	outputSpeakerArrangements.resize(numOutputAudioBuses, SpeakerArr::kEmpty);
	// We enable the 1st compatible Main bus and the 1st Aux bus
	for (int i = 0; i < numInputAudioBuses; ++i) {
		BusInfo info = {};
		if (vstPlug->getBusInfo(kAudio, kInput, i, info) != kResultTrue) {
			return false;
		}
		if (info.channelCount < 0 || info.channelCount > MAX_PREPROC_CHANNELS) {
			infovst3plugin("Unsupported channel count on VST3 input bus %d: %d", i, info.channelCount);
			return false;
		}
		inputAudioBusInfos.push_back(info);
		(void)audioEffect->getBusArrangement(kInput, i, inputSpeakerArrangements[i]);
		bool isMain = (info.busType == Steinberg::Vst::BusTypes::kMain);
		// only 1 Main input bus is enabled by obs + 1 Aux (side-channel) Bus if it is available
		if (isMain) {
			if (mainBusCount == 0) {
				if (!setBusActive(kAudio, kInput, i, true)) {
					continue;
				}
				mainInputBusNumChannels = info.channelCount;
				mainInputBusIndex = i;
				inputSpeakerArrangements[i] = arr;
				numEnabledInputAudioBuses++;
				mainBusCount = 1;
			} else {
				(void)setBusActive(kAudio, kInput, i, false);
			}
		} else {
			// The 1st aux bus (sidechain) is enabled only if it is mono or stereo
			if (auxBusCount == 0 && (info.channelCount == 1 || info.channelCount == 2)) {
				if (!setBusActive(kAudio, kInput, i, true)) {
					continue;
				}
				SpeakerArrangement speakerArr = info.channelCount == 1
									? Steinberg::Vst::SpeakerArr::kMono
									: Steinberg::Vst::SpeakerArr::kStereo;
				inputSpeakerArrangements[i] = speakerArr;
				numEnabledInputAudioBuses++;
				sidechainNumChannels = info.channelCount;
				auxBusIndex = i;
				auxBusCount = 1;
			} else {
				(void)setBusActive(kAudio, kInput, i, false);
			}
		}
	}
	// We disable the plugin if it has no Input bus.
	if (mainBusCount == 0) {
		infovst3plugin(
			"No input bus detected ! OBS VST3 Host only supports audio effects VST3 with 1 Main Input Bus (+ 1 Sidechannel Bus).");
		vstPlug->setActive(false);
		return false;
	}
	// Only the 1st Main output bus is enabled
	for (int i = 0; i < numOutputAudioBuses; ++i) {
		BusInfo info = {};
		if (vstPlug->getBusInfo(kAudio, kOutput, i, info) != kResultTrue) {
			return false;
		}
		if (info.channelCount < 0 || info.channelCount > MAX_PREPROC_CHANNELS) {
			infovst3plugin("Unsupported channel count on VST3 output bus %d: %d", i, info.channelCount);
			return false;
		}
		outputAudioBusInfos.push_back(info);
		(void)audioEffect->getBusArrangement(kOutput, i, outputSpeakerArrangements[i]);
		bool isMain = (info.busType == Steinberg::Vst::BusTypes::kMain);
		if (isMain && !numEnabledOutputAudioBuses) {
			if (!setBusActive(kAudio, kOutput, i, true)) {
				continue;
			}
			mainOutputBusIndex = i;
			mainOutputBusNumChannels = info.channelCount;
			outputSpeakerArrangements[i] = arr;
			numEnabledOutputAudioBuses++;
		} else {
			(void)setBusActive(kAudio, kOutput, i, false);
		}
	}
	return numEnabledOutputAudioBuses == 1;
}

bool VST3Plugin::init(const std::string &classId, const std::string &path_, int sample_rate, int max_blocksize,
		      SpeakerArrangement arrangement)
{
	std::string error;
	if (classId.empty() || path_.empty() || sample_rate <= 0 || max_blocksize <= 0 ||
	    arrangement == SpeakerArr::kEmpty) {
		return false;
	}

	path = path_;

	sampleRate = sample_rate;
	maxBlockSize = max_blocksize;
	symbolicSampleSize = kSample32;
	realtime = kRealtime;

	processSetup.processMode = realtime;
	processSetup.symbolicSampleSize = symbolicSampleSize;
	processSetup.sampleRate = sampleRate;
	processSetup.maxSamplesPerBlock = maxBlockSize;

	processContext.state = ProcessContext::kPlaying | ProcessContext::kRecording | ProcessContext::kSystemTimeValid;
	processContext.sampleRate = sampleRate;

	processData.numSamples = 0;
	processData.symbolicSampleSize = symbolicSampleSize;
	processData.processContext = &processContext;

	// module creation
	module = VST3::Hosting::Module::create(path, error);
	if (!module) {
		error = vst3_sanitize_display_text(std::move(error));
		infovst3plugin("%.1024s", error.c_str());
		return false;
	}

	// enable parameter changes
	componentContext->guiToDsp = &guiToDsp;
	inputParameterChanges = std::make_unique<Steinberg::Vst::ParameterChanges>();
	outputParameterChanges = std::make_unique<Steinberg::Vst::ParameterChanges>();

	// get factory & set host context
	VST3::Hosting::PluginFactory factory = module->getFactory();
	factory.setHostContext(hostContext->getFUnknown());

	// use plugProvider to retrieve & setup component, processor & controller
	for (auto &classInfo : factory.classInfos()) {
		if (classInfo.category() == kVstAudioEffectClass && classInfo.ID().toString() == classId) {
			if (classId != classInfo.ID().toString()) {
				continue;
			}
			plugProvider = owned(new OBSPlugProvider(factory, classInfo, false));
			if (plugProvider->setup(hostContext->getFUnknown()) == false) {
				plugProvider = nullptr;
			}
			name = vst3_sanitize_display_text(classInfo.name());
			if (name.empty()) {
				name = "Unnamed VST3";
			}
			break;
		}
	}
	if (!plugProvider) {
		infovst3plugin("No VST3 Audio Module Class with UID %s found. You probably uninstalled the VST3.",
			       classId.c_str());
		return false;
	}

	vstPlug = plugProvider->getComponentPtr(); // IComponent*
	if (!vstPlug) {
		infovst3plugin("No VST3 Component class found.");
		return false;
	}

	editController = plugProvider->getControllerPtr(); // IEditController*
	if (!editController) {
		infovst3plugin("No VST3 EditorController class found.");
		return false;
	} else {
		editController->setComponentHandler(componentContext->getComponentHandler());
	}

	const int32 reportedParamCount = editController ? editController->getParameterCount() : 0;
	const int32 transferCapacity =
		std::clamp<int32>(reportedParamCount > 0 ? reportedParamCount : 256, 1, MAX_HOSTED_PARAMETERS);
	guiToDsp.setMaxParameters(transferCapacity);
	dspToGui.setMaxParameters(transferCapacity);
	inputParameterChanges->setMaxParameters(transferCapacity);
	outputParameterChanges->setMaxParameters(transferCapacity);

	audioEffect = FUnknownPtr<IAudioProcessor>(vstPlug).getInterface();
	if (!audioEffect) {
		infovst3plugin("Failed to get an audio processor from VST3");
		// try to get audioProcessor from EditorController, à la Juce, from badly coded VST3.
		audioEffect = FUnknownPtr<IAudioProcessor>(editController).getInterface();
		if (!audioEffect) {
			return false;
		}
	}
	if (audioEffect->canProcessSampleSize(kSample32) != kResultTrue) {
		infovst3plugin("VST3 does not support 32-bit floating-point audio");
		return false;
	}

	// Getting the audio buses; event buses (for midi) are not scanned.
	if (!scanAudioBuses(arrangement)) {
		if (auto *data = obsVst3Data.load(std::memory_order_acquire)) {
			data->bypass.store(true, std::memory_order_release);
		}
		infovst3plugin("Error during the bus scan.");
		return false;
	}

	tresult res = audioEffect->setBusArrangements(inputSpeakerArrangements.data(), numInputAudioBuses,
						      outputSpeakerArrangements.data(), numOutputAudioBuses);
	if (res != kResultTrue) {
		// We check the speaker arrangements to detect what went wrong.
		SpeakerArrangement speakerArrangement = SpeakerArr::kEmpty;
		if (audioEffect->getBusArrangement(kInput, mainInputBusIndex, speakerArrangement) != kResultTrue ||
		    speakerArrangement != arrangement) {
			infovst3plugin("Failed to set input bus to obs speaker layout.");
			return false;
		}

		if (numEnabledInputAudioBuses == 2) {
			const tresult sidechainResult =
				audioEffect->getBusArrangement(kInput, auxBusIndex, speakerArrangement);
			SpeakerArrangement sideArr = sidechainNumChannels == 1 ? Steinberg::Vst::SpeakerArr::kMono
									       : Steinberg::Vst::SpeakerArr::kStereo;
			if (sidechainResult != kResultTrue || speakerArrangement != sideArr) {
				infovst3plugin("Failed to set side chain bus to desired speaker layout!");
				return false;
			}
		}

		if (audioEffect->getBusArrangement(kOutput, mainOutputBusIndex, speakerArrangement) != kResultTrue ||
		    speakerArrangement != arrangement) {
			infovst3plugin("Failed to set output bus to obs speaker layout.");
			return false;
		}
	}

	auto validateBuses = [this](BusDirection direction, int count) {
		for (int index = 0; index < count; ++index) {
			BusInfo info = {};
			if (vstPlug->getBusInfo(kAudio, direction, index, info) != kResultTrue ||
			    info.channelCount < 0 || info.channelCount > MAX_PREPROC_CHANNELS) {
				return false;
			}
		}
		return true;
	};
	if (!validateBuses(kInput, numInputAudioBuses) || !validateBuses(kOutput, numOutputAudioBuses)) {
		infovst3plugin("VST3 returned an invalid bus layout after arrangement negotiation");
		return false;
	}

	const int expectedMainChannels = SpeakerArr::getChannelCount(arrangement);
	BusInfo mainInputInfo = {};
	BusInfo mainOutputInfo = {};
	if (vstPlug->getBusInfo(kAudio, kInput, mainInputBusIndex, mainInputInfo) != kResultTrue ||
	    vstPlug->getBusInfo(kAudio, kOutput, mainOutputBusIndex, mainOutputInfo) != kResultTrue ||
	    mainInputInfo.channelCount != expectedMainChannels || mainOutputInfo.channelCount != expectedMainChannels) {
		infovst3plugin("VST3 main bus channel count does not match the OBS audio layout");
		return false;
	}
	mainInputBusNumChannels = mainInputInfo.channelCount;
	mainOutputBusNumChannels = mainOutputInfo.channelCount;
	if (numEnabledInputAudioBuses == 2) {
		BusInfo auxInfo = {};
		if (vstPlug->getBusInfo(kAudio, kInput, auxBusIndex, auxInfo) != kResultTrue ||
		    auxInfo.channelCount != sidechainNumChannels) {
			infovst3plugin("VST3 sidechain bus changed to an unsupported layout");
			return false;
		}
	}

	// End of setup stage, activation of VST3
	res = audioEffect->setupProcessing(processSetup);
	if (res == kResultOk) {
		if (!processData.prepare(*vstPlug, maxBlockSize, processSetup.symbolicSampleSize)) {
			infovst3plugin("Failed to allocate VST3 process buffers");
			return false;
		}
		// silence outputs on preparation, safety move
		for (int32 busIdx = 0; busIdx < processData.numOutputs; ++busIdx) {
			auto &bus = processData.outputs[busIdx];

			if (bus.channelBuffers32) {
				for (int32 ch = 0; ch < bus.numChannels; ++ch) {
					std::fill_n(bus.channelBuffers32[ch], maxBlockSize, 0.0f);
				}
			}
		}
	} else {
		infovst3plugin("Failed to setup VST3 processing.");
		return false;
	}

	// this often reports 0 in my tests, which probably means that the VST3 authors didn't really measure the value, lol
	uint32 latency = latencySamplesSafe();
	infovst3plugin("Latency of the plugin is %i samples", latency);

	return true;
}

void VST3Plugin::drainDspToGui()
{
	ParamID id;
	ParamValue value;
	int32 sampleOffset;

	while (dspToGui.getNextChange(id, value, sampleOffset)) {
		if (editController) {
			try {
				editController->setParamNormalized(id, value);
			} catch (...) {
				warnvst3plugin("VST3 controller threw while applying an output parameter change");
			}
		}
	}
}

void VST3Plugin::preprocess()
{
	inputParameterChanges->clearQueue();
	outputParameterChanges->clearQueue();
	processData.inputParameterChanges = inputParameterChanges.get();
	processData.outputParameterChanges = outputParameterChanges.get();
	guiToDsp.transferChangesTo(*inputParameterChanges);

	for (int32 busIndex = 0; busIndex < processData.numOutputs; ++busIndex) {
		auto &bus = processData.outputs[busIndex];
		bus.silenceFlags = 0;
		if (!bus.channelBuffers32) {
			continue;
		}
		for (int32 channel = 0; channel < bus.numChannels; ++channel) {
			if (bus.channelBuffers32[channel]) {
				std::fill_n(bus.channelBuffers32[channel], maxBlockSize, 0.0f);
			}
		}
	}
}

void VST3Plugin::postprocess()
{
	if (!processData.outputParameterChanges || outputParameterChanges->getParameterCount() == 0) {
		return;
	}

	dspToGui.transferChangesFrom(*outputParameterChanges);
}

bool VST3Plugin::setProcessing(bool processing) const noexcept
{
	if (!audioEffect) {
		return false;
	}
	try {
		return audioEffect->setProcessing(processing) == kResultOk;
	} catch (...) {
		warnvst3plugin("VST3 component threw while changing processing state");
		return false;
	}
}

bool VST3Plugin::process(int numSamples)
{
	if (!audioEffect) {
		return false;
	}

	try {
		preprocess();

		if (numSamples > maxBlockSize) {
#ifdef _DEBUG
			infovst3plugin("numSamples > _maxBlockSize");
#endif
			numSamples = maxBlockSize;
		}

		processData.numSamples = numSamples;
		processContext.projectTimeSamples += numSamples;
		processContext.systemTime = static_cast<int64>(os_gettime_ns());

		tresult result = audioEffect->process(processData);

		if (result != kResultOk) {
			return false;
		}

		postprocess();
	} catch (...) {
		return false;
	}

	return true;
}

Steinberg::Vst::Sample32 *VST3Plugin::channelBuffer32(const BusDirection direction, const int ch) const
{
	if (ch < 0) {
		return nullptr;
	}

	const AudioBusBuffers *bus = nullptr;
	if (direction == kInput && processData.inputs && mainInputBusIndex >= 0 &&
	    mainInputBusIndex < processData.numInputs) {
		bus = &processData.inputs[mainInputBusIndex];
	} else if (direction == kOutput && processData.outputs && mainOutputBusIndex >= 0 &&
		   mainOutputBusIndex < processData.numOutputs) {
		bus = &processData.outputs[mainOutputBusIndex];
	}

	if (!bus || ch >= bus->numChannels || !bus->channelBuffers32) {
		return nullptr;
	}
	return bus->channelBuffers32[ch];
}

Steinberg::Vst::Sample32 *VST3Plugin::auxChannelBuffer32(const BusDirection direction, const int ch) const
{
	if (direction == kInput) {
		if (ch < 0 || !processData.inputs || auxBusIndex < 0 || auxBusIndex >= processData.numInputs) {
			return nullptr;
		}
		const auto &bus = processData.inputs[auxBusIndex];
		if (ch >= bus.numChannels || !bus.channelBuffers32) {
			return nullptr;
		}
		return bus.channelBuffers32[ch];
	}
	return nullptr;
}

/* hack ripped from Juce, to create the view even with badly coded VST3s... */
void VST3Plugin::tryCreatingView()
{
	if (auto *raw = editController->createView(Vst::ViewType::kEditor)) {
		view = IPtr<IPlugView>::adopt(raw);
		return;
	}

	if (auto *raw = editController->createView(nullptr)) {
		view = IPtr<IPlugView>::adopt(raw);
		return;
	}

	IPlugView *raw = nullptr;
	if (editController->queryInterface(IPlugView::iid, reinterpret_cast<void **>(&raw)) == kResultOk) {
		view = IPtr<IPlugView>::adopt(raw);
	}
}

bool VST3Plugin::createView()
{
	if (!editController) {
		infovst3plugin("VST3 does not provide an edit controller");
		return false;
	}

	if (view) {
		debugvst3plugin("Editor view or window already exists");
		return false;
	} else {
		tryCreatingView();
	}

	if (!view) {
		infovst3plugin("EditController does not provide its own view");
		return false;
	}

#ifdef _WIN32
	if (view->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) != Steinberg::kResultTrue) {
		infovst3plugin("Editor view does not support HWND");
		return false;
	}
#elif defined(__APPLE__)
	if (view->isPlatformTypeSupported(Steinberg::kPlatformTypeNSView) != Steinberg::kResultTrue) {
		infovst3plugin("Editor view does not support NSView");
		return false;
	}
#elif defined(__linux__)
	if (view->isPlatformTypeSupported(Steinberg::kPlatformTypeX11EmbedWindowID) != Steinberg::kResultTrue) {
		infovst3plugin("Editor view does not support X11");
		return false;
	}
#else
	infovst3plugin("Platform is not supported yet");
	return false;
#endif

	return true;
}

void VST3Plugin::showEditor()
{
	if (!view) {
		return;
	}

	if (!window) {
		int width = 800, height = 600;
		if (view) {
			Steinberg::ViewRect rect;
			if (view->getSize(&rect) == Steinberg::kResultOk) {
				width = rect.getWidth();
				height = rect.getHeight();
			} else {
				infovst3plugin("Failed to get size before attaching an IFrame. Not SDK compliant.");
			}
		}
		auto *data = obsVst3Data.load(std::memory_order_acquire);
		if (!data) {
			return;
		}
		const char *sourceNameText = obs_source_get_name(data->context);
		std::string sourceName = sourceNameText ? sourceNameText : "OBS Audio Source";
		std::string windowName = sourceName + ": VST3 Plugin - " + name;
#ifdef __linux__
		auto candidate = std::make_unique<VST3EditorWindow>(view, windowName, display, runLoop);
#else
		auto candidate = std::make_unique<VST3EditorWindow>(view, windowName);
#endif
		if (candidate->create(width, height)) {
			candidate->show();
			window = candidate.release();
			editorVisible = true;
		} else {
			infovst3plugin("Failed to create editor window");
		}
	} else {
		window->show();
		editorVisible = true;
	}
}

void VST3Plugin::hideEditor()
{
	if (window && view) {
		window->close();
	}
	editorVisible = false;
}

// This function is required because we don't really close the GUI window; we hide it on Windows & macOS.
// This then means we have to track when a GUI has been closed by the user when clicking X. I decided to just hide because
// creating the GUI each time the user wants to display it, was prone to crashes. This also simplified the coding.
bool VST3Plugin::isEditorVisible()
{
	if (window) {
		bool wasClosed = window->getClosedState();
		if (wasClosed && editorVisible) {
			editorVisible = false;
		}
	}
	return editorVisible;
}

bool VST3Plugin::saveStates(std::vector<uint8_t> &compOut, std::vector<uint8_t> &ctrlOut) const
{
	compOut.clear();
	ctrlOut.clear();

	if (!vstPlug) {
		return false;
	}

	auto copyStream = [](Steinberg::MemoryStream &stream, std::vector<uint8_t> &destination, bool requireContent) {
		const Steinberg::int64 size = stream.getSize();
		if (size < 0 || size > MAX_VST3_STATE_BYTES || (requireContent && size == 0)) {
			return false;
		}
		if (size == 0) {
			destination.clear();
			return true;
		}

		Steinberg::int64 seekResult = 0;
		if (stream.seek(0, Steinberg::IBStream::kIBSeekSet, &seekResult) != Steinberg::kResultTrue ||
		    seekResult != 0) {
			return false;
		}
		destination.resize(static_cast<size_t>(size));
		Steinberg::int32 bytesRead = 0;
		if (stream.read(destination.data(), static_cast<Steinberg::int32>(size), &bytesRead) !=
			    Steinberg::kResultTrue ||
		    bytesRead != static_cast<Steinberg::int32>(size)) {
			destination.clear();
			return false;
		}
		return true;
	};

	try {
		Steinberg::MemoryStream componentStream;
		if (vstPlug->getState(&componentStream) != Steinberg::kResultOk ||
		    !copyStream(componentStream, compOut, true)) {
			return false;
		}

		if (editController) {
			Steinberg::MemoryStream controllerStream;
			if (editController->getState(&controllerStream) == Steinberg::kResultOk &&
			    !copyStream(controllerStream, ctrlOut, false)) {
				compOut.clear();
				return false;
			}
		}
		return true;
	} catch (...) {
		compOut.clear();
		ctrlOut.clear();
		warnvst3plugin("VST3 component threw while saving state");
		return false;
	}
}

bool VST3Plugin::loadStates(const std::vector<uint8_t> &comp, const std::vector<uint8_t> &ctrl)
{
	if (!vstPlug || comp.empty() || comp.size() > MAX_VST3_STATE_BYTES || ctrl.size() > MAX_VST3_STATE_BYTES) {
		return false;
	}

	auto fillStream = [](const std::vector<uint8_t> &source, Steinberg::MemoryStream &stream) {
		Steinberg::int32 bytesWritten = 0;
		if (stream.write(const_cast<uint8_t *>(source.data()), static_cast<Steinberg::int32>(source.size()),
				 &bytesWritten) != Steinberg::kResultTrue ||
		    bytesWritten != static_cast<Steinberg::int32>(source.size())) {
			return false;
		}
		Steinberg::int64 seekResult = 0;
		return stream.seek(0, Steinberg::IBStream::kIBSeekSet, &seekResult) == Steinberg::kResultTrue &&
		       seekResult == 0;
	};

	try {
		Steinberg::MemoryStream componentStream;
		if (!fillStream(comp, componentStream) || vstPlug->setState(&componentStream) != Steinberg::kResultOk) {
			return false;
		}

		if (editController) {
			Steinberg::int64 seekResult = 0;
			if (componentStream.seek(0, Steinberg::IBStream::kIBSeekSet, &seekResult) !=
				    Steinberg::kResultTrue ||
			    seekResult != 0) {
				return false;
			}
			(void)editController->setComponentState(&componentStream);

			if (!ctrl.empty()) {
				Steinberg::MemoryStream controllerStream;
				if (!fillStream(ctrl, controllerStream) ||
				    editController->setState(&controllerStream) != Steinberg::kResultOk) {
					return false;
				}
			}
		}
		return true;
	} catch (...) {
		warnvst3plugin("VST3 component threw while loading state");
		return false;
	}
}
