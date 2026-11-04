/* Included after the extracted TDMA code (see run_tdma.py). Layout: own slot
 * 0, 8 slots of 16 ticks (frame 128 ticks), PING period 256 frames. */

static void frame_start_exact(void)
{
	uint32_t n = 0;
	for (unsigned i = 0; i < 100000; i++) {
		assert(relay_frame_start(0xfffffff0U, n) == 0xfffffff0U + (uint32_t)((uint64_t)n * 4096U / 15U));
		n = n * 1664525U + 1013904223U;
	}
	assert(relay_frame_start(0, UINT32_MAX) == (uint32_t)((uint64_t)UINT32_MAX * 4096U / 15U));
	/* Fast skew projection must preserve signed truncation at its boundary. */
	for (int32_t skew = -1000001; skew <= 1000001; skew += 100000) {
		for (uint32_t elapsed = 0; elapsed < 10000; elapsed += 17) {
			assert(esb_project_network_ticks(100 + elapsed, 100, 7, skew)
			       == 107 + elapsed + (uint32_t)((int64_t)skew * elapsed / 1000000000LL));
		}
	}
}

static void configure(uint8_t own, uint8_t total)
{
	tdma_cfg_pack = 0;
	assert(tdma_update_config(own, total, 16, 7));
	tdma_last_admitted_frame = tdma_forward_admitted_frame = UINT32_MAX;
}

static void data_windows(void)
{
	assert(TDMA_UNPACK_SLOT(TDMA_PACK(16, 16, 17, 255)) == 16);
	configure(0, 8);
	uint32_t start;
	/* Ordinary frame: next start of slot 3 strictly after `after`. */
	set_ticks(10 * 128 + 5);
	assert(tdma_slot_data_window(3, now_ticks(), &start));
	assert(start == 10 * 128 + 3 * 16);
	assert(tdma_slot_data_window(3, 10 * 128 + 3 * 16, &start));
	assert(start == 11 * 128 + 3 * 16);
	/* Slot 2 PINGs in frame 64 (phase 2*256/8) and spills into slot 3. */
	assert(tdma_slot_data_window(3, 63 * 128 + 3 * 16, &start));
	assert(start == 65 * 128 + 3 * 16);
	/* Slot 3's own PING frame (96) is skipped too. */
	assert(tdma_slot_data_window(3, 95 * 128 + 3 * 16, &start));
	assert(start == 97 * 128 + 3 * 16);
	/* Frame 0 (the 32-bit rollover frame) is never a data frame. */
	assert(tdma_slot_data_window(5, 0, &start) && start == 128 + 5 * 16);
	assert(!tdma_slot_data_window(8, 0, &start)); /* outside the layout */
	puts("PASS relay data windows follow the shared guard rules");
}

static void ping_windows(void)
{
	configure(0, 8);
	uint32_t start;
	uint16_t length;
	assert(tdma_slot_ping_window(5, 1000, &start, &length));
	assert(start == 160 * 128 + 5 * 16); /* phase 5*256/8 = 160 */
	assert(length == 2 * 16 + 2 + 6);    /* both slots, offset, late tolerance */
	assert(tdma_slot_ping_window(5, start, &start, &length));
	assert(start == (160 + 256) * 128 + 5 * 16);
	puts("PASS presence windows land on other trackers' PING exchanges");
}

static void forward_slot(void)
{
	/* 8 direct members + one forwarding slot (index 8): frame 144 ticks. */
	configure(0, 9);
	set_ticks(20 * 144 + 8 * 16 - 3);
	uint32_t before = sleeps;
	assert(tdma_wait_for_forward_slot(8, 72));
	uint32_t pos = now_ticks() % 144;
	assert(pos >= 8 * 16 + 2 && pos <= 8 * 16 + 16 - 12); /* 72-byte reserve */
	assert(sleeps > before);
	uint32_t frame = now_ticks() / 144;
	/* One packet per frame: the next admission is a later frame. */
	assert(!tdma_wait_for_forward_slot(8, 72));
	assert(now_ticks() / 144 == frame); /* return before waiting across own slot */
	assert(tdma_last_admitted_frame == UINT32_MAX);
	assert(tdma_wait_for_slot(17));
	assert(now_ticks() / 144 > frame);
	assert(tdma_wait_for_forward_slot(8, 72));
	/* The own slot is never a forwarding slot; the layout bounds it. */
	assert(!tdma_wait_for_forward_slot(0, 72));
	assert(!tdma_wait_for_forward_slot(9, 72));
	/* Guard: slot 7 PINGs in frame 7*227/9 = 176 and spills into slot 8. */
	uint32_t period = 32768 / 144;
	uint32_t guard_frame = 7 * period / 9;
	set_ticks(guard_frame * 144 + 8 * 16 - 1);
	assert(!tdma_wait_for_forward_slot(8, 72));
	assert(tdma_wait_for_slot(17));
	assert(tdma_wait_for_forward_slot(8, 72));
	assert(now_ticks() / 144 != guard_frame);
	puts("PASS forwarding slot admission: reserve, one per frame, guards, independence");
}


static void final_admission_check(void)
{
 configure(1,8);set_ticks(20*128+16+1);
 assert(tdma_wait_for_slot(17));assert(tdma_slot_tx_valid(17));
 uint32_t now=now_ticks();set_ticks(now+16);
 assert(!tdma_slot_tx_valid(17));
 set_ticks(now);tdma_cfg_pack^=(1U<<24);
 assert(!tdma_slot_tx_valid(17));
 puts("PASS final TX validation rejects elapsed and replaced admissions");
}

/* Staged layout activation: the staged tuple is promoted by the first
 * snapshot at or after its activation tick, no data or PING window straddles
 * that tick, an unassigned stage withdraws, and a later tuple recovers. */
static void staged_activation(void)
{
	struct tdma_layout layout;
	configure(0, 8);
	set_ticks(1000 * 128 + 20);
	uint32_t act = 1003 * 128 + 2; /* 2 ticks into own slot 0 of frame 1003 */
	assert(tdma_stage_config(3, 9, 16, 8, act));
	assert(!tdma_stage_config(9, 9, 16, 8, act)); /* slot outside layout */
	assert(tdma_layout_get(&layout) && layout.epoch == 7 && layout.total_slots == 8);
	/* Frames 1001 and 1002 are clear of the activation: old layout. */
	assert(tdma_wait_for_slot(17));
	assert(now_ticks() / 128 == 1001 && tdma_get_config_epoch() == 7);
	assert(tdma_wait_for_slot(17));
	assert(now_ticks() / 128 == 1002);
	/* Frame 1003's own slot straddles the activation: never admitted. The
	 * next admission is in the new layout (slot 3 of 9, frame 144 ticks);
	 * the wait that crosses the activation aborts and the caller retries. */
	assert(!tdma_wait_for_slot(17));
	assert(tdma_wait_for_slot(17));
	assert(now_ticks() >= act + TDMA_ACTIVATION_QUIET_TICKS);
	assert(tdma_layout_get(&layout) && layout.epoch == 8 && layout.slot_index == 3
		&& layout.total_slots == 9);
	assert(now_ticks() % 144 >= 3 * 16 && now_ticks() % 144 < 4 * 16);
	assert(!tdma_layout_withdrawn());

	/* Removal: an unassigned stage disables TDMA at activation. */
	act = now_ticks() + 1000;
	assert(tdma_stage_config(TDMA_SLOT_UNASSIGNED, 8, 16, 9, act));
	assert(tdma_layout_get(&layout) && layout.epoch == 8);
	set_ticks(act);
	assert(!tdma_layout_get(&layout) && tdma_layout_withdrawn());
	assert(!tdma_wait_for_slot(17));
	/* A later authoritative tuple restores transmission rights. */
	assert(tdma_update_config(2, 8, 16, 10));
	assert(!tdma_layout_withdrawn() && tdma_layout_get(&layout) && layout.epoch == 10);

	/* A stage cleared before activation (receiver dropped it) never applies;
	 * a tuple of the staged epoch, heard directly, supersedes the stage. */
	act = now_ticks() + 1000;
	assert(tdma_stage_config(1, 8, 16, 11, act));
	tdma_stage_clear();
	set_ticks(act + 10);
	assert(tdma_layout_get(&layout) && layout.epoch == 10);
	act = now_ticks() + 1000;
	assert(tdma_stage_config(1, 8, 16, 11, act));
	assert(tdma_update_config(4, 8, 16, 11));
	set_ticks(act + 10);
	assert(tdma_layout_get(&layout) && layout.epoch == 11 && layout.slot_index == 4);
	puts("PASS staged layout activation: promotion, quiet zone, withdrawal, recovery");
}

/* Review counterexample (old slot 0 and a new forwarding slot admitting at
 * the same tick): with staging, every admission around an activation, at any
 * phase, lies wholly in the old layout before the quiet zone or wholly in the
 * new one after it, so trackers that switch together cannot mix layouts.
 * Trackers that missed the announcement are held off by the layout grant
 * (esb_layout_granted, run_submission.py / run_presence.py). */
static void staged_no_mixed_layouts(void)
{
	for (uint32_t phase = 0; phase < 2 * 144; phase++) {
		configure(0, 11);   /* old: own slot 0 of 11 x 16 ticks (176/frame) */
		uint32_t act = 2000 * 176 + phase;
		set_ticks(act - 3 * 176);
		assert(tdma_stage_config(1, 12, 16, 8, act)); /* new: slot 1 of 12 */
		for (unsigned n = 0; n < 8; n++) {
			if (!tdma_wait_for_slot(17)) {
				continue; /* aborted at the promotion; retried */
			}
			uint32_t t = now_ticks();
			struct tdma_layout layout;
			assert(tdma_layout_get(&layout));
			uint32_t slot_start = t - (t % layout.frame_ticks) + layout.slot_index * layout.slot_ticks;
			uint32_t slot_end = slot_start + layout.slot_ticks;
			if (layout.epoch == 7) {
				assert(slot_end <= act - TDMA_ACTIVATION_QUIET_TICKS);
			} else {
				assert(layout.epoch == 8 && layout.total_slots == 12);
				assert(slot_start >= act + TDMA_ACTIVATION_QUIET_TICKS);
			}
			set_ticks(t + 1);
		}
		assert(tdma_get_config_epoch() == 8);
	}
	puts("PASS staged activation never mixes layouts at any activation phase");
}

static void stream_profile(void)
{
 const uint32_t origin=123456;
 configure(2,3);set_ticks(origin-1000);
 assert(tdma_stage_config(2,0x81,16,8,origin));
 /* A running profile is announced alongside a same-epoch legacy tuple. */
 assert(tdma_update_config(2,10,16,8));
 set_ticks(origin);assert(tdma_profile_get()==1);
 granted = false;
 uint32_t first; bool broadcast;
 assert(tdma_stream_next(&first, &broadcast) && broadcast);
 assert(first == origin + RELAY_V3_B_START - RELAY_RX_LEAD_TICKS);
 granted = true;
 struct tdma_layout layout;assert(tdma_layout_get(&layout));
 assert(layout.profile==1 && layout.total_slots==17 && layout.slot_index==2);
 for(unsigned f=0;f<120;f++) {
  uint32_t own=relay_frame_start(origin,f)+34;
  set_ticks(own-1);
  uint32_t target;bool b;
  assert(tdma_stream_next(&target,&b) && !b && target==own);
  set_ticks(own+1);
  assert(tdma_stream_next(&target,&b) && !b && target==own+1);
  assert(tdma_wait_for_slot(60));
  assert(tdma_slot_tx_valid(60) && !tdma_wait_for_slot(17));
  set_ticks(own+1);assert(tdma_stream_next(&target,&b) && b);
  assert(target==relay_frame_start(origin,f)+172);
  for (unsigned slot = RELAY_V3_REPAIR_FIRST_SLOT; slot < RELAY_V3_TOTAL_SLOTS; slot++) {
	  uint32_t tx_target=relay_frame_start(origin, f) + slot * RELAY_V3_SLOT_TICKS + RELAY_V3_TX_OFFSET;
	  set_ticks(tx_target - 4U);
	  assert(tdma_wait_for_forward_slot(slot, 72));
	  assert(now_ticks() >= tx_target && now_ticks() <= tx_target + 2U);
	  assert(tdma_forward_tx_valid(slot, 72));
	  assert(!tdma_wait_for_forward_slot(slot, 72));
  }
 }
 assert(relay_frame_start(origin,120)==origin+32768);
 puts("PASS profile bootstrap, 120 direct admissions and compulsory B deadlines");
}

static void stream_d0_boundary(void)
{
 const uint32_t origins[] = {123456U, 0xfffffe00U};
 for (unsigned o=0;o<2;o++) {
  uint32_t origin=origins[o];
  configure(0,3);set_ticks(origin-1000);
  assert(tdma_stage_config(0,0x81,16,8,origin));
  set_ticks(origin);assert(tdma_profile_get()==1);granted=true;
  for (unsigned f=1;f<120;f++) {
   uint32_t start=relay_frame_start(origin,f);
   set_ticks(start-3);
   assert(!tdma_wait_for_slot(60)); /* Outside the unchanged early hold. */
   set_ticks(start-2);
   assert(!tdma_slot_tx_valid(60)); /* Never transmit in the preceding frame. */
   assert(f & 1 ? tdma_admit_stream_source(60) : tdma_wait_for_slot(60));
   assert(now_ticks()==start+TDMA_SLOT_TARGET_OFFSET);
   assert(tdma_slot_tx_valid(60));
   assert(!tdma_wait_for_slot(60));
   set_ticks(start+RELAY_V3_SLOT_TICKS-TDMA_LEN33_PLUS_RESERVE_TICKS+1);
   assert(!tdma_slot_tx_valid(60)); /* Original final deadline remains exact. */
  }
 }
 puts("PASS D0 early handoff across frame and clock rollover, with unchanged deadline");
}

static void coarse_sleep_entry_delay(void)
{
 configure(0, 8); set_ticks(1000);
 sleep_entry_delay = 150;
 assert(tdma_wait_network_tick(1040));
 assert(now_ticks() == 1040);
 sleep_entry_delay = 0;
 puts("PASS coarse wait preserves target despite sleep-entry work");
}

static void stream_only_admission(void)
{
 configure(3,8);set_ticks(1000);unsigned before=sleeps;
 assert(!tdma_admit_stream_source(17));
 assert(sleeps==before);
}

int main(void)
{
 coarse_sleep_entry_delay();
	frame_start_exact();
	stream_profile();
	stream_d0_boundary();
	stream_only_admission();
	data_windows();
	ping_windows();
	forward_slot();
	final_admission_check();
	staged_activation();
	staged_no_mixed_layouts();
	return 0;
}
