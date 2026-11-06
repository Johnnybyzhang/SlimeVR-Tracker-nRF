# Local ledger refusal attribution

Events 12, 13 and 14 are source refusal, helper B decision and repair refusal.
The existing source/repair events retain errno. No wire format changes.

Refusal details: 0 unknown, 1 channel wait, 2 stale/missing layout grant,
3 slot admission, 4 final validation, 5 no repair target, 6 empty payload,
7 target wait, 8 radio busy, 9 precondition gate, 10 ESB queue refusal.
Repair per-record reasons exist only after selection; no-target and gate calls
have no selected record and must not be counted as refused source frames.

Helper B detail bit 0 is the missing bit for the recorded source frame.
Bits 1 onward identify local repair-slot grants for that child. A B record
proves local receipt, not successful selection or submission. Its age is the
source age at B. Missing records are not proof of missed B.

The joiner counts unique frames with any EAGAIN separately for source and
repair, even if a later attempt succeeds. Reason counts are unique per frame
and reason, so their sum may exceed the refused-frame count.

Events 15/16 record source/repair window failure detail, using the exact clock
sample that rejected admission. Positive values are ticks beyond the unchanged
reserve deadline. Negative values -101/-102/-104/-105 mean quiet/early/duplicate/
missing prior admission; -32768 means rejection outside this window check.
These records are emitted only after failure. Diagnostic-disabled compilation
has no storage or stores. The TDMA host cases run in both configurations.

Events 17-19 measure source construction/event work after its fresh-pose
wake, encoding, and ESB preparation before admission. Events 20 and 22
measure repair construction and admission-to-final-check work. Details are
DWT CPU cycles saturated at 32767, not network ticks. The Smol CPU runs
at 64 MHz. These samples require CONFIG_RELAY_LEDGER_TIMING=y (default off);
they are recorded after submission and disappear in production builds.

Event 21 records repair handoff lateness in network ticks relative to the
software handoff target, four ticks before TX target. It uses the wait's
existing clock sample. Zero means on time; -32768 means unavailable.
This does not change the TX target, deadline, reserve or RX lead.

Recovery acceptance uses the joiner's lost frames, including refusals that
never recover. Raw refusal-frame counts include covered refusals and are
diagnostic totals, not independent loss rates. Source-only joins cannot
attribute helper losses; missing helper rows do not prove no radio capture.

Event 23 records source fresh-pose wake lateness using the existing wait sample.

Capture miss detail 1 means radio acquisition was attempted but refused;
5 means the acquisition deadline had passed before the first attempt;
6 means the clock was unavailable. Details 2/3/4 remain no END, bad CRC and
unusable capture.

Event 24 samples network time immediately after RXEN and records ticks
relative to the child slot start. It is emitted after the receive window,
never before radio startup. -32768 means the clock sample was unavailable.
This sample also requires CONFIG_RELAY_LEDGER_TIMING=y.
