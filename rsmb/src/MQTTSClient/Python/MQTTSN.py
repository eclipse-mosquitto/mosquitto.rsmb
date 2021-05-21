"""
/*******************************************************************************
 * Copyright (c) 2011, 2013 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *******************************************************************************/
"""

# Low-level protocol interface for MQTTs

try:
  False = not True
except NameError:
  True = (1 == 1)
  False = not True

# Message types
ADVERTISE, SEARCHGW, GWINFO, reserved, \
CONNECT, CONNACK, \
WILLTOPICREQ, WILLTOPIC, WILLMSGREQ, WILLMSG, \
REGISTER, REGACK, \
PUBLISH, PUBACK, PUBCOMP, PUBREC, PUBREL, reserved1, \
SUBSCRIBE, SUBACK, UNSUBSCRIBE, UNSUBACK, \
PINGREQ, PINGRESP, DISCONNECT, reserved2, \
WILLTOPICUPD, WILLTOPICRESP, WILLMSGUPD, WILLMSGRESP = range(30)

packetNames = [ "ADVERTISE", "SEARCHGW", "GWINFO", "reserved", \
"CONNECT", "CONNACK", \
"WILLTOPICREQ", "WILLTOPIC", "WILLMSGREQ", "WILLMSG", \
"REGISTER", "REGACK", \
"PUBLISH", "PUBACK", "PUBCOMP", "PUBREC", "PUBREL", "reserved", \
"SUBSCRIBE", "SUBACK", "UNSUBSCRIBE", "UNSUBACK", \
"PINGREQ", "PINGRESP", "DISCONNECT", "reserved", \
"WILLTOPICUPD", "WILLTOPICRESP", "WILLMSGUPD", "WILLMSGRESP"]

TopicIdType_Names = ["NORMAL", "PREDEFINED", "SHORT_NAME"]
TOPIC_NORMAL, TOPIC_PREDEFINED, TOPIC_SHORTNAME = range(3)

def writeInt16(length):
  return chr(length / 256)+ chr(length % 256)

def readInt16(buf):
  return ord(buf[0])*256 + ord(buf[1])

def getPacket(aSocket):
  "receive the next packet"
  buf, address = aSocket.recvfrom(65535) # get the first byte fixed header
  if buf == "":
    return None
  
  length = ord(buf[0])
  if length == 1:
    if buf == "":
      return None
    length = readInt16(buf[1:])
    
  return buf, address

def MessageType(buf):
  if ord(buf[0]) == 1:
    msgtype = ord(buf[3])
  else:
    msgtype = ord(buf[1])
  return msgtype

class Flags:
  
  def __init__(self):
    self.DUP = False          # 1 bit
    self.QoS = 0              # 2 bits
    self.Retain = False       # 1 bit
    self.Will = False         # 1 bit
    self.CleanSession = True  # 1 bit
    self.TopicIdType = 0      # 2 bits
    
  def __eq__(self, flags):
    return self.DUP == flags.DUP and \
         self.QoS == flags.QoS and \
         self.Retain == flags.Retain and \
         self.Will == flags.Will and \
         self.CleanSession == flags.CleanSession and \
         self.TopicIdType == flags.TopicIdType
  
  def __ne__(self, flags):
    return not self.__eq__(flags)
  
  def __str__(self):
    "return printable representation of our data"
    return '{DUP '+str(self.DUP)+ \
           ", QoS "+str(self.QoS)+", Retain "+str(self.Retain) + \
           ", Will "+str(self.Will)+", CleanSession "+str(self.CleanSession) + \
           ", TopicIdType "+str(self.TopicIdType)+"}"
    
  def pack(self):
    "pack data into string buffer ready for transmission down socket"
    buffer = chr( (self.DUP << 7) | (self.QoS << 5) | (self.Retain << 4) | \
         (self.Will << 3) | (self.CleanSession << 2) | self.TopicIdType )
    #print "Flags - pack", str(bin(ord(buffer))), len(buffer)
    return buffer
  
  def unpack(self, buffer):
    "unpack data from string buffer into separate fields"
    b0 = ord(buffer[0])
    #print "Flags - unpack", str(bin(b0)), len(buffer), buffer
    self.DUP = ((b0 >> 7) & 0x01) == 1
    self.QoS = (b0 >> 5) & 0x03
    self.Retain = ((b0 >> 4) & 0x01) == 1
    self.Will = ((b0 >> 3) & 0x01) == 1
    self.CleanSession = ((b0 >> 2) & 0x01) == 1
    self.TopicIdType = (b0 & 0x03)
    return 1

class MessageHeaders:

  def __init__(self, aMsgType):
    self.Length = 0
    self.MsgType = aMsgType

  def __eq__(self, mh):
    return self.Length == mh.Length and self.MsgType == mh.MsgType

  def __str__(self):
    "return printable stresentation of our data"
    return "Length "+str(self.Length) + ", " + packetNames[self.MsgType]

  def pack(self, length):
    "pack data into string buffer ready for transmission down socket"
    # length does not yet include the length or msgtype bytes we are going to add
    buffer = self.encode(length) + chr(self.MsgType)
    return buffer

  def encode(self, length):
    self.Length = length + 2
    assert 2 <= self.Length <= 65535
    if self.Length < 256:
      buffer = chr(self.Length)
      print("length", self.Length)
    else:
      self.Length += 2
      buffer = chr(1) + writeInt16(self.Length)
    return buffer

  def unpack(self, buffer):
    "unpack data from string buffer into separate fields"
    (self.Length, bytes) = self.decode(buffer)
    self.MsgType = ord(buffer[bytes])
    return bytes + 1

  def decode(self, buffer):
    value = ord(buffer[0])
    if value > 1:
      bytes = 1
    else:
      value = readInt16(buffer[1:])
      bytes = 3
    return (value, bytes)

def writeUTF(aString):
  return writeInt16(len(aString)) + aString

def readUTF(buffer):
  length = readInt16(buffer)
  return buffer[2:2+length]


class Packets:

  def pack(self):
    return self.mh.pack(0)

  def __str__(self):
    return str(self.mh)

  def __eq__(self, packet):
    return False if packet == None else self.mh == packet.mh
  
  def __ne__(self, packet):
    return not self.__eq__(packet)

class Advertises(Packets):

  def __init__(self, buffer=None):
    self.mh = MessageHeaders(ADVERTISE)
    self.GwId = 0     # 1 byte
    self.Duration = 0 # 2 bytes
    if buffer:
      self.unpack(buffer)
      
  def pack(self):
    buffer = chr(self.GwId) + writeInt16(self.Duration)
    return self.mh.pack(len(buffer)) + buffer
  
  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == ADVERTISE
    self.GwId = ord(buffer[pos])
    pos += 1
    self.Duration = readInt16(buffer[pos:])
    
  def __str__(self):
    return str(self.mh) + " GwId "+str(self.GwId)+" Duration "+str(self.Duration)
    
  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.GwId == packet.GwId and \
           self.Duration == packet.Duration
  
  
class SearchGWs(Packets):

  def __init__(self, buffer=None):
    self.mh = MessageHeaders(SEARCHGW)
    self.Radius = 0
    if buffer:
      self.unpack(buffer)
      
  def pack(self):
    buffer = writeInt16(self.Radius)
    buffer = self.mh.pack(len(buffer)) + buffer
    return buffer
  
  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == SEARCHGW
    self.Radius = readInt16(buffer[pos:])
    
  def __str__(self):
    return str(self.mh) + " Radius "+str(self.Radius)
    
class GWInfos(Packets):

  def __init__(self, buffer=None):
    self.mh = MessageHeaders(GWINFO)
    self.GwId = 0  # 1 byte
    self.GwAdd = None # optional
    if buffer:
      self.unpack(buffer)
      
  def pack(self):
    buffer = chr(self.GwId)
    if self.GwAdd:
      buffer += self.GwAdd
    buffer = self.mh.pack(len(buffer)) + buffer
    return buffer
  
  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == GWINFO
    self.GwId = buffer[pos]
    pos += 1
    if pos >= self.mh.Length:
      self.GwAdd = None
    else:
      self.GwAdd = buffer[pos:]
          
  def __str__(self):
    buf = str(self.mh) + " Radius "+str(self.GwId)
    if self.GwAdd:
      buf += " GwAdd "+self.GwAdd
    return buf

class Connects(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(CONNECT)
    self.Flags = Flags()
    self.ProtocolId = 1
    self.Duration = 30
    self.ClientId = ""
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    buffer = self.Flags.pack() + chr(self.ProtocolId) + writeInt16(self.Duration) + self.ClientId
    return self.mh.pack(len(buffer)) + buffer

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == CONNECT
    pos += self.Flags.unpack(buffer[pos])
    self.ProtocolId = ord(buffer[pos])
    pos += 1
    self.Duration = readInt16(buffer[pos:])
    pos += 2
    self.ClientId = buffer[pos:]

  def __str__(self):
    buf = str(self.mh) + ", " + str(self.Flags) + \
    ", ProtocolId " + str(self.ProtocolId) + \
    ", Duration " + str(self.Duration) + \
    ", ClientId " + self.ClientId
    return buf

  def __eq__(self, packet):
    rc = Packets.__eq__(self, packet) and \
           self.Flags == packet.Flags and \
           self.ProtocolId == packet.ProtocolId and \
           self.Duration == packet.Duration and \
           self.ClientId == packet.ClientId
    return rc


class Connacks(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(CONNACK)
    self.ReturnCode = 0 # 1 byte
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    buffer = chr(self.ReturnCode)
    return self.mh.pack(len(buffer)) + buffer

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == CONNACK
    self.ReturnCode = ord(buffer[pos])

  def __str__(self):
    return str(self.mh)+", ReturnCode "+str(self.ReturnCode)

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.ReturnCode == packet.ReturnCode


class WillTopicReqs(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(WILLTOPICREQ)
    if buffer != None:
      self.unpack(buffer)

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == WILLTOPICREQ
    
    
class WillTopics(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(WILLTOPIC)
    self.flags = Flags()
    self.WillTopic = ""
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    buffer = self.flags.pack() + self.WillTopic
    return self.mh.pack(len(buffer)) + buffer

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == WILLTOPIC
    pos += self.flags.unpack(buffer[pos:])
    self.WillTopic = buffer[pos:self.mh.Length]

  def __str__(self):
    return str(self.mh)+", Flags "+str(self.flags)+", WillTopic "+self.WillTopic

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.flags == packet.flags and \
           self.WillTopic == packet.WillTopic
          
class WillMsgReqs(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(WILLMSGREQ)
    if buffer != None:
      self.unpack(buffer)

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == WILLMSGREQ
    
    
class WillMsgs(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(WILLMSG)
    self.WillMsg = ""
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    return self.mh.pack(len(self.WillMsg)) + self.WillMsg

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == WILLMSG
    self.WillMsg = buffer[pos:self.mh.Length]

  def __str__(self):
    return str(self.mh)+", WillMsg "+self.WillMsg

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.WillMsg == packet.WillMsg
          
class Registers(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(REGISTER)
    self.TopicId = 0
    self.MsgId = 0
    self.TopicName = ""
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    buffer = writeInt16(self.TopicId) + writeInt16(self.MsgId) + self.TopicName
    return self.mh.pack(len(buffer)) + buffer

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == REGISTER
    self.TopicId = readInt16(buffer[pos:])
    pos += 2
    self.MsgId = readInt16(buffer[pos:])
    pos += 2
    self.TopicName = buffer[pos:self.mh.Length]

  def __str__(self):
    return str(self.mh)+", TopicId "+str(self.TopicId)+", MsgId "+str(self.MsgId)+", TopicName "+self.TopicName

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.TopicId == packet.TopicId and \
           self.MsgId == packet.MsgId and \
           self.TopicName == packet.TopicName


class Regacks(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(REGACK)
    self.TopicId = 0
    self.MsgId = 0
    self.ReturnCode = 0 # 1 byte
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    buffer = writeInt16(self.TopicId) + writeInt16(self.MsgId) + chr(self.ReturnCode)
    return self.mh.pack(len(buffer)) + buffer

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == REGACK
    self.TopicId = readInt16(buffer[pos:])
    pos += 2
    self.MsgId = readInt16(buffer[pos:])
    pos += 2
    self.ReturnCode = ord(buffer[pos])

  def __str__(self):
    return str(self.mh)+", TopicId "+str(self.TopicId)+", MsgId "+str(self.MsgId)+", ReturnCode "+str(self.ReturnCode)

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.TopicId == packet.TopicId and \
           self.MsgId == packet.MsgId and \
           self.ReturnCode == packet.ReturnCode


class Publishes(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(PUBLISH)
    self.Flags = Flags()
    self.TopicId = 0 # 2 bytes
    self.TopicName = ""
    self.MsgId = 0 # 2 bytes
    self.Data = ""
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    buffer = self.Flags.pack()
    if self.Flags.TopicIdType in [TOPIC_NORMAL, TOPIC_PREDEFINED, 3]:
      print("topic id is", self.TopicId)
      buffer += writeInt16(self.TopicId)
    elif self.Flags.TopicIdType == TOPIC_SHORTNAME:
      buffer += (self.TopicName + "  ")[0:2]
    buffer += writeInt16(self.MsgId) + self.Data
    return self.mh.pack(len(buffer)) + buffer

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == PUBLISH
    pos += self.Flags.unpack(buffer[pos:])
    
    self.TopicId = 0
    self.TopicName = ""
    if self.Flags.TopicIdType in [TOPIC_NORMAL, TOPIC_PREDEFINED]:
      self.TopicId = readInt16(buffer[pos:])
    elif self.Flags.TopicIdType == TOPIC_SHORTNAME:
      self.TopicName = buffer[pos:pos+2]
    pos += 2
    self.MsgId = readInt16(buffer[pos:])
    pos += 2
    self.Data = buffer[pos:self.mh.Length]

  def __str__(self):
    return str(self.mh)+", Flags "+str(self.Flags)+", TopicId "+str(self.TopicId)+", MsgId "+str(self.MsgId)+", Data "+self.Data

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
         self.Flags == packet.Flags and \
         self.TopicId == packet.TopicId and \
         self.MsgId == packet.MsgId and \
         self.Data == packet.Data


class Pubacks(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(PUBACK)
    self.TopicId = 0
    self.MsgId = 0
    self.ReturnCode = 0 # 1 byte
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    buffer = writeInt16(self.TopicId) + writeInt16(self.MsgId) + chr(self.ReturnCode)
    return self.mh.pack(len(buffer)) + buffer

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == PUBACK
    self.TopicId = readInt16(buffer[pos:])
    pos += 2
    self.MsgId = readInt16(buffer[pos:])
    pos += 2
    self.ReturnCode = ord(buffer[pos])

  def __str__(self):
    return str(self.mh)+", TopicId "+str(self.TopicId)+" , MsgId "+str(self.MsgId)+", ReturnCode "+str(self.ReturnCode)

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.TopicId == packet.TopicId and \
           self.MsgId == packet.MsgId and \
           self.ReturnCode == packet.ReturnCode
          
          
class Pubrecs(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(PUBREC)
    self.MsgId = 0
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    return self.mh.pack(2) + writeInt16(self.MsgId)

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == PUBREC
    self.MsgId = readInt16(buffer[pos:])

  def __str__(self):
    return str(self.mh)+" , MsgId "+str(self.MsgId)

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and self.MsgId == packet.MsgId
    
class Pubrels(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(PUBREL)
    self.MsgId = 0
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    return self.mh.pack(2) + writeInt16(self.MsgId)

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == PUBREL
    self.MsgId = readInt16(buffer[pos:])

  def __str__(self):
    return str(self.mh)+" , MsgId "+str(self.MsgId)

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and self.MsgId == packet.MsgId


class Pubcomps(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(PUBCOMP)
    self.MsgId = 0
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    return self.mh.pack(2) + writeInt16(self.MsgId)

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == PUBCOMP
    self.MsgId = readInt16(buffer[pos:])

  def __str__(self):
    return str(self.mh)+" , MsgId "+str(self.MsgId)

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and self.MsgId == packet.MsgId
    
    
class Subscribes(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(SUBSCRIBE)
    self.Flags = Flags()
    self.MsgId = 0 # 2 bytes
    self.TopicId = 0 # 2 bytes
    self.TopicName = ""
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    buffer = self.Flags.pack() + writeInt16(self.MsgId)
    if self.Flags.TopicIdType == TOPIC_PREDEFINED:
      buffer += writeInt16(self.TopicId)
    elif self.Flags.TopicIdType in [TOPIC_NORMAL, TOPIC_SHORTNAME]:
      buffer += self.TopicName
    return self.mh.pack(len(buffer)) + buffer


  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == SUBSCRIBE
    pos += self.Flags.unpack(buffer[pos:])
    self.MsgId = readInt16(buffer[pos:])
    pos += 2
    self.TopicId = 0
    self.TopicName = ""
    if self.Flags.TopicIdType == TOPIC_PREDEFINED:
      self.TopicId = readInt16(buffer[pos:])
    elif self.Flags.TopicIdType in [TOPIC_NORMAL, TOPIC_SHORTNAME]:
      self.TopicName = buffer[pos:pos+2]

  def __str__(self):
    buffer = str(self.mh)+", Flags "+str(self.Flags)+", MsgId "+str(self.MsgId)
    if self.Flags.TopicIdType == 0:
      buffer += ", TopicName "+self.TopicName
    elif self.Flags.TopicIdType == 1:
      buffer += ", TopicId "+str(self.TopicId)
    elif self.Flags.TopicIdType == 2:
      buffer += ", TopicId "+self.TopicId
    return buffer

  def __eq__(self, packet):
    if self.Flags.TopicIdType == 0:
      rc = self.TopicName == packet.TopicName
    else:
      rc = self.TopicId == packet.TopicId
    return Packets.__eq__(self, packet) and \
         self.Flags == packet.Flags and \
         self.MsgId == packet.MsgId and rc


class Subacks(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(SUBACK)
    self.Flags = Flags() # 1 byte
    self.TopicId = 0 # 2 bytes
    self.MsgId = 0 # 2 bytes
    self.ReturnCode = 0 # 1 byte
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    buffer = self.Flags.pack() + writeInt16(self.TopicId) + writeInt16(self.MsgId) + chr(self.ReturnCode)
    return self.mh.pack(len(buffer)) + buffer

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == SUBACK
    pos += self.Flags.unpack(buffer[pos:])
    self.TopicId = readInt16(buffer[pos:])
    pos += 2
    self.MsgId = readInt16(buffer[pos:])
    pos += 2
    self.ReturnCode = ord(buffer[pos])

  def __str__(self):
    return str(self.mh)+", Flags "+str(self.Flags)+", TopicId "+str(self.TopicId)+" , MsgId "+str(self.MsgId)+", ReturnCode "+str(self.ReturnCode)

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.Flags == packet.Flags and \
           self.TopicId == packet.TopicId and \
           self.MsgId == packet.MsgId and \
           self.ReturnCode == packet.ReturnCode


class Unsubscribes(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(UNSUBSCRIBE)
    self.Flags = Flags()
    self.MsgId = 0 # 2 bytes
    self.TopicId = 0 # 2 bytes
    self.TopicName = ""
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    buffer = self.Flags.pack() + writeInt16(self.MsgId)
    if self.Flags.TopicIdType == 0:
      buffer += self.TopicName
    elif self.Flags.TopicIdType == 1:
      buffer += writeInt16(self.TopicId)
    elif self.Flags.TopicIdType == 2:
      buffer += self.TopicId
    return self.mh.pack(len(buffer)) + buffer

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == UNSUBSCRIBE
    pos += self.Flags.unpack(buffer[pos:])
    self.MsgId = readInt16(buffer[pos:])
    pos += 2
    self.TopicId = 0
    self.TopicName = ""
    if self.Flags.TopicIdType == 0:
      self.TopicName = buffer[pos:self.mh.Length]
    elif self.Flags.TopicIdType == 1:
      self.TopicId = readInt16(buffer[pos:])
    elif self.Flags.TopicIdType == 3:
      self.TopicId = buffer[pos:pos+2]

  def __str__(self):
    buffer = str(self.mh)+", Flags "+str(self.Flags)+", MsgId "+str(self.MsgId)
    if self.Flags.TopicIdType == 0:
      buffer += ", TopicName "+self.TopicName
    elif self.Flags.TopicIdType == 1:
      buffer += ", TopicId "+str(self.TopicId)
    elif self.Flags.TopicIdType == 2:
      buffer += ", TopicId "+self.TopicId
    return buffer

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
         self.Flags == packet.Flags and \
         self.MsgId == packet.MsgId and \
         self.TopicId == packet.TopicId and \
         self.TopicName == packet.TopicName

class Unsubacks(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(UNSUBACK)
    self.MsgId = 0
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    return self.mh.pack(2) + writeInt16(self.MsgId)

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == UNSUBACK
    self.MsgId = readInt16(buffer[pos:])

  def __str__(self):
    return str(self.mh)+" , MsgId "+str(self.MsgId)

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and self.MsgId == packet.MsgId


class Pingreqs(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(PINGREQ)
    self.ClientId = None
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    if self.ClientId:
      buf = self.mh.pack(len(self.ClientId)) + self.ClientId
    else:
      buf = self.mh.pack(0)
    return buf

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == PINGREQ
    self.ClientId = buffer[pos:self.mh.Length]
    if self.ClientId == '':
      self.ClientId = None

  def __str__(self):
    buf = str(self.mh)
    if self.ClientId:
      buf += ", ClientId "+self.ClientId
    return buf

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.ClientId == packet.ClientId
          

class Pingresps(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(PINGRESP)
    if buffer != None:
      self.unpack(buffer)

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == PINGRESP
  
class Disconnects(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(DISCONNECT)
    self.Duration = None
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    if self.Duration:
      buf = self.mh.pack(2) + writeInt16(self.Duration)
    else:
      buf = self.mh.pack(0)
    return buf

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == DISCONNECT
    buf = buffer[pos:self.mh.Length]
    if buf == '':
      self.Duration = None
    else:
      self.Duration = readInt16(buffer[pos:])

  def __str__(self):
    buf = str(self.mh)
    if self.Duration:
      buf += ", Duration "+str(self.Duration)
    return buf

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.Duration == packet.Duration
          
class WillTopicUpds(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(WILLTOPICUPD)
    self.flags = Flags()
    self.WillTopic = ""
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    buffer = self.flags.pack() + self.WillTopic
    return self.mh.pack(len(buffer)) + buffer

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == WILLTOPICUPD
    pos += self.flags.unpack(buffer[pos:])
    self.WillTopic = buffer[pos:self.mh.Length]

  def __str__(self):
    return str(self.mh)+", Flags "+str(self.flags)+", WillTopic "+self.WillTopic

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.flags == packet.flags and \
           self.WillTopic == packet.WillTopic
          
class WillMsgUpds(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(WILLMSGUPD)
    self.WillMsg = ""
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    return self.mh.pack(len(self.WillMsg)) + self.WillMsg

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == WILLMSGUPD
    self.WillMsg = buffer[pos:self.mh.Length]

  def __str__(self):
    return str(self.mh)+", WillMsg "+self.WillMsg

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.WillMsg == packet.WillMsg
          
class WillTopicResps(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(WILLTOPICRESP)
    self.ReturnCode = 0
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    buffer = writeInt16(self.ReturnCode)
    return self.mh.pack(len(buffer)) + buffer

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == WILLTOPICRESP
    self.ReturnCode = readInt16(buffer[pos:])

  def __str__(self):
    return str(self.mh)+", ReturnCode "+str(self.ReturnCode)

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.ReturnCode == packet.ReturnCode
          
class WillMsgResps(Packets):

  def __init__(self, buffer = None):
    self.mh = MessageHeaders(WILLMSGRESP)
    self.ReturnCode = 0
    if buffer != None:
      self.unpack(buffer)

  def pack(self):
    buffer = writeInt16(self.ReturnCode)
    return self.mh.pack(len(buffer)) + buffer

  def unpack(self, buffer):
    pos = self.mh.unpack(buffer)
    assert self.mh.MsgType == WILLMSGRESP
    self.returnCode = readInt16(buffer[pos:])

  def __str__(self):
    return str(self.mh)+", ReturnCode "+str(self.ReturnCode)

  def __eq__(self, packet):
    return Packets.__eq__(self, packet) and \
           self.ReturnCode == packet.ReturnCode

objects = [Advertises, SearchGWs, GWInfos, None,
           Connects, Connacks,
           WillTopicReqs, WillTopics, WillMsgReqs, WillMsgs, 
           Registers, Regacks, 
           Publishes, Pubacks, Pubcomps, Pubrecs, Pubrels, None,
           Subscribes, Subacks, Unsubscribes, Unsubacks,
           Pingreqs, Pingresps, Disconnects, None,
           WillTopicUpds, WillTopicResps, WillMsgUpds, WillMsgResps]

def unpackPacket(buffer, address):
  if MessageType(buffer) != None:
    packet = objects[MessageType(buffer)]()
    packet.unpack(buffer)
  else:
    packet = None
  return packet, address

if __name__ == "__main__":
  print("Object string representations")
  for o in objects:
    if o:
      print(o())
      
  print("\nComparisons")
  for o in [Flags] + objects:
    if o:
      o1 = o()
      o2 = o()
      o2.unpack(o1.pack())
      if o1 != o2:
        print("error! ", str(o1.mh) if hasattr(o1, "mh") else o1.__class__.__name__)
        print(str(o1))
        print(str(o2))
      else:
        print("ok ", str(o1.mh) if hasattr(o1, "mh") else o1.__class__.__name__)
  

