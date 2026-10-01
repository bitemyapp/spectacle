# Performance and memory measurements

Measured on CachyOS, Plasma/KWin 6.7.5, Qt 6.11.2, Intel graphics. These are individual desktop measurements, not a cross-hardware benchmark or a measurement of macOS.

## Final raster selector

| Display | First mapping | Repeated mapping, median | Range, 12 captures | Idle PSS | Selecting PSS | Idle RSS |
|---|---:|---:|---:|---:|---:|---:|
| 3840×2160, 240 Hz, scale 1.45 | 30.2 ms | 36.9 ms | 22.8–40.5 ms | 8.7 MiB | 72.0 MiB | 56.5 MiB |
| Earlier 2880×1800, 120 Hz, scale 2 run | 29.4 ms | 28.2 ms | 27.2–29.4 ms | 8.5 MiB | 28.3 MiB | 56.5 MiB |

The display changed between these runs; differences cannot be attributed solely to code. The final 4K run used the same production selector as the published fork. Latency starts before spawning `gdbus` and ends when KWin reports window mapping. It includes dispatch and initial software painting, but **does not measure physical key-to-photon latency**. A resident service is required to avoid cold process startup on every key press.

The 4K selection buffers are temporary. They are destroyed on release or cancel. After a 3190×1740 synthetic image was copied, decoded and pasted in another Qt application, the private test process returned to approximately **12 MiB PSS**. It used **zero CPU ticks during a three-second idle sample**. The private bridge, logging and test fixture differ from the production daemon; do not treat that value as its exact idle baseline.

The process had no DRM client memory entries during the final mapping/cancellation benchmark. This avoids Spectacle's application-owned editor GL surfaces. KWin still composites the overlay and owns graphics buffers: **8.7 MiB PSS is not a claim that the entire desktop screenshot operation uses 8.7 MiB of physical memory**. RSS counts resident shared library pages fully; PSS apportions them. Address-space size is not allocated physical RAM.

## Why the old path was large

Earlier tests on the internal 2880×1800 display found:

| Configuration after repeated captures/cancellations | Process PSS |
|---|---:|
| Full retained capture/editor UI, normal OCR | 254.2 MiB |
| Simplified QML UI, normal OCR | 246.7 MiB |
| Simplified QML UI, OCR initialization suppressed | 178.8 MiB |
| Same, unused heap yielded | 116.1 MiB |
| Same, CPU images released and heap yielded | 76.6 MiB |

Suppressing OCR initialization alone reduced the no-UI daemon from 88.9 to 33.2 MiB PSS in that diagnostic. The full retained editor also had 364.1 MiB of resident DRM GTT buffers, including shared buffers; simply adding that number to PSS would double-count some memory. These retained-editor prototypes are different from the distribution's unmodified idle daemon.

A 2880×1800 four-byte bitmap is 19.8 MiB. Spectacle's annotation document keeps an additional canvas; its editor uploads textures and mipmaps. Removing visible buttons while retaining that backend saves little. The separate raster selector avoids initializing the editor, QML, OCR and OpenCV on the shortcut path, then releases screen images, native windows and unused heap after each operation. Clipboard ownership retains a PNG rather than a second raw image. Temporary decoding for paste also triggers heap cleanup.

## Reproduce mapping and memory measurements

This briefly displays and cancels twelve selectors. It never takes a screenshot or changes the clipboard. Run in the graphical session with Python, `qdbus6`, `gdbus`, `kbuildsycoca6`, and the user journal available:

```sh
systemctl --user stop spectacle-fast.service
python tools/benchmark-fast.py /usr/bin/spectacle-fast --output result.json
systemctl --user start spectacle-fast.service
```

For an uninstalled build, pass `build-fast/fast/spectacle-fast`. The script temporarily registers the exact executable with KWin, disables launch feedback for that desktop entry, records mapping times and `/proc` memory, and removes the temporary entry/script afterward. Run without competing benchmarks or full-screen test fixtures. Repeated results naturally vary with compositor load and other activity.

## Correctness validation

The full build passed its three CTest suites: AppStream metadata, filename generation, and fast-selector geometry/clipboard formats. The selector-only build passed its Qt unit suite.

Private Plasma-session validation exercised live and frozen modes, RGB fixture captures including a 3190×1740 image at fractional scale, reverse drags, keyboard focus, Escape, repeated activation, pending cancellation, PNG and Qt-image paste in a separate process, capture-time semantics, and clipboard survival after fifteen seconds of cleanup. The original clipboard was restored after testing.

An apparent small pixel corruption was traced to the **test entry's launch-feedback icon**, not the screenshot pixels or area API. `StartupNotify=false` prevents that icon. The production entry and benchmark use it too. KWin's [activation feedback implementation](https://github.com/KDE/kwin/blob/Plasma/6.7/src/xdgactivationv1.cpp) checks this entry when issuing activation tokens. The installed service has no test input or clipboard-inspection API.
