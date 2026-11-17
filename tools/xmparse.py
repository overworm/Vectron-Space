"""xmparse.py - minimal FastTracker 2 .xm reader (patterns, instruments, 8/16-bit samples decoded to float)"""
import struct, numpy as np
def parse(path):
    d=open(path,'rb').read()
    hs,=struct.unpack('<I',d[60:64])
    songlen,restart,nch,npat,ninst,flags,tempo,bpm=struct.unpack('<8H',d[64:80])
    orders=list(d[80:80+songlen])
    off=60+hs
    pats=[]
    for p in range(npat):
        hl,pt,rows,ps=struct.unpack('<IBHH',d[off:off+9])
        data=d[off+hl:off+hl+ps]; off+=hl+ps
        grid=[[None]*nch for _ in range(rows)]
        i=0
        if ps:
            for r in range(rows):
                for c in range(nch):
                    b=data[i]; i+=1
                    note=inst=vol=eff=par=0
                    if b&0x80:
                        if b&1: note=data[i]; i+=1
                        if b&2: inst=data[i]; i+=1
                        if b&4: vol=data[i]; i+=1
                        if b&8: eff=data[i]; i+=1
                        if b&16: par=data[i]; i+=1
                    else:
                        note=b; inst=data[i]; vol=data[i+1]; eff=data[i+2]; par=data[i+3]; i+=4
                    grid[r][c]=(note,inst,vol,eff,par)
        pats.append(grid)
    insts=[]
    for k in range(ninst):
        sz,=struct.unpack('<I',d[off:off+4]); name=d[off+4:off+26].rstrip(b'\0').decode('latin1'); ns,=struct.unpack('<H',d[off+27:off+29])
        info={'name':name,'samples':[],'map':[0]*96}
        if ns:
            sh,=struct.unpack('<I',d[off+29:off+33])
            info['map']=list(d[off+33:off+129])
            volenv=list(struct.unpack('<24H',d[off+129:off+177])); 
            nvp=d[off+225]; vtype=d[off+233]; vsus=d[off+227]; vls=d[off+228]; vle=d[off+229]
            info['volenv']=(volenv[:nvp*2],vtype,vsus,vls,vle)
            info['fadeout']=struct.unpack('<H',d[off+239:off+241])[0]
            o=off+sz
            hdrs=[]
            for s in range(ns):
                ln,ls,ll=struct.unpack('<III',d[o:o+12]); vol,fin,typ,pan,rel,res=struct.unpack('<BbBBbB',d[o:o+18][12:18]); nm=d[o+18:o+40].rstrip(b'\0').decode('latin1')
                hdrs.append((ln,ls,ll,vol,fin,typ,pan,rel,nm)); o+=sh if sh else 40
            for h in hdrs:
                ln,ls,ll,vol,fin,typ,pan,rel,nm=h
                raw=d[o:o+ln]; o+=ln
                if typ&16:
                    x=np.frombuffer(raw[:ln//2*2],dtype='<i2').astype(float)
                    x=np.cumsum(x).astype(np.int64); x=((x+32768)%65536-32768).astype(float)/32768
                    ls//=2; ll//=2
                else:
                    x=np.frombuffer(raw,dtype=np.int8).astype(float); x=np.cumsum(x).astype(np.int64); x=((x+128)%256-128).astype(float)/128
                info['samples'].append(dict(data=x,ls=ls,ll=ll,vol=vol,fin=fin,typ=typ&3,pan=pan,rel=rel,name=nm))
            off=o
        else: off+=sz
        insts.append(info)
    return dict(orders=orders,nch=nch,pats=pats,insts=insts,tempo=tempo,bpm=bpm,flags=flags)
