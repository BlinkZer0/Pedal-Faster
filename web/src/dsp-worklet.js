// Pedal Faster web preview - AudioWorklet DSP.
// A JavaScript port of src/dsp (same algorithms, same parameter order as the C++ EffectRegistry).
// Runs inside the AudioWorkletGlobalScope; the main thread posts the effective rig state.

const PI = Math.PI;
const dbToGain = (db) => Math.pow(10, db * 0.05);
const gainToDb = (g) => 20 * Math.log10(Math.max(g, 1e-9));
const clamp = (x, lo, hi) => (x < lo ? lo : x > hi ? hi : x);
const lerp = (a, b, t) => a + (b - a) * t;
const expMap = (t, lo, hi) => lo * Math.pow(hi / lo, clamp(t, 0, 1));
const undenormal = (x) => (Math.abs(x) < 1e-20 ? 0 : x);

function fastTanh(x) {
  if (x > 4.97) return 1;
  if (x < -4.97) return -1;
  const x2 = x * x;
  const a = x * (135135 + x2 * (17325 + x2 * (378 + x2)));
  const b = 135135 + x2 * (62370 + x2 * (3150 + x2 * 28));
  return clamp(a / b, -1, 1);
}
function logCosh(x) { const ax = Math.abs(x); return ax + Math.log1p(Math.exp(-2 * ax)) - 0.6931471805599453; }

const SYNC_BEATS = [0, 4, 2, 3, 1, 1.5, 2 / 3, 0.5, 0.75, 1 / 3, 0.25, 1 / 6, 0.125];
const syncBeats = (i) => (i > 0 && i < 13 ? SYNC_BEATS[i] : 0);

// ============================================================================ primitives
class Smoother {
  constructor() { this.current = 0; this.target = 0; this.coeff = 1; }
  prepare(sr, ms) { this.coeff = 1 - Math.exp(-1 / Math.max(1, ms * 0.001 * sr)); }
  setTarget(t) { this.target = t; }
  snap() { this.current = this.target; }
  snapTo(v) { this.current = this.target = v; }
  next() { this.current += (this.target - this.current) * this.coeff; return this.current; }
}

class Biquad {
  constructor() { this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0; this.s1 = [0, 0]; this.s2 = [0, 0]; }
  reset() { this.s1[0] = this.s1[1] = this.s2[0] = this.s2[1] = 0; }
  copyCoeffs(o) { this.b0 = o.b0; this.b1 = o.b1; this.b2 = o.b2; this.a1 = o.a1; this.a2 = o.a2; }
  process(x, ch) {
    const y = this.b0 * x + this.s1[ch];
    this.s1[ch] = undenormal(this.b1 * x - this.a1 * y + this.s2[ch]);
    this.s2[ch] = undenormal(this.b2 * x - this.a2 * y);
    return y;
  }
  set(B0, B1, B2, A0, A1, A2) { const i = 1 / A0; this.b0 = B0 * i; this.b1 = B1 * i; this.b2 = B2 * i; this.a1 = A1 * i; this.a2 = A2 * i; }
  static w0(f, sr) { return 2 * PI * clamp(f, 5, sr * 0.49) / sr; }
  lowpass(sr, f, q) { const w = Biquad.w0(f, sr), c = Math.cos(w), al = Math.sin(w) / (2 * q); this.set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
  highpass(sr, f, q) { const w = Biquad.w0(f, sr), c = Math.cos(w), al = Math.sin(w) / (2 * q); this.set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
  bandpass(sr, f, q) { const w = Biquad.w0(f, sr), c = Math.cos(w), al = Math.sin(w) / (2 * q); this.set(al, 0, -al, 1 + al, -2 * c, 1 - al); }
  peak(sr, f, q, db) {
    const A = Math.pow(10, db / 40), w = Biquad.w0(f, sr), c = Math.cos(w), al = Math.sin(w) / (2 * q);
    this.set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
  }
  lowShelf(sr, f, q, db) {
    const A = Math.pow(10, db / 40), w = Biquad.w0(f, sr), c = Math.cos(w), s = Math.sin(w), al = s / (2 * q), sq = 2 * Math.sqrt(A) * al;
    this.set(A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
      (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
  }
  highShelf(sr, f, q, db) {
    const A = Math.pow(10, db / 40), w = Biquad.w0(f, sr), c = Math.cos(w), s = Math.sin(w), al = s / (2 * q), sq = 2 * Math.sqrt(A) * al;
    this.set(A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
      (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
  }
}

class SVF {
  constructor() { this.g = 0; this.k = 1; this.a1 = 0; this.a2 = 0; this.a3 = 0; this.ic1 = [0, 0]; this.ic2 = [0, 0]; this.lp = 0; this.bp = 0; this.hp = 0; }
  reset() { this.ic1[0] = this.ic1[1] = this.ic2[0] = this.ic2[1] = 0; }
  set(sr, f, q) {
    f = clamp(f, 10, sr * 0.45);
    this.g = Math.tan(PI * f / sr); this.k = 1 / Math.max(0.05, q);
    this.a1 = 1 / (1 + this.g * (this.g + this.k)); this.a2 = this.g * this.a1; this.a3 = this.g * this.a2;
  }
  tick(x, ch) {
    const v3 = x - this.ic2[ch];
    const v1 = this.a1 * this.ic1[ch] + this.a2 * v3;
    const v2 = this.ic2[ch] + this.a2 * this.ic1[ch] + this.a3 * v3;
    this.ic1[ch] = undenormal(2 * v1 - this.ic1[ch]);
    this.ic2[ch] = undenormal(2 * v2 - this.ic2[ch]);
    this.lp = v2; this.bp = v1; this.hp = x - this.k * v1 - v2;
  }
}

class OnePole {
  constructor() { this.a = 0; this.z = [0, 0]; }
  reset() { this.z[0] = this.z[1] = 0; }
  setCutoff(sr, f) { this.a = Math.exp(-2 * PI * clamp(f, 1, sr * 0.49) / sr); }
  lowpass(x, ch) { this.z[ch] = undenormal(x + this.a * (this.z[ch] - x)); return this.z[ch]; }
  highpass(x, ch) { return x - this.lowpass(x, ch); }
}

class DCBlocker {
  constructor() { this.R = 0.995; this.x1 = [0, 0]; this.y1 = [0, 0]; }
  prepare(sr) { this.R = Math.exp(-2 * PI * 12 / sr); }
  reset() { this.x1[0] = this.x1[1] = this.y1[0] = this.y1[1] = 0; }
  process(x, ch) { const y = x - this.x1[ch] + this.R * this.y1[ch]; this.x1[ch] = x; this.y1[ch] = undenormal(y); return y; }
}

class DelayLine {
  constructor() { this.buf = new Float32Array(8); this.mask = 7; this.w = 0; }
  setMaxDelay(samples) { let s = 1; while (s < samples + 4) s <<= 1; this.buf = new Float32Array(s); this.mask = s - 1; this.w = 0; }
  reset() { this.buf.fill(0); this.w = 0; }
  capacity() { return this.mask - 3; }
  push(x) { this.buf[this.w] = x; this.w = (this.w + 1) & this.mask; }
  readInt(d) { return this.buf[(this.w - d) & this.mask]; }
  readLinear(d) {
    d = clamp(d, 1, this.capacity());
    const i = Math.floor(d), f = d - i, a = this.readInt(i), b = this.readInt(i + 1);
    return a + (b - a) * f;
  }
  readHermite(d) {
    d = clamp(d, 2, this.capacity() - 2);
    const i = Math.floor(d), f = d - i;
    const xm1 = this.readInt(i - 1), x0 = this.readInt(i), x1 = this.readInt(i + 1), x2 = this.readInt(i + 2);
    const c1 = 0.5 * (x1 - xm1), c2 = xm1 - 2.5 * x0 + 2 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
  }
}

class DiffuserAllpass {
  constructor() { this.line = new DelayLine(); this.delay = 100; this.gain = 0.6; }
  setMax(n) { this.line.setMaxDelay(n); }
  reset() { this.line.reset(); }
  process(x, mod = 0) {
    const d = mod === 0 ? this.line.readInt(this.delay | 0) : this.line.readLinear(this.delay + mod);
    const v = x + this.gain * d;
    this.line.push(undenormal(v));
    return d - this.gain * v;
  }
}

class FirstOrderAllpass {
  constructor() { this.z = [0, 0]; }
  reset() { this.z[0] = this.z[1] = 0; }
  process(x, a, ch) { const y = a * x + this.z[ch]; this.z[ch] = undenormal(x - a * y); return y; }
}

const SHAPE = { Sine: 0, Triangle: 1, Square: 2, SawDown: 3, SawUp: 4 };
class Lfo {
  constructor() { this.phase = 0; }
  advance(inc) { this.phase += inc; if (this.phase >= 1) this.phase -= Math.floor(this.phase); }
  static shape(p, s) {
    p -= Math.floor(p);
    switch (s) {
      case 0: return Math.sin(p * 2 * PI);
      case 1: return p < 0.5 ? 4 * p - 1 : 3 - 4 * p;
      case 2: return fastTanh(Math.sin(p * 2 * PI) * 6);
      case 3: return 1 - 2 * p;
      case 4: return 2 * p - 1;
    }
    return 0;
  }
  value(s, off = 0) { return Lfo.shape(this.phase + off, s); }
}

class EnvelopeFollower {
  constructor() { this.att = 0; this.rel = 0; this.env = 0; }
  set(sr, aMs, rMs) { this.att = Math.exp(-1 / (Math.max(0.01, aMs) * 0.001 * sr)); this.rel = Math.exp(-1 / (Math.max(0.01, rMs) * 0.001 * sr)); }
  reset() { this.env = 0; }
  process(x) { const a = Math.abs(x); const c = a > this.env ? this.att : this.rel; this.env = undenormal(a + c * (this.env - a)); return this.env; }
}

class AdaaTanh {
  constructor() { this.x1 = [0, 0]; this.F1 = [0, 0]; this.bias = 0; this.biasOut = 0; this.enabled = true; }
  setBias(b) { this.bias = b; this.biasOut = Math.tanh(b); }
  reset() { this.x1[0] = this.x1[1] = this.bias; this.F1[0] = this.F1[1] = logCosh(this.bias); }
  process(xIn, ch) {
    const x = xIn + this.bias;
    if (!this.enabled) { this.x1[ch] = x; return Math.tanh(x) - this.biasOut; }
    const F = logCosh(x), dx = x - this.x1[ch];
    const y = Math.abs(dx) < 1e-5 ? Math.tanh(0.5 * (x + this.x1[ch])) : (F - this.F1[ch]) / dx;
    this.x1[ch] = x; this.F1[ch] = F;
    return y - this.biasOut;
  }
}

class AdaaHardClip {
  constructor() { this.x1 = [0, 0]; this.F1 = [0, 0]; this.bias = 0; this.biasOut = 0; this.enabled = true; }
  static clip(u) { return clamp(u, -1, 1); }
  static F(u) { return Math.abs(u) <= 1 ? 0.5 * u * u : Math.abs(u) - 0.5; }
  setBias(b) { this.bias = b; this.biasOut = AdaaHardClip.clip(b); }
  reset() { this.x1[0] = this.x1[1] = this.bias; this.F1[0] = this.F1[1] = AdaaHardClip.F(this.bias); }
  process(xIn, ch) {
    const x = xIn + this.bias;
    if (!this.enabled) { this.x1[ch] = x; return AdaaHardClip.clip(x) - this.biasOut; }
    const Fx = AdaaHardClip.F(x), dx = x - this.x1[ch];
    const y = Math.abs(dx) < 1e-5 ? AdaaHardClip.clip(0.5 * (x + this.x1[ch])) : (Fx - this.F1[ch]) / dx;
    this.x1[ch] = x; this.F1[ch] = Fx;
    return y - this.biasOut;
  }
}

class Rand {
  constructor() { this.s = 0x9e3779b9 | 0; }
  bipolar() { let s = this.s; s ^= s << 13; s ^= s >>> 17; s ^= s << 5; this.s = s | 0; return (this.s >>> 0) / 2147483648 - 1; }
}

// ============================================================================ effect base
class Effect {
  prepare(sr, maxBlock) { this.sr = sr; this.maxBlock = maxBlock; this.onPrepare(); }
  onPrepare() {}
  setParameter() {}
  reset() {}
  process() {}
  hasTail() { return false; }
}

// ---------------------------------------------------------------- dynamics
class NoiseGate extends Effect {
  constructor() { super(); this.p = [-55, 1, 40, 120, 6, -80]; this.gain = 0; this.open = false; this.hold = 0; this.det = new EnvelopeFollower(); this.sr = 48000; this.upd(); }
  setParameter(i, v) { this.p[i] = v; this.upd(); }
  upd() {
    const sr = this.sr;
    this.ac = Math.exp(-1 / (Math.max(0.05, this.p[1]) * 0.001 * sr));
    this.rc = Math.exp(-1 / (Math.max(1, this.p[3]) * 0.001 * sr));
    this.holdS = (this.p[2] * 0.001 * sr) | 0; this.floor = dbToGain(this.p[5]);
  }
  onPrepare() { this.upd(); this.det.set(this.sr, 0.2, 25); this.reset(); }
  reset() { this.det.reset(); this.gain = this.open ? 1 : this.floor; this.hold = 0; }
  process(L, R, n) {
    const openL = dbToGain(this.p[0]), closeL = dbToGain(this.p[0] - this.p[4]);
    for (let i = 0; i < n; i++) {
      const env = this.det.process(Math.max(Math.abs(L[i]), Math.abs(R[i])));
      if (env >= openL) { this.open = true; this.hold = this.holdS; }
      else if (env < closeL) { if (this.hold > 0) this.hold--; else this.open = false; }
      else if (this.open && this.hold > 0) this.hold--;
      const t = this.open ? 1 : this.floor, c = t > this.gain ? this.ac : this.rc;
      this.gain = t + c * (this.gain - t);
      L[i] *= this.gain; R[i] *= this.gain;
    }
  }
}

class Compressor extends Effect {
  constructor() { super(); this.p = [-20, 4, 10, 150, 4, 6]; this.gr = 0; this.makeup = new Smoother(); this.sr = 48000; this.upd(); }
  setParameter(i, v) { this.p[i] = v; if (i === 4) this.makeup.setTarget(dbToGain(v)); this.upd(); }
  upd() { this.ac = Math.exp(-1 / (Math.max(0.05, this.p[2]) * 0.001 * this.sr)); this.rc = Math.exp(-1 / (Math.max(1, this.p[3]) * 0.001 * this.sr)); }
  onPrepare() { this.makeup.prepare(this.sr, 30); this.upd(); }
  reset() { this.gr = 0; this.makeup.snap(); }
  process(L, R, n) {
    const [th, ratio, , , , knee] = this.p, slope = 1 - 1 / Math.max(1, ratio);
    for (let i = 0; i < n; i++) {
      const over = gainToDb(Math.max(Math.abs(L[i]), Math.abs(R[i]))) - th;
      let t;
      if (knee > 0 && Math.abs(over) <= knee * 0.5) { const k = over + knee * 0.5; t = slope * k * k / (2 * knee); }
      else t = over > 0 ? slope * over : 0;
      const c = t > this.gr ? this.ac : this.rc;
      this.gr = t + c * (this.gr - t);
      const g = dbToGain(-this.gr) * this.makeup.next();
      L[i] *= g; R[i] *= g;
    }
  }
}

// ---------------------------------------------------------------- gain
class GainBase extends Effect {
  constructor() { super(); this.dirty = true; this.lastQ = -1; }
  quality(ctx, ...sh) { if (ctx.quality !== this.lastQ) { this.lastQ = ctx.quality; for (const s of sh) s.enabled = ctx.quality > 0; } }
}

class CleanBoost extends GainBase {
  constructor() { super(); this.gain = new Smoother(); this.out = new Smoother(); this.tone = 0.5; this.sh = new AdaaTanh(); this.lo = new Biquad(); this.hi = new Biquad(); }
  setParameter(i, v) { if (i === 0) this.gain.setTarget(dbToGain(v)); if (i === 1) { this.tone = v; this.dirty = true; } if (i === 2) this.out.setTarget(dbToGain(v)); }
  onPrepare() { this.gain.prepare(this.sr, 20); this.out.prepare(this.sr, 20); }
  reset() { this.gain.snap(); this.out.snap(); this.sh.setBias(0.08); this.sh.reset(); this.lo.reset(); this.hi.reset(); }
  process(L, R, n, ctx) {
    this.quality(ctx, this.sh);
    if (this.dirty) { this.dirty = false; this.lo.lowShelf(this.sr, 300, 0.7, -(this.tone - 0.5) * 12); this.hi.highShelf(this.sr, 2200, 0.7, (this.tone - 0.5) * 12); }
    for (let i = 0; i < n; i++) {
      const g = this.gain.next(), o = this.out.next();
      for (let ch = 0; ch < 2; ch++) {
        const d = ch ? R : L;
        let y = 2 * this.sh.process(0.5 * d[i] * g, ch);
        d[i] = this.hi.process(this.lo.process(y, ch), ch) * o;
      }
    }
  }
}

class TransparentOD extends GainBase {
  constructor() { super(); this.drive = new Smoother(); this.level = new Smoother(); this.bass = 0.5; this.treble = 0.5; this.clip = new AdaaHardClip(); this.hp = new Biquad(); this.lo = new Biquad(); this.hi = new Biquad(); }
  setParameter(i, v) { if (i === 0) this.drive.setTarget(v); if (i === 1) { this.bass = v; this.dirty = true; } if (i === 2) { this.treble = v; this.dirty = true; } if (i === 3) this.level.setTarget(dbToGain(-24 + 36 * v)); }
  onPrepare() { this.drive.prepare(this.sr, 25); this.level.prepare(this.sr, 25); }
  reset() { this.drive.snap(); this.level.snap(); this.clip.reset(); this.hp.reset(); this.lo.reset(); this.hi.reset(); }
  process(L, R, n, ctx) {
    this.quality(ctx, this.clip);
    if (this.dirty) { this.dirty = false; this.hp.highpass(this.sr, 140, 0.6); this.lo.lowShelf(this.sr, 120, 0.7, (this.bass - 0.5) * 24); this.hi.highShelf(this.sr, 2600, 0.7, (this.treble - 0.5) * 24); }
    for (let i = 0; i < n; i++) {
      const d = this.drive.next(), lv = this.level.next(), g = 1 + 90 * d * d, ca = 1 - 0.75 * d, cl = 0.35 + 0.4 * Math.sqrt(d);
      for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L, x = s[i];
        const c = this.clip.process(this.hp.process(x, ch) * g * 0.8, ch);
        s[i] = this.hi.process(this.lo.process(x * ca + c * cl, ch), ch) * lv;
      }
    }
  }
}

class MidOD extends GainBase {
  constructor() { super(); this.drive = new Smoother(); this.level = new Smoother(); this.tone = 0.5; this.mid = 0.5; this.clip = new AdaaTanh(); this.hp = new Biquad(); this.pk = new Biquad(); this.t1 = new Biquad(); this.t2 = new Biquad(); }
  setParameter(i, v) { if (i === 0) this.drive.setTarget(v); if (i === 1) { this.tone = v; this.dirty = true; } if (i === 2) { this.mid = v; this.dirty = true; } if (i === 3) this.level.setTarget(dbToGain(-24 + 36 * v)); }
  onPrepare() { this.drive.prepare(this.sr, 25); this.level.prepare(this.sr, 25); this.clip.setBias(0.05); }
  reset() { this.drive.snap(); this.level.snap(); this.clip.reset(); this.hp.reset(); this.pk.reset(); this.t1.reset(); this.t2.reset(); }
  process(L, R, n, ctx) {
    this.quality(ctx, this.clip);
    if (this.dirty) {
      this.dirty = false; this.hp.highpass(this.sr, 720, 0.55); this.pk.peak(this.sr, 720, 0.8, this.mid * 10);
      const f = expMap(this.tone, 900, 7500); this.t1.lowpass(this.sr, f, 0.6); this.t2.lowpass(this.sr, Math.min(f * 2, 18000), 0.6);
    }
    for (let i = 0; i < n; i++) {
      const d = this.drive.next(), lv = this.level.next(), g = 2 + 140 * d * d;
      for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L, x = s[i];
        const br = this.hp.process(this.pk.process(x, ch), ch);
        const y = 0.5 * x + 0.55 * this.clip.process(br * g, ch);
        s[i] = this.t2.process(this.t1.process(y, ch), ch) * lv;
      }
    }
  }
}

class HardClipDist extends GainBase {
  constructor() { super(); this.gainS = new Smoother(); this.level = new Smoother(); this.gk = 0.5; this.tone = 0.5; this.clip = new AdaaHardClip(); this.hp = new Biquad(); this.pre = new Biquad(); this.gbw = new Biquad(); this.tlp = new Biquad(); this.dc = new DCBlocker(); }
  setParameter(i, v) { if (i === 0) { this.gk = v; this.dirty = true; } if (i === 1) { this.tone = v; this.dirty = true; } if (i === 2) this.clip.setBias(v * 0.45); if (i === 3) this.level.setTarget(dbToGain(-30 + 36 * v)); }
  onPrepare() { this.gainS.prepare(this.sr, 25); this.level.prepare(this.sr, 25); this.dc.prepare(this.sr); }
  reset() { this.gainS.snap(); this.level.snap(); this.clip.reset(); this.pre.reset(); this.gbw.reset(); this.tlp.reset(); this.dc.reset(); this.hp.reset(); }
  process(L, R, n, ctx) {
    this.quality(ctx, this.clip);
    if (this.dirty) {
      this.dirty = false;
      const g = 1 + 1500 * Math.pow(this.gk, 2.4); this.gainS.setTarget(g);
      this.hp.highpass(this.sr, 50, 0.7); this.pre.highShelf(this.sr, 1500, 0.6, 3 + 6 * this.gk);
      this.gbw.lowpass(this.sr, clamp(2.2e6 / g, 3000, 20000), 0.7); this.tlp.lowpass(this.sr, expMap(this.tone, 700, 12000), 0.65);
    }
    for (let i = 0; i < n; i++) {
      const g = this.gainS.next(), lv = this.level.next();
      for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L;
        let y = this.gbw.process(this.pre.process(this.hp.process(s[i], ch), ch) * g, ch);
        y = this.clip.process(y * 0.5, ch);
        s[i] = this.dc.process(this.tlp.process(y, ch), ch) * lv;
      }
    }
  }
}

class HighGainDist extends GainBase {
  constructor() {
    super(); this.gain = new Smoother(); this.level = new Smoother(); this.v = [0.6, 0.5, 0.5, 0.5, 0.5];
    this.s1 = new AdaaTanh(); this.s2 = new AdaaTanh(); this.s3 = new AdaaTanh();
    this.tight = new Biquad(); this.lo = new Biquad(); this.mid = new Biquad(); this.hi = new Biquad(); this.pres = new Biquad(); this.fizz = new Biquad();
    this.i1 = new OnePole(); this.i2 = new OnePole(); this.d1 = new DCBlocker(); this.d2 = new DCBlocker();
  }
  setParameter(i, v) { if (i === 0) this.gain.setTarget(v); else if (i === 5) this.level.setTarget(dbToGain(-30 + 36 * v)); else { this.v[i] = v; this.dirty = true; } }
  onPrepare() {
    this.gain.prepare(this.sr, 25); this.level.prepare(this.sr, 25);
    this.s1.setBias(0.18); this.s2.setBias(-0.12); this.s3.setBias(0.05);
    this.i1.setCutoff(this.sr, 7500); this.i2.setCutoff(this.sr, 6000); this.d1.prepare(this.sr); this.d2.prepare(this.sr);
  }
  reset() { this.gain.snap(); this.level.snap(); for (const s of [this.s1, this.s2, this.s3]) s.reset(); for (const b of [this.tight, this.lo, this.mid, this.hi, this.pres, this.fizz]) b.reset(); this.i1.reset(); this.i2.reset(); this.d1.reset(); this.d2.reset(); }
  process(L, R, n, ctx) {
    this.quality(ctx, this.s1, this.s2, this.s3);
    if (this.dirty) {
      this.dirty = false; const sr = this.sr, [, b, m, t, p] = this.v;
      this.tight.highpass(sr, 115, 0.75); this.lo.lowShelf(sr, 110, 0.7, (b - 0.5) * 24); this.mid.peak(sr, 650, 0.8, (m - 0.5) * 20);
      this.hi.highShelf(sr, 2500, 0.7, (t - 0.5) * 20); this.pres.peak(sr, 4500, 1, (p - 0.5) * 16); this.fizz.lowpass(sr, 9000, 0.7);
    }
    for (let i = 0; i < n; i++) {
      const gk = this.gain.next(), lv = this.level.next(), g1 = 2 + 35 * gk, g2 = 2 + 22 * gk;
      for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L;
        let y = this.s1.process(this.tight.process(s[i], ch) * g1, ch);
        y = this.s2.process(this.d1.process(this.i1.lowpass(y, ch), ch) * g2, ch);
        y = this.s3.process(this.d2.process(this.i2.lowpass(y, ch), ch) * 3, ch);
        y = this.fizz.process(this.pres.process(this.hi.process(this.mid.process(this.lo.process(y, ch), ch), ch), ch), ch);
        s[i] = y * lv;
      }
    }
  }
}

class Fuzz extends GainBase {
  constructor() { super(); this.sus = new Smoother(); this.tone = new Smoother(); this.level = new Smoother(); this.dz = 0; this.s1 = new AdaaTanh(); this.s2 = new AdaaTanh(); this.hp = new OnePole(); this.lp = new OnePole(); this.hpT = new OnePole(); this.dc = new DCBlocker(); }
  setParameter(i, v) {
    if (i === 0) this.sus.setTarget(v); if (i === 1) this.tone.setTarget(v);
    if (i === 2) { this.s1.setBias((v - 0.5) * 0.9); this.dz = Math.max(0, 0.5 - v) * 0.7; }
    if (i === 3) this.level.setTarget(dbToGain(-30 + 36 * v));
  }
  onPrepare() { for (const s of [this.sus, this.tone, this.level]) s.prepare(this.sr, 25); this.hp.setCutoff(this.sr, 45); this.lp.setCutoff(this.sr, 480); this.hpT.setCutoff(this.sr, 1150); this.dc.prepare(this.sr); }
  reset() { this.sus.snap(); this.tone.snap(); this.level.snap(); this.s1.reset(); this.s2.reset(); this.hp.reset(); this.lp.reset(); this.hpT.reset(); this.dc.reset(); }
  process(L, R, n, ctx) {
    this.quality(ctx, this.s1, this.s2);
    for (let i = 0; i < n; i++) {
      const su = this.sus.next(), t = this.tone.next(), lv = this.level.next(), g = 8 + 500 * su * su;
      for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L;
        let y = this.s1.process(this.hp.highpass(s[i], ch) * g * 0.5, ch);
        const a = Math.abs(y);
        y = a <= this.dz ? 0 : Math.sign(y) * (a - this.dz) / (1 - this.dz);
        y = this.s2.process(y * 3, ch);
        const lo = this.lp.lowpass(y, ch), hi = this.hpT.highpass(y, ch);
        s[i] = this.dc.process(lo * (1 - t) * 1.4 + hi * t * 1.1, ch) * lv;
      }
    }
  }
}

class OctaveFuzz extends GainBase {
  constructor() { super(); this.drive = new Smoother(); this.oct = new Smoother(); this.level = new Smoother(); this.tone = 0.5; this.pre = new AdaaTanh(); this.post = new AdaaTanh(); this.dc = new DCBlocker(); this.dc2 = new DCBlocker(); this.tlp = new Biquad(); this.hp = new Biquad(); this.ax = [0, 0]; this.aF = [0, 0]; }
  setParameter(i, v) { if (i === 0) this.drive.setTarget(v); if (i === 1) this.oct.setTarget(v); if (i === 2) { this.tone = v; this.dirty = true; } if (i === 3) this.level.setTarget(dbToGain(-30 + 36 * v)); }
  onPrepare() { for (const s of [this.drive, this.oct, this.level]) s.prepare(this.sr, 25); this.dc.prepare(this.sr); this.dc2.prepare(this.sr); this.pre.setBias(0); this.post.setBias(0.1); }
  reset() { this.drive.snap(); this.oct.snap(); this.level.snap(); this.pre.reset(); this.post.reset(); this.dc.reset(); this.dc2.reset(); this.tlp.reset(); this.hp.reset(); this.ax = [0, 0]; this.aF = [0, 0]; }
  process(L, R, n, ctx) {
    this.quality(ctx, this.pre, this.post);
    if (this.dirty) { this.dirty = false; this.tlp.lowpass(this.sr, expMap(this.tone, 900, 10000), 0.7); this.hp.highpass(this.sr, 80, 0.7); }
    const adaa = ctx.quality > 0;
    for (let i = 0; i < n; i++) {
      const dr = this.drive.next(), oc = this.oct.next(), lv = this.level.next(), g = 2 + 45 * dr * dr;
      for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L;
        const v = this.pre.process(this.hp.process(s[i], ch) * g, ch);
        const F = 0.5 * v * Math.abs(v), dx = v - this.ax[ch];
        const r = !adaa || Math.abs(dx) < 1e-6 ? Math.abs(v) : (F - this.aF[ch]) / dx;
        this.ax[ch] = v; this.aF[ch] = F;
        const rect = this.dc.process(r * 2, ch);
        const y = this.post.process((v * (1 - oc) + rect * oc) * (1.5 + 3 * dr), ch);
        s[i] = this.dc2.process(this.tlp.process(y, ch), ch) * lv;
      }
    }
  }
}

class Bitcrusher extends Effect {
  constructor() { super(); this.bits = 8; this.ds = 4; this.tone = 1; this.phase = 1; this.held = [0, 0]; this.dirty = true; this.out = new Smoother(); this.lp = new Biquad(); }
  setParameter(i, v) { if (i === 0) this.bits = v; if (i === 1) this.ds = Math.max(1, v); if (i === 2) { this.tone = v; this.dirty = true; } if (i === 3) this.out.setTarget(dbToGain(v)); }
  onPrepare() { this.out.prepare(this.sr, 20); }
  reset() { this.out.snap(); this.phase = 1; this.held = [0, 0]; this.lp.reset(); }
  process(L, R, n) {
    if (this.dirty) { this.dirty = false; this.lp.lowpass(this.sr, expMap(this.tone, 1200, 20000), 0.7); }
    const q = Math.pow(2, this.bits - 1), inc = 1 / this.ds;
    for (let i = 0; i < n; i++) {
      this.phase += inc;
      if (this.phase >= 1) { this.phase -= 1; this.held[0] = Math.round(clamp(L[i], -1, 1) * q) / q; this.held[1] = Math.round(clamp(R[i], -1, 1) * q) / q; }
      const o = this.out.next();
      L[i] = this.lp.process(this.held[0], 0) * o; R[i] = this.lp.process(this.held[1], 1) * o;
    }
  }
}

// ---------------------------------------------------------------- filters / EQ
class Wah extends Effect {
  constructor() { super(); this.pos = new Smoother(); this.q = 6; this.drive = 0; this.range = 0; this.svf = new SVF(); }
  setParameter(i, v) { if (i === 0) this.pos.setTarget(v); if (i === 1) this.q = 1.5 + v * 11; if (i === 2) this.drive = v; if (i === 3) this.range = v | 0; }
  onPrepare() { this.pos.prepare(this.sr, 12); }
  reset() { this.pos.snap(); this.svf.reset(); }
  process(L, R, n) {
    const lo = [350, 250, 450][this.range] || 350, hi = [2200, 1600, 3200][this.range] || 2200, dg = 1 + this.drive * 12;
    for (let i = 0; i < n; i += 16) {
      const m = Math.min(16, n - i); let p = 0;
      for (let k = 0; k < m; k++) p = this.pos.next();
      this.svf.set(this.sr, expMap(p, lo, hi), this.q);
      for (let k = i; k < i + m; k++) for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L; let x = s[k];
        if (this.drive > 0.001) x = fastTanh(x * dg) / Math.sqrt(dg);
        this.svf.tick(x, ch); s[k] = (this.svf.bp * 2.2 + this.svf.lp * 0.25) * 0.9;
      }
    }
  }
}

class AutoWah extends Effect {
  constructor() { super(); this.rate = 1.5; this.sync = 0; this.depth = 0.7; this.q = 5; this.sens = 0.2; this.centre = 0.45; this.mode = 0; this.svf = new SVF(); this.env = new EnvelopeFollower(); this.lfo = new Lfo(); }
  setParameter(i, v) { [() => this.rate = v, () => this.sync = v | 0, () => this.depth = v, () => this.q = 0.8 + v * 12, () => this.sens = v, () => this.centre = v, () => this.mode = v | 0][i]?.(); }
  onPrepare() { this.env.set(this.sr, 4, 120); }
  reset() { this.svf.reset(); this.env.reset(); this.lfo.phase = 0; }
  process(L, R, n, ctx) {
    const hz = this.sync > 0 ? ctx.bpm / 60 / syncBeats(this.sync) : this.rate, inc = hz / this.sr;
    for (let i = 0; i < n; i += 16) {
      const m = Math.min(16, n - i); let e = 0;
      for (let k = i; k < i + m; k++) e = this.env.process(Math.max(Math.abs(L[k]), Math.abs(R[k])));
      this.lfo.advance(inc * m);
      const l = 0.5 + 0.5 * this.lfo.value(0, -0.25);
      const t = this.centre + this.depth * (l - 0.5) * 1.6 + this.sens * Math.min(1, e * 6);
      this.svf.set(this.sr, expMap(clamp(t, 0, 1), 180, 4200), this.q);
      for (let k = i; k < i + m; k++) for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L; this.svf.tick(s[k], ch);
        s[k] = this.mode === 0 ? this.svf.bp * 2 : this.mode === 1 ? this.svf.lp : this.svf.lp + this.svf.bp;
      }
    }
  }
}

class EnvelopeFilter extends Effect {
  constructor() { super(); this.sens = 0.5; this.attack = 8; this.decay = 250; this.q = 4; this.up = true; this.mode = 0; this.base = 0.1; this.dirty = true; this.svf = new SVF(); this.env = new EnvelopeFollower(); }
  setParameter(i, v) { [() => this.sens = v, () => { this.attack = v; this.dirty = true; }, () => { this.decay = v; this.dirty = true; }, () => this.q = v, () => this.up = v < 0.5, () => this.mode = v | 0, () => this.base = v][i]?.(); }
  reset() { this.svf.reset(); this.env.reset(); }
  process(L, R, n) {
    if (this.dirty) { this.dirty = false; this.env.set(this.sr, this.attack, this.decay); }
    const gIn = 2 + this.sens * 40;
    for (let i = 0; i < n; i += 8) {
      const m = Math.min(8, n - i); let e = 0;
      for (let k = i; k < i + m; k++) e = this.env.process(0.5 * (L[k] + R[k]) * gIn);
      const a = clamp(e, 0, 1), t = this.up ? this.base + (1 - this.base) * a : this.base * (1 - a);
      this.svf.set(this.sr, expMap(t, 150, 5000), this.q);
      for (let k = i; k < i + m; k++) for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L; this.svf.tick(s[k], ch);
        s[k] = this.mode === 0 ? this.svf.lp : this.mode === 1 ? this.svf.bp * 1.8 : this.svf.hp;
      }
    }
  }
}

class GraphicEQ extends Effect {
  constructor() { super(); this.g = [0, 0, 0, 0, 0, 0, 0]; this.dirty = true; this.bands = Array.from({ length: 7 }, () => new Biquad()); this.out = new Smoother(); }
  setParameter(i, v) { if (i < 7) { this.g[i] = v; this.dirty = true; } else this.out.setTarget(dbToGain(v)); }
  onPrepare() { this.out.prepare(this.sr, 20); }
  reset() { for (const b of this.bands) b.reset(); this.out.snap(); }
  process(L, R, n) {
    const f = [100, 200, 400, 800, 1600, 3200, 6400];
    if (this.dirty) { this.dirty = false; this.bands.forEach((b, k) => b.peak(this.sr, f[k], 1.4, this.g[k])); }
    for (let i = 0; i < n; i++) {
      let l = L[i], r = R[i];
      for (const b of this.bands) { l = b.process(l, 0); r = b.process(r, 1); }
      const o = this.out.next(); L[i] = l * o; R[i] = r * o;
    }
  }
}

class ParametricEQ extends Effect {
  constructor() { super(); this.p = [120, 0, 0.7, 800, 0, 1, 4000, 0, 0.7]; this.dirty = true; this.lo = new Biquad(); this.mid = new Biquad(); this.hi = new Biquad(); this.out = new Smoother(); }
  setParameter(i, v) { if (i === 9) this.out.setTarget(dbToGain(v)); else { this.p[i] = v; this.dirty = true; } }
  onPrepare() { this.out.prepare(this.sr, 20); }
  reset() { this.lo.reset(); this.mid.reset(); this.hi.reset(); this.out.snap(); }
  process(L, R, n) {
    const p = this.p;
    if (this.dirty) { this.dirty = false; this.lo.lowShelf(this.sr, p[0], Math.max(0.3, p[2]), p[1]); this.mid.peak(this.sr, p[3], p[5], p[4]); this.hi.highShelf(this.sr, p[6], Math.max(0.3, p[8]), p[7]); }
    for (let i = 0; i < n; i++) {
      const o = this.out.next();
      L[i] = this.hi.process(this.mid.process(this.lo.process(L[i], 0), 0), 0) * o;
      R[i] = this.hi.process(this.mid.process(this.lo.process(R[i], 1), 1), 1) * o;
    }
  }
}

// ---------------------------------------------------------------- modulation
class Phaser extends Effect {
  constructor() { super(); this.rate = 0.5; this.depth = 0.7; this.fbk = 0.3; this.stages = 4; this.centre = 0.5; this.stereo = 0.5; this.fb = [0, 0]; this.ap = Array.from({ length: 12 }, () => new FirstOrderAllpass()); this.lfo = new Lfo(); }
  setParameter(i, v) { [() => this.rate = v, () => this.depth = v, () => this.fbk = clamp(v, -0.95, 0.95), () => this.stages = 2 + 2 * clamp(v | 0, 0, 5), () => this.centre = v, () => this.stereo = v][i]?.(); }
  reset() { for (const a of this.ap) a.reset(); this.fb = [0, 0]; }
  process(L, R, n) {
    const inc = this.rate / this.sr, coeff = [0, 0];
    for (let i = 0; i < n; i += 8) {
      const m = Math.min(8, n - i); this.lfo.advance(inc * m);
      for (let ch = 0; ch < 2; ch++) {
        const l = this.lfo.value(1, ch === 0 ? 0 : this.stereo * 0.25);
        const f = expMap(clamp(this.centre + this.depth * 0.5 * l, 0, 1), 120, 6000), t = Math.tan(PI * f / this.sr);
        coeff[ch] = (t - 1) / (t + 1);
      }
      for (let k = i; k < i + m; k++) for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L; let y = s[k] + this.fb[ch] * this.fbk;
        for (let st = 0; st < this.stages; st++) y = this.ap[st].process(y, coeff[ch], ch);
        this.fb[ch] = undenormal(clamp(y, -4, 4)); s[k] = y;
      }
    }
  }
}

class Flanger extends Effect {
  constructor() { super(); this.rate = 0.3; this.depth = 0.7; this.fbk = 0.5; this.stereo = 0.5; this.dms = new Smoother(); this.line = [new DelayLine(), new DelayLine()]; this.lfo = new Lfo(); }
  setParameter(i, v) { [() => this.rate = v, () => this.depth = v, () => this.fbk = clamp(v, -0.95, 0.95), () => this.dms.setTarget(v), () => this.stereo = v][i]?.(); }
  onPrepare() { const m = (0.025 * this.sr | 0) + 8; this.line[0].setMaxDelay(m); this.line[1].setMaxDelay(m); this.dms.prepare(this.sr, 50); }
  reset() { this.line[0].reset(); this.line[1].reset(); this.dms.snap(); }
  process(L, R, n) {
    const inc = this.rate / this.sr;
    for (let i = 0; i < n; i++) {
      this.lfo.advance(inc); const base = this.dms.next() * 0.001 * this.sr;
      for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L, l = this.lfo.value(1, ch === 0 ? 0 : this.stereo * 0.5);
        const y = this.line[ch].readHermite(Math.max(2, base * (1 + this.depth * 0.95 * l)));
        this.line[ch].push(undenormal(s[i] + this.fbk * y)); s[i] = y;
      }
    }
  }
}

class Chorus extends Effect {
  constructor() { super(); this.rate = 0.8; this.width = 0.7; this.tone = 0.8; this.voices = 2; this.dirty = true; this.depth = new Smoother(); this.dms = new Smoother(); this.line = [new DelayLine(), new DelayLine()]; this.lfos = [new Lfo(), new Lfo(), new Lfo(), new Lfo()]; this.lp = new Biquad(); }
  setParameter(i, v) { [() => this.rate = v, () => this.depth.setTarget(v), () => this.voices = 1 + clamp(v | 0, 0, 3), () => this.width = v, () => this.dms.setTarget(v), () => { this.tone = v; this.dirty = true; }][i]?.(); }
  onPrepare() { const m = (0.06 * this.sr | 0) + 8; this.line[0].setMaxDelay(m); this.line[1].setMaxDelay(m); this.depth.prepare(this.sr, 40); this.dms.prepare(this.sr, 60); this.lfos.forEach((l, v) => l.phase = 0.25 * v); }
  reset() { this.line[0].reset(); this.line[1].reset(); this.depth.snap(); this.dms.snap(); this.lp.reset(); }
  process(L, R, n) {
    const mul = [1, 1.13, 0.87, 1.31];
    if (this.dirty) { this.dirty = false; this.lp.lowpass(this.sr, expMap(this.tone, 2500, 18000), 0.7); }
    const norm = 1 / Math.sqrt(this.voices);
    for (let i = 0; i < n; i++) {
      const base = this.dms.next() * 0.001 * this.sr, md = this.depth.next() * base * 0.45;
      this.line[0].push(L[i]); this.line[1].push(R[i]);
      let oL = 0, oR = 0;
      for (let v = 0; v < this.voices; v++) {
        this.lfos[v].advance(this.rate * mul[v] / this.sr);
        const d = base + md * this.lfos[v].value(0, v / this.voices);
        const pan = (v % 2 === 0 ? -1 : 1) * this.width;
        const yl = this.line[0].readHermite(d), yr = this.line[1].readHermite(d), y = 0.5 * (yl + yr);
        oL += lerp(y, yl, 0.5) * (1 - Math.max(0, pan)); oR += lerp(y, yr, 0.5) * (1 + Math.min(0, pan));
      }
      L[i] = this.lp.process(oL * norm, 0); R[i] = this.lp.process(oR * norm, 1);
    }
  }
}

class Vibrato extends Effect {
  constructor() { super(); this.rate = 5; this.shape = 0; this.depth = new Smoother(); this.line = [new DelayLine(), new DelayLine()]; this.lfo = new Lfo(); }
  setParameter(i, v) { if (i === 0) this.rate = v; if (i === 1) this.depth.setTarget(v); if (i === 2) this.shape = v | 0; }
  onPrepare() { const m = (0.02 * this.sr | 0) + 8; this.line[0].setMaxDelay(m); this.line[1].setMaxDelay(m); this.depth.prepare(this.sr, 40); }
  reset() { this.line[0].reset(); this.line[1].reset(); this.depth.snap(); }
  process(L, R, n) {
    const sh = [0, 1, 2][this.shape] ?? 0, centre = 0.006 * this.sr;
    for (let i = 0; i < n; i++) {
      this.lfo.advance(this.rate / this.sr);
      const dev = this.depth.next() * Math.min(0.005, 0.0035 * 5 / Math.max(0.5, this.rate)) * this.sr;
      const d = centre + dev * this.lfo.value(sh);
      this.line[0].push(L[i]); this.line[1].push(R[i]);
      L[i] = this.line[0].readHermite(d); R[i] = this.line[1].readHermite(d);
    }
  }
}

class Tremolo extends Effect {
  constructor() { super(); this.rate = 5; this.sync = 0; this.wave = 0; this.stereo = 0; this.depth = new Smoother(); this.lfo = new Lfo(); }
  setParameter(i, v) { [() => this.rate = v, () => this.sync = v | 0, () => this.depth.setTarget(v), () => this.wave = clamp(v | 0, 0, 4), () => this.stereo = v][i]?.(); }
  onPrepare() { this.depth.prepare(this.sr, 20); }
  reset() { this.depth.snap(); }
  process(L, R, n, ctx) {
    let hz = this.rate;
    if (this.sync > 0) { const b = syncBeats(this.sync); hz = ctx.bpm / 60 / b; }
    const inc = hz / this.sr;
    for (let i = 0; i < n; i++) {
      this.lfo.advance(inc); const d = this.depth.next();
      L[i] *= 1 - d * (0.5 + 0.5 * this.lfo.value(this.wave));
      R[i] *= 1 - d * (0.5 + 0.5 * this.lfo.value(this.wave, this.stereo * 0.5));
    }
  }
}

class Rotary extends Effect {
  constructor() {
    super(); this.speed = 0; this.accel = 0.4; this.width = 0.8; this.drive = 0.1; this.balance = 0.55; this.hornHz = 0.8; this.drumHz = 0.67;
    this.lp1 = new Biquad(); this.lp2 = new Biquad(); this.hp1 = new Biquad(); this.hp2 = new Biquad(); this.hl = new DelayLine(); this.dl = new DelayLine(); this.horn = new Lfo(); this.drum = new Lfo();
  }
  setParameter(i, v) { [() => this.speed = v | 0, () => this.accel = v, () => this.width = v, () => this.drive = v, () => this.balance = v][i]?.(); }
  th() { return this.speed === 0 ? 0.8 : this.speed === 1 ? 6.7 : 0; }
  td() { return this.speed === 0 ? 0.67 : this.speed === 1 ? 5.9 : 0; }
  onPrepare() {
    const m = (0.01 * this.sr | 0) + 8; this.hl.setMaxDelay(m); this.dl.setMaxDelay(m);
    this.lp1.lowpass(this.sr, 800, 0.7071); this.lp2.lowpass(this.sr, 800, 0.7071); this.hp1.highpass(this.sr, 800, 0.7071); this.hp2.highpass(this.sr, 800, 0.7071);
    this.drum.phase = 0.3;
  }
  reset() { for (const b of [this.lp1, this.lp2, this.hp1, this.hp2]) b.reset(); this.hl.reset(); this.dl.reset(); this.hornHz = this.th(); this.drumHz = this.td(); }
  process(L, R, n) {
    const sr = this.sr, hc = Math.exp(-1 / ((0.25 + this.accel * 1.8) * sr)), dc = Math.exp(-1 / ((0.9 + this.accel * 4.5) * sr));
    const th = this.th(), td = this.td(), dg = 1 + this.drive * 8, hD = 0.00055 * sr, dD = 0.00025 * sr, base = 0.0015 * sr;
    const hG = Math.sqrt(this.balance) * 1.2, dG = Math.sqrt(1 - this.balance) * 1.2, out = [0, 0];
    for (let i = 0; i < n; i++) {
      this.hornHz = th + hc * (this.hornHz - th); this.drumHz = td + dc * (this.drumHz - td);
      this.horn.advance(this.hornHz / sr); this.drum.advance(this.drumHz / sr);
      let x = 0.5 * (L[i] + R[i]);
      if (this.drive > 0.001) x = fastTanh(x * dg) / Math.sqrt(dg);
      this.hl.push(this.hp2.process(this.hp1.process(x, 0), 0)); this.dl.push(this.lp2.process(this.lp1.process(x, 0), 0));
      for (let mic = 0; mic < 2; mic++) {
        const off = mic === 0 ? 0 : 0.5 * this.width;
        const hs = Lfo.shape(this.horn.phase + off, 0), hcos = Lfo.shape(this.horn.phase + off + 0.25, 0), ds = Lfo.shape(this.drum.phase + off, 0);
        const h = this.hl.readLinear(base + hD * (1 + hs)) * (1 - 0.35 * (0.5 + 0.5 * hcos));
        const d = this.dl.readLinear(base + dD * (1 + ds)) * (1 - 0.22 * (0.5 + 0.5 * ds));
        out[mic] = h * hG + d * dG;
      }
      const mid = 0.5 * (out[0] + out[1]);
      L[i] = lerp(mid, out[0], this.width); R[i] = lerp(mid, out[1], this.width);
    }
  }
}

// ---------------------------------------------------------------- pitch
class GranularShifter {
  constructor() { this.line = new DelayLine(); this.phase = 0; }
  setMax(n) { this.line.setMaxDelay(n); }
  reset() { this.line.reset(); this.phase = 0; }
  process(x, ratio, w) {
    this.line.push(x);
    this.phase += (1 - ratio) / w; this.phase -= Math.floor(this.phase);
    const p1 = this.phase; let p2 = p1 + 0.5; if (p2 >= 1) p2 -= 1;
    const g1 = Math.sin(PI * p1), g2 = Math.sin(PI * p2);
    return this.line.readLinear(3 + p1 * w) * g1 * g1 + this.line.readLinear(3 + p2 * w) * g2 * g2;
  }
}

class PitchShifter extends Effect {
  constructor() { super(); this.semis = 12; this.cents = 0; this.grain = new Smoother(); this.sh = [new GranularShifter(), new GranularShifter()]; }
  setParameter(i, v) { if (i === 0) this.semis = v; if (i === 1) this.cents = v; if (i === 2) this.grain.setTarget(v); }
  onPrepare() { const m = (0.12 * this.sr | 0) + 16; this.sh[0].setMax(m); this.sh[1].setMax(m); this.grain.prepare(this.sr, 80); }
  reset() { this.sh[0].reset(); this.sh[1].reset(); this.grain.snap(); }
  process(L, R, n) {
    const ratio = Math.pow(2, (this.semis + this.cents / 100) / 12);
    for (let i = 0; i < n; i++) { const w = this.grain.next() * 0.001 * this.sr; L[i] = this.sh[0].process(L[i], ratio, w); R[i] = this.sh[1].process(R[i], ratio, w); }
  }
}

class Octaver extends Effect {
  constructor() {
    super(); this.s = [new Smoother(), new Smoother(), new Smoother(), new Smoother()]; this.tracking = 0.5; this.f1 = 1; this.f2 = 1; this.armed = true; this.dirty = true;
    this.lpA = new Biquad(); this.lpB = new Biquad(); this.upHp = new Biquad(); this.subLp = new OnePole(); this.sub2Lp = new OnePole(); this.dc = new DCBlocker(); this.env = new EnvelopeFollower();
  }
  setParameter(i, v) { if (i < 4) this.s[i].setTarget(v); else { this.tracking = v; this.dirty = true; } }
  onPrepare() { for (const s of this.s) s.prepare(this.sr, 25); this.env.set(this.sr, 1, 60); this.subLp.setCutoff(this.sr, 700); this.sub2Lp.setCutoff(this.sr, 450); this.upHp.highpass(this.sr, 160, 0.7); this.dc.prepare(this.sr); }
  reset() { for (const s of this.s) s.snap(); for (const f of [this.lpA, this.lpB, this.upHp, this.subLp, this.sub2Lp, this.dc, this.env]) f.reset(); this.f1 = this.f2 = 1; this.armed = true; }
  process(L, R, n) {
    if (this.dirty) { this.dirty = false; const fc = expMap(1 - this.tracking, 180, 1100); this.lpA.lowpass(this.sr, fc, 0.6); this.lpB.lowpass(this.sr, fc, 0.6); }
    for (let i = 0; i < n; i++) {
      const x = 0.5 * (L[i] + R[i]), e = this.env.process(x), t = this.lpB.process(this.lpA.process(x, 0), 0), hyst = e * 0.08 + 1e-4;
      if (this.armed && t > hyst) { this.armed = false; this.f1 = -this.f1; if (this.f1 > 0) this.f2 = -this.f2; }
      else if (!this.armed && t < -hyst) this.armed = true;
      const gate = e > 0.002 ? 1 : e / 0.002;
      const s1 = this.subLp.lowpass(this.f1 * Math.abs(t) * 1.6 * gate, 0), s2 = this.sub2Lp.lowpass(this.f2 * Math.abs(t) * 1.6 * gate, 0);
      const u = this.upHp.process(this.dc.process(Math.abs(x) * 2, 0), 0);
      const wet = s1 * this.s[0].next() + s2 * this.s[1].next() + u * this.s[2].next(), d = this.s[3].next();
      L[i] = L[i] * d + wet; R[i] = R[i] * d + wet;
    }
  }
}

const HARM_SEMIS = [-12, -7, -5, -4, -3, 3, 4, 5, 7, 9, 12];
class Harmonizer extends Effect {
  constructor() { super(); this.r1 = 1.5; this.r2 = 1.26; this.v2 = false; this.spread = 0.5; this.level = new Smoother(); this.win = new Smoother(); this.voice = [new GranularShifter(), new GranularShifter()]; this.aa = new OnePole(); this.wp = 0; this.dc = 0; this.since = 0; }
  setParameter(i, v) {
    if (i === 0) this.r1 = Math.pow(2, HARM_SEMIS[clamp(v | 0, 0, 10)] / 12);
    if (i === 1) { const k = v | 0; this.v2 = k > 0; if (this.v2) this.r2 = Math.pow(2, HARM_SEMIS[clamp(k - 1, 0, 10)] / 12); }
    if (i === 2) this.level.setTarget(v); if (i === 3) this.spread = v;
  }
  onPrepare() { const m = (0.12 * this.sr | 0) + 16; for (const s of this.voice) s.setMax(m); this.an = new Float32Array(1024); this.level.prepare(this.sr, 30); this.win.prepare(this.sr, 15); this.aa.setCutoff(this.sr, this.sr / 10); }
  reset() { for (const s of this.voice) s.reset(); this.level.snap(); this.win.snapTo(0.04 * this.sr); this.an.fill(0); this.wp = this.dc = this.since = 0; }
  analyse() {
    const A = this.an, N = 1024, dsr = this.sr / 4, minLag = Math.max(2, dsr / 1000 | 0), maxLag = Math.min(N / 2, dsr / 60 | 0), len = N - maxLag;
    let en = 0; for (let k = 0; k < N; k++) en += A[k] * A[k];
    if (en < 1e-5) return;
    let best = 1e30, bestLag = 0;
    for (let lag = minLag; lag <= maxLag; lag++) {
      let d = 0;
      for (let k = 0; k < len; k += 2) d += Math.abs(A[(this.wp + k) % N] - A[(this.wp + k + lag) % N]);
      if (d < best * 0.9 || (bestLag === 0 && d < best)) { best = d; bestLag = lag; }
    }
    if (bestLag > 0) { const per = bestLag * 4, per2 = Math.max(1, Math.round(0.035 * this.sr / per)); this.win.setTarget(clamp(per2 * per, 0.012 * this.sr, 0.09 * this.sr)); }
  }
  process(L, R, n) {
    for (let i = 0; i < n; i++) {
      const x = 0.5 * (L[i] + R[i]), f = this.aa.lowpass(x, 0);
      if (++this.dc >= 4) { this.dc = 0; this.an[this.wp] = f; this.wp = (this.wp + 1) % 1024; }
      if (++this.since >= 512) { this.since = 0; this.analyse(); }
      const w = this.win.next(), lv = this.level.next();
      const a = this.voice[0].process(x, this.r1, w), b = this.v2 ? this.voice[1].process(x, this.r2, w) : 0;
      const pA = 0.5 - 0.5 * this.spread, pB = 0.5 + 0.5 * this.spread;
      L[i] += lv * (a * (1 - pA) * 1.4 + b * (1 - pB) * 1.4);
      R[i] += lv * (a * pA * 1.4 + b * pB * 1.4);
    }
  }
}

// ---------------------------------------------------------------- delay
const syncedSeconds = (sync, free, ctx) => (sync <= 0 ? free : syncBeats(sync) * 60 / clamp(ctx.bpm, 20, 400));

class Slapback extends Effect {
  constructor() { super(); this.time = new Smoother(); this.fb = 0.1; this.tone = 0.6; this.dirty = true; this.line = [new DelayLine(), new DelayLine()]; this.lp = new Biquad(); this.hp = new Biquad(); }
  setParameter(i, v) { if (i === 0) this.time.setTarget(v); if (i === 1) this.fb = clamp(v, 0, 0.7); if (i === 2) { this.tone = v; this.dirty = true; } }
  hasTail() { return true; }
  onPrepare() { const m = (0.3 * this.sr | 0) + 8; this.line[0].setMaxDelay(m); this.line[1].setMaxDelay(m); this.time.prepare(this.sr, 120); }
  reset() { this.line[0].reset(); this.line[1].reset(); this.time.snap(); this.lp.reset(); this.hp.reset(); }
  process(L, R, n) {
    if (this.dirty) { this.dirty = false; this.lp.lowpass(this.sr, expMap(this.tone, 1500, 12000), 0.7); this.hp.highpass(this.sr, 120, 0.7); }
    for (let i = 0; i < n; i++) {
      const d = this.time.next() * 0.001 * this.sr;
      for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L, y = this.hp.process(this.lp.process(this.line[ch].readLinear(d), ch), ch);
        this.line[ch].push(undenormal(s[i] + y * this.fb)); s[i] = y;
      }
    }
  }
}

class DigitalDelay extends Effect {
  constructor() { super(); this.timeMs = 400; this.sync = 0; this.fb = 0.35; this.mode = 1; this.spread = 0.5; this.lowCut = 80; this.highCut = 9000; this.dirty = true; this.first = true; this.ds = new Smoother(); this.line = [new DelayLine(), new DelayLine()]; this.hp = new Biquad(); this.lp = new Biquad(); }
  setParameter(i, v) { [() => this.timeMs = v, () => this.sync = v | 0, () => this.fb = clamp(v, 0, 0.95), () => this.mode = v | 0, () => this.spread = v, () => { this.lowCut = v; this.dirty = true; }, () => { this.highCut = v; this.dirty = true; }][i]?.(); }
  hasTail() { return true; }
  onPrepare() { const m = (2.6 * this.sr | 0) + 8; this.line[0].setMaxDelay(m); this.line[1].setMaxDelay(m); this.ds.prepare(this.sr, 160); }
  reset() { this.line[0].reset(); this.line[1].reset(); this.hp.reset(); this.lp.reset(); this.first = true; }
  process(L, R, n, ctx) {
    if (this.dirty) { this.dirty = false; this.hp.highpass(this.sr, this.lowCut, 0.7); this.lp.lowpass(this.sr, this.highCut, 0.7); }
    this.ds.setTarget(Math.min(syncedSeconds(this.sync, this.timeMs * 0.001, ctx) * this.sr, this.line[0].capacity() * 0.8));
    if (this.first) { this.ds.snap(); this.first = false; }
    for (let i = 0; i < n; i++) {
      const d = this.ds.next(), dR = this.mode === 1 ? d * (1 + 0.25 * this.spread) : d;
      const fl = this.lp.process(this.hp.process(this.line[0].readLinear(d), 0), 0), fr = this.lp.process(this.hp.process(this.line[1].readLinear(dR), 1), 1);
      if (this.mode === 2) { const m = 0.5 * (L[i] + R[i]); this.line[0].push(undenormal(m + fr * this.fb)); this.line[1].push(undenormal(fl * this.fb)); }
      else if (this.mode === 0) { const m = 0.5 * (L[i] + R[i]); this.line[0].push(undenormal(m + fl * this.fb)); this.line[1].push(undenormal(m + fl * this.fb)); }
      else { this.line[0].push(undenormal(L[i] + fl * this.fb)); this.line[1].push(undenormal(R[i] + fr * this.fb)); }
      L[i] = fl; R[i] = this.mode === 0 ? fl : fr;
    }
  }
}

class TapeDelay extends Effect {
  constructor() { super(); this.timeMs = 350; this.sync = 0; this.fb = 0.4; this.sat = 0.4; this.wow = 0.3; this.flut = 0.3; this.tone = 0.5; this.fbs = [0, 0]; this.drift = 0; this.dirty = true; this.first = true; this.ds = new Smoother(); this.line = [new DelayLine(), new DelayLine()]; this.lp = new Biquad(); this.hp = new Biquad(); this.wl = new Lfo(); this.fl = new Lfo(); this.rng = new Rand(); }
  setParameter(i, v) { [() => this.timeMs = v, () => this.sync = v | 0, () => this.fb = clamp(v, 0, 1.05), () => this.sat = v, () => this.wow = v, () => this.flut = v, () => { this.tone = v; this.dirty = true; }][i]?.(); }
  hasTail() { return true; }
  onPrepare() { const m = (2.2 * this.sr | 0) + 8; this.line[0].setMaxDelay(m); this.line[1].setMaxDelay(m); this.ds.prepare(this.sr, 300); }
  reset() { this.line[0].reset(); this.line[1].reset(); this.fbs = [0, 0]; this.lp.reset(); this.hp.reset(); this.first = true; }
  process(L, R, n, ctx) {
    if (this.dirty) { this.dirty = false; this.lp.lowpass(this.sr, expMap(this.tone, 1500, 9000), 0.6); this.hp.highpass(this.sr, 90, 0.6); }
    this.ds.setTarget(Math.min(syncedSeconds(this.sync, this.timeMs * 0.001, ctx) * this.sr, this.line[0].capacity() * 0.8));
    if (this.first) { this.ds.snap(); this.first = false; }
    const drive = 1 + this.sat * 4;
    for (let i = 0; i < n; i++) {
      this.wl.advance(0.55 / this.sr); this.fl.advance(6.3 / this.sr);
      this.drift = 0.9995 * this.drift + 0.0005 * this.rng.bipolar();
      const d = this.ds.next();
      for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L;
        const mod = 1 + this.wow * 0.006 * this.wl.value(0, ch * 0.13) + this.flut * 0.0012 * this.fl.value(0, ch * 0.29) + this.wow * 0.02 * this.drift;
        const y = this.line[ch].readHermite(d * mod);
        let loop = s[i] + this.fbs[ch] * this.fb;
        loop = fastTanh(loop * drive) / drive * (1 + 0.15 * this.sat);
        this.line[ch].push(undenormal(loop));
        const f = this.hp.process(this.lp.process(y, ch), ch);
        this.fbs[ch] = f; s[i] = f;
      }
    }
  }
}

// ---------------------------------------------------------------- reverb
class SpringReverb extends Effect {
  constructor() {
    super(); this.decay = 2; this.tone = 0.5; this.character = 0.5; this.dwell = 0.3; this.dirty = true;
    this.springs = [0, 1].map(() => ({ loop: new DelayLine(), disp: Array.from({ length: 14 }, () => new FirstOrderAllpass()), damp: new OnePole(), len: 1000, fb: 0 }));
    this.inHp = new Biquad(); this.outLp = new Biquad(); this.outHp = new Biquad();
  }
  setParameter(i, v) { [() => this.decay = v, () => { this.tone = v; this.dirty = true; }, () => this.character = v, () => this.dwell = v][i]?.(); }
  hasTail() { return true; }
  onPrepare() {
    this.springs[0].len = 0.0331 * this.sr | 0; this.springs[1].len = 0.0417 * this.sr | 0;
    for (const s of this.springs) { s.loop.setMaxDelay(s.len + 8); s.damp.setCutoff(this.sr, 4200); }
    this.inHp.highpass(this.sr, 250, 0.7); this.outHp.highpass(this.sr, 180, 0.7);
  }
  reset() { for (const s of this.springs) { s.loop.reset(); s.disp.forEach((a) => a.reset()); s.fb = 0; s.damp.reset(); } this.inHp.reset(); this.outLp.reset(); this.outHp.reset(); }
  process(L, R, n) {
    if (this.dirty) { this.dirty = false; this.outLp.lowpass(this.sr, expMap(this.tone, 1800, 7000), 0.7); }
    const a = -0.55 - this.character * 0.25, dg = 1 + this.dwell * 6;
    const gk = this.springs.map((s) => Math.pow(10, -3 * s.len / this.sr / Math.max(0.2, this.decay)));
    const out = [0, 0];
    for (let i = 0; i < n; i++) {
      const x = fastTanh(this.inHp.process(0.5 * (L[i] + R[i]), 0) * dg) / Math.sqrt(dg);
      for (let k = 0; k < 2; k++) {
        const s = this.springs[k]; let y = x + s.fb;
        for (const d of s.disp) y = d.process(y, a, 0);
        s.loop.push(undenormal(y));
        const tap = s.loop.readInt(s.len);
        s.fb = undenormal(s.damp.lowpass(tap, 0) * gk[k]);
        out[k] = tap + this.character * 0.4 * s.loop.readInt(s.len / 3 | 0);
      }
      L[i] = this.outHp.process(this.outLp.process(out[0], 0), 0) * 0.8;
      R[i] = this.outHp.process(this.outLp.process(out[1], 1), 1) * 0.8;
    }
  }
}

class PlateReverb extends Effect {
  constructor() {
    super(); this.decay = 2.5; this.damping = 0.4; this.lowCut = 120; this.mod = 0.5; this.scale = 1; this.fbR = 0; this.dirty = true;
    this.pdms = new Smoother(); this.pre = new DelayLine(); this.bw = new OnePole(); this.inDiff = [0, 1, 2, 3].map(() => new DiffuserAllpass());
    this.tank = [0, 1].map(() => ({ modAp: new DiffuserAllpass(), d1: new DelayLine(), ap2: new DiffuserAllpass(), d2: new DelayLine(), damp: new OnePole(), len1: 1, len2: 1 }));
    this.outHp = new Biquad(); this.lfo = new Lfo();
  }
  setParameter(i, v) { [() => this.decay = v, () => this.pdms.setTarget(v), () => { this.damping = v; this.dirty = true; }, () => { this.lowCut = v; this.dirty = true; }, () => this.mod = v][i]?.(); }
  hasTail() { return true; }
  onPrepare() {
    this.scale = this.sr / 29761; const S = (v) => Math.max(2, Math.round(v * this.scale));
    this.pre.setMaxDelay((0.26 * this.sr | 0) + 8); this.bw.setCutoff(this.sr, 9000);
    [142, 107, 379, 277].forEach((l, k) => { const d = this.inDiff[k]; d.setMax(S(l) + 4); d.delay = S(l); d.gain = k < 2 ? 0.75 : 0.625; });
    const modLen = [672, 908], l1 = [4453, 4217], apl = [1800, 2656], l2 = [3720, 3163], order = [1, 0];
    this.tank.forEach((t, k) => {
      const o = order[k];
      t.modAp.setMax(S(modLen[o]) + 40); t.modAp.delay = S(modLen[o]); t.modAp.gain = -0.7;
      t.len1 = S(l1[o]); t.d1.setMaxDelay(t.len1 + 4); t.ap2.setMax(S(apl[o]) + 4); t.ap2.delay = S(apl[o]); t.ap2.gain = 0.5;
      t.len2 = S(l2[o]); t.d2.setMaxDelay(t.len2 + 4);
    });
    this.pdms.prepare(this.sr, 80);
  }
  reset() { this.pre.reset(); this.bw.reset(); this.inDiff.forEach((d) => d.reset()); for (const t of this.tank) { t.modAp.reset(); t.d1.reset(); t.ap2.reset(); t.d2.reset(); t.damp.reset(); } this.fbR = 0; this.outHp.reset(); this.pdms.snap(); }
  process(L, R, n) {
    if (this.dirty) { this.dirty = false; for (const t of this.tank) t.damp.setCutoff(this.sr, expMap(1 - this.damping, 1500, 16000)); this.outHp.highpass(this.sr, this.lowCut, 0.7); }
    const g = Math.pow(10, -3 * 0.18 / Math.max(0.2, this.decay)), exc = 16 * this.scale * this.mod, sc = this.scale;
    const [a, b] = this.tank, tap = (d, r) => d.readInt(Math.max(1, r * sc | 0));
    for (let i = 0; i < n; i++) {
      this.lfo.advance(0.9 / this.sr);
      this.pre.push(0.5 * (L[i] + R[i]));
      let x = this.bw.lowpass(this.pre.readLinear(Math.max(1, this.pdms.next() * 0.001 * this.sr)), 0);
      for (const d of this.inDiff) x = d.process(x);
      let y = a.modAp.process(x + this.fbR * g, exc * this.lfo.value(0));
      a.d1.push(undenormal(y)); y = a.ap2.process(a.damp.lowpass(a.d1.readInt(a.len1), 0) * g); a.d2.push(undenormal(y));
      const leftOut = a.d2.readInt(a.len2);
      let z = b.modAp.process(x + leftOut * g, exc * this.lfo.value(0, 0.25));
      b.d1.push(undenormal(z)); z = b.ap2.process(b.damp.lowpass(b.d1.readInt(b.len1), 0) * g); b.d2.push(undenormal(z));
      this.fbR = b.d2.readInt(b.len2);
      const oL = tap(b.d1, 266) + tap(b.d1, 2974) - tap(b.ap2.line, 1913) + tap(b.d2, 1996) - tap(a.d1, 1990) - tap(a.ap2.line, 187) - tap(a.d2, 1066);
      const oR = tap(a.d1, 353) + tap(a.d1, 3627) - tap(a.ap2.line, 1228) + tap(a.d2, 2673) - tap(b.d1, 2111) - tap(b.ap2.line, 335) - tap(b.d2, 121);
      L[i] = this.outHp.process(oL * 0.6, 0); R[i] = this.outHp.process(oR * 0.6, 1);
    }
  }
}

class HallReverb extends Effect {
  constructor() {
    super(); this.size = 0.6; this.decay = 3.5; this.diffusion = 0.7; this.damping = 0.4; this.mod = 0.4; this.lowCut = 100; this.shimFb = 0; this.dirty = true;
    this.pdms = new Smoother(); this.shimmer = new Smoother(); this.sizeS = new Smoother();
    this.lines = Array.from({ length: 8 }, () => new DelayLine()); this.pre = new DelayLine(); this.diff = [0, 1, 2, 3].map(() => new DiffuserAllpass());
    this.damp = Array.from({ length: 8 }, () => new OnePole()); this.shifter = new GranularShifter(); this.shimHp = new Biquad(); this.outHp = new Biquad(); this.lfo = new Lfo(); this.lfo2 = new Lfo();
    this.outs = new Float32Array(8);
  }
  setParameter(i, v) {
    [() => { this.size = v; this.sizeS.setTarget(0.35 + v * 1.25); }, () => this.decay = v, () => this.diffusion = v, () => { this.damping = v; this.dirty = true; },
      () => this.pdms.setTarget(v), () => this.mod = v, () => this.shimmer.setTarget(v), () => { this.lowCut = v; this.dirty = true; }][i]?.();
  }
  hasTail() { return true; }
  onPrepare() {
    const m = (0.0731 * 1.6 * this.sr + 0.003 * this.sr | 0) + 64;
    for (const l of this.lines) l.setMaxDelay(m);
    [4.7, 3.6, 12.7, 9.3].forEach((ms, k) => { const d = ms * 0.001 * this.sr | 0; this.diff[k].setMax(d + 4); this.diff[k].delay = d; });
    this.pre.setMaxDelay((0.26 * this.sr | 0) + 8); this.shifter.setMax((0.1 * this.sr | 0) + 16); this.shimHp.highpass(this.sr, 400, 0.7);
    this.pdms.prepare(this.sr, 80); this.shimmer.prepare(this.sr, 50); this.sizeS.prepare(this.sr, 400); this.sizeS.snapTo(0.35 + this.size * 1.25);
  }
  reset() { this.lines.forEach((l) => l.reset()); this.diff.forEach((d) => d.reset()); this.damp.forEach((d) => d.reset()); this.pre.reset(); this.shifter.reset(); this.shimHp.reset(); this.outHp.reset(); this.pdms.snap(); this.shimmer.snap(); this.sizeS.snap(); this.shimFb = 0; }
  process(L, R, n) {
    const base = [29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1], sr = this.sr;
    if (this.dirty) { this.dirty = false; const fc = expMap(1 - this.damping, 1500, 16000); this.damp.forEach((d) => d.setCutoff(sr, fc)); this.outHp.highpass(sr, this.lowCut, 0.7); }
    for (const d of this.diff) d.gain = 0.25 + 0.45 * this.diffusion;
    const md = this.mod * 0.0025 * sr, sz = this.sizeS.current;
    const bd = base.map((ms) => ms * 0.001 * sr * sz), gk = bd.map((d) => Math.pow(10, -3 * d / sr / Math.max(0.3, this.decay)));
    const outs = this.outs;
    for (let i = 0; i < n; i++) {
      this.lfo.advance(0.31 / sr); this.lfo2.advance(0.43 / sr); this.sizeS.next();
      this.pre.push(0.5 * (L[i] + R[i]));
      let x = this.pre.readLinear(Math.max(1, this.pdms.next() * 0.001 * sr));
      for (const d of this.diff) x = d.process(x);
      x += this.shimFb;
      let sum = 0;
      for (let k = 0; k < 8; k++) {
        let dly = bd[k];
        if (k === 1) dly += md * this.lfo.value(0);
        if (k === 6) dly += md * this.lfo2.value(0);
        outs[k] = this.damp[k].lowpass(this.lines[k].readLinear(dly), 0) * gk[k];
        sum += outs[k];
      }
      const h = 0.25 * sum;
      for (let k = 0; k < 8; k++) this.lines[k].push(undenormal(outs[k] - h + ((k & 1) ? -x : x) * 0.5));
      const wL = outs[0] - outs[2] + outs[4] - outs[6], wR = outs[1] - outs[3] + outs[5] - outs[7];
      const sh = this.shimmer.next();
      this.shimFb = sh > 0.001 ? 0.5 * fastTanh(2 * this.shimHp.process(this.shifter.process(0.5 * (wL + wR), 2, 0.06 * sr), 0) * sh * 0.25) : 0;
      L[i] = this.outHp.process(wL * 0.45, 0); R[i] = this.outHp.process(wR * 0.45, 1);
    }
  }
}

// Order = C++ EffectRegistry order (index 0 = empty)
const EFFECTS = [null, NoiseGate, Compressor, CleanBoost, TransparentOD, MidOD, HardClipDist, HighGainDist, Fuzz, OctaveFuzz, Bitcrusher,
  Wah, AutoWah, EnvelopeFilter, GraphicEQ, ParametricEQ, Phaser, Flanger, Chorus, Vibrato, Tremolo, Rotary,
  PitchShifter, Octaver, Harmonizer, Slapback, DigitalDelay, TapeDelay, SpringReverb, PlateReverb, HallReverb];

// ============================================================================ amps
class Triode {
  constructor(neg, bias) { this.neg = neg; this.bias = bias; this.miller = new OnePole(); this.coupling = new DCBlocker(); this.bo = 0; }
  prepare(sr, mHz) { this.miller.setCutoff(sr, mHz); this.coupling.prepare(sr); this.bo = this.shape(this.bias); }
  reset() { this.miller.reset(); this.coupling.reset(); }
  shape(u) { return u >= 0 ? fastTanh(u) : fastTanh(u * this.neg) / this.neg; }
  process(x, g, ch) { return this.coupling.process(this.shape(this.miller.lowpass(x * g, ch) + this.bias) - this.bo, ch); }
}
class ToneStack {
  constructor(o) { Object.assign(this, { bassHz: 100, midHz: 650, midQ: 0.8, trebleHz: 2500, bassRange: 12, midRange: 10, midOffset: 0, trebleRange: 12 }, o); this.lo = new Biquad(); this.mid = new Biquad(); this.hi = new Biquad(); }
  reset() { this.lo.reset(); this.mid.reset(); this.hi.reset(); }
  set(sr, b, m, t) { this.lo.lowShelf(sr, this.bassHz, 0.7, (b - 0.5) * 2 * this.bassRange); this.mid.peak(sr, this.midHz, this.midQ, this.midOffset + (m - 0.5) * 2 * this.midRange); this.hi.highShelf(sr, this.trebleHz, 0.7, (t - 0.5) * 2 * this.trebleRange); }
  process(x, ch) { return this.hi.process(this.mid.process(this.lo.process(x, ch), ch), ch); }
}
class Power {
  constructor(classA, headroom) { this.classA = classA; this.headroom = headroom; this.sag = 0.3; this.drive = 1; this.supply = new EnvelopeFollower(); this.pres = new Biquad(); }
  prepare(sr) { this.supply.set(sr, 8, 180); }
  reset() { this.supply.reset(); this.pres.reset(); }
  setPresence(sr, hz, db) { this.pres.highShelf(sr, hz, 0.7, db); }
  process(x, ch) {
    const env = ch === 0 ? this.supply.process(x * this.drive) : this.supply.env;
    const u = x * this.drive / (1 + this.sag * 2.5 * env) / this.headroom;
    let y = this.classA ? (u >= 0 ? fastTanh(u) : fastTanh(u * 1.7) / 1.7) : fastTanh(u);
    y *= this.headroom * (1 - 0.25 * this.sag * env);
    return this.pres.process(y, ch);
  }
}

class AmpBase {
  constructor() { this.gainS = new Smoother(); this.masterS = new Smoother(); }
  prepareBase(sr) { this.sr = sr; this.gainS.prepare(sr, 20); this.masterS.prepare(sr, 20); }
}
class StudioClean extends AmpBase {
  prepare(sr) { this.prepareBase(sr); this.inHp = new Biquad(); this.inHp.highpass(sr, 30, 0.7); this.bright = new Biquad(); this.s1 = new Triode(1.4, 0.05); this.s1.prepare(sr, 14000); this.s2 = new Triode(1.3, 0.02); this.s2.prepare(sr, 16000); this.tone = new ToneStack({ bassHz: 90, midHz: 480, midQ: 0.6, midRange: 8, midOffset: -3, trebleHz: 2800 }); this.power = new Power(false, 1.6); this.power.prepare(sr); }
  reset() { this.inHp.reset(); this.bright.reset(); this.s1.reset(); this.s2.reset(); this.tone.reset(); this.power.reset(); this.gainS.snap(); this.masterS.snap(); }
  setControls(c) { this.bright.highShelf(this.sr, 2500, 0.7, 6 * (1 - c.gain)); this.tone.set(this.sr, c.bass, c.mid, c.treble); this.power.setPresence(this.sr, 4000, (c.presence - 0.5) * 12); this.power.sag = c.sag * 0.5; this.gainS.setTarget(1.2 + 9 * c.gain * c.gain); this.masterS.setTarget(0.25 + 1.6 * c.master); }
  process(L, R, n) { for (let i = 0; i < n; i++) { const g = this.gainS.next(); this.power.drive = this.masterS.next(); for (let ch = 0; ch < 2; ch++) { const s = ch ? R : L; let y = this.s1.process(this.bright.process(this.inHp.process(s[i], ch), ch), g, ch); y = this.s2.process(this.tone.process(y, ch), 1.8, ch); s[i] = this.power.process(y, ch) * 1.25; } } }
}
class ClassAChime extends AmpBase {
  prepare(sr) { this.prepareBase(sr); this.inHp = new Biquad(); this.inHp.highpass(sr, 60, 0.7); this.s1 = new Triode(2.1, 0.12); this.s1.prepare(sr, 16000); this.tone = new ToneStack({ bassHz: 120, bassRange: 10, midHz: 1100, midQ: 0.9, midRange: 8, trebleHz: 2200, trebleRange: 14 }); this.chime = new Biquad(); this.chime.peak(sr, 3200, 1.2, 3.5); this.power = new Power(true, 0.9); this.power.prepare(sr); }
  reset() { this.inHp.reset(); this.s1.reset(); this.tone.reset(); this.chime.reset(); this.power.reset(); this.gainS.snap(); this.masterS.snap(); }
  setControls(c) { this.tone.set(this.sr, c.bass, c.mid, c.treble); this.power.setPresence(this.sr, 5000, (c.presence - 0.5) * 16); this.power.sag = 0.2 + c.sag * 0.8; this.gainS.setTarget(2 + 28 * c.gain * c.gain); this.masterS.setTarget(0.5 + 3.5 * c.master); }
  process(L, R, n) { for (let i = 0; i < n; i++) { const g = this.gainS.next(); this.power.drive = this.masterS.next(); for (let ch = 0; ch < 2; ch++) { const s = ch ? R : L; let y = this.s1.process(this.inHp.process(s[i], ch), g, ch); y = this.chime.process(this.tone.process(y, ch), ch); s[i] = this.power.process(y * 0.8, ch) * 1.1; } } }
}
class BritishCrunch extends AmpBase {
  prepare(sr) { this.prepareBase(sr); this.inHp = new Biquad(); this.inHp.highpass(sr, 90, 0.7); this.bc = new Biquad(); this.bc.highShelf(sr, 700, 0.7, 4); this.s1 = new Triode(1.6, 0.15); this.s1.prepare(sr, 11000); this.s2 = new Triode(1.8, -0.1); this.s2.prepare(sr, 9000); this.tone = new ToneStack({ bassHz: 100, bassRange: 10, midHz: 720, midQ: 0.8, midRange: 10, midOffset: 2, trebleHz: 2200, trebleRange: 10 }); this.power = new Power(false, 1); this.power.prepare(sr); }
  reset() { this.inHp.reset(); this.bc.reset(); this.s1.reset(); this.s2.reset(); this.tone.reset(); this.power.reset(); this.gainS.snap(); this.masterS.snap(); }
  setControls(c) { this.tone.set(this.sr, c.bass, c.mid, c.treble); this.power.setPresence(this.sr, 3500, (c.presence - 0.5) * 14); this.power.sag = c.sag * 0.9; this.gainS.setTarget(1.5 + 14 * c.gain); this.masterS.setTarget(0.4 + 2.6 * c.master); }
  process(L, R, n) { for (let i = 0; i < n; i++) { const g = this.gainS.next(); this.power.drive = this.masterS.next(); for (let ch = 0; ch < 2; ch++) { const s = ch ? R : L; let y = this.bc.process(this.inHp.process(s[i], ch), ch); y = this.s1.process(y, g * 1.5, ch); y = this.s2.process(y, 1 + g * 0.9, ch); y = this.tone.process(fastTanh(y * 1.2), ch); s[i] = this.power.process(y * 0.9, ch) * 0.4; } } }
}
class HotRodLead extends AmpBase {
  prepare(sr) { this.prepareBase(sr); this.inHp = new Biquad(); this.inHp.highpass(sr, 70, 0.7); this.s1 = new Triode(1.5, 0.2); this.s1.prepare(sr, 8000); this.s2 = new Triode(1.7, -0.15); this.s2.prepare(sr, 7000); this.s3 = new Triode(1.4, 0.1); this.s3.prepare(sr, 6500); this.tone = new ToneStack({ bassHz: 110, midHz: 900, midQ: 0.7, midRange: 10, midOffset: 3, trebleHz: 2000 }); this.top = new Biquad(); this.top.lowpass(sr, 6500, 0.6); this.power = new Power(false, 1); this.power.prepare(sr); }
  reset() { this.inHp.reset(); this.s1.reset(); this.s2.reset(); this.s3.reset(); this.tone.reset(); this.top.reset(); this.power.reset(); this.gainS.snap(); this.masterS.snap(); }
  setControls(c) { this.tone.set(this.sr, c.bass, c.mid, c.treble); this.power.setPresence(this.sr, 3800, (c.presence - 0.5) * 12); this.power.sag = c.sag * 0.7; this.gainS.setTarget(2 + 16 * c.gain); this.masterS.setTarget(0.35 + 2.2 * c.master); }
  process(L, R, n) { for (let i = 0; i < n; i++) { const g = this.gainS.next(); this.power.drive = this.masterS.next(); for (let ch = 0; ch < 2; ch++) { const s = ch ? R : L; let y = this.s1.process(this.inHp.process(s[i], ch), g, ch); y = this.s2.process(y, 1.5 + g * 1.2, ch); y = this.s3.process(y, 1.5 + g * 0.6, ch); s[i] = this.top.process(this.power.process(this.tone.process(y, ch) * 0.8, ch), ch) * 0.4; } } }
}
class ModernMetal extends AmpBase {
  prepare(sr) { this.prepareBase(sr); this.t1 = new Biquad(); this.t1.highpass(sr, 160, 0.7); this.t2 = new Biquad(); this.t2.lowShelf(sr, 250, 0.7, -6); this.s1 = new Triode(1.5, 0.2); this.s1.prepare(sr, 10000); this.s2 = new Triode(1.9, -0.2); this.s2.prepare(sr, 9000); this.s3 = new Triode(1.6, 0.15); this.s3.prepare(sr, 8000); this.s4 = new Triode(1.3, 0); this.s4.prepare(sr, 9000); this.inter = new Biquad(); this.inter.highpass(sr, 110, 0.7); this.tone = new ToneStack({ bassHz: 90, midHz: 750, midQ: 1, midRange: 12, trebleHz: 2800 }); this.res = new Biquad(); this.res.lowShelf(sr, 90, 0.8, 4); this.fizz = new Biquad(); this.fizz.lowpass(sr, 9500, 0.7); this.power = new Power(false, 1); this.power.prepare(sr); }
  reset() { for (const f of [this.t1, this.t2, this.s1, this.s2, this.s3, this.s4, this.inter, this.tone, this.res, this.fizz, this.power]) f.reset(); this.gainS.snap(); this.masterS.snap(); }
  setControls(c) { this.tone.set(this.sr, c.bass, c.mid, c.treble); this.power.setPresence(this.sr, 4500, (c.presence - 0.5) * 16); this.power.sag = c.sag * 0.3; this.gainS.setTarget(3 + 22 * c.gain); this.masterS.setTarget(0.4 + 2 * c.master); }
  process(L, R, n) { for (let i = 0; i < n; i++) { const g = this.gainS.next(); this.power.drive = this.masterS.next(); for (let ch = 0; ch < 2; ch++) { const s = ch ? R : L; let y = this.t2.process(this.t1.process(s[i], ch), ch); y = this.inter.process(this.s1.process(y, g, ch), ch); y = this.s2.process(y, 2 + g, ch); y = this.s3.process(y, 1.5 + g * 0.5, ch); y = this.s4.process(y, 2, ch); y = this.res.process(this.tone.process(y, ch), ch); s[i] = this.fizz.process(this.power.process(y * 0.7, ch), ch) * 0.32; } } }
}
class BassDI extends AmpBase {
  prepare(sr) { this.prepareBase(sr); this.inHp = new Biquad(); this.inHp.highpass(sr, 28, 0.7); this.lp1 = new Biquad(); this.lp2 = new Biquad(); this.hp1 = new Biquad(); this.hp2 = new Biquad(); this.lp1.lowpass(sr, 220, 0.7071); this.lp2.lowpass(sr, 220, 0.7071); this.hp1.highpass(sr, 220, 0.7071); this.hp2.highpass(sr, 220, 0.7071); this.grit = new Triode(1.8, 0.15); this.grit.prepare(sr, 6000); this.tone = new ToneStack({ bassHz: 60, bassRange: 12, midHz: 500, midQ: 0.8, midRange: 10, trebleHz: 3000 }); this.air = new Biquad(); this.comp = new EnvelopeFollower(); this.comp.set(sr, 5, 120); this.power = new Power(false, 2); this.power.prepare(sr); this.sag = 0.3; this.drive = 0.3; }
  reset() { for (const f of [this.inHp, this.lp1, this.lp2, this.hp1, this.hp2, this.grit, this.tone, this.air, this.comp, this.power]) f.reset(); this.gainS.snap(); this.masterS.snap(); }
  setControls(c) { this.tone.set(this.sr, c.bass, c.mid, c.treble); this.air.highShelf(this.sr, 6000, 0.7, (c.presence - 0.5) * 20); this.sag = c.sag; this.drive = c.gain; this.gainS.setTarget(1 + 45 * c.gain * c.gain); this.masterS.setTarget(0.3 + 1.4 * c.master); }
  process(L, R, n) {
    for (let i = 0; i < n; i++) {
      const g = this.gainS.next(); this.power.drive = this.masterS.next();
      const comp = 1 / (1 + this.sag * 4 * this.comp.process(0.5 * (L[i] + R[i])));
      for (let ch = 0; ch < 2; ch++) {
        const s = ch ? R : L, x = this.inHp.process(s[i], ch);
        const low = this.lp2.process(this.lp1.process(x, ch), ch), high = this.hp2.process(this.hp1.process(x, ch), ch);
        const y = low * comp * 1.2 + this.grit.process(high, g, ch) * (0.4 + 0.4 * this.drive) + high * (1 - this.drive);
        s[i] = this.power.process(this.air.process(this.tone.process(y, ch), ch), ch) * 1.2;
      }
    }
  }
}
const AMPS = [StudioClean, ClassAChime, BritishCrunch, HotRodLead, ModernMetal, BassDI];

/** 2x up/down sampler: zero-stuffing / decimation with a 4th-order Butterworth low-pass at 0.45 x base rate. */
class Oversampler2x {
  constructor(sr2x, maxBlock) {
    this.up = [0, 1].map(() => [new Biquad(), new Biquad()]); this.dn = [0, 1].map(() => [new Biquad(), new Biquad()]);
    for (const set of [...this.up, ...this.dn]) { set[0].lowpass(sr2x, sr2x * 0.225, 0.5412); set[1].lowpass(sr2x, sr2x * 0.225, 1.3066); }
    this.L = new Float32Array(maxBlock * 2); this.R = new Float32Array(maxBlock * 2);
  }
  reset() { for (const s of [...this.up, ...this.dn]) { s[0].reset(); s[1].reset(); } }
  upsample(L, R, n) {
    for (let ch = 0; ch < 2; ch++) {
      const src = ch ? R : L, dst = ch ? this.R : this.L, f = this.up[ch];
      for (let i = 0; i < n; i++) {
        dst[2 * i] = f[1].process(f[0].process(src[i] * 2, 0), 0);
        dst[2 * i + 1] = f[1].process(f[0].process(0, 0), 0);
      }
    }
  }
  downsample(L, R, n) {
    for (let ch = 0; ch < 2; ch++) {
      const src = ch ? this.R : this.L, dst = ch ? R : L, f = this.dn[ch];
      for (let i = 0; i < n; i++) { f[1].process(f[0].process(src[2 * i], 0), 0); dst[i] = f[1].process(f[0].process(src[2 * i + 1], 0), 0); }
    }
  }
}

class AmpSection {
  prepare(sr, maxBlock) {
    this.sr = sr; this.maxBlock = maxBlock;
    this.os1 = new Oversampler2x(sr * 2, maxBlock); this.os2 = new Oversampler2x(sr * 4, maxBlock * 2);
    this.models = [1, 2, 4].map((f) => AMPS.map((C) => { const m = new C(); m.prepare(sr * f); return m; }));
    this.scratchL = new Float32Array(maxBlock * 4); this.scratchR = new Float32Array(maxBlock * 4);
    this.quality = 1; this.type = 0; this.fadeFrom = -1; this.fadePos = 1; this.ctrl = null;
    this.trim = new Smoother(); this.trim.prepare(sr, 20); this.trim.snapTo(1);
    this.en = new Smoother(); this.en.prepare(sr, 15); this.en.snapTo(1);
    this.dryL = new Float32Array(maxBlock); this.dryR = new Float32Array(maxBlock);
  }
  setState(a, quality) {
    this.trim.setTarget(dbToGain(a.trimDb)); this.en.setTarget(a.on ? 1 : 0);
    const c = { gain: a.gain, bass: a.bass, mid: a.mid, treble: a.treble, presence: a.presence, master: a.master, sag: a.sag };
    const q = clamp(quality, 0, 2);
    if (q !== this.quality) { this.quality = q; for (const m of this.models[q]) { if (this.ctrl) m.setControls(this.ctrl); m.reset(); } this.os1.reset(); this.os2.reset(); this.fadeFrom = -1; }
    if (a.type !== this.type && this.fadeFrom < 0) { this.fadeFrom = this.type; this.type = a.type; this.fadePos = 0; const m = this.models[this.quality][this.type]; m.setControls(c); m.reset(); }
    this.ctrl = c;
  }
  run(L, R, n) {
    const set = this.models[this.quality], f = 1 << this.quality;
    set[this.type].setControls(this.ctrl);
    if (this.fadeFrom >= 0) {
      set[this.fadeFrom].setControls(this.ctrl);
      this.scratchL.set(L.subarray(0, n)); this.scratchR.set(R.subarray(0, n));
      set[this.fadeFrom].process(this.scratchL, this.scratchR, n);
      set[this.type].process(L, R, n);
      const inc = 1 / (0.02 * this.sr * f);
      for (let i = 0; i < n; i++) {
        this.fadePos = Math.min(1, this.fadePos + inc);
        const a = Math.sin(this.fadePos * PI / 2), b = Math.cos(this.fadePos * PI / 2);
        L[i] = L[i] * a + this.scratchL[i] * b; R[i] = R[i] * a + this.scratchR[i] * b;
      }
      if (this.fadePos >= 1) this.fadeFrom = -1;
    } else set[this.type].process(L, R, n);
  }
  process(L, R, n) {
    if (!this.ctrl) return;
    this.dryL.set(L.subarray(0, n)); this.dryR.set(R.subarray(0, n));
    const active = this.en.current > 1e-4 || this.en.target > 0;
    if (active) {
      if (this.quality === 0) this.run(L, R, n);
      else if (this.quality === 1) { this.os1.upsample(L, R, n); this.run(this.os1.L, this.os1.R, 2 * n); this.os1.downsample(L, R, n); }
      else {
        this.os1.upsample(L, R, n); this.os2.upsample(this.os1.L, this.os1.R, 2 * n);
        this.run(this.os2.L, this.os2.R, 4 * n);
        this.os2.downsample(this.os1.L, this.os1.R, 2 * n); this.os1.downsample(L, R, n);
      }
    }
    for (let i = 0; i < n; i++) {
      const e = this.en.next(), t = this.trim.next();
      L[i] = this.dryL[i] + ((active ? L[i] * t : 0) - this.dryL[i]) * e;
      R[i] = this.dryR[i] + ((active ? R[i] * t : 0) - this.dryR[i]) * e;
    }
  }
}

// ============================================================================ cabinet
class Cabinet {
  prepare(sr, maxBlock) {
    this.sr = sr; this.type = 2; this.mic = 0.3; this.lowCut = 70; this.highCut = 9000;
    this.cur = this.voicing(); this.prev = this.voicing(); this.fadeFrom = -1; this.fadePos = 1;
    this.lc = new Biquad(); this.hc = new Biquad();
    this.level = new Smoother(); this.mixS = new Smoother(); this.en = new Smoother();
    for (const s of [this.level, this.mixS, this.en]) { s.prepare(sr, 20); s.snapTo(1); }
    this.dryL = new Float32Array(maxBlock); this.dryR = new Float32Array(maxBlock); this.tL = new Float32Array(maxBlock); this.tR = new Float32Array(maxBlock);
    this.design(this.cur, this.type); this.dirty = true;
  }
  voicing() { const c = [new DelayLine(), new DelayLine()]; c[0].setMaxDelay((0.002 * this.sr | 0) + 8); c[1].setMaxDelay((0.002 * this.sr | 0) + 8); return { f: Array.from({ length: 8 }, () => new Biquad()), count: 0, combDelay: 1, combGain: 0, comb: c }; }
  design(v, t) {
    const sr = this.sr, m = this.mic; let c = 0;
    const hp = (f, q) => v.f[c++].highpass(sr, f, q), lp = (f, q) => v.f[c++].lowpass(sr, f, q), pk = (f, q, g) => v.f[c++].peak(sr, f, q, g);
    v.combGain = 0; v.combDelay = 1;
    if (t === 6) t = 2; // custom IR is plugin-only: fall back to the 4x12
    switch (t) {
      case 0: hp(85, 0.9); pk(120, 1, 1.5); pk(400, 0.8, -2); pk(2200, 1.2, 3 - 3 * m); pk(4200, 3, -4); lp(5800 - 1800 * m, 0.8); lp(7500 - 2000 * m, 0.6); v.combGain = -0.22; v.combDelay = 0.00035 * sr | 0; break;
      case 1: hp(75, 1); pk(110, 1.1, 2.5); pk(500, 0.7, -2.5); pk(3000, 1.5, 3.5 - 4 * m); pk(5000, 3, -5); lp(5600 - 1600 * m, 0.8); lp(7200 - 1800 * m, 0.6); v.combGain = -0.18; v.combDelay = 0.0005 * sr | 0; break;
      case 2: hp(70, 1.4); pk(105, 1.2, 3.5); pk(420, 0.9, -3.5); pk(2300, 1.2, 3); pk(3400, 2, 3 - 4 * m); pk(4600, 3, -6); lp(5200 - 1500 * m, 0.9); lp(6500, 0.6); break;
      case 3: hp(45, 1); pk(80, 1, 2); pk(350, 0.8, -2); pk(1500, 1, 2); pk(3000, 1.5, 2 - 3 * m); lp(6500 - 1500 * m, 0.7); lp(9000, 0.6); break;
      case 4: hp(38, 1.1); pk(70, 1.1, 3); pk(600, 0.7, -2); pk(1200, 1, 1.5); lp(3500 - 900 * m, 0.8); lp(5000, 0.6); break;
      default: break;
    }
    v.count = c;
  }
  runVoicing(v, L, R, n) {
    for (let ch = 0; ch < 2; ch++) {
      const d = ch ? R : L, comb = v.comb[ch];
      for (let i = 0; i < n; i++) {
        let x = d[i];
        if (v.combGain !== 0) { comb.push(x); x += v.combGain * comb.readInt(v.combDelay); }
        for (let k = 0; k < v.count; k++) x = v.f[k].process(x, ch);
        d[i] = x;
      }
    }
  }
  setState(c) {
    this.level.setTarget(dbToGain(c.levelDb)); this.mixS.setTarget(c.mix); this.en.setTarget(c.on ? 1 : 0);
    if (c.mic !== this.mic || c.lowCut !== this.lowCut || c.highCut !== this.highCut) { this.mic = c.mic; this.lowCut = c.lowCut; this.highCut = c.highCut; this.dirty = true; }
    if (c.type !== this.type && this.fadeFrom < 0) {
      const t = this.prev; this.prev = this.cur; this.cur = t;
      this.fadeFrom = this.type; this.type = c.type; this.fadePos = 0;
      for (const b of this.cur.f) b.reset(); this.cur.comb.forEach((d) => d.reset());
      this.design(this.cur, this.type);
    }
  }
  process(L, R, n) {
    if (this.dirty) {
      this.dirty = false; const tmp = this.voicing(); this.design(tmp, this.type);
      for (let k = 0; k < 8; k++) this.cur.f[k].copyCoeffs(tmp.f[k]);
      this.cur.count = tmp.count; this.cur.combGain = tmp.combGain; this.cur.combDelay = tmp.combDelay;
      this.lc.highpass(this.sr, this.lowCut, 0.7); this.hc.lowpass(this.sr, this.highCut, 0.7);
    }
    this.dryL.set(L.subarray(0, n)); this.dryR.set(R.subarray(0, n));
    const fading = this.fadeFrom >= 0;
    if (fading) { this.tL.set(L.subarray(0, n)); this.tR.set(R.subarray(0, n)); }
    this.runVoicing(this.cur, L, R, n);
    if (fading) {
      this.runVoicing(this.prev, this.tL, this.tR, n);
      const inc = 1 / (0.02 * this.sr);
      for (let i = 0; i < n; i++) {
        this.fadePos = Math.min(1, this.fadePos + inc);
        const a = Math.sin(this.fadePos * PI / 2), b = Math.cos(this.fadePos * PI / 2);
        L[i] = L[i] * a + this.tL[i] * b; R[i] = R[i] * a + this.tR[i] * b;
      }
      if (this.fadePos >= 1) this.fadeFrom = -1;
    }
    for (let i = 0; i < n; i++) {
      const lv = this.level.next(), mx = this.mixS.next(), e = this.en.next(), dl = this.dryL[i], dr = this.dryR[i];
      const wl = this.hc.process(this.lc.process(L[i], 0), 0) * lv, wr = this.hc.process(this.lc.process(R[i], 1), 1) * lv;
      L[i] = dl + ((dl + (wl - dl) * mx) - dl) * e; R[i] = dr + ((dr + (wr - dr) * mx) - dr) * e;
    }
  }
}

// ============================================================================ slots
class Slot {
  constructor(sr, maxBlock) {
    this.sr = sr; this.maxBlock = maxBlock; this.cur = null; this.curType = 0; this.fading = null; this.fadePos = 1;
    this.mixS = new Smoother(); this.mixS.prepare(sr, 20); this.en = new Smoother(); this.en.prepare(sr, 8);
    this.dryL = new Float32Array(maxBlock); this.dryR = new Float32Array(maxBlock); this.fL = new Float32Array(maxBlock); this.fR = new Float32Array(maxBlock);
    this.values = []; this.silent = 0; this.needsReset = false; this.hasMix = false; this.fadeMix = 1;
  }
  make(type, values) {
    const C = EFFECTS[type]; if (!C) return null;
    const e = new C(); e.sr = this.sr; e.prepare(this.sr, this.maxBlock);
    values.forEach((v, i) => e.setParameter(i, v)); e.reset();
    return e;
  }
  setState(s, info) {
    if (s.type !== this.curType) {
      this.fading = this.cur; this.fadeMix = this.hasMix ? this.mixS.current : 1;
      this.cur = this.make(s.type, s.values); this.curType = s.type; this.values = s.values.slice();
      this.fadePos = this.fading ? 0 : 1; this.needsReset = false;
    } else if (this.cur) {
      for (let i = 0; i < s.values.length; i++) if (s.values[i] !== this.values[i]) { this.values[i] = s.values[i]; this.cur.setParameter(i, s.values[i]); }
    }
    this.hasMix = !!(info && info.hasMix);
    this.mixS.setTarget(this.cur && this.hasMix ? s.mix : 1);
    this.en.setTarget(s.on ? 1 : 0); this.on = s.on;
  }
  process(L, R, n, ctx) {
    if (!this.cur && !this.fading) return;
    const tail = this.cur && this.cur.hasTail();
    if (!this.on && this.en.current < 1e-4 && !this.fading) {
      if (!tail || this.silent > this.sr) { this.needsReset = true; this.en.snapTo(0); return; }
    }
    if (this.needsReset && this.on) { if (this.cur) this.cur.reset(); this.needsReset = false; this.silent = 0; }
    this.dryL.set(L.subarray(0, n)); this.dryR.set(R.subarray(0, n));
    if (this.fading) { this.fL.set(L.subarray(0, n)); this.fR.set(R.subarray(0, n)); }
    if (this.cur) {
      if (tail) { const r = Object.assign(new Smoother(), this.en); for (let i = 0; i < n; i++) { const g = r.next(); L[i] *= g; R[i] *= g; } }
      this.cur.process(L, R, n, ctx);
    }
    let peak = 0;
    for (let i = 0; i < n; i++) {
      const m = this.mixS.next(), e = this.en.next(), d0 = this.dryL[i], d1 = this.dryR[i];
      if (!this.cur) { L[i] = d0; R[i] = d1; continue; }
      if (tail) { peak = Math.max(peak, Math.abs(L[i]), Math.abs(R[i])); L[i] = d0 * (1 - m * e) + L[i] * m; R[i] = d1 * (1 - m * e) + R[i] * m; }
      else { L[i] = d0 + (L[i] - d0) * m * e; R[i] = d1 + (R[i] - d1) * m * e; }
    }
    if (this.fading) {
      this.fading.process(this.fL, this.fR, n, ctx);
      const inc = 1 / (0.015 * this.sr), fm = this.fadeMix;
      for (let i = 0; i < n; i++) {
        this.fadePos = Math.min(1, this.fadePos + inc);
        const a = Math.sin(this.fadePos * PI / 2), b = Math.cos(this.fadePos * PI / 2), d0 = this.dryL[i], d1 = this.dryR[i];
        L[i] = L[i] * a + (d0 + (this.fL[i] - d0) * fm) * b; R[i] = R[i] * a + (d1 + (this.fR[i] - d1) * fm) * b;
      }
      if (this.fadePos >= 1) this.fading = null;
    }
    this.silent = tail && !this.on ? (peak < 1e-5 ? this.silent + n : 0) : 0;
  }
  resetAll() { if (this.cur) this.cur.reset(); this.fading = null; }
}

// ============================================================================ engine / processor
class PedalFasterProcessor extends AudioWorkletProcessor {
  constructor(opts) {
    super();
    const sr = sampleRate, mb = 128;
    this.sr = sr; this.mb = mb; this.state = null; this.registry = (opts.processorOptions || {}).registry || [];
    this.slots = Array.from({ length: 8 }, () => new Slot(sr, mb));
    this.amp = new AmpSection(); this.amp.prepare(sr, mb);
    this.cab = new Cabinet(); this.cab.prepare(sr, mb);
    this.gate = new NoiseGate(); this.gate.sr = sr; this.gate.prepare(sr, mb);
    this.gate.setParameter(1, 0.5); this.gate.setParameter(2, 30); this.gate.setParameter(4, 6); this.gate.setParameter(5, -80);
    this.gateMix = new Smoother(); this.inGain = new Smoother(); this.outGain = new Smoother(); this.master = new Smoother(); this.eqMix = new Smoother(); this.bypass = new Smoother();
    for (const s of [this.gateMix, this.inGain, this.outGain, this.master, this.eqMix, this.bypass]) s.prepare(sr, 20);
    this.eqL = new Biquad(); this.eqM = new Biquad(); this.eqH = new Biquad(); this.eqKey = '';
    this.limGain = 1; this.limRel = Math.exp(-1 / (0.12 * sr));
    this.L = new Float32Array(mb); this.R = new Float32Array(mb); this.dL = new Float32Array(mb); this.dR = new Float32Array(mb); this.gL = new Float32Array(mb); this.gR = new Float32Array(mb);
    this.inPeak = 0; this.outPeak = 0; this.minGain = 1; this.frames = 0;
    this.order = [0, 1, 2, 3, 4, 5, 6, 7]; this.numPre = 4; this.pendingOrder = null; this.duck = 1; this.duckState = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
    const initial = (opts.processorOptions || {}).state;
    if (initial) this.onMessage({ type: 'state', state: initial });
  }
  onMessage(m) {
    if (m.type !== 'state') return;
    const s = m.state; this.state = s;
    s.slots.forEach((ss, i) => this.slots[i].setState(ss, this.registry[ss.type]));
    this.amp.setState(s.amp, s.quality); this.cab.setState(s.cab);
    this.gate.setParameter(0, s.gate.thresholdDb); this.gate.setParameter(3, s.gate.releaseMs);
    const key = s.routing.order.join(',') + '|' + s.routing.numPre;
    if (key !== this.orderKey) { if (this.orderKey === undefined) { this.order = s.routing.order.slice(); this.numPre = s.routing.numPre; } else { this.pendingOrder = s.routing; this.duckState = 1; } this.orderKey = key; }
  }
  process(inputs, outputs) {
    const out = outputs[0]; if (!out || out.length === 0) return true;
    const n = out[0].length, s = this.state, L = this.L, R = this.R;
    const inp = inputs[0] || [];
    if (inp.length > 0) { L.set(inp[0]); R.set(inp.length > 1 ? inp[1] : inp[0]); } else { L.fill(0); R.fill(0); }
    if (!s) { out[0].set(L); if (out[1]) out[1].set(R); return true; }
    const ctx = { bpm: s.tempo, quality: s.quality };
    if (s.inMode === 1) R.set(L); else if (s.inMode === 2) L.set(R);
    else if (s.inMode === 3) for (let i = 0; i < n; i++) L[i] = R[i] = 0.5 * (L[i] + R[i]);

    // input
    this.inGain.setTarget(dbToGain(s.inGainDb));
    let pk = 0;
    for (let i = 0; i < n; i++) { const g = this.inGain.next(); L[i] *= g; R[i] *= g; pk = Math.max(pk, Math.abs(L[i]), Math.abs(R[i])); }
    this.inPeak = Math.max(this.inPeak, pk);
    this.dL.set(L); this.dR.set(R);

    // gate
    this.gateMix.setTarget(s.gate.on ? 1 : 0);
    if (s.gate.on || this.gateMix.current > 1e-4) {
      this.gL.set(L); this.gR.set(R); this.gate.process(L, R, n);
      for (let i = 0; i < n; i++) { const g = this.gateMix.next(); L[i] = this.gL[i] + (L[i] - this.gL[i]) * g; R[i] = this.gR[i] + (R[i] - this.gR[i]) * g; }
    }

    // chain
    for (let p = 0; p < this.numPre; p++) this.slots[this.order[p]].process(L, R, n, ctx);
    this.amp.process(L, R, n);
    this.cab.process(L, R, n);
    for (let p = this.numPre; p < 8; p++) this.slots[this.order[p]].process(L, R, n, ctx);

    // master EQ, output, limiter
    const e = s.eq, key = [e.lowDb, e.midDb, e.midFreqHz, e.highDb].join();
    if (key !== this.eqKey) { this.eqKey = key; this.eqL.lowShelf(this.sr, 100, 0.7, e.lowDb); this.eqM.peak(this.sr, e.midFreqHz, 0.9, e.midDb); this.eqH.highShelf(this.sr, 6000, 0.7, e.highDb); }
    this.eqMix.setTarget(e.on ? 1 : 0); this.outGain.setTarget(dbToGain(s.outputGainDb)); this.master.setTarget(dbToGain(s.masterDb)); this.bypass.setTarget(s.bypass ? 1 : 0);
    const ceil = dbToGain(s.limiter.ceilingDb);
    let opk = 0, bad = false;
    for (let i = 0; i < n; i++) {
      let l = L[i], r = R[i];
      const em = this.eqMix.next();
      if (em > 1e-4) { const el = this.eqH.process(this.eqM.process(this.eqL.process(l, 0), 0), 0), er = this.eqH.process(this.eqM.process(this.eqL.process(r, 1), 1), 1); l += (el - l) * em; r += (er - r) * em; }
      const og = this.outGain.next(); l *= og; r *= og;
      if (s.limiter.enabled) {
        const p = Math.max(Math.abs(l), Math.abs(r)), t = p > ceil ? ceil / p : 1;
        this.limGain = t < this.limGain ? t : t + this.limRel * (this.limGain - t);
        l = clamp(l * this.limGain, -ceil, ceil); r = clamp(r * this.limGain, -ceil, ceil); this.minGain = Math.min(this.minGain, this.limGain);
      }
      const mg = this.master.next(); l *= mg; r *= mg;
      if (s.outMode === 1) { const m = 0.5 * (l + r); l = r = m; }
      const bm = this.bypass.next();
      if (bm > 1e-4) { l += (this.dL[i] - l) * bm; r += (this.dR[i] - r) * bm; }
      if (this.duckState === 1) { this.duck -= 1 / 128; if (this.duck <= 0) { this.duck = 0; this.duckState = 2; this.order = this.pendingOrder.order.slice(); this.numPre = this.pendingOrder.numPre; } }
      else if (this.duckState === 2) { this.duck += 1 / 128; if (this.duck >= 1) { this.duck = 1; this.duckState = 0; } }
      l *= this.duck; r *= this.duck;
      if (!Number.isFinite(l) || !Number.isFinite(r)) { bad = true; l = r = 0; }
      L[i] = clamp(l, -4, 4); R[i] = clamp(r, -4, 4);
      opk = Math.max(opk, Math.abs(L[i]), Math.abs(R[i]));
    }
    if (bad) { for (const sl of this.slots) sl.resetAll(); this.amp.models.forEach((set) => set.forEach((m) => m.reset())); L.fill(0); R.fill(0); }
    this.outPeak = Math.max(this.outPeak, opk);
    out[0].set(L.subarray(0, n)); if (out[1]) out[1].set(R.subarray(0, n));

    this.frames += n;
    if (this.frames >= this.sr / 30) {
      this.frames = 0;
      this.port.postMessage({ type: 'meters', inPeak: this.inPeak, outPeak: this.outPeak, limGrDb: -gainToDb(this.minGain) });
      this.inPeak = this.outPeak = 0; this.minGain = 1;
    }
    return true;
  }
}

registerProcessor('pedal-faster', PedalFasterProcessor);
