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

#include "VST3ComponentHolder.h"

#include "VST3Plugin.h"
#include "obs-vst3.h"

#include <QMetaObject>

#include <util/platform.h>

#include <algorithm>

using namespace Steinberg;
using namespace Vst;

namespace {
constexpr uint64_t RESTART_WAIT_NS = 2ULL * 1000000000ULL;

bool waitForAudioIdle(vst3_audio_data *data)
{
	const uint64_t deadline = os_gettime_ns() + RESTART_WAIT_NS;
	while (data->active_audio_calls.load(std::memory_order_acquire) != 0) {
		if (os_gettime_ns() >= deadline) {
			return false;
		}
		os_sleep_ms(1);
	}
	return true;
}
} // namespace

VST3ComponentHolder::VST3ComponentHolder(VST3Plugin *plugin_) : plugin(plugin_) {}

VST3ComponentHolder::~VST3ComponentHolder() noexcept {FUNKNOWN_DTOR}

tresult PLUGIN_API VST3ComponentHolder::beginEdit(ParamID)
{
	return kResultOk;
}

tresult PLUGIN_API VST3ComponentHolder::performEdit(ParamID id, ParamValue valueNormalized)
{
	if (guiToDsp) {
		guiToDsp->addChange(id, valueNormalized, 0);
	}

	return kResultOk;
}

tresult PLUGIN_API VST3ComponentHolder::endEdit(ParamID)
{
	return kResultOk;
}

tresult PLUGIN_API VST3ComponentHolder::restartComponent(int32 flags)
{
	if (!plugin) {
		return kInvalidArgument;
	}

	const bool reloadComponent = (flags & kReloadComponent) != 0;
	const bool latencyChanged = (flags & kLatencyChanged) != 0;
	if (reloadComponent || latencyChanged) {
		auto *plugin_data = plugin->obsVst3Data.load(std::memory_order_acquire);
		if (!plugin_data || plugin->restartScheduled.exchange(true, std::memory_order_acq_rel)) {
			return kResultOk;
		}
		if (reloadComponent) {
			plugin_data->bypass.store(true, std::memory_order_release);
		}
		const bool queued = QMetaObject::invokeMethod(
			plugin,
			[plugin = plugin, reloadComponent] {
				struct ClearScheduled {
					std::atomic<bool> &flag;
					~ClearScheduled() { flag.store(false, std::memory_order_release); }
				} clear{plugin->restartScheduled};

				auto *data = plugin->obsVst3Data.load(std::memory_order_acquire);
				if (!data) {
					return;
				}

				bool restartSucceeded = true;
				if (reloadComponent) {
					if (!waitForAudioIdle(data)) {
						warnvst3plugin(
							"Timed out waiting for VST3 processing to stop during restart");
						restartSucceeded = false;
					} else {
						try {
							if (plugin->audioEffect &&
							    plugin->audioEffect->setProcessing(false) != kResultOk) {
								restartSucceeded = false;
							}
							if (plugin->vstPlug &&
							    plugin->vstPlug->setActive(false) != kResultTrue) {
								restartSucceeded = false;
							}
							if (restartSucceeded && plugin->vstPlug &&
							    plugin->vstPlug->setActive(true) != kResultTrue) {
								restartSucceeded = false;
							}
							if (restartSucceeded && plugin->audioEffect &&
							    plugin->audioEffect->setProcessing(true) != kResultOk) {
								restartSucceeded = false;
							}
						} catch (...) {
							warnvst3plugin("VST3 component threw while restarting");
							restartSucceeded = false;
						}
					}
				}

				uint32 latency = plugin->latencySamplesSafe();
				infovst3plugin("Latency of the plugin is %u samples", latency);
				if (plugin->obsVst3Data.load(std::memory_order_acquire) == data) {
					const uint64_t plugin_latency =
						((uint64_t)latency * 1000000000ULL) / data->sample_rate;
					data->latency.store(data->buffer_latency + plugin_latency,
							    std::memory_order_release);
					if (reloadComponent) {
						data->process_failed.store(!restartSucceeded,
									   std::memory_order_release);
						data->bypass.store(!restartSucceeded, std::memory_order_release);
					}
				}
			},
			Qt::QueuedConnection);
		if (!queued) {
			plugin->restartScheduled.store(false, std::memory_order_release);
			if (reloadComponent && plugin->obsVst3Data.load(std::memory_order_acquire) == plugin_data) {
				plugin_data->bypass.store(false, std::memory_order_release);
			}
			return kResultFalse;
		}
		return kResultTrue;
	}
	if ((flags & kParamTitlesChanged) || (flags & kParamValuesChanged)) {
		if (!plugin->editController ||
		    plugin->parameterRefreshScheduled.exchange(true, std::memory_order_acq_rel)) {
			return kResultTrue;
		}

		const bool queued = QMetaObject::invokeMethod(
			plugin,
			[plugin = plugin] {
				struct ClearScheduled {
					std::atomic<bool> &flag;
					~ClearScheduled() { flag.store(false, std::memory_order_release); }
				} clear{plugin->parameterRefreshScheduled};

				try {
					const int32 count =
						std::clamp<int32>(plugin->editController->getParameterCount(), 0,
								  VST3Plugin::MAX_HOSTED_PARAMETERS);
					for (int32 i = 0; i < count; ++i) {
						Steinberg::Vst::ParameterInfo info{};
						if (plugin->editController->getParameterInfo(i, info) == kResultOk) {
							const ParamValue value =
								plugin->editController->getParamNormalized(info.id);
							plugin->guiToDsp.addChange(info.id, value, 0);
						}
					}
				} catch (...) {
					warnvst3plugin("VST3 controller threw while refreshing parameters");
				}
			},
			Qt::QueuedConnection);
		if (!queued) {
			plugin->parameterRefreshScheduled.store(false, std::memory_order_release);
			return kResultFalse;
		}
		return kResultTrue;
	}

	return kNotImplemented;
}

tresult PLUGIN_API VST3ComponentHolder::notifyUnitSelection(UnitID)
{
	return kResultTrue;
}

tresult PLUGIN_API VST3ComponentHolder::notifyProgramListChange(ProgramListID, int32)
{
	return kResultTrue;
}

tresult PLUGIN_API VST3ComponentHolder::queryInterface(const TUID _iid, void **obj)
{
	if (!_iid || !obj) {
		return kInvalidArgument;
	}
	*obj = nullptr;
	if (FUnknownPrivate::iidEqual(_iid, IComponentHandler::iid)) {
		*obj = static_cast<IComponentHandler *>(this);
		return kResultOk;
	}
	if (FUnknownPrivate::iidEqual(_iid, IUnitHandler::iid)) {
		*obj = static_cast<IUnitHandler *>(this);
		return kResultOk;
	}
	return kNoInterface;
}
