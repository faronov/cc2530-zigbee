/* SPDX-License-Identifier: BSD-3-Clause
 * Original synthetic peripheral inputs; all progress uses production calls.
 * The model's frame filter is not simulated; only its AUTOACK decision reads
 * the installed PAN/short registers, as in the handoff model.
 */
#define MAC_ADAPTER_MAIN adapter_component_main
#include "test_mac_adapter.c"

#if !defined(CC2530_MAC_RECONFIG)
#error test_mac_reconfig requires the reconfiguration profile
#endif

static struct { uint8_t before[4]; radio_autoack_config_t value; uint8_t after[4]; } link;
static mac_epoch_stamp_t link_opened, link_before;
static uint16_t link_address = 0x1900, opened_address = 0x1920;
static unsigned reconfig_writes, reconfig_corrupt, tx_channels, rx_opens;
static uint8_t tx_channel[16], rx_channel[16];
static uint16_t last_reconfig;
#define RECONFIG_CASES 6u

static uint8_t frequency(uint8_t channel) { return (uint8_t)(11u + 5u * (channel - 11u)); }
static int before_or_equal(const mac_epoch_stamp_t *a, const mac_epoch_stamp_t *b)
{
    return a->symbols == b->symbols ? a->fine <= b->fine :
        (uint32_t)(b->symbols - a->symbols) < MAC_TX_HALF;
}
static uint16_t reconfig_address(const volatile void *p)
{
    if (p == &link.value) return link_address;
    if (p == &link_opened) return opened_address;
    if (p == &link_before) return 0x1930;
    return adapter_address(p);
}
static void reconfig_xstore(uint16_t a, uint8_t value)
{
    if (mode == 5 && ((a >= 0x6172 && a <= 0x6175) || a == 0x618f)) {
        handoff_tick(); advance_clocks(mmio_clocks);
        assert(xwrite_count == 1 && xwrites[0].address == a && xwrites[0].value == value);
        xwrite_count = 0; logs(); trace('w', a, value);
        assert(!count && !packets && !XR(0x618b) && !(XR(0x6193) & 0x27) && !ack_active);
        assert(XR(a) != value && a > last_reconfig);
        last_reconfig = a; reconfig_writes++;
        XR(a) = value ^ (reconfig_writes == reconfig_corrupt ? 1u : 0u);
        return;
    }
    if (a == 0x618c && rx_opens < sizeof(rx_channel)) rx_channel[rx_opens++] = XR(0x618f);
    handoff_xstore(a, value);
}
static void reconfig_store(uint8_t a, uint8_t before, uint8_t value)
{
    if (a == SOC_RFST_ADDRESS && value == 0xea && tx_channels < sizeof(tx_channel))
        tx_channel[tx_channels++] = XR(0x618f);
    adapter_store(a, before, value);
}
static void install_hooks(void)
{
    host_mmio_xaddress_hook = reconfig_address;
    host_mmio_xwrite_hook = reconfig_xstore; host_mmio_write_hook = reconfig_store;
    memset(&link, 0x5a, sizeof(link)); link.value = config.value;
    memset(&link_opened, 0x69, sizeof(link_opened));
    link_address = 0x1900; opened_address = 0x1920;
    reconfig_writes = reconfig_corrupt = tx_channels = rx_opens = 0;
}
static void link_receiver(void)
{
    start_receiver();
    install_hooks();
}
static void link_adapter(void)
{
    link_receiver();
    close_receiver();
    assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_OK);
    assert(mac_tx_interval_init(&adapter_tx, 0x5a, adapter_now.symbols) == MAC_TX_OK);
}
static mac_adapter_result_t configure_link(void)
{
    last_reconfig = 0;
    return mac_adapter_configure(&link.value, bound, cap);
}
static void installed(uint16_t pan, uint16_t short_address, uint8_t channel)
{
    unsigned i;
    for (i = 0; i < 8; i++) assert(XR(0x616a + i) == config.value.ieee[i]);
    assert(XR(0x6172) == (uint8_t)pan && XR(0x6173) == pan >> 8);
    assert(XR(0x6174) == (uint8_t)short_address && XR(0x6175) == short_address >> 8);
    assert(XR(0x618f) == frequency(channel));
    assert(XR(0x6181) == 0x70 && XR(0x6182) == 0 && XR(0x618a) == 0 && XR(0x6194) == 0x7f);
}
static void open_link(void)
{
    assert(mac_adapter_now(bound, cap, &link_before) == MAC_ADAPTER_OK);
    assert(mac_adapter_open(bound, cap, &link_opened) == MAC_ADAPTER_OK);
    assert(mac_adapter_diagnostic()->phase == MAC_ADAPTER_RX && mac_adapter_diagnostic()->normal_rx &&
           XR(0x618b) == 1 && XR(0x6180) == RADIO_AUTOACK_NORMAL_FILTER && XR(0x6189) == 0x60 &&
           !memcmp(&link_opened, &mac_adapter_diagnostic()->live, sizeof(link_opened)) &&
           before_or_equal(&link_before, &link_opened));
}
static void deliver_to(uint16_t pan, uint16_t destination)
{
    unsigned first = tail;
    enqueue(13, 0xe9, 0x20);
    fifo[(first+1)&127] = 0x61; fifo[(first+2)&127] = 0x98;
    fifo[(first+4)&127] = (uint8_t)pan; fifo[(first+5)&127] = (uint8_t)(pan >> 8);
    fifo[(first+6)&127] = (uint8_t)destination; fifo[(first+7)&127] = (uint8_t)(destination >> 8);
    fifo[(first+8)&127] = 0x78; fifo[(first+9)&127] = 0x56;
    if ((XR(0x6180) & 1) && ((XR(0x6180) >> 2) & 3) >= 1 &&
        (XR(0x6189) & 0x20) && (XR(0x6181) & 0x10) &&
        fifo[(first+4)&127] == XR(0x6172) && fifo[(first+5)&127] == XR(0x6173) &&
        fifo[(first+6)&127] == XR(0x6174) && fifo[(first+7)&127] == XR(0x6175))
        start_ack();
}
static void deliver_beacon(uint8_t sequence)
{
    unsigned first = tail;
    enqueue(13, 0xe9, sequence);
    fifo[(first+1)&127] = 0x00; fifo[(first+2)&127] = 0x80;
}
static const mac_adapter_observation_t *next_frame(void)
{
    mac_adapter_result_t result;
    do { result = adapter_tick(); } while (result == MAC_ADAPTER_WAIT);
    assert(result == MAC_ADAPTER_EVENT && mac_adapter_observation()->kind == MAC_ADAPTER_RX_EVENT);
    rx_seen++;
    return mac_adapter_observation();
}
static void unchanged_state_error(void)
{
    unsigned before = accesses;
    mac_adapter_diagnostics_t saved = *mac_adapter_diagnostic();
    uint8_t saved_regs[sizeof(xregs)];
    memcpy(saved_regs, xregs, sizeof(saved_regs));
    assert(configure_link() == MAC_ADAPTER_STATE);
    assert(mac_adapter_open(bound, cap, &link_opened) == MAC_ADAPTER_STATE);
    assert(accesses == before && !memcmp(&saved, mac_adapter_diagnostic(), sizeof(saved)) &&
           !memcmp(saved_regs, xregs, sizeof(saved_regs)));
}
static void invalid_reconfig(void)
{
    unsigned before, i;
    uint16_t addresses[6];
    mac_adapter_diagnostics_t saved;
    mac_radio_diagnostics_t saved_radio;
    radio_autoack_diagnostics_t saved_driver;
    uint8_t saved_regs[sizeof(xregs)];
    adapter_reset(); install_hooks();
    unchanged_state_error();
    link_receiver();
    unchanged_state_error();
    assert(mac_adapter_close(NULL) == MAC_ADAPTER_OK);
    unchanged_state_error();
    while (adapter_tick() != MAC_ADAPTER_EVENT) {}
    assert(mac_adapter_observation()->kind == MAC_ADAPTER_CLOSED_EVENT &&
           mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF);
    unchanged_state_error();
    consume_observation();
    before = accesses; saved = *mac_adapter_diagnostic(); memcpy(saved_regs, xregs, sizeof(saved_regs));
    saved_radio = *mac_attempt_diagnostic(); saved_driver = *radio_autoack_diagnostic();
    assert(mac_adapter_configure(NULL, bound, cap) == MAC_ADAPTER_INVALID);
    assert(mac_adapter_configure(&link.value, 0, cap) == MAC_ADAPTER_INVALID);
    assert(mac_adapter_configure(&link.value, TIMEBASE_HALF_RANGE, cap) == MAC_ADAPTER_INVALID);
    assert(mac_adapter_configure(&link.value, bound, 0) == MAC_ADAPTER_INVALID);
    link.value.channel = 10; assert(configure_link() == MAC_ADAPTER_INVALID);
    link.value.channel = 27; assert(configure_link() == MAC_ADAPTER_INVALID);
    link.value.channel = 15; link.value.power = 0; assert(configure_link() == MAC_ADAPTER_INVALID);
    link.value.power = RADIO_AUTOACK_POWER_05;
    for (i = 0; i < 8; i++) {
        link.value.ieee[i] ^= 0x80; assert(configure_link() == MAC_ADAPTER_INVALID);
        link.value.ieee[i] ^= 0x80;
    }
    addresses[0] = 1; addresses[1] = adapter_end; addresses[2] = helper-24;
    addresses[3] = helper-11; addresses[4] = helper; addresses[5] = helper+4;
    for (i = 0; i < 6; i++) {
        link_address = addresses[i]; assert(configure_link() == MAC_ADAPTER_STORAGE);
        opened_address = i == 2 ? helper-16 : addresses[i];
        assert(mac_adapter_open(bound, cap, &link_opened) == MAC_ADAPTER_STORAGE);
        link_address = 0x1900; opened_address = 0x1920;
    }
    link_address = 0x1df3; assert(configure_link() == MAC_ADAPTER_RANGE);
    link_address = 0x1f00; assert(configure_link() == MAC_ADAPTER_RANGE);
    link_address = 0x1900; opened_address = 0x1dfb;
    assert(mac_adapter_open(bound, cap, &link_opened) == MAC_ADAPTER_RANGE);
    opened_address = 0x1920;
    assert(mac_adapter_open(bound, cap, NULL) == MAC_ADAPTER_INVALID);
    assert(mac_adapter_open(0, cap, &link_opened) == MAC_ADAPTER_INVALID);
    assert(mac_adapter_open(bound, 0, &link_opened) == MAC_ADAPTER_INVALID);
    assert(accesses == before && !memcmp(&saved, mac_adapter_diagnostic(), sizeof(saved)) &&
           !memcmp(&saved_radio, mac_attempt_diagnostic(), sizeof(saved_radio)) &&
           !memcmp(&saved_driver, radio_autoack_diagnostic(), sizeof(saved_driver)) &&
           !memcmp(saved_regs, xregs, sizeof(saved_regs)) && !reconfig_writes);
    assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_OK);
    assert(mac_tx_interval_init(&adapter_tx, 0x5a, adapter_now.symbols) == MAC_TX_OK);
    submit_packet(1);
    assert(mac_adapter_prepare(&adapter_tx, MAC_ADAPTER_KEEP_AUTOACK, bound, cap) == MAC_ADAPTER_OK);
    unchanged_state_error();
    assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_OK);
    assert(mac_tx_observed_step(&adapter_tx, adapter_now.symbols, NULL, &adapter_action) == MAC_TX_OK);
    cancel_input();
    assert(mac_adapter_unprepare(&adapter_tx, bound, cap) == MAC_ADAPTER_OK && !tx_started);
    link.value.channel = 12;
    assert(configure_link() == MAC_ADAPTER_OK && reconfig_writes == 1);
    installed(0x1234, 0x9abc, 12);
    open_link();
    assert(rx_channel[rx_opens-1] == frequency(12));
    close_receiver();
}
static void reconfig_case(unsigned selected)
{
    const mac_adapter_observation_t *view;
    unsigned i, before;
    static const uint8_t channels[] = { 11, 15, 20, 25 };
    if (selected == 0) {
        link_adapter();
        open_link();
        assert(mac_adapter_open(bound, cap, &link_opened) == MAC_ADAPTER_STATE);
        assert(rx_channel[rx_opens-1] == frequency(26) && !reconfig_writes);
        deliver_to(0x1234, 0x9abc);
        assert(ack_active);
        view = next_frame();
        assert(view->normal_rx && view->rx_serial == mac_adapter_diagnostic()->frames &&
               view->frame->body[5] == 0xbc && view->frame->body[6] == 0x9a);
        consume_observation(); close_receiver();
        assert(automatic_acks == 1 && !ack_active);
    } else if (selected == 1) {
        link_adapter();
        link.value.pan = 0x4321; link.value.short_address = 0x2468; link.value.channel = 15;
        assert(configure_link() == MAC_ADAPTER_OK && reconfig_writes == 5);
        assert(radio_autoack_diagnostic()->phase == 25 && radio_autoack_diagnostic()->writes == 5 &&
               radio_autoack_diagnostic()->verified == 5 && mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF &&
               !XR(0x618b));
        installed(0x4321, 0x2468, 15);
        assert(configure_link() == MAC_ADAPTER_OK && reconfig_writes == 5 &&
               radio_autoack_diagnostic()->writes == 0);
        link.value.short_address = 0x2469; link.value.channel = 20;
        assert(configure_link() == MAC_ADAPTER_OK && reconfig_writes == 7);
        installed(0x4321, 0x2469, 20);
        open_link();
        assert(rx_channel[rx_opens-1] == frequency(20));
        deliver_to(0x1234, 0x9abc);
        assert(!ack_active);
        view = next_frame();
        assert(view->normal_rx);
        consume_observation();
        deliver_to(0x4321, 0x2469);
        assert(ack_active);
        view = next_frame();
        assert(view->frame->body[3] == 0x21 && view->frame->body[4] == 0x43 &&
               view->frame->body[5] == 0x69 && view->frame->body[6] == 0x24);
        consume_observation(); close_receiver();
        assert(automatic_acks == 1);
        submit_packet(1); assert(drive_tx(MAC_ADAPTER_KEEP_AUTOACK) == MAC_ADAPTER_OK);
        assert(adapter_tx.engine.outcome == MAC_TX_ACKED && tx_channels == 1 &&
               tx_channel[0] == frequency(20) && mac_adapter_diagnostic()->phase == MAC_ADAPTER_RX &&
               XR(0x6180) == RADIO_AUTOACK_NORMAL_FILTER && XR(0x6189) == 0x60);
        assert(mac_tx_interval_release(&adapter_tx) == MAC_TX_OK);
        deliver_to(0x4321, 0x2469);
        assert(ack_active);
        (void)next_frame(); consume_observation(); close_receiver();
        assert(automatic_acks == 2);
    } else if (selected == 2) {
        link_adapter();
        for (i = 0; i < sizeof(channels); i++) {
            link.value.channel = channels[i];
            assert(configure_link() == MAC_ADAPTER_OK);
            installed(0x1234, 0x9abc, channels[i]);
            submit_packet(0); assert(drive_tx(MAC_ADAPTER_KEEP_RAW) == MAC_ADAPTER_OK);
            assert(adapter_tx.engine.outcome == MAC_TX_UNACKNOWLEDGED && tx_channels == i+1u &&
                   tx_channel[i] == frequency(channels[i]) &&
                   mac_adapter_diagnostic()->phase == MAC_ADAPTER_RX && !mac_adapter_diagnostic()->normal_rx &&
                   XR(0x6180) == 0x0c && XR(0x6189) == 0x40);
            assert(mac_tx_interval_release(&adapter_tx) == MAC_TX_OK);
            deliver_beacon((uint8_t)(0x40 + i));
            assert(!ack_active);
            view = next_frame();
            assert(!view->normal_rx && view->frame->body[0] == 0 && view->frame->body[1] == 0x80 &&
                   view->frame->body[2] == 0x40 + i);
            consume_observation();
            close_receiver();
            assert(mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF);
        }
        assert(reconfig_writes == 4 && automatic_acks == 0);
        link.value.channel = 26;
        assert(configure_link() == MAC_ADAPTER_OK && reconfig_writes == 5);
        installed(0x1234, 0x9abc, 26);
        open_link();
        assert(rx_channel[rx_opens-1] == frequency(26));
        deliver_to(0x1234, 0x9abc); assert(ack_active);
        (void)next_frame(); consume_observation(); close_receiver();
        assert(automatic_acks == 1);
    } else if (selected == 3) {
        link_adapter();
        reconfig_corrupt = 2; link.value.pan = 0x4321; link.value.channel = 11;
        assert(configure_link() == MAC_ADAPTER_RADIO_ERROR);
        assert(mac_adapter_diagnostic()->phase == MAC_ADAPTER_FAULT &&
               mac_adapter_diagnostic()->fault == MAC_ADAPTER_RADIO_ERROR &&
               radio_autoack_diagnostic()->phase == 24 && radio_autoack_diagnostic()->writes == 2 &&
               radio_autoack_diagnostic()->verified == 1 &&
               radio_autoack_diagnostic()->result == RADIO_AUTOACK_STATE_CHANGED &&
               radio_autoack_state == RADIO_AUTOACK_FAULT && reconfig_writes == 2);
        before = accesses;
        assert(configure_link() == MAC_ADAPTER_RADIO_ERROR);
        assert(mac_adapter_open(bound, cap, &link_opened) == MAC_ADAPTER_RADIO_ERROR);
        assert(accesses == before);
        retained_fault();
    } else if (selected == 4) {
        link_adapter();
        mmio_clocks = 16;
        link.value.pan = 0xfffe; link.value.short_address = 0xffff; link.value.channel = 11;
        assert(configure_link() == MAC_ADAPTER_OK && reconfig_writes == 5);
        installed(0xfffe, 0xffff, 11);
        link.value.pan = 0; link.value.short_address = 0; link.value.channel = 26;
        assert(configure_link() == MAC_ADAPTER_OK && reconfig_writes == 10);
        installed(0, 0, 26);
        open_link(); close_receiver();
        link.value.pan = 0x1234; link.value.short_address = 0x9abc;
        assert(configure_link() == MAC_ADAPTER_OK && reconfig_writes == 14);
        installed(0x1234, 0x9abc, 26);
        open_link();
        deliver_to(0x1234, 0x9abc); assert(ack_active);
        (void)next_frame(); consume_observation(); close_receiver();
    } else {
        assert(selected == 5); invalid_reconfig();
    }
}
int main(int argc, char **argv)
{
    unsigned selected;
    (void)argv;
    assert(argc == 1);
    for (selected = 0; selected < RECONFIG_CASES; selected++) reconfig_case(selected);
    assert(adapter_component_main(1, argv) == 0);
    puts("MAC reconfiguration: OFF-only PAN/short/channel install, reopen, raw sweep, guards and faults PASS.");
    return 0;
}
