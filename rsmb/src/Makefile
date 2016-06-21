
ifeq ($(OS), Windows_NT)

# the /Zp4 option aligns structure elements on 4 byte boundaries to allow
# the heapdump mapping to work

all: broker.exe broker_mqtts.exe rsmb.ini

broker.exe: *.c *.h
	vcvars32.bat
	cl -O2 -DWIN32 ws2_32.lib *.c /W3 /D _CRT_SECURE_NO_WARNINGS /D UNICODE /MD /Zp4 /Febroker.exe /link /MANIFEST
	mt -manifest broker.exe.manifest -outputresource:broker.exe\;1


broker_mqtts.exe: *.c *.h
	vcvars32.bat
	cl -O2 -DWIN32 Iphlpapi.lib ws2_32.lib *.c /W3 /D _CRT_SECURE_NO_WARNINGS /D UNICODE /MD /DMQTTS /Zp4 /Febroker_mqtts.exe /link /MANIFEST
	mt -manifest broker_mqtts.exe.manifest -outputresource:broker_mqtts.exe\;1

rsmb.ini: *.h
	perl tools\\be\\be.pl

else

ifndef CC
  CC = gcc
endif

all: broker broker_dbg broker_mqtts rsmb.ini

broker: *.c *.h 
	$(CC) -Wall -Os *.c -o broker

broker_dbg: *.c *.h
	$(CC) -Wall -ggdb *.c -o broker_dbg

broker_mqtts: *.c *.h
	$(CC) -DMQTTS -Wall -Os *.c -o broker_mqtts
	
broker_mqtts32: *.c *.h
	$(CC) -m32 -DMQTTS -Wall -Os *.c -o broker_mqtts32

rsmb.ini: *.h
	perl tools/be/be.pl

endif

