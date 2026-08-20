#include "ProgramAudioStrip.hpp"

#include <api/obs-frontend-api.h>
#include <components/VolumeMeter.hpp>
#include <widgets/OBSBasic.hpp>

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
constexpr int kUpdateIntervalMs = 34;
constexpr uint32_t kMaxCallbackFrames = AUDIO_OUTPUT_FRAMES;
constexpr size_t kMomentarySegments = 4;
constexpr size_t kShortTermSegments = 30;
constexpr double kLoudnessOffset = -0.691;
constexpr double kAbsoluteGateLufs = -70.0;
constexpr double kGateBinWidth = 0.1;
constexpr size_t kGateBinCount = 1000;

struct BiquadCoefficients {
	double b0;
	double b1;
	double b2;
	double a1;
	double a2;
};

constexpr BiquadCoefficients kShelf48{1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241,
				      0.73248077421585};
constexpr BiquadCoefficients kHighPass48{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};
constexpr BiquadCoefficients kShelf441{1.530841230050347, -2.650979995154729, 1.169079079921586, -1.66365511325602,
				       0.712595428073225};
constexpr BiquadCoefficients kHighPass441{1.0, -2.0, 1.0, -1.989169673629795, 0.989199035787039};

constexpr double kTruePeakFir[12][4] = {
	{0.0017089843750, -0.0291748046875, -0.0189208984375, -0.0083007812500},
	{0.0109863281250, 0.0292968750000, 0.0330810546875, 0.0148925781250},
	{-0.0196533203125, -0.0517578125000, -0.0582275390625, -0.0266113281250},
	{0.0332031250000, 0.0891113281250, 0.1015625000000, 0.0476074218750},
	{-0.0594482421875, -0.1665039062500, -0.2003173828125, -0.1022949218750},
	{0.1373291015625, 0.4650878906250, 0.7797851562500, 0.9721679687500},
	{0.9721679687500, 0.7797851562500, 0.4650878906250, 0.1373291015625},
	{-0.1022949218750, -0.2003173828125, -0.1665039062500, -0.0594482421875},
	{0.0476074218750, 0.1015625000000, 0.0891113281250, 0.0332031250000},
	{-0.0266113281250, -0.0582275390625, -0.0517578125000, -0.0196533203125},
	{0.0148925781250, 0.0330810546875, 0.0292968750000, 0.0109863281250},
	{-0.0083007812500, -0.0189208984375, -0.0291748046875, 0.0017089843750},
};

static_assert(std::atomic<uint32_t>::is_always_lock_free,
	      "Program audio callback publishing requires lock-free 32-bit atomics");
static_assert(sizeof(float) == sizeof(uint32_t) && std::numeric_limits<float>::is_iec559,
	      "Program audio peak latches require 32-bit IEEE-754 floats");

size_t configuredStreamMix()
{
	OBSBasic *main = OBSBasic::Get();
	if (!main)
		return 0;

	config_t *config = main->Config();
	const char *outputMode = config_get_string(config, "Output", "Mode");
	if (!outputMode || strcmp(outputMode, "Advanced") != 0)
		return 0;

	const int64_t track = std::clamp<int64_t>(config_get_int(config, "AdvOut", "TrackIndex"), 1, MAX_AUDIO_MIXES);
	return static_cast<size_t>(track - 1);
}

void repolish(QWidget *widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}
} // namespace

struct ProgramAudioStrip::MeasurementState {
	struct Biquad {
		BiquadCoefficients coefficients{};
		double x1{0.0};
		double x2{0.0};
		double y1{0.0};
		double y2{0.0};

		double process(double sample) noexcept
		{
			double output = coefficients.b0 * sample + coefficients.b1 * x1 + coefficients.b2 * x2 -
					coefficients.a1 * y1 - coefficients.a2 * y2;
			if (!std::isfinite(output)) {
				reset();
				return 0.0;
			}
			x2 = x1;
			x1 = sample;
			y2 = y1;
			y1 = std::fabs(output) < 1.0e-30 ? 0.0 : output;
			return y1;
		}

		void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }
	};

	struct ChannelState {
		Biquad shelf;
		Biquad highPass;
		std::array<double, 12> truePeakHistory{};
		size_t truePeakPosition{0};

		void reset() noexcept
		{
			shelf.reset();
			highPass.reset();
			truePeakHistory.fill(0.0);
			truePeakPosition = 0;
		}
	};

	std::array<ChannelState, MAX_AUDIO_CHANNELS> channelState{};
	std::array<double, kShortTermSegments> segmentPowers{};
	std::array<uint64_t, kGateBinCount> gateCounts{};
	std::array<double, kGateBinCount> gateEnergy{};
	std::atomic<uint32_t> momentaryBits{};
	std::atomic<uint32_t> shortTermBits{};
	std::atomic<uint32_t> integratedBits{};
	std::atomic<uint32_t> truePeakBits{};
	std::atomic<bool> resetRequested{false};

	uint32_t sampleRate{0};
	speaker_layout speakers{SPEAKERS_UNKNOWN};
	uint32_t channelCount{0};
	uint32_t segmentFrameTarget{0};
	uint32_t segmentFrameCount{0};
	double segmentEnergy{0.0};
	size_t segmentWrite{0};
	size_t segmentAvailable{0};
	bool loudnessSupported{false};

	MeasurementState() { resetOutputs(); }

	void configure(uint32_t newSampleRate, speaker_layout newSpeakers, uint32_t newChannels)
	{
		sampleRate = newSampleRate;
		speakers = newSpeakers;
		channelCount = std::min(newChannels, static_cast<uint32_t>(MAX_AUDIO_CHANNELS));
		segmentFrameTarget = sampleRate / 10;
		loudnessSupported = sampleRate == 48000 || sampleRate == 44100;

		const BiquadCoefficients shelf = sampleRate == 44100 ? kShelf441 : kShelf48;
		const BiquadCoefficients highPass = sampleRate == 44100 ? kHighPass441 : kHighPass48;
		for (ChannelState &channel : channelState) {
			channel.shelf.coefficients = shelf;
			channel.highPass.coefficients = highPass;
		}
		reset();
	}

	void requestReset() noexcept { resetRequested.store(true, std::memory_order_release); }

	void applyRequestedReset() noexcept
	{
		if (resetRequested.exchange(false, std::memory_order_acq_rel))
			reset();
	}

	double processLoudness(uint32_t channel, float sample) noexcept
	{
		if (!loudnessSupported)
			return 0.0;

		const double weight = channelWeight(channel);
		if (weight == 0.0)
			return 0.0;

		ChannelState &state = channelState[channel];
		const double filtered = state.highPass.process(state.shelf.process(sample));
		return weight * filtered * filtered;
	}

	float processTruePeak(uint32_t channel, float sample) noexcept
	{
		ChannelState &state = channelState[channel];
		state.truePeakHistory[state.truePeakPosition] = sample;

		double maximum = 0.0;
		for (size_t phase = 0; phase < 4; ++phase) {
			double reconstructed = 0.0;
			for (size_t tap = 0; tap < 12; ++tap) {
				const size_t index = (state.truePeakPosition + 12 - tap) % 12;
				reconstructed += kTruePeakFir[tap][phase] * state.truePeakHistory[index];
			}
			maximum = std::max(maximum, std::fabs(reconstructed));
		}

		state.truePeakPosition = (state.truePeakPosition + 1) % 12;
		return static_cast<float>(maximum);
	}

	void addProgramPower(double power) noexcept
	{
		if (!loudnessSupported || segmentFrameTarget == 0)
			return;

		segmentEnergy += power;
		if (++segmentFrameCount < segmentFrameTarget)
			return;

		segmentPowers[segmentWrite] = segmentEnergy / segmentFrameCount;
		segmentWrite = (segmentWrite + 1) % segmentPowers.size();
		segmentAvailable = std::min(segmentAvailable + 1, segmentPowers.size());
		segmentFrameCount = 0;
		segmentEnergy = 0.0;
		publishLoudness();
	}

	void publishTruePeak(float peak) noexcept { ProgramAudioStrip::publishPeakMaximum(truePeakBits, peak); }

private:
	void resetOutputs() noexcept
	{
		const uint32_t unavailable = ProgramAudioStrip::floatToBits(-std::numeric_limits<float>::infinity());
		momentaryBits.store(unavailable, std::memory_order_relaxed);
		shortTermBits.store(unavailable, std::memory_order_relaxed);
		integratedBits.store(unavailable, std::memory_order_relaxed);
		truePeakBits.store(ProgramAudioStrip::floatToBits(0.0f), std::memory_order_relaxed);
	}

	void reset() noexcept
	{
		for (ChannelState &channel : channelState)
			channel.reset();
		segmentPowers.fill(0.0);
		gateCounts.fill(0);
		gateEnergy.fill(0.0);
		segmentFrameCount = 0;
		segmentEnergy = 0.0;
		segmentWrite = 0;
		segmentAvailable = 0;
		resetOutputs();
	}

	double channelWeight(uint32_t channel) const noexcept
	{
		if ((speakers == SPEAKERS_2POINT1 && channel == 2) ||
		    ((speakers == SPEAKERS_4POINT1 || speakers == SPEAKERS_5POINT1 || speakers == SPEAKERS_7POINT1) &&
		     channel == 3)) {
			return 0.0;
		}

		switch (speakers) {
		case SPEAKERS_4POINT0:
			return channel == 3 ? 1.41 : 1.0;
		case SPEAKERS_4POINT1:
			return channel == 4 ? 1.41 : 1.0;
		case SPEAKERS_5POINT1:
			return channel >= 4 ? 1.41 : 1.0;
		case SPEAKERS_7POINT1:
			return channel >= 6 ? 1.41 : 1.0;
		default:
			return 1.0;
		}
	}

	double windowPower(size_t segments) const noexcept
	{
		double sum = 0.0;
		for (size_t offset = 0; offset < segments; ++offset) {
			const size_t index = (segmentWrite + segmentPowers.size() - 1 - offset) % segmentPowers.size();
			sum += segmentPowers[index];
		}
		return sum / segments;
	}

	static float loudnessFromPower(double power) noexcept
	{
		return power > 0.0 ? static_cast<float>(kLoudnessOffset + 10.0 * std::log10(power))
				   : -std::numeric_limits<float>::infinity();
	}

	void publishLoudness() noexcept
	{
		if (segmentAvailable >= kMomentarySegments) {
			const double blockPower = windowPower(kMomentarySegments);
			const float momentary = loudnessFromPower(blockPower);
			momentaryBits.store(ProgramAudioStrip::floatToBits(momentary), std::memory_order_release);

			if (momentary >= kAbsoluteGateLufs) {
				const double position = (momentary - kAbsoluteGateLufs) / kGateBinWidth;
				const size_t bin = std::min(static_cast<size_t>(position), kGateBinCount - 1);
				++gateCounts[bin];
				gateEnergy[bin] += blockPower;
			}
		}

		if (segmentAvailable >= kShortTermSegments) {
			const float shortTerm = loudnessFromPower(windowPower(kShortTermSegments));
			shortTermBits.store(ProgramAudioStrip::floatToBits(shortTerm), std::memory_order_release);
		}

		uint64_t absoluteCount = 0;
		double absoluteEnergy = 0.0;
		for (size_t bin = 0; bin < kGateBinCount; ++bin) {
			absoluteCount += gateCounts[bin];
			absoluteEnergy += gateEnergy[bin];
		}
		if (absoluteCount == 0)
			return;

		const float absoluteLoudness = loudnessFromPower(absoluteEnergy / absoluteCount);
		const double relativeGate = absoluteLoudness - 10.0;
		uint64_t gatedCount = 0;
		double gatedEnergy = 0.0;
		for (size_t bin = 0; bin < kGateBinCount; ++bin) {
			const double binCenter = kAbsoluteGateLufs + (static_cast<double>(bin) + 0.5) * kGateBinWidth;
			if (binCenter <= relativeGate)
				continue;
			gatedCount += gateCounts[bin];
			gatedEnergy += gateEnergy[bin];
		}
		if (gatedCount == 0)
			return;

		const float integrated = loudnessFromPower(gatedEnergy / gatedCount);
		integratedBits.store(ProgramAudioStrip::floatToBits(integrated), std::memory_order_release);
	}
};

ProgramAudioStrip::ProgramAudioStrip(QWidget *parent)
	: QFrame(parent),
	  measurement(std::make_unique<MeasurementState>())
{
	setObjectName("programAudioStrip");
	setProperty("programState", "idle");
	setProperty("peakState", "idle");
	setFrameShape(QFrame::NoFrame);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setFixedHeight(88);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 4, 8, 4);
	layout->setSpacing(3);

	auto *meterRow = new QHBoxLayout;
	meterRow->setContentsMargins(0, 0, 0, 0);
	meterRow->setSpacing(8);
	auto *measurementRow = new QHBoxLayout;
	measurementRow->setContentsMargins(0, 0, 0, 0);
	measurementRow->setSpacing(6);

	auto *titleLabel = new QLabel(QTStr("Basic.AudioMixer.ProgramAudio.Title"), this);
	titleLabel->setObjectName("programAudioTitle");
	titleLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	titleLabel->setFixedWidth(titleLabel->fontMetrics().horizontalAdvance(titleLabel->text()) + 8);

	mixLabel = new QLabel(this);
	mixLabel->setObjectName("programAudioMixLabel");
	mixLabel->setAlignment(Qt::AlignCenter);
	mixLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	const QString widestMix = QTStr("Basic.AudioMixer.ProgramAudio.StreamMix").arg(MAX_AUDIO_MIXES);
	mixLabel->setFixedWidth(mixLabel->fontMetrics().horizontalAdvance(widestMix) + 12);

	meter = new VolumeMeter(this);
	meter->setObjectName("programAudioMeter");
	meter->setProperty("readOnly", true);
	meter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	meter->setMinimumWidth(160);

	statusBadge = new QLabel(this);
	statusBadge->setObjectName("programAudioStatusBadge");
	statusBadge->setProperty("programState", "idle");
	statusBadge->setAlignment(Qt::AlignCenter);
	statusBadge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	const QString widestStatus = QTStr("Basic.AudioMixer.ProgramAudio.Status.LiveRecording");
	statusBadge->setFixedWidth(statusBadge->fontMetrics().horizontalAdvance(widestStatus) + 16);

	auto makeMeasurementLabel = [this](const char *objectName, const char *widestText) {
		auto *label = new QLabel(this);
		label->setObjectName(objectName);
		label->setAlignment(Qt::AlignCenter);
		label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
		label->setFixedWidth(label->fontMetrics().horizontalAdvance(widestText) + 14);
		return label;
	};
	momentaryLabel = makeMeasurementLabel("programAudioMomentaryLabel", "M -999.9 LUFS");
	shortTermLabel = makeMeasurementLabel("programAudioShortTermLabel", "S -999.9 LUFS");
	integratedLabel = makeMeasurementLabel("programAudioIntegratedLabel", "I -999.9 LUFS");
	truePeakLabel = makeMeasurementLabel("programAudioTruePeakLabel", "MAX TP +99.9 dBTP");
	truePeakLabel->setProperty("peakState", "idle");

	resetButton = new QPushButton(QTStr("Basic.AudioMixer.ProgramAudio.Reset"), this);
	resetButton->setObjectName("programAudioResetButton");
	resetButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	resetButton->setFixedWidth(resetButton->fontMetrics().horizontalAdvance(resetButton->text()) + 24);
	connect(resetButton, &QPushButton::clicked, this, &ProgramAudioStrip::requestMeasurementReset);

	meterRow->addWidget(titleLabel);
	meterRow->addWidget(mixLabel);
	meterRow->addWidget(meter, 1);
	meterRow->addWidget(statusBadge);
	measurementRow->addStretch(1);
	measurementRow->addWidget(momentaryLabel);
	measurementRow->addWidget(shortTermLabel);
	measurementRow->addWidget(integratedLabel);
	measurementRow->addWidget(truePeakLabel);
	measurementRow->addWidget(resetButton);
	measurementRow->addStretch(1);
	layout->addLayout(meterRow, 1);
	layout->addLayout(measurementRow);

	updateTimer.setInterval(kUpdateIntervalMs);
	updateTimer.setTimerType(Qt::PreciseTimer);
	connect(&updateTimer, &QTimer::timeout, this, &ProgramAudioStrip::updateUi);

	resetPublishedLevels();
	updateStatus();
}

ProgramAudioStrip::~ProgramAudioStrip()
{
	shutdown();
}

void ProgramAudioStrip::refreshColors()
{
	meter->refreshColors();
}

void ProgramAudioStrip::setMeteringEnabled(bool enabled)
{
	if (shuttingDown || meteringEnabled == enabled)
		return;

	meteringEnabled = enabled;
	if (enabled) {
		refreshConnection();
		updateStatus();
		updateTimer.start();
	} else {
		updateTimer.stop();
		disconnectCallback();
		resetPublishedLevels();
		updateStatus();
	}
}

void ProgramAudioStrip::shutdown()
{
	if (shuttingDown)
		return;

	shuttingDown = true;
	updateTimer.stop();
	disconnectCallback();
}

void ProgramAudioStrip::rawAudioCallback(void *param, size_t, audio_data *data)
{
	auto *strip = static_cast<ProgramAudioStrip *>(param);
	if (!strip || !strip->measurement || !data)
		return;

	const uint32_t channels = std::min(strip->callbackChannels.load(std::memory_order_relaxed),
					   static_cast<uint32_t>(MAX_AUDIO_CHANNELS));
	const uint32_t frames = std::min(data->frames, kMaxCallbackFrames);
	if (channels == 0 || frames == 0)
		return;

	strip->measurement->applyRequestedReset();
	strip->publishSequence.fetch_add(1, std::memory_order_acq_rel);

	std::array<const float *, MAX_AUDIO_CHANNELS> samples{};
	std::array<double, MAX_AUDIO_CHANNELS> sumSquares{};
	std::array<float, MAX_AUDIO_CHANNELS> peaks{};
	float callbackTruePeak = 0.0f;
	for (uint32_t channel = 0; channel < channels; ++channel)
		samples[channel] = reinterpret_cast<const float *>(data->data[channel]);

	for (uint32_t frame = 0; frame < frames; ++frame) {
		double programPower = 0.0;
		for (uint32_t channel = 0; channel < channels; ++channel) {
			float sample = samples[channel] ? samples[channel][frame] : 0.0f;
			if (!std::isfinite(sample))
				sample = 0.0f;

			const float absolute = std::fabs(sample);
			peaks[channel] = std::max(peaks[channel], absolute);
			sumSquares[channel] += static_cast<double>(sample) * sample;
			programPower += strip->measurement->processLoudness(channel, sample);
			callbackTruePeak =
				std::max(callbackTruePeak, strip->measurement->processTruePeak(channel, sample));
		}
		strip->measurement->addProgramPower(programPower);
	}
	strip->measurement->publishTruePeak(callbackTruePeak);

	for (uint32_t channel = 0; channel < channels; ++channel) {
		const float rms = static_cast<float>(std::sqrt(sumSquares[channel] / frames));
		strip->rmsBits[channel].store(floatToBits(rms), std::memory_order_relaxed);
		publishPeakMaximum(strip->peakLatchBits[channel], peaks[channel]);
	}

	strip->publishSequence.fetch_add(1, std::memory_order_release);
}

void ProgramAudioStrip::refreshConnection()
{
	if (shuttingDown || !meteringEnabled || !obs_initialized())
		return;

	obs_audio_info audioInfo{};
	if (!obs_get_audio_info(&audioInfo))
		return;

	const size_t mix = configuredStreamMix();
	const uint32_t channels =
		std::clamp(get_audio_channels(audioInfo.speakers), 1U, static_cast<uint32_t>(MAX_AUDIO_CHANNELS));
	if (callbackConnected && connectedMix == mix && connectedSampleRate == audioInfo.samples_per_sec &&
	    connectedSpeakers == audioInfo.speakers && connectedChannels == channels) {
		return;
	}

	disconnectCallback();
	resetPublishedLevels();

	connectedMix = mix;
	connectedSampleRate = audioInfo.samples_per_sec;
	connectedSpeakers = audioInfo.speakers;
	connectedChannels = channels;
	measurement->configure(audioInfo.samples_per_sec, audioInfo.speakers, channels);
	callbackChannels.store(channels, std::memory_order_release);
	meter->setManualChannelCount(static_cast<int>(channels));

	audio_convert_info conversion{};
	conversion.samples_per_sec = audioInfo.samples_per_sec;
	conversion.format = AUDIO_FORMAT_FLOAT_PLANAR;
	conversion.speakers = audioInfo.speakers;
	conversion.allow_clipping = true;

	obs_add_raw_audio_callback(mix, &conversion, ProgramAudioStrip::rawAudioCallback, this);
	callbackConnected = true;
	updateLabels();
}

void ProgramAudioStrip::disconnectCallback()
{
	if (!callbackConnected)
		return;

	if (obs_initialized())
		obs_remove_raw_audio_callback(connectedMix, ProgramAudioStrip::rawAudioCallback, this);

	callbackConnected = false;
	callbackChannels.store(0, std::memory_order_release);
}

void ProgramAudioStrip::resetPublishedLevels()
{
	publishSequence.fetch_add(1, std::memory_order_acq_rel);
	for (size_t channel = 0; channel < MAX_AUDIO_CHANNELS; ++channel) {
		rmsBits[channel].store(floatToBits(0.0f), std::memory_order_relaxed);
		peakLatchBits[channel].store(floatToBits(0.0f), std::memory_order_relaxed);
	}
	publishSequence.fetch_add(1, std::memory_order_release);
}

void ProgramAudioStrip::updateUi()
{
	refreshConnection();
	updateStatus();

	const uint32_t sequenceBefore = publishSequence.load(std::memory_order_acquire);
	if ((sequenceBefore & 1U) != 0)
		return;

	float magnitudeDb[MAX_AUDIO_CHANNELS];
	float peakDb[MAX_AUDIO_CHANNELS];
	float inputPeakDb[MAX_AUDIO_CHANNELS];

	for (size_t channel = 0; channel < MAX_AUDIO_CHANNELS; ++channel) {
		const float rms = bitsToFloat(rmsBits[channel].load(std::memory_order_relaxed));
		magnitudeDb[channel] = linearToDb(rms);
	}

	const uint32_t sequenceAfter = publishSequence.load(std::memory_order_acquire);
	if (sequenceBefore != sequenceAfter || (sequenceAfter & 1U) != 0)
		return;

	for (size_t channel = 0; channel < MAX_AUDIO_CHANNELS; ++channel) {
		const uint32_t peakBits = peakLatchBits[channel].exchange(floatToBits(0.0f), std::memory_order_acq_rel);
		peakDb[channel] = linearToDb(bitsToFloat(peakBits));
		inputPeakDb[channel] = peakDb[channel];
	}

	meter->setLevels(magnitudeDb, peakDb, inputPeakDb);

	const float momentary = bitsToFloat(measurement->momentaryBits.load(std::memory_order_acquire));
	const float shortTerm = bitsToFloat(measurement->shortTermBits.load(std::memory_order_acquire));
	const float integrated = bitsToFloat(measurement->integratedBits.load(std::memory_order_acquire));
	const float truePeakDb = linearToDb(bitsToFloat(measurement->truePeakBits.load(std::memory_order_acquire)));

	momentaryLabel->setText(formatMeasurement(momentary, "Basic.AudioMixer.ProgramAudio.MomentaryValue",
						  "Basic.AudioMixer.ProgramAudio.MomentaryEmpty"));
	shortTermLabel->setText(formatMeasurement(shortTerm, "Basic.AudioMixer.ProgramAudio.ShortTermValue",
						  "Basic.AudioMixer.ProgramAudio.ShortTermEmpty"));
	integratedLabel->setText(formatMeasurement(integrated, "Basic.AudioMixer.ProgramAudio.IntegratedValue",
						   "Basic.AudioMixer.ProgramAudio.IntegratedEmpty"));
	truePeakLabel->setText(formatMeasurement(truePeakDb, "Basic.AudioMixer.ProgramAudio.TruePeakValue",
						 "Basic.AudioMixer.ProgramAudio.TruePeakEmpty"));
	truePeakLabel->setAccessibleName(QTStr("Basic.AudioMixer.ProgramAudio.TruePeakAccessible")
						 .arg(std::isfinite(truePeakDb)
							      ? QString::number(truePeakDb, 'f', 1)
							      : QTStr("Basic.AudioMixer.ProgramAudio.Silence")));
	updatePeakState(truePeakDb);
}

void ProgramAudioStrip::updateLabels()
{
	const int displayedMix = static_cast<int>(connectedMix + 1);
	const QString mixText = QTStr("Basic.AudioMixer.ProgramAudio.StreamMix").arg(displayedMix);
	const QString description = QTStr("Basic.AudioMixer.ProgramAudio.Tooltip").arg(displayedMix);

	mixLabel->setText(mixText);
	setProperty("streamMix", displayedMix);
	setToolTip(description);
	setAccessibleName(QTStr("Basic.AudioMixer.ProgramAudio.AccessibleName").arg(displayedMix));
	setAccessibleDescription(description);
	meter->setToolTip(description);
	meter->setAccessibleName(QTStr("Basic.AudioMixer.ProgramAudio.MeterAccessible").arg(displayedMix));
	meter->setAccessibleDescription(description);
	mixLabel->setToolTip(description);
	momentaryLabel->setToolTip(QTStr("Basic.AudioMixer.ProgramAudio.MomentaryTooltip"));
	shortTermLabel->setToolTip(QTStr("Basic.AudioMixer.ProgramAudio.ShortTermTooltip"));
	integratedLabel->setToolTip(QTStr("Basic.AudioMixer.ProgramAudio.IntegratedTooltip"));
	truePeakLabel->setToolTip(QTStr("Basic.AudioMixer.ProgramAudio.TruePeakTooltip"));
	resetButton->setToolTip(QTStr("Basic.AudioMixer.ProgramAudio.ResetTooltip"));
	statusBadge->setToolTip(description);
}

void ProgramAudioStrip::updateStatus()
{
	const bool streaming = obs_frontend_streaming_active();
	const bool recording = obs_frontend_recording_active();
	const bool outputActive = streaming || recording;
	if (outputActive && !outputWasActive)
		requestMeasurementReset();
	outputWasActive = outputActive;

	QString state;
	QString text;
	if (streaming && recording) {
		state = QStringLiteral("live-recording");
		text = QTStr("Basic.AudioMixer.ProgramAudio.Status.LiveRecording");
	} else if (streaming) {
		state = QStringLiteral("live");
		text = QTStr("Basic.AudioMixer.ProgramAudio.Status.Live");
	} else if (recording) {
		state = QStringLiteral("recording");
		text = QTStr("Basic.AudioMixer.ProgramAudio.Status.Recording");
	} else {
		state = QStringLiteral("idle");
		text = QTStr("Basic.AudioMixer.ProgramAudio.Status.Idle");
	}

	statusBadge->setText(text);
	statusBadge->setAccessibleName(text);
	if (currentStatusState == state)
		return;

	currentStatusState = state;
	setProperty("programState", state);
	statusBadge->setProperty("programState", state);
	repolish(this);
	repolish(statusBadge);
}

void ProgramAudioStrip::updatePeakState(float truePeakDb)
{
	QString state;
	if (!std::isfinite(truePeakDb))
		state = QStringLiteral("idle");
	else if (truePeakDb >= 0.0f)
		state = QStringLiteral("clip");
	else if (truePeakDb > -1.0f)
		state = QStringLiteral("error");
	else if (truePeakDb > -2.0f)
		state = QStringLiteral("warning");
	else
		state = QStringLiteral("nominal");

	if (currentPeakState == state)
		return;

	currentPeakState = state;
	setProperty("peakState", state);
	truePeakLabel->setProperty("peakState", state);
	repolish(this);
	repolish(truePeakLabel);
}

void ProgramAudioStrip::requestMeasurementReset()
{
	if (measurement)
		measurement->requestReset();
}

QString ProgramAudioStrip::formatMeasurement(float value, const char *formatKey, const char *emptyKey)
{
	if (!std::isfinite(value))
		return QTStr(emptyKey);
	return QTStr(formatKey).arg(QString::number(value, 'f', 1));
}

uint32_t ProgramAudioStrip::floatToBits(float value) noexcept
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

float ProgramAudioStrip::bitsToFloat(uint32_t bits) noexcept
{
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

float ProgramAudioStrip::linearToDb(float value) noexcept
{
	return value > 0.0f ? 20.0f * std::log10(value) : -M_INFINITE;
}

void ProgramAudioStrip::publishPeakMaximum(std::atomic<uint32_t> &latch, float peak) noexcept
{
	const uint32_t candidate = floatToBits(peak);
	uint32_t observed = latch.load(std::memory_order_relaxed);
	while (observed < candidate && !latch.compare_exchange_weak(observed, candidate, std::memory_order_relaxed,
								    std::memory_order_relaxed)) {
	}
}
