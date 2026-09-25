/* SPDX-License-Identifier: BSD-3-Clause
 * Native public-call transcript. Never installs controller state in the MCU.
 */
static unsigned trace_enabled, trace_started;
static uint8_t trace_policy, trace_selector, trace_random_byte, trace_length, trace_dsn;
static uint16_t trace_limit, trace_work;
static uint32_t trace_timeout, trace_lifetime, trace_token;
static uint8_t trace_packet[125];
static mac_tx_interval_event_t trace_random;
static mac_epoch_stamp_t trace_through;
static uint16_t trace_config_ptr, trace_tx_ptr, trace_action_ptr, trace_clock_ptr, trace_through_ptr;

static void emit_number(unsigned char *out, uint32_t value, unsigned size)
{
    unsigned i;
    for (i = 0; i < size; i++) out[i] = (uint8_t)(value >> (8*i));
}
static void emit_pointer(unsigned char *out, const void *value, unsigned size);
#include "mac_adapter_layout.h"
static void emit_pointer(unsigned char *out, const void *value, unsigned size)
{
    uint32_t address;
    if (!value) address = 0;
    else if (value == &mac_adapter_receipt.frame) address = TARGET_adapter_frame;
    else if (value == mac_adapter_receipt.frame.body) address = TARGET_adapter_body;
    else { fprintf(stderr, "unowned native observation pointer\n"); abort(); }
    emit_number(out, address, size);
}

static void trace_begin(unsigned operation)
{
    unsigned i;
    if (!trace_enabled) return;
    printf("%s{\"operation\":%u,\"timeout\":%lu,\"limit\":%u,\"policy\":%u,\"selector\":%u,"
           "\"random_byte\":%u,\"length\":%u,\"dsn\":%u,\"work\":%u,\"lifetime\":%lu,\"token\":%lu,"
           "\"packet\":\"", trace_started++ ? "," : "", operation, (unsigned long)trace_timeout,
           trace_limit, trace_policy, trace_selector, trace_random_byte, trace_length, trace_dsn,
           trace_work, (unsigned long)trace_lifetime, (unsigned long)trace_token);
    hex(trace_packet, sizeof(trace_packet)); printf("\",\"configuration\":\""); emit_adapter_config(&config.value);
    printf("\",\"through\":\""); emit_adapter_through(&trace_through);
    printf("\",\"config_ptr\":%u,\"tx_ptr\":%u,\"action_ptr\":%u,\"clock_ptr\":%u,\"through_ptr\":%u,\"initial\":{",
           trace_config_ptr, trace_tx_ptr, trace_action_ptr, trace_clock_ptr, trace_through_ptr);
#define REG(name, address) printf("\"%u\":%u,", address, name);
    CC2530_REGISTER_LIST(REG)
#undef REG
    printf("\"24705\":%u,\"24707\":%u,", tx_fifo[1], tx_fifo[3]);
    for (i = 0; i < sizeof(xregs); i++)
        printf("\"%u\":%u%s", 0x6100u+i, xregs[i], i+1 == sizeof(xregs) ? "" : ",");
    printf("},\"events\":[");
    printing = 1; trace_count = 0;
}
static unsigned trace_end(unsigned result)
{
    if (!trace_enabled) return result;
    printing = 0;
    printf("],\"return\":%u,\"tx\":\"", result); emit_adapter_tx(&adapter_tx);
    printf("\",\"action\":\""); emit_adapter_action(&adapter_action);
    printf("\",\"random\":\""); emit_adapter_random(&trace_random);
    printf("\",\"clock\":\""); emit_adapter_clock(&adapter_now);
    printf("\",\"observation\":\""); emit_adapter_observation(mac_adapter_observation());
    printf("\",\"diagnostics\":\""); emit_adapter_diagnostics(mac_adapter_diagnostic());
    printf("\",\"record\":\""); emit_adapter_record(mac_adapter_record());
    printf("\"}");
    return result;
}
static mac_adapter_result_t traced_init(const radio_autoack_config_t *c, uint32_t timeout, uint16_t limit)
{
    assert(!c || c == &config.value);
    trace_config_ptr = c ? MMIO_XADDRESS(c) : 0;
    trace_timeout = timeout; trace_limit = limit; trace_begin(0);
    return (mac_adapter_result_t)trace_end(mac_adapter_init(c, timeout, limit));
}
static mac_adapter_result_t traced_now(uint32_t timeout, uint16_t limit, mac_epoch_stamp_t *out)
{
    assert(!out || out == &adapter_now);
    trace_clock_ptr = out ? MMIO_XADDRESS(out) : 0;
    trace_timeout = timeout; trace_limit = limit; trace_begin(1);
    return (mac_adapter_result_t)trace_end(mac_adapter_now(timeout, limit, out));
}
static mac_tx_result_t traced_tx_init(mac_tx_interval_t *tx, uint8_t dsn, uint32_t now)
{
    assert(tx == &adapter_tx && now == adapter_now.symbols);
    trace_dsn = dsn; trace_begin(2);
    return (mac_tx_result_t)trace_end(mac_tx_interval_init(tx, dsn, now));
}
static mac_tx_result_t traced_submit(mac_tx_interval_t *tx, const uint8_t *body,
    uint16_t length, uint32_t now, uint32_t lifetime, uint16_t work)
{
    assert(tx == &adapter_tx && length <= 125 && now == mac_adapter_diagnostic()->live.symbols);
    memset(trace_packet, 0, sizeof(trace_packet)); memcpy(trace_packet, body, length);
    trace_length = (uint8_t)length; trace_lifetime = lifetime; trace_work = work; trace_begin(3);
    return (mac_tx_result_t)trace_end(mac_tx_interval_submit(tx, body, length, now, lifetime, work));
}
static mac_adapter_result_t traced_prepare(const mac_tx_interval_t *tx, uint8_t policy,
                                           uint32_t timeout, uint16_t limit)
{
    assert(!tx || tx == &adapter_tx);
    trace_tx_ptr = tx ? MMIO_XADDRESS(tx) : 0;
    trace_policy = policy; trace_timeout = timeout; trace_limit = limit; trace_begin(4);
    return (mac_adapter_result_t)trace_end(mac_adapter_prepare(tx, policy, timeout, limit));
}
static mac_tx_result_t traced_step(mac_tx_interval_t *tx, uint32_t now,
    const mac_tx_interval_event_t *event, mac_tx_interval_action_t *action)
{
    assert(tx == &adapter_tx && action == &adapter_action && now == mac_adapter_diagnostic()->live.symbols);
    trace_selector = !event ? 0 : event == &adapter_input ?
        (event->source.kind == MAC_TX_EVENT_CANCEL ? 3 : 1) : 2;
    assert(trace_selector != 2 || (event == &mac_adapter_observation()->tx &&
                                  mac_adapter_diagnostic()->ready && event->source.kind));
    if (trace_selector == 1 || trace_selector == 3) {
        assert(event->source.kind == MAC_TX_EVENT_RANDOM || event->source.kind == MAC_TX_EVENT_CANCEL);
        trace_random_byte = event->source.value;
    }
    trace_begin(5);
    memset(&trace_random, 0, sizeof(trace_random));
    trace_random.source.kind = trace_selector == 3 ? MAC_TX_EVENT_CANCEL : MAC_TX_EVENT_RANDOM;
    trace_random.source.value = trace_random_byte;
    trace_random.source.generation = tx->engine.generation;
    trace_random.source.retry = tx->engine.retries; trace_random.source.nb = tx->engine.nb;
    trace_random.source.stamp = now;
    return (mac_tx_result_t)trace_end(mac_tx_observed_step(tx, now, event, action));
}
static mac_adapter_result_t traced_accept(const mac_tx_interval_t *tx, const mac_tx_interval_action_t *action)
{
    assert((!tx || tx == &adapter_tx) && (!action || action == &adapter_action));
    trace_tx_ptr = tx ? MMIO_XADDRESS(tx) : 0;
    trace_action_ptr = action ? MMIO_XADDRESS(action) : 0;
    trace_begin(6);
    return (mac_adapter_result_t)trace_end(mac_adapter_accept(tx, action));
}
static mac_adapter_result_t traced_progress(uint32_t timeout, uint16_t limit)
{
    trace_timeout = timeout; trace_limit = limit; trace_begin(7);
    return (mac_adapter_result_t)trace_end(mac_adapter_step(timeout, limit));
}
static mac_adapter_result_t traced_consume(uint32_t token)
{
    trace_token = token; trace_begin(8);
    return (mac_adapter_result_t)trace_end(mac_adapter_consume(token));
}
static mac_adapter_result_t traced_close(const mac_epoch_stamp_t *end)
{
    trace_selector = end != NULL;
    trace_through_ptr = end ? MMIO_XADDRESS(end) : 0;
    if (end) trace_through = *end;
    trace_begin(9);
    return (mac_adapter_result_t)trace_end(mac_adapter_close(end));
}
static mac_adapter_result_t traced_unprepare(const mac_tx_interval_t *tx, uint32_t timeout, uint16_t limit)
{
    assert(!tx || tx == &adapter_tx);
    trace_tx_ptr = tx ? MMIO_XADDRESS(tx) : 0;
    trace_timeout = timeout; trace_limit = limit; trace_begin(10);
    return (mac_adapter_result_t)trace_end(mac_adapter_unprepare(tx, timeout, limit));
}
static mac_tx_result_t traced_release(mac_tx_interval_t *tx)
{
    assert(tx == &adapter_tx); trace_begin(11);
    return (mac_tx_result_t)trace_end(mac_tx_interval_release(tx));
}
#define mac_adapter_init traced_init
#define mac_adapter_now traced_now
#define mac_tx_interval_init traced_tx_init
#define mac_tx_interval_submit traced_submit
#define mac_adapter_prepare traced_prepare
#define mac_tx_observed_step traced_step
#define mac_adapter_accept traced_accept
#define mac_adapter_step traced_progress
#define mac_adapter_consume traced_consume
#define mac_adapter_close traced_close
#define mac_adapter_unprepare traced_unprepare
#define mac_tx_interval_release traced_release
