QT -= gui

CONFIG += c++11 console
CONFIG -= app_bundle
DEFINES += MQTTS

HEADERS += \
    src/Bridge.h \
    src/Broker.h \
    src/Clients.h \
    src/Filter.h \
    src/Heap.h \
    src/LinkedList.h \
    src/Log.h \
    src/MQTTPacket.h \
    src/MQTTPacketOut.h \
    src/MQTTProtocol.h \
    src/MQTTProtocolClient.h \
    src/MQTTProtocolOut.h \
    src/MQTTSPacket.h \
    src/MQTTSPacketSerialize.h \
    src/MQTTSProtocol.h \
    src/MQTTSProtocolOut.h \
    src/Messages.h \
    src/Persistence.h \
    src/Protocol.h \
    src/Socket.h \
    src/SocketBuffer.h \
    src/StackTrace.h \
    src/SubsEngine.h \
    src/Topics.h \
    src/Tree.h \
    src/Users.h

SOURCES += \
    src/Bridge.c \
    src/Broker.c \
    src/Clients.c \
    src/Filter.c \
    src/Heap.c \
    src/LinkedList.c \
    src/Log.c \
    src/MQTTPacket.c \
    src/MQTTPacketOut.c \
    src/MQTTProtocol.c \
    src/MQTTProtocolClient.c \
    src/MQTTProtocolOut.c \
    src/MQTTSPacket.c \
    src/MQTTSPacketSerialize.c \
    src/MQTTSProtocol.c \
    src/MQTTSProtocolOut.c \
    src/Messages.c \
    src/Persistence.c \
    src/Protocol.c \
    src/Socket.c \
    src/SocketBuffer.c \
    src/StackTrace.c \
    src/SubsEngine.c \
    src/Topics.c \
    src/Tree.c \
    src/Users.c

win32 {
LIBS += -lws2_32 -lIphlpapi
}
