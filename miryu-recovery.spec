Name:           miryu-recovery
Version:        45.0.0
Release:        1%{?dist}
Summary:        Plasma notification and rollback for booted Btrfs snapshots
License:        GPL-3.0-only
URL:            https://github.com/evernightvista/miryu-recovery
Source0:        https://github.com/evernightvista/miryu-recovery/archive/refs/heads/main.zip

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  extra-cmake-modules
BuildRequires:  qt6-qtbase-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kdbusaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-knotifications-devel
BuildRequires:  gettext
BuildRequires:  desktop-file-utils
BuildRequires:  systemd-rpm-macros

Requires:       btrfs-progs
Requires:       polkit
Requires:       systemd
Requires:       util-linux
Requires:       btrfs-assistant-evernight

%description
Miryu Recovery displays a native Plasma notification when the running system
is a Btrfs snapshot booted with systemd.volatile=overlay. It can restore the
currently booted snapshot through polkit and offers to restart afterwards.

%prep
%autosetup -n %{name}-main

%build
%cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_SYSCONFDIR=/etc
%cmake_build

%install
%cmake_install
%find_lang %{name}

%check
for po in po/*/%{name}.po; do
    msgfmt --check --check-compatibility -o /dev/null "$po"
done
desktop-file-validate %{buildroot}%{_sysconfdir}/xdg/autostart/%{name}.desktop

%files -f %{name}.lang
%{_libexecdir}/%{name}
%{_libexecdir}/%{name}-helper
%{_userunitdir}/%{name}.service
%{_sysconfdir}/xdg/autostart/%{name}.desktop
%{_datadir}/knotifications6/%{name}.notifyrc
%{_datadir}/applications/miryu-recovery.desktop
%{_datadir}/dbus-1/system-services/org.miryugaming.recovery.helper.service
%{_datadir}/dbus-1/system.d/org.miryugaming.recovery.helper.conf
%{_datadir}/polkit-1/actions/org.miryugaming.recovery.policy

%post
# Enable the user unit for every user, so it is started at login.
systemctl --global enable %{name}.service 2>/dev/null || :

%postun
if [ $1 -eq 0 ]; then
    # Last removal of the package: undo the global enable.
    systemctl --global disable %{name}.service 2>/dev/null || :
fi

%changelog
* Tue Nov 24 2026 KairikiFedora <13278297951@sina.cn> - 45.0.0-1
- Initial Miryu Recovery release
