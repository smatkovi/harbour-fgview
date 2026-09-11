TARGET = harbour-fgview

CONFIG += sailfishapp sailfishapp_i18n c++14

QT += quick gui sensors network

SOURCES += src/harbour-fgview.cpp

HEADERS += src/fgruntime.h

LIBS += -lrt

DISTFILES += \
    qml/harbour-fgview.qml \
    qml/pages/FlightPage.qml \
    qml/pages/StartPage.qml \
    qml/pages/SettingsPage.qml \
    qml/pages/FlatButton.qml \
    qml/pages/AirportCountryPage.qml \
    qml/pages/AirportSizePage.qml \
    qml/pages/AirportListPage.qml \
    qml/pages/AircraftPage.qml \
    qml/pages/HangarPage.qml \
    rpm/harbour-fgview.spec \
    harbour-fgview.desktop

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

# Per-country airport lists, generated once from FGData's apt.dat.gz by
# make-airports.py.  Shipped rather than parsed at run time: apt.dat is
# 27 MB compressed with 27000 airports, and the picker needs it in the
# first second, not after a minute of parsing.
airports.files = airports
airports.path = /usr/share/$${TARGET}
INSTALLS += airports
