#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

# Run as the desktop user after installing. Use a new action identity when
# migrating the earlier full-Spectacle launcher: KWin caches its old Exec.
# Re-running preserves the fast action identity and backs up changed launchers.
for tool in gdbus kbuildsycoca6 systemctl; do
    command -v "$tool" >/dev/null || { echo "Required command missing: $tool" >&2; exit 1; }
done

component=spectacle-fast-region-clipboard.desktop
action="['$component', '_launch', 'Copy Screenshot Region to Clipboard', 'Launch']"
legacy_action="['spectacle-region-clipboard.desktop', '_launch', 'Copy Screenshot Region to Clipboard', 'Launch']"
available=$(gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
    --method org.kde.KGlobalAccel.isGlobalShortcutAvailable 134217764 "$component")
current=$(gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
    --method org.kde.KGlobalAccel.shortcut "$action")
legacy_current=$(gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
    --method org.kde.KGlobalAccel.shortcut "$legacy_action")
if [[ "$available" != '(true,)' && "$current" != '([134217764],)' && "$legacy_current" != '([134217764],)' ]]; then
    echo 'Alt+Shift+4 is already assigned to another action. Choose a free key in System Settings > Keyboard > Shortcuts.' >&2
    exit 1
fi

launcher_dir="${XDG_DATA_HOME:-$HOME/.local/share}/kglobalaccel"
mkdir -p "$launcher_dir"
launcher="$launcher_dir/$component"
temporary=$(mktemp "$launcher_dir/.spectacle-fast.XXXXXX")
trap 'rm -f "$temporary"' EXIT
cat > "$temporary" <<'DESKTOP'
#!/usr/bin/env xdg-open
[Desktop Entry]
Type=Application
Name=Copy Screenshot Region to Clipboard
Comment=Copy a selected region on release using Spectacle Fast; Escape cancels
Exec=gdbus call --session --dest org.kde.Spectacle.Fast --object-path /org/kde/Spectacle/Fast --method org.kde.Spectacle.Fast.Capture
Icon=spectacle-fast
StartupNotify=false
Terminal=false
X-KDE-GlobalAccel-CommandShortcut=true
X-KDE-Shortcuts=Alt+$
DESKTOP
if [[ ! -f "$launcher" ]] || ! cmp -s "$temporary" "$launcher"; then
    if [[ -e "$launcher" ]]; then
        cp -p "$launcher" "$launcher.backup.$(date +%Y%m%d%H%M%S).$$"
    fi
    chmod 755 "$temporary"
    mv "$temporary" "$launcher"
fi

# Remove only our previous region action if it still owns this key. Keeping a
# .desktop file with its old X-KDE-Shortcuts would re-register it at next login.
if [[ "$legacy_current" == '([134217764],)' ]]; then
    legacy_launcher="$launcher_dir/spectacle-region-clipboard.desktop"
    if [[ -e "$legacy_launcher" ]]; then
        mv "$legacy_launcher" "$legacy_launcher.backup.$(date +%Y%m%d%H%M%S).$$"
    fi
    gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
        --method org.kde.KGlobalAccel.unregister spectacle-region-clipboard.desktop _launch >/dev/null
fi

kbuildsycoca6 --noincremental
systemctl --user daemon-reload
systemctl --user enable --now spectacle-fast.service
gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
    --method org.kde.KGlobalAccel.doRegister "$action" >/dev/null
# SetPresent (2) activates the key immediately; NoAutoloading (4) saves the
# requested key instead of restoring a previous value from configuration.
gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
    --method org.kde.KGlobalAccel.setShortcut "$action" '[134217764]' 6 >/dev/null
echo 'Alt+Shift+4: drag a region and release to copy. Escape or right-click cancels.'
