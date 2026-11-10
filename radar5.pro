QT       += core gui widgets
CONFIG   += c++14
TARGET    = Radar
TEMPLATE  = app

OSG_DIR = D:/software/OpenSceneG/install-mingw73

INCLUDEPATH += $$OSG_DIR/include
LIBS += -L$$OSG_DIR/lib \
        -losg -losgUtil -losgDB -losgViewer -losgGA -losgText -lOpenThreads

SOURCES += main.cpp MainWindow.cpp OSGWidget.cpp \
    geojson.cpp \
    situationscene.cpp
HEADERS += MainWindow.h OSGWidget.h \
    SceneUtil.h \
    geojson.h \
    situationscene.h \
    SituationCore.h
