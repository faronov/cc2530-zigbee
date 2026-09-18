# Bounded radio queues and ownership

`include/radio_queue.h` and `src/radio_queue.c` implement the isolated #11
composition: fixed pools, a reentrant request producer and the real
[passive RX service](RADIO_RX.md). This is **host-tested, image-checked and
simulated**, not a radio ISR, TX/ACK implementation, continuous receiver or
working MAC/network. No current board image links this module.

## Fixed storage and transitions

| Storage | Capacity | Ownership and pressure policy |
| --- | ---: | --- |
| RX frames | 2 x 128 bytes | FREE -> foreground filling -> QUEUED -> caller copy -> FREE |
| TX candidate | 1 x 126 bytes | caller copy -> QUEUED -> consumer copy/cancel -> FREE |
| RX request cookies | 4 x 1 byte | ISR/foreground producer -> ring -> one foreground service attempt/cancel |

No pointer or lease into a packet pool escapes. Copy-out completes before
the internal slot is released; the caller owns its independent copy.
Metadata and the declared body length are copied, not stale bytes beyond
that length. Caller output tails remain unchanged. All caller objects must
be complete, persistent, ordinary XDATA after the combined private/compiler
prefix, below `1E00` and disjoint from the generic-store helper scratch.
Foreground calls and caller-buffer access are serialized; neither DMA nor
an ISR may modify their storage during a call.

`radio_queue_tx_submit(body, length)` accepts 1..125 opaque bytes and copies
them into the one TX slot. `radio_queue_tx_read(output)` copies out and
releases it; `radio_queue_tx_cancel()` explicitly discards it. These are
**memory ownership operations**, not transmission, confirmation or delivery.
A future transmitter owns its dequeued copy and its own failure policy.
The queue never retries, transmits, validates a MAC header or fabricates ACKs.

Full pools reject new work without overwriting existing frames. A disconnected
consumer leaves a bounded occupied slot, not growing memory or a busy loop.
The RX pool's two-entry FIFO preserves successful receive order.
`radio_queue_rx_read(output)` returns EMPTY without publication if no frame
exists. Reset loses queued work; there is no stale internal lease to reuse.
Application leave/rejoin/transaction cancellation remains separate policy.

## ISR-private requests and foreground scheduling

`radio_queue_request_rx(cookie)` is the **only ISR-callable API**. The cookie
is an opaque byte identifying a request, not proof that a frame arrived or
that any radio interrupt was acknowledged. Four requests are retained FIFO;
further requests return FULL and increment a dropped counter saturating at 255.
The 8-bit producer/consumer cursors wrap modulo 256 while their distance stays
bounded by 4. Corrupt distance is rejected and latched, not normalized.

Publication uses the existing reentrant EA save-disable/exact-restore leaves.
The producer's actual SDCC locals are registers only, with normal caller
stack saves; it has **no static compiler XDATA/DATA/BIT scratch**. Its only
calls are those two IRQ leaves. It copies no packet, reads no Sleep Timer/RFD,
and calls no radio, generic-pointer or foreground helper. Its owned volatile
storage is limited to the cookie ring, producer cursor, dropped counter and
fault indication; the foreground owns the consumer cursor and packet pools.
An ABI-correct ISR must preserve interrupted context and handle its own source.
This module enables no source and provides no CC2530 peripheral dispatcher.

`radio_queue_service(channel, timeout, limit)` performs at most **one**
request/one bounded passive receive, not a drain-until-empty loop. It validates
channel 11..26, positive raw timeout below the 24-bit half range and a positive
poll bound before consumption. Empty requests or a full RX pool return
without MMIO; a full pool leaves the event pending.

The existing receiver still requires all IEN0/1/2 bytes zero. The adapter
checks that before dequeue: IRQ_ACTIVE leaves the event and driver unmodified.
It does not disable unowned interrupt sources or weaken the receiver's
reset/clock/CSP/DMA/history preconditions. The producer can run from an ISR
outside this foreground RX operation; this is **not interrupt-driven RF
reception**. The foreground owner must establish the receiver's required
quiescent interrupt context before scheduling it.

The real driver performs configuration, calibration/readiness polling,
reception, verified stop and flush. OK queues its FCS-free body and raw
metadata. BAD_CRC consumes one request but publishes no frame, frees the
filling slot and permits a later request; there is no implicit retry.
Other driver errors retain the original driver result and latch RADIO_FAILED.
Subsequent service, notification and new TX submissions return the fault
without MMIO. Previously queued copies may still be read or explicitly
cancelled, without RF access. No fault-clear/recovery API is supplied.

`radio_queue_cancel_requests()` atomically discards pending hints and
saturates its cancellation count at 255. It cannot cancel an in-progress,
non-reentrant receive or act as a radio reset. `radio_queue_snapshot(output)`
captures its nine-byte status during a short EA critical section, restores
EA, then publishes from foreground-private staging. An ISR admitted by
restore cannot overwrite that snapshot. The fields are fault, last driver
result (`FF` if never called), last consumed cookie, pending/dropped/cancelled
requests, RX count, TX count and BAD_CRC count. Counts describe software work,
not calibrated statistics, RF delivery, authentication or network membership.

## Offline proof and allocation

The composition links timebase, actual RX, IRQ leaves, queues and a synthetic
caller/ISR. It is a new, separately budgeted integrated image, not an increase
to any existing component's bounds:

| Item | Checked result |
| --- | --- |
| CODE | 7,189 bytes, within an 8,192-byte budget |
| Ordinary XDATA | 818 bytes |
| M0 status reservation | 64 bytes; 882 combined, within 1,024 |
| Backend private XDATA | `0000..00CE` |
| Queue/pool/compiler prefix | `00CF..029C`; caller starts `029D` |
| Generic-store helper scratch | `0331`, explicitly excluded from caller ranges |
| Stack | starts `35`, final checkpoint SP `36`, observed peak `50` |
| CODE SHA-256 | `10b3de7b32381022dbe99c0e9713ed01805b70ad81539d0fb31e4222d2c820b3` |

The ledger includes both RX frames, the TX candidate, real receiver staging
and diagnostics, compiler parameters and the 128-byte synthetic caller union.
XDATA `1F00..1FFF` remains the IRAM alias, not extra storage. No heap, IRQ
private pool or hidden frame-sized buffer is omitted. The protocol-codec
resource image is still separate; these totals do **not** establish complete
stack fit, arbitrary IRQ nesting or interrupt headroom in that other image.

The native corpus counts 69,108 memory/IRQ calls plus a five-receive composition
using the existing receiver's stateful peripheral model. The linked corpus
executes 24 composed operations/11,031 exact MMIO events and 676 memory-only
operations: cursor wrap, exact full limits, saturation, copy-in/out, preserved
tails, cancellation, blocked scheduling, BAD_CRC reuse and retained faults.
The receiver is not replaced by a success callback. The original model emits
successive traces without resetting the radio between receives.

Another 181 cases use the simulator's **genuine generic C52 external interrupt**
and compiled ISR/RETI. They interrupt every executed producer/IRQ-leaf boundary
at empty, one-free and full capacity, verifying return values, FIFO ordering,
reject-new accounting, actual context saves and unwind. A snapshot case
admits the ISR from EA restore and still publishes its original capture.
The simulator's pending-request sampling relative to atomic JBC is explicit;
none of this claims CC2530 RF interrupt delivery or physical priority timing.

Whole CODE mutation, private ABI/allocation, matching per-link listing,
exactly-once RFD, FSCAL1 mask, real timebase, ISR call graph, upper IRAM,
alias and unallocated/status guards are checked. The 15-second per-simulator
limit remains unchanged; transcripts are indexed rather than repeatedly
scanned or truncated. No physical operation or private capture is performed.

Both board definitions produce identical linked CODE and pass the complete
private/MMIO layout checks and native queue/RX composition. The directly
coupled standalone RX corpus remains unchanged: 151,177 host cases, 33 linked
traces and its original 5,214-byte CODE/375-byte ordinary XDATA allocation.

```sh
make BUILD=build/radio-queue-dev test-radio-queue
```

**Never flash `radio_queue_test.ihx` or upload it as a board artifact.**
TX/CCA, CSMA/ACK/retries, association and separately authorized on-air
acceptance remain subsequent roadmap tasks.
