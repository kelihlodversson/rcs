#!/usr/bin/env python3
# Drive RCS under QEMU: boot pTOS, start the program from a vvfat disk, and
# optionally send it input and take screenshots.
#
# usage: qd.py ARCH PRG OUT ACTIONS...   ARCH m68k|arm
#   actions: wN (seconds)  sNAME (screenshot)  cX,Y (click)  dX,Y (double click)
#            kA-B-C (keys, qcodes joined by -)  D (drag) P/U/R/m (pointer)  hCMD (monitor)
#
# Environment:
#   PTOS_M68K_ELF  path to the pTOS virt-m68k ELF image (required for m68k)
#   PTOS_ARM_ELF   path to the pTOS virt-arm ELF image (required for arm)
#   QD_RSC         optional resource file: copied to DESK.RSC and started with
#                  LAUNCH2.PRG instead of LAUNCH.PRG (which starts RCS.RSC)
#   QD_DFN         definition file to copy next to DESK.RSC (default: the .def
#                  file beside QD_RSC, if there is one)
#   QD_NODFN       if set, do not copy any definition file
#   QD_DEFEXT      if set, copy that definition file as DESK.DEF instead of DESK.DFN
#
# Output goes to build/emulator/ under the repository root: the vvfat disk
# (qdir-ARCH), the serial log (qd-ARCH.out) and screenshots (NAME-N.png).
# The RCS resource files are taken from src/atari/.
import sys,os,socket,json,subprocess,time,shutil

HERE=os.path.dirname(os.path.abspath(__file__))
REPO=os.path.dirname(os.path.dirname(HERE))
S=HERE                                   # launcher PRGs live next to this script
B=os.path.join(REPO,'build','emulator')  # everything written goes here
R=os.path.join(REPO,'src','atari')+os.sep
os.makedirs(B,exist_ok=True)

arch,prg,out=sys.argv[1:4]; acts=sys.argv[4:]
ELF={'m68k':os.environ.get('PTOS_M68K_ELF'),'arm':os.environ.get('PTOS_ARM_ELF')}
if arch not in ELF:
    sys.exit('qd.py: ARCH must be m68k or arm')
if not ELF[arch]:
    sys.exit('qd.py: set PTOS_%s_ELF to the pTOS %s image' % (arch.upper(), 'virt-m68k.elf' if arch=='m68k' else 'virt-arm.elf'))

d=B+'/qdir-'+arch
shutil.rmtree(d,ignore_errors=True); os.makedirs(d)
for f,n in (('rcs.rsc','RCS.RSC'),('rcslow.rsc','RCSLOW.RSC'),('rcs.dfn','RCS.DFN'),('rcslow.dfn','RCSLOW.DFN')): shutil.copy(R+f,d+'/'+n)
shutil.copy(prg,d+'/RCS.PRG')
extra=os.environ.get('QD_RSC')
l2='2' if extra else ''
shutil.copy(S+('/LAUNCH%s.PRG'%l2 if arch=='m68k' else '/LAUNCH%s-ARM.PRG'%l2),d+'/LAUNCH.PRG')
if extra:
    shutil.copy(extra,d+'/DESK.RSC')
    # the definition file RCS needs next to the resource: QD_DFN, else the .def beside QD_RSC
    dfn=os.environ.get('QD_DFN') or (extra[:-4]+'.def' if os.path.exists(extra[:-4]+'.def') else None)
    if dfn and not os.environ.get('QD_NODFN'): shutil.copy(dfn,d+('/DESK.DEF' if os.environ.get('QD_DEFEXT') else '/DESK.DFN'))
open(d+'/EMUDESK.INF' if arch=='m68k' else d+'/emudesk.inf','w').write('#R 01\r\n#E 1A E1\r\n#Q 41 40 43 40 43 40\r\n#Z 00 C:\\LAUNCH.PRG@\r\n#M 00 00 00 FF C Drive C@ @\r\n#T 00 01 03 FF   Trash@ @\r\n#F FF 07 @ *.*@\r\n#N FF 07 @ *.*@\r\n#D FF 02 @ *.*@\r\n#G 06 FF *.APP@ @ 000 @\r\n#G 06 FF *.PRG@ @ 000 @\r\n')
sock='/tmp/qd-%s.sock'%arch
if os.path.exists(sock): os.unlink(sock)
subprocess.run(['pkill','-9','-f','qemu-system-'+('m68k' if arch=='m68k' else 'arm')],stderr=subprocess.DEVNULL)
common=['-global','virtio-mmio.force-legacy=false','-drive','if=none,id=hd0,format=raw,file.driver=vvfat,file.dir='+d+',file.rw=on','-device','virtio-blk-device,drive=hd0','-device','virtio-gpu-device,id=gpu0','-device','virtio-keyboard-device','-device','virtio-tablet-device,id=tab0,display=gpu0','-display','none','-serial','file:'+B+'/qd-%s.out'%arch,'-qmp','unix:'+sock+',server,nowait']
if arch=='m68k': cmd=['qemu-system-m68k','-M','virt','-m','128','-cpu','m68020','-kernel',ELF[arch]]+common
else: cmd=['qemu-system-arm','-M','virt,highmem=off','-cpu','cortex-a7','-m','128','-kernel',ELF[arch]]+common
p=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
for i in range(50):
    if os.path.exists(sock): break
    time.sleep(0.2)
s=socket.socket(socket.AF_UNIX); s.connect(sock); f=s.makefile('rw'); f.readline()
def q(c):
    f.write(json.dumps(c)+'\n'); f.flush()
    while True:
        l=json.loads(f.readline())
        if 'return' in l or 'error' in l: return l
q({'execute':'qmp_capabilities'})

def ev(events): r=q({'execute':'input-send-event','arguments':{'device':'gpu0','events':events}}); print('ev',r) if 'error' in r else None
A=[640,400]; GP=[640,405]; first=[True]
def sendabs(ax,ay):
    ev([{'type':'abs','data':{'axis':'x','value':int(ax*32767/1279)}},{'type':'abs','data':{'axis':'y','value':int(ay*32767/799)}}]); time.sleep(0.15)
def move(x,y):
    # the guest driver turns abs events into relative moves: keep track of where the pointer is
    if first[0]:
        sendabs(A[0],A[1]); first[0]=False
    while GP[0]!=x or GP[1]!=y:
        dx=max(-A[0],min(1279-A[0],x-GP[0])); dy=max(-A[1],min(799-A[1],y-GP[1]))
        A[0]+=dx; A[1]+=dy; GP[0]+=dx; GP[1]+=dy; sendabs(A[0],A[1])
    time.sleep(0.3)
def btn(down,dt=0.15): ev([{'type':'btn','data':{'button':'left','down':down}}]); time.sleep(dt)
cur=[0,0]
def click(x,y,n=1):
    cur[0],cur[1]=x,y
    move(x,y)
    for i in range(n): btn(True,0.08 if n>1 else 0.15); btn(False,0.08 if n>1 else 0.15)
    time.sleep(0.5)
def keys(*k): q({'execute':'send-key','arguments':{'keys':[{'type':'qcode','data':x} for x in k],'hold-time':150}}); time.sleep(0.8)
def shot(name):
    ppm=B+'/%s.ppm'%name; q({'execute':'screendump','arguments':{'filename':ppm}}); time.sleep(0.5)
    subprocess.run(['sips','-s','format','png',ppm,'--out',B+'/%s.png'%name],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); os.unlink(ppm)
for a in acts:
    if a[0]=='w': time.sleep(float(a[1:]))
    elif a[0]=='s': shot(out+'-'+a[1:])
    elif a[0]=='c': x,y=a[1:].split(','); click(int(x),int(y))
    elif a[0]=='d': x,y=a[1:].split(','); click(int(x),int(y),2)
    elif a[0]=='k': keys(*a[1:].split('-'))
    elif a[0]=='D':
        x1,y1,x2,y2=[int(v) for v in a[1:].split(',')]; move(x1,y1); btn(True); time.sleep(0.3)
        for t in range(1,6): move(x1+(x2-x1)*t//5,y1+(y2-y1)*t//5)
        btn(False); time.sleep(0.6)
    elif a[0]=='P':
        x,y=[int(v) for v in a[1:].split(',')]; move(x,y); cur[0],cur[1]=x,y; btn(True,0.02); btn(False,0.02); btn(True,1.0)
    elif a[0]=='U': btn(False,0.5)
    elif a[0]=='R':
        x,y=[int(v) for v in a[1:].split(',')]; move(x,y); cur[0],cur[1]=x,y
        ev([{'type':'btn','data':{'button':'right','down':True}}]); time.sleep(0.4)
        ev([{'type':'btn','data':{'button':'right','down':False}}]); time.sleep(0.8)
    elif a[0]=='m': x,y=a[1:].split(','); cur[0],cur[1]=int(x),int(y); move(int(x),int(y))
    elif a[0]=='h': print(q({'execute':'human-monitor-command','arguments':{'command-line':a[1:]}}))
# quit closes the disk, which is when vvfat writes new files to the directory
q({'execute':'quit'})
try: p.wait(timeout=60)
except subprocess.TimeoutExpired: p.kill()
