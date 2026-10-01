# Maintainer: bitemyapp <320177+bitemyapp@users.noreply.github.com>
# Arch's Spectacle dependency list, with Tesseract removed.
pkgname=spectacle-snappy-git
pkgver=6.7.5.r1
pkgrel=1
epoch=1
pkgdesc='Spectacle without OCR, with a lightweight resident region-to-clipboard selector'
arch=(x86_64 aarch64)
url='https://github.com/bitemyapp/spectacle'
license=(GPL-2.0-or-later)
depends=(glibc libgcc libstdc++ kconfig kconfigwidgets kcoreaddons kcrash
         kdbusaddons kglobalaccel kguiaddons ki18n kio kirigami kjobwidgets
         knotifications kpipewire kquickimageeditor kservice kstatusnotifieritem
         kwidgetsaddons kwindowsystem kxmlgui layer-shell-qt libxcb opencv prison
         purpose qt6-base qt6-declarative qt6-imageformats qt6-multimedia qt6-wayland
         wayland xcb-util xcb-util-cursor xcb-util-image)
makedepends=(cmake git extra-cmake-modules plasma-wayland-protocols)
provides=("spectacle=1:6.7.5" spectacle-fast)
conflicts=(spectacle spectacle-fast-git)
source=("spectacle::git+$url.git#branch=fast-region")
sha256sums=('SKIP')

pkgver() {
    cd spectacle
    printf '6.7.5.r%s.g%s' "$(git rev-list --count 343a0229b4cbb7961ae915629eaa687f8ef81eaa..HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
    cmake -S spectacle -B build -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_TESTING=ON -DSPECTACLE_FAST_LIVE_TESTING=OFF
    cmake --build build
}

check() {
    ctest --test-dir build --output-on-failure
}

package() {
    DESTDIR="$pkgdir" cmake --install build
}
