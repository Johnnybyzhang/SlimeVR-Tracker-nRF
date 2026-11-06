from pathlib import Path
import subprocess,tempfile,sys
h=Path(__file__).resolve().parent
sys.path.insert(0,str(h.parent/'harness/python'))
from c_extract import extract_block
for repo in ('tracker',):
 root=h.parents[2]
 s=(root/'src/console.c').read_text()
 f=extract_block(s,r'^static void\s+console_relay_ledger\([^;{]*\)\s*\{')
 prefix='''#include <stdbool.h>
#define RELAY_MAX_TRACKERS 16U
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static int calls,last,prints;
#define printk(...) (++prints)
#define ledger_command(n) (++calls,last=(n))
#define ledger_command_mask(n) (++calls,last=(n))
'''
 cases='''int main(void){console_relay_ledger("bad",false);
#ifdef CONFIG_RELAY_FRAME_LEDGER
assert(!calls && prints==1);
console_relay_ledger("-1",false);console_relay_ledger("16",false);console_relay_ledger("9999999999999999999999999",false);
assert(!calls);console_relay_ledger("0",false);assert(calls==1 && last==0);
console_relay_ledger("10",false);assert(calls==2 && last==10);
console_relay_ledger("dump",false);assert(calls==3 && last==-1);
console_relay_ledger("0x7ff",true);assert(calls==4 && last==2047);
console_relay_ledger("0x10000",true);assert(calls==4);
console_relay_ledger("15",false);assert(calls==5 && last==15);
console_relay_ledger("0x8000",true);assert(calls==6 && last==32768);
#else
assert(!calls && prints==1);(void)last;
#endif
}
'''
 with tempfile.TemporaryDirectory() as t:
  p=Path(t)/'c.c';p.write_text(prefix+f+cases);exe=Path(t)/'c'
  for flags in ([],['-DCONFIG_RELAY_FRAME_LEDGER=1']):
   subprocess.run(['cc','-Wall','-Wextra','-Werror',*flags,str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('Tracker console: disabled feedback, dump, valid bounds, invalid input and overflow passed')

# Frozen private ABI values from the reviewed heads.
with tempfile.TemporaryDirectory() as t:
 p=Path(t)/'abi.c';p.write_text('#include "frame_ledger.h"\n'+'_Static_assert(LEDGER_SOURCE == 1, "ABI");\n_Static_assert(LEDGER_CAPTURE == 2, "ABI");\n_Static_assert(LEDGER_MISS == 3, "ABI");\n_Static_assert(LEDGER_SELECTED == 4, "ABI");\n_Static_assert(LEDGER_REPAIR == 5, "ABI");\n_Static_assert(LEDGER_B == 6, "ABI");\n_Static_assert(LEDGER_ACCEPT == 7, "ABI");\n_Static_assert(LEDGER_DUPLICATE == 8, "ABI");\n_Static_assert(LEDGER_OBSOLETE == 9, "ABI");\n_Static_assert(LEDGER_EXPIRE == 10, "ABI");\n_Static_assert(LEDGER_TARGET == 11, "ABI");\n_Static_assert(LEDGER_SOURCE_REFUSAL == 12, "ABI");\n_Static_assert(LEDGER_HELPER_B == 13, "ABI");\n_Static_assert(LEDGER_REPAIR_REFUSAL == 14, "ABI");\n_Static_assert(LEDGER_SOURCE_WINDOW == 15, "ABI");\n_Static_assert(LEDGER_REPAIR_WINDOW == 16, "ABI");\n_Static_assert(LEDGER_SOURCE_BUILD_CYCLES == 17, "ABI");\n_Static_assert(LEDGER_SOURCE_ENCODE_CYCLES == 18, "ABI");\n_Static_assert(LEDGER_SOURCE_ADMIT_CYCLES == 19, "ABI");\n_Static_assert(LEDGER_REPAIR_BUILD_CYCLES == 20, "ABI");\n_Static_assert(LEDGER_REPAIR_WAIT_LATE == 21, "ABI");\n_Static_assert(LEDGER_REPAIR_FINAL_CYCLES == 22, "ABI");\n_Static_assert(LEDGER_SOURCE_WAIT_LATE == 23, "ABI");\n_Static_assert(LEDGER_CAPTURE_START == 24, "ABI");\n_Static_assert(LEDGER_SOURCE_SETUP_CYCLES == 25, "ABI");\n_Static_assert(LEDGER_SOURCE_WAIT_CYCLES == 26, "ABI");\n_Static_assert(LEDGER_SOURCE_FINAL_CYCLES == 27, "ABI");\n_Static_assert(LEDGER_SOURCE_START_CYCLES == 28, "ABI");\n_Static_assert(LEDGER_SOURCE_TX_START == 29, "ABI");\n_Static_assert(LEDGER_SOURCE_TX_COMPLETE == 30, "ABI");\n_Static_assert(LEDGER_SOURCE_TX_END_TICK == 31, "ABI");\n_Static_assert(LEDGER_CAPTURE_RADIO_STATE == 32, "ABI");\n_Static_assert(LEDGER_REFUSAL_NONE == 0, "ABI");\n_Static_assert(LEDGER_REFUSAL_CHANNEL == 1, "ABI");\n_Static_assert(LEDGER_REFUSAL_LAYOUT == 2, "ABI");\n_Static_assert(LEDGER_REFUSAL_ADMISSION == 3, "ABI");\n_Static_assert(LEDGER_REFUSAL_FINAL == 4, "ABI");\n_Static_assert(LEDGER_REFUSAL_NO_TARGET == 5, "ABI");\n_Static_assert(LEDGER_REFUSAL_EMPTY == 6, "ABI");\n_Static_assert(LEDGER_REFUSAL_WAIT == 7, "ABI");\n_Static_assert(LEDGER_REFUSAL_BUSY == 8, "ABI");\n_Static_assert(LEDGER_REFUSAL_GATE == 9, "ABI");\n_Static_assert(LEDGER_REFUSAL_QUEUE == 10, "ABI");')
 subprocess.run(['cc','-std=c11','-fsyntax-only','-I'+str(h.parents[3]/'tracker'/'src'),str(p)],check=True)
with tempfile.TemporaryDirectory() as t:
 p=Path(t)/'abi.c';p.write_text('#include "frame_ledger.h"\n'+'_Static_assert(LEDGER_SOURCE == 1, "ABI");\n_Static_assert(LEDGER_CAPTURE == 2, "ABI");\n_Static_assert(LEDGER_MISS == 3, "ABI");\n_Static_assert(LEDGER_SELECTED == 4, "ABI");\n_Static_assert(LEDGER_REPAIR == 5, "ABI");\n_Static_assert(LEDGER_B == 6, "ABI");\n_Static_assert(LEDGER_ACCEPT == 7, "ABI");\n_Static_assert(LEDGER_DUPLICATE == 8, "ABI");\n_Static_assert(LEDGER_OBSOLETE == 9, "ABI");\n_Static_assert(LEDGER_EXPIRE == 10, "ABI");\n_Static_assert(LEDGER_TARGET == 11, "ABI");\n_Static_assert(LEDGER_DIRECT_SEEN == 33, "ABI");\n_Static_assert(LEDGER_RESCUE_PLAN == 34, "ABI");\n_Static_assert(LEDGER_GRANT_HELPERS == 35, "ABI");\n_Static_assert(LEDGER_SURVEY_HISTORY == 36, "ABI");')
 subprocess.run(['cc','-std=c11','-fsyntax-only','-I'+str(h.parents[3]/'receiver'/'src'),str(p)],check=True)
