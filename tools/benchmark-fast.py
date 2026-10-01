#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse, json, os, pathlib, re, select, statistics, subprocess, time
parser=argparse.ArgumentParser(description="Map/cancel 12 test overlays; no screenshot is taken.")
parser.add_argument("binary", type=pathlib.Path, help="Path to spectacle-fast; stop its installed service first")
parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("fast-benchmark-results.json"))
args=parser.parse_args()
R=pathlib.Path(__file__).resolve().parent
B=args.binary.resolve()
assert B.is_file(), B
D=pathlib.Path(os.environ.get("XDG_DATA_HOME", str(pathlib.Path.home()/".local/share")))/"applications/org.kde.spectacle.fast.test.desktop"
D.parent.mkdir(parents=True,exist_ok=True)
def run(*command,check=True):
 result=subprocess.run(command,text=True,capture_output=True)
 if check and result.returncode:raise RuntimeError(result.stderr)
 return result.stdout.strip()
SERVICE='org.kde.Spectacle.Fast'
NAME='spectacle-fast-measurement'
reader=None;proc=None;rows=[]
def call(method):return run('qdbus6',SERVICE,'/org/kde/Spectacle/Fast',method)
def owner():return run('qdbus6','org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.NameHasOwner',SERVICE)=='true'
def memory(pid):
 text=pathlib.Path(f'/proc/{pid}/smaps_rollup').read_text()
 result={k:round(int(v)/1024,2) for k,v in re.findall(r'(?m)^(Rss|Pss):\s+(\d+)',text)}
 gpu={}
 for fd in pathlib.Path(f'/proc/{pid}/fdinfo').iterdir():
  s=fd.read_text()
  if 'drm-driver:' in s:
   data={k:v.strip() for k,v in re.findall(r'(?m)^(drm-[^:]+):\s*(.+)$',s)}
   gpu[(data.get('drm-pdev'),data.get('drm-client-id'))]=data
 result['drm']=list(gpu.values());return result
try:
 assert not D.exists()
 assert not owner()
 D.write_text(f'[Desktop Entry]\nType=Application\nName=Temporary raster selector benchmark\nExec={B}\nNoDisplay=true\nStartupNotify=false\nX-KDE-Wayland-Interfaces=org_kde_plasma_window_management\nX-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2\n')
 run('kbuildsycoca6','--noincremental')
 sid=run('qdbus6','org.kde.KWin','/Scripting','org.kde.kwin.Scripting.loadScript',str(R/'latency-observer.js'),NAME)
 run('qdbus6','org.kde.KWin',f'/Scripting/Script{sid}','org.kde.kwin.Script.run')
 reader=subprocess.Popen(['journalctl','--user','-f','-n','0','-o','json','_COMM=kwin_wayland'],stdout=subprocess.PIPE,bufsize=0)
 env=os.environ.copy();env['SPECTACLE_FAST_TRACE']='1';env['QT_FORCE_STDERR_LOGGING']='1';env['QT_LOGGING_RULES']='*.debug=false;*.info=false;default.info=true'
 proc=subprocess.Popen([str(B),'--daemon'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 until=time.monotonic()+5
 while not owner():
  if proc.poll() is not None or time.monotonic()>until:raise RuntimeError('Start failed')
  time.sleep(.025)
 time.sleep(.5)
 print('Idle before',json.dumps(memory(proc.pid)),flush=True)
 for i in range(12):
  start=time.time_ns()/1e6
  run('gdbus','call','--session','--dest',SERVICE,'--object-path','/org/kde/Spectacle/Fast','--method',SERVICE+'.Capture')
  until=time.monotonic()+5;found=None
  while time.monotonic()<until:
   if not select.select([reader.stdout],[],[],.1)[0]:continue
   msg=json.loads(reader.stdout.readline()).get('MESSAGE','')
   if 'SPECTACLE_FAST_BENCHMARK ' not in msg:continue
   event=json.loads(msg.split('SPECTACLE_FAST_BENCHMARK ',1)[1])
   if event['pid']==proc.pid and event['time_ms']>=start:found=event;break
  if found is None:raise RuntimeError('No overlay; inspect fast-benchmark.log')
  time.sleep(.05)
  row=dict(iteration=i,mapped_ms=round(found['time_ms']-start,1),active=memory(proc.pid))
  call('Cancel');time.sleep(.65);row['idle']=memory(proc.pid)
  print(json.dumps(row),flush=True);rows.append(row)
 values=[r['mapped_ms'] for r in rows]
 print('first',values[0],'repeat median',statistics.median(values[1:]),'range',min(values),max(values),flush=True)
finally:
 if proc and proc.poll() is None:proc.terminate();proc.wait(timeout=3)
 if reader:reader.terminate();reader.wait(timeout=3)
 run('qdbus6','org.kde.KWin','/Scripting','org.kde.kwin.Scripting.unloadScript',NAME,check=False)
 D.unlink(missing_ok=True);run('kbuildsycoca6','--noincremental')
 args.output.write_text(json.dumps(rows,indent=2))
