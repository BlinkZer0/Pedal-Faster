// Parity check for the web DSP port: renders factory presets through the JavaScript engine with the
// C++ reference DI and compares output levels and band profiles with tests/regression/baseline.json.
// Injected into a copy of web/index.html by scripts/test-web.ps1 and run in headless Chrome.
(async () => {
  const out = document.createElement('pre');
  out.id = 'parity-result';
  try {
    const di = new Float32Array(await (await fetch('ref-di.f32')).arrayBuffer());
    const base = await (await fetch('baseline.json')).json();
    const src = document.getElementById('worklet-src').textContent;
    let Cls = null;
    class AWP { constructor() { this.port = { postMessage() {}, onmessage: null }; } }
    new Function('AudioWorkletProcessor', 'registerProcessor', 'sampleRate', src)(AWP, (n, c) => (Cls = c), 48000);
    const registry = REG.map((r) => ({ hasMix: r.hasMix }));
    const limit = +(new URLSearchParams(location.search).get('n') || 0);
    const presets = limit ? D.presets.slice(0, limit) : D.presets;

    const sr = 48000, centres = [60, 90, 130, 180, 250, 350, 500, 700, 1000, 1400, 2000, 2800, 4000, 5600, 8000, 11000];
    function profile(x, start) {   // same Goertzel band profile as tests/TestUtils.h
      const len = Math.min(x.length - start, sr), res = [];
      for (const f of centres) {
        let power = 0;
        for (const m of [0.85, 1, 1.15]) {
          const w = 2 * Math.PI * f * m / sr, coeff = 2 * Math.cos(w);
          let s1 = 0, s2 = 0;
          for (let i = 0; i < len; i++) { const win = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (len - 1)); const s0 = x[start + i] * win + coeff * s1 - s2; s2 = s1; s1 = s0; }
          power += s1 * s1 + s2 * s2 - coeff * s1 * s2;
        }
        res.push(10 * Math.log10(power + 1e-12));
      }
      return res;
    }
    const shapeDist = (a, b) => {
      const ma = a.reduce((s, v) => s + v, 0) / a.length, mb = b.reduce((s, v) => s + v, 0) / b.length;
      return a.reduce((s, v, i) => s + Math.abs((v - ma) - (b[i] - mb)), 0) / a.length;
    };

    const rows = [];
    const t0 = performance.now();
    for (const p of presets) {
      applyPreset(p);
      rig.p.master_vol = 0; // the web page defaults Master to -6 dB for safety; the C++ baseline uses 0 dB
      const proc = new Cls({ processorOptions: { registry } });
      proc.port.onmessage({ data: { type: 'state', state: effectiveState() } });
      const n = di.length - (di.length % 128), L = new Float32Array(n), inL = new Float32Array(128), inR = new Float32Array(128), oL = new Float32Array(128), oR = new Float32Array(128);
      let acc = 0, cnt = 0, finite = true;
      for (let pos = 0; pos < n; pos += 128) {
        inL.set(di.subarray(pos, pos + 128)); inR.set(inL);
        proc.process([[inL, inR]], [[oL, oR]]);
        L.set(oL, pos);
        for (let i = 0; i < 128; i++) {
          if (!Number.isFinite(oL[i]) || !Number.isFinite(oR[i])) finite = false;
          if (pos + i >= 4800) { acc += oL[i] * oL[i] + oR[i] * oR[i]; cnt += 2; }
        }
      }
      const level = 10 * Math.log10(Math.max(1e-20, acc / cnt));
      const ref = base[p.id];
      const prof = profile(L, Math.floor(0.2 * sr));
      rows.push({ id: p.id, level: +level.toFixed(2), ref: ref ? +ref[0].toFixed(2) : null, dLevel: ref ? +(level - ref[0]).toFixed(2) : null,
        shape: ref ? +shapeDist(prof, ref.slice(1)).toFixed(2) : null, finite });
    }
    const dl = rows.filter((r) => r.dLevel != null).map((r) => Math.abs(r.dLevel)), sh = rows.filter((r) => r.shape != null).map((r) => r.shape);
    const med = (a) => { const s = a.slice().sort((x, y) => x - y); return s[Math.floor(s.length / 2)]; };
    out.textContent = JSON.stringify({
      presets: rows.length, seconds: +((performance.now() - t0) / 1000).toFixed(1),
      realtimeFactor: +((rows.length * di.length / sr) / ((performance.now() - t0) / 1000)).toFixed(1),
      nonFinite: rows.filter((r) => !r.finite).length,
      levelDiffMedian: med(dl), levelDiffMax: Math.max(...dl), within1_5dB: dl.filter((x) => x <= 1.5).length, within3dB: dl.filter((x) => x <= 3).length,
      shapeMedian: med(sh), shapeMax: Math.max(...sh),
      worst: rows.slice().sort((a, b) => Math.abs(b.dLevel) - Math.abs(a.dLevel)).slice(0, 12),
      worstShape: rows.slice().sort((a, b) => b.shape - a.shape).slice(0, 8),
    }, null, 1);
  } catch (e) { out.textContent = 'ERROR ' + (e && e.stack || e); }
  document.body.append(out);
  document.title = 'PARITY-DONE';
})();
