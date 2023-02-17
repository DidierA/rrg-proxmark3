#!/usr/bin/env python3

"""
//-----------------------------------------------------------------------------
// Based on pm3_reblay_emualting by Salvador Mendoza (salmg.net), 2021
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Code to relay Desfire tag to Proxmark3 Standalone mode aka reblay_desfire
// NFC tag <-libnfc compatible reader->This script<-bluetooth->proxmark3 hf_reblay_desfire<>Card reader
// Requires:
//      libnfc       https://github.com/nfc-tools/libnfc
//      nfc-bindings https://github.com/xantares/nfc-bindings.git
//-----------------------------------------------------------------------------
"""
from __future__ import print_function

import argparse
import nfc 
import serial
import sys

parser=argparse.ArgumentParser(description='reblay_desfire')
parser.add_argument('-p', '--port', default='/dev/rfcomm0')
args=parser.parse_args()

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

# NFC tag found on our side, connect to PM3
print(f'Opening serial port {args.port}')
ser = serial.Serial(args.port)  # open Proxmark3 Bluetooth port

print('Testing code: bluetooth has to be connected with the right rfcomm port!')
print('Waiting for data...')
rping = ser.read(2)

print('Terminal command:'),
nfc.print_hex(rping,2)

# send card info :  UID 
tosend = bytearray(nt.nti.nai.abtUid[0:7])

print('Sending serial:')
ser.write(tosend)
nfc.print_hex(tosend,len(tosend))

while True:
    lenpk = ser.read(1) #first byte is the buffer length
    bufferlen = lenpk[0]

    buffer = ser.read(bufferlen)
    print('Terminal command:'),
    nfc.print_hex(buffer,bufferlen)

    # Transmit command to tag
    szRx, pbtRx = nfc.initiator_transceive_bytes(pnd, buffer, len(buffer), 264,  0)
    if szRx < 0:
        print('Error sending command to tag')
        pbtRx=b'\xCA' # MFDES_E_COMMAND_ABORTED in include/protocols.h
        szRx=len(pbtRx)
    
    # send result to proxmark
    print('Sending serial:')
    sent=ser.write(pbtRx[0:szRx])
    print(f'sent {sent} bytes: ', end='')
    nfc.print_hex((pbtRx[0:szRx]),szRx)
