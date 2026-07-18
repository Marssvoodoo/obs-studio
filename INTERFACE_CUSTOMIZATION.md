# Interface Customization and Mixer Guide

This build adds a movable program preview, saved dock presets, a role-aware peak guide, program-output loudness metering, a DAW-style Ignants theme, aux sends, monitor buses, and Windows VST3 effects. Layouts and per-source choices continue to use the normal OBS configuration files.

## Ignants Voodoo 2.0 theme

Select **Ignants Voodoo 2.0** under **Settings > Appearance > Theme**. The theme keeps the original `com.marssvoodoo.IgnantsVoodoo` identifier, so an existing Ignants Voodoo selection upgrades in place. It extends Yami with the dark red, charcoal, white, green, and amber colors used by Ignants.

Version 2.0 applies the palette beyond the basic accent color. Menus, tabs, dock headers, toolbars, buttons, scrollbars, status surfaces, scene selections, mixer channels, faders, mute and monitor controls, and the Program Audio row now share one visual hierarchy. Mixer category headers distinguish pinned, hidden, unassigned, inactive, and preview sources without relying on the old blue palette.

The mixer treatment is modeled after a compact DAW console: dark channel plates, high-contrast source names, recessed fader tracks, bordered fader caps, fixed numeric readouts, and clear green, amber, and red meter states. Dynamic selectors change colors only. They do not change margins, padding, border widths, or widget dimensions, which prevents hover, metering, role, live, and recording states from resizing the mixer or its dock columns.

The theme also gives the peak guide distinct states:

- grey: low or muted;
- green: inside the selected role's target range;
- amber: above the target range;
- red: clipping or a source that still needs a role.

## Movable preview

The normal Preview and Studio Mode editor now lives in the **Stream Preview / Program** dock. It can be moved, resized, floated, hidden, and restored like other OBS docks. **View > Docks > Reset UI** restores it if it is closed or moved off-screen.

The preview uses all available dock space without the old unused border. A zero-height central layout anchor keeps Qt's dock calculations valid without leaving a blank strip above or below the workspace.

## Dock resizing

The outer left and right separators resize only the side being dragged. During the drag, the opposite outer column is held at its current width; its normal minimum and maximum constraints are restored as soon as the mouse button is released. The middle workspace absorbs the size change until its docks reach their actual minimum widths.

Browser minimum sizes are applied to the browser content instead of the `QDockWidget` wrapper. This keeps Restream and custom browser docks usable without forcing an entire dock column to jump to the browser's preferred width.

### Dock layout presets

Open **Docks > Dock Layouts** to switch between two starting points:

- **Standard** restores the normal OBS arrangement.
- **Balanced 6 | 6 | 6** divides the window into equal left, middle, and right zones. Scenes and Sources share the left zone, Preview fills the upper middle, and Audio Mixer, Scene Transitions, and Controls form an equal row below it. Visible Restream, browser, custom, and Stats docks are retained as tabs in the right zone so a large dock set cannot force windows to float or collapse the workspace.

The balanced preset is a starting layout, not a lock. Every dock remains movable and resizable, and the resulting arrangement is saved through the normal OBS window state. Applying the preset again rebuilds the three-zone arrangement without creating duplicate browser docks.

## Peak guide

Each Audio Mixer channel has a fixed-size badge that reports its recent dBFS peak as **LOW**, **GOOD**, **HOT**, **CLIP**, or **MUTED**. The badge does not resize the mixer strip when its text changes.

Click the badge, or open the channel's context menu and choose **Peak Guide Role**, to assign a role. The choice is stored with the source. **Auto from source name** clears the stored override and returns to name-based detection.

| Role | Target peak range |
| --- | ---: |
| Stream mic / voice | -12 to -6 dBFS |
| Guest / co-host | -14 to -8 dBFS |
| Party / Discord chat | -20 to -10 dBFS |
| PS4 / console chat | -20 to -10 dBFS |
| Yells / reactions | -10 to -3 dBFS |
| Whisper / ASMR | -18 to -10 dBFS |
| TTS / voiceover | -14 to -8 dBFS |
| Game | -24 to -14 dBFS |
| Desktop / browser | -24 to -14 dBFS |
| Video / dialogue | -20 to -10 dBFS |
| Music | -30 to -20 dBFS |
| Alerts / notifications | -18 to -10 dBFS |
| Soundboard | -18 to -8 dBFS |
| Sound effects | -18 to -10 dBFS |
| Loud impact / explosion | -12 to -4 dBFS |
| Ambient / background | -36 to -24 dBFS |
| General | -20 to -10 dBFS |

Automatic detection recognizes common source terms such as Scarlett and Focusrite for the stream mic, Discord for party chat, `ps4chat` and Line In for console chat, and game, music, alert, soundboard, effect, video, desktop, and ambient names for their matching roles. Sources that do not match a known role show **UNASSIGNED** instead of presenting a generic target as authoritative.

The peak guide is a monitoring aid. It does not change gain, add filters, or install a limiter. Use the Program Output strip for final-mix loudness and true-peak checks.

## Program Output strip

The fixed **Program Output** row above the source channels measures the configured streaming mix. Simple Output uses Stream Mix 1. Advanced Output follows its selected streaming track. The main bar shows per-channel RMS and sample peak, while the lower row shows:

- **M**: momentary loudness over 400 ms;
- **S**: short-term loudness over 3 seconds;
- **I**: integrated loudness using the -70 LUFS absolute gate, a relative gate 10 LU below the absolute-gated result, and 400 ms blocks with 75% overlap;
- **MAX TP**: maximum true peak since reset using the 48-tap, four-phase interpolating filter specified by ITU-R BS.1770-5.

Use **Audio Mixer > Options > Show Program Audio strip** to hide or restore the row. The choice is saved with the normal interface settings. Its title, mix label, peak value, and status badge use fixed widths so live updates do not resize the mixer columns.

Press **RESET** to clear integrated loudness and maximum true peak. Starting a new stream or recording from idle also resets them. Measurements use one decimal place and support OBS's 44.1 kHz and 48 kHz audio rates. The implementation follows the BS.1770 and EBU measurement model but is not presented as a certified compliance meter.

The strip is deliberately read-only. It does not apply gain, mute the program, change routing, add a limiter, or alter encoder audio. A red true-peak readout warns that the measured maximum exceeded -1 dBTP; it does not claim to prevent the overage.

## Aux sends and monitor buses

Advanced Audio Properties keeps the six normal OBS track routes and adds an independent send level beneath each track checkbox. A new source starts at 0.0 dB on every enabled route, which is the same output as standard OBS. Lowering a send changes that source only on the selected bus; setting it to `-inf dB` silences the send without forgetting the route or its saved level.

Send gain is applied to each source before scene and group audio is combined. This makes the result consistent for direct sources, nested scenes, and duplicated scene items. The gain values are stored with each source in the scene collection and use atomic snapshots in the audio render path, so changing a control does not add a lock or allocation to the audio callback.

The **Monitor Bus** selector listens to one complete output bus through the monitoring device selected under **Settings > Advanced > Audio**. It is off by default and does not replace per-source Monitor Off, Monitor Only, or Monitor and Output settings. Leave bus monitoring off when the monitoring device is also captured by OBS; routing a captured device back to itself can create feedback.

## Windows VST3 effects

Add **VST3 Plugin** from a source's Filters window. Discovery is limited to the standard system and user VST3 folders. Each installed module is inspected in a separate helper process with a 15-second limit and a 768 MiB memory cap, so a crashing or stalled module is skipped without ending the rest of the scan. The validated list is saved incrementally, and OBS keeps the last valid cache if a later refresh fails.

The selector stores both the 32-character VST3 class ID and the canonical module path. State chunks are size-limited and checked before they are restored. Audio buses, latency, editor dimensions, restart requests, parameter changes, and sidechain delivery are bounded before reaching OBS.

Discovery is isolated, but an effect selected for live processing still runs native third-party code inside OBS. Use trusted 64-bit VST3 effects only. If a selected effect throws or returns invalid audio, the filter is bypassed; a process-level crash in the effect can still terminate OBS.

## Restream docks

When the Restream service is active, Chat, Stream Information, and Channels continue to use the automatic Restream dock recovery path. Their content minimums are intentionally modest so the docks can participate in compact layouts without destabilizing adjacent columns.

## Local plugin compatibility

The accepted Windows setup keeps the Logitech OBS module and Downstream Keyer disabled. The Logitech module fast-failed after an otherwise complete shutdown, while Downstream Keyer failed during source cleanup when combined with the current streaming plugin set. The waveform plugin is also disabled because its installed binary does not load in this OBS build. These are user-level plugin manager settings, not source defaults; retest a clean launch and close before re-enabling any of them after an update.

## Validation

For dock changes, apply both layout presets, then test both outer separators independently and confirm that the opposite outer column keeps the same width. After a drag, leave the window idle and confirm that no dock changes position or size on its own. A clean close and relaunch should restore the saved layout without adding duplicate Restream docks.

For Ignants Voodoo 2.0, exercise hover, focus, mute, monitor, role, Program Output peak, live, and recording states while watching the mixer and outer columns. Colors should change immediately, but channel widths, the Program Output row height, dock separators, and adjacent dock geometry must remain stationary. Also test the smallest practical mixer dock width and the supported OBS font and density scales before accepting a deployment.

For Program Output, a 1 kHz in-phase stereo sine wave with a -18 dBFS per-channel peak should read about -18.0 LUFS after the filters settle. Confirm that M appears after 400 ms, S after 3 seconds, RESET clears I and MAX TP, and silence does not add gated blocks. Treat MAX TP as a warning surface, not a limiter acceptance test.

For aux sends, verify that 0.0 dB matches the unmodified track, `-6.0 dB` measures approximately half amplitude, and `-inf dB` removes only that source from that bus. Repeat one check through a nested scene, save and reopen the scene collection, and confirm the values return. For bus monitoring, use a monitoring device that OBS is not capturing, switch through all six buses, then return the selector to Off and confirm that only normal per-source monitoring remains.

For VST3, refresh the list with no effect selected, confirm one known effect can open and close its editor, save and reopen its state, and test sidechain selection with a compatible effect. A scan is acceptable when responsive effects remain selectable and failed modules are reported as skipped; a single bad module must not abort discovery.
