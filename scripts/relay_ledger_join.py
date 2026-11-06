"""Join local ledger dumps; never infer delivery from a queue success.

Frame denominator is the contiguous network-frame interval covered by source
scheduler targets and receiver B records, trimmed by three frames at both ends.
An absent source event remains unknown, not a fabricated TX or RF loss.
"""
import argparse, collections, json, pathlib, re
EVENTS = dict(enumerate(('source','capture','miss','selected','repair','B','accept','duplicate','obsolete','expire','target','source_refusal','helper_B','repair_refusal','source_window','repair_window','source_build_cycles','source_encode_cycles','source_admit_cycles','repair_build_cycles','repair_wait_late','repair_final_cycles','source_wait_late','capture_start','source_setup_cycles','source_wait_cycles','source_final_cycles','source_start_cycles','source_tx_start','source_tx_complete','source_tx_end_tick','capture_radio_state','direct_seen','rescue_plan','grant_helpers','survey_history'),1))

def parse(path):
 text = path.read_text()
 headers = re.findall(r'LEDGER begin count=(\d+) dropped=(\d+)', text)
 rows = [tuple(map(int,m)) for m in re.findall(r'LEDGER (\d+) (\d+) (\d+) (\d+) (-?\d+) (\d+) (\d+)',text)]
 if len(headers)!=1 or 'LEDGER end' not in text: raise ValueError(f'{path}: incomplete dump')
 count,dropped=map(int,headers[0])
 if dropped or count!=len(rows): raise ValueError(f'{path}: overflow or missing rows: {count}/{len(rows)}, dropped={dropped}')
 return rows

def join(paths,child):
 byframe=collections.defaultdict(list); targets=[]; broadcast=[]
 for path in paths:
  for session,frame,c,event,detail,peer,age in parse(path):
   if c!=child: continue
   byframe[session,frame].append(dict(device=path.stem,event=EVENTS.get(event,f'event{event}'),detail=detail,peer=peer,age=age))
   if event==11:targets.append((session,frame))
   if event==6:broadcast.append((session,frame))
 sessions={s for s,f in targets+broadcast}
 if len(sessions)!=1 or not targets or not broadcast:raise ValueError('Need source targets and receiver B in one session')
 session=sessions.pop()
 lo=max(min(f for s,f in targets),min(f for s,f in broadcast))+3
 hi=min(max(f for s,f in targets),max(f for s,f in broadcast))-3
 if hi<lo:raise ValueError('No complete common interval')
 bins=collections.Counter({str(i):0 for i in range(4)});bins['lost']=0
 attribution=collections.Counter();rows=[]
 refusals={name:collections.Counter() for name in ("source_refusal","repair_refusal")}
 refused_frames=collections.Counter()
 for frame in range(lo,hi+1):
  events=byframe[session,frame]
  for name in refusals:
   reasons={e['detail'] for e in events if e['event']==name}
   for reason in reasons: refusals[name][str(reason)]+=1
  for name in ('source','repair'):
   if any(e['event']==name and e['detail']==-11 for e in events): refused_frames[name]+=1
  accepts=[e['age'] for e in events if e['event']=='accept']
  age=min(accepts) if accepts else None
  bins[str(age) if age is not None and age<=3 else 'lost']+=1
  if age is None:
   source=[e for e in events if e['event']=='source']
   if not source: reason='source_unobserved'
   elif not any(e['detail']==0 for e in source):reason='source_refused'
   elif any(e['event']=='obsolete' for e in events):reason='receiver_obsolete'
   elif any(e['event']=='repair' and e['detail']==0 for e in events):reason='queued_no_accept'
   elif any(e['event']=='repair' for e in events):reason='repair_refused'
   elif any(e['event']=='selected' for e in events):reason='repair_result_unobserved'
   elif any(e['event']=='capture' for e in events):reason='captured_no_selection'
   else:reason='capture_unobserved_or_missed'
   attribution[reason]+=1
  rows.append(dict(session=session,frame=frame,events=events,age=age))
 return dict(refusal_frames=dict(refused_frames), refusal_reasons={k:dict(v) for k,v in refusals.items()},child=child,first=lo,last=hi,denominator=len(rows),ages=dict(bins),loss_attribution=dict(attribution),within3=sum(bins[str(i)] for i in range(4))/len(rows),rows=rows)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('child',type=int);p.add_argument('files',nargs='+',type=pathlib.Path);p.add_argument('-o',type=pathlib.Path,required=True);a=p.parse_args()
 result=join(a.files,a.child);a.o.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='rows'},indent=2))
