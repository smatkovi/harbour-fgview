Name:       harbour-fgview
Summary:    FlightGear viewer and controls for Sailfish OS
Version:    0.2.0
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
* Mon Aug 24 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.2.0-1
- Vollstaendig geladenes Archiv wird erkannt und direkt entpackt
- Fortschrittsanzeige beim Entpacken, gespeist aus der Verzeichnisgroesse
- Restzeit (ETA) aus aria2s Ausgabe uebernommen
- Fortschritt wird aus stderr gelesen, nicht nur aus stdout
- Sekundentakt haelt die Anzeige waehrend des Downloads aktuell
- aria2 und fgfs werden beim Schliessen der App sauber beendet
- Sandboxing=Disabled in der .desktop, sonst startet die App nicht
  ueber das Anwendungsgitter
- Requires auf fgfs-sailfish, curl und aria2
- Oberflaeche auf Englisch, Texte in qsTr() fuer Uebersetzungen

* Mon Aug 24 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.1.1-1
- Mirror-Aufloesung ueber curl statt QNetworkAccessManager: Qt 5.6 auf
  Sailfish OS ist gegen OpenSSL 1.0 gebaut und scheitert auf Systemen
  mit 1.1/3.x still an HTTPS
- SourceForge beantwortet aria2-Anfragen auf die /download-Weiterleitung
  mit 403; gegen die aufgeloeste Mirror-Adresse geht Segmentierung
- Selbstzuweisung der FgRuntime-Property behoben, die alle Bindings
  der Startseite ins Leere laufen liess

* Mon Aug 24 2026 Sebastian Matkovich <smatkovi@users.noreply.github.com> - 0.1.0-1
- Erste Fassung
- Zeigt die von FlightGear ueber Shared Memory gelieferten Frames an
- Neigungssteuerung fuer Quer- und Hoehenruder mit Kalibrierung auf
  die aktuelle Lage
- Gashebel, selbstzentrierendes Seitenruder, Fahrwerk, Klappen, Bremse
- Steuerbefehle per UDP an FlightGears generic-Protokoll
- FGData wird beim ersten Start mit aria2 nachgeladen
