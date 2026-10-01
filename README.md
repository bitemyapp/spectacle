# Spectacle, with a fast region-to-clipboard selector

Public fork of [KDE Spectacle](https://invent.kde.org/plasma/spectacle), based on Plasma **6.7.5**. This fork removes OCR and adds `spectacle-fast`, a small resident selector for **Plasma Wayland**.

Press **Alt+Shift+4**, drag a rectangle, and release to copy the image. Escape or right-click cancels. The selector has a crosshair, dimming outside the selection, and a thin outline. No toolbar, confirmation, editor, notification, or file save is involved.

## What changed

- OCR code, actions, settings, language scanning, and the Tesseract build dependency are removed from Spectacle.
- The fast path uses Qt raster windows and KWin's screenshot API. It never initializes SpectacleCore, QML, the annotation editor, OpenCV, or an application-owned OpenGL scene.
- On KWin ScreenShot2 API v5, selection appears before a screenshot is taken. Only the selected area is captured on release, with the selector excluded by KWin. The desktop stays live during selection.
- `--freeze` captures the desktop before selection. Older screenshot API versions automatically use this mode; it costs more startup time and memory.
- Selection buffers and native windows are released on mouse release; screenshots are discarded after copy/cancel. On glibc, unused heap is yielded after asynchronous cleanup.
- The clipboard owner retains one PNG and decodes a QImage on demand. It stays alive so the image can be pasted later. Klipper can keep its own history independently.

The regular Spectacle UI, annotations, recording, and X11 support remain available through `spectacle`. The fast selector targets Plasma Wayland and does not provide annotations, OCR, region adjustment after release, or screen recording.

## Arch Linux / CachyOS

Two PKGBUILDs are included. **Choose one**:

### Full OCR-free fork

Replaces the distribution's `spectacle` package and includes the fast selector:

```sh
git clone --branch fast-region https://github.com/bitemyapp/spectacle.git
cd spectacle
makepkg -si
spectacle-fast-setup
```

### Selector only

Coexists with the distribution's Spectacle. This does **not** remove OCR from the distribution's binary:

```sh
git clone --branch fast-region https://github.com/bitemyapp/spectacle.git
cd spectacle/packaging/fast
makepkg -si
spectacle-fast-setup
```

Both packages build from this fork's `fast-region` branch and run geometry/clipboard-format tests. They are VCS packages: rebuild to receive changes. They are not published to the AUR. This is a community fork, not a KDE or Arch release.

Run setup as your desktop user. It enables `spectacle-fast.service` and assigns **Alt+$**, KDE's representation of physical **Alt+Shift+4** on a US layout, to the daemon's normal `CaptureRegion` QAction. Key presses dispatch directly to the resident process; no command launcher or `gdbus` process is spawned. Another action's shortcut is not overwritten. For a different layout, set your preferred key under **Spectacle Fast** in System Settings → Keyboard → Shortcuts.

If setup finds a launcher from the earlier fork, it disables and backs up that launcher, then asks you to log out and back in before running setup again. The earlier direct desktop-action registration exposed a Plasma 6.7.5 registry bug; a damaged registry can retain a stale key until KWin restarts. Setup never restarts your compositor. See [the crash investigation and isolated reproducer](tools/SHORTCUT-CRASH.md). Fresh installations do not need a logout.

If you previously enabled the full Spectacle service solely for warm screenshots, disable that preload to avoid keeping both processes resident:

```sh
systemctl --user disable --now app-org.kde.spectacle.service
```

Do this only if you enabled that preload yourself. Print Screen can still launch the regular application when needed.

## Build without a package

The selector alone needs a C++20 compiler, CMake, Qt 6 Core/Gui/DBus/Test, KF6 GuiAddons and GlobalAccel, and the Qt Wayland platform plugin:

```sh
cmake -S . -B build-fast -DSPECTACLE_FAST_ONLY=ON \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
  -DBUILD_TESTING=ON
cmake --build build-fast --parallel
ctest --test-dir build-fast --output-on-failure
cmake --install build-fast
"$HOME/.local/bin/spectacle-fast-setup"
```

For the full application, omit `SPECTACLE_FAST_ONLY` and install the build dependencies listed in the root PKGBUILD. OpenCV 4.7 or newer is supported, including OpenCV 5. A local installation can coexist with a distribution package; it does not replace that package in pacman's database.

KWin authorizes the installed executable through `org.kde.spectacle.fast.desktop`. Rebuild the KDE service cache after installing or moving the executable:

```sh
kbuildsycoca6 --noincremental
```

No KWin permission checks need to be disabled.

## Commands

```sh
spectacle-fast           # activate the resident instance, or start and capture
spectacle-fast --daemon  # start without showing selection
spectacle-fast --freeze  # start a new instance in frozen-desktop mode

gdbus call --session --dest org.kde.Spectacle.Fast \
  --object-path /org/kde/Spectacle/Fast \
  --method org.kde.Spectacle.Fast.Capture
```

Mode is chosen when the resident process starts. To use frozen mode with the service, override its ExecStart, clear the previous ExecStart first, and restart the service. A repeated Capture while selection is active cancels it; it does not start overlapping capture operations.

## Measurements and validation

See [PERFORMANCE.md](PERFORMANCE.md) for methodology and limits. Results are software timings to KWin window mapping, **not** physical key-to-photon measurements. Try the shortcut and use an external camera for that measurement.

Unit tests cover native/fractional scale crops, reverse drags, negative monitor origins, mixed scales, transparent display gaps, and PNG/Qt clipboard formats. An optional private bridge, `SPECTACLE_FAST_LIVE_TESTING=ON`, supports synthetic mouse/key and clipboard checks in a real Plasma session. It is never installed by either package. The installed D-Bus interface exposes only Capture, Cancel, and State.

Report fork issues at [this repository](https://github.com/bitemyapp/spectacle/issues). Upstream project information is preserved in [README.upstream.md](README.upstream.md). Existing KDE copyright notices and licenses are retained.
