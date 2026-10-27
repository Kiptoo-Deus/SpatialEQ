<script>
  // Mirrors TopBar in macos/App/UI/MainView.swift.
  import { ui, edit, command } from "./bridge.svelte.js";
  import { P, PRESETS } from "./params.js";
  import Menu from "./Menu.svelte";

  const enabled = $derived(!ui.p[P.bypass]);
  const meterFrac = (lin) => Math.max(0, Math.min(1, (20 * Math.log10(Math.max(lin, 1e-5)) + 60) / 60));

  const presetItems = $derived([
    { header: "Built-in" },
    ...PRESETS.map((name, i) => ({ label: name, action: () => command("preset", i) })),
    { divider: true },
    { label: "Import AutoEq Profile…", action: () => command("import") },
    { label: "Export as ParametricEQ.txt…", action: () => command("export") },
  ]);
</script>

<div class="topbar">
  <button class="power" class:on={enabled} title={enabled ? "Bypass processing" : "Enable processing"}
          onclick={() => edit(P.bypass, enabled ? 1 : 0)} aria-label="power">
    <svg viewBox="0 0 24 24" width="16" height="16"><path d="M12 3v8M7.2 6.3a7 7 0 1 0 9.6 0" fill="none"
      stroke="currentColor" stroke-width="2.6" stroke-linecap="round"/></svg>
  </button>

  <div class="title">
    <div class="name">SpatialEQ</div>
    <div class="caption dim">Plug-in · {Math.round(ui.sampleRate / 1000)} kHz · {ui.latencyMs.toFixed(1)} ms</div>
  </div>

  <div class="spacer"></div>

  <Menu items={presetItems} width={200}>
    <svg viewBox="0 0 16 16" width="13" height="13" class="ico"><g stroke="currentColor" stroke-width="1.4" stroke-linecap="round">
      <path d="M2 4h12M2 8h12M2 12h12"/></g><g fill="currentColor"><circle cx="5" cy="4" r="1.8"/><circle cx="11" cy="8" r="1.8"/><circle cx="7" cy="12" r="1.8"/></g></svg>
    <span class="label">{ui.preset}{ui.edited ? " (edited)" : ""}</span>
  </Menu>

  <div class="meters">
    <div class="meter"><div class="fill" style:clip-path="inset(0 {100 - meterFrac(ui.peakL) * 100}% 0 0 round 3px)"></div></div>
    <div class="meter"><div class="fill" style:clip-path="inset(0 {100 - meterFrac(ui.peakR) * 100}% 0 0 round 3px)"></div></div>
  </div>
  {#if ui.lim < -0.5}<span class="lim mono">LIM {Math.round(ui.lim)}</span>{/if}
</div>

<style>
  .topbar { display: flex; align-items: center; gap: 14px; height: 40px; flex: none; }
  .power {
    width: 30px; height: 30px; border-radius: 50%; border: none; flex: none;
    display: flex; align-items: center; justify-content: center;
    background: rgba(255, 255, 255, 0.1); color: white;
  }
  .power.on { background: var(--accent); color: black; }
  .title .name { font-size: 15px; font-weight: 700; }
  .spacer { flex: 1; }
  .ico { margin-right: 6px; flex: none; }
  .label { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .meters { width: 90px; display: flex; flex-direction: column; gap: 3px; }
  .meter { height: 5px; border-radius: 3px; background: rgba(255, 255, 255, 0.08); position: relative; }
  .fill {
    position: absolute; inset: 0; border-radius: 3px;
    background: linear-gradient(to right, var(--accent), #34c759, #ffd60a, #ff453a);
  }
  .lim { font-size: 9px; font-weight: 700; color: orange; }
</style>
