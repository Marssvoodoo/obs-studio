#pragma once

#include <obs.h>

#include <QFrame>
#include <QString>
#include <QTimer>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

class QLabel;
class QPushButton;
class VolumeMeter;

class ProgramAudioStrip : public QFrame {
public:
	explicit ProgramAudioStrip(QWidget *parent = nullptr);
	~ProgramAudioStrip() override;

	void refreshColors();
	void shutdown();

private:
	static void rawAudioCallback(void *param, size_t mixIndex, audio_data *data);

	void refreshConnection();
	void disconnectCallback();
	void resetPublishedLevels();
	void updateUi();
	void updateLabels();
	void updateStatus();
	void updatePeakState(float truePeakDb);
	void requestMeasurementReset();

	static uint32_t floatToBits(float value) noexcept;
	static float bitsToFloat(uint32_t bits) noexcept;
	static float linearToDb(float value) noexcept;
	static void publishPeakMaximum(std::atomic<uint32_t> &latch, float peak) noexcept;
	static QString formatMeasurement(float value, const char *formatKey, const char *emptyKey);

	struct MeasurementState;

	VolumeMeter *meter{nullptr};
	QLabel *mixLabel{nullptr};
	QLabel *momentaryLabel{nullptr};
	QLabel *shortTermLabel{nullptr};
	QLabel *integratedLabel{nullptr};
	QLabel *truePeakLabel{nullptr};
	QLabel *statusBadge{nullptr};
	QPushButton *resetButton{nullptr};
	QTimer updateTimer;
	std::unique_ptr<MeasurementState> measurement;

	std::array<std::atomic<uint32_t>, MAX_AUDIO_CHANNELS> rmsBits{};
	std::array<std::atomic<uint32_t>, MAX_AUDIO_CHANNELS> peakLatchBits{};
	std::atomic<uint32_t> publishSequence{0};
	std::atomic<uint32_t> callbackChannels{0};

	size_t connectedMix{0};
	uint32_t connectedSampleRate{0};
	speaker_layout connectedSpeakers{SPEAKERS_UNKNOWN};
	uint32_t connectedChannels{0};
	bool callbackConnected{false};
	bool shuttingDown{false};
	bool outputWasActive{false};
	QString currentStatusState;
	QString currentPeakState;
};
