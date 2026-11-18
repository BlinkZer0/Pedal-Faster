// Pedal Faster web preview - UI, rig state, presets and audio routing.
// Mirrors the plugin's processor logic (src/PluginProcessor.cpp): preset application, routing,
// derived macros and capture to the same JSON preset format (presets round-trip with the VST3).
'use strict';

const D = RIG_DATA, REG = D.effects;
const $ = (id) => document.getElementById(id);
const el = (tag, cls, txt) => { const e = document.createElement(tag); if (cls) e.className = cls; if (txt != null) e.textContent = txt; return e; };
const clamp = (x, lo, hi) => Math.min(hi, Math.max(lo, x));
const CAT_COLOURS = { Dynamics: '#4fa3d9', Gain: '#e8743b', Experimental: '#d65db1', Filter: '#a47de0', EQ: '#9aa7b4', Modulation: '#4f7fe0', Pitch: '#e06b8f', Delay: '#5cbf6e', Reverb: '#3fc6d0' };
const MACROS = ['Drive', 'Tone', 'Space', 'Output'];

// ============================================================================ parameter specs
const Spec = {
  toReal(p, n) {
    n = clamp(n, 0, 1);
    if (p.choices) return Math.min(p.choices.length - 1, Math.round(n * (p.choices.length - 1)));
    let v = p.log ? p.min * Math.pow(p.max / p.min, n) : p.min + (p.max - p.min) * n;
    if (p.int) v = Math.round(v);
    return clamp(v, p.min, p.max);
  },
  toNorm(p, v) {
    if (p.choices) return p.choices.length > 1 ? clamp(v, 0, p.choices.length - 1) / (p.choices.length - 1) : 0;
    v = clamp(v, p.min, p.max);
    return p.log ? clamp(Math.log(v / p.min) / Math.log(p.max / p.min), 0, 1) : (p.max > p.min ? (v - p.min) / (p.max - p.min) : 0);
  },
  format(p, v) {
    if (p.choices) return p.choices[clamp(Math.round(v), 0, p.choices.length - 1)];
    const u = p.unit ? ' ' + p.unit : '';
    if (p.unit === '%') return Math.round(v * 100) + '%';
    if (p.unit === 'Hz' && v >= 1000) return (v / 1000).toFixed(2) + ' kHz';
    if (p.int || Math.abs(v) >= 100) return Math.round(v) + u;
    if (Math.abs(v) >= 10) return v.toFixed(1) + u;
    return v.toFixed(2) + u;
  },
};
const P = (name, min, max, def, unit = '', log = false) => ({ name, min, max, def, unit, log });
const GLOBAL = {
  in_gain: P('Input', -24, 24, 0, 'dB'), gate_thresh: P('Gate', -90, -20, -60, 'dB'), gate_release: P('Gate Release', 5, 500, 80, 'ms', true),
  amp_gain: P('Gain', 0, 1, 0.4, '%'), amp_bass: P('Bass', 0, 1, 0.5, '%'), amp_mid: P('Middle', 0, 1, 0.5, '%'), amp_treble: P('Treble', 0, 1, 0.5, '%'),
  amp_presence: P('Presence', 0, 1, 0.5, '%'), amp_master: P('Master', 0, 1, 0.5, '%'), amp_sag: P('Sag', 0, 1, 0.3, '%'), amp_trim: P('Output Trim', -24, 12, 0, 'dB'),
  cab_mic: P('Mic Position', 0, 1, 0.3, '%'), cab_lowcut: P('Low Cut', 20, 400, 70, 'Hz', true), cab_highcut: P('High Cut', 2000, 20000, 9000, 'Hz', true),
  cab_level: P('Level', -24, 12, 0, 'dB'), cab_mix: P('Wet/Dry', 0, 1, 1, '%'),
  eq_low: P('Low', -12, 12, 0, 'dB'), eq_mid: P('Mid', -12, 12, 0, 'dB'), eq_midfreq: P('Mid Freq', 200, 5000, 800, 'Hz', true), eq_high: P('High', -12, 12, 0, 'dB'),
  out_gain: P('Level', -24, 12, 0, 'dB'), lim_ceiling: P('Ceiling', -12, 0, -0.5, 'dB'), master_vol: P('Master', -60, 6, -6, 'dB'), tempo: P('Tempo', 40, 240, 120, 'BPM'),
  macro_drive: P('Drive', 0, 1, 0.5, '%'), macro_tone: P('Tone', 0, 1, 0.5, '%'), macro_space: P('Space', 0, 1, 0.5, '%'), macro_output: P('Output', 0, 1, 0.5, '%'),
};
const MACRO_IDS = ['macro_drive', 'macro_tone', 'macro_space', 'macro_output'];

// ============================================================================ rig state
const emptySlot = () => ({ type: 0, on: true, mix: 1, values: [] });
const rig = {
  p: Object.fromEntries(Object.entries(GLOBAL).map(([k, d]) => [k, d.def])),
  ampOn: true, ampType: 0, cabOn: true, cabType: 2, gateOn: false, eqOn: true, limOn: true, bypass: false, outMode: 0, inMode: 0, quality: 1,
  slots: Array.from({ length: 8 }, emptySlot), routing: { order: [0, 1, 2, 3, 4, 5, 6, 7], numPre: 4 }, macroMaps: [],
  presetId: '', presetName: 'Init', dirty: false,
};
let selected = 'amp';

const info = (type) => REG[type] || REG[0];
const defaultsFor = (type) => info(type).params.map((p) => p.def);

function setSlotType(s, type) {
  rig.slots[s] = { type, on: true, mix: info(type).defaultMix, values: defaultsFor(type) };
  changed(true);
}

function moveSlot(slot, newPos, pre) {
  const r = rig.routing, preL = [], postL = [];
  r.order.forEach((s, i) => { if (s !== slot) (i < r.numPre ? preL : postL).push(s); });
  const t = pre ? preL : postL;
  t.splice(clamp(newPos, 0, t.length), 0, slot);
  rig.routing = { order: [...preL, ...postL], numPre: preL.length };
  changed(true);
}
const isPre = (slot) => rig.routing.order.indexOf(slot) < rig.routing.numPre;

// ---------------------------------------------------------------- presets (port of PedalFasterProcessor::applyPreset)
function deriveMacros(p) {
  const m = [], add = (macro, target, amount) => m.push({ macro, target, amount });
  if (p.amp.enabled) add(0, 'amp_gain', 0.25);
  const keyFor = (t) => ({ transparent_od: 'drive', mid_overdrive: 'drive', octave_fuzz: 'drive', hard_distortion: 'gain', high_gain_distortion: 'gain', fuzz: 'sustain', clean_boost: 'gainDb' })[t];
  const gi = p.effects.findIndex((e) => keyFor(e.type));
  if (gi >= 0) add(0, `fx:${gi}:${keyFor(p.effects[gi].type)}`, 0.25);
  if (!p.amp.enabled) { const ci = p.effects.findIndex((e) => e.type === 'compressor'); if (ci >= 0) add(0, `fx:${ci}:thresholdDb`, -0.3); }
  if (p.amp.enabled) { add(1, 'amp_treble', 0.2); add(1, 'amp_presence', 0.15); } else add(1, 'eq_high', 0.2);
  add(1, 'cab_highcut', 0.15);
  let space = false;
  p.effects.forEach((e, i) => { const f = REG.find((r) => r.id === e.type); if (f && (f.category === 'Delay' || f.category === 'Reverb')) { add(2, `fx:${i}:mix`, 0.3); space = true; } });
  if (!space) p.effects.forEach((e, i) => { const f = REG.find((r) => r.id === e.type); if (f && f.category === 'Modulation' && f.hasMix) add(2, `fx:${i}:mix`, 0.3); });
  add(3, 'out_gain', 0.25);
  return m;
}

function applyPreset(pr) {
  const R = (id, v) => { if (v != null && Number.isFinite(v)) rig.p[id] = clamp(v, GLOBAL[id].min, GLOBAL[id].max); };
  R('in_gain', pr.inputGainDb ?? 0);
  rig.gateOn = !!pr.gate?.enabled; R('gate_thresh', pr.gate?.thresholdDb ?? -60); R('gate_release', pr.gate?.releaseMs ?? 80);
  const a = pr.amp || {}, ap = a.parameters || {};
  rig.ampOn = a.enabled !== false; rig.ampType = Math.max(0, D.ampIds.indexOf(a.type));
  for (const k of ['gain', 'bass', 'mid', 'treble', 'presence', 'master', 'sag']) R('amp_' + k, ap[k] ?? GLOBAL['amp_' + k].def);
  R('amp_trim', ap.trimDb ?? 0);
  const c = pr.cabinet || {};
  rig.cabOn = c.enabled !== false; rig.cabType = Math.max(0, D.cabIds.indexOf(c.type));
  if (rig.cabType === 6) { rig.cabType = 2; toast('Custom impulse responses are plugin-only - using the 4x12 cabinet'); }
  R('cab_mic', c.mic ?? 0.3); R('cab_lowcut', c.lowCutHz ?? 70); R('cab_highcut', c.highCutHz ?? 9000); R('cab_level', c.levelDb ?? 0); R('cab_mix', c.mix ?? 1);
  const e = pr.masterEq || {};
  rig.eqOn = e.enabled !== false; R('eq_low', e.lowDb ?? 0); R('eq_mid', e.midDb ?? 0); R('eq_midfreq', e.midFreqHz ?? 800); R('eq_high', e.highDb ?? 0);
  R('out_gain', pr.outputGainDb ?? 0); rig.limOn = pr.limiter?.enabled !== false; R('lim_ceiling', pr.limiter?.ceilingDb ?? -0.5);
  if (pr.tempoBpm > 0) R('tempo', pr.tempoBpm);
  MACRO_IDS.forEach((m) => (rig.p[m] = 0.5));

  const fx = (pr.effects || []).slice(0, 8);
  let slotOf = fx.map((f) => { const m = /^slot-(\d)$/.exec(f.slotId || ''); return m ? +m[1] - 1 : -1; });
  if (slotOf.some((s, i) => s < 0 || s > 7 || slotOf.indexOf(s) !== i)) slotOf = fx.map((_, i) => i);
  rig.slots = Array.from({ length: 8 }, emptySlot);
  fx.forEach((f, i) => {
    const t = REG.findIndex((r) => r.id === f.type); if (t <= 0) return;
    const inf = REG[t];
    rig.slots[slotOf[i]] = { type: t, on: f.enabled !== false, mix: f.mix != null && f.mix >= 0 ? f.mix : inf.defaultMix,
      values: inf.params.map((p) => { const v = f.parameters?.[p.key]; return Spec.toReal(p, Spec.toNorm(p, Number.isFinite(v) ? v : p.def)); }) };
  });
  const pre = [], post = [], empty = [];
  fx.forEach((f, i) => (f.position === 'post' ? post : pre).push(slotOf[i]));
  for (let s = 0; s < 8; s++) if (!slotOf.includes(s) || slotOf.indexOf(s) >= fx.length) empty.push(s);
  const ep = Math.ceil(empty.length / 2);
  empty.forEach((s, i) => (i < ep ? pre : post).push(s));
  rig.routing = { order: [...pre, ...post], numPre: pre.length };

  rig.macroMaps = [];
  for (const m of (pr.macros && pr.macros.length ? pr.macros.map((x) => ({ macro: typeof x.macro === 'string' ? ['drive', 'tone', 'space', 'output'].indexOf(x.macro) : x.macro, target: x.target, amount: x.amount })) : deriveMacros(pr))) {
    if (m.target.startsWith('fx:')) {
      const [, ei, key] = m.target.split(':');
      if (+ei < fx.length) rig.macroMaps.push({ macro: m.macro, target: `slot:${slotOf[+ei]}:${key}`, amount: m.amount });
    } else if (GLOBAL[m.target]) rig.macroMaps.push(m);
  }
  rig.presetId = pr.id; rig.presetName = pr.name; rig.dirty = false;
  if (!['amp', 'cab', 'master'].includes(selected) && rig.slots[selected].type === 0) selected = 'amp';
  changed(true, false);
  if ($('bottom').childElementCount) renderBottom();
}

function captureState() {
  const src = findPreset(rig.presetId);
  const v = (id) => rig.p[id];
  const effects = [];
  const listIdx = {};
  rig.routing.order.forEach((s, pos) => {
    const sl = rig.slots[s]; if (sl.type <= 0) return;
    const inf = info(sl.type), params = {};
    inf.params.forEach((p, k) => (params[p.key] = sl.values[k]));
    listIdx[s] = effects.length;
    const e = { slotId: `slot-${s + 1}`, type: inf.id, position: pos < rig.routing.numPre ? 'pre' : 'post', enabled: sl.on, parameters: params };
    if (inf.hasMix) e.mix = sl.mix;
    effects.push(e);
  });
  const macros = [];
  for (const m of rig.macroMaps) {
    let t = m.target;
    if (t.startsWith('slot:')) { const [, s, key] = t.split(':'); if (listIdx[+s] == null) continue; t = `fx:${listIdx[+s]}:${key}`; }
    macros.push({ macro: ['drive', 'tone', 'space', 'output'][m.macro], target: t, amount: m.amount });
  }
  return {
    schemaVersion: 1, id: rig.presetId, name: rig.presetName, category: src?.category || 'User', description: src?.description || '', author: 'Web preview', tags: src?.tags || [],
    inputGainDb: v('in_gain'), gate: { enabled: rig.gateOn, thresholdDb: v('gate_thresh'), releaseMs: v('gate_release') },
    amp: { type: D.ampIds[rig.ampType], enabled: rig.ampOn, parameters: { gain: v('amp_gain'), bass: v('amp_bass'), mid: v('amp_mid'), treble: v('amp_treble'), presence: v('amp_presence'), master: v('amp_master'), sag: v('amp_sag'), trimDb: v('amp_trim') } },
    cabinet: { type: D.cabIds[rig.cabType], enabled: rig.cabOn, mic: v('cab_mic'), lowCutHz: v('cab_lowcut'), highCutHz: v('cab_highcut'), levelDb: v('cab_level'), mix: v('cab_mix') },
    effects, masterEq: { enabled: rig.eqOn, lowDb: v('eq_low'), midDb: v('eq_mid'), midFreqHz: v('eq_midfreq'), highDb: v('eq_high') },
    limiter: { enabled: rig.limOn, ceilingDb: v('lim_ceiling') }, macros, ...(src?.tempoBpm ? { tempoBpm: v('tempo') } : {}), outputGainDb: v('out_gain'),
  };
}

/** Effective state for the DSP: base values with the macro offsets applied in the normalised domain. */
function effectiveState() {
  const p = { ...rig.p }, slots = rig.slots.map((s) => ({ type: s.type, on: s.on, mix: s.mix, values: s.values.slice() }));
  for (const m of rig.macroMaps) {
    const off = (rig.p[MACRO_IDS[m.macro]] - 0.5) * 2 * m.amount;
    if (Math.abs(off) < 1e-6) continue;
    if (m.target.startsWith('slot:')) {
      const [, s, key] = m.target.split(':'), sl = slots[+s];
      if (!sl || sl.type <= 0) continue;
      if (key === 'mix') { sl.mix = clamp(sl.mix + off, 0, 1); continue; }
      const k = info(sl.type).params.findIndex((q) => q.key === key);
      if (k >= 0) { const sp = info(sl.type).params[k]; sl.values[k] = Spec.toReal(sp, Spec.toNorm(sp, sl.values[k]) + off); }
    } else if (GLOBAL[m.target]) { const g = GLOBAL[m.target]; p[m.target] = Spec.toReal(g, Spec.toNorm(g, p[m.target]) + off); }
  }
  return {
    inGainDb: p.in_gain, inMode: rig.inMode, bypass: rig.bypass, quality: rig.quality, tempo: p.tempo, outMode: rig.outMode,
    gate: { on: rig.gateOn, thresholdDb: p.gate_thresh, releaseMs: p.gate_release },
    amp: { on: rig.ampOn, type: rig.ampType, gain: p.amp_gain, bass: p.amp_bass, mid: p.amp_mid, treble: p.amp_treble, presence: p.amp_presence, master: p.amp_master, sag: p.amp_sag, trimDb: p.amp_trim },
    cab: { on: rig.cabOn, type: rig.cabType, mic: p.cab_mic, lowCut: p.cab_lowcut, highCut: p.cab_highcut, levelDb: p.cab_level, mix: p.cab_mix },
    eq: { on: rig.eqOn, lowDb: p.eq_low, midDb: p.eq_mid, midFreqHz: p.eq_midfreq, highDb: p.eq_high },
    outputGainDb: p.out_gain, limiter: { enabled: rig.limOn, ceilingDb: p.lim_ceiling }, masterDb: p.master_vol,
    routing: rig.routing, slots,
  };
}

// ============================================================================ preset library
const store = {
  get(k, d) { try { return JSON.parse(localStorage.getItem(k)) ?? d; } catch { return d; } },
  set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch { /* storage unavailable */ } },
};
let userPresets = store.get('pf.user', []);
const favs = new Set(store.get('pf.favs', []));
const allPresets = () => [...D.presets, ...userPresets];
const findPreset = (id) => allPresets().find((p) => p.id === id);
const bankIndex = (p) => { const i = D.banks.findIndex((b) => b.name === p.category); return i < 0 ? 999 : i; };

function gainLevel(p) {
  if (p.gainLevel != null) return p.gainLevel;
  return 0;
}

function query() {
  const words = $('search').value.toLowerCase().split(/\s+/).filter(Boolean), cat = $('category').value, src = $('source').value;
  let list = (src === 'factory' ? D.presets : src === 'user' ? userPresets : allPresets()).filter((p) => {
    if (cat && p.category !== cat) return false;
    if (src === 'fav' && !favs.has(p.id)) return false;
    const hay = [p.name, p.category, p.description, ...(p.tags || [])].join(' ').toLowerCase();
    return words.every((w) => hay.includes(w));
  });
  const s = $('sort').value;
  if (s === 'bank') list = list.slice().sort((a, b) => bankIndex(a) - bankIndex(b));
  if (s === 'name') list = list.slice().sort((a, b) => a.name.localeCompare(b.name));
  if (s === 'cat') list = list.slice().sort((a, b) => a.category.localeCompare(b.category) || a.name.localeCompare(b.name));
  if (s === 'gain') list = list.slice().sort((a, b) => gainLevel(a) - gainLevel(b));
  return list;
}

function renderBrowser() {
  const list = query(), box = $('list');
  box.textContent = '';
  $('count').textContent = `${list.length} presets`;
  for (const p of list) {
    const r = el('div', 'row' + (p.id === rig.presetId ? ' current' : ''));
    r.setAttribute('role', 'option'); r.tabIndex = 0;
    const t = el('div', 't');
    t.append(el('b', null, (favs.has(p.id) ? '★ ' : '') + p.name), el('span', 'cat' + (p.id.startsWith('user.') ? ' user' : ''), (p.id.startsWith('user.') ? 'USER  ' : '') + p.category));
    r.append(t, el('div', 'd', p.description || ''), el('div', 'tags', (p.tags || []).map((x) => '#' + x).join('  ') + `  gain ${gainLevel(p)}`));
    r.onclick = () => applyPreset(p);
    r.onkeydown = (e) => { if (e.key === 'Enter') applyPreset(p); };
    box.append(r);
    if (p.id === rig.presetId) requestAnimationFrame(() => r.scrollIntoView({ block: 'nearest' }));
  }
}

function step(delta) {
  const list = query().length ? query() : allPresets();
  const i = list.findIndex((p) => p.id === rig.presetId);
  applyPreset(list[i < 0 ? (delta > 0 ? 0 : list.length - 1) : (i + delta + list.length) % list.length]);
}

// ============================================================================ knob control
function knob({ label, small, accent = '#2bbfb0', get, set, spec, title }) {
  const root = el('div', 'knob' + (small ? ' sm' : ''));
  root.tabIndex = 0; root.setAttribute('role', 'slider'); root.setAttribute('aria-label', title || label);
  const cap = el('div', 'cap', label);
  const NS = 'http://www.w3.org/2000/svg', svg = document.createElementNS(NS, 'svg');
  svg.setAttribute('viewBox', '0 0 54 54');
  const mk = (t, attrs) => { const e = document.createElementNS(NS, t); for (const k in attrs) e.setAttribute(k, attrs[k]); svg.append(e); return e; };
  mk('circle', { cx: 27, cy: 27, r: 17, fill: '#272b31', stroke: '#3a4047', 'stroke-width': 1 });
  const arc = (a0, a1) => { const p = (a) => [27 + 23 * Math.cos(a), 27 + 23 * Math.sin(a)], [x0, y0] = p(a0), [x1, y1] = p(a1); return `M${x0} ${y0} A23 23 0 ${a1 - a0 > Math.PI ? 1 : 0} 1 ${x1} ${y1}`; };
  const A0 = Math.PI * 0.75, A1 = Math.PI * 2.25;
  mk('path', { d: arc(A0, A1), fill: 'none', stroke: '#2e3339', 'stroke-width': 4, 'stroke-linecap': 'round' });
  const val = mk('path', { fill: 'none', stroke: accent, 'stroke-width': 4, 'stroke-linecap': 'round' });
  const ptr = mk('line', { x1: 27, y1: 27, stroke: '#e4e7ea', 'stroke-width': 2.5, 'stroke-linecap': 'round' });
  const txt = el('div', 'val');
  root.append(cap, svg, txt);
  const norm = () => Spec.toNorm(spec, get());
  const setNorm = (n) => { set(Spec.toReal(spec, clamp(n, 0, 1))); update(); };
  function update() {
    const n = norm(), a = A0 + n * (A1 - A0);
    val.setAttribute('d', n > 0.002 ? arc(A0, a) : '');
    ptr.setAttribute('x2', 27 + 14 * Math.cos(a)); ptr.setAttribute('y2', 27 + 14 * Math.sin(a));
    const s = Spec.format(spec, get());
    txt.textContent = s; root.setAttribute('aria-valuetext', s);
  }
  let drag = null;
  svg.onpointerdown = (e) => { drag = { y: e.clientY, n: norm() }; svg.setPointerCapture(e.pointerId); root.focus(); };
  svg.onpointermove = (e) => { if (!drag) return; setNorm(drag.n + (drag.y - e.clientY) / (e.shiftKey || e.ctrlKey ? 1200 : 180)); };
  svg.onpointerup = () => (drag = null);
  svg.ondblclick = () => setNorm(Spec.toNorm(spec, spec.def));
  root.onwheel = (e) => { e.preventDefault(); setNorm(norm() - Math.sign(e.deltaY) * (e.shiftKey ? 0.004 : 0.02)); };
  root.onkeydown = (e) => {
    const d = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[e.key];
    if (d) { e.preventDefault(); e.stopPropagation(); setNorm(norm() + d * (e.shiftKey ? 0.002 : spec.choices ? 1 / (spec.choices.length - 1) : 0.01)); }
    if (e.key === 'Home') setNorm(0); if (e.key === 'End') setNorm(1);
  };
  txt.ondblclick = () => {
    const s = prompt(`${label} (${spec.unit || 'value'})`, String(+get().toFixed(3)));
    if (s == null) return;
    let v = parseFloat(s); if (!Number.isFinite(v)) return;
    if (spec.unit === '%' && v > 1) v /= 100;
    set(clamp(v, spec.min, spec.max)); update();
  };
  update();
  return root;
}
const gKnob = (id, label, accent, small = true) => knob({ label: label || GLOBAL[id].name, small, accent, spec: GLOBAL[id], get: () => rig.p[id], set: (v) => { rig.p[id] = v; changed(false); } });
const slotKnob = (s, k, small, accent) => {
  const sp = info(rig.slots[s].type).params[k];
  return knob({ label: sp.name, small, accent, spec: sp, get: () => rig.slots[s].values[k], set: (v) => { rig.slots[s].values[k] = v; changed(false); } });
};
const mixSpec = { name: 'Mix', min: 0, max: 1, def: 1, unit: '%' };
const mixKnob = (s, small, accent) => knob({ label: 'Mix', small, accent, spec: { ...mixSpec, def: info(rig.slots[s].type).defaultMix }, get: () => rig.slots[s].mix, set: (v) => { rig.slots[s].mix = v; changed(false); } });
function toggle(text, get, setv, cls = '') {
  const b = el('button', cls, text);
  const upd = () => b.classList.toggle('on', !!get());
  b.onclick = () => { setv(!get()); upd(); changed(false); };
  upd(); return b;
}
function typeSelect(value, onChange) {
  const s = el('select');
  s.append(new Option('Empty', 0));
  const cats = [...new Set(REG.slice(1).map((r) => r.category))];
  for (const c of cats) { const g = el('optgroup'); g.label = c; REG.filter((r) => r.index > 0 && r.category === c).forEach((r) => g.append(new Option(r.name, r.index))); s.append(g); }
  s.value = value; s.onchange = () => onChange(+s.value);
  return s;
}
const choiceSelect = (opts, value, onChange) => { const s = el('select'); opts.forEach((o, i) => s.append(new Option(o, i))); s.value = value; s.onchange = () => onChange(+s.value); return s; };

// ============================================================================ chain view
function pedalCard(s) {
  const sl = rig.slots[s], inf = info(sl.type), colour = CAT_COLOURS[inf.category] || '#2e3339';
  const c = el('div', 'card' + (sl.type ? '' : ' empty') + (selected === s ? ' sel' : '') + (sl.type && !sl.on ? ' off' : ''));
  if (sl.type) { const st = el('div', 'strip'); st.style.background = colour; c.append(st); }
  const head = el('div', 'head');
  head.draggable = true;
  head.ondragstart = (e) => { e.dataTransfer.setData('text/plain', 'slot:' + s); e.dataTransfer.effectAllowed = 'move'; };
  head.append(el('span', null, `${s + 1}  ${sl.type ? inf.category.toUpperCase() : 'EMPTY'}  ≡`));
  if (sl.type) head.append(toggle('ON', () => rig.slots[s].on, (v) => { rig.slots[s].on = v; renderChain(); }));
  c.append(head, typeSelect(sl.type, (t) => { setSlotType(s, t); selected = s; renderAll(); }));
  const knobs = el('div', 'knobs');
  let n = 0;
  inf.params.forEach((p, k) => { if (!p.choices && n < 3) { knobs.append(slotKnob(s, k, true, colour)); n++; } });
  if (inf.hasMix && n < 3) knobs.append(mixKnob(s, true, colour));
  c.append(knobs);
  const btns = el('div', 'btns');
  const row = () => { const pos = rig.routing.order.indexOf(s); return isPre(s) ? pos : pos - rig.routing.numPre; };
  const b = (t, title, fn) => { const x = el('button', null, t); x.title = title; x.onclick = (e) => { e.stopPropagation(); fn(); renderAll(); }; btns.append(x); };
  b('<', 'Move earlier', () => moveSlot(s, row() - 1, isPre(s)));
  b('>', 'Move later', () => moveSlot(s, row() + 1, isPre(s)));
  b('Pre/Post', 'Move between pre-amp and post-amp', () => moveSlot(s, isPre(s) ? 0 : 99, !isPre(s)));
  b('✕', 'Remove pedal', () => setSlotType(s, 0));
  c.append(btns);
  c.onclick = () => { selected = s; renderChain(); renderEditor(); };
  c.ondragover = (e) => { e.preventDefault(); c.classList.add('drop'); };
  c.ondragleave = () => c.classList.remove('drop');
  c.ondrop = (e) => {
    e.preventDefault(); e.stopPropagation();
    const src = +(e.dataTransfer.getData('text/plain').split(':')[1]);
    if (Number.isFinite(src) && src !== s) { moveSlot(src, row(), isPre(s)); renderAll(); }
  };
  return c;
}

function moduleCard(kind) {
  const amp = kind === 'amp';
  const m = el('div', `module ${kind}` + (selected === kind ? ' sel' : ''));
  const head = el('div', 'head');
  head.append(el('strong', null, amp ? 'AMPLIFIER' : 'CABINET'));
  const sel = amp ? choiceSelect(D.ampNames, rig.ampType, (v) => { rig.ampType = v; changed(false); renderEditor(); })
    : choiceSelect(D.cabNames.slice(0, 6), rig.cabType, (v) => { rig.cabType = v; changed(false); renderEditor(); });
  sel.onclick = (e) => e.stopPropagation();
  head.append(sel, toggle('ON', () => (amp ? rig.ampOn : rig.cabOn), (v) => (amp ? (rig.ampOn = v) : (rig.cabOn = v))));
  m.append(head);
  const k = el('div', 'knobs');
  const ids = amp ? ['amp_gain', 'amp_bass', 'amp_mid', 'amp_treble', 'amp_presence', 'amp_master'] : ['cab_mic', 'cab_lowcut', 'cab_highcut', 'cab_level'];
  ids.forEach((id) => k.append(gKnob(id, id === 'cab_mic' ? 'Mic' : null, amp ? '#f0a23b' : '#2bbfb0')));
  m.append(k);
  m.onclick = () => { selected = kind; renderChain(); renderEditor(); };
  return m;
}

function renderChain() {
  const ch = $('chain'); ch.textContent = '';
  const row = (label, from, to, pre) => {
    ch.append(el('div', 'rowlabel', label));
    const r = el('div', 'pedals');
    for (let i = from; i < to; i++) r.append(pedalCard(rig.routing.order[i]));
    r.ondragover = (e) => { e.preventDefault(); r.classList.add('dropzone'); };
    r.ondragleave = () => r.classList.remove('dropzone');
    r.ondrop = (e) => { e.preventDefault(); r.classList.remove('dropzone'); const src = +(e.dataTransfer.getData('text/plain').split(':')[1]); if (Number.isFinite(src)) { moveSlot(src, 99, pre); renderAll(); } };
    ch.append(r);
  };
  row('INPUT  >  PRE-AMP', 0, rig.routing.numPre, true);
  const mods = el('div', 'modules'); mods.append(moduleCard('amp'), moduleCard('cab')); ch.append(mods);
  row('POST-AMP  >  OUTPUT', rig.routing.numPre, 8, false);
}

// ============================================================================ parameter editor
function renderEditor() {
  const ed = $('editor'); ed.textContent = '';
  const h = el('h2'), sub = el('div', 'sub'), grid = el('div', 'grid'), actions = el('div', 'actions');
  const wide = (label, ctrl) => { const w = el('div', 'wide'); if (label) w.append(el('label', null, label)); w.append(ctrl); grid.append(w); };
  if (typeof selected === 'number') {
    const s = selected, sl = rig.slots[s], inf = info(sl.type), colour = CAT_COLOURS[inf.category] || '#f0a23b';
    h.textContent = `Slot ${s + 1}: ${sl.type ? inf.name : 'Empty'}`; h.style.borderColor = colour;
    sub.textContent = sl.type ? `${inf.category}  |  ${isPre(s) ? 'pre-amp' : 'post-amp'}` : 'Choose a pedal';
    wide('Pedal', typeSelect(sl.type, (t) => { setSlotType(s, t); renderAll(); }));
    if (sl.type) {
      const t = toggle('Enabled', () => rig.slots[s].on, (v) => { rig.slots[s].on = v; renderChain(); }); t.classList.add('fill'); wide(null, t);
      if (inf.hasMix) grid.append(mixKnob(s, false, colour));
      inf.params.forEach((p, k) => {
        if (p.choices) wide(p.name, choiceSelect(p.choices, sl.values[k], (v) => { rig.slots[s].values[k] = v; changed(false); }));
        else grid.append(slotKnob(s, k, false, colour));
      });
      const a = (t2, fn) => { const b = el('button', null, t2); b.onclick = () => { fn(); renderAll(); }; actions.append(b); };
      a('Reset', () => setSlotType(s, sl.type));
      a('Copy', () => { window.pfClip = JSON.parse(JSON.stringify(rig.slots[s])); toast('Pedal copied'); });
      a('Paste', () => { if (window.pfClip) { rig.slots[s] = JSON.parse(JSON.stringify(window.pfClip)); changed(true); } });
      a('Remove', () => setSlotType(s, 0));
    }
  } else if (selected === 'amp') {
    h.textContent = 'Amplifier'; sub.textContent = 'Original voicings - not hardware captures';
    wide('Model', choiceSelect(D.ampNames, rig.ampType, (v) => { rig.ampType = v; changed(false); renderChain(); }));
    const t = toggle('Amp enabled', () => rig.ampOn, (v) => { rig.ampOn = v; renderChain(); }); t.classList.add('fill'); wide(null, t);
    ['amp_gain', 'amp_bass', 'amp_mid', 'amp_treble', 'amp_presence', 'amp_master', 'amp_sag', 'amp_trim'].forEach((id) => grid.append(gKnob(id, null, '#f0a23b', false)));
  } else if (selected === 'cab') {
    h.textContent = 'Cabinet'; h.style.borderColor = '#2bbfb0'; sub.textContent = 'Filter-based voicings (custom IR loading is in the plugin)';
    wide('Cabinet', choiceSelect(D.cabNames.slice(0, 6), rig.cabType, (v) => { rig.cabType = v; changed(false); renderChain(); }));
    const t = toggle('Cabinet enabled', () => rig.cabOn, (v) => { rig.cabOn = v; renderChain(); }); t.classList.add('fill'); wide(null, t);
    ['cab_mic', 'cab_lowcut', 'cab_highcut', 'cab_level', 'cab_mix'].forEach((id) => grid.append(gKnob(id, null, '#2bbfb0', false)));
  } else {
    h.textContent = 'Master'; h.style.borderColor = '#2bbfb0'; sub.textContent = 'Input, gate, EQ, output, tempo, macros';
    wide('Input', choiceSelect(['Stereo', 'Left (Mono)', 'Right (Mono)', 'Sum to Mono'], rig.inMode, (v) => { rig.inMode = v; changed(false); }));
    grid.append(gKnob('in_gain', null, '#2bbfb0', false));
    const tg = (text, k) => { const t = toggle(text, () => rig[k], (v) => { rig[k] = v; }); t.classList.add('fill'); wide(null, t); };
    tg('Noise gate', 'gateOn'); grid.append(gKnob('gate_thresh', 'Gate Threshold', '#2bbfb0', false), gKnob('gate_release', null, '#2bbfb0', false));
    tg('Master EQ', 'eqOn'); ['eq_low', 'eq_mid', 'eq_midfreq', 'eq_high'].forEach((id) => grid.append(gKnob(id, 'EQ ' + GLOBAL[id].name, '#2bbfb0', false)));
    grid.append(gKnob('out_gain', 'Preset Level', '#2bbfb0', false));
    tg('Output limiter', 'limOn'); grid.append(gKnob('lim_ceiling', null, '#2bbfb0', false), gKnob('master_vol', 'Master Volume', '#2bbfb0', false));
    wide('Output', choiceSelect(['Stereo', 'Mono'], rig.outMode, (v) => { rig.outMode = v; changed(false); }));
    grid.append(gKnob('tempo', null, '#2bbfb0', false));
    MACRO_IDS.forEach((id) => grid.append(gKnob(id, 'Macro ' + GLOBAL[id].name, '#f0a23b', false)));
  }
  ed.append(h, sub, grid, actions);
}

// ============================================================================ bottom bar
function renderBottom() {
  const b = $('bottom'); b.textContent = '';
  const group = (label, ...items) => { const g = el('div', 'bb-group'); g.append(el('span', 'bb-label', label), ...items); b.append(g); };
  group('IN', gKnob('in_gain', 'Input'));
  group('GATE', toggle('GATE', () => rig.gateOn, (v) => (rig.gateOn = v)), gKnob('gate_thresh'));
  group('EQ', gKnob('eq_low'), gKnob('eq_mid'), gKnob('eq_high'));
  group('MACROS', ...MACRO_IDS.map((id) => gKnob(id, null, '#f0a23b')));
  const tap = el('div', 'tap'), tb = el('button', null, 'TAP'), bpm = el('span', null, '');
  bpm.id = 'bpm'; tb.title = 'Tap tempo (T)'; tb.onclick = tapTempo; tap.append(tb, bpm); b.append(tap);
  b.append(el('div', 'grow'));
  group('OUT', choiceSelect(['Stereo', 'Mono'], rig.outMode, (v) => { rig.outMode = v; changed(false); }), gKnob('out_gain', 'Level'), gKnob('master_vol', 'Master'));
  const led = el('span', 'led'); led.id = 'limLed';
  group('LIMIT', toggle('LIMITER', () => rig.limOn, (v) => (rig.limOn = v)), led);
  updateBpm();
}
const updateBpm = () => { const e = $('bpm'); if (e) e.textContent = rig.p.tempo.toFixed(1) + ' BPM'; };
let taps = [];
function tapTempo() {
  const now = performance.now();
  if (taps.length && now - taps[taps.length - 1] > 2000) taps = [];
  taps.push(now); if (taps.length > 5) taps.shift();
  if (taps.length >= 2) { rig.p.tempo = clamp(60000 / ((taps[taps.length - 1] - taps[0]) / (taps.length - 1)), 40, 240); changed(false); updateBpm(); }
}

// ============================================================================ top bar, menus, toasts
function renderTop() {
  $('presetName').textContent = rig.presetName + (rig.dirty ? '  *' : '');
  const fav = favs.has(rig.presetId);
  $('fav').textContent = fav ? '★' : '☆'; $('fav').style.color = fav ? '#f0a23b' : '';
  $('bypass').classList.toggle('on', rig.bypass);
  $('quality').value = rig.quality;
}
function renderAll() { renderTop(); renderBrowser(); renderChain(); renderEditor(); renderBottom(); }

let toastTimer = 0;
function toast(msg, ms = 3500) { const t = $('toast'); t.textContent = msg; t.hidden = false; clearTimeout(toastTimer); toastTimer = setTimeout(() => (t.hidden = true), ms); }

function download(name, text) {
  const a = el('a'); a.href = URL.createObjectURL(new Blob([text], { type: 'application/json' })); a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 2000);
}
const fileSafe = (s) => s.replace(/[\\/:*?"<>|]+/g, '_');

function saveUser(asNew) {
  let name = rig.presetName;
  const isUser = rig.presetId.startsWith('user.');
  if (asNew || !isUser) { name = prompt('Save preset as (stored in this browser):', isUser ? name : name + ' (User)'); if (!name) return; }
  const p = captureState();
  p.name = name; p.category = isUser ? p.category : 'User'; p.author = 'Web preview';
  if (asNew || !isUser) p.id = 'user.' + name.toLowerCase().replace(/[^a-z0-9]+/g, '-') + '-' + Date.now().toString(36);
  userPresets = userPresets.filter((u) => u.id !== p.id).concat([p]);
  store.set('pf.user', userPresets);
  rig.presetId = p.id; rig.presetName = p.name; rig.dirty = false;
  renderTop(); renderBrowser(); toast('Saved "' + p.name + '"');
}

function moreMenu() {
  const items = [
    ['Save as new...', () => saveUser(true)],
    ['Export preset (.pfpreset, opens in the VST3)', () => download(fileSafe(rig.presetName) + '.pfpreset', JSON.stringify(captureState(), null, 2))],
    ['Export my browser presets (.pfbank)', () => download('Pedal Faster web presets.pfbank', JSON.stringify({ bankFormat: 1, name: 'Web presets', presets: userPresets }, null, 2))],
    ['Import preset or bank...', () => $('presetInput').click()],
    ['Rename this user preset...', () => {
      const u = userPresets.find((x) => x.id === rig.presetId); if (!u) return toast('Only user presets can be renamed');
      const n = prompt('New name', u.name); if (!n) return; u.name = n; rig.presetName = n; store.set('pf.user', userPresets); renderTop(); renderBrowser();
    }],
    ['Delete this user preset', () => {
      if (!rig.presetId.startsWith('user.')) return toast('Factory presets cannot be deleted');
      if (!confirm(`Delete "${rig.presetName}"?`)) return;
      userPresets = userPresets.filter((x) => x.id !== rig.presetId); store.set('pf.user', userPresets); applyPreset(D.presets[0]);
    }],
  ];
  const old = document.querySelector('.menu'); if (old) { old.remove(); return; }
  const m = el('div', 'menu panel');
  Object.assign(m.style, { position: 'fixed', zIndex: 40, padding: '4px', border: '1px solid #2e3339', top: '50px' });
  const r = $('more').getBoundingClientRect(); m.style.left = r.left + 'px';
  for (const [t, fn] of items) { const b = el('button', null, t); b.style.textAlign = 'left'; b.style.margin = '2px'; b.onclick = () => { m.remove(); fn(); }; m.append(b); }
  document.body.append(m);
  setTimeout(() => document.addEventListener('click', function off(e) { if (!m.contains(e.target)) { m.remove(); document.removeEventListener('click', off); } }), 0);
}

$('presetInput').onchange = async (e) => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  try {
    const j = JSON.parse(await f.text());
    const list = Array.isArray(j.presets) ? j.presets : [j];
    let n = 0;
    for (const p of list) {
      if (!p || typeof p.name !== 'string') continue;
      if ((p.schemaVersion || 0) > 1) { toast('Preset is from a newer version'); continue; }
      const copy = { ...p, id: 'user.' + p.name.toLowerCase().replace(/[^a-z0-9]+/g, '-') + '-' + (Date.now() + n).toString(36) };
      copy.effects = (copy.effects || []).filter((x) => REG.some((r) => r.id === x.type && r.index > 0));
      userPresets.push(copy); n++;
    }
    store.set('pf.user', userPresets);
    toast(`Imported ${n} preset${n === 1 ? '' : 's'}`);
    if (n) applyPreset(userPresets[userPresets.length - n]);
  } catch (err) { toast('Not a valid preset file: ' + err.message); }
};

// ============================================================================ change propagation
let postPending = false;
function changed(structural, markDirty = true) {
  if (markDirty) rig.dirty = true;
  if (structural) { renderChain(); renderEditor(); }
  renderTop();
  if (!postPending) { postPending = true; requestAnimationFrame(() => { postPending = false; postState(); }); }
  if (structural || !markDirty) renderBrowser();
}

// ============================================================================ audio
let ac = null, node = null, nodeIsWorklet = false, input = null, stream = null, analyser = null, sourceKind = '';
const meters = { inPeak: 0, outPeak: 0, limGrDb: 0 };
let feedbackSince = 0;

function postState() { if (node) node.port.postMessage({ type: 'state', state: effectiveState() }); }

function onMeters(m) {
  Object.assign(meters, m);
  meters.count = (meters.count || 0) + 1;
  meters.maxIn = Math.max(meters.maxIn || 0, m.inPeak); meters.maxOut = Math.max(meters.maxOut || 0, m.outPeak);
  if (meters.waiter && meters.count >= meters.waitFor) { const w = meters.waiter; meters.waiter = null; w(); }
  // Feedback guard: when system audio is captured the processed output can be captured again.
  if (sourceKind === 'system' && m.limGrDb > 9) {
    if (!feedbackSince) feedbackSince = performance.now();
    else if (performance.now() - feedbackSince > 2000) {
      rig.p.master_vol = -60; postState(); renderBottom(); feedbackSince = 0;
      toast('Possible feedback loop detected - master volume muted. Share a browser tab instead, or route the source app to another output.', 9000);
    }
  } else feedbackSince = 0;
}

async function ensureAudio() {
  if (ac) { if (ac.state === 'suspended') await ac.resume(); return; }
  ac = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
  const src = document.getElementById('worklet-src').textContent;
  const registry = REG.map((r) => ({ hasMix: r.hasMix }));
  try {
    // file:// pages cannot load blob:null worklet modules, so try a data: URL first, then a blob: URL.
    const dataUrl = 'data:text/javascript;base64,' + btoa(unescape(encodeURIComponent(src)));
    try { await ac.audioWorklet.addModule(dataUrl); }
    catch { await ac.audioWorklet.addModule(URL.createObjectURL(new Blob([src], { type: 'text/javascript' }))); }
    node = new AudioWorkletNode(ac, 'pedal-faster', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], channelCount: 2, channelCountMode: 'explicit', processorOptions: { registry, state: effectiveState() } });
    node.port.onmessage = (e) => onMeters(e.data);
    nodeIsWorklet = true;
  } catch (err) {
    // Fallback (e.g. some file:// contexts): run the same processor on the main thread.
    console.warn('AudioWorklet unavailable, using ScriptProcessor fallback', err);
    window.pfWorkletError = String(err && (err.message || err.name) || err);
    let Cls = null;
    const shim = { postMessage: (m) => onMeters(m), onmessage: null };
    class AudioWorkletProcessor { constructor() { this.port = shim; } }
    new Function('AudioWorkletProcessor', 'registerProcessor', 'sampleRate', src)(AudioWorkletProcessor, (n, c) => (Cls = c), ac.sampleRate);
    const proc = new Cls({ processorOptions: { registry, state: effectiveState() } });
    const sp = ac.createScriptProcessor(1024, 2, 2);
    const inL = new Float32Array(128), inR = new Float32Array(128), oL = new Float32Array(128), oR = new Float32Array(128);
    sp.onaudioprocess = (e) => {
      const ib = e.inputBuffer, ob = e.outputBuffer, a = ib.getChannelData(0), b = ib.numberOfChannels > 1 ? ib.getChannelData(1) : a;
      const x = ob.getChannelData(0), y = ob.getChannelData(1);
      for (let p = 0; p < ob.length; p += 128) {
        inL.set(a.subarray(p, p + 128)); inR.set(b.subarray(p, p + 128));
        proc.process([[inL, inR]], [[oL, oR]]);
        x.set(oL, p); y.set(oR, p);
      }
    };
    node = { port: { postMessage: (m) => shim.onmessage && shim.onmessage({ data: m }) }, connect: (d) => sp.connect(d), disconnect: () => sp.disconnect(), input: sp };
    nodeIsWorklet = false;
  }
  node.connect(ac.destination);
  analyser = ac.createAnalyser(); analyser.fftSize = 4096;
  postState();
}
const nodeInput = () => (nodeIsWorklet ? node : node.input);

function stopSource() {
  if (input) { try { input.stop?.(); } catch { /* already stopped */ } try { input.disconnect(); } catch { /* not connected */ } input = null; }
  if (stream) { stream.getTracks().forEach((t) => t.stop()); stream = null; }
  sourceKind = '';
  setStatus('Not running', false);
}
function setStatus(text, live) { const s = $('status'); s.textContent = text; s.classList.toggle('live', !!live); }

async function startSource(kind) {
  await ensureAudio();
  stopSource();
  try {
    if (kind === 'system' || kind === 'tab') {
      if (!navigator.mediaDevices?.getDisplayMedia) throw new Error('This browser cannot capture system audio. Use Chrome or Edge on desktop.');
      stream = await navigator.mediaDevices.getDisplayMedia({
        video: { displaySurface: kind === 'tab' ? 'browser' : 'monitor' },
        audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false, suppressLocalAudioPlayback: kind === 'tab', restrictOwnAudio: true },
        systemAudio: 'include', selfBrowserSurface: 'exclude', surfaceSwitching: 'include', monitorTypeSurfaces: 'include',
      });
      if (stream.getAudioTracks().length === 0) {
        stream.getTracks().forEach((t) => t.stop()); stream = null;
        throw new Error(kind === 'tab' ? 'No audio was shared - pick a tab and keep "Share tab audio" ticked.' : 'No audio was shared - choose "Entire screen" and tick "Share system audio".');
      }
      stream.getVideoTracks().forEach((t) => (t.enabled = false));
      stream.getAudioTracks()[0].addEventListener('ended', () => { stopSource(); toast('Audio sharing stopped'); });
      input = ac.createMediaStreamSource(stream);
      rig.inMode = 0;
      setStatus(kind === 'tab' ? 'Live: browser tab' : 'Live: system audio', true);
      if (kind === 'system' && !navigator.mediaDevices.getSupportedConstraints?.().restrictOwnAudio)
        toast('Heads-up: this browser may re-capture its own output. If you hear feedback, lower Master or share a tab instead.', 8000);
    } else if (kind === 'mic') {
      stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: 2 } });
      input = ac.createMediaStreamSource(stream);
      rig.inMode = 1;
      setStatus('Live: ' + (stream.getAudioTracks()[0].label || 'input device'), true);
      toast('Input is set to Left (Mono) - change it in Master if your guitar is on input 2. Use headphones to avoid feedback.', 6000);
    } else if (kind === 'demo' || kind === 'file') {
      const buf = kind === 'demo' ? makeDemoGuitar(ac.sampleRate) : await pickFile();
      if (!buf) return;
      input = ac.createBufferSource(); input.buffer = buf; input.loop = true; input.start();
      rig.inMode = 0;
      setStatus(kind === 'demo' ? 'Playing: demo guitar loop' : 'Playing: ' + (buf.name || 'file'), true);
    }
    input.connect(nodeInput()); input.connect(analyser);
    sourceKind = kind; postState();
    $('startDialog').hidden = true;
  } catch (err) {
    if (err.name === 'NotAllowedError') toast('Permission was not granted.');
    else toast(err.message || String(err), 7000);
    setStatus('Not running', false);
  }
}

function pickFile() {
  return new Promise((resolve) => {
    const inp = $('fileInput');
    inp.onchange = async () => {
      const f = inp.files[0]; inp.value = '';
      if (!f) return resolve(null);
      try { const b = await ac.decodeAudioData(await f.arrayBuffer()); b.name = f.name; resolve(b); }
      catch { toast('Could not decode that audio file'); resolve(null); }
    };
    inp.click();
  });
}

/** Karplus-Strong clean DI guitar riff (same idea as the test suite's reference DI), 8 s loop. */
function makeDemoGuitar(sr) {
  const secs = 8, n = Math.floor(sr * secs), mix = new Float32Array(n);
  const notes = [];
  const prog = [[82.41, 123.47, 164.81, 207.65, 246.94], [110, 164.81, 220, 277.18, 329.63], [73.42, 110, 146.83, 185, 220], [98, 146.83, 196, 246.94, 293.66]];
  prog.forEach((chord, c) => {
    const t0 = c * 2;
    chord.forEach((hz, k) => notes.push([t0 + k * 0.06, hz, 0.8]));
    [0.75, 1.0, 1.25, 1.5, 1.75].forEach((dt, k) => notes.push([t0 + dt, chord[(k + 2) % chord.length], 0.6]));
  });
  let seed = 1;
  const rnd = () => { seed = (seed * 16807) % 2147483647; return seed / 2147483647 * 2 - 1; };
  for (const [t, hz, vel] of notes) {
    const start = Math.floor(t * sr), period = Math.max(2, Math.round(sr / hz)), line = new Float32Array(period);
    for (let i = 0; i < period; i++) line[i] = rnd();
    for (let i = start, k = 0; i < Math.min(n, start + sr * 3); i++, k++) {
      const idx = k % period, cur = line[idx];
      line[idx] = 0.4985 * (cur + line[(idx + 1) % period]) * 0.999;
      mix[i] += cur * vel * 0.18;
    }
  }
  let peak = 0; for (const v of mix) peak = Math.max(peak, Math.abs(v));
  const buf = ac.createBuffer(2, n, sr), g = 0.45 / (peak || 1);
  for (let ch = 0; ch < 2; ch++) { const d = buf.getChannelData(ch); for (let i = 0; i < n; i++) d[i] = mix[i] * g; }
  return buf;
}

// ============================================================================ tuner (YIN on the input)
let tunerOn = false;
const tunerBuf = new Float32Array(4096);
function tunerTick() {
  if (!tunerOn || !analyser) return;
  analyser.getFloatTimeDomainData(tunerBuf);
  const sr = ac.sampleRate, dec = Math.max(1, Math.round(sr / 11025)), N = Math.floor(tunerBuf.length / dec), w = new Float32Array(N);
  let energy = 0;
  for (let i = 0; i < N; i++) { let s = 0; for (let k = 0; k < dec; k++) s += tunerBuf[i * dec + k]; w[i] = s / dec; energy += w[i] * w[i]; }
  const dsr = sr / dec, half = N >> 1, diff = new Float32Array(half);
  let res = null;
  if (energy / N > 1e-7) {
    let run = 0; diff[0] = 1;
    for (let tau = 1; tau < half; tau++) { let d = 0; for (let i = 0; i < half; i++) { const v = w[i] - w[i + tau]; d += v * v; } run += d; diff[tau] = run > 0 ? d * tau / run : 1; }
    const minT = Math.max(2, Math.floor(dsr / 1400)), maxT = Math.min(half - 2, Math.floor(dsr / 30));
    for (let t = minT; t <= maxT; t++) if (diff[t] < 0.15) {
      while (t + 1 <= maxT && diff[t + 1] < diff[t]) t++;
      const s0 = diff[t - 1], s1 = diff[t], s2 = diff[t + 1], den = 2 * (2 * s1 - s2 - s0);
      const hz = dsr / (t + (Math.abs(den) > 1e-9 ? (s2 - s0) / den : 0));
      const midi = 69 + 12 * Math.log2(hz / 440), note = Math.round(midi);
      res = { hz, cents: (midi - note) * 100, name: ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'][((note % 12) + 12) % 12] + (Math.floor(note / 12) - 1) };
      break;
    }
  }
  const good = res && Math.abs(res.cents) < 3;
  $('tunerNote').textContent = res ? res.name : '--';
  $('tunerNote').className = 'note' + (good ? ' good' : '');
  $('tunerNeedle').style.left = (res ? 50 + clamp(res.cents, -50, 50) : 50) + '%';
  $('tunerNeedle').style.background = good ? '#5ccf7a' : '#f0a23b';
  $('tunerInfo').textContent = res ? `${res.hz.toFixed(1)} Hz   ${res.cents >= 0 ? '+' : ''}${res.cents.toFixed(1)} cents` : analyser && input ? 'Play a single note' : 'Start an audio source first';
}

// ============================================================================ wiring
function meterTick() {
  const w = (lin) => clamp((20 * Math.log10(Math.max(lin, 1e-6)) + 60) / 60 * 100, 0, 100) + '%';
  meters.dispIn = Math.max(meters.inPeak, (meters.dispIn || 0) * 0.85);
  meters.dispOut = Math.max(meters.outPeak, (meters.dispOut || 0) * 0.85);
  $('inMeter').style.width = w(meters.dispIn); $('outMeter').style.width = w(meters.dispOut);
  const led = $('limLed'); if (led) led.classList.toggle('on', meters.limGrDb > 0.5);
  meters.inPeak = meters.outPeak = 0;
  requestAnimationFrame(meterTick);
}

function init() {
  const cat = $('category');
  cat.append(new Option('All categories', ''));
  D.banks.forEach((b) => cat.append(new Option(b.name, b.name)));
  for (const id of ['search', 'category', 'source', 'sort']) $(id).addEventListener('input', renderBrowser);
  $('prev').onclick = () => step(-1); $('next').onclick = () => step(1);
  $('presetName').onclick = () => { $('search').value = ''; $('category').value = ''; renderBrowser(); };
  $('fav').onclick = () => { favs.has(rig.presetId) ? favs.delete(rig.presetId) : favs.add(rig.presetId); store.set('pf.favs', [...favs]); renderTop(); renderBrowser(); };
  $('save').onclick = () => saveUser(false);
  $('more').onclick = (e) => { e.stopPropagation(); moreMenu(); };
  $('quality').onchange = () => { rig.quality = +$('quality').value; changed(false, false); };
  $('masterBtn').onclick = () => { selected = 'master'; renderChain(); renderEditor(); };
  $('bypass').onclick = () => { rig.bypass = !rig.bypass; changed(false, false); };
  $('tunerBtn').onclick = () => { tunerOn = !tunerOn; $('tuner').hidden = !tunerOn; $('tunerBtn').classList.toggle('on', tunerOn); };
  setInterval(tunerTick, 100);
  $('startBtn').onclick = () => { $('startDialog').hidden = false; };
  document.querySelectorAll('#startDialog [data-src]').forEach((b) => (b.onclick = () => startSource(b.dataset.src)));
  document.querySelector('#startDialog [data-close]').onclick = () => ($('startDialog').hidden = true);
  document.addEventListener('keydown', (e) => {
    if (e.target.closest('input, select, textarea, .knob')) return;
    if (e.key === 'ArrowRight' || e.key === 'PageDown') { step(1); e.preventDefault(); }
    else if (e.key === 'ArrowLeft' || e.key === 'PageUp') { step(-1); e.preventDefault(); }
    else if ((e.ctrlKey || e.metaKey) && e.key === 's') { e.preventDefault(); saveUser(false); }
    else if ((e.ctrlKey || e.metaKey) && e.key === 'f') { e.preventDefault(); $('search').focus(); }
    else if (e.key === 't') tapTempo();
    else if (e.key === 'u') $('tunerBtn').click();
    else if (e.key === 'b') $('bypass').click();
    else if (e.key === 'Escape') { $('search').value = ''; renderBrowser(); }
  });
  applyPreset(D.presets[0]);
  if (new URLSearchParams(location.search).has('browse')) $('startDialog').hidden = true;
  renderAll();
  requestAnimationFrame(meterTick);
}
init();

// ?selftest: renders the demo guitar through the real Web Audio graph (OfflineAudioContext + the AudioWorklet
// module loaded exactly as the live page loads it) and reports the result. Used by scripts/test-web.ps1.
if (new URLSearchParams(location.search).has('selftest')) {
  (async () => {
    const res = el('pre'); res.id = 'selftest-result';
    try {
      const ids = ['factory.clean.glass-room', 'factory.metal-rhythm.tight-machine', 'factory.dubstep.wobble-machine'];
      const sr = 48000, secs = 3, ctx = new OfflineAudioContext(2 * ids.length, sr * secs, sr);
      const src = document.getElementById('worklet-src').textContent;
      await ctx.audioWorklet.addModule('data:text/javascript;base64,' + btoa(unescape(encodeURIComponent(src))));
      ac = ctx;
      const source = ctx.createBufferSource(); source.buffer = makeDemoGuitar(sr);
      const merger = ctx.createChannelMerger(2 * ids.length);
      ids.forEach((id, k) => {   // one AudioWorkletNode per preset, rendered side by side
        applyPreset(findPreset(id)); rig.p.master_vol = 0;
        const n = new AudioWorkletNode(ctx, 'pedal-faster', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], channelCount: 2, channelCountMode: 'explicit',
          processorOptions: { registry: REG.map((r) => ({ hasMix: r.hasMix })), state: effectiveState() } });
        source.connect(n);
        const split = ctx.createChannelSplitter(2); n.connect(split);
        split.connect(merger, 0, 2 * k); split.connect(merger, 1, 2 * k + 1);
      });
      merger.connect(ctx.destination); source.start();
      const out = await ctx.startRendering();
      const results = ids.map((id, k) => {
        const L = out.getChannelData(2 * k), R = out.getChannelData(2 * k + 1);
        let acc = 0, finite = true;
        for (let i = 4800; i < L.length; i++) { if (!Number.isFinite(L[i]) || !Number.isFinite(R[i])) finite = false; acc += L[i] * L[i] + R[i] * R[i]; }
        return { id, rmsDb: +(10 * Math.log10(acc / (2 * (L.length - 4800)) + 1e-20)).toFixed(2), finite };
      });
      ac = null;
      res.textContent = JSON.stringify({ workletLoaded: true, results });
    } catch (e) { res.textContent = 'ERROR ' + (e && e.stack || e); }
    document.body.append(res); document.title = 'SELFTEST ' + res.textContent;
  })();
}
