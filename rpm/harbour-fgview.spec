Name:       harbour-fgview
Summary:    FlightGear viewer and controls for Sailfish OS
Version:    0.1.0
Release:    1
License:    GPLv2+
URL:        https://github.com/smatkovi/harbour-fgview
Source0:    %{name}-%{version}.tar.bz2
Source100:  harbour-fgview.yaml

Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   qt5-qtdeclarative-import-sensors
Requires:   fgfs-sailfish >= 2020.3.19
Requires:   curl
Requires:   aria2

BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Sensors)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  desktop-file-utils

%description
Zeigt die von FlightGear ueber ein Shared-Memory-Segment gelieferten
Frames an und sendet Steuerbefehle (Neigungssensor, Gashebel,
Seitenruder, Klappen, Fahrwerk, Bremse) per UDP an FlightGears
generic-Protokoll.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%qmake5_install

desktop-file-install --delete-original       \
  --dir %{buildroot}%{_datadir}/applications \
  %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
