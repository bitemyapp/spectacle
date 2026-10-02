# KGlobalAccelD upstream patch archive

This directory preserves the current-master crash fix and its integrated
regression tests on GitHub, independently of the KDE Invent fork. It is a
development patch, not an installed component of the Spectacle package.

| Item | Revision |
| --- | --- |
| Upstream project | https://invent.kde.org/plasma/kglobalacceld |
| Base on upstream master | `5b7f39b88d33877aeecaf9e73e80cbf755c14f12` |
| Tested patch commit | `5b889e8b8b07e10ff29ae9460f58edf3dbdf62de` |
| Published branch | [fix/desktop-action-registration](https://invent.kde.org/theodorvaryag/kglobalacceld/-/tree/fix/desktop-action-registration) |
| CI | [Successful Invent pipeline 1365998](https://invent.kde.org/theodorvaryag/kglobalacceld/-/pipelines/1365998) |

Creating a service component can load the requested desktop action before
`addAction()` reaches its allocation. The patch reuses that action instead of
replacing it and leaving an orphaned key registration after removal. Its two
upstream test cases cover both application launch and a named desktop action,
including repeated registration, preserved defaults and key handling after
component removal.

## Reconstruct the branch

With the normal KGlobalAccelD development dependencies installed:

```sh
git clone https://invent.kde.org/plasma/kglobalacceld.git
cd kglobalacceld
git switch -c fix/desktop-action-registration 5b7f39b88d33877aeecaf9e73e80cbf755c14f12
git am /path/to/spectacle/tools/upstream/0001-Reuse-desktop-actions-loaded-during-component-creation.patch
cmake -S . -B build -DBUILD_TESTING=ON -DWITH_X11=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R 'shortcutstest|allowlisttest|appstreamtest'
```

`git am` preserves the author and change; the resulting commit ID can differ
because the committer timestamp changes. Do not install this build over the
distribution's library solely to restore Spectacle: its native client shortcut
already avoids the affected desktop-service registration path.

Validation on the recorded base passed 27 shortcut cases, 6 allowlist cases and
the AppStream test. Both newly added cases failed without the fix. A separate
`migrateconfigtest` failure (14 actual groups versus 16 expected) reproduced
identically with and without the patch. The published Invent CI pipeline passed.

The older [v6.7.5 diagnostic patch](../kglobalacceld-registration.patch) and
[isolated registration probe](../shortcut-registration-probe.cpp) remain for
the original crash investigation. Use the patch in this directory for the
upstream-master branch. The latest upstream patch was prepared for the user's
submission; this archive does not submit a merge request or post a message.

The dotfiles repository's
[recovery guide](https://github.com/bitemyapp/dotfiles/blob/arch-idempotent-shells/.install/RECOVERY.md)
records the rest of the Arch/CachyOS desktop setup. This archive contains no
machine diagnostics, core dumps, screenshots, account tokens or SSH key files.
