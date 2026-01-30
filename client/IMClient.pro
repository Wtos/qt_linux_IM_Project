QT += core gui network widgets

TARGET = IMClient
TEMPLATE = app

CONFIG += c++14

SOURCES += \
    main.cpp \
    ui/chatwindow.cpp \
    ui/loginwindow.cpp \
    network/tcpclient.cpp \
    network/protocol.cpp

HEADERS += \
    ui/chatwindow.h \
    ui/loginwindow.h \
    network/tcpclient.h \
    network/protocol.h

FORMS += \
    ui/chatwindow.ui \
    ui/loginwindow.ui

INCLUDEPATH += $$PWD/ui $$PWD/network

DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc
UI_DIR = $$PWD/build/ui
RCC_DIR = $$PWD/build/rcc

RESOURCES += resources/resources.qrc

win32 {
    LIBS += -lws2_32
}
