#!/usr/bin/env python3

"""
//-----------------------------------------------------------------------------
// Based on pm2_reblay_emualting by Salvador Mendoza (salmg.net), 2021
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Code to relay Desfire tag to Proxmark3 Standalone mode aka reblay by Salvador Mendoza
// Requires:
//      libnfc       https://github.com/nfc-tools/libnfc
//      nfc-bindings https://github.com/xantares/nfc-bindings.git
//-----------------------------------------------------------------------------
"""
from __future__ import print_function

import serial
from smartcard.util import toHexString, toBytes
from smartcard.CardType import AnyCardType
from smartcard.CardRequest import CardRequest

import sys
import nfc # nfc-bidings: https://github.com/xantares/nfc-bindings.git


# Init libnfc and get local tag's UID

print('Version: ', nfc.__version__)

context = nfc.init()
pnd = nfc.open(context)
if pnd is None:
    print('ERROR: Unable to open NFC device.')
    sys.exit(1)

if nfc.initiator_init(pnd) < 0:
    nfc.perror(pnd, "nfc_initiator_init")
    print('ERROR: Unable to init NFC device.')
    sys.exit(1)

print('NFC reader: %s opened' % nfc.device_get_name(pnd))

nmMifare = nfc.modulation()
nmMifare.nmt = nfc.NMT_ISO14443A
nmMifare.nbr = nfc.NBR_106

nt = nfc.target()
ret = nfc.initiator_select_passive_target(pnd, nmMifare, 0, 0, nt)

if (ret <=0):
        print('Error: no tag was found\n')
        nfc.close(pnd)
        nfc.exit(context)
        sys.exit(1)

print('The following (NFC) ISO14443A tag was found:')
print('    ATQA (SENS_RES): ', end='')
nfc.print_hex(nt.nti.nai.abtAtqa, 2)
id = 1
if nt.nti.nai.abtUid[0] == 8:
    id = 3
print('       UID (NFCID%d): ' % id , end='')
nfc.print_hex(nt.nti.nai.abtUid, nt.nti.nai.szUidLen)
print('      SAK (SEL_RES): ', end='')
nfc.print_hex([nt.nti.nai.btSak], 1)
if nt.nti.nai.szAtsLen:
    print('          ATS (ATR): ', end='')
    nfc.print_hex(nt.nti.nai.abtAts, nt.nti.nai.szAtsLen)

#nfc.close(pnd)
#nfc.exit(context)

# NFC tag found on our side, connect to PM3

ser = serial.Serial('/dev/rfcomm0')  # open Proxmark3 Bluetooth port

print('Testing code: bluetooth has to be connected with the right rfcomm port!')
print('Waiting for data...')
rping = ser.read(2)


print('Terminal command:'),
print(toHexString(list(rping)))

# send card info :  UID 
tosend = bytearray(nt.nti.nai.abtUid[0:7])
#tosend.extend(nt.nti.nai.abtAtqa)
#tosend.extend(nt.nti.nai.abtUid[0:7])
#tosend.append(nt.nti.nai.btSak)
#tosend.extend(nt.nti.nai.abtAts[0:nt.nti.nai.szAtsLen])
#tosend.insert(0, len(tosend)) # first byte is size of the packet

print('Sending serial:')
ser.write(tosend)
print(toHexString(list(tosend)))

while True:
    lenpk = ser.read(1) #first byte is the buffer length
    bufferlen = lenpk[0]

    buffer = ser.read(bufferlen)
    print('Terminal command:'),
    print(toHexString(list(buffer)))

    # transmit command to tag
    szRx, pbtRx = nfc.initiator_transceive_bytes(pnd, buffer, len(buffer), 264,  0)
    if szRx < 0:
        print('Error sending command to tag')
        pbtRx='\xFF' # what should we send to return an error to the reader?
        szRx=len(pbtRx)
    
    #pbtRx=[0x90,0x00]
    #szRx=len(pbtRx)
    
    # send result to proxmark
    # print(type(pbtRx))
    print('Sending serial:')
    sent=ser.write(pbtRx[0:szRx])
    print(f'sent {sent} bytes: ', end='')
    print(toHexString(list(pbtRx[0:szRx])))
