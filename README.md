RSMB: Really Small Message Broker
=================================

The Really Small Message Broker is a server implementation of the MQTT and MQTT-SN protocols. Any client that implements this protocol properly can use this server for sending and receiving messages.

The main reason for using RSMB over the main [Mosquitto](http://mosquitto.org/) codebase is that Mosquitto doesn't currently have support for the MQTT-SN protocol.


## Project History

After several IBM servers were produced during the 2000s, Really Small Message Broker (RSMB) was released onto IBM alphaWorks in 2008. It's aim was to be a minimalist MQTT server, and according to the usual alphaWorks practices, was closed source and released under an evaluation-only license. During the following two years, it gained a small but enthusiastic following. In 2010, Roger Light learned about MQTT and RSMB from an Andy Stanford-Clark presentation. He created Mosquitto to be an open source alternative to RSMB.

RSMB has had MQTT-SN capabilities added but not released outside of IBM. Now we have the chance to bring Mosquitto and RSMB back together as one Eclipse project, taking advantage of the collaboration of the authors of both previous projects.

You can read more about the background of RSMB on the Eclipse project page for Mosquitto:
https://www.eclipse.org/proposals/technology.mosquitto/


## Getting started

RSMB has been tested on Linux, Mac OS and Windows. 

To compile on Linux and Mac, it should be as simple as:

```
cd rsmb/src
make
```

For more detailed information, see the rsmb/doc/gettingstarted.htm document.


## Sample Configuration

```
# Uncomment this to show you packets being sent and received
#trace_output protocol

# Normal MQTT listener
listener 1883 INADDR_ANY
ipv6 true

# MQTT-SN listener
listener 1883 INADDR_ANY mqtts
ipv6 true
```

For a more complex example, see Ian Craggs' blog post:
http://modelbasedtesting.co.uk/?p=44


## Links

- Source code repository: <https://github.com/eclipse/mosquitto.rsmb>
- Find existing bugs or submit a new bug: <https://github.com/eclipse/mosquitto.rsmb/issues>
- Mailing-list: <https://dev.eclipse.org/mailman/listinfo/mosquitto-dev>


[![Travis Build Status (master)](https://travis-ci.org/eclipse/mosquitto.rsmb.svg?branch=master)](https://travis-ci.org/eclipse/mosquitto.rsmb)
