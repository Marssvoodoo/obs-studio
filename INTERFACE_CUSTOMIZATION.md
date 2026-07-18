# Interface Customization and Mixer Guide

This build adds a movable program preview, a role-aware peak guide, an Ignants-inspired theme, and more predictable dock sizing. The changes stay within OBS's existing dock and source-settings systems, so layouts and per-source choices continue to use the normal OBS configuration files.

## Ignants Voodoo theme

Select **Ignants Voodoo** under **Settings > Appearance > Theme**. The theme extends Yami and replaces its accent palette with the dark red, charcoal, white, green, and amber colors used by Ignants.

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

The peak guide is a monitoring aid. It does not change gain, add filters, calculate integrated LUFS, or install a limiter. Final program loudness and true-peak compliance still need to be checked on the program output.

## Restream docks

When the Restream service is active, Chat, Stream Information, and Channels continue to use the automatic Restream dock recovery path. Their content minimums are intentionally modest so the docks can participate in compact layouts without destabilizing adjacent columns.

## Local plugin compatibility

The accepted Windows setup keeps the Logitech OBS module and Downstream Keyer disabled. The Logitech module fast-failed after an otherwise complete shutdown, while Downstream Keyer failed during source cleanup when combined with the current streaming plugin set. The waveform plugin is also disabled because its installed binary does not load in this OBS build. These are user-level plugin manager settings, not source defaults; retest a clean launch and close before re-enabling any of them after an update.

## Validation

For dock changes, test both outer separators independently and confirm that the opposite outer column keeps the same width. After a drag, leave the window idle and confirm that no dock changes position or size on its own. A clean close and relaunch should restore the saved layout without adding duplicate Restream docks.
