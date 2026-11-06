# Diagnostic ledger ABI

Build with `CONFIG_RELAY_FRAME_LEDGER=y` in the application. Default is off.
The sysbuild tracker option is `tracker_CONFIG_RELAY_FRAME_LEDGER=y`.
`relay ledger N` clears and arms a local 4096-record buffer for child N.
`relay ledger dump` stops collection before printing. The dump is paced to
avoid the deferred logging buffer overflow observed in trial-ledger3.txt.

Each record is session, full network frame, child, event, detail, peer, age.
The event numbers are defined in src/frame_ledger.h in each repository.
No protocol header or over-the-air record changed. The buffer uses 64 KiB;
there is no buffer and no event-call runtime cost in default builds.

- Source: return code from esb_write, attributed to the scheduled source
  opportunity. Zero means queued, not hardware TX-END. Refusal events identify
  the rejecting branch; see relay_ledger_reasons.md.
- Target: a selected direct-frame scheduler opportunity. The host uses the
  full contiguous network-frame interval, including gaps in these events.
- Capture: validated helper capture, with negative RSSI in dBm.
- Miss: attempted capture outcome, including acquisition refusal, late entry,
  unavailable clock, no END, CRC and unusable frame. See the reason reference.
- Selected/repair: selected cached record and subsequent queue result.
- B: receiver attempted missing announcement for this source frame; detail is
  the broadcast result. A failed B is never represented as a successful B.
- Accept/duplicate/obsolete: receiver repair-publication result and age.
- Expire: cached record aged beyond the existing three-frame cache.

The host rejects partial dumps, dropped records, multiple sessions and missing
source or receiver bounds. It trims three frames from each common boundary.
Lost means no accepted repair in that bounded interval. Source-unobserved and
capture-unobserved are explicit incomplete evidence, not proven RF causes.
No absence of a helper event establishes that the helper failed to capture.

T0/T1 are not collected by capture.py. There is no source TX-END hook.
Helper observations remain incomplete; recovery is established by the joined
receiver acceptance events, including every frame in the bounded denominator.

Detailed phase probes are separately enabled with `CONFIG_RELAY_LEDGER_TIMING=y`
(default off, requires the frame ledger and DWT). This adds source/repair cycle
samples and helper RX startup samples. Recovery acceptance runs should leave it
off: source opportunities, refusal reasons/windows, capture, selection, helper B
and receiver acceptance events remain in the core ledger.
