import numpy as np, wave, sys
sr=44100; bpm=112; beat=60/bpm; bars=24; dur=bars*4*beat
t=np.arange(int(sr*dur))/sr; L=np.zeros_like(t); R=np.zeros_like(t)
def midi(n): return 440*2**((n-69)/12)
prog=[[57,60,64,67],[53,57,60,64],[48,52,55,60],[55,59,62,65]]  # Am7 Fmaj7 C G7
def env(x,a,d): return np.minimum(x/a,1)*np.exp(-np.maximum(x-a,0)/d)
for bar in range(bars):
    ch=prog[bar%4]; t0=bar*4*beat; i0=int(t0*sr); n=int(4*beat*sr); tt=np.arange(n)/sr
    pad=sum(np.sin(2*np.pi*midi(m)*tt+0.3*np.sin(2*np.pi*0.3*tt))*0.05 for m in ch)*np.minimum(tt/0.4,1)
    L[i0:i0+n]+=pad*0.9; R[i0:i0+n]+=pad*1.1
    for b in range(8):  # bass eighths
        s=int((t0+b*beat/2)*sr); k=int(beat/2*sr); x=np.arange(k)/sr
        bs=np.tanh(2*np.sin(2*np.pi*midi(ch[0]-24)*x))*env(x,0.005,0.15)*0.22
        L[s:s+k]+=bs; R[s:s+k]+=bs
    for b in range(16):  # arpeggio, panned
        s=int((t0+b*beat/4)*sr); k=int(beat/4*sr); x=np.arange(k)/sr
        m=ch[[0,1,2,3,2,1,3,2][b%8]]+12
        a=(np.sign(np.sin(2*np.pi*midi(m)*x))*0.3+np.sin(2*np.pi*midi(m)*x))*env(x,0.003,0.08)*0.06
        p=0.5+0.4*np.sin(bar+b*0.7); L[s:s+k]+=a*(1-p)*2; R[s:s+k]+=a*p*2
    for b in range(4):  # kick + snare
        s=int((t0+b*beat)*sr); k=int(0.35*sr); x=np.arange(k)/sr
        kick=np.sin(2*np.pi*(50+90*np.exp(-x*30))*x)*np.exp(-x*9)*0.5
        L[s:s+k]+=kick; R[s:s+k]+=kick
        if b%2==1:
            sn=np.random.randn(k)*np.exp(-x*18)*0.15; L[s:s+k]+=sn; R[s:s+k]+=sn
    for b in range(8):  # hats
        s=int((t0+b*beat/2+beat/4)*sr); k=int(0.05*sr)
        h=np.diff(np.random.randn(k+1))*np.exp(-np.arange(k)/sr*80)*0.05; L[s:s+k]+=h*0.7; R[s:s+k]+=h
fade=np.minimum(1,np.minimum(t/1.0,(dur-t)/2.0)); L*=fade; R*=fade
peak=max(abs(L).max(),abs(R).max()); L/=peak/0.8; R/=peak/0.8
data=(np.stack([L,R],1)*32767).astype('<i2')
w=wave.open(sys.argv[1],'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr); w.writeframes(data.tobytes()); w.close()
print(f"{dur:.1f}s")
