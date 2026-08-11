# Voodoo OBS Studio 32.2.1-voodoo1

This maintenance release moves the production fork to the exact OBS Studio
`32.2.1` foundation while preserving its custom mixer, routing, VST2/VST3,
workspace, Draw Dock, Restream, performance, and security features.

## Upstream maintenance

- Fixed game capture after an OBS update leaves the previous hook in use by
  resolving the injection helper and hook DLL to absolute paths.
- Added detailed logging for hook installation and update failures.
- Fixed deprecated source ordering in the Add Source dialog.
- Fixed default source-thumbnail positioning.

## Windows DLL resolution

- Removed the inherited `PreferSystem32Images` mitigation that could make OBS
  or a child process load a system-installed FFmpeg or dependency DLL instead
  of the copy shipped with the application.
- Retained explicit default DLL directories, absolute game-capture paths, DEP,
  ASLR, extension-point protection, strict-handle checks, and font blocking.

## Compatibility

- The custom audio mixer, monitor buses, six sends, VST2/VST3 hosting and
  scanners, Draw Dock, Restream docks, vertical canvas, dock layouts, and Voodoo
  theme remain part of the fork.
- OBS 32.2 requires NVIDIA driver 570 or later for the NVIDIA SDK 13 encoder
  stack.
