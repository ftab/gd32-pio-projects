/*!
    \file    usbh_hid_core.c
    \brief   USB host HID class driver

    \version 2020-08-13, V3.0.0, firmware for GD32F3x0
*/

/*
    Copyright (c) 2020, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this 
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, 
       this list of conditions and the following disclaimer in the documentation 
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors 
       may be used to endorse or promote products derived from this software without 
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
OF SUCH DAMAGE.
*/

#include "usbh_pipe.h"
#include "usbh_hid_core.h"
#include "usbh_hid_mouse.h"
#include "usbh_hid_keybd.h"
#include <string.h>
#include <stdbool.h>

/* local function prototypes ('static') */
static void usbh_hiddesc_parse (usb_desc_hid *hid_desc, uint8_t *buf);
static void usbh_hid_itf_deinit (usbh_host *puhost);
static usbh_status usbh_hid_itf_init (usbh_host *puhost);
static usbh_status usbh_hid_class_req (usbh_host *puhost);
static usbh_status usbh_hid_handle (usbh_host *puhost);
static usbh_status usbh_hid_reportdesc_get (usbh_host *puhost, uint16_t len);
static usbh_status usbh_hid_sof(usbh_host *puhost);
static usbh_status usbh_hid_desc_get (usbh_host *puhost, uint16_t len);
static usbh_status usbh_set_idle (usbh_host *puhost, uint8_t duration, uint8_t report_ID);
static usbh_status usbh_set_protocol (usbh_host *puhost, uint8_t protocol);

usbh_class usbh_hid = 
{
    USB_HID_CLASS,
    usbh_hid_itf_init,
    usbh_hid_itf_deinit,
    usbh_hid_class_req,
    usbh_hid_handle,
    usbh_hid_sof
};

/*!
    \brief      get report
    \param[in]  puhost: pointer to usb host
    \param[in]  report_type: duration for HID set idle request
    \param[in]  report_ID: targeted report ID for HID set idle request
    \param[in]  report_len: length of data report to be send
    \param[in]  report_buf: report buffer
    \param[out] none
    \retval     operation status
*/
usbh_status usbh_get_report (usbh_host *puhost,
                             uint8_t  report_type,
                             uint8_t  report_ID,
                             uint8_t  report_len,
                             uint8_t *report_buf)
{
    usbh_status status = USBH_BUSY;

    if (CTL_IDLE == puhost->control.ctl_state) {
        puhost->control.setup.req = (usb_req) {
            .bmRequestType = USB_TRX_IN | USB_RECPTYPE_ITF | USB_REQTYPE_CLASS,
            .bRequest      = GET_REPORT,
            .wValue        = (report_type << 8U) | report_ID,
            .wIndex        = 0U,
            .wLength       = report_len
        };

        usbh_ctlstate_config (puhost, report_buf, report_len);
    }

    status = usbh_ctl_handler (puhost);

    return status;
}

/*!
    \brief      set report
    \param[in]  pudev: pointer to usb core instance
    \param[in]  puhost: pointer to usb host
    \param[in]  report_type: duration for HID set idle request
    \param[in]  report_ID: targeted report ID for HID set idle request
    \param[in]  report_len: length of data report to be send
    \param[in]  report_buf: report buffer
    \param[out] none
    \retval     operation status
*/
usbh_status usbh_set_report (usb_core_driver *pudev, 
                             usbh_host *puhost,
                             uint8_t  report_type,
                             uint8_t  report_ID,
                             uint8_t  report_len,
                             uint8_t *report_buf)
{
    usbh_status status = USBH_BUSY;

    if (CTL_IDLE == puhost->control.ctl_state) {
        puhost->control.setup.req = (usb_req) {
            .bmRequestType = USB_TRX_OUT | USB_RECPTYPE_ITF | USB_REQTYPE_CLASS,
            .bRequest      = SET_REPORT,
            .wValue        = (report_type << 8U) | report_ID,
            .wIndex        = 0U,
            .wLength       = report_len
        };

        usbh_ctlstate_config (puhost, report_buf, report_len);
    }

    status = usbh_ctl_handler (puhost);

    return status;
}

/*!
    \brief      de-initialize the host pipes used for the HID class
    \param[in]  puhost: pointer to usb host
    \param[out] none
    \retval     operation status
*/
void usbh_hid_itf_deinit (usbh_host *puhost)
{
    usbh_hid_handler *hid = (usbh_hid_handler *)puhost->active_class->class_data;

    if (0x00U != hid->pipe_in) {
        usb_pipe_halt (puhost->data, hid->pipe_in);

        usbh_pipe_free (puhost->data, hid->pipe_in);

        hid->pipe_in = 0U;     /* reset the pipe as free */
    }

    if (0x00U != hid->pipe_out) {
        usb_pipe_halt (puhost->data, hid->pipe_out);

        usbh_pipe_free (puhost->data, hid->pipe_out);

        hid->pipe_out = 0U; /* reset the channel as free */
    }
}

/*!
    \brief      return device type
    \param[in]  pudev: pointer to usb core instance
    \param[in]  puhost: pointer to usb host
    \param[out] none
    \retval     hid_type
*/
hid_type usbh_hid_device_type_get(usb_core_driver *pudev, usbh_host *puhost)
{
    hid_type type = HID_UNKNOWN;
    uint8_t interface_protocol;

    if (HOST_CLASS_HANDLER == puhost->cur_state) {
        interface_protocol = puhost->dev_prop.cfg_desc_set.itf_desc_set[puhost->dev_prop.cur_itf][0].itf_desc.bInterfaceProtocol;

        if (USB_HID_PROTOCOL_KEYBOARD == interface_protocol) {
            type = HID_KEYBOARD;
        } else {
            if (USB_HID_PROTOCOL_MOUSE == interface_protocol) {
                type = HID_MOUSE;
            }
        }
    }

    return type;
}

/*!
    \brief      return HID device poll time
    \param[in]  pudev: pointer to usb core instance
    \param[in]  puhost: pointer to usb host
    \param[out] none
    \retval     poll time (ms)
*/
uint8_t usbh_hid_poll_interval_get (usb_core_driver *pudev, usbh_host *puhost)
{
    usbh_hid_handler *hid = (usbh_hid_handler *)puhost->active_class->class_data;

    if ((HOST_CLASS_ENUM == puhost->cur_state) ||
         (HOST_USER_INPUT == puhost->cur_state) ||
           (HOST_CHECK_CLASS == puhost->cur_state) ||
             (HOST_CLASS_HANDLER == puhost->cur_state)) {
        return (uint8_t)(hid->poll);
    } else {
        return 0U;
    }
}

/*!
    \brief      read from FIFO
    \param[in]  fifo: fifo address
    \param[in]  buf: read buffer
    \param[in]  nbytes: number of item to read
    \param[out] none
    \retval     number of read items
*/
uint16_t usbh_hid_fifo_read (data_fifo *fifo, void *buf, uint16_t nbytes)
{
    uint16_t i = 0U;
    uint8_t *p = (uint8_t*) buf;

    if (0U == fifo->lock) {
        fifo->lock = 1U;

        for (i = 0U; i < nbytes; i++) {
            if (fifo->tail != fifo->head) {
                *p++ = fifo->buf[fifo->tail];
                fifo->tail++;

                if (fifo->tail == fifo->size) {
                    fifo->tail = 0U;
                }
            } else {
                fifo->lock = 0U;

                return i;
            }
        }
    }

    fifo->lock = 0U;

    return nbytes;
}

/*!
    \brief      write to FIFO
    \param[in]  fifo: fifo address
    \param[in]  buf: read buffer
    \param[in]  nbytes: number of item to read
    \param[out] none
    \retval     number of write items
*/
uint16_t usbh_hid_fifo_write (data_fifo *fifo, void *buf, uint16_t nbytes)
{
    uint16_t i = 0U;
    uint8_t *p = (uint8_t*) buf;

    if (0U == fifo->lock) {
        fifo->lock = 1U;

        for (i = 0U; i < nbytes; i++) {
            if ((fifo->head + 1U == fifo->tail) ||
                 ((fifo->head + 1U == fifo->size) && (0U == fifo->tail))) {
                fifo->lock = 0U;

                return i;
            } else {
                fifo->buf[fifo->head] = *p++;
                fifo->head++;
        
                if (fifo->head == fifo->size) {
                    fifo->head = 0U;
                }
            }
        }
    }

    fifo->lock = 0U;

    return nbytes;
}

/*!
    \brief      initialize FIFO
    \param[in]  fifo: fifo address
    \param[in]  buf: read buffer
    \param[in]  size: size of FIFO
    \param[out] none
    \retval     none
*/
void usbh_hid_fifo_init (data_fifo *fifo, uint8_t *buf, uint16_t size)
{
    fifo->head = 0U;
    fifo->tail = 0U;
    fifo->lock = 0U;
    fifo->size = size;
    fifo->buf = buf;
}

#include <stdio.h>

/** Buttons on the controllers */
const uint16_t XBOX_BUTTONS[]  = {
        0x0100, // UP
        0x0800, // RIGHT
        0x0200, // DOWN
        0x0400, // LEFT

        0x2000, // BACK
        0x1000, // START
        0x4000, // L3
        0x8000, // R3

        0, 0, // Skip L2 and R2 as these are analog buttons
        0x0001, // L1
        0x0002, // R1

        0x0020, // B
        0x0010, // A
        0x0040, // X
        0x0080, // Y

        0x0004, // XBOX
        0x0008, // SYNC
};


/** This enum is used to read all the different buttons on the different controllers */
typedef enum {

        /**@{*/
        /** Directional Pad Buttons - available on most controllers */
        UP = 0,
        RIGHT = 1,
        DOWN = 2,
        LEFT = 3,
        /**@}*/

        /**@{*/
        /** Playstation buttons */
        TRIANGLE,
        CIRCLE,
        CROSS,
        SQUARE,

        SELECT,
        START,

        L3,
        R3,

        L1,
        R1,
        L2,
        R2,

        PS,
        /**@}*/

        /**@{*/
        /** PS3 Move Controller */
        MOVE, // Covers 12 bits - we only need to read the top 8
        T, // Covers 12 bits - we only need to read the top 8
        /**@}*/

        /**@{*/
        /** PS Buzz controllers */
        RED,
        YELLOW,
        GREEN,
        ORANGE,
        BLUE,
        /**@}*/

        /**@{*/
        /** PS4 buttons - SHARE and OPTIONS are present instead of SELECT and START */
        SHARE,
        OPTIONS,
        TOUCHPAD,
        /**@}*/

        /**@{*/
        /** PS5 buttons */
        CREATE,
        MICROPHONE,
        /**@}*/

        /**@{*/
        /** Xbox buttons */
        A,
        B,
        X,
        Y,

        BACK,
        // START,  // listed under Playstation buttons

        LB,
        RB,
        LT,
        RT,

        XBOX,
        SYNC,

        BLACK, // Available on the original Xbox controller
        WHITE, // Available on the original Xbox controller
        /**@}*/

        /**@{*/
        /** Xbox One S buttons */
        VIEW,
        MENU,
        /**@}*/

        /**@{*/
        /** Wii buttons */
        PLUS,
        TWO,
        ONE,
        MINUS,
        HOME,
        Z,
        C,
        // B,  // listed under Xbox buttons
        // A,  // listed under Xbox buttons
        /**@}*/

        /**@{*/
        /** Wii U Pro Controller */
        L,
        R,
        ZL,
        ZR,
        /**@}*/

        /**@{*/
        /** Switch Pro Controller */
        CAPTURE,
        /**@}*/
} ButtonEnum;

static int8_t ButtonIndex(ButtonEnum key) {
    // using a chained ternary in place of a switch for constexpr on older compilers
    return
        (key == UP || key == RED) ? 0 :
        (key == RIGHT || key == YELLOW) ? 1 :
        (key == DOWN || key == GREEN) ? 2 :
        (key == LEFT || key == ORANGE) ? 3 :
        (key == SELECT || key == SHARE || key == BACK || key == VIEW || key == BLUE || key == CREATE || key == CAPTURE) ? 4 :
        (key == START || key == OPTIONS || key == MENU || key == PLUS) ? 5 :
        (key == L3 || key == TWO) ? 6 :
        (key == R3 || key == ONE) ? 7 :
        (key == L2 || key == LT || key == MINUS || key == BLACK) ? 8 :
        (key == R2 || key == RT || key == HOME || key == WHITE) ? 9 :
        (key == L1 || key == LB || key == Z) ? 10 :
        (key == R1 || key == RB || key == C) ? 11 :
        (key == TRIANGLE || key == B) ? 12 :
        (key == CIRCLE || key == A) ? 13 :
        (key == CROSS || key == X) ? 14 :
        (key == SQUARE || key == Y) ? 15 :
        (key == L || key == PS || key == XBOX) ? 16 :
        (key == R || key == MOVE || key == TOUCHPAD || key == SYNC) ? 17 :
        (key == ZL || key == T || key == MICROPHONE) ? 18 :
        (key == ZR) ? 19 :
        -1;  // not a match
}

static int8_t getButtonIndexXbox(ButtonEnum b) {
        const int8_t index = ButtonIndex(b);
        if ((uint8_t) index >= (sizeof(XBOX_BUTTONS) / sizeof(XBOX_BUTTONS[0]))) return -1;
        return index;
}


uint8_t XBOXUSB_getButtonPress(ButtonEnum b, uint32_t ButtonState) {
        const int8_t index = getButtonIndexXbox(b); if (index < 0) return 0;
        if(index == ButtonIndex(L2)) // These are analog buttons
                return (uint8_t)(ButtonState >> 8);
        else if(index == ButtonIndex(R2))
                return (uint8_t)ButtonState;
        return (bool)(ButtonState & (XBOX_BUTTONS[index] << 16));
}

static uint8_t custom_iface_input_data[64];

static usbh_status xpad_init_dev (usbh_host *puhost, uint16_t len)
{
    usbh_status status = USBH_BUSY;
    if (CTL_IDLE == puhost->control.ctl_state) {
        puhost->control.setup.req = (usb_req) {
            .bmRequestType = USB_TRX_IN | USB_RECPTYPE_ITF | USB_REQTYPE_VENDOR,
            .bRequest      = USB_CLEAR_FEATURE,
            .wValue        = 0x100, /* USB_DEVICE_REMOTE_WAKEUP */
            .wIndex        = 0U,
            .wLength       = len
        };
        usbh_ctlstate_config (puhost, puhost->dev_prop.data, len);
    }
    status = usbh_ctl_handler (puhost);
    return status;
}

usbh_status usbh_custom_interface_device_init(usb_core_driver *pudev, usbh_host *puhost) {
    usbh_hid_handler *hid = (usbh_hid_handler *)puhost->active_class->class_data;
    hid->pdata = (uint8_t*)(void *)custom_iface_input_data;
    printf("Initializing USB HID FIFO with %d report length %d elements\n", (int) sizeof(custom_iface_input_data), HID_QUEUE_SIZE);
    usbh_hid_fifo_init (&hid->fifo, puhost->dev_prop.data, HID_QUEUE_SIZE * sizeof(custom_iface_input_data));

    //send special init commands
    xpad_init_dev(puhost, 0xff);

    return USBH_OK;

}

void usbh_custom_interface_device_machine(usb_core_driver *pudev, usbh_host *puhost) {
    usbh_hid_handler *hid = (usbh_hid_handler *)puhost->active_class->class_data;

    printf("usbh_custom_interface_device_machine() called, len = %d\n", hid->len);
    if (hid->len == 0U) {
        //return USBH_FAIL;
    }
    /* fill report */
    uint16_t ret_len = 0;
    if ((ret_len = usbh_hid_fifo_read (&hid->fifo, &custom_iface_input_data, hid->len)) == hid->len) {
        printf("Read %d bytes okay\n", ret_len);
        for(uint16_t i = 0; i < ret_len; i++) {
            printf("%02x ", custom_iface_input_data[i]);
            if(i % 16 == 15) {
                printf("\n");
            }
        }
        printf("\n");
        //Code from https://github.com/felis/USB_Host_Shield_2.0/blob/master/XBOXUSB.cpp#L244
        //However, our readBuf is their readBuf advanced by 2 bytes.
        uint8_t* readBuf = (uint8_t*)(custom_iface_input_data - 2);
        uint32_t ButtonState = (uint32_t)(readBuf[5] | ((uint16_t)readBuf[4] << 8) | ((uint32_t)readBuf[3] << 16) | ((uint32_t)readBuf[2] << 24));

        int16_t hatValue[4];
        /** Joysticks on the PS3 and Xbox controllers. */
        enum AnalogHatEnum {
                /** Left joystick x-axis */
                LeftHatX = 0,
                /** Left joystick y-axis */
                LeftHatY = 1,
                /** Right joystick x-axis */
                RightHatX = 2,
                /** Right joystick y-axis */
                RightHatY = 3,
        };
        hatValue[LeftHatX] = (int16_t)(((uint16_t)readBuf[7] << 8) | readBuf[6]);
        hatValue[LeftHatY] = (int16_t)(((uint16_t)readBuf[9] << 8) | readBuf[8]);
        hatValue[RightHatX] = (int16_t)(((uint16_t)readBuf[11] << 8) | readBuf[10]);
        hatValue[RightHatY] = (int16_t)(((uint16_t)readBuf[13] << 8) | readBuf[12]);
        

        printf("Hat values: LX = %d, LY = %d, RX = %d, RY = %d\n", hatValue[LeftHatX], hatValue[LeftHatY], hatValue[RightHatX], hatValue[RightHatY]);
        ButtonEnum buttons_to_check[] = {
            UP, RIGHT, DOWN, LEFT, START, L3, R3, LB, RB, LT, RT, A, B, X, Y, BACK, START, XBOX, SYNC
        };
        const char* buttons_to_check_names[] = {
            "UP", "RIGHT", "DOWN", "LEFT", "START", "L3", "R3", "LB", "RB", "LT", "RT", "A", "B", "X", "Y", "BACK", "START", "XBOX", "SYNC"
        };

        printf("Buttons: ");
        for(int i=0; i < sizeof(buttons_to_check) / sizeof(*buttons_to_check); i++) {
            ButtonEnum btn = buttons_to_check[i];
            if(XBOXUSB_getButtonPress(btn, ButtonState) != 0) {
                printf("%s ", buttons_to_check_names[i]);
            }
        }
        printf("\n");
       // return USBH_OK;
    } else {
        printf("Read %d bytes not okay\n", ret_len);
    }
    //return USBH_FAIL;
} 

/*!
    \brief      initialize the hid class
    \param[in]  puhost: pointer to usb host
    \param[out] none
    \retval     operation status
*/
static usbh_status usbh_hid_itf_init (usbh_host *puhost)
{
    uint8_t num = 0U, ep_num = 0U, interface = 0U;
    usbh_status status = USBH_BUSY;

    interface = usbh_interface_find(&puhost->dev_prop, USB_HID_CLASS, USB_HID_SUBCLASS_BOOT_ITF, 0xFFU);

    if (0xFFU == interface && false) {
        puhost->usr_cb->dev_not_supported();

        status = USBH_FAIL;
    } else {
        if(interface == 0xFF) 
            interface = 0; // or 1

        usbh_interface_select(&puhost->dev_prop, interface);

        static usbh_hid_handler hid_handler;

        memset((void*)&hid_handler, 0, sizeof(usbh_hid_handler));

        hid_handler.state = HID_ERROR;

        uint8_t itf_protocol = puhost->dev_prop.cfg_desc_set.itf_desc_set[puhost->dev_prop.cur_itf][0].itf_desc.bInterfaceProtocol;
        if (USB_HID_PROTOCOL_KEYBOARD == itf_protocol) {
            hid_handler.init = usbh_hid_keybd_init;
            hid_handler.machine = usbh_hid_keybrd_machine;
        } else if (USB_HID_PROTOCOL_MOUSE == itf_protocol) {
            hid_handler.init = usbh_hid_mouse_init;
            hid_handler.machine = usbh_hid_mouse_machine;
        } else {
            hid_handler.init = usbh_custom_interface_device_init;
            hid_handler.machine = usbh_custom_interface_device_machine;
            //status = USBH_FAIL;
        }

        hid_handler.state = HID_INIT;
        hid_handler.ctl_state = HID_REQ_INIT;
        hid_handler.ep_addr = puhost->dev_prop.cfg_desc_set.itf_desc_set[puhost->dev_prop.cur_itf][0].ep_desc[0].bEndpointAddress;
        hid_handler.len = puhost->dev_prop.cfg_desc_set.itf_desc_set[puhost->dev_prop.cur_itf][0].ep_desc[0].wMaxPacketSize;
        hid_handler.poll = puhost->dev_prop.cfg_desc_set.itf_desc_set[puhost->dev_prop.cur_itf][0].ep_desc[0].bInterval;

        if (hid_handler.poll < HID_MIN_POLL) {
            hid_handler.poll = HID_MIN_POLL;
        }

        /* check for available number of endpoints */
        /* find the number of endpoints in the interface descriptor */
        /* choose the lower number in order not to overrun the buffer allocated */
        ep_num = USB_MIN(puhost->dev_prop.cfg_desc_set.itf_desc_set[puhost->dev_prop.cur_itf][0].itf_desc.bNumEndpoints, USBH_MAX_EP_NUM);

        /* decode endpoint IN and OUT address from interface descriptor */
        for (num = 0U; num < ep_num; num++) {
            usb_desc_ep *ep_desc = &puhost->dev_prop.cfg_desc_set.itf_desc_set[puhost->dev_prop.cur_itf][0].ep_desc[num];

            uint8_t ep_addr = ep_desc->bEndpointAddress;

            if (ep_addr & 0x80U) {
                hid_handler.ep_in = ep_addr;
                hid_handler.pipe_in = usbh_pipe_allocate (puhost->data, ep_addr);

                /* open channel for IN endpoint */
                usbh_pipe_create (puhost->data,
                                  &puhost->dev_prop, 
                                  hid_handler.pipe_in,
                                  USB_EPTYPE_INTR,
                                  hid_handler.len);

                usbh_pipe_toggle_set(puhost->data, hid_handler.pipe_in, 0U);
            } else {
                hid_handler.ep_out = ep_addr;
                hid_handler.pipe_out = usbh_pipe_allocate (puhost->data, ep_addr);

                /* open channel for OUT endpoint */
                usbh_pipe_create (puhost->data,
                                  &puhost->dev_prop,
                                  hid_handler.pipe_out,
                                  USB_EPTYPE_INTR,
                                  hid_handler.len);

                usbh_pipe_toggle_set(puhost->data, hid_handler.pipe_out, 0U);
            }
        }

        puhost->active_class->class_data = (void *)&hid_handler;

        status = USBH_OK;
    }
    printf("Returning %d\n", (int) status);
    return status;
}

/*!
    \brief      handle HID class requests for HID class
    \param[in]  puhost: pointer to usb host
    \param[out] none
    \retval     operation status
*/
static usbh_status usbh_hid_class_req (usbh_host *puhost)
{
    usbh_status status = USBH_BUSY;
    usbh_status class_req_status = USBH_BUSY;

    usbh_hid_handler *hid = (usbh_hid_handler *)puhost->active_class->class_data;

    printf("usbh_hid_class_req(): ctl_state = %d\n", hid->ctl_state);
    /* handle HID control state machine */
    usbh_status ret = 0;
    switch (hid->ctl_state) {
    case HID_REQ_INIT:
    case HID_REQ_GET_HID_DESC:
        /* get HID descriptor */ 
        if (USBH_OK == (ret = usbh_hid_desc_get (puhost, USB_HID_DESC_SIZE))) {
            printf("Got HID Descriptor! Parsing..\n");
            usbh_hiddesc_parse(&hid->hid_desc, puhost->dev_prop.data);

            hid->ctl_state = HID_REQ_GET_REPORT_DESC;
        } else {
            printf("usbh_hid_desc_get() FAILED: %d\n", ret);
            //hack: try retunrning okay anyways..
            hid->ctl_state = HID_REQ_IDLE;
            return USBH_OK;
        }
        break;

    case HID_REQ_GET_REPORT_DESC:
        /* get report descriptor */ 
        if (USBH_OK == usbh_hid_reportdesc_get(puhost, hid->hid_desc.wDescriptorLength)) {
            hid->ctl_state = HID_REQ_SET_IDLE;
        }
        break;

    case HID_REQ_SET_IDLE:
        class_req_status = usbh_set_idle (puhost, 0U, 0U);

        /* set idle */
        if (USBH_OK == class_req_status) {
            hid->ctl_state = HID_REQ_SET_PROTOCOL;
        } else {
            if(USBH_NOT_SUPPORTED == class_req_status) {
                hid->ctl_state = HID_REQ_SET_PROTOCOL;
            }
        }
        break; 

    case HID_REQ_SET_PROTOCOL:
        /* set protocol */
        if (USBH_OK == usbh_set_protocol (puhost, 0U)) {
            hid->ctl_state = HID_REQ_IDLE;

            /* all requests performed */
            status = USBH_OK;
        }
        break;

    case HID_REQ_IDLE:
    default:
        break;
    }

    return status;
}

/*!
    \brief      manage state machine for HID data transfers 
    \param[in]  puhost: pointer to usb host
    \param[out] none
    \retval     operation status
*/
static usbh_status usbh_hid_handle (usbh_host *puhost)
{
    usbh_status status = USBH_OK;
    usbh_hid_handler *hid = (usbh_hid_handler *)puhost->active_class->class_data;
    //printf("HID state: %d\n", (int)hid->state);
    switch (hid->state) {
    case HID_INIT:
        hid->init(puhost->data, puhost);
        hid->state = HID_IDLE;
        break;

    case HID_IDLE:
        hid->state = HID_SYNC;
        status = USBH_OK;
        break;

    case HID_SYNC:
        /* sync with start of even frame */
        if (true == usb_frame_even(puhost->data)) {
            hid->state = HID_GET_DATA;
        }
        break;

    case HID_GET_DATA:
        usbh_data_recev (puhost->data, hid->pdata, hid->pipe_in, hid->len);

        hid->state = HID_POLL;
        hid->timer = usb_curframe_get (puhost->data);
        hid->data_ready = 0U;
        break;

    case HID_POLL:
        if (URB_DONE == usbh_urbstate_get (puhost->data, hid->pipe_in)) {
            if (0U == hid->data_ready) { /* handle data once */
                usbh_hid_fifo_write(&hid->fifo, hid->pdata, hid->len);
                hid->data_ready = 1U;

                hid->machine(puhost->data, puhost);
            }
        } else {
            if (URB_STALL == usbh_urbstate_get (puhost->data, hid->pipe_in)) { /* IN endpoint stalled */
                /* issue clear feature on interrupt in endpoint */ 
                if (USBH_OK == (usbh_clrfeature (puhost, hid->ep_addr, hid->pipe_in))) {
                    /* change state to issue next in token */
                    hid->state = HID_GET_DATA;
                }
            }
        }
        break;

    default:
        break;
    }
    return status;
}

/*!
    \brief      send get report descriptor command to the device
    \param[in]  puhost: pointer to usb host
    \param[in]  len: HID report descriptor length
    \param[out] none
    \retval     operation status
*/
static usbh_status usbh_hid_reportdesc_get (usbh_host *puhost, uint16_t len)
{
    usbh_status status = USBH_BUSY;

    if (CTL_IDLE == puhost->control.ctl_state) {
        puhost->control.setup.req = (usb_req) {
            .bmRequestType = USB_TRX_IN | USB_RECPTYPE_ITF | USB_REQTYPE_STRD,
            .bRequest      = USB_GET_DESCRIPTOR,
            .wValue        = USBH_DESC(USB_DESCTYPE_REPORT),
            .wIndex        = 0U,
            .wLength       = len
        };

        usbh_ctlstate_config (puhost, puhost->dev_prop.data, len);
    }

    status = usbh_ctl_handler (puhost);

    return status;
}

/*!
    \brief      managing the SOF process
    \param[in]  puhost: pointer to usb host
    \param[out] none
    \retval     operation status
*/
static usbh_status usbh_hid_sof(usbh_host *puhost)
{
    usbh_hid_handler *hid = (usbh_hid_handler *)puhost->active_class->class_data;

    if (HID_POLL == hid->state) {
        uint32_t frame_count = usb_curframe_get (puhost->data);

        if ((frame_count > hid->timer) && ((frame_count - hid->timer) >= hid->poll)) {
            hid->state = HID_GET_DATA;
        } else if ((frame_count < hid->timer) && ((frame_count + 0x3FFFU - hid->timer) >= hid->poll)) {
            hid->state = HID_GET_DATA;
        } else {
            /* no operation */
        }
    }

    return USBH_OK;
}

/*!
    \brief      send the command of get HID descriptor to the device
    \param[in]  puhost: pointer to usb host
    \param[in]  len: HID descriptor length
    \param[out] none
    \retval     operation status
*/
static usbh_status usbh_hid_desc_get (usbh_host *puhost, uint16_t len)
{
    usbh_status status = USBH_BUSY;

    printf("usbh_hid_desc_get(): Current ctl state: %d\n", (int) puhost->control.ctl_state);
    if (CTL_IDLE == puhost->control.ctl_state) {
        puhost->control.setup.req = (usb_req) {
            .bmRequestType = USB_TRX_IN | USB_RECPTYPE_ITF | USB_REQTYPE_STRD,
            .bRequest      = USB_GET_DESCRIPTOR,
            .wValue        = USBH_DESC(USB_DESCTYPE_HID),
            .wIndex        = 0U,
            .wLength       = len
        };

        usbh_ctlstate_config (puhost, puhost->dev_prop.data, len);
    }

    status = usbh_ctl_handler (puhost);
    printf("usbh_ctl_handler() returned %d\n", (int) status);

    return status;
}

/*!
    \brief      set idle state
    \param[in]  puhost: pointer to usb host
    \param[in]  duration: duration for HID set idle request
    \param[in]  report_ID: targeted report ID for HID set idle request
    \param[out] none
    \retval     operation status
*/
static usbh_status usbh_set_idle (usbh_host *puhost, uint8_t duration, uint8_t report_ID)
{
    usbh_status status = USBH_BUSY;

    if (CTL_IDLE == puhost->control.ctl_state) {
        puhost->control.setup.req = (usb_req) {
            .bmRequestType = USB_TRX_OUT | USB_RECPTYPE_ITF | USB_REQTYPE_CLASS,
            .bRequest      = SET_IDLE,
            .wValue        = (duration << 8U) | report_ID,
            .wIndex        = 0U,
            .wLength       = 0U
        };

        usbh_ctlstate_config (puhost, NULL, 0U);
    }

    status = usbh_ctl_handler (puhost);

    return status;
}

/*!
    \brief      set protocol state
    \param[in]  puhost: pointer to usb host
    \param[in]  protocol: boot/report protocol
    \param[out] none
    \retval     operation status
*/
static usbh_status usbh_set_protocol (usbh_host *puhost, uint8_t protocol)
{
    usbh_status status = USBH_BUSY;

    if (CTL_IDLE == puhost->control.ctl_state) {
        puhost->control.setup.req = (usb_req) {
            .bmRequestType = USB_TRX_OUT | USB_RECPTYPE_ITF | USB_REQTYPE_CLASS,
            .bRequest      = SET_PROTOCOL,
            .wValue        = !protocol,
            .wIndex        = 0U,
            .wLength       = 0U
        };

        usbh_ctlstate_config (puhost, NULL, 0U);
    }

    status = usbh_ctl_handler (puhost);

    return status;
}

/*!
    \brief      parse the HID descriptor
    \param[in]  hid_desc: pointer to HID descriptor
    \param[in]  buf: pointer to buffer where the source descriptor is available
    \param[out] none
    \retval     none
*/
static void  usbh_hiddesc_parse (usb_desc_hid *hid_desc, uint8_t *buf)
{
    hid_desc->header.bLength         = *(uint8_t *)(buf + 0U);
    hid_desc->header.bDescriptorType = *(uint8_t *)(buf + 1U);
    hid_desc->bcdHID                 = BYTE_SWAP(buf + 2U);
    hid_desc->bCountryCode           = *(uint8_t *)(buf + 4U);
    hid_desc->bNumDescriptors        = *(uint8_t *)(buf + 5U);
    hid_desc->bDescriptorType        = *(uint8_t *)(buf + 6U);
    hid_desc->wDescriptorLength      = BYTE_SWAP(buf + 7U);
}
