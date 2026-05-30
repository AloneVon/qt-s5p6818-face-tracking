# pc-client.pro — Qt desktop client for the S5P6818 face-tracking terminal.
#
# Pure Qt, NO OpenCV: the SEC1 video stream is JPEG, which QImage decodes
# natively. We reuse terminal/src/net/Protocol.h (dependency-free) as the single
# source of truth for the wire framing.
#
# Build:  qmake && make        (Qt 5.15, qmake in PATH)
# Run:    ./sec-pc-client.app/Contents/MacOS/sec-pc-client   (macOS)
#         ./sec-pc-client                                     (Linux)

QT += widgets network
CONFIG += c++17
TEMPLATE = app
TARGET = sec-pc-client

INCLUDEPATH += $$PWD/src $$PWD/../terminal/src

# --- macOS / Anaconda-Qt workaround -----------------------------------------
# Qt 5.15.2's mkspec links the AGL framework, which Apple removed from recent
# macOS SDKs. We don't use AGL; link only OpenGL so the app builds on current
# Xcode. Harmless no-op on a Qt that doesn't reference AGL.
macx: QMAKE_LIBS_OPENGL = -framework OpenGL

SOURCES += \
    src/main.cpp \
    src/net/TcpClient.cpp \
    src/ui/MainWindow.cpp \
    src/ui/VideoWidget.cpp \
    src/ui/ControlPanel.cpp \
    src/ui/FileBrowser.cpp

HEADERS += \
    src/net/TcpClient.h \
    src/ui/MainWindow.h \
    src/ui/VideoWidget.h \
    src/ui/ControlPanel.h \
    src/ui/FileBrowser.h
