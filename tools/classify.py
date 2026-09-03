# SPDX-License-Identifier: GPL-3.0-or-later
import omkpaths
import re,json,collections,sys
lines=open(omkpaths.require_decomp(),encoding="utf-8",errors="replace").read().split("\n")
M=re.compile(r'^//----- \(([0-9A-F]{8})\) -+$')
marks=[(i,M.match(l).group(1)) for i,l in enumerate(lines) if M.match(l)]
asms=json.load(open(omkpaths.clean("_asmstrings.json")))

RULES=[  # (module, regex over the function text)
 ("d3d",   r'\bIDirect3D|\bD3D|d3d|Direct3D|_D3DRM|Execute\w*Buffer'),
 ("ddraw", r'DirectDraw|IDirectDraw|lpDDS|\bDDSURFACEDESC|DDSCAPS|\bBlt\b|_DDSD'),
 ("dsound",r'DirectSound|IDirectSound|DSBUFFER|dsbd|lpDSB'),
 ("dinput",r'DirectInput|IDirectInput|DIDEVICE|dinput|joyGetPos'),
 ("mixer", r'\bmixer(Open|Close|GetDevCaps|GetLineInfo|GetLineControls|GetControlDetails|SetControlDetails)'),
 ("wave",  r'\bwaveOut\w+|\bwaveIn\w+|\bmmio\w+|WAVEFORMATEX|\bMMCKINFO'),
 ("mci",   r'mciSendCommandA|mciGetErrorStringA|MCI_'),
 ("video", r'AVIStream|AVIFile|ICDecompress|ICOpen|\bvids\b|smk|Bink'),
 ("net",   r'\bWSA\w+|socket|recvfrom|sendto|inet_addr|gethostby'),
 ("reg",   r'RegOpenKey|RegQueryValue|RegSetValue|RegCreateKey'),
 ("win32", r'\bWndProc|RegisterClass|CreateWindowEx|PeekMessage|DispatchMessage|ShowCursor|GetActiveWindow'),
 ("ole",   r'CoCreateInstance|CoInitialize|LoadTypeLib|IID_|\bIUnknown'),
 ("crt",   r'_\_report_gsfailure|_initterm|_controlfp|__set\w*|_onexit|HeapAlloc|__crt|GetStartupInfo|LC_ALL|setlocale|__lconv'),
]
NAMEPFX=[("script",r'^Script_'),("o3de",r'^o3de_'),("sys",r'^Sys_'),("mem",r'^Mem_')]

rec=json.load(open(omkpaths.clean("_recovered.json")))
IDENT=re.compile(r'\b[A-Za-z_]\w*\b')

ev={}
order=[]
for n,(ln,addr) in enumerate(marks):
    end=marks[n+1][0] if n+1<len(marks) else len(lines)
    txt="\n".join(lines[ln:end])
    # expand referenced string symbols into their text so strings count as evidence
    extra=[]
    for t in set(IDENT.findall(txt)):
        if t in asms: extra.append(asms[t])
    full=txt+"\n"+"\n".join(extra)
    hits=collections.Counter()
    for mod,pat in RULES:
        c=len(re.findall(pat,full))
        if c: hits[mod]+=c
    nm=rec.get(addr)
    if nm:
        for mod,pat in NAMEPFX:
            if re.match(pat,nm): hits[mod]+=50
    ev[addr]=hits
    order.append(addr)

labeled={a:h.most_common(1)[0][0] for a,h in ev.items() if h}
print(f"functions with direct evidence: {len(labeled)} / {len(order)}",file=sys.stderr)
print(collections.Counter(labeled.values()).most_common(),file=sys.stderr)
json.dump({"order":order,"labeled":labeled,
           "ev":{a:dict(h) for a,h in ev.items()}},open(omkpaths.clean("_evidence.json"),"w"))
