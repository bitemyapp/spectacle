# Shortcut registration crash on Plasma 6.7.5

The first setup scripts directly called `doRegister` for a new desktop
component's `_launch` action. This exposes an upstream KGlobalAccelD bug, and
our subsequent launcher migration left a stale key in the running registry.
Pressing Alt+Shift+4 then crashed KWin before the screenshot code was invoked.
The compositor restart disconnected its Wayland clients.

The installed crash trace and an isolated reproduction both reached:

```text
Component::uniqueName()
GlobalShortcutsRegistry::processKey(134217764, Pressed)
GlobalShortcutsRegistry::keyEvent(...)
```

## Cause and correction

In [KGlobalAccelD v6.7.5](https://invent.kde.org/plasma/kglobalacceld/-/tree/v6.7.5),
`KGlobalAccelDPrivate::addAction` calls `getOrCreateComponent`. For a new
desktop service that call loads its desktop actions, including `_launch`, and
registers their keys. `addAction` then creates another action with the same
name. The context map replaces the first pointer without unregistering its
key. Removing the visible action and its empty component leaves the old key
pointing to a destroyed context. This is why invoking the visible action
directly appeared to work while a real key press crashed.

The fork now owns a normal `QAction`, `CaptureRegion`, in the **spectacle-fast**
client component through `KF6::GlobalAccel`. It does not register desktop
service `_launch` actions. Keys dispatch directly to the resident process;
there is no command launcher. Auto-repeat is disabled. KDE's standard client
library manages action lifetime and re-registration.

Setup uses only the existing client action. On detecting an earlier launcher,
it disables it, backs it up and asks for a logout before enabling the new key:
changing the visible action cannot clear an already orphaned registry entry.
Setup never restarts KWin itself. A fresh session, including one following a
compositor restart, clears the old in-memory registry.

An independent [candidate upstream patch](kglobalacceld-registration.patch)
reuses an action loaded during component creation. It is included for review;
the Spectacle package does not install or modify KWin or KGlobalAccelD.

## Isolated reproduction and regression checks

[shortcut-registration-probe.cpp](shortcut-registration-probe.cpp) uses KDE's
dummy keyboard backend and temporary configuration/data directories. Run it
only in a private D-Bus session with the offscreen Qt platform. It refuses an
existing shortcut service. It never injects keyboard input into the desktop.

Build against the upstream tag, with its normal development dependencies:

```sh
git clone --branch v6.7.5 https://invent.kde.org/plasma/kglobalacceld.git
cp /path/to/spectacle/tools/shortcut-registration-probe.cpp \
  kglobalacceld/autotests/registrationprobe.cpp
cd kglobalacceld
cat >> autotests/CMakeLists.txt <<'CMAKE'
add_executable(registrationprobe registrationprobe.cpp)
target_link_libraries(registrationprobe Qt::Test Qt::Widgets KF6::ConfigCore KF6::Service KGlobalAccelD dummyplugin)
CMAKE
cmake -S . -B build -DBUILD_TESTING=ON -DWITH_X11=OFF \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target registrationprobe --parallel

# Unpatched library: reproduces the crash in this private process.
dbus-run-session -- env QT_QPA_PLATFORM=offscreen gdb --batch \
  -ex run -ex bt --args build/bin/registrationprobe unsafe

# Unpatched library: the new client action passes.
dbus-run-session -- env QT_QPA_PLATFORM=offscreen \
  build/bin/registrationprobe native

# Candidate upstream fix: the formerly unsafe registration/removal now passes.
git apply /path/to/spectacle/tools/kglobalacceld-registration.patch
cmake --build build --target registrationprobe --parallel
dbus-run-session -- env QT_QPA_PLATFORM=offscreen \
  build/bin/registrationprobe unsafe
```

The native check passed 500 press/release cycles across five client lifetimes,
including modifier press/release, repeat suppression, and key disabling. The
punctuation event uses Alt+$, matching KWin's translated event after XKB consumes
Shift to produce `$`. These are dispatcher tests, not physical hardware tests.

In the real session, setup passed twice, the registered client action mapped
and cancelled twelve selectors, and KWin's process remained unchanged. No
screenshot was captured or clipboard replaced by those mapping tests. See
[PERFORMANCE.md](../PERFORMANCE.md) for timings and their limits.
