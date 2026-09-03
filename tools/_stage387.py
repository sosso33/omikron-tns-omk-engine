# SPDX-License-Identifier: GPL-3.0-or-later
import sys, os, math, struct
ROOT='/Users/sofianekerrakchou/Documents/omk'
OUT='/private/tmp/claude-501/-Users-sofianekerrakchou-Documents-omk/347e86c4-20b2-4859-97e8-5201710654ad/scratchpad'
sys.path.insert(0, ROOT+'/tools'); os.chdir(ROOT)
import omkdata as O, camshot, tex3dt
from PIL import Image, ImageDraw

DIALOG, SET = 387, 'AResto14'

def avg_colors(path):
    try: txs = tex3dt.textures(path)
    except Exception: txs = []
    out={}
    for i,t in enumerate(txs):
        rgb=t['rgb']; n=len(rgb)//3
        if not n: out[i]=(150,150,150); continue
        step=max(1,n//2000); r=g=b=c=0
        for k in range(0,n,step):
            r+=rgb[3*k]; g+=rgb[3*k+1]; b+=rgb[3*k+2]; c+=1
        out[i]=(r//c,g//c,b//c)
    return out

def qrot(q,v):
    w,x,y,z=q; vx,vy,vz=v
    tx=2*(y*vz-z*vy); ty=2*(z*vx-x*vz); tz=2*(x*vy-y*vx)
    return (vx+w*tx+(y*tz-z*ty), vy+w*ty+(z*tx-x*tz), vz+w*tz+(x*ty-y*tx))

def pelvis_and_root(model, scx, anim):
    import anim_3da, mesh3do
    st=anim_3da.scx_stream(os.path.join('gamedata/SCPTDATA',scx)); a=st['anims'][anim]
    r=anim_3da.descriptor(st['data'],a['offset'],a['declared'])
    key0=anim_3da.positions(st['data'],a['offset'],r['tracks'][0])[0]
    h,ms=mesh3do.meshes(os.path.join(O.PERSOS,model+'.3DO'))
    bi=next(i for i,m in enumerate(ms) if 'bassin' in m['name'].lower())
    return key0, bi

def character(model, scx, anim, yaw_deg, world):
    """-> (triangles, feet) posed exactly as the viewer poses it."""
    meta, blob = O.ani_pose_stream(model, scx, anim)
    F = max(1, meta['frames']//2)
    per = meta['meshes']*7
    vals = struct.unpack_from('<%df'%per, blob, F*per*4)
    pose = [vals[i*7:(i+1)*7] for i in range(meta['meshes'])]
    feet = meta.get('feet', 64.0)
    g = O.model_geometry(model)
    ax = g.get('bodyAxis',[0,0])
    fo = g.get('faceOffset',[0,0,0])
    pts=[]
    for c in g['corners']:
        if c[0]==1:
            b=(g['faceBind'] and g['faceBind'][c[1]]) or [0,0,0]
            lx,ly,lz=b[0]+fo[0],b[1]+fo[1],b[2]+fo[2]; mi=g['faceMeshIndex']
        else:
            L=g.get('staticLocal') and g['staticLocal'][c[1]]
            if L is None:
                v=g['static'][c[1]]; pts.append((v[0],v[1],v[2])); continue
            lx,ly,lz=L[0],L[1],L[2]; mi=g['staticMesh'][c[1]]
        q=pose[mi][:4]; off=pose[mi][4:]
        r=qrot(q,(lx,ly,lz))
        pts.append((r[0]+off[0], r[1]+off[1], r[2]+off[2]))
    key0, bi = pelvis_and_root(model, scx, anim)
    pel = pose[bi][4:]                     # the posed pelvis, model space
    th=math.radians(yaw_deg); cs,sn=math.cos(th),math.sin(th)
    wp=[]
    for (x,y,z) in pts:
        x-=ax[0]; z-=ax[1]
        # ENGINE RECIPE: the clip root's key 0 IS the pelvis's world position
        # (Anim_SnapRootToStart copies it onto the node) - no floor probe,
        # no feet heuristic.
        wp.append((key0[0]+x*cs+z*sn, key0[1]+(y-pel[1]), key0[2]-x*sn+z*cs))
    cols=avg_colors(os.path.join(O.PERSOS, model+'.3DO'))
    tris=[]
    for b in g['batches']:
        col=cols.get(b['material'],(170,150,140))
        for t in range(b['start'], b['start']+b['count'], 3):
            tris.append(([wp[t],wp[t+1],wp[t+2]], col))
    return tris, feet

# ---- placement, straight from the server's own resolution -----------------
sp  = O.speaker_positions(O.conversation(DIALOG), SET)
sin = O.scene_idle(DIALOG); sip = O.scene_idle(DIALOG, player=True)
act = O.dialog_actor(DIALOG)
print('npc   ', [round(v,1) for v in sp['npc']],    sp['npcSource'],    sin['clip'], sin['rootYaw'])
print('player', [round(v,1) for v in sp['player']], sp['playerSource'], sip['clip'], sip['rootYaw'])

ctris, nfeet = character(act['model'], sin['scx'], sin['anim'], sin['rootYaw'], sp['npc'])
ptris, pfeet = character('HO1_FNM',    sip['scx'], sip['anim'], sip['rootYaw'], sp['player'])
print('feet: npc %.1f  player %.1f' % (nfeet, pfeet))

geo = O.decor_geometry_cached(SET); V = geo['verts']
dcols = camshot._avg_colors(SET)

def draw(eye, at, fov, path, topdown=False, marks=(), cull_above=None):
    if topdown:                       # camshot's up is degenerate looking down
        f=[at[i]-eye[i] for i in range(3)]
        l=math.hypot(*f) or 1; f=[v/l for v in f]
        up=[0,0,1]
        cr=lambda a,b:[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
        un=lambda a:[v/(math.hypot(*a) or 1) for v in a]
        s=un(cr(f,up)); u=cr(s,f)
        W,H=camshot.W,camshot.H; tanh=math.tan(math.radians(fov)/2); tanv=tanh/(W/H)
        def pr(p):
            d=[p[i]-eye[i] for i in range(3)]
            z=sum(d[i]*f[i] for i in range(3))
            if z<=1: return None
            x=sum(d[i]*s[i] for i in range(3)); y=sum(d[i]*u[i] for i in range(3))
            return (W*0.5*(1+(x/z)/tanh), H*0.5*(1-(y/z)/tanv), z)
    else:
        pr = camshot.projector(eye, at, fov)
    img=Image.new('RGB',(camshot.W,camshot.H),(0,0,0)); dr=ImageDraw.Draw(img)
    tris=[]
    for b in geo['batches']:
        col=dcols.get(b['material'],(120,120,120))
        for t in range(b['start'], b['start']+b['count'], 3):
            if cull_above is not None and all(V[t+k][1] < cull_above for k in range(3)):
                continue                      # game Y grows down: skip the ceiling
            P=[pr(V[t+k]) for k in range(3)]
            if any(p is None for p in P): continue
            lit=0.25+0.75*sum(V[t+k][5] for k in range(3))/3.0
            tris.append((max(p[2] for p in P),[(p[0],p[1]) for p in P],
                         tuple(int(v*lit) for v in col)))
    for group,tint in ((ctris,1.0),(ptris,1.0)):
        for pts3,col in group:
            P=[pr(p) for p in pts3]
            if any(p is None for p in P): continue
            tris.append((max(p[2] for p in P),[(p[0],p[1]) for p in P],
                         tuple(min(255,int(v*tint)) for v in col)))
    tris.sort(key=lambda t:-t[0])
    for _,poly,col in tris: dr.polygon(poly, fill=col)
    for p,col,label in marks:
        q=pr(p)
        if q: dr.ellipse([q[0]-6,q[1]-6,q[0]+6,q[1]+6], outline=col, width=2); dr.text((q[0]+9,q[1]-6), label, fill=col)
    img.save(path); print('wrote', path)

conv, cam = camshot.line_camera(DIALOG, 0)
eye = cam['pos'][0:3]; at = cam['pos'][3:6]; fov = float(cam['angle'][1])
print('line camera %d: eye %s at %s fov %.1f' % (cam['id'], eye, at, fov))
draw(eye, at, fov, OUT+'/stage387_camera.png')

mid=[(sp['npc'][i]+sp['player'][i])/2 for i in range(3)]
MK=[([sp['npc'][0],15.9,sp['npc'][2]],(255,80,80),'Telis'),
    ([sp['player'][0],15.9,sp['player'][2]],(80,160,255),"Kay'l")]
draw([mid[0], mid[1]-300, mid[2]+1], [mid[0],mid[1],mid[2]], 45,
     OUT+'/stage387_topdown.png', topdown=True, marks=MK, cull_above=mid[1]-90)

# a wide eye-level shot from across the table, looking at the midpoint
import math as _m
for name,(ex,ez) in (('wideA',(mid[0]+150, mid[2]+150)), ('wideB',(mid[0]-150, mid[2]+150))):
    draw([ex, mid[1]-40, ez], [mid[0], mid[1]-25, mid[2]], 55,
         OUT+'/stage387_%s.png'%name, marks=MK, cull_above=mid[1]-95)
