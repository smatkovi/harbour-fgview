Name:       harbour-fgview
Summary:    FlightGear viewer and controls for Sailfish OS
Version:    0.9.10
Release:    1
License:    GPLv2+
URL:        https://github.com/smatkovi/harbour-fgview
Source0:    %{name}-%{version}.tar.bz2
Source100:  harbour-fgview.yaml

Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   qt5-qtdeclarative-import-sensors
Requires:   nemo-qml-plugin-configuration-qt5
Requires:   fgfs-sailfish >= 2020.3.19-9
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
* Fri Sep 11 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.9.10-1
- Settings: "Update scenery in flight" turns on FlightGear's own TerraSync,
  pointed straight at a mirror (its server discovery is a DNS NAPTR lookup
  that mobile resolvers refuse): tiles around the aircraft are fetched and
  loaded as it flies, so leaving the pre-fetched area no longer ends over
  water. Off by default - it needs a connection in flight and fetches the
  shared models and airport data once.

* Fri Sep 11 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.9.9-1
- A start with the scenery already on the device no longer waits for a
  comparison with the servers: tiles on disk without a finished-run record
  start the simulator at once (fgfs-scenery --check exit 2). The fetch
  covers Terrain and Objects, the tile-organised trees; the world-wide
  Airports and Models trees are left out - walking them held the start
  for twenty minutes with nothing on screen.
- Settings: "Real weather" fetches the current METAR at start and keeps it
  updated in flight (--enable-real-weather-fetch); off by default, as it
  needs a connection while flying.

* Fri Sep 11 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.9.8-1
- Scenery is checked before every start, offline: fgfs-run (fgfs-sailfish
  2020.3.19-8) asks fgfs-scenery whether a finished download covers the
  departure airport and only fetches when it does not. The status line
  says "present", "fetching" with aria2's progress, "fetched", or that the
  fetch failed and the start goes on with what is there. A settings switch
  "Refresh scenery on every start" forces the comparison with the servers.
- The simulator's stderr is read as well: the scenery progress lines come
  from there.

* Fri Sep 11 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.9.7-1
- Scenery is fetched before a start: the airport lists now carry
  coordinates, and the simulator is started with --region around the
  departure airport, so fgfs-run pulls the TerraSync tiles one degree each
  way (about half a gigabyte, only what is missing on later starts) and
  the status line shows the download. A fresh device showed nothing but
  water - the development phone's scenery had been fetched by hand.
- The camera no longer jumps between two directions on a fast drag: all
  telnet traffic goes over one connection kept open, so commands arrive in
  order (a connection per command set let FlightGear serve an older view
  offset after a newer one), and drag updates are coalesced to one every
  50 ms.

* Fri Sep 11 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.9.6-1
- Engine start for any aircraft: one Nasal script over the telnet channel
  cranks every engine the simulator has, with the piston items (magnetos,
  mixture, primer) and the turbine items (cutoff off, starter on), and
  holds each starter until that engine reports running, up to ninety
  seconds. Before, only engine[0] was cranked and the starter dropped after
  eight fixed seconds - a JSBSim turbine aborts its start the moment that
  happens, so the A320's engines never came up and a multi-engined
  aircraft ran on one engine. Aircraft with their own start-up automation
  (the A320 family's acconfig) are handed to it; the simulator is started
  with --allow-nasal-from-sockets for this.
- One throttle field per engine, eight of them: a six-engined An-225 with
  the lever wired to engine[0] alone taxied on one engine.
- The control protocol is written by the application itself instead of
  being copied from the runtime package, so sender and field order cannot
  drift apart.

* Wed Sep 10 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.9.5-1
- The look-to-the-side buttons are arrows without a caption and sit in the
  control column at the edge instead of over the middle of the picture,
  where they took up viewport and caught drags meant for the camera.

* Wed Sep 10 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.9.4-1
- The hangar says what happened after a download, and keeps saying it for a
  few seconds. A 22 MB aircraft on a good line is done in five seconds, so
  the bar was seen once at nought and the window then vanished - which reads
  as a failure even though the aircraft had been installed. The message can
  be tapped away.
- The download line carries the megabyte count as well as the percentage, so
  that a download too fast to fill the bar still visibly moves.

* Wed Sep 10 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.9.3-1
- A download from the hangar now says so. It did not before: the busy state
  is read from the download process, and the change was announced before the
  process was started, so the view bound to "idle" and heard nothing further
  until the download had already finished.
- The hangar shows the percentage and the rate rather than only a spinner:
  aircraft run to tens of megabytes - the Cub is 66 MB - and a spinner with
  no number looks the same as a hang. Downloaded with aria2c for the
  progress it reports; the bar appears only while there is a real
  percentage, not while the catalogue is being fetched or the zip unpacked.
- The aircraft page repeats the message, because a download started in the
  hangar keeps running after leaving that page.

* Wed Sep 10 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.9.2-1
- Aircraft are chosen from what is actually installed, found by scanning for
  -set.xml under FGData and under the download directory, and named by their
  own description. The three fixed entries this replaces included "j3cub",
  which is neither in FGData nor in the catalogue - the Cub is called J3Cub -
  so choosing it started nothing at all. FGData itself carries only the
  c172p, with five variants, plus the ufo and mibs.
- A hangar page offers the 648 aircraft of the official FlightGear 2020
  catalogue, with a search field and the catalogue's own ratings for flight
  model and 3D model. Tapping one downloads its zip and unpacks it beside
  FGData - not inside, because FGData is replaced wholesale whenever the
  base data is fetched again - and FlightGear is pointed at it with
  --fg-aircraft. Installed aircraft can be removed again, but only those
  under that directory: the ones that came with the base data are not ours
  to delete.
- The catalogue is fetched with curl rather than Qt networking, for the same
  reason the base data is: Qt 5.6 here is built against an OpenSSL the
  system no longer has.

* Wed Sep 10 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.9.1-1
- Finds the GLES runtime under /home/.system/fgfs, where
  fgfs-sailfish-gles-9 puts it, and still accepts /opt so that a phone with
  the new application but the old runtime still starts

* Wed Sep 10 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.9.0-1
- Departure airport is picked by country, then large or small, then from the
  list. All 27486 airports of FGData's apt.dat are in there, sorted by
  runway length, with a search field on both lists; the previous chooser
  held five. Large means a hard runway of 1800 m or more. The lists are
  generated once at build time rather than parsed at start: apt.dat is 27 MB
  compressed. Airports whose identifier carries no ICAO prefix - the 6461
  American ones such as 07MT or 3C8 - are placed by their position instead,
  which left 526 of 27486 unfiled
- Starting the simulator goes straight to the cockpit
- One finger on the picture turns the view, in the cockpit or outside; two
  still change the field of view. Both gestures now share one touch area,
  because a separate one on top took the first touch point and the pinch was
  then never recognised
- Two buttons for looking left and right while held, letting go returns the
  view to where the drag had left it
- Every control can be used at once: the buttons, the throttle and the
  rudder were MouseAreas, which only ever see the first touch point, so the
  cockpit took exactly one input at a time
- Sound, off by default, switchable. It could never have worked before:
  OpenAL on this device is built with the PulseAudio backend only, and a
  PulseAudio client finds its socket through XDG_RUNTIME_DIR, which has to
  point at /run/display for Wayland - where there is no socket. PULSE_SERVER
  is now derived from the application's own environment. Measured at 2.7 ms
  per frame
- Vertex array objects for the GLES backends. Draw time is bound by the
  number of draw calls, not by fill rate - a quarter of the pixels leaves it
  unchanged - so carrying the per-drawable attribute setup in a VAO pays.
  Measured 74.1 -> 66.6 ms per frame at Vienna
- Settings page grouped into scenery, weather and time, traffic, flying and
  system, with buildings, visibility, clouds, time of day, auto-coordination
  and a frame rate cap added. Every default is what the simulator already
  did, so the page changes nothing until something is touched. A pulley menu
  resets them
- AI traffic is adjustable rather than only on or off. FlightGear compares
  the share against "rand() & 100", a bitwise and, so the random value can
  only ever be one of eight numbers; the percentages offered are the ones
  actually reachable. Off has to switch the manager off outright, because a
  share of zero still lets one schedule in eight through
- Trees off means random-vegetation off, not a density of zero: with the
  vegetation still enabled the tile loader keeps doing the placement work,
  and the loading stalls that produces cost far more than the trees do
- English throughout the flight page

* Sun Sep 06 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.8.4-1
- Button captions wrap instead of being cut off; Silica's Button keeps its
  label on one line, which the narrow column could not fit
- Two fingers on the picture change the field of view between 20 and 110
  degrees

* Sun Sep 06 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.8.3-1
- Flaps button walks the c172p's own detents - up, 10, 20, 30 degrees -
  instead of half steps that sit between them, which made the flaps look
  stuck at the first notch. A long press retracts them fully

* Sat Sep 05 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.8.2-1
- Roll axis less sensitive: 35 degrees for full aileron, against 18 for
  full elevator. Rolling the phone is a wider motion than pitching it

* Fri Sep 04 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.8.1-1
- Simulation settings page: trees, flight model rate, AI traffic, detail
  range, texture filtering, particles. Defaults are the values measured
  on the device, trees off. Thirty percent still reads as forest and
  takes a third off the frame time; the slider goes from none to full

* Fri Sep 04 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.8.0-1
- First release that flies: engine start, throttle, rudder and tilt
  steering all reach the aircraft, the c172p takes off and can be flown
- Tilt steering works on angles, symmetric around whatever attitude the
  phone was zeroed in; 18 degrees is full deflection. Top edge towards
  the pilot is pulling
- Control packets use a decimal point whatever the system locale; under
  a German locale the comma separated line carried twice the fields and
  nothing reached the aircraft as intended
- Engine start button back on the flight page, start sequence sets
  engine[0] as well as the c172p switch properties
- Flight page ignores the back gesture; the rudder slider is a horizontal
  swipe along the same edge. Closing the app is the system gesture
- "Sicht" cycles the views
- Optional start in the air with the engine running
- GLES3 is the default backend, draw on its own thread, flight model at
  60 Hz, traffic manager off. Needs fgfs-sailfish 2020.3.19-5 and
  fgfs-sailfish-gles 2020.3.19-7 or newer

* Fri Sep 04 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.7.10-1
- Pitch axis the other way round: top edge towards the pilot is pulling

* Fri Sep 04 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.7.9-1
- Tilt steering more direct: 18 degrees for full deflection, 3 percent
  dead zone, less expo. Seven degrees of tilt now give about thirty
  percent instead of eight

* Fri Sep 04 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.7.8-1
- Tilt steering works on angles: thirty degrees either side of the
  reference is full deflection, whatever the reference attitude. The old
  mapping assumed a flat reference and left twenty percent of elevator
  travel one way when the phone was held at forty degrees

* Fri Sep 04 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.7.7-1
- Rudder slider: right is right again; the earlier inversion followed a
  reading from a broken packet
- Tilt reference is taken a second after the flight page appears, not at
  the first sensor reading

* Fri Sep 04 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.7.6-1
- Control packets use a decimal point regardless of locale. Under the
  German locale the comma separated line carried twice the fields, the
  throttle arrived as 9943 and the engine never got fuel

* Thu Sep 03 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.7.5-1
- Rudder slider sign: right is right pedal now
- "Sicht" button cycles the views

* Thu Sep 03 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.7.4-1
- The flight page ignores the back gesture; the rudder slider is a
  horizontal swipe along the same edge. Closing the app is the system
  gesture from the top edge, as everywhere else
- Throttle also to current-engine, so the cockpit lever moves

* Thu Sep 03 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.7.3-1
- The traffic manager is off: --disable-ai-models did not cover it, and
  it put fifty scheduled aircraft into the scene
- Flight model at 60 Hz; plenty for a light aircraft, and the frame goes
  from 83 to 58 ms with the c172p

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
