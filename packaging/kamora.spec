Name:           kamora
Version:        0
Release:        1%{?dist}
Summary:        Scheduled borg backups to a USB drive

License:        GPL-3.0-or-later
URL:            https://github.com/ramanenka/kamora
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  extra-cmake-modules
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qttools-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kconfig-devel
BuildRequires:  kf6-kdbusaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kiconthemes-devel
BuildRequires:  kf6-kirigami-devel
BuildRequires:  kf6-knotifications-devel
BuildRequires:  kf6-kstatusnotifieritem-devel
BuildRequires:  kf6-solid-devel

Requires:       borgbackup
Requires:       kf6-kirigami
Requires:       kf6-qqc2-desktop-style
Requires:       breeze-icon-theme
Requires:       hicolor-icon-theme

%description
A Kirigami application that keeps a borg repository on a USB drive up to date.
It watches for the drive by the UUID of its filesystem, mounts it when it shows
up, and runs the backup if one is due.

%prep
%autosetup

%build
%cmake -GNinja -DKAMORA_DEV=OFF -DKAMORA_VERSION=%{version}
%cmake_build

%install
%cmake_install

%files
%doc README.md
%{_bindir}/kamora
%{_datadir}/applications/io.github.ramanenka.kamora.desktop
%{_datadir}/knotifications6/kamora.notifyrc
%{_datadir}/icons/hicolor/scalable/apps/io.github.ramanenka.kamora.svg
%{_datadir}/icons/hicolor/scalable/status/io.github.ramanenka.kamora-tray*.svg
%{_datadir}/icons/hicolor/22x22/status/io.github.ramanenka.kamora-tray*.svg

%changelog
