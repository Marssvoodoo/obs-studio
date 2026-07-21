# Acknowledgements

Voodoo OBS Studio builds on the work of OBS contributors and the people whose
changes were incorporated directly into this fork. Git authorship and copyright
notices remain the canonical attribution record; this page makes those credits
visible from the project page and release documentation.

## Project direction and development

- [Marcus Booker](https://github.com/Marssvoodoo) owns the fork, defines its
  production direction, tests it in the live streaming setup, and approves its
  releases.
- **ChatGPT Sol 5.6** and **Fable** provide development, review, release, and
  documentation assistance under the project owner's direction.
- Earlier commits retain their original co-author trailers for Codex and Claude
  Opus releases where those systems contributed.

## VST3 foundation

- [pkv](https://github.com/pkviet) provided the original OBS VST3 host
  foundation used by this branch, along with monitoring-deduplication work that
  the fork retained.
- [Steinberg Media Technologies](https://github.com/steinbergmedia/vst3sdk)
  provides the VST 3 SDK. Its copyright and license notices remain intact.

## Directly retained and cherry-picked contributions

These contributors have authored commits that remain unique in the fork's
history relative to its OBS 32.2 RC2 base:

- [Warchamp7](https://github.com/Warchamp7) — mixer and frontend behavior,
  styling, shutdown handling, and obs-websocket updates.
- [tfo](https://github.com/xtfo) — graphics overflow corrections and NVENC
  resource cleanup.
- [Ryan Foster](https://github.com/RytoEX) — obs-websocket and C++20 build
  maintenance.
- [Sebastian Beckmann](https://github.com/sebastian-s-beckmann) — frontend
  safety, crash-handler behavior, and VST2 API maintenance.
- [Henri Kulotie](https://github.com/Dankirk) — Windows IPC shutdown and
  cancellation handling.
- [derrod](https://github.com/derrod) — canvas control and inactive-canvas
  behavior.
- [Dennis Sädtler](https://github.com/dsaedtler) — canvas video reset and
  restore correctness.
- [Hoshino Lina](https://github.com/hoshinolina) — process-pipe file-descriptor
  safety.
- [Joel Bethke](https://github.com/Fenrirthviti) — contributor-documentation
  link maintenance.
- [Richard Stanway](https://github.com/notr1ch) — Windows process mitigation
  policies.
- [shiina424](https://github.com/shiina424) — system-theme control styling.

## OBS Project and ecosystem

The fork is based on [OBS Studio](https://github.com/obsproject/obs-studio) and
retains the OBS Project's full [author list](AUTHORS), source history, copyright
notices, and GNU General Public License terms. Restream, Aitum Vertical, Draw
Dock, and the wider OBS plug-in community also provide integrations used by the
maintainer's production setup; their respective projects retain ownership and
credit for that work.
