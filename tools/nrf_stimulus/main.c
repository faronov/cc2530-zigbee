/* SPDX-License-Identifier: BSD-3-Clause */
#include "control.h"
#include "transport.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <hal/nrf_radio.h>
#include <nrf_802154.h>
#include <nrf_802154_callouts.h>

extern bool ns51_observer_profile_valid(void);
extern bool ns51_observer_asleep(void);

static const struct device *const port = DEVICE_DT_GET(DT_NODELABEL(uart0));
static struct stim_control control;
static uint8_t tx_frame[sizeof(stim_tx_frame)];
static struct stim_uart serial;
static bool uart_ready;
static uint8_t *retained_buffers[4];
static unsigned retained_count;

/* PRIMASK, not Zephyr BASEPRI: also excludes configurable zero-latency IRQs. */
static uint32_t enter(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return key;
}

static void leave(uint32_t key)
{
    __DMB();
    __set_PRIMASK(key);
}

static bool radio_stopped(void)
{
    return ns51_observer_asleep() &&
           nrf_radio_state_get(NRF_RADIO) == NRF_RADIO_STATE_DISABLED &&
           NVIC_GetPendingIRQ(RADIO_IRQn) == 0u;
}

static void release_rx(uint8_t *p)
{
    if (!nrf_802154_buffer_free_immediately_raw(p)) {
        uint32_t key = enter();
        if (retained_count < ARRAY_SIZE(retained_buffers)) {
            retained_buffers[retained_count++] = p;
        }
        stim_fault(&control, k_uptime_get_32(), STIM_BUFFER);
        leave(key);
    }
}

void ns51_phy_tx_done(uint8_t *frame)
{
    uint32_t key = enter();
    uint32_t now = k_uptime_get_32();
    if (frame != tx_frame || memcmp(frame, stim_tx_frame, sizeof(tx_frame)) != 0) {
        stim_fault(&control, now, STIM_CALLBACK);
    } else {
        stim_phy_done(&control, now);
    }
    leave(key);
}

void nrf_802154_received_raw(uint8_t *p, int8_t power, uint8_t lqi)
{
    uint32_t key = enter();
    uint32_t now = k_uptime_get_32();
    if (p == NULL) {
        stim_fault(&control, now, STIM_BUFFER);
    } else if (p[0] < 5u || p[0] > 127u) {
        stim_fault(&control, now, STIM_RX_LENGTH);
    } else {
        stim_rx(&control, now, p + 1, (size_t)p[0] - 2u, power, lqi);
    }
    leave(key);
    if (p != NULL) {
        release_rx(p);
    }
}

void nrf_802154_receive_failed(nrf_802154_rx_error_t error, uint32_t id)
{
    uint32_t key = enter();
    (void)id;
    stim_rx_failed(&control, k_uptime_get_32(), (uint8_t)error);
    leave(key);
}

void nrf_802154_tx_ack_started(const uint8_t *frame)
{
    uint32_t key = enter();
    (void)frame;
    stim_fault(&control, k_uptime_get_32(), STIM_CALLBACK);
    leave(key);
}

void nrf_802154_transmit_failed(uint8_t *frame, nrf_802154_tx_error_t error,
                               const nrf_802154_transmit_done_metadata_t *metadata)
{
    uint32_t key = enter();
    (void)metadata;
    if (frame != tx_frame) {
        stim_fault(&control, k_uptime_get_32(), STIM_CALLBACK);
    } else {
        stim_tx_failed(&control, k_uptime_get_32(), (uint8_t)error);
    }
    leave(key);
}

void nrf_802154_transmitted_raw(uint8_t *frame,
                               const nrf_802154_transmit_done_metadata_t *metadata)
{
    uint32_t key = enter();
    (void)frame;
    stim_fault(&control, k_uptime_get_32(), STIM_CALLBACK);
    leave(key);
    if (metadata != NULL && metadata->data.transmitted.p_ack != NULL) {
        release_rx(metadata->data.transmitted.p_ack);
    }
}

static void uart_event(const struct device *dev, void *user)
{
    unsigned work;
    uint32_t key = enter();
    (void)user;
    if (!uart_irq_update(dev) || uart_err_check(dev) != 0) {
        serial.rx_fault = true;
        uart_irq_rx_disable(dev);
        uart_irq_err_disable(dev);
    }
    for (work = 0; work < STIM_UART_SLICE && !serial.rx_fault &&
                   uart_irq_rx_ready(dev); ++work) {
        uint8_t byte;
        int n = uart_fifo_read(dev, &byte, 1);
        if (n != 1 || !stim_uart_put(&serial, byte)) {
            serial.rx_fault = true;
            uart_irq_rx_disable(dev);
            uart_irq_err_disable(dev);
            break;
        }
    }
    if (uart_irq_tx_ready(dev)) {
        if (serial.busy && serial.position < serial.length) {
            size_t requested = MIN(serial.length - serial.position, STIM_UART_SLICE);
            int n = uart_fifo_fill(dev, (const uint8_t *)serial.line + serial.position,
                                  (int)requested);
            if (!stim_uart_advance(&serial, requested, n)) {
                uart_irq_tx_disable(dev);
            }
        } else {
            (void)stim_uart_complete(&serial);
            uart_irq_tx_disable(dev);
        }
    }
    leave(key);
}

static void serial_service(void)
{
    unsigned work;
    bool can_publish;
    uint32_t key = enter();
    uint32_t now = k_uptime_get_32();
    stim_uart_tick(&serial, now);
    if (serial.rx_fault || serial.tx_fault) {
        stim_fault(&control, now, STIM_UART);
        uart_irq_rx_disable(port);
        uart_irq_err_disable(port);
    }
    if (serial.tx_fault) {
        uart_irq_tx_disable(port);
    }
    for (work = 0; work < STIM_UART_SLICE; ++work) {
        uint8_t byte;
        if (!stim_uart_take(&serial, &byte)) {
            break;
        }
        stim_byte(&control, now, byte);
        if (control.error != STIM_OK || control.input_work == STIM_INPUT_MAX) {
            uart_irq_rx_disable(port);
            uart_irq_err_disable(port);
            serial.count = 0;
            break;
        }
    }
    can_publish = !serial.busy && !serial.tx_fault;
    leave(key);

    /* Encode outside the PRIMASK section; IRQ never reads an unpublished line. */
    if (can_publish) {
        struct stim_record r;
        bool available;
        key = enter();
        available = stim_pop(&control, &r);
        leave(key);
        if (available) {
            size_t length = stim_encode(r.type, r.payload, r.length,
                                        serial.line, sizeof(serial.line));
            key = enter();
            if (!stim_uart_begin(&serial, length, k_uptime_get_32())) {
                stim_fault(&control, k_uptime_get_32(), STIM_UART);
            } else {
                uart_irq_tx_enable(port);
            }
            leave(key);
        }
    }
}

int main(void)
{
    static const nrf_802154_transmit_metadata_t metadata = {
        .frame_props = {.is_secured = false, .dynamic_data_is_set = false},
        .cca = false,
        .tx_power = {.use_metadata_value = true, .power = -20},
        .tx_channel = {.use_metadata_value = true, .channel = 26}
    };
    bool initialize;
    uint32_t key;

    memcpy(tx_frame, stim_tx_frame, sizeof(tx_frame));
    key = enter();
    initialize = stim_startup_begin(&control, k_uptime_get_32(),
        nrf_radio_state_get(NRF_RADIO) == NRF_RADIO_STATE_DISABLED &&
        NVIC_GetPendingIRQ(RADIO_IRQn) == 0u);
    leave(key);
    if (initialize) {
        nrf_802154_init();
        nrf_802154_auto_ack_set(false);
        nrf_802154_promiscuous_set(true);
        nrf_802154_rx_on_when_idle_set(true);
        nrf_802154_channel_set(26);
        nrf_802154_tx_power_set(-20);
        key = enter();
        stim_startup_complete(&control, k_uptime_get_32(),
            !nrf_802154_auto_ack_get() && nrf_802154_promiscuous_get() &&
            ns51_observer_profile_valid() &&
            nrf_802154_channel_get() == 26 && nrf_802154_tx_power_get() == -20,
            radio_stopped());
        leave(key);
    }
    if (!device_is_ready(port) ||
        uart_irq_callback_user_data_set(port, uart_event, NULL) != 0) {
        key = enter();
        stim_fault(&control, k_uptime_get_32(), STIM_UART);
        serial.rx_fault = true;
        serial.tx_fault = true;
        leave(key);
    } else {
        uart_ready = true;
        uart_irq_err_enable(port);
        uart_irq_rx_enable(port);
    }
    for (;;) {
        enum stim_action action;
        bool observe;
        if (uart_ready) {
            serial_service();
        }
        key = enter();
        action = stim_action(&control, k_uptime_get_32());
        observe = control.stop_accepted && !control.terminal_made;
        leave(key);
        if (action == STIM_SUBMIT) {
            bool accepted = nrf_802154_transmit_raw(tx_frame, &metadata);
            key = enter();
            stim_schedule(&control, k_uptime_get_32(), accepted);
            leave(key);
        } else if (action == STIM_SLEEP) {
            bool accepted = nrf_802154_sleep();
            key = enter();
            stim_stop_result(&control, k_uptime_get_32(), accepted);
            leave(key);
        }
        if (observe) {
            key = enter();
            stim_stop_observed(&control, k_uptime_get_32(), radio_stopped());
            leave(key);
        }
        k_sleep(K_MSEC(1));
    }
}
