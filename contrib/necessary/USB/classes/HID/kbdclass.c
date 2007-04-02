/*
    Copyright (C) 2006 by Michal Schulz
    $Id$

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Library General Public License as 
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this program; if not, write to the
    Free Software Foundation, Inc.,
    59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#define DEBUG 1

#include <aros/debug.h>
#include <aros/libcall.h>
#include <aros/asmcall.h>

#include <dos/dos.h>
#include <dos/dosextens.h>

#include <devices/inputevent.h>
#include <devices/input.h>
#include <devices/rawkeycodes.h>

#include <usb/usb.h>
#include <usb/usb_core.h>
#include <usb/hid.h>
#include "hid.h"

#include <proto/oop.h>
#include <proto/dos.h>
#include <proto/input.h>

static void kbd_process();

#define __(a) RAWKEY_##a
#define _(a) __(a)

/* 
 * Unusual convertions:
 *   PrintScreen -> Help 
 *   NumLock -> 0 (driver state change)
 */
static const uint8_t keyconv[] = {
    0xFF, 0xFF, 0xFF, 0xFF,                                     /* 00 - 03 */
    _(A), _(B), _(C), _(D), _(E), _(F), _(G), _(H),             /* 04 - 0B */
    _(I), _(J), _(K), _(L), _(M), _(N), _(O), _(P),             /* 0C - 13 */
    _(Q), _(R), _(S), _(T), _(U), _(V), _(W), _(X),             /* 14 - 1B */
    _(Y), _(Z), _(1), _(2), _(3), _(4), _(5), _(6),             /* 1C - 23 */
    _(7), _(8), _(9), _(0),                                     /* 24 - 27 */
    _(RETURN), _(ESCAPE), _(BACKSPACE), _(TAB),                 /* 28 - 2B */
    _(SPACE), _(MINUS), _(EQUAL), _(LBRACKET),                  /* 2C - 2F */
    _(RBRACKET), _(BACKSLASH), _(2B), _(SEMICOLON),             /* 30 - 33 */
    _(QUOTE), _(TILDE), _(COMMA), _(PERIOD),                    /* 34 - 37 */
    _(SLASH), _(CAPSLOCK), _(F1), _(F2),                        /* 38 - 3B */
    _(F3), _(F4), _(F5), _(F6), _(F7), _(F8), _(F9), _(F10),    /* 3C - 43 */
    _(F11), _(F12), _(HELP), 0xFF,                              /* 44 - 47 */ 
    0x6E, _(INSERT), _(HOME), _(PAGEUP),                        /* 48 - 4B */
    _(DELETE), _(END), _(PAGEDOWN), _(RIGHT),                   /* 4C - 4F */
    _(LEFT), _(DOWN), _(UP), 0x5A,                              /* 50 - 53 */
    0x5B, 0x5C, 0x5D, _(KP_PLUS),                               /* 54 - 57 */ // Keypad!!!!!!
    _(KP_ENTER), _(KP_1), _(KP_2), _(KP_3),                     /* 58 - 5B */
    _(KP_4), _(KP_5), _(KP_6), _(KP_7),                         /* 5C - 5F */
    _(KP_8), _(KP_9), _(KP_0), _(KP_DECIMAL),                   /* 60 - 63 */
    _(LESSGREATER), 0xFF, 0xFF, 0xFF,                           /* 64 - 67 */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* 68 - 6F */                                     
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* 70 - 77 */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* 78 - 7F */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* 80 - 87 */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* 88 - 8F */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* 90 - 97 */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* 98 - 9F */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* A0 - A7 */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* A8 - AF */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* B0 - B7 */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* B8 - BF */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* C0 - C7 */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* C8 - CF */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* D0 - D7 */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* D8 - DF */
    _(CONTROL), _(LSHIFT), _(LALT), _(LAMIGA),
    _(CONTROL), _(RSHIFT), _(RALT), _(RAMIGA)
};

#undef _
#undef __

void METHOD(USBKbd, Hidd_USBHID, ParseReport)
{
    KbdData *kbd = OOP_INST_DATA(cl, o);
    uint8_t *buff = msg->report;
    int i;
    
    CopyMem(kbd->code, kbd->prev_code, kbd->loc_keycnt + 1);

    for (i=0; i < kbd->loc_modcnt; i++)
    {
        if (hid_get_data(msg->report, &kbd->loc_mod[i].loc))
            kbd->code[0] |= 1 << i;
    }
    
    CopyMem(msg->report + kbd->loc_keycode.pos / 8, &kbd->code[1], kbd->loc_keycode.count);
    
    if (kbd->kbd_task)
        Signal(kbd->kbd_task, SIGBREAKF_CTRL_F);
}

OOP_Object *METHOD(USBKbd, Root, New)
{
    D(bug("[USBKbd] USBKeyboard::New()\n"));

    o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg) msg);
    if (o)
    {
        struct hid_data *d;
        struct hid_item h;
        
        KbdData *kbd = OOP_INST_DATA(cl, o);
        kbd->sd = SD(cl);
        kbd->o = o;
        kbd->hd = HIDD_USBHID_GetHidDescriptor(o);
        uint32_t flags;

        HIDD_USBHID_SetIdle(o, 500 / 4, 0);
        HIDD_USBHID_SetProtocol(o, 1);
        
        D(bug("[USBKbd::New()] Hid descriptor @ %p\n", kbd->hd));
        D(bug("[USBKbd::New()] Number of Report descriptors: %d\n", kbd->hd->bNumDescriptors));
        
        kbd->reportLength = AROS_LE2WORD(kbd->hd->descrs[0].wDescriptorLength);
        kbd->report = AllocVecPooled(SD(cl)->MemPool, kbd->reportLength);
        
        D(bug("[USBKbd::New()] Getting report descriptor of size %d\n", kbd->reportLength));
        
        HIDD_USBHID_GetReportDescriptor(o, kbd->reportLength, kbd->report);
      
        d = hid_start_parse(kbd->report, kbd->reportLength, hid_input);
        kbd->loc_modcnt = 0;
        
        while (hid_get_item(d, &h))
        {
            if (h.kind != hid_input || (h.flags & HIO_CONST) ||
                    HID_GET_USAGE_PAGE(h.usage) != HUP_KEYBOARD)
                continue;
            
            if (h.flags & HIO_VARIABLE)
            {
                kbd->loc_modcnt++;
                
                if (kbd->loc_modcnt > 8)
                {
                    bug("[USBKbd::New()] modifier code exceeds 8 bits size\n");
                    continue;
                }
                else
                {
                    D(bug("[USBKbd::New()] modifier %d, code %02x\n", kbd->loc_modcnt, HID_GET_USAGE(h.usage)));
                    kbd->loc_mod[kbd->loc_modcnt-1].loc = h.loc;
                    kbd->loc_mod[kbd->loc_modcnt-1].key = HID_GET_USAGE(h.usage);
                }
            }
            else
            {
                kbd->loc_keycode = h.loc;
                kbd->loc_keycnt = h.loc.count;
            }
        }
        hid_end_parse(d);
        
        D(bug("[USBKbd::New()] %d key modifiers\n", kbd->loc_modcnt));
        D(bug("[USBKbd::New()] This keyboard reports at most %d simultanously pressed keys\n", kbd->loc_keycnt));
        
        kbd->prev_code = AllocVecPooled(SD(cl)->MemPool, kbd->loc_keycnt + 1);
        kbd->code = AllocVecPooled(SD(cl)->MemPool, kbd->loc_keycnt + 1);
        
        struct TagItem tags[] = {
                { NP_Entry,     (intptr_t)kbd_process },
                { NP_UserData,  (intptr_t)kbd },                
                { NP_Priority,  50 },
                { NP_Name,      (intptr_t)"HID Keyboard" },
                { TAG_DONE,     0UL },
        };

        kbd->kbd_task = CreateNewProc(tags);
    }
    
    return o;
}

struct pRoot_Dispose {
    OOP_MethodID        mID;
};

void METHOD(USBKbd, Root, Dispose)
{
    KbdData *kbd = OOP_INST_DATA(cl, o);
    
    Signal(kbd->kbd_task, SIGBREAKF_CTRL_C);
    
    if (kbd->report)
        FreeVecPooled(SD(cl)->MemPool, kbd->report);
    
    if (kbd->code)
        FreeVecPooled(SD(cl)->MemPool, kbd->code);

    if (kbd->prev_code)
        FreeVecPooled(SD(cl)->MemPool, kbd->prev_code);

    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

/* Maximal number of input events before forced flush is issued */
#define IEC_MAX 16

static inline ie_send(struct IOStdReq *req, struct InputEvent *ie, int iec)
{
    if (iec)
    {
        req->io_Data = ie;
        req->io_Length = iec * sizeof(struct InputEvent);
        req->io_Command = IND_ADDEVENT;
    
        DoIO(req);
    }
}

#define IE_NEXT \
    do {                                \
        iec++;                          \
        if (iec == IEC_MAX) {           \
            ie_send(req, ie, iec);      \
            iec = 0;                    \
        }                               \
    } while(0);

static void kbd_process()
{
    KbdData *kbd = (KbdData *)(FindTask(NULL)->tc_UserData);
    struct hid_staticdata *sd = kbd->sd;
    OOP_Object *o = kbd->o;
    OOP_Class *cl = sd->hidClass;
    uint32_t sigset;
    
    struct MsgPort *port = CreateMsgPort();
    struct IOStdReq *req = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
    struct Device *InputBase;
    
    struct InputEvent *ie = AllocVec(IEC_MAX * sizeof(struct InputEvent), MEMF_PUBLIC | MEMF_CLEAR);
    int iec;

    if (OpenDevice("input.device", 0, (struct IORequest *)req, 0))
    {
        DeleteIORequest((struct IORequest *)req);
        DeleteMsgPort(port);
        kbd->kbd_task = NULL;
        
        bug("[Kbd] Failed to open input.device\n");
        
        return;
    }
    
    InputBase = req->io_Device;
    
    for (;;)
    {
        sigset = Wait(SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F);
        
        if (sigset & SIGBREAKF_CTRL_C)
        {
            D(bug("[Kbd] USB mouse detached. Cleaning up\n"));
            
            CloseDevice((struct IORequest *)req);
            DeleteIORequest((struct IORequest *)req);
            DeleteMsgPort(port);
            FreeVec(ie);
            return;
        }
        
        if (sigset & SIGBREAKF_CTRL_F)
        {
            int i;
            
            iec = 0;
            uint16_t qual = PeekQualifier() & ~(0x1f);
            
            /* Process qualifiers */
            
            
            /* Check all new keycode buffers */
            for (i=0; i < kbd->loc_keycnt; i++)
            {
                int j;
                
                /* Code == 0? Ignore */
                if (!kbd->code[i+1])
                    continue;
                
                /* In case of failure assume that the previous report is the current one too
                 * and do nothing else. */
                if (kbd->code[i+1] == 1)
                {
                    CopyMem(kbd->prev_code, kbd->code, kbd->loc_keycnt + 1);
                    bug("[USBKbd] ERROR! Too many keys pressed at once?\n");
                    return;
                }
                
                /* Check whether this code exists in previous buffer */
                for (j=0; j < kbd->loc_keycnt; j++)
                    if (kbd->code[i+1] == kbd->prev_code[j+1])
                        break;
                
                /* Not in previous buffer. KeyDown event */
                if (j >= kbd->loc_keycnt && keyconv[kbd->code[i+1]] != 0xff)
                {            
                    ie[iec].ie_Class            = IECLASS_RAWKEY;
                    ie[iec].ie_SubClass         = 0;
                    if (kbd->code[i+1] < sizeof(keyconv))
                        ie[iec].ie_Code         = keyconv[kbd->code[i+1]];
                    else
                        ie[iec].ie_Code         = 0;
                    ie[iec].ie_Qualifier        = qual;
                    if (kbd->code[i+1] >= 0x54 &&  kbd->code[i+1] <= 0x63)
                        ie[iec].ie_Qualifier    |= IEQUALIFIER_NUMERICPAD;
                    
                    ie[iec].ie_position.ie_dead.ie_prev1DownCode = kbd->prev_key;
                    ie[iec].ie_position.ie_dead.ie_prev1DownQual = kbd->prev_qual;
                    
                    ie[iec].ie_position.ie_dead.ie_prev2DownCode = kbd->prev_prev_key;
                    ie[iec].ie_position.ie_dead.ie_prev2DownQual = kbd->prev_prev_qual;
                    
                    IE_NEXT
                    
                    kbd->prev_prev_key = kbd->prev_key;
                    kbd->prev_prev_qual = kbd->prev_qual;
                    
                    kbd->prev_key = ie[iec].ie_Code;
                    kbd->prev_qual = ie[iec].ie_Qualifier;
                    
                    D(bug("[Kbd] KeyDown event for key %02x->%02x\n", kbd->code[i+1], ie[iec].ie_Code));
                }
            }
            
            /* check all old keycode buffers */
            for (i=0; i < kbd->loc_keycnt; i++)
            {
                int j;
                
                /* Code == 0? Ignore */
                if (!kbd->prev_code[i+1])
                    continue;
               
                /* Check whether this code exists in previous buffer */
                for (j=0; j < kbd->loc_keycnt; j++)
                    if (kbd->prev_code[i+1] == kbd->code[j+1])
                        break;
                
                /* Not in previous buffer. KeyDown event */
                if (j >= kbd->loc_keycnt && keyconv[kbd->prev_code[i+1]] != 0xff)
                {
                    ie[iec].ie_Class            = IECLASS_RAWKEY;
                    ie[iec].ie_SubClass         = 0;
                    if (kbd->prev_code[i+1] < sizeof(keyconv))
                        ie[iec].ie_Code         = keyconv[kbd->prev_code[i+1]];
                    else
                        ie[iec].ie_Code         = 0;
                    ie[iec].ie_Qualifier        = qual;
                    if (kbd->prev_code[i+1] >= 0x54 &&  kbd->prev_code[i+1] <= 0x63)
                        ie[iec].ie_Qualifier    |= IEQUALIFIER_NUMERICPAD;
                    
                    ie[iec].ie_Code |= IECODE_UP_PREFIX;

                    ie[iec].ie_position.ie_dead.ie_prev1DownCode = kbd->prev_key;
                    ie[iec].ie_position.ie_dead.ie_prev1DownQual = kbd->prev_qual;
                    
                    ie[iec].ie_position.ie_dead.ie_prev2DownCode = kbd->prev_prev_key;
                    ie[iec].ie_position.ie_dead.ie_prev2DownQual = kbd->prev_prev_qual;

                    IE_NEXT
                    
                    D(bug("[Kbd] KeyUp event for key %02x->%02x\n", kbd->prev_code[i+1], ie[iec].ie_Code));
                }
            }
            
            ie_send(req, ie, iec);
        }
    }    
}
