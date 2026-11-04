#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read local shelf timing CSV. Nested durations are deliberately never added into a fake total."""
from pathlib import Path
import csv,io,json,re,statistics,sys

def navigation_summary(rows):
 # Navigation spans may cross a shelf request-ID change during on_enter. Join by time, not request.
 def ms_union(items):
  total=0;left=right=None
  for r in sorted(items,key=lambda r:(r['start_us'],r['end_us'])):
   a,b=r['start_us'],r['end_us']
   if left is None:left,right=a,b
   elif a<=right:right=max(right,b)
   else:total+=right-left;left,right=a,b
  return round((total+(right-left if left is not None else 0))/1000,3)
 pages={0:'Home',1:'Books',2:'Apps',3:'Settings'};result=[]
 for switch in sorted((r for r in rows if r['stage']=='nav_switch'),key=lambda r:r['start_us']):
  start,end=switch['start_us'],switch['end_us'];dest=switch['b']
  children=[r for r in rows if r['kind']=='span' and start<=r['start_us'] and r['end_us']<=end and r is not switch]
  stages={name:ms_union([r for r in children if r['stage']==name]) for name in ('nav_exit','nav_font','nav_enter','nav_present')}
  present=[r for r in children if r['stage']=='nav_present' and r['a']==dest]
  in_present=[r for r in children if any(p['start_us']<=r['start_us'] and r['end_us']<=p['end_us'] for p in present)]
  render=[r for r in in_present if (r['stage']=='nav_render' and r['a']==dest) or (r['stage']=='render' and dest==1)]
  output=[r for r in in_present if r['stage']=='nav_output' and r['a']==dest]
  painting,driving=ms_union(render),ms_union(output)
  result.append({'from':switch['a'],'to':dest,'from_name':pages.get(switch['a'],'Other'),'to_name':pages.get(dest,'Other'),'start_ms':round(start/1000,3),'total_ms':round(switch['duration_us']/1000,3),'exit_ms':stages['nav_exit'],'font_ms':stages['nav_font'],'enter_ms':stages['nav_enter'],'present_ms':stages['nav_present'],'render_ms':painting,'output_ms':driving,'present_other_ms':round(stages['nav_present']-painting-driving,3),'output_groups':len(output),'output_flags':[r['b'] for r in output],'home_clean_output':any(r['a']==0 and r['b']&256 for r in output),'banner_prepare_ms':ms_union([r for r in children if r['stage']=='home_banner']),'banner_cache_hit':any(r['stage']=='home_banner' and r['b']==1 for r in children)})
 return result

def analyze(path):
 text=Path(path).read_text();head=text.splitlines()[0]
 if not head.startswith('# shelf_perf schema=1 ') or not text.endswith('# complete=1\n'):raise ValueError('Incomplete/unsupported shelf performance log')
 metadata=dict(re.findall(r'(\w+)=([^\s]+)',head))
 rows=list(csv.DictReader(io.StringIO('\n'.join(s for s in text.splitlines() if not s.startswith('#')))))
 for row in rows:
  for key in ('start_us','end_us','duration_us','request','a','b','c','d'):row[key]=int(row[key])
  if row['end_us']<row['start_us'] or row['duration_us']!=row['end_us']-row['start_us']:raise ValueError('Inconsistent timestamp')
 if len(rows)!=int(metadata['records']):raise ValueError('Record count mismatch')
 spans={}
 for row in rows:
  if row['kind']=='span':spans.setdefault(row['stage'],[]).append(row['duration_us']/1000)
 durations={k:{'n':len(v),'p50_ms':round(statistics.median(v),3),'max_ms':round(max(v),3),'inclusive_sum_ms':round(sum(v),3)} for k,v in spans.items()}
 requests=[]
 for entry in [r for r in rows if r['stage']=='enter']:
  current=[r for r in rows if r['request']==entry['request']];reuses=[r for r in current if r['stage']=='reuse'];scans=[r for r in current if r['stage']=='scan']
  gate=entry['end_us'] if reuses and reuses[0]['a'] else min((r['end_us'] for r in scans),default=None)
  frames=sorted((r for r in current if r['stage']=='frame' and r['a']==0),key=lambda r:r['end_us'])
  valid=[r for r in frames if gate is not None and r['end_us']>=gate]
  finished=[r for r in valid if r['c']==0];gaps=[r['duration_us']/1000 for r in current if r['stage']=='loop_gap']
  requests.append({'request':entry['request'],'epoch':entry['b'],'reused':bool(reuses and reuses[0]['a']),'first_valid_frame_ms':round((valid[0]['end_us']-entry['start_us'])/1000,3) if valid else None,'pending_covers_exhausted_ms':round((finished[0]['end_us']-entry['start_us'])/1000,3) if finished else None,'shelf_frames':len(frames),'epd_output_groups':sum(r['stage']=='epd' for r in current),'max_loop_gap_over20ms':round(max(gaps),3) if gaps else None,'decode_failures':sum(r['stage']=='cover_load' and r['a']==1 and r['b']==0 for r in current),'scan_flags':[r['b'] for r in scans]})
 return {'metadata':metadata,'incomplete_due_to_drops':int(metadata['dropped'])!=0,'span_statistics':durations,'requests':requests,'navigation':navigation_summary(rows),'notes':['Inclusive span durations overlap; never sum all stages as total latency.','epd counts book present output groups, not individual electrical waveform phases; exclude recovery/error samples.','Timing is software dispatch to output; it excludes the unobserved delay before touch sampling.','Pending mask exhaustion is not a promise that every image decoded; check decode_failures and frame errors.','Navigation joins by enclosing time spans because on_enter may change shelf request IDs.','Navigation total ends after the first app_present and trailing page log; deferred on_tick jobs are separate. nav_output includes the existing output/recovery/settle boundary, not just one electrical phase.','Export may stop capture inside the final transfer-page entry; incomplete final switches have no nav_switch row and are not reported as completed navigation.']}
if __name__=='__main__':print(json.dumps(analyze(sys.argv[1]),ensure_ascii=False,indent=2))
