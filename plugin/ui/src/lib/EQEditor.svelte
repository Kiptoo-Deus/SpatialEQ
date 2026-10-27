<script>
  // Mirrors macos/App/UI/EQEditorView.swift: grid, live spectrum waves, response curve, draggable
  // numbered handles (drag = freq/gain, wheel = Q, double-click = on/off), band inspector.
  import { onMount } from "svelte";
  import Switch from "./Switch.svelte";
  import Slider from "./Slider.svelte";
  import Menu from "./Menu.svelte";
  import { ui, edit, command, spectrum, stats } from "./bridge.svelte.js";
  import { P, BAND, BAND_TYPES, MAX_BANDS, bandId, hasGain, eqResponse } from "./params.js";

  const MIN_F = 20, MAX_F = 20000, RANGE = 18;
  let area, grid, spec, curve;
  let w = $state(800), h = $state(170);

  const p = $derived(ui.p);
  const count = $derived(p[P.numBands]);
  const sel = $derived(ui.selectedBand >= 0 && ui.selectedBand < count ? ui.selectedBand : -1);

  const x = (f) => (Math.log(f / MIN_F) / Math.log(MAX_F / MIN_F)) * w;
  const freqAt = (px) => MIN_F * Math.pow(MAX_F / MIN_F, Math.max(0, Math.min(1, px / w)));
  const y = (db) => h / 2 - (db / RANGE) * (h / 2 - 8);
  const dbAt = (py) => ((h / 2 - py) / (h / 2 - 8)) * RANGE;
  const fmtHz = (f) => (f >= 1000 ? (f >= 10000 ? `${Math.round(f / 1000)}k` : `${(f / 1000).toFixed(1)}k`) : `${Math.round(f)}`);

  function ctx2d(c) {
    const dpr = window.devicePixelRatio || 1;
    c.width = Math.round(w * dpr);
    c.height = Math.round(h * dpr);
    const g = c.getContext("2d");
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    return g;
  }

  function drawGrid() {
    if (!grid) return;
    const g = ctx2d(grid);
    g.strokeStyle = "rgba(255,255,255,0.06)";
    g.lineWidth = 1;
    g.font = "9px -apple-system, 'Segoe UI', sans-serif";
    g.fillStyle = "rgba(255,255,255,0.55)";
    g.textBaseline = "middle";
    g.beginPath();
    for (const f of [30, 50, 100, 200, 500, 1000, 2000, 5000, 10000]) {
      const px = Math.round(x(f)) + 0.5;
      g.moveTo(px, 0); g.lineTo(px, h);
      g.fillText(fmtHz(f), px + 2, h - 6);
    }
    for (let d = -12; d <= 12; d += 6) {
      const py = Math.round(y(d)) + 0.5;
      g.moveTo(0, py); g.lineTo(w, py);
      g.fillText(d === 0 ? "0" : `${d > 0 ? "+" : ""}${d}`, 4, py - 6);
    }
    g.stroke();
  }

  function drawCurve() {
    if (!curve) return;
    const g = ctx2d(curve);
    const n = 220;
    const freqs = Array.from({ length: n }, (_, i) => MIN_F * Math.pow(MAX_F / MIN_F, i / (n - 1)));
    const r = eqResponse(p, freqs);
    g.beginPath();
    for (let i = 0; i < n; i++) i ? g.lineTo(x(freqs[i]), y(r[i])) : g.moveTo(x(freqs[i]), y(r[i]));
    const col = p[P.eqOn] ? "250,158,69" : "142,142,147";
    g.strokeStyle = `rgba(${col},0.25)`; g.lineWidth = 6; g.lineJoin = "round"; g.stroke();
    g.strokeStyle = `rgb(${col})`; g.lineWidth = 2; g.stroke();
  }

  function drawSpectrum(t) {
    if (!spec) return;
    const g = ctx2d(spec);
    const bands = Array.from(spectrum.bands);
    const n = bands.length;
    const wave = (phase, speed, ripple) => bands.map((live, i) => {
      const f = 20 * Math.pow(1000, (i + 0.5) / n);
      const motion = ripple * (0.5 + 0.5 * Math.sin(t * speed + i * 0.32 + phase))
        + 0.5 * ripple * Math.sin(t * speed * 0.53 - i * 0.17 + phase);
      const v = Math.min(1, Math.max(0, 0.03 + live + motion * (0.4 + live)));
      return [x(f), h * (1 - v * 0.9)];
    });
    const path = (pts, closed) => {
      g.beginPath();
      if (closed) { g.moveTo(0, h); g.lineTo(pts[0][0], pts[0][1]); } else g.moveTo(pts[0][0], pts[0][1]);
      for (let i = 1; i < pts.length; i++) {
        const mx = (pts[i - 1][0] + pts[i][0]) / 2, my = (pts[i - 1][1] + pts[i][1]) / 2;
        g.quadraticCurveTo(pts[i - 1][0], pts[i - 1][1], mx, my);
      }
      g.lineTo(pts[pts.length - 1][0], pts[pts.length - 1][1]);
      if (closed) { g.lineTo(w, h); g.closePath(); }
    };
    const body = wave(0, 2.4, 0.05);
    const grad = g.createLinearGradient(0, 0, 0, h);
    grad.addColorStop(0, "rgba(92,217,242,0.4)");
    grad.addColorStop(1, "rgba(92,217,242,0.03)");
    path(body, true); g.fillStyle = grad; g.fill();
    path(body, false); g.strokeStyle = "rgba(92,217,242,0.9)"; g.lineWidth = 1.5; g.stroke();
    path(wave(2.1, 1.7, 0.08), false); g.strokeStyle = "rgba(92,217,242,0.35)"; g.lineWidth = 1; g.stroke();
    path(wave(4.2, 3.1, 0.06), false); g.strokeStyle = "rgba(250,158,69,0.25)"; g.lineWidth = 1; g.stroke();
  }

  // Redraw the static layers when size or settings change; the spectrum animates at 30 fps.
  $effect(() => { w; h; drawGrid(); });
  $effect(() => { w; h; JSON.stringify(p); drawCurve(); });

  onMount(() => {
    const ro = new ResizeObserver(() => { w = area.clientWidth; h = area.clientHeight; });
    ro.observe(area);
    let last = 0, raf;
    const loop = (ms) => {
      raf = requestAnimationFrame(loop);
      if (ms - last < 33) return;
      last = ms;
      const t0 = performance.now();
      drawSpectrum(ms / 1000);
      stats.eq.n++; stats.eq.ms += performance.now() - t0;
    };
    raf = requestAnimationFrame(loop);
    return () => { ro.disconnect(); cancelAnimationFrame(raf); };
  });

  // ---- handles
  let drag = -1;
  function handleDown(e, b) {
    e.stopPropagation();
    ui.selectedBand = b;
    if (e.detail === 2) { edit(bandId(b, BAND.on), p[bandId(b, BAND.on)] ? 0 : 1); return; }
    drag = b;
    e.currentTarget.setPointerCapture(e.pointerId);
    edit(bandId(b, BAND.freq), p[bandId(b, BAND.freq)], "begin");
    edit(bandId(b, BAND.gain), p[bandId(b, BAND.gain)], "begin");
  }
  function handleMove(e, b) {
    if (drag !== b) return;
    const r = area.getBoundingClientRect();
    edit(bandId(b, BAND.freq), Math.round(freqAt(e.clientX - r.left) * 10) / 10, "set");
    if (hasGain(p[bandId(b, BAND.type)]))
      edit(bandId(b, BAND.gain), Math.round(Math.max(-RANGE, Math.min(RANGE, dbAt(e.clientY - r.top))) * 10) / 10, "set");
  }
  function handleUp(e, b) {
    if (drag !== b) return;
    drag = -1;
    edit(bandId(b, BAND.freq), p[bandId(b, BAND.freq)], "end");
    edit(bandId(b, BAND.gain), p[bandId(b, BAND.gain)], "end");
  }
  function handleWheel(e, b) {
    e.preventDefault();
    const id = bandId(b, BAND.q);
    edit(id, Math.max(0.1, Math.min(20, p[id] * (1 - e.deltaY * 0.004))));
  }

  // ---- inspector number fields
  function setNumber(id, text, lo, hi) {
    const v = parseFloat(text);
    if (!Number.isNaN(v)) edit(id, Math.max(lo, Math.min(hi, v)));
  }

  const eqMenu = [
    { label: "Import AutoEq / APO file…", action: () => command("import") },
    { label: "Export as ParametricEQ.txt…", action: () => command("export") },
    { divider: true },
    { label: "Reset to 10-band flat", action: () => command("resetEq") },
  ];
</script>

<div class="eq panel" style:height={sel >= 0 ? "280px" : "240px"}>
  <div class="header">
    <span class="section-title">PARAMETRIC EQ</span>
    <Switch size="mini" checked={!!p[P.eqOn]} onchange={(v) => edit(P.eqOn, v ? 1 : 0)} />
    <div class="spacer"></div>
    <span class="caption dim">Preamp</span>
    <Slider width={110} value={p[P.preamp]} min={-24} max={6} defaultValue={0} onedit={(v, ph) => edit(P.preamp, v, ph)} />
    <span class="caption mono preamp">{p[P.preamp] >= 0 ? "+" : ""}{p[P.preamp].toFixed(1)} dB</span>
    <button class="icon-button" title="Add band" disabled={count >= MAX_BANDS} onclick={() => command("addBand")} aria-label="add">
      <svg viewBox="0 0 12 12" width="12" height="12"><path d="M6 1v10M1 6h10" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>
    </button>
    <button class="icon-button" title="Remove selected band" disabled={sel < 0}
            onclick={() => { command("removeBand", sel); ui.selectedBand = -1; }} aria-label="remove">
      <svg viewBox="0 0 12 12" width="12" height="12"><path d="M1 6h10" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>
    </button>
    <div class="more"><Menu items={eqMenu} icon={true}>
      <svg viewBox="0 0 20 20" width="15" height="15"><circle cx="10" cy="10" r="8.2" fill="none" stroke="currentColor" stroke-width="1.3"/>
        <g fill="currentColor"><circle cx="6" cy="10" r="1.2"/><circle cx="10" cy="10" r="1.2"/><circle cx="14" cy="10" r="1.2"/></g></svg>
      <svg viewBox="0 0 10 10" width="8" height="8"><path d="M2 3.5l3 3 3-3" fill="none" stroke="currentColor" stroke-width="1.4"/></svg>
    </Menu></div>
  </div>

  <div class="area" bind:this={area} onpointerdown={() => (ui.selectedBand = -1)} role="presentation">
    <canvas bind:this={grid}></canvas>
    <canvas bind:this={spec}></canvas>
    <canvas bind:this={curve}></canvas>
    {#each Array(count) as _, b}
      {@const on = !!p[bandId(b, BAND.on)]}
      {@const hx = x(p[bandId(b, BAND.freq)])}
      {@const hy = y(hasGain(p[bandId(b, BAND.type)]) ? p[bandId(b, BAND.gain)] : 0)}
      <div class="handle" class:off={!on} class:selected={b === sel} style:left="{hx}px" style:top="{hy}px"
           onpointerdown={(e) => handleDown(e, b)} onpointermove={(e) => handleMove(e, b)}
           onpointerup={(e) => handleUp(e, b)} onwheel={(e) => handleWheel(e, b)} role="presentation">
        <span>{b + 1}</span>
      </div>
    {/each}
  </div>

  {#if sel >= 0}
    {@const type = p[bandId(sel, BAND.type)]}
    <div class="inspector caption">
      <span class="band-name">Band {sel + 1}</span>
      <label class="check"><input type="checkbox" checked={!!p[bandId(sel, BAND.on)]}
             onchange={(e) => edit(bandId(sel, BAND.on), e.currentTarget.checked ? 1 : 0)} /> On</label>
      <select value={type} onchange={(e) => edit(bandId(sel, BAND.type), +e.currentTarget.value)}>
        {#each BAND_TYPES as t, i}<option value={i}>{t}</option>{/each}
      </select>
      {#each [["Freq", BAND.freq, "Hz", 10, 22000], ["Gain", BAND.gain, "dB", -24, 24], ["Q", BAND.q, "", 0.1, 20]] as [label, f, suffix, lo, hi]}
        <span class="field" class:disabled={f === BAND.gain && !hasGain(type)}>
          <span class="dim">{label}</span>
          <input type="text" value={+p[bandId(sel, f)].toFixed(2)} disabled={f === BAND.gain && !hasGain(type)}
                 onchange={(e) => setNumber(bandId(sel, f), e.currentTarget.value, lo, hi)} />
          {#if suffix}<span class="dim">{suffix}</span>{/if}
        </span>
      {/each}
    </div>
  {/if}
</div>

<style>
  .eq { flex: none; padding: 12px; display: flex; flex-direction: column; gap: 8px; }
  .header { display: flex; align-items: center; gap: 10px; }
  .spacer { flex: 1; }
  .preamp { width: 56px; text-align: center; }
  .more :global(.icon) { gap: 1px; }
  .area { position: relative; flex: 1; min-height: 170px; }
  canvas { position: absolute; inset: 0; width: 100%; height: 100%; pointer-events: none; }
  .handle {
    position: absolute;
    width: 20px; height: 20px;
    margin: -10px 0 0 -10px;
    border-radius: 50%;
    display: flex; align-items: center; justify-content: center;
    touch-action: none;
  }
  .handle span {
    width: 14px; height: 14px; border-radius: 50%;
    background: var(--accent2);
    color: black; font-size: 8px; font-weight: 700;
    display: flex; align-items: center; justify-content: center;
  }
  .handle.off span { background: #8e8e93; }
  .handle.selected { box-shadow: inset 0 0 0 2px white; }
  .inspector { display: flex; align-items: center; gap: 14px; }
  .band-name { font-weight: 600; }
  .check { display: flex; align-items: center; gap: 4px; }
  select {
    width: 110px; font-size: 11px; color: var(--text);
    background: rgba(255, 255, 255, 0.1); border: none; border-radius: 5px; padding: 2px 4px;
  }
  .field { display: flex; align-items: center; gap: 4px; }
  .field.disabled { opacity: 0.4; }
  .field input {
    width: 64px; font-size: 11px; color: var(--text);
    background: rgba(255, 255, 255, 0.06);
    border: 0.5px solid rgba(255, 255, 255, 0.18); border-radius: 4px; padding: 2px 5px;
    user-select: text; -webkit-user-select: text;
  }
</style>
