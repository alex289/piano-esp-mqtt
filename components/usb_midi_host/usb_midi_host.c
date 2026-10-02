#include "usb_midi_host.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb/usb_host.h"

static const char *TAG = "usb_midi";

#define USB_CLASS_AUDIO              0x01
#define USB_SUBCLASS_MIDISTREAMING   0x03

#define ACTION_OPEN_DEV   BIT0
#define ACTION_CLOSE_DEV  BIT1

#define DAEMON_TASK_STACK 4096

typedef struct {
    usb_midi_host_config_t   cfg;
    usb_host_client_handle_t client;
    usb_device_handle_t      dev;
    uint8_t                  dev_addr;
    uint8_t                  intf_num;
    uint8_t                  intf_alt;
    bool                     intf_claimed;
    usb_transfer_t          *in_transfer;
    volatile uint32_t        actions;
    volatile bool            connected;
    volatile bool            streaming;
} midi_host_t;

static midi_host_t s_host;

bool usb_midi_host_is_connected(void)
{
    return s_host.connected;
}

/* --------------------------------------------------------------------------
 * USB-MIDI 1.0 event packet decoding
 *
 * Every packet is exactly 4 bytes: a header byte carrying the virtual cable
 * number (high nibble) and the Code Index Number (low nibble), followed by up
 * to three MIDI bytes. CINs 0x8..0xE mirror the MIDI status nibbles, which is
 * everything we care about here; sysex (0x4..0x7) and single bytes such as
 * active sensing (0xF) are skipped.
 * ------------------------------------------------------------------------ */
static void decode_packets(const uint8_t *buf, int len)
{
    for (int i = 0; i + 4 <= len; i += 4) {
        const uint8_t header = buf[i];
        if (header == 0) {
            continue; /* padding */
        }
        const uint8_t cin = header & 0x0F;
        if (cin < 0x8 || cin > 0xE) {
            continue;
        }
        const uint8_t status = buf[i + 1];
        if ((status & 0x80) == 0) {
            continue; /* not a status byte, malformed packet */
        }

        usb_midi_msg_t msg = {
            .cable   = (uint8_t)(header >> 4),
            .status  = (uint8_t)(status & 0xF0),
            .channel = (uint8_t)(status & 0x0F),
            .data1   = buf[i + 2],
            .data2   = buf[i + 3],
        };
        if (s_host.cfg.on_message) {
            s_host.cfg.on_message(&msg, s_host.cfg.arg);
        }
    }
}

static void in_transfer_cb(usb_transfer_t *transfer)
{
    switch (transfer->status) {
    case USB_TRANSFER_STATUS_COMPLETED:
        decode_packets(transfer->data_buffer, transfer->actual_num_bytes);
        break;
    case USB_TRANSFER_STATUS_TIMED_OUT:
        break; /* nothing arrived, just ask again */
    case USB_TRANSFER_STATUS_NO_DEVICE:
    case USB_TRANSFER_STATUS_CANCELED:
        s_host.streaming = false;
        return;
    case USB_TRANSFER_STATUS_STALL:
        ESP_LOGW(TAG, "bulk IN stalled, stopping stream");
        s_host.streaming = false;
        return;
    default:
        ESP_LOGW(TAG, "transfer status %d", transfer->status);
        break;
    }

    if (!s_host.streaming) {
        return;
    }
    esp_err_t err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "resubmit failed: %s", esp_err_to_name(err));
        s_host.streaming = false;
    }
}

/*
 * Walks the active configuration descriptor looking for a MIDIStreaming
 * interface with a bulk IN endpoint.
 */
static bool find_midi_interface(const usb_config_desc_t *cfg,
                                uint8_t *intf_num, uint8_t *intf_alt,
                                uint8_t *ep_addr, uint16_t *ep_mps)
{
    int offset = 0;
    const usb_standard_desc_t *desc = (const usb_standard_desc_t *)cfg;
    bool in_midi_intf = false;
    uint8_t cand_num = 0, cand_alt = 0;

    while ((desc = usb_parse_next_descriptor(desc, cfg->wTotalLength, &offset)) != NULL) {
        if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const usb_intf_desc_t *intf = (const usb_intf_desc_t *)desc;
            in_midi_intf = (intf->bInterfaceClass == USB_CLASS_AUDIO &&
                            intf->bInterfaceSubClass == USB_SUBCLASS_MIDISTREAMING);
            if (in_midi_intf) {
                cand_num = intf->bInterfaceNumber;
                cand_alt = intf->bAlternateSetting;
                ESP_LOGI(TAG, "MIDIStreaming interface %u alt %u", cand_num, cand_alt);
            }
        } else if (in_midi_intf && desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
            const usb_ep_desc_t *ep = (const usb_ep_desc_t *)desc;
            const bool is_bulk = (USB_EP_DESC_GET_XFERTYPE(ep) == USB_TRANSFER_TYPE_BULK);
            const bool is_in = (USB_EP_DESC_GET_EP_DIR(ep) != 0);
            if (is_bulk && is_in) {
                *intf_num = cand_num;
                *intf_alt = cand_alt;
                *ep_addr = ep->bEndpointAddress;
                *ep_mps = (uint16_t)(ep->wMaxPacketSize & 0x07FF);
                return true;
            }
        }
    }
    return false;
}

static void close_device(void)
{
    s_host.streaming = false;

    if (s_host.in_transfer) {
        usb_host_endpoint_halt(s_host.dev, s_host.in_transfer->bEndpointAddress);
        usb_host_endpoint_flush(s_host.dev, s_host.in_transfer->bEndpointAddress);
        usb_host_transfer_free(s_host.in_transfer);
        s_host.in_transfer = NULL;
    }
    if (s_host.intf_claimed) {
        usb_host_interface_release(s_host.client, s_host.dev, s_host.intf_num);
        s_host.intf_claimed = false;
    }
    if (s_host.dev) {
        usb_host_device_close(s_host.client, s_host.dev);
        s_host.dev = NULL;
    }
    if (s_host.connected) {
        s_host.connected = false;
        ESP_LOGI(TAG, "MIDI device disconnected");
        if (s_host.cfg.on_connection) {
            s_host.cfg.on_connection(false, NULL, s_host.cfg.arg);
        }
    }
}

static void open_device(void)
{
    const usb_config_desc_t *cfg_desc = NULL;
    const usb_device_desc_t *dev_desc = NULL;
    uint8_t ep_addr = 0;
    uint16_t ep_mps = 0;

    esp_err_t err = usb_host_device_open(s_host.client, s_host.dev_addr, &s_host.dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "device_open failed: %s", esp_err_to_name(err));
        return;
    }
    if (usb_host_get_device_descriptor(s_host.dev, &dev_desc) != ESP_OK ||
        usb_host_get_active_config_descriptor(s_host.dev, &cfg_desc) != ESP_OK) {
        ESP_LOGE(TAG, "could not read descriptors");
        goto fail;
    }

    ESP_LOGI(TAG, "device %04x:%04x attached", dev_desc->idVendor, dev_desc->idProduct);

    if (!find_midi_interface(cfg_desc, &s_host.intf_num, &s_host.intf_alt, &ep_addr, &ep_mps)) {
        ESP_LOGW(TAG, "no USB MIDI interface found, ignoring this device");
        goto fail;
    }

    err = usb_host_interface_claim(s_host.client, s_host.dev, s_host.intf_num, s_host.intf_alt);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "interface_claim failed: %s", esp_err_to_name(err));
        goto fail;
    }
    s_host.intf_claimed = true;

    /* IN transfers must be a whole number of max packets. */
    if (ep_mps == 0) {
        ep_mps = 64;
    }
    const size_t buf_size = (size_t)ep_mps * 2;
    err = usb_host_transfer_alloc(buf_size, 0, &s_host.in_transfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "transfer_alloc failed: %s", esp_err_to_name(err));
        goto fail;
    }

    s_host.in_transfer->device_handle = s_host.dev;
    s_host.in_transfer->bEndpointAddress = ep_addr;
    s_host.in_transfer->callback = in_transfer_cb;
    s_host.in_transfer->context = NULL;
    s_host.in_transfer->num_bytes = (int)buf_size;
    s_host.in_transfer->timeout_ms = 0; /* no timeout: wait for the player */

    s_host.streaming = true;
    err = usb_host_transfer_submit(s_host.in_transfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "transfer_submit failed: %s", esp_err_to_name(err));
        s_host.streaming = false;
        goto fail;
    }

    s_host.connected = true;
    ESP_LOGI(TAG, "streaming MIDI from EP 0x%02x (mps %u)", ep_addr, ep_mps);
    if (s_host.cfg.on_connection) {
        const usb_midi_dev_info_t info = {
            .vid = dev_desc->idVendor,
            .pid = dev_desc->idProduct,
        };
        s_host.cfg.on_connection(true, &info, s_host.cfg.arg);
    }
    return;

fail:
    close_device();
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    (void)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        s_host.dev_addr = event_msg->new_dev.address;
        s_host.actions |= ACTION_OPEN_DEV;
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        s_host.actions |= ACTION_CLOSE_DEV;
        break;
    default:
        break;
    }
}

static void client_task(void *arg)
{
    (void)arg;
    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &s_host.client));

    while (true) {
        usb_host_client_handle_events(s_host.client, pdMS_TO_TICKS(200));

        const uint32_t actions = s_host.actions;
        s_host.actions = 0;

        if (actions & ACTION_OPEN_DEV) {
            if (s_host.dev == NULL) {
                open_device();
            }
        }
        if (actions & ACTION_CLOSE_DEV) {
            close_device();
        }
        /* The stream can also die without a disconnect event (stall, error). */
        if (s_host.dev != NULL && !s_host.streaming) {
            ESP_LOGW(TAG, "stream stopped, reopening device");
            close_device();
            s_host.actions |= ACTION_OPEN_DEV;
        }
    }
}

static void daemon_task(void *arg)
{
    (void)arg;
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    ESP_LOGI(TAG, "USB host installed, waiting for a MIDI device");

    xTaskCreate(client_task, "midi_client",
                s_host.cfg.task_stack, NULL, s_host.cfg.task_priority, NULL);

    while (true) {
        uint32_t event_flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

esp_err_t usb_midi_host_start(const usb_midi_host_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_host.cfg = *config;
    if (s_host.cfg.task_priority == 0) {
        s_host.cfg.task_priority = 5;
    }
    if (s_host.cfg.task_stack == 0) {
        s_host.cfg.task_stack = 4096;
    }

    BaseType_t ok = xTaskCreate(daemon_task, "usb_daemon", DAEMON_TASK_STACK,
                                NULL, s_host.cfg.task_priority + 1, NULL);
    return (ok == pdTRUE) ? ESP_OK : ESP_ERR_NO_MEM;
}
