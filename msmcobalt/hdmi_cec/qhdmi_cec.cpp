/*
* Copyright (c) 2014, 2016, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define DEBUG 0
#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)
#include <cstdlib>
#include <cutils/log.h>
#include <errno.h>
#include <fcntl.h>
#include <hardware/hdmi_cec.h>
#include <utils/Trace.h>
#include "qhdmi_cec.h"
#include "QHDMIClient.h"

namespace qhdmicec {

const int NUM_HDMI_PORTS = 1;
const int MAX_SYSFS_DATA = 128;
const int MAX_CEC_FRAME_SIZE = 20;
const int MAX_SEND_MESSAGE_RETRIES = 1;

enum {
    LOGICAL_ADDRESS_SET   =  1,
    LOGICAL_ADDRESS_UNSET = -1,
};

// Offsets of members of struct hdmi_cec_msg
// drivers/video/msm/mdss/mdss_hdmi_cec.c
// XXX: Get this from a driver header
enum {
    CEC_OFFSET_SENDER_ID,
    CEC_OFFSET_RECEIVER_ID,
    CEC_OFFSET_OPCODE,
    CEC_OFFSET_OPERAND,
    CEC_OFFSET_FRAME_LENGTH = 17,
    CEC_OFFSET_RETRANSMIT,
};

//Forward declarations
static void cec_close_context(cec_context_t* ctx __unused);
static int cec_enable(cec_context_t *ctx, int enable);
static int cec_is_connected(const struct hdmi_cec_device* dev, int port_id);

static ssize_t read_node(const char *path, char *data)
{
    ssize_t err = 0;
    FILE *fp = NULL;
    err = access(path, R_OK);
    if (!err) {
        fp = fopen(path, "r");
        if (fp) {
            err = fread(data, sizeof(char), MAX_SYSFS_DATA ,fp);
            fclose(fp);
        }
    }
    return err;
}

static ssize_t write_node(const char *path, const char *data, size_t len)
{
    ssize_t err = 0;
    int fd = -1;
    err = access(path, W_OK);
    if (!err) {
        fd = open(path, O_WRONLY);
        errno = 0;
        err = write(fd, data, len);
        if (err < 0) {
            err = -errno;
        }
        close(fd);
    } else {
        ALOGE("%s: Failed to access path: %s error: %s",
                __FUNCTION__, path, strerror(errno));
        err = -errno;
    }
    return err;
}

// Helper function to write integer values to the full sysfs path
static ssize_t write_int_to_node(cec_context_t *ctx,
        const char *path_postfix,
        const int value)
{
    char sysfs_full_path[MAX_PATH_LENGTH];
    char sysfs_data[MAX_SYSFS_DATA];
    snprintf(sysfs_data, sizeof(sysfs_data), "%d",value);
    snprintf(sysfs_full_path,sizeof(sysfs_full_path), "%s/%s",
            ctx->fb_sysfs_path, path_postfix);
    ssize_t err = write_node(sysfs_full_path, sysfs_data, strlen(sysfs_data));
    return err;
}

static void hex_to_string(const char *msg, ssize_t len, char *str)
{
    //Functions assumes sufficient memory in str
    char *ptr = str;
    for(int i=0; i < len ; i++) {
        ptr += snprintf(ptr, 3,  "%02X", msg[i]);
        // Overwrite null termination of snprintf in all except the last byte
        if (i < len - 1)
            *ptr = ':';
        ptr++;
    }
}

static ssize_t cec_get_fb_node_number(cec_context_t *ctx)
{
    //XXX: Do this from a common utility library across the display HALs
    const int MAX_FB_DEVICES = 2;
    ssize_t len = 0;
    char fb_type_path[MAX_PATH_LENGTH];
    char fb_type[MAX_SYSFS_DATA];
    const char *dtv_panel_str = "dtv panel";

    for(int num = 0; num < MAX_FB_DEVICES; num++) {
        snprintf(fb_type_path, sizeof(fb_type_path),"%s%d/msm_fb_type",
                SYSFS_BASE,num);
        ALOGD_IF(DEBUG, "%s: num: %d fb_type_path: %s", __FUNCTION__, num, fb_type_path);
        len = read_node(fb_type_path, fb_type);
        ALOGD_IF(DEBUG, "%s: fb_type:%s", __FUNCTION__, fb_type);
        if(len > 0 && (strncmp(fb_type, dtv_panel_str, strlen(dtv_panel_str)) == 0)){
            ALOGD_IF(DEBUG, "%s: Found DTV panel at fb%d", __FUNCTION__, num);
            ctx->fb_num = num;
            snprintf(ctx->fb_sysfs_path, sizeof(ctx->fb_sysfs_path),
                    "%s%d", SYSFS_BASE, num);
            break;
        }
    }
    if (len < 0)
        return len;
    else
        return 0;
}

static int cec_add_logical_address(const struct hdmi_cec_device* dev,
        cec_logical_address_t addr)
{
    if (addr <  CEC_ADDR_TV || addr > CEC_ADDR_BROADCAST) {
        ALOGE("%s: Received invalid address: %d ", __FUNCTION__, addr);
        return -EINVAL;
    }
    cec_context_t* ctx = (cec_context_t*)(dev);
    ctx->logical_address[addr] = LOGICAL_ADDRESS_SET;

    //XXX: We can get multiple logical addresses here but we can only send one
    //to the driver. Store locally for now
    ssize_t err = write_int_to_node(ctx, "cec/logical_addr", addr);
    ALOGI("%s: Allocated logical address: %d ", __FUNCTION__, addr);
    return (int) err;
}

static void cec_clear_logical_address(const struct hdmi_cec_device* dev)
{
    cec_context_t* ctx = (cec_context_t*)(dev);
    memset(ctx->logical_address, LOGICAL_ADDRESS_UNSET,
            sizeof(ctx->logical_address));
    //XXX: Find logical_addr that needs to be reset
    write_int_to_node(ctx, "cec/logical_addr", 15);
    ALOGD_IF(DEBUG, "%s: Cleared logical addresses", __FUNCTION__);
}

static int cec_get_physical_address(const struct hdmi_cec_device* dev,
        uint16_t* addr)
{
    cec_context_t* ctx = (cec_context_t*)(dev);
    char pa_path[MAX_PATH_LENGTH];
    char pa_data[MAX_SYSFS_DATA];
    snprintf (pa_path, sizeof(pa_path),"%s/pa",
            ctx->fb_sysfs_path);
    int err = (int) read_node(pa_path, pa_data);
    *addr = (uint16_t) atoi(pa_data);
    ALOGD_IF(DEBUG, "%s: Physical Address: 0x%x", __FUNCTION__, *addr);
    if (err < 0)
        return err;
    else
        return 0;
}

static int cec_send_message(const struct hdmi_cec_device* dev,
        const cec_message_t* msg)
{
    ATRACE_CALL();
    if(cec_is_connected(dev, 0) <= 0)
        return HDMI_RESULT_FAIL;

    cec_context_t* ctx = (cec_context_t*)(dev);
    ALOGD_IF(DEBUG, "%s: initiator: %d destination: %d length: %u",
            __FUNCTION__, msg->initiator, msg->destination,
            (uint32_t) msg->length);

    // Dump message received from framework
    char dump[128];
    if(msg->length > 0) {
        hex_to_string((char*)msg->body, msg->length, dump);
        ALOGD_IF(DEBUG, "%s: message from framework: %s", __FUNCTION__, dump);
    }

    char write_msg_path[MAX_PATH_LENGTH];
    char write_msg[MAX_CEC_FRAME_SIZE];
    memset(write_msg, 0, sizeof(write_msg));
    // See definition of struct hdmi_cec_msg in driver code
    // drivers/video/msm/mdss/mdss_hdmi_cec.c
    // Write header block
    // XXX: Include this from header in kernel
    write_msg[CEC_OFFSET_SENDER_ID] = msg->initiator;
    write_msg[CEC_OFFSET_RECEIVER_ID] = msg->destination;
    //Kernel splits opcode/operand, but Android sends it in one byte array
    write_msg[CEC_OFFSET_OPCODE] = msg->body[0];
    if(msg->length > 1) {
        memcpy(&write_msg[CEC_OFFSET_OPERAND], &msg->body[1],
                sizeof(char)*(msg->length - 1));
    }
    //msg length + initiator + destination
    write_msg[CEC_OFFSET_FRAME_LENGTH] = (unsigned char) (msg->length + 1);
    hex_to_string(write_msg, sizeof(write_msg), dump);
    ALOGD_IF(DEBUG, "%s: message to driver: %s", __FUNCTION__, dump);
    snprintf(write_msg_path, sizeof(write_msg_path), "%s/cec/wr_msg",
            ctx->fb_sysfs_path);
    int retry_count = 0;
    ssize_t err = 0;
    //HAL spec requires us to retry at least once.
    while (true) {
        err = write_node(write_msg_path, write_msg, sizeof(write_msg));
        retry_count++;
        if (err == -EAGAIN && retry_count <= MAX_SEND_MESSAGE_RETRIES) {
            ALOGE("%s: CEC line busy, retrying", __FUNCTION__);
        } else {
            break;
        }
    }

    if (err < 0) {
       if (err == -ENXIO) {
           ALOGI("%s: No device exists with the destination address",
                   __FUNCTION__);
           return HDMI_RESULT_NACK;
       } else if (err == -EAGAIN) {
            ALOGE("%s: CEC line is busy, max retry count exceeded",
                    __FUNCTION__);
            return HDMI_RESULT_BUSY;
        } else {
            return HDMI_RESULT_FAIL;
            ALOGE("%s: Failed to send CEC message err: %zd - %s",
                    __FUNCTION__, err, strerror(int(-err)));
        }
    } else {
        ALOGD_IF(DEBUG, "%s: Sent CEC message - %zd bytes written",
                __FUNCTION__, err);
        return HDMI_RESULT_SUCCESS;
    }
}

void cec_receive_message(cec_context_t *ctx, char *msg, ssize_t len)
{
    if(!ctx->system_control)
        return;

    char dump[128];
    if(len > 0) {
        hex_to_string(msg, len, dump);
        ALOGD_IF(DEBUG, "%s: Message from driver: %s", __FUNCTION__, dump);
    }

    hdmi_event_t event;
    event.type = HDMI_EVENT_CEC_MESSAGE;
    event.dev = (hdmi_cec_device *) ctx;
    // Remove initiator/destination from this calculation
    event.cec.length = msg[CEC_OFFSET_FRAME_LENGTH] - 1;
    event.cec.initiator = (cec_logical_address_t) msg[CEC_OFFSET_SENDER_ID];
    event.cec.destination = (cec_logical_address_t) msg[CEC_OFFSET_RECEIVER_ID];
    //Copy opcode and operand
    memcpy(event.cec.body, &msg[CEC_OFFSET_OPCODE], event.cec.length);
    hex_to_string((char *) event.cec.body, event.cec.length, dump);
    ALOGD_IF(DEBUG, "%s: Message to framework: %s", __FUNCTION__, dump);
    ctx->callback.callback_func(&event, ctx->callback.callback_arg);
}

void cec_hdmi_hotplug(cec_context_t *ctx, int connected)
{
    //Ignore unplug events when system control is disabled
    if(!ctx->system_control && connected == 0)
        return;
    hdmi_event_t event;
    event.type = HDMI_EVENT_HOT_PLUG;
    event.dev = (hdmi_cec_device *) ctx;
    event.hotplug.connected = connected ? HDMI_CONNECTED : HDMI_NOT_CONNECTED;
    ctx->callback.callback_func(&event, ctx->callback.callback_arg);
}

static void cec_register_event_callback(const struct hdmi_cec_device* dev,
            event_callback_t callback, void* arg)
{
    ALOGD_IF(DEBUG, "%s: Registering callback", __FUNCTION__);
    cec_context_t* ctx = (cec_context_t*)(dev);
    ctx->callback.callback_func = callback;
    ctx->callback.callback_arg = arg;
}

static void cec_get_version(const struct hdmi_cec_device* dev, int* version)
{
    cec_context_t* ctx = (cec_context_t*)(dev);
    *version = ctx->version;
    ALOGD_IF(DEBUG, "%s: version: %d", __FUNCTION__, *version);
}

static void cec_get_vendor_id(const struct hdmi_cec_device* dev,
        uint32_t* vendor_id)
{
    cec_context_t* ctx = (cec_context_t*)(dev);
    *vendor_id = ctx->vendor_id;
    ALOGD_IF(DEBUG, "%s: vendor id: %u", __FUNCTION__, *vendor_id);
}

static void cec_get_port_info(const struct hdmi_cec_device* dev,
            struct hdmi_port_info* list[], int* total)
{
    ALOGD_IF(DEBUG, "%s: Get port info", __FUNCTION__);
    cec_context_t* ctx = (cec_context_t*)(dev);
    *total = NUM_HDMI_PORTS;
    *list = ctx->port_info;
}

static void cec_set_option(const struct hdmi_cec_device* dev, int flag,
        int value)
{
    cec_context_t* ctx = (cec_context_t*)(dev);
    switch (flag) {
        case HDMI_OPTION_WAKEUP:
            ALOGD_IF(DEBUG, "%s: Wakeup: value: %d", __FUNCTION__, value);
            //XXX
            break;
        case HDMI_OPTION_ENABLE_CEC:
            ALOGD_IF(DEBUG, "%s: Enable CEC: value: %d", __FUNCTION__, value);
            cec_enable(ctx, value? 1 : 0);
            break;
        case HDMI_OPTION_SYSTEM_CEC_CONTROL:
            ALOGD_IF(DEBUG, "%s: system_control: value: %d",
                    __FUNCTION__, value);
            ctx->system_control = !!value;
            break;
    }
}

static void cec_set_audio_return_channel(const struct hdmi_cec_device* dev,
        int port, int flag)
{
    cec_context_t* ctx = (cec_context_t*)(dev);
    ctx->arc_enabled = flag ? true : false;
    ALOGD_IF(DEBUG, "%s: ARC flag: %d port: %d", __FUNCTION__, flag, port);
}

static int cec_is_connected(const struct hdmi_cec_device* dev, int port_id)
{
    // Ignore port_id since we have only one port
    int connected = 0;
    cec_context_t* ctx = (cec_context_t*)(dev);
    char connected_path[MAX_PATH_LENGTH];
    char connected_data[MAX_SYSFS_DATA];
    snprintf (connected_path, sizeof(connected_path),"%s/connected",
            ctx->fb_sysfs_path);
    ssize_t err = read_node(connected_path, connected_data);
    connected = atoi(connected_data);

    ALOGD_IF(DEBUG, "%s: HDMI at port %d is - %s", __FUNCTION__, port_id,
            connected ? "connected":"disconnected");
    if (err < 0)
        return (int) err;
    else
        return connected;
}

static int cec_device_close(struct hw_device_t *dev)
{
    ALOGD_IF(DEBUG, "%s: Close CEC HAL ", __FUNCTION__);
    if (!dev) {
        ALOGE("%s: NULL device pointer", __FUNCTION__);
        return -EINVAL;
    }
    cec_context_t* ctx = (cec_context_t*)(dev);
    cec_close_context(ctx);
    free(dev);
    return 0;
}

static int cec_enable(cec_context_t *ctx, int enable)
{
    ssize_t err;
    // Enable CEC
    int value = enable ? 0x3 : 0x0;
    err = write_int_to_node(ctx, "cec/enable", value);
    if(err < 0) {
        ALOGE("%s: Failed to toggle CEC: enable: %d",
                __FUNCTION__, enable);
        return (int) err;
    }
    ctx->enabled = enable;
    return 0;
}

static void cec_init_context(cec_context_t *ctx)
{
    ALOGD_IF(DEBUG, "%s: Initializing context", __FUNCTION__);
    cec_get_fb_node_number(ctx);

    //Initialize ports - We support only one output port
    ctx->port_info = new hdmi_port_info[NUM_HDMI_PORTS];
    ctx->port_info[0].type = HDMI_OUTPUT;
    ctx->port_info[0].port_id = 1;
    ctx->port_info[0].cec_supported = 1;
    //XXX: Enable ARC if supported
    ctx->port_info[0].arc_supported = 0;
    cec_get_physical_address((hdmi_cec_device *) ctx,
            &ctx->port_info[0].physical_address );

    ctx->version = 0x4;
    ctx->vendor_id = 0xA47733;
    cec_clear_logical_address((hdmi_cec_device_t*)ctx);

    //Set up listener for HDMI events
    ctx->disp_client = new qClient::QHDMIClient();
    ctx->disp_client->setCECContext(ctx);
    ctx->disp_client->registerClient(ctx->disp_client);

    //Enable CEC - framework expects it to be enabled by default
    cec_enable(ctx, true);

    ALOGD("%s: CEC enabled", __FUNCTION__);
}

static void cec_close_context(cec_context_t* ctx __unused)
{
    ALOGD("%s: Closing context", __FUNCTION__);
}

static int cec_device_open(const struct hw_module_t* module,
        const char* name,
        struct hw_device_t** device)
{
    ALOGD_IF(DEBUG, "%s: name: %s", __FUNCTION__, name);
    int status = -EINVAL;
    if (!strcmp(name, HDMI_CEC_HARDWARE_INTERFACE )) {
        struct cec_context_t *dev;
        dev = (cec_context_t *) calloc (1, sizeof(*dev));
        if (dev) {
            cec_init_context(dev);

            //Setup CEC methods
            dev->device.common.tag       = HARDWARE_DEVICE_TAG;
            dev->device.common.version   = HDMI_CEC_DEVICE_API_VERSION_1_0;
            dev->device.common.module    = const_cast<hw_module_t* >(module);
            dev->device.common.close     = cec_device_close;
            dev->device.add_logical_address = cec_add_logical_address;
            dev->device.clear_logical_address = cec_clear_logical_address;
            dev->device.get_physical_address = cec_get_physical_address;
            dev->device.send_message = cec_send_message;
            dev->device.register_event_callback = cec_register_event_callback;
            dev->device.get_version = cec_get_version;
            dev->device.get_vendor_id = cec_get_vendor_id;
            dev->device.get_port_info = cec_get_port_info;
            dev->device.set_option = cec_set_option;
            dev->device.set_audio_return_channel = cec_set_audio_return_channel;
            dev->device.is_connected = cec_is_connected;

            *device = &dev->device.common;
            status = 0;
        } else {
            status = -EINVAL;
        }
    }
    return status;
}
}; //namespace qhdmicec

// Standard HAL module, should be outside qhdmicec namespace
static struct hw_module_methods_t cec_module_methods = {
        .open = qhdmicec::cec_device_open
};

hdmi_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .version_major = 1,
        .version_minor = 0,
        .id = HDMI_CEC_HARDWARE_MODULE_ID,
        .name = "QTI HDMI CEC module",
        .author = "The Linux Foundation",
        .methods = &cec_module_methods,
    }
};


