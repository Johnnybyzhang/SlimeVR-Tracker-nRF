#!/usr/bin/env python3
"""Exercise bounded diagnostic storage without a console or radio."""
import pathlib, subprocess, tempfile, os, shlex
root=pathlib.Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory() as d:
 p=pathlib.Path(d);(p/'zephyr/sys').mkdir(parents=True)
 (p/'zephyr/kernel.h').write_text('''#include <assert.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
static unsigned depth;
static unsigned irq_lock(void) { return depth++; }
static void irq_unlock(unsigned key) { assert(depth == key+1); depth=key; }
static void k_msleep(unsigned ms) { assert(!depth && ms == 10); }
''')
 (p/'zephyr/sys/printk.h').write_text('#include <stdio.h>\n#define printk printf\n')
 (p/'main.c').write_text('''#include "frame_ledger.h"
int main(void) {
 ledger_command(3);
 ledger_note(7, 0, 9, LEDGER_SOURCE, 0, 9, 0);
 for (unsigned i=0; i<4097; i++) ledger_note(7, i, 3, LEDGER_SOURCE, -11, 3, 0);
 ledger_command(-1);
 ledger_note(7, 5000, 3, LEDGER_SOURCE, 0, 3, 0);
 ledger_command(9);
 ledger_note(8, 1, 9, LEDGER_ACCEPT, -42, 2, 1);
 ledger_command(-1);
 return 0;
}
''')
 exe=p/'test'
 subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-DCONFIG_RELAY_FRAME_LEDGER=1','-I',str(p),'-I',str(root/'src'),str(p/'main.c'),str(root/'src/frame_ledger.c'),'-o',str(exe)],check=True)
 output=subprocess.check_output([str(exe)],text=True)
 assert 'LEDGER begin count=4096 dropped=1' in output
 assert 'LEDGER 7 4095 3 1 -11 3 0' in output
 assert 'LEDGER 7 4096' not in output and 'LEDGER 7 5000' not in output
 assert 'LEDGER begin count=1 dropped=0' in output
 assert 'LEDGER 8 1 9 7 -42 2 1' in output
 print('PASS ledger: filtering, capacity, overflow, stop, reset and unlocked dump pacing')

# Compile the real deferred B accounting with the real bounded ledger.
import sys
sys.path.insert(0,str(root/'tests/host/harness/python'))
from c_extract import extract_block
account=extract_block((root/'src/connection/relay.c').read_text(),r'^static void relay_control_account\([^;{]*\)\s*\{')
with tempfile.TemporaryDirectory() as d:
 p=pathlib.Path(d);(p/'zephyr/sys').mkdir(parents=True)
 (p/'zephyr/kernel.h').write_text('''#include <assert.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
static unsigned depth;
static unsigned irq_lock(void){return depth++;}
static void irq_unlock(unsigned key){assert(depth==key+1);depth=key;}
static __attribute__((unused)) void k_msleep(unsigned ms){assert(!depth && ms==10);}
''')
 (p/'zephyr/sys/printk.h').write_text('#include <stdio.h>\n#define printk printf\n')
 (p/'main.c').write_text('''#include "zephyr/kernel.h"
#include "frame_ledger.h"
#include "connection/relay_protocol.h"
static struct relay_control control_plan;
static uint8_t control_helper;
static bool control_account_pending;
static struct {unsigned grants;} repair_diag[RELAY_MAX_TRACKERS];
'''+account+'''
int main(void){
 ledger_command_mask((1U<<3)|(1U<<9));
 control_plan=(struct relay_control){.session=7,.frame=80,.repair_count=2};
 control_plan.grants[0]=(struct relay_grant){.owner=4,.children=1U<<3};
 control_plan.grants[1]=(struct relay_grant){.owner=4,.children=1U<<9};
 control_plan.missing[0]=1U<<3;control_plan.missing[1]=1U<<9;
 control_plan.missing[2]=(1U<<3)|(1U<<9);
 control_helper=4;control_account_pending=true;
 relay_control_account();relay_control_account();
 assert(repair_diag[3].grants==1 && repair_diag[9].grants==1 && !depth);
 ledger_command(-1);
}
''')
 exe=p/'account'
 subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-DCONFIG_RELAY_FRAME_LEDGER=1','-I',str(p),'-I',str(root/'src'),str(p/'main.c'),str(root/'src/frame_ledger.c'),'-o',str(exe)],check=True)
 output=subprocess.check_output([str(exe)],text=True)
 assert 'LEDGER begin count=6 dropped=0' in output
 for frame,child,detail,age in ((80,3,3,0),(79,3,2,1),(78,3,3,2),(80,9,4,0),(79,9,5,1),(78,9,5,2)):
  assert f'LEDGER 7 {frame} {child} 13 {detail} 4 {age}' in output
 print('PASS ledger: deferred B accounting preserves grants and every current/aged helper observation exactly once')
