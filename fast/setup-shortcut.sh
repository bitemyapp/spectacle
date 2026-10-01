#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

# The daemon owns a normal QAction. Do not doRegister a desktop-service _launch:
# Plasma 6.7.5 can leave a stale key when its implicit action is replaced.
for tool in gdbus systemctl; do
    command -v "$tool" >/dev/null || { echo "Required command missing: $tool" >&2; exit 1; }
done

component=spectacle-fast
action="['$component', 'CaptureRegion', 'Spectacle Fast', 'Copy Screenshot Region to Clipboard']"
launcher_dir="${XDG_DATA_HOME:-$HOME/.local/share}/kglobalaccel"
available=$(gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
    --method org.kde.KGlobalAccel.isGlobalShortcutAvailable 134217764 "$component")
current=$(gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
    --method org.kde.KGlobalAccel.shortcut "$action")
legacy_found=0
for legacy in spectacle-region-clipboard.desktop spectacle-fast-region-clipboard.desktop; do
    legacy_action="['$legacy', '_launch', 'Copy Screenshot Region to Clipboard', 'Launch']"
    legacy_current=$(gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
        --method org.kde.KGlobalAccel.shortcut "$legacy_action")
    if [[ "$legacy_current" == '([134217764],)' ]] || [[ -f "$launcher_dir/$legacy" ]]; then
        # A previously damaged registry cannot be repaired by changing its
        # visible action: an orphaned key may still exist. Require a fresh KWin
        # before enabling another binding. Never restart the user's compositor.
        gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
            --method org.kde.KGlobalAccel.setShortcut "$legacy_action" '[]' 4 >/dev/null
        if [[ -f "$launcher_dir/$legacy" ]]; then
            mv "$launcher_dir/$legacy" "$launcher_dir/$legacy.backup.$(date +%Y%m%d%H%M%S).$$"
        fi
        legacy_found=1
    fi
done
if [[ "$legacy_found" == 1 ]]; then
    systemctl --user disable --now spectacle-fast.service
    echo 'Removed the old launcher. Log out and back in, then run spectacle-fast-setup again. Do not press Alt+Shift+4 before that: KWin may still hold a stale key.' >&2
    exit 1
fi
if [[ "$available" != '(true,)' && "$current" != '([134217764],)' ]]; then
    echo 'Alt+Shift+4 is already assigned to another action. Choose a free key in System Settings > Keyboard > Shortcuts.' >&2
    exit 1
fi

systemctl --user daemon-reload
systemctl --user enable --now spectacle-fast.service
# The application registers CaptureRegion through KF6::GlobalAccel at startup.
# Wait for that existing action, never create a service component ourselves.
for ((attempt=0; attempt<40; attempt++)); do
    registered=$(gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
        --method org.kde.KGlobalAccel.allActionsForComponent "$action")
    if [[ "$registered" == *"'CaptureRegion'"* ]]; then
        break
    fi
    sleep 0.05
done
if [[ "$registered" != *"'CaptureRegion'"* ]]; then
    echo 'The installed daemon did not register CaptureRegion. Check systemctl --user status spectacle-fast.service.' >&2
    exit 1
fi
# NoAutoloading (4) changes the existing action; presence is owned by the daemon.
assigned=$(gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
    --method org.kde.KGlobalAccel.setShortcut "$action" '[134217764]' 4)
if [[ "$assigned" != '([134217764],)' ]]; then
    echo "Could not assign Alt+Shift+4: $assigned" >&2
    exit 1
fi
echo 'Alt+Shift+4: drag a region and release to copy. Escape or right-click cancels.'
