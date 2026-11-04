#!/usr/bin/env python3
"""Exercise the production B RX window, including IRQ delivery after claim."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = Path(os.environ.get('SOURCE_ROOT', HERE.parents[2]))
sys.path.insert(0, str(HERE.parent / 'harness/python'))
from c_extract import extract_block
source = (ROOT / 'src/connection/esb.c').read_text()
window = extract_block(source, r'^bool esb_relay_control_window\([^;{]*\)\s*\{')
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "relay_protocol.h"
#define BIT(n) (1U<<(n))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
static unsigned control_window_fail[6], control_open_late, control_open_lag_max;
static unsigned control_crc_fail,control_missed,control_open_hist[16][2];
static unsigned control_no_address,control_address_timeout,radio_channel;
static unsigned depth,claimed,started,stopped,clock_users,attempts;
static unsigned tick=100,busy_attempts,delay_calls;
static bool clock_fail,init_fail,wait_fail,network_fail,timeout,take_fail,stamp_fail,reject;
struct relay_rx_frame {uint32_t end_kernel_ticks;};
struct esb_network_clock {uint32_t receiver_ticks;};
static unsigned irq_lock(void){return depth++;}
static void irq_unlock(unsigned key){
 assert(depth==key+1);depth=key;
 /* Model a pending interrupt stretching the unprotected claim-to-RXEN gap. */
 if(!depth && claimed && !started) tick+=20;
}
static int esb_relay_clock(bool on){
 if(on){if(clock_fail)return -1;clock_users++;}else{assert(clock_users);clock_users--;}
 return 0;
}
static int radio_capture_init(void){return init_fail ? -1 : 0;}
static bool tdma_wait_network_tick(uint32_t t){assert(t==100 && !depth);return !wait_fail;}
static bool tdma_network_now(uint32_t *t){*t=tick;return !network_fail;}
static bool esb_raw_rx_begin(void){
 unsigned key=irq_lock();bool ok=attempts++>=busy_attempts;
 if(ok)claimed=1;irq_unlock(key);return ok;
}
static void k_busy_wait(unsigned t){assert(!depth);tick+=t;delay_calls++;}
static bool radio_capture_rx_end(uint32_t *t){*t=tick;return !stamp_fail;}
static void radio_capture_set_enabled(bool enabled){assert(enabled && claimed);}
static bool relay_radio_start(unsigned channel,unsigned pipes,bool rx){
 (void)channel;assert(pipes==BIT(1) && rx && claimed && depth && tick<110);started++;return true;
}
static bool esb_get_network_clock(struct esb_network_clock *c){c->receiver_ticks=tick;return true;}
static bool relay_radio_ended(void){return !timeout;}
static bool relay_radio_take(struct relay_rx_frame *f){(void)f;return !take_fail;}
static void relay_radio_peek(uint8_t *len,bool *crc,uint8_t *rssi){*len=1;*crc=false;*rssi=0;}
static bool relay_radio_stop(void){assert(started && !depth);stopped++;return true;}
static void esb_raw_rx_end(void){assert(claimed);claimed=0;}
static bool esb_relay_control_receive(struct relay_rx_frame *f){(void)f;assert(!depth && !claimed);return !reject;}
static bool relay_radio_addressed(void){return false;}
'''
cases = r'''
int main(int argc,char **argv){
 assert(argc==2);const char *c=argv[1];
 clock_fail=!strcmp(c,"clock");init_fail=!strcmp(c,"init");wait_fail=!strcmp(c,"wait");
 timeout=!strcmp(c,"no-end");take_fail=!strcmp(c,"crc");stamp_fail=!strcmp(c,"stamp");reject=!strcmp(c,"reject");
 if(!strcmp(c,"busy"))busy_attempts=2;
 if(!strcmp(c,"deadline") || !strcmp(c,"no-clock"))busy_attempts=100;
 network_fail=!strcmp(c,"no-clock");
 bool early=clock_fail||init_fail||wait_fail||busy_attempts==100;
 bool result=esb_relay_control_window(100);
 assert(result==!(early||timeout||take_fail||stamp_fail||reject));
 assert(!depth && !claimed && !clock_users);
 assert(started==!early && stopped==started);
 if(!strcmp(c,"busy"))assert(attempts==3 && delay_calls==2);
 if(timeout)assert(control_window_fail[5]==1 && control_no_address==1);
 if(take_fail)assert(control_window_fail[3]==1 && control_crc_fail==1);
 if(stamp_fail)assert(control_window_fail[4]==1);
 if(clock_fail)assert(control_window_fail[0]==1);
 if(init_fail)assert(control_window_fail[2]==1);
 if(wait_fail||busy_attempts==100)assert(control_window_fail[1]==1);
 printf("PASS control window %s\n",c);return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='relay-control-', dir='/tmp') as tmp:
    path = Path(tmp) / 'control.c'
    path.write_text(prefix + window + cases)
    binary = Path(tmp) / 'control'
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
        '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-O1',
        '-fsanitize=address,undefined', '-I', str(ROOT / 'src/connection'),
        str(path), '-o', str(binary)], check=True)
    for case in sys.argv[1:] or ('valid','busy','deadline','no-clock','clock','init','wait','no-end','crc','stamp','reject'):
        subprocess.run([str(binary), case], check=True)
