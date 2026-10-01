#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

# Run as the desktop user after installing. Re-running preserves the same action
# identity. Existing launcher contents are backed up before replacing them.
for tool in gdbus kbuildsycoca6 systemctl; do
    command -v "$tool" >/dev/null || { echo "Required command missing: $tool" >&2; exit 1; }
done

action="['spectacle-region-clipboard.desktop', '_launch', 'Copy Screenshot Region to Clipboard', 'Launch']"
available=$(gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
    --method org.kde.KGlobalAccel.isGlobalShortcutAvailable 134217764 spectacle-region-clipboard.desktop)
current=$(gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
    --method org.kde.KGlobalAccel.shortcut "$action")
if [[ "$available" != '(true,)' && "$current" != '([134217764],)' ]]; then
    echo 'Alt+Shift+4 is already assigned to another action. Choose a free key in System Settings > Keyboard > Shortcuts.' >&2
    exit 1
fi

launcher_dir="${XDG_DATA_HOME:-$HOME/.local/share}/kglobalaccel"
mkdir -p "$launcher_dir"
launcher="$launcher_dir/spectacle-region-clipboard.desktop"
temporary=$(mktemp "$launcher_dir/.spectacle-fast.XXXXXX")
trap 'rm -f "$temporary"' EXIT
cat > "$temporary" <<'DESKTOP'
#!/usr/bin/env xdg-open
[Desktop Entry]
Type=Application
Name=Copy Screenshot Region to Clipboard
Comment=Select a rectangle and copy on release; Escape cancels
Exec=gdbus call --session --dest org.kde.Spectacle.Fast --object-path /org/kde/Spectacle/Fast --method org.kde.Spectacle.Fast.Capture
Icon=spectacle-fast
StartupNotify=false
Terminal=false
X-KDE-GlobalAccel-CommandShortcut=true
X-KDE-Shortcuts=Alt+$
DESKTOP
if [[ ! -f "$launcher" ]] || ! cmp -s "$temporary" "$launcher"; then
    if [[ -e "$launcher" ]]; then
        cp -p "$launcher" "$launcher.backup.$(date +%Y%m%d%H%M%S)"
    fi
    chmod 755 "$temporary"
    mv "$temporary" "$launcher"
fi

kbuildsycoca6 --noincremental
systemctl --user daemon-reload
systemctl --user enable --now spectacle-fast.service
gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
    --method org.kde.KGlobalAccel.doRegister "$action" >/dev/null
gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel \
    --method org.kde.KGlobalAccel.setShortcut "$action" '[134217764]' 4 >/dev/null
echo 'Alt+Shift+4: drag a region and release to copy. Escape or right-click cancels.'
