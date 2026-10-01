const fs=require('fs'),crypto=require('crypto');
let src=fs.readFileSync(''+__dirname+'/../resources/ui/host-bridge.js','utf8');
const a=src.indexOf('var Sec = (function'), b=src.indexOf('  /* ---------- состояние');
const code="var SECRET_KEY='"+crypto.randomBytes(32).toString('hex')+"';\n"+src.slice(a,b)+"\nreturn Sec;";
const Sec=new Function('crypto','TextEncoder','TextDecoder','btoa','atob',code)(crypto.webcrypto,TextEncoder,TextDecoder,btoa,atob);
// sha256
for (const n of [0,1,3,55,56,63,64,65,119,120,1000]) {
  const m=crypto.randomBytes(n);
  if (Buffer.from(Sec.sha256(m)).toString('hex')!==crypto.createHash('sha256').update(m).digest('hex')) throw new Error('sha '+n);
}
// hmac
for (const kl of [5,32,64,100]) { const k=crypto.randomBytes(kl), m=crypto.randomBytes(77);
  if (Buffer.from(Sec.hmac(k,m)).toString('hex')!==crypto.createHmac('sha256',k).update(m).digest('hex')) throw new Error('hmac '+kl); }
// chacha20 vs node (node chacha20: 16-byte iv = counter(4 LE)+nonce(12))
for (const n of [1,63,64,65,200,1000]) { const k=crypto.randomBytes(32), nc=crypto.randomBytes(12), d=crypto.randomBytes(n);
  const iv=Buffer.concat([Buffer.from([1,0,0,0]),nc]);
  const c=crypto.createCipheriv('chacha20',k,iv); const ref=Buffer.concat([c.update(d),c.final()]);
  if (Buffer.from(Sec.chacha20(k,nc,d)).toString('hex')!==ref.toString('hex')) throw new Error('chacha '+n); }
// roundtrip
for (const t of ['', 'abc', 'Пароль 🔐 '+'x'.repeat(500), JSON.stringify([{u:'a',p:'b'}])]) {
  const e=Sec.enc(t); if(!e.startsWith('ss1:')) throw new Error('prefix'); if (Sec.dec(e)!==t) throw new Error('rt');
  const bad=e.slice(0,-3)+(e.slice(-3)==='AAA'?'BBB':'AAA'); if (Sec.dec(bad)!=='') throw new Error('tamper');
}
console.log('secret tests OK');
