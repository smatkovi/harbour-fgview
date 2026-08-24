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
    rpm/harbour-fgview.spec \
    harbour-fgview.desktop

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172
