Name:       harbour-fgview
Summary:    FlightGear viewer and controls for Sailfish OS
Version:    0.7.2
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

%changelog
* Thu Sep 03 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.7.2-1
- DrawThreadPerContext for the GLES backends, single-threaded for Zink

* Wed Sep 02 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.7.1-1
- The engine start button is back on the flight page; it had been lost
  when the page was rebuilt
- The start sequence sets engine[0] as well as the c172p switch
  properties: throttle and mixture went only to the current-engine alias,
  which the engine model does not read, and cranks for eight seconds
- Tilt steering is on from the start

* Mon Aug 31 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.7.0-1
- GLES3 is the default backend: it renders scenery with textures, sun and
  fog on this device and measured faster than Zink
- New switch for starting in the air, off by default. FlightGear brings the
  engine up by itself at altitude; on the ground the c172p needs the full
  start-up procedure and its Nasal scripts reset magnetos and battery
  behind anything set from outside
- Protocol/fgtouch.xml is refreshed from the fgfs-sailfish package on every
  start. An older copy pointed the throttle at
  /controls/engines/current-engine/throttle, which the engine model does
  not read, so the throttle had no effect

* Mon Aug 24 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.3.0-1
- Engine start button. FlightGear's generic protocol can only set the
  control axes, so fuel selectors, battery, magnetos and primer are set
  over the telnet channel (port 5401), which fgfs now opens
- Brake button also releases the wheel brakes, not just the parking
  brake — they are separate properties and the aircraft would not roll
  with the wheel brakes still applied
- Throttle now writes to current-engine instead of engine[0]; the c172p
  reads the former, so the lever had no effect on engine power
- Cockpit controls translated to English

* Mon Aug 24 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.2.1-1
- Simulator output is captured and shown on a log page in the app
- Notable lines (scenery loading, JSBSim init, trim) surface in the
  status line, so the two-minute startup is no longer a blank wait
- Full output written to ~/.local/share/harbour-fgview/fgfs.log
- Child processes terminated on aboutToQuit instead of in the
  destructor, where Qt had already torn down the QProcess objects
  and left orphaned fgfs instances behind

* Mon Aug 24 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.2.0-1
- A fully downloaded archive is detected and extraction starts
  immediately instead of downloading again
- Progress bar during extraction, derived from the growing directory
- Remaining time (ETA) taken from aria2 output
- Progress read from stderr, not just stdout
- One-second tick keeps the display alive while downloading
- aria2 and fgfs shut down when the app closes
- Sandboxing=Disabled in the .desktop file; without it the app does
  not launch from the application grid
- Requires on fgfs-sailfish, curl and aria2
- Interface in English, strings wrapped in qsTr() for translations

* Mon Aug 24 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.1.1-1
- Mirror resolution via curl instead of QNetworkAccessManager: Qt 5.6
  on Sailfish OS is built against OpenSSL 1.0 and fails silently on
  HTTPS with 1.1/3.x on the system
- SourceForge answers aria2 requests to the /download redirect with
  403; segmentation works against the resolved mirror address
- Fixed a self-assignment of the FgRuntime property that left every
  binding on the start page evaluating to null

* Mon Aug 24 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.1.0-1
- First release
- Displays frames delivered by FlightGear through shared memory
- Tilt steering for ailerons and elevator, calibrated to the current
  device position
- Throttle lever, self-centering rudder, gear, flaps, brake
- Controls sent over UDP to FlightGear's generic protocol
- FGData downloaded on first start using aria2
