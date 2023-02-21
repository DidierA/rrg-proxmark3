//-----------------------------------------------------------------------------
// Copyright (C) Salvador Mendoza (salmg.net) - January 01, 2021
// Copyright (C) Proxmark3 contributors. See AUTHORS.md for details.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// See LICENSE.txt for the text of the license.
//-----------------------------------------------------------------------------
// Code to relay 14a technology data aka reblay by Salvador Mendoza
//-----------------------------------------------------------------------------
#include "standalone.h"
#include "proxmark3_arm.h"
#include "appmain.h"
#include "fpgaloader.h"
#include "util.h"
#include "dbprint.h"
#include "ticks.h"
#include "string.h"
#include "BigBuf.h"
#include "iso14443a.h"
#include "protocols.h"
#include "cmd.h"

#include "usart.h" // Bluetooth reading & writing

void ModInfo(void) {
    DbpString("  HF - Relaying DESFire data over Bluetooth - (Salvador Mendoza, DidierA)");
}
/* This standalone implements emulating a DESFire tag.
*
* Reading ISO-14443A technology is not limited to payment cards. This example
* was designed to open new possibilities relaying ISO-14443A data over Bluetooth.
*
* Instructions:
*
* I recommend setting up & run the other end before start sending or receiving data in this Proxmark3
* standalone.
*
* - Set up and run the other end first, from where the Proxmark will receive the data. See tools/pm3-reblay_desfire.py
* - At start, the Proxmark sends a ping and waits for a 7-byte UID, which will be used for emulation.
* - When the Proxmark3 detected the terminal, it will send the command to the connection.
* - The first byte will be the package length, then, the terminal command. Use the first
*   length byte to read the whole package.
* - Proxmark3 will expect a raw APDU from the other end, then it will be sent to the terminal.
* - The command of the terminal will be sent back to the connection, repeating the cycle.
*
* - You can exit by holding the button for 1sec.
* - A short click on the button will clear the trace and restart emulation with the same UID
*
*  Notes:
* - The emulation mode was tested in a test environment with DESFire EV2. This does not mean
*   that it will work in all the terminals around the world.
* - The emulation mode implements different techniques to try to keep the connection alive:
*   WTX or ACK for NACK requests. Some of these requests could be denied depending on
*   the reader configuration.
*
*
* Be brave enough to share your knowledge & inspire others.
*/

void RunMod() {
    StandAloneMode();
    Dbprintf(_YELLOW_(">>")  "Relaying DESFire data over Bluetooth a.k.a. reblay_desfire Started<<");
    FpgaDownloadAndGo(FPGA_BITSTREAM_HF);

// Allocate 512 bytes for the dynamic modulation, created when the reader queries for it
// Such a response is less time critical, so we can prepare them on the fly
#define DYNAMIC_RESPONSE_BUFFER_SIZE 512
#define DYNAMIC_MODULATION_BUFFER_SIZE 1024

    uint8_t flags = FLAG_7B_UID_IN_DATA; //UID 7 bytes
    uint8_t data[PM3_CMD_DATA_SIZE] = {0x00}; // in case there is a read command received we shouldn't break

    // to initialize the emulation
    uint8_t tagType = 3; // 3 = Mifare DESFire
    tag_response_info_t *responses;

    uint32_t cuid = 0;
    uint32_t counters[3] = { 0x00, 0x00, 0x00 };
    uint8_t tearings[3] = { 0xbd, 0xbd, 0xbd };
    uint8_t pages = 0;


    // For received Bluetooth package
    uint8_t rpacket[MAX_FRAME_SIZE] = { 0x00 };
    uint16_t lenpacket;

    // Buffer for Bluetooth data
    uint8_t buffert[MAX_FRAME_SIZE] = { 0x00 };
    uint8_t bufferlen = 0;

    // Command buffers
    uint8_t receivedCmd[MAX_FRAME_SIZE] = { 0x00 };
    uint8_t receivedCmdPar[MAX_PARITY_SIZE] = { 0x00 };

    uint8_t dynamic_response_buffer[DYNAMIC_RESPONSE_BUFFER_SIZE] = {0};
    uint8_t dynamic_modulation_buffer[DYNAMIC_MODULATION_BUFFER_SIZE] = {0};

    // Command response - handler
    tag_response_info_t dynamic_response_info = {
        .response = dynamic_response_buffer,
        .response_n = 0,
        .modulation = dynamic_modulation_buffer,
        .modulation_n = 0
    };

    DbpString(_YELLOW_("[ ") "In emulation mode" _YELLOW_(" ]"));

    uint8_t initialized = 0;
#define VERBOSE 0 // attempt to gain speed by suppressing messages
    for (;;) {
        WDT_HIT();

        // Exit from RunMod, send a usbcommand.
        if (data_available()) break;

        // Button held down or pressed?
        int button_pressed = BUTTON_HELD(1000);

        if (button_pressed  == BUTTON_HOLD) // Holding down the button
            break;

        SpinDelay(500);

        LED_A_OFF();
        LED_C_ON();
        if (!initialized) {
            // Tell bluetooth side we are ready
            buffert[0]=1;
            usart_writebuffer_sync(buffert, 2);

            // Receive card info from bluetooth
            lenpacket = usart_read_ng(rpacket, sizeof(rpacket));

            // We should receive 7 byte UID
            if (lenpacket > 0) {
                DbpString(_YELLOW_("[ ") "Received Bluetooth data" _YELLOW_(" ]"));
                Dbhexdump(lenpacket, rpacket, false);
                if (lenpacket != 7) {
                    DbpString(_RED_("[ ") "Wrong length. expected 8" _RED_(" ]"));
                } else {
                    memcpy(&data, rpacket, 7);
                    DbpString(_GREEN_("[ ") "Set into emulator UID" _GREEN_(" ]"));
                    initialized=1 ;
                }
            }
        }

        // free eventually allocated BigBuf memory but keep Emulator Memory
        BigBuf_free_keep_EM();

        if (SimulateIso14443aInit(tagType, flags, data, &responses, &cuid, counters, tearings, &pages) == false) {
            BigBuf_free_keep_EM();
            reply_ng(CMD_HF_MIFARE_SIMULATE, PM3_EINIT, NULL, 0);
            DbpString(_YELLOW_("!!") "Error initializing the emulation process!");
            SpinDelay(500);
            continue;
        }
        DbpString(_YELLOW_("[") "Initialized emulation process" _YELLOW_("]"));

        // We need to listen to the high-frequency, peak-detected path.
        iso14443a_setup(FPGA_HF_ISO14443A_TAGSIM_LISTEN);

        int len = 0; // Command length
        int retval = PM3_SUCCESS; // Check emulation status

        uint8_t btresp = 0; // Bluetooth response
        lenpacket = 0;

        uint8_t prevcmd = 0x00; // Keep track of last terminal type command

        clear_trace();
        set_tracing(true);

        for (;;) {
            LED_B_OFF();
            // Clean receive command buffer
            if ( !GetIso14443aCommandFromReader(receivedCmd, receivedCmdPar, &len)) {
                // Button held down or pressed?
                if (BUTTON_PRESS()) {
                    DbpString(_YELLOW_("!!") "Emulator stopped");
                    retval = PM3_EOPABORTED;
                    break;
                }
                len=0;
            }
            tag_response_info_t *p_response = NULL;
            LED_B_ON();

            // dynamic_response_info will be in charge of responses
            dynamic_response_info.response_n = 0;

            if (lenpacket == 0 && btresp == 2) { // Check for Bluetooth packages
                if (usart_rxdata_available()) {
                    lenpacket = usart_read_ng(rpacket, sizeof(rpacket));

                    if (lenpacket > 0) {
                    #if VERBOSE
                        DbpString(_YELLOW_("[ ") "Received Bluetooth data" _YELLOW_(" ]"));
                        Dbhexdump(lenpacket, rpacket, false);
                    #endif
                        memcpy(&dynamic_response_info.response[1], rpacket, lenpacket);
                        dynamic_response_info.response[0] = prevcmd;
                        dynamic_response_info.response_n = lenpacket + 1;
                        btresp = 1;
                    }
                }
            }
            if (receivedCmd[0] == ISO14443A_CMD_REQA && len == 1) {  // Received a REQUEST
//                    DbpString(_YELLOW_("+") "REQUEST Received");
                p_response = &responses[RESP_INDEX_ATQA];
            } else if (receivedCmd[0] == ISO14443A_CMD_HALT && len == 4) {  // Received a HALT
                DbpString(_YELLOW_("+") "Received a HALT");
                p_response = NULL;
                btresp = 0;
            } else if (receivedCmd[0] == ISO14443A_CMD_WUPA && len == 1) {  // Received a WAKEUP
//                    DbpString(_YELLOW_("+") "WAKEUP Received");
                p_response = &responses[RESP_INDEX_ATQA];
                btresp = 0;
            } else if (receivedCmd[1] == 0x20 && receivedCmd[0] == ISO14443A_CMD_ANTICOLL_OR_SELECT && len == 2) {  // Received request for UID (cascade 1)
//                   DbpString(_YELLOW_("+") "Request for UID C1");
                p_response = &responses[RESP_INDEX_UIDC1];
            } else if (receivedCmd[1] == 0x20 && receivedCmd[0] == ISO14443A_CMD_ANTICOLL_OR_SELECT_2 && len == 2) {  // Received request for UID (cascade 2)
                p_response = &responses[RESP_INDEX_UIDC2];
            } else if (receivedCmd[1] == 0x70 && receivedCmd[0] == ISO14443A_CMD_ANTICOLL_OR_SELECT && len == 9) {  // Received a SELECT (cascade 1)
//                    DbpString(_YELLOW_("+") "Request for SELECT S1");
                p_response = &responses[RESP_INDEX_SAKC1];
            } else if (receivedCmd[1] == 0x70 && receivedCmd[0] == ISO14443A_CMD_ANTICOLL_OR_SELECT_2 && len == 9) {  // Received a SELECT (cascade 2)
                p_response = &responses[RESP_INDEX_SAKC2];
            } else if (receivedCmd[0] == ISO14443A_CMD_RATS && len == 4) {  // Received a RATS request
//                    DbpString(_YELLOW_("+") "Request for RATS");
                p_response = &responses[RESP_INDEX_RATS];
                btresp = 1;
            } else if (receivedCmd[0] == 0xf2 && len == 4) {  // ACKed - Time extension
            #if VERBOSE
                DbpString(_YELLOW_("!!") "Reader accepted time extension!");
            #endif
                p_response = NULL;
            } else if ((receivedCmd[0] == 0xb2 || receivedCmd[0] == 0xb3) && len == 3) { //NACK - Request more time WTX
            #if VERBOSE
                DbpString(_YELLOW_("!!") "NACK - time extension request?");
            #endif
                if (btresp == 2 && lenpacket == 0) {
                #if VERBOSE
                    DbpString(_YELLOW_("!!") "Requesting more time - WTX");
                #endif
                    dynamic_response_info.response_n = 2;
                    dynamic_response_info.response[0] = 0xf2;
                    dynamic_response_info.response[1] = 0x0b;  // Requesting the maximum amount of time
                } else if (lenpacket == 0) {
                #if VERBOSE
                    DbpString(_YELLOW_("!!") "NACK - ACK - Resend last command!"); // To burn some time as well
                #endif
                    dynamic_response_info.response[0] = 0xa3;
                    dynamic_response_info.response_n = 1;
                } else {
                #if VERBOSE
                    DbpString(_YELLOW_("!!") "Avoiding request - Bluetooth data already in memory!!");
                #endif
                }
            } else {
                if (len >0) {
                #if VERBOSE
                    DbpString(_GREEN_("[ ") "Card reader command" _GREEN_(" ]"));
                    Dbhexdump(len, receivedCmd, false);
                #endif
                    if ((receivedCmd[0] == 0x02 || receivedCmd[0] == 0x03) && len > 3) { // Process reader commands

                        if (btresp == 1) {
                            prevcmd = receivedCmd[0];
                            bufferlen = len - 3;
                            memcpy(&buffert[0], &bufferlen, 1);
                            memcpy(&buffert[1], &receivedCmd[1], bufferlen);
                            btresp = 2;
                        }
                        if (lenpacket > 0) {
                        #if VERBOSE
                            DbpString(_YELLOW_("[ ") "Answering using Bluetooth data!" _YELLOW_(" ]"));
                        #endif
                            memcpy(&dynamic_response_info.response[1], rpacket, lenpacket);
                            dynamic_response_info.response[0] = receivedCmd[0];
                            dynamic_response_info.response_n = lenpacket + 1;
                            lenpacket = 0;
                            btresp = 1;
                        } else {
                        #if VERBOSE
                            DbpString(_YELLOW_("[ ") "New command: sent it & waiting for Bluetooth response!" _YELLOW_(" ]"));
                        #endif
                            usart_writebuffer_sync(buffert, bufferlen + 1);
                            p_response = NULL;
                            btresp=2; // added by DA 
                        }

                    } else {
                        if (lenpacket == 0 ) {
                        #if VERBOSE
                            DbpString(_YELLOW_("!!") "Received unknown command!");
                        #endif
                            memcpy(dynamic_response_info.response, receivedCmd, len);
                            dynamic_response_info.response_n = len;
                        } else  {
                        #if VERBOSE
                            DbpString(_YELLOW_("!!") "Avoiding unknown command - Bluetooth data already in memory!!");
                        #endif
                        }
                    }
                }
            }
            if (dynamic_response_info.response_n > 0) {
            #if VERBOSE
                DbpString(_GREEN_("[ ") "Proxmark3 answer" _GREEN_(" ]"));
                Dbhexdump(dynamic_response_info.response_n, dynamic_response_info.response, false);
                DbpString("----");
            #endif
                if (lenpacket > 0) {
                    lenpacket = 0;
                    btresp = 1;
                }
                // Add CRC bytes, always used in ISO 14443A-4 compliant cards
                AddCrc14A(dynamic_response_info.response, dynamic_response_info.response_n);
                dynamic_response_info.response_n += 2;

                if (prepare_tag_modulation(&dynamic_response_info, DYNAMIC_MODULATION_BUFFER_SIZE) == false) {
                    Dbprintf(_YELLOW_("[ ") "Buffer size: %d "_YELLOW_(" ]"), dynamic_response_info.response_n);
                    SpinDelay(500);
                    DbpString(_YELLOW_("!!") "Error preparing Proxmark to answer!");
                    continue;
                }
                p_response = &dynamic_response_info;
            }

            if (p_response != NULL) {
                EmSendPrecompiledCmd(p_response);
            }
        }
        switch_off();

        set_tracing(false);
        BigBuf_free_keep_EM();
        reply_ng(CMD_HF_MIFARE_SIMULATE, retval, NULL, 0);
    }

    DbpString(_YELLOW_("[=]") "exiting");
    if (usart_rxdata_available()) {
        lenpacket = usart_read_ng(rpacket, sizeof(rpacket));

        if (lenpacket > 0) {
            DbpString(_YELLOW_("[ ") "There was bluetooth data waiting in usart buffer" _YELLOW_(" ]"));
            Dbhexdump(lenpacket, rpacket, false);
        }
    }
    LEDsoff();
}
