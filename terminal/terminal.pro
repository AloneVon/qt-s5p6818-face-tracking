# terminal.pro — OPTIONAL Qt Creator project for editing/indexing/building the
# terminal. The Makefile is the canonical build. This is a pure C++ console app
# (no Qt libs): we drop Qt and link OpenCV + pthread via pkg-config.
#
# Host-dev build:  qmake CONFIG+=dev_host && make
# Cross build:     configure a Kit with the arm-linux-gnueabihf-g++ compiler.

TEMPLATE = app
TARGET   = terminal
CONFIG  += console c++17
CONFIG  -= app_bundle qt

# Recursively pick up all sources/headers under src/.
SOURCES += $$files(src/*.cpp, true)
HEADERS += $$files(src/*.h, true)

INCLUDEPATH += src

# OpenCV via pkg-config (opencv4, else legacy opencv).
CONFIG += link_pkgconfig
packagesExist(opencv4): PKGCONFIG += opencv4
else:                   PKGCONFIG += opencv

LIBS += -lpthread

# qmake CONFIG+=dev_host  -> host stubs (window display, cv::VideoCapture, no PWM)
dev_host: DEFINES += DEV_HOST

# qmake CONFIG+=tls  -> compile in SEC1-over-TLS + wss. Needs OpenSSL (>= 1.1.0:
# libssl/libcrypto) for the target; cross builds need it in the armhf sysroot.
tls {
    DEFINES  += SEC_ENABLE_TLS
    PKGCONFIG += openssl
}
