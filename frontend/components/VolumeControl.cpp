#include "VolumeControl.hpp"

#include <components/MuteCheckBox.hpp>
#include <components/VolumeMeter.hpp>
#include <components/VolumeName.hpp>
#include <components/VolumeSlider.hpp>
#include <dialogs/NameDialog.hpp>
#include <widgets/OBSBasic.hpp>

#include <QActionGroup>
#include <QFontMetrics>
#include <QMessageBox>
#include <QMouseEvent>
#include <QObjectCleanupHandler>
#include <QTimer>

#include <array>
#include <cmath>

#include "moc_VolumeControl.cpp"

namespace {
enum class LevelGuideGroup {
	Voice,
	Program,
	Effects,
	General,
};

struct LevelGuideProfile {
	const char *id;
	const char *nameKey;
	float lowDb;
	float highDb;
	LevelGuideGroup group;
};

constexpr std::array<LevelGuideProfile, 17> levelGuideProfiles{{
	{"voice", "Basic.AudioMixer.LevelGuide.Role.Voice", -12.0f, -6.0f, LevelGuideGroup::Voice},
	{"guest", "Basic.AudioMixer.LevelGuide.Role.Guest", -14.0f, -8.0f, LevelGuideGroup::Voice},
	{"party_chat", "Basic.AudioMixer.LevelGuide.Role.PartyChat", -20.0f, -10.0f, LevelGuideGroup::Voice},
	{"console_chat", "Basic.AudioMixer.LevelGuide.Role.ConsoleChat", -20.0f, -10.0f,
	 LevelGuideGroup::Voice},
	{"yells", "Basic.AudioMixer.LevelGuide.Role.Yells", -10.0f, -3.0f, LevelGuideGroup::Voice},
	{"whisper", "Basic.AudioMixer.LevelGuide.Role.Whisper", -18.0f, -10.0f, LevelGuideGroup::Voice},
	{"tts", "Basic.AudioMixer.LevelGuide.Role.TTS", -14.0f, -8.0f, LevelGuideGroup::Voice},
	{"game", "Basic.AudioMixer.LevelGuide.Role.Game", -24.0f, -14.0f, LevelGuideGroup::Program},
	{"desktop", "Basic.AudioMixer.LevelGuide.Role.Desktop", -24.0f, -14.0f, LevelGuideGroup::Program},
	{"video", "Basic.AudioMixer.LevelGuide.Role.Video", -20.0f, -10.0f, LevelGuideGroup::Program},
	{"music", "Basic.AudioMixer.LevelGuide.Role.Music", -30.0f, -20.0f, LevelGuideGroup::Program},
	{"alerts", "Basic.AudioMixer.LevelGuide.Role.Alerts", -18.0f, -10.0f, LevelGuideGroup::Effects},
	{"soundboard", "Basic.AudioMixer.LevelGuide.Role.Soundboard", -18.0f, -8.0f,
	 LevelGuideGroup::Effects},
	{"effects", "Basic.AudioMixer.LevelGuide.Role.Effects", -18.0f, -10.0f, LevelGuideGroup::Effects},
	{"impact", "Basic.AudioMixer.LevelGuide.Role.Impact", -12.0f, -4.0f, LevelGuideGroup::Effects},
	{"ambient", "Basic.AudioMixer.LevelGuide.Role.Ambient", -36.0f, -24.0f, LevelGuideGroup::Effects},
	{"general", "Basic.AudioMixer.LevelGuide.Role.General", -20.0f, -10.0f, LevelGuideGroup::General},
}};

const LevelGuideProfile &getLevelGuideProfile(const QString &id)
{
	for (const LevelGuideProfile &profile : levelGuideProfiles) {
		if (id == QLatin1String(profile.id))
			return profile;
	}

	return levelGuideProfiles.back();
}

bool hasAudioPeak(float peak)
{
	/* libobs uses -M_INFINITE (-3.4e38f), rather than IEEE -infinity,
	 * as its silent/uninitialized meter sentinel. */
	return std::isfinite(peak) && peak > (-M_INFINITE / 2.0f);
}

QString inferLevelGuideRole(const QString &sourceName)
{
	const QString name = sourceName.toLower();

	if (name.contains("ps4chat") || name.contains("console chat") || name.contains("line in at rear panel"))
		return QStringLiteral("console_chat");
	if (name.contains("yell") || name.contains("reaction") || name.contains("shout") || name.contains("scream"))
		return QStringLiteral("yells");
	if (name.contains("guest") || name.contains("co-host") || name.contains("cohost"))
		return QStringLiteral("guest");
	if (name.contains("tts") || name.contains("text to speech") || name.contains("voiceover"))
		return QStringLiteral("tts");
	if (name.contains("whisper") || name.contains("asmr"))
		return QStringLiteral("whisper");
	if (name.contains("discord") || name.contains("chat") || name.contains("party"))
		return QStringLiteral("party_chat");
	if (name.contains("microphone") || name.contains("mic ") || name.endsWith(" mic") ||
	    name.contains("focusrite") || name.contains("scarlett") || name.contains("yeti") ||
	    name.contains("voice"))
		return QStringLiteral("voice");
	if (name.contains("music") || name.contains("spotify"))
		return QStringLiteral("music");
	if (name.contains("game") || name.contains("ps4") || name.contains("ps5") || name.contains("xbox"))
		return QStringLiteral("game");
	if (name.contains("soundboard"))
		return QStringLiteral("soundboard");
	if (name.contains("explosion") || name.contains("impact"))
		return QStringLiteral("impact");
	if (name.contains("alert") || name.contains("notification"))
		return QStringLiteral("alerts");
	if (name.contains("sfx") || name.contains("effect"))
		return QStringLiteral("effects");
	if (name.contains("ambient") || name.contains("background") || name.contains("room tone"))
		return QStringLiteral("ambient");
	if (name.contains("video") || name.contains("dialogue") || name.contains("dialog"))
		return QStringLiteral("video");
	if (name.contains("desktop"))
		return QStringLiteral("desktop");

	return QStringLiteral("general");
}

bool isSourceUnassigned(obs_source_t *source)
{
	uint32_t mixes = (obs_source_get_audio_mixers(source) & ((1 << MAX_AUDIO_MIXES) - 1));
	obs_monitoring_type mt = obs_source_get_monitoring_type(source);

	return mixes == 0 && mt != OBS_MONITORING_TYPE_MONITOR_ONLY;
}

void showUnassignedWarning(const char *name)
{
	auto msgBox = [=]() {
		QMessageBox msgbox(App()->GetMainWindow());
		msgbox.setWindowTitle(QTStr("VolControl.UnassignedWarning.Title"));
		msgbox.setText(QTStr("VolControl.UnassignedWarning.Text").arg(name));
		msgbox.setIcon(QMessageBox::Icon::Information);
		msgbox.addButton(QMessageBox::Ok);

		QCheckBox *cb = new QCheckBox(QTStr("DoNotShowAgain"));
		msgbox.setCheckBox(cb);

		msgbox.exec();

		if (cb->isChecked()) {
			config_set_bool(App()->GetUserConfig(), "General", "WarnedAboutUnassignedSources", true);
			config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
		}
	};

	QMetaObject::invokeMethod(App(), "Exec", Qt::QueuedConnection, Q_ARG(VoidFunc, msgBox));
}
} // namespace

VolumeControl::VolumeControl(obs_source_t *source, QWidget *parent, bool vertical)
	: weakSource_(OBSGetWeakRef(source)),
	  obs_fader(obs_fader_create(OBS_FADER_LOG)),
	  vertical(vertical),
	  contextMenu(nullptr),
	  QFrame(parent)
{
	utils = std::make_unique<idian::Utils>(this);

	uuid = obs_source_get_uuid(source);

	mainLayout = new QBoxLayout(QBoxLayout::LeftToRight, this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);
	setLayout(mainLayout);

	categoryLabel = new QLabel("Active");
	categoryLabel->setAlignment(Qt::AlignCenter);
	utils->addClass(categoryLabel, "mixer-category");
	utils->addClass(categoryLabel, "text-tiny");

	nameButton = new VolumeName(source, this);
	nameButton->setMaximumWidth(280);
	utils->addClass(nameButton, "text-small");
	utils->addClass(nameButton, "mixer-name");

	muteButton = new QPushButton(this);
	muteButton->setCheckable(true);
	utils->addClass(muteButton, "btn-mute");

	monitorButton = new QPushButton(this);
	monitorButton->setCheckable(true);
	utils->addClass(monitorButton, "btn-monitor");

	volumeLabel = new QLabel(this);
	volumeLabel->setIndent(0);
	volumeLabel->setObjectName("volLabel");

	levelGuideLabel = new QLabel(this);
	levelGuideLabel->setObjectName("levelGuideLabel");
	levelGuideLabel->setAlignment(Qt::AlignCenter);
	levelGuideLabel->setTextFormat(Qt::PlainText);
	levelGuideLabel->setCursor(Qt::PointingHandCursor);
	levelGuideLabel->installEventFilter(this);
	utils->addClass(levelGuideLabel, "mixer-level-guide");
	utils->addClass(levelGuideLabel, "text-tiny");

	slider = new VolumeSlider(obs_fader, Qt::Horizontal, this);
	slider->setMinimum(0);
	slider->setMaximum(int(FADER_PRECISION));

	sourceName = obs_source_get_name(source);
	setObjectName(sourceName);

	OBSDataAutoRelease privateSettings = obs_source_get_private_settings(source);
	levelRole = QString::fromUtf8(obs_data_get_string(privateSettings, "mixer_level_role"));
	levelRoleAutomatic = levelRole.isEmpty();
	if (levelRole.isEmpty())
		levelRole = inferLevelGuideRole(sourceName);

	utils->applyStateStylingEventFilter(muteButton);
	utils->applyStateStylingEventFilter(monitorButton);

	volumeMeter = new VolumeMeter(this, source);

	obsMuted = obs_source_muted(source);
	bool unassigned = isSourceUnassigned(source);
	obsMonitoringType = obs_source_get_monitoring_type(source);

	volumeMeter->setMuted(obsMuted || unassigned);

	setLayoutVertical(vertical);
	setName(sourceName);
	updateLevelGuide();

	QTimer *levelTimer = new QTimer(this);
	levelTimer->setInterval(250);
	connect(levelTimer, &QTimer::timeout, this, &VolumeControl::updateLevelGuide);
	levelTimer->start();

	obs_fader_add_callback(obs_fader, obsVolumeChanged, this);

	obsSignals.reserve(9);
	obsSignals.emplace_back(obs_source_get_signal_handler(source), "mute", obsVolumeMuted, this);
	obsSignals.emplace_back(obs_source_get_signal_handler(source), "audio_mixers", obsMixersChanged, this);
	obsSignals.emplace_back(obs_source_get_signal_handler(source), "audio_monitoring", obsMonitoringChanged, this);
	obsSignals.emplace_back(obs_source_get_signal_handler(source), "activate", VolumeControl::obsSourceActivated,
				this);
	obsSignals.emplace_back(obs_source_get_signal_handler(source), "deactivate",
				VolumeControl::obsSourceDeactivated, this);
	obsSignals.emplace_back(obs_source_get_signal_handler(source), "audio_activate",
				VolumeControl::obsSourceActivated, this);
	obsSignals.emplace_back(obs_source_get_signal_handler(source), "audio_deactivate",
				VolumeControl::obsSourceDeactivated, this);

	obsSignals.emplace_back(obs_source_get_signal_handler(source), "remove", VolumeControl::obsSourceDestroy, this);
	obsSignals.emplace_back(obs_source_get_signal_handler(source), "destroy", VolumeControl::obsSourceDestroy,
				this);

	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QWidget::customContextMenuRequested, this, &VolumeControl::showVolumeControlMenu);

	connect(nameButton, &VolumeName::renamed, this, &VolumeControl::setName);
	connect(nameButton, &VolumeName::clicked, this, [&]() { showVolumeControlMenu(); });

	connect(slider, &VolumeSlider::valueChanged, this, &VolumeControl::sliderChanged);
	connect(muteButton, &QPushButton::clicked, this, &VolumeControl::handleMuteButton);
	connect(monitorButton, &QPushButton::clicked, this, &VolumeControl::handleMonitorButton);

	OBSBasic *main = OBSBasic::Get();
	if (main) {
		connect(main, &OBSBasic::profileSettingChanged, this,
			[this](const std::string &category, const std::string &name) {
				if (category == "Audio" && name == "MeterDecayRate") {
					updateDecayRate();
				} else if (category == "Audio" && name == "PeakMeterType") {
					updatePeakMeterType();
				}
			});
	}

	obs_fader_attach_source(obs_fader, source);

	// Call volume changed once to init the slider position and label
	changeVolume();

	processMixerState();
}

VolumeControl::~VolumeControl()
{
	obs_fader_remove_callback(obs_fader, obsVolumeChanged, this);

	obsSignals.clear();

	if (contextMenu) {
		contextMenu->close();
	}
}

const QIcon &VolumeControl::getWarningIcon()
{
	static const QIcon &icon = *new QIcon(":/res/images/unassigned.svg");
	return icon;
}

const QIcon &VolumeControl::getMutedIcon()
{
	static const QIcon &icon = *new QIcon(":/settings/images/settings/audio.svg");
	return icon;
}

const QIcon &VolumeControl::getUnmutedIcon()
{
	static const QIcon &icon = *new QIcon(":/settings/images/settings/audio.svg");
	return icon;
}

const QIcon &VolumeControl::getMonitorOnIcon()
{
	static const QIcon &icon = *new QIcon(":/res/images/headphones.svg");
	return icon;
}

const QIcon &VolumeControl::getMonitorOffIcon()
{
	static const QIcon &icon = *new QIcon(":/res/images/headphones-off.svg");
	return icon;
}

void VolumeControl::obsVolumeChanged(void *data, float)
{
	VolumeControl *volControl = static_cast<VolumeControl *>(data);

	QMetaObject::invokeMethod(volControl, "changeVolume", Qt::QueuedConnection);
}

void VolumeControl::obsVolumeMuted(void *data, calldata_t *params)
{
	VolumeControl *volControl = static_cast<VolumeControl *>(data);
	bool muted = calldata_bool(params, "muted");

	QMetaObject::invokeMethod(volControl, "onMuteChanged", Qt::QueuedConnection, Q_ARG(bool, muted));
}

void VolumeControl::obsMixersChanged(void *data, calldata_t *)
{
	VolumeControl *volControl = static_cast<VolumeControl *>(data);
	QMetaObject::invokeMethod(volControl, "processMixerState", Qt::QueuedConnection);
}

void VolumeControl::obsMonitoringChanged(void *data, calldata_t *params)
{
	VolumeControl *volControl = static_cast<VolumeControl *>(data);
	auto type = static_cast<int>(calldata_int(params, "type"));

	QMetaObject::invokeMethod(volControl, "onMonitoringChanged", Qt::QueuedConnection, Q_ARG(int, type));
}

void VolumeControl::obsSourceActivated(void *data, calldata_t *)
{
	QMetaObject::invokeMethod(static_cast<VolumeControl *>(data), "onSourceActiveChanged", Qt::QueuedConnection,
				  Q_ARG(bool, true));
}

void VolumeControl::obsSourceDeactivated(void *data, calldata_t *)
{
	QMetaObject::invokeMethod(static_cast<VolumeControl *>(data), "onSourceActiveChanged", Qt::QueuedConnection,
				  Q_ARG(bool, false));
}

void VolumeControl::obsSourceDestroy(void *data, calldata_t *)
{
	QMetaObject::invokeMethod(static_cast<VolumeControl *>(data), "onSourceDestroyed", Qt::QueuedConnection);
}

void VolumeControl::setLayoutVertical(bool vertical)
{
	QBoxLayout *newLayout = new QBoxLayout(QBoxLayout::TopToBottom);
	newLayout->setContentsMargins(0, 0, 0, 0);
	newLayout->setSpacing(0);

	if (vertical) {
		setMaximumWidth(110);
		setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

		QHBoxLayout *categoryLayout = new QHBoxLayout;
		QHBoxLayout *nameLayout = new QHBoxLayout;
		QHBoxLayout *controlLayout = new QHBoxLayout;
		QHBoxLayout *volLayout = new QHBoxLayout;
		QFrame *meterFrame = new QFrame;
		QHBoxLayout *meterLayout = new QHBoxLayout;

		volumeMeter->setVertical(true);
		volumeMeter->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);

		slider->setOrientation(Qt::Vertical);
		slider->setLayoutDirection(Qt::LeftToRight);
		slider->setDisplayTicks(true);

		nameButton->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
		categoryLabel->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
		/* Ignore changing text size hints so live peaks never resize a
		 * vertical mixer strip. */
		levelGuideLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		levelGuideLabel->setMinimumWidth(0);
		levelGuideLabel->setMaximumWidth(QWIDGETSIZE_MAX);
		levelGuideLabel->setFixedHeight(34);
		levelGuideLabel->setWordWrap(false);
		volumeLabel->setAlignment(Qt::AlignLeft);

		categoryLayout->setAlignment(Qt::AlignCenter);
		nameLayout->setAlignment(Qt::AlignCenter);
		meterLayout->setAlignment(Qt::AlignCenter);
		controlLayout->setAlignment(Qt::AlignCenter);
		volLayout->setAlignment(Qt::AlignCenter);

		meterFrame->setObjectName("volMeterFrame");

		categoryLayout->setContentsMargins(0, 0, 0, 0);
		categoryLayout->setSpacing(0);
		categoryLayout->addWidget(categoryLabel);

		nameLayout->setContentsMargins(0, 0, 0, 0);
		nameLayout->setSpacing(0);
		nameLayout->addWidget(nameButton);

		controlLayout->setContentsMargins(0, 0, 0, 0);
		controlLayout->setSpacing(0);

		// Add Headphone (audio monitoring) widget here
		controlLayout->addWidget(muteButton);
		controlLayout->addWidget(monitorButton);

		meterLayout->setContentsMargins(0, 0, 0, 0);
		meterLayout->setSpacing(0);
		meterLayout->addWidget(slider);
		meterLayout->addWidget(volumeMeter);

		meterFrame->setLayout(meterLayout);

		volLayout->setContentsMargins(0, 0, 0, 0);
		volLayout->setSpacing(0);
		volLayout->addWidget(volumeLabel);
		volLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::MinimumExpanding, QSizePolicy::Maximum));

		newLayout->addItem(categoryLayout);
		newLayout->addItem(nameLayout);
		newLayout->addWidget(levelGuideLabel);
		newLayout->addItem(volLayout);
		newLayout->addWidget(meterFrame);
		newLayout->addItem(controlLayout);

		newLayout->setStretch(0, 0);
		newLayout->setStretch(1, 0);
		newLayout->setStretch(2, 0);
		newLayout->setStretch(3, 0);
		newLayout->setStretch(4, 1);
		newLayout->setStretch(5, 0);

		volumeMeter->setFocusProxy(slider);
	} else {
		setMaximumWidth(QWIDGETSIZE_MAX);
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

		QHBoxLayout *textLayout = new QHBoxLayout;
		QHBoxLayout *controlLayout = new QHBoxLayout;
		QFrame *meterFrame = new QFrame;
		QVBoxLayout *meterLayout = new QVBoxLayout;
		QVBoxLayout *buttonLayout = new QVBoxLayout;

		volumeMeter->setVertical(false);
		volumeMeter->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);

		slider->setOrientation(Qt::Horizontal);
		slider->setLayoutDirection(Qt::LeftToRight);
		slider->setDisplayTicks(true);

		nameButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		categoryLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
		levelGuideLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		levelGuideLabel->setFixedWidth(220);
		levelGuideLabel->setFixedHeight(24);
		levelGuideLabel->setWordWrap(false);
		volumeLabel->setAlignment(Qt::AlignRight);

		QHBoxLayout *textSubLayout = new QHBoxLayout;
		textSubLayout->setContentsMargins(0, 0, 0, 0);
		textLayout->setContentsMargins(0, 0, 0, 0);

		textLayout->addWidget(nameButton);
		textLayout->addItem(textSubLayout);

		textSubLayout->addSpacerItem(
			new QSpacerItem(0, 0, QSizePolicy::MinimumExpanding, QSizePolicy::Preferred));
		textSubLayout->addWidget(categoryLabel);
		textSubLayout->addWidget(volumeLabel);

		meterFrame->setObjectName("volMeterFrame");
		meterFrame->setLayout(meterLayout);

		meterLayout->setContentsMargins(0, 0, 0, 0);
		meterLayout->setSpacing(0);

		meterLayout->addWidget(slider);
		meterLayout->addWidget(volumeMeter);
		meterLayout->setStretch(0, 2);
		meterLayout->setStretch(1, 2);

		buttonLayout->setContentsMargins(0, 0, 0, 0);
		buttonLayout->setSpacing(0);

		buttonLayout->addWidget(muteButton);
		buttonLayout->addWidget(monitorButton);

		controlLayout->addItem(buttonLayout);
		controlLayout->addWidget(meterFrame);

		newLayout->addItem(textLayout);
		newLayout->addWidget(levelGuideLabel);
		newLayout->addItem(controlLayout);
		newLayout->setStretch(0, 3);
		newLayout->setStretch(1, 0);
		newLayout->setStretch(2, 6);

		volumeMeter->setFocusProxy(slider);
	}

	QWidget().setLayout(mainLayout);

	setLayout(newLayout);
	mainLayout = newLayout;
	updateTabOrder();

	adjustSize();
}

void VolumeControl::showVolumeControlMenu(QPoint pos)
{
	OBSSource source = OBSGetStrongRef(weakSource());
	if (!source) {
		return;
	}

	QMenu *popup = new QMenu(window());

	// Create menu QActions
	QAction *lockAction = new QAction(QTStr("LockVolume"), popup);
	lockAction->setCheckable(true);
	lockAction->setChecked(mixerStatus().has(VolumeControl::MixerStatus::Locked));

	bool isGlobal = mixerStatus().has(VolumeControl::MixerStatus::Global);

	QAction *pinAction = new QAction(QTStr("Basic.AudioMixer.Pin"), popup);
	bool isPinned = mixerStatus().has(VolumeControl::MixerStatus::Pinned);
	if (isPinned) {
		pinAction->setText(QTStr("Basic.AudioMixer.Unpin"));
	}

	QAction *hideAction = new QAction(QTStr("Basic.AudioMixer.Hide"), popup);
	bool isHidden = mixerStatus().has(VolumeControl::MixerStatus::Hidden);
	if (isHidden && !isGlobal) {
		hideAction->setText(QTStr("Basic.AudioMixer.Unhide"));
	}

	QAction *unhideAllAction = new QAction(QTStr("UnhideAll"), popup);
	QAction *mixerRenameAction = new QAction(QTStr("Rename"), popup);

	QAction *copyFiltersAction = new QAction(QTStr("Copy.Filters"), popup);
	QAction *pasteFiltersAction = new QAction(QTStr("Paste.Filters"), popup);

	QAction *filtersAction = new QAction(QTStr("Filters"), popup);
	QAction *propertiesAction = new QAction(QTStr("Properties"), popup);

	QMenu *levelGuideMenu = createLevelGuideMenu(popup);

	// Set properties on actions that require source reference
	hideAction->setProperty("source", QVariant::fromValue<OBSSource>(source));
	pinAction->setProperty("source", QVariant::fromValue<OBSSource>(source));

	mixerRenameAction->setProperty("source", QVariant::fromValue<OBSSource>(source));

	copyFiltersAction->setProperty("source", QVariant::fromValue<OBSSource>(source));
	pasteFiltersAction->setProperty("source", QVariant::fromValue<OBSSource>(source));

	filtersAction->setProperty("source", QVariant::fromValue<OBSSource>(source));
	propertiesAction->setProperty("source", QVariant::fromValue<OBSSource>(source));

	// Connect actions to signals
	OBSBasic *main = OBSBasic::Get();

	connect(unhideAllAction, &QAction::triggered, this, [this]() { emit unhideAll(); });

	connect(hideAction, &QAction::triggered, this, [this, isHidden]() { setHiddenInMixer(!isHidden); });
	connect(
		pinAction, &QAction::triggered, this, [this, isPinned]() { setPinnedInMixer(!isPinned); },
		Qt::DirectConnection);
	connect(lockAction, &QAction::toggled, this, &VolumeControl::setLocked);

	connect(copyFiltersAction, &QAction::triggered, main, &OBSBasic::actionCopyFilters);
	connect(pasteFiltersAction, &QAction::triggered, main, &OBSBasic::actionPasteFilters);

	connect(mixerRenameAction, &QAction::triggered, this, &VolumeControl::renameSource);

	connect(filtersAction, &QAction::triggered, main, &OBSBasic::actionOpenSourceFilters);
	connect(propertiesAction, &QAction::triggered, main, &OBSBasic::actionOpenSourceProperties);

	// Enable/disable actions
	copyFiltersAction->setEnabled(obs_source_filter_count(source) > 0);
	pasteFiltersAction->setEnabled(!obs_weak_source_expired(main->copyFiltersSource()));

	if (isGlobal) {
		pinAction->setDisabled(true);
		hideAction->setDisabled(true);
	}

	if (isPinned) {
		hideAction->setDisabled(true);
	}

	// Build menu
	popup->addAction(unhideAllAction);
	popup->addSeparator();
	popup->addAction(pinAction);
	popup->addAction(hideAction);
	popup->addAction(lockAction);
	popup->addMenu(levelGuideMenu);

	popup->addSeparator();
	popup->addAction(copyFiltersAction);
	popup->addAction(pasteFiltersAction);
	popup->addSeparator();
	popup->addAction(mixerRenameAction);
	popup->addSeparator();
	popup->addAction(filtersAction);
	popup->addAction(propertiesAction);

	// Calculate menu position
	QPoint popupPos = mapToGlobal(pos);

	if (pos.isNull()) {
		QPoint menuPos = nameButton->mapToGlobal(nameButton->rect().bottomLeft());
		QSize menuSize = popup->sizeHint();

		QRect available = QGuiApplication::screenAt(menuPos)->availableGeometry();
		int spaceBelow = available.bottom() - menuPos.y();
		int spaceAbove = menuPos.y() - available.top();

		if (menuSize.height() > spaceBelow && spaceAbove > spaceBelow) {
			menuPos = nameButton->mapToGlobal(nameButton->rect().topLeft());
			menuPos.ry() -= menuSize.height();
		}

		if (menuPos.x() + menuSize.width() > available.right()) {
			menuPos.rx() = available.right() - menuSize.width();
		}

		popupPos = menuPos;
	}

	popup->popup(popupPos);

	connect(popup, &QMenu::aboutToHide, popup, &QMenu::deleteLater);
}

QMenu *VolumeControl::createLevelGuideMenu(QWidget *parent)
{
	QMenu *menu = new QMenu(QTStr("Basic.AudioMixer.LevelGuide.Menu"), parent);
	QActionGroup *actionGroup = new QActionGroup(menu);
	actionGroup->setExclusive(true);

	QAction *autoAction = menu->addAction(QTStr("Basic.AudioMixer.LevelGuide.Role.AutoSource"));
	autoAction->setCheckable(true);
	autoAction->setChecked(levelRoleAutomatic);
	actionGroup->addAction(autoAction);
	connect(autoAction, &QAction::triggered, this, &VolumeControl::setAutomaticLevelRole);
	menu->addSeparator();

	QMenu *voiceMenu = menu->addMenu(QTStr("Basic.AudioMixer.LevelGuide.Group.Voice"));
	QMenu *programMenu = menu->addMenu(QTStr("Basic.AudioMixer.LevelGuide.Group.Program"));
	QMenu *effectsMenu = menu->addMenu(QTStr("Basic.AudioMixer.LevelGuide.Group.Effects"));

	for (const LevelGuideProfile &profile : levelGuideProfiles) {
		QMenu *targetMenu = menu;
		switch (profile.group) {
		case LevelGuideGroup::Voice:
			targetMenu = voiceMenu;
			break;
		case LevelGuideGroup::Program:
			targetMenu = programMenu;
			break;
		case LevelGuideGroup::Effects:
			targetMenu = effectsMenu;
			break;
		case LevelGuideGroup::General:
			break;
		}

		if (profile.group == LevelGuideGroup::General)
			menu->addSeparator();

		QAction *roleAction = targetMenu->addAction(QTStr(profile.nameKey));
		roleAction->setCheckable(true);
		roleAction->setChecked(!levelRoleAutomatic && levelRole == QLatin1String(profile.id));
		actionGroup->addAction(roleAction);
		connect(roleAction, &QAction::triggered, this,
			[this, role = QString::fromLatin1(profile.id)]() { setLevelRole(role); });
	}

	return menu;
}

void VolumeControl::showLevelGuideMenu()
{
	QMenu *menu = createLevelGuideMenu(this);
	const QPoint menuPos = levelGuideLabel->mapToGlobal(QPoint(0, levelGuideLabel->height()));
	menu->popup(menuPos);
	connect(menu, &QMenu::aboutToHide, menu, &QMenu::deleteLater);
}

bool VolumeControl::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == levelGuideLabel && event->type() == QEvent::MouseButtonRelease) {
		auto *mouseEvent = static_cast<QMouseEvent *>(event);
		if (mouseEvent->button() == Qt::LeftButton || mouseEvent->button() == Qt::RightButton) {
			showLevelGuideMenu();
			return true;
		}
	}

	return QFrame::eventFilter(watched, event);
}

void VolumeControl::renameSource()
{
	QAction *action = reinterpret_cast<QAction *>(sender());
	OBSSource source = action->property("source").value<OBSSource>();

	std::string uuid = obs_source_get_uuid(source);

	OBSBasic *main = OBSBasic::Get();

	// Defer the rename dialog to avoid blocking the UI thread while the context menu is closing, which can cause issues on some platforms
	QTimer::singleShot(0, main, [main, uuid]() {
		if (!main) {
			return;
		}

		OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.c_str());
		if (!source) {
			return;
		}

		const char *prevName = obs_source_get_name(source);

		for (;;) {
			std::string name;
			bool accepted = NameDialog::AskForName(main, QTStr("Basic.Main.MixerRename.Title"),
							       QTStr("Basic.Main.MixerRename.Text"), name,
							       QT_UTF8(prevName));
			if (!accepted) {
				return;
			}

			if (name.empty()) {
				OBSMessageBox::warning(main, QTStr("NoNameEntered.Title"), QTStr("NoNameEntered.Text"));
				continue;
			}

			if (name == prevName) {
				return;
			}

			OBSSourceAutoRelease sourceTest = obs_get_source_by_name(name.c_str());

			if (sourceTest) {
				OBSMessageBox::warning(main, QTStr("NameExists.Title"), QTStr("NameExists.Text"));
				continue;
			}

			std::string prevName(obs_source_get_name(source));
			auto undo = [prevName](const std::string &data) {
				OBSSourceAutoRelease source = obs_get_source_by_uuid(data.c_str());
				obs_source_set_name(source, prevName.c_str());
			};

			std::string editedName = name;
			auto redo = [editedName](const std::string &data) {
				OBSSourceAutoRelease source = obs_get_source_by_uuid(data.c_str());
				obs_source_set_name(source, editedName.c_str());
			};

			main->undo_s.add_action(QTStr("Undo.Rename").arg(name.c_str()), undo, redo, uuid, uuid);

			obs_source_set_name(source, name.c_str());
			break;
		}
	});
}

void VolumeControl::changeVolume()
{
	QSignalBlocker blocker(slider);
	slider->setValue((int)(obs_fader_get_deflection(obs_fader) * FADER_PRECISION));

	updateText();
}

void VolumeControl::setLocked(bool locked)
{
	OBSSource source = OBSGetStrongRef(weakSource());
	if (!source) {
		return;
	}
	OBSDataAutoRelease priv_settings = obs_source_get_private_settings(source);
	obs_data_set_bool(priv_settings, "volume_locked", locked);

	enableSlider(!locked);
	mixerStatus().set(VolumeControl::MixerStatus::Locked, locked);

	OBSBasic *main = OBSBasic::Get();
	emit main->mixerStatusChanged(uuid);
}

void VolumeControl::onMuteChanged(bool muted)
{
	obsMuted = muted;
	processMixerState();
}

void VolumeControl::onMonitoringChanged(int type)
{
	obsMonitoringType = static_cast<obs_monitoring_type>(type);
	processMixerState();
}

void VolumeControl::updateCategoryLabel()
{
	QString labelText = QTStr("Basic.AudioMixer.Category.Active");

	if (mixerStatus().has(VolumeControl::MixerStatus::Unassigned)) {
		labelText = QTStr("Basic.AudioMixer.Category.Unassigned");
	} else if (mixerStatus().has(VolumeControl::MixerStatus::Global)) {
		labelText = QTStr("Basic.AudioMixer.Category.Global");
	} else if (mixerStatus().has(VolumeControl::MixerStatus::Pinned)) {
		labelText = QTStr("Basic.AudioMixer.Category.Pinned");
	} else if (mixerStatus().has(VolumeControl::MixerStatus::Hidden)) {
		labelText = QTStr("Basic.AudioMixer.Category.Hidden");
	} else if (!mixerStatus().has(VolumeControl::MixerStatus::Active)) {
		labelText = QTStr("Basic.AudioMixer.Category.Inactive");

		if (mixerStatus().has(VolumeControl::MixerStatus::Preview)) {
			labelText = QTStr("Basic.AudioMixer.Category.Preview");
		}
	}

	bool stylePinned = mixerStatus().has(VolumeControl::MixerStatus::Global) ||
			   mixerStatus().has(VolumeControl::MixerStatus::Pinned);
	bool styleInactive = mixerStatus().has(VolumeControl::MixerStatus::Active) != true;
	bool styleHidden = mixerStatus().has(VolumeControl::MixerStatus::Hidden);
	bool styleUnassigned = mixerStatus().has(VolumeControl::MixerStatus::Unassigned);
	bool stylePreviewed = mixerStatus().has(VolumeControl::MixerStatus::Preview);

	utils->toggleClass("volume-pinned", stylePinned);
	utils->toggleClass("volume-inactive", styleInactive);
	utils->toggleClass("volume-preview", styleInactive && stylePreviewed);
	utils->toggleClass("volume-hidden", styleHidden && !stylePinned);
	utils->toggleClass("volume-unassigned", styleUnassigned);

	categoryLabel->setText(labelText);
	categoryLabel->setAlignment(Qt::AlignCenter);

	style()->polish(categoryLabel);
	style()->polish(volumeMeter);

	bool forceUpdate = true;
	volumeMeter->updateBackgroundCache(forceUpdate);
}

void VolumeControl::updateDecayRate()
{
	OBSBasic *main = OBSBasic::Get();
	double meterDecayRate = config_get_double(main->Config(), "Audio", "MeterDecayRate");

	setMeterDecayRate(meterDecayRate);
}

void VolumeControl::updatePeakMeterType()
{
	OBSBasic *main = OBSBasic::Get();
	uint32_t peakMeterTypeIdx = config_get_uint(main->Config(), "Audio", "PeakMeterType");

	enum obs_peak_meter_type peakMeterType;
	switch (peakMeterTypeIdx) {
	case 0:
		peakMeterType = SAMPLE_PEAK_METER;
		break;
	case 1:
		peakMeterType = TRUE_PEAK_METER;
		break;
	default:
		peakMeterType = SAMPLE_PEAK_METER;
		break;
	}

	setPeakMeterType(peakMeterType);
}

void VolumeControl::setLevelRole(const QString &role, bool persist)
{
	const LevelGuideProfile &profile = getLevelGuideProfile(role);
	levelRole = QString::fromLatin1(profile.id);
	levelRoleAutomatic = false;
	heldGuidePeak = -M_INFINITE;
	levelGuideHoldTicks = 0;

	if (persist) {
		OBSSource source = OBSGetStrongRef(weakSource());
		if (source) {
			OBSDataAutoRelease privateSettings = obs_source_get_private_settings(source);
			obs_data_set_string(privateSettings, "mixer_level_role", profile.id);
		}
	}

	updateLevelGuide();
}

void VolumeControl::setAutomaticLevelRole()
{
	levelRole = inferLevelGuideRole(sourceName);
	levelRoleAutomatic = true;
	heldGuidePeak = -M_INFINITE;
	levelGuideHoldTicks = 0;

	OBSSource source = OBSGetStrongRef(weakSource());
	if (source) {
		OBSDataAutoRelease privateSettings = obs_source_get_private_settings(source);
		obs_data_erase(privateSettings, "mixer_level_role");
	}

	updateLevelGuide();
}

void VolumeControl::updateLevelGuide()
{
	if (!volumeMeter || !levelGuideLabel)
		return;

	OBSSource source = OBSGetStrongRef(weakSource());
	if (!source)
		return;

	const float recentPeak = volumeMeter->takeRecentPeak();
	if (hasAudioPeak(recentPeak)) {
		if (!hasAudioPeak(heldGuidePeak) || recentPeak >= heldGuidePeak || levelGuideHoldTicks <= 0) {
			heldGuidePeak = recentPeak;
			levelGuideHoldTicks = 4;
		} else {
			--levelGuideHoldTicks;
		}
	} else if (levelGuideHoldTicks > 0) {
		--levelGuideHoldTicks;
	} else {
		heldGuidePeak = -M_INFINITE;
	}

	const LevelGuideProfile &profile = getLevelGuideProfile(levelRole);
	const bool roleUnassigned = levelRoleAutomatic && levelRole == QStringLiteral("general");
	QString roleName = roleUnassigned ? QTStr("Basic.AudioMixer.LevelGuide.Role.Unassigned")
					  : QTStr(profile.nameKey);
	if (levelRoleAutomatic && !roleUnassigned)
		roleName = QTStr("Basic.AudioMixer.LevelGuide.Role.Auto").arg(roleName);
	QString state;
	QString stateText;
	QString peakText;

	if (roleUnassigned) {
		state = QStringLiteral("unassigned");
		stateText = QTStr("Basic.AudioMixer.LevelGuide.State.SetRole");
		if (hasAudioPeak(heldGuidePeak))
			peakText = QStringLiteral("%1 dBFS").arg(QString::number(heldGuidePeak, 'f', 1));
	} else if (obs_source_muted(source)) {
		state = QStringLiteral("muted");
		stateText = QTStr("Basic.AudioMixer.LevelGuide.State.Muted");
	} else if (!hasAudioPeak(heldGuidePeak)) {
		state = QStringLiteral("low");
		stateText = QTStr("Basic.AudioMixer.LevelGuide.State.Low");
		peakText = QStringLiteral("-inf dBFS");
	} else {
		peakText = QStringLiteral("%1 dBFS").arg(QString::number(heldGuidePeak, 'f', 1));
		if (heldGuidePeak >= -0.1f) {
			state = QStringLiteral("clip");
			stateText = QTStr("Basic.AudioMixer.LevelGuide.State.Clip");
		} else if (heldGuidePeak > profile.highDb) {
			state = QStringLiteral("hot");
			stateText = QTStr("Basic.AudioMixer.LevelGuide.State.Hot");
		} else if (heldGuidePeak >= profile.lowDb) {
			state = QStringLiteral("good");
			stateText = QTStr("Basic.AudioMixer.LevelGuide.State.Good");
		} else {
			state = QStringLiteral("low");
			stateText = QTStr("Basic.AudioMixer.LevelGuide.State.Low");
		}
	}

	QString text;
	const QFontMetrics fontMetrics(levelGuideLabel->font());
	const int availableTextWidth = std::max(40, levelGuideLabel->width() - 16);
	if (vertical) {
		const QString roleLine = fontMetrics.elidedText(roleName, Qt::ElideRight, availableTextWidth);
		const QString statusLine = peakText.isEmpty() ? stateText
							      : QStringLiteral("%1  %2").arg(stateText, peakText);
		text = QStringLiteral("%1\n%2")
			       .arg(roleLine, fontMetrics.elidedText(statusLine, Qt::ElideRight, availableTextWidth));
	} else {
		const QString fullText = peakText.isEmpty() ? QStringLiteral("%1  \u2022  %2").arg(roleName, stateText)
							    : QStringLiteral("%1  \u2022  %2  %3")
								      .arg(roleName, stateText, peakText);
		text = fontMetrics.elidedText(fullText, Qt::ElideRight, availableTextWidth);
	}

	const QString tooltip =
		roleUnassigned
			? QTStr("Basic.AudioMixer.LevelGuide.Tooltip.Unassigned")
			: QTStr("Basic.AudioMixer.LevelGuide.Tooltip")
				  .arg(QString::number(profile.lowDb, 'f', 0),
				       QString::number(profile.highDb, 'f', 0));
	if (levelGuideLabel->text() != text)
		levelGuideLabel->setText(text);
	levelGuideLabel->setToolTip(tooltip);
	levelGuideLabel->setAccessibleName(text);
	levelGuideLabel->setAccessibleDescription(tooltip);

	if (levelGuideLabel->property("levelState").toString() != state) {
		levelGuideLabel->setProperty("levelState", state);
		levelGuideLabel->style()->unpolish(levelGuideLabel);
		levelGuideLabel->style()->polish(levelGuideLabel);
	}
}

void VolumeControl::setMuted(bool mute)
{
	OBSSource source = OBSGetStrongRef(weakSource());
	if (!source) {
		return;
	}

	bool prev = obs_source_muted(source);
	bool unassigned = isSourceUnassigned(source);

	obs_source_set_muted(source, mute);

	if (!mute && unassigned) {
		// Show notice about the source no being assigned to any tracks
		bool has_shown_warning =
			config_get_bool(App()->GetUserConfig(), "General", "WarnedAboutUnassignedSources");
		if (!has_shown_warning) {
			showUnassignedWarning(obs_source_get_name(source));
		}
	}

	auto undo_redo = [](const std::string &uuid, bool val) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.c_str());
		obs_source_set_muted(source, val);
	};

	QString text = QTStr(mute ? "Undo.Volume.Mute" : "Undo.Volume.Unmute");

	const char *name = obs_source_get_name(source);
	OBSBasic::Get()->undo_s.add_action(text.arg(name), std::bind(undo_redo, std::placeholders::_1, prev),
					   std::bind(undo_redo, std::placeholders::_1, mute), uuid, uuid);
}

void VolumeControl::setMonitoring(obs_monitoring_type type)
{
	OBSSource source = OBSGetStrongRef(weakSource());
	if (!source) {
		return;
	}

	obs_monitoring_type prevMonitoringType = obs_source_get_monitoring_type(source);
	obs_source_set_monitoring_type(source, type);

	auto undo_redo = [](const std::string &uuid, obs_monitoring_type val) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.c_str());
		obs_source_set_monitoring_type(source, val);
	};

	QString text = QTStr("Undo.MonitoringType.Change");

	const char *name = obs_source_get_name(source);
	OBSBasic::Get()->undo_s.add_action(text.arg(name),
					   std::bind(undo_redo, std::placeholders::_1, prevMonitoringType),
					   std::bind(undo_redo, std::placeholders::_1, type), uuid, uuid);
}

void VolumeControl::onSourceActiveChanged(bool active)
{
	processMixerState();

	mixerStatus().set(VolumeControl::MixerStatus::Active, active);

	OBSBasic *main = OBSBasic::Get();
	emit main->mixerStatusChanged(uuid);
}

void VolumeControl::processMixerState()
{
	OBSSource source = OBSGetStrongRef(weakSource());
	if (!source) {
		deleteLater();
		return;
	}

	bool unassigned = isSourceUnassigned(source);

	bool isActive = obs_source_active(source) && obs_source_audio_active(source);

	mixerStatus().set(VolumeControl::MixerStatus::Active, isActive);
	mixerStatus().set(VolumeControl::MixerStatus::Unassigned, unassigned);

	QSignalBlocker blockMute(muteButton);
	QSignalBlocker blockMonitor(monitorButton);

	bool showAsMuted = obsMuted || obsMonitoringType == OBS_MONITORING_TYPE_MONITOR_ONLY;
	bool showAsMonitored = obsMonitoringType != OBS_MONITORING_TYPE_NONE;
	bool showAsUnassigned = !obsMuted && unassigned;
	bool showWarningIcon = showAsUnassigned || obsMonitoringType == OBS_MONITORING_TYPE_MONITOR_ONLY;

	volumeMeter->setMuted((showAsMuted || showAsUnassigned) && !showAsMonitored);
	setUseDisabledColors(showAsMuted || !isActive);

	muteButton->setChecked(showAsMuted);
	monitorButton->setChecked(showAsMonitored);

	QString muteTooltip = showAsMuted ? QTStr("Unmute") : QTStr("Mute");
	muteButton->setToolTip(muteTooltip);

	QString monitorTooltip = showAsMonitored ? QTStr("Basic.AudioMixer.Monitoring.Disable")
						 : QTStr("Basic.AudioMixer.Monitoring.Enable");
	monitorButton->setToolTip(monitorTooltip);

	if (showWarningIcon) {
		muteButton->setIcon(getWarningIcon());
	} else if (showAsMuted) {
		muteButton->setIcon(getMutedIcon());
	} else {
		muteButton->setIcon(getUnmutedIcon());
	}

	if (showAsMonitored) {
		monitorButton->setIcon(getMonitorOnIcon());
	} else {
		monitorButton->setIcon(getMonitorOffIcon());
	}

	// Qt doesn't support overriding the QPushButton icon using pseudo state selectors like :checked
	// in QSS so we set a checked class selector on the button to be used instead.
	utils->toggleClass(muteButton, "checked", showAsMuted);
	utils->toggleClass(monitorButton, "checked", showAsMonitored);

	utils->toggleClass(muteButton, "mute-warning", showWarningIcon);

	style()->polish(muteButton);
	style()->polish(monitorButton);

	updateCategoryLabel();
}

void VolumeControl::handleMuteButton(bool mute)
{
	setMuted(mute);

	if (!mute && obsMonitoringType == OBS_MONITORING_TYPE_MONITOR_ONLY) {
		setMonitoring(OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
	}
}

void VolumeControl::handleMonitorButton(bool enableMonitoring)
{
	obs_monitoring_type newType = (enableMonitoring) ? OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT
							 : OBS_MONITORING_TYPE_NONE;

	setMonitoring(newType);
}

void VolumeControl::sliderChanged(int vol)
{
	OBSSource source = OBSGetStrongRef(weakSource());
	if (!source) {
		return;
	}

	float prev = obs_source_get_volume(source);

	obs_fader_set_deflection(obs_fader, float(vol) / FADER_PRECISION);
	updateText();

	auto undo_redo = [](const std::string &uuid, float val) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.c_str());
		obs_source_set_volume(source, val);
	};

	float val = obs_source_get_volume(source);
	const char *name = obs_source_get_name(source);
	OBSBasic::Get()->undo_s.add_action(QTStr("Undo.Volume.Change").arg(name),
					   std::bind(undo_redo, std::placeholders::_1, prev),
					   std::bind(undo_redo, std::placeholders::_1, val), uuid, uuid, true);
}

void VolumeControl::updateText()
{
	QString text;
	float db = obs_fader_get_db(obs_fader);

	if (db < -96.0f) {
		text = "-inf dB";
	} else {
		text = QString::number(db, 'f', 1).append(" dB");
	}

	volumeLabel->setText(text);

	OBSSource source = OBSGetStrongRef(weakSource());
	if (!source) {
		return;
	}

	bool muted = obs_source_muted(source);
	const char *accTextLookup = muted ? "VolControl.SliderMuted" : "VolControl.SliderUnmuted";

	QString sourceName = obs_source_get_name(source);
	QString accText = QTStr(accTextLookup).arg(sourceName);

	slider->setAccessibleName(accText);
}

void VolumeControl::setVertical(bool vertical_)
{
	if (vertical == vertical_) {
		return;
	}

	vertical = vertical_;

	setLayoutVertical(vertical);
	updateLevelGuide();
}

void VolumeControl::updateTabOrder()
{
	QWidget *prevFocus = firstWidget()->previousInFocusChain();
	QWidget *lastFocus = lastWidget()->nextInFocusChain();

	if (vertical) {
		setTabOrder(prevFocus, nameButton);
		setTabOrder(nameButton, slider);
		setTabOrder(slider, muteButton);
		setTabOrder(muteButton, monitorButton);
		setTabOrder(monitorButton, lastFocus);
	} else {
		setTabOrder(prevFocus, nameButton);
		setTabOrder(nameButton, muteButton);
		setTabOrder(muteButton, monitorButton);
		setTabOrder(monitorButton, slider);
		setTabOrder(slider, lastFocus);
	}
}

void VolumeControl::updateName()
{
	setName(sourceName);
}

void VolumeControl::setName(QString name)
{
	sourceName = name;

	muteButton->setAccessibleName(QTStr("VolControl.Mute").arg(name));
}

void VolumeControl::setMeterDecayRate(qreal q)
{
	volumeMeter->setPeakDecayRate(q);
}

void VolumeControl::setPeakMeterType(enum obs_peak_meter_type peakMeterType)
{
	volumeMeter->setPeakMeterType(peakMeterType);
}

void VolumeControl::enableSlider(bool enable)
{
	slider->setEnabled(enable);
}

void VolumeControl::setUseDisabledColors(bool greyscale)
{
	volumeMeter->setUseDisabledColors(greyscale);
}

void VolumeControl::setGlobalInMixer(bool global)
{
	if (mixerStatus().has(VolumeControl::MixerStatus::Global) != global) {
		mixerStatus().set(VolumeControl::MixerStatus::Global, global);

		OBSBasic *main = OBSBasic::Get();
		emit main->mixerStatusChanged(uuid);
	}
}

void VolumeControl::setPinnedInMixer(bool pinned)
{
	if (mixerStatus().has(VolumeControl::MixerStatus::Pinned) != pinned) {
		OBSSource source = OBSGetStrongRef(weakSource());
		if (!source) {
			return;
		}
		OBSDataAutoRelease priv_settings = obs_source_get_private_settings(source);
		obs_data_set_bool(priv_settings, "mixer_pinned", pinned);

		mixerStatus().set(VolumeControl::MixerStatus::Pinned, pinned);

		if (pinned) {
			// Unset hidden state when pinning controls
			setHiddenInMixer(false);
		}

		OBSBasic *main = OBSBasic::Get();
		emit main->mixerStatusChanged(uuid);
	}
}

void VolumeControl::setHiddenInMixer(bool hidden)
{
	if (mixerStatus().has(VolumeControl::MixerStatus::Hidden) != hidden) {
		OBSSource source = OBSGetStrongRef(weakSource());
		if (!source) {
			return;
		}
		OBSDataAutoRelease priv_settings = obs_source_get_private_settings(source);
		obs_data_set_bool(priv_settings, "mixer_hidden", hidden);

		mixerStatus().set(VolumeControl::MixerStatus::Hidden, hidden);

		OBSBasic *main = OBSBasic::Get();
		emit main->mixerStatusChanged(uuid);
	}
}

void VolumeControl::refreshColors()
{
	volumeMeter->refreshColors();
}

void VolumeControl::setLevels(const float magnitude[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
			      const float inputPeak[MAX_AUDIO_CHANNELS])
{
	if (volumeMeter) {
		volumeMeter->setLevels(magnitude, peak, inputPeak);
	}
}
