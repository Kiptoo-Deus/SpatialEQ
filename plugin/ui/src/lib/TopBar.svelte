<script>
  // Mirrors TopBar in macos/App/UI/MainView.swift.
  import { ui, edit, command } from "./bridge.svelte.js";
  import { P, PRESETS } from "./params.js";
  import Menu from "./Menu.svelte";
  import Dialog from "./Dialog.svelte";

  const enabled = $derived(!ui.p[P.bypass]);
  const meterFrac = (lin) => Math.max(0, Math.min(1, (20 * Math.log10(Math.max(lin, 1e-5)) + 60) / 60));
  const app = $derived(ui.app); // the standalone app (Windows); null in the plug-in
  let saving = $state(false);

  // The plug-in's menu; the app's mirrors PresetMenu in MainView.swift (My Presets, save, update, delete).
  const presetItems = $derived(app ? [
    { header: "Built-in" },
    ...app.presets.map((name, i) => ({ label: name, action: () => command("preset", i) })),
    ...(app.userPresets.length ? [{ header: "My Presets" },
      ...app.userPresets.map((name, i) => ({ label: name, action: () => command("userPreset", i) }))] : []),
    { divider: true },
    { label: "Save as New Preset…", action: () => (saving = true) },
    ...(app.presetIsUser ? [
      { label: `Update “${app.preset}”`, action: () => command("updatePreset") },
      { label: `Delete “${app.preset}”`, action: () => command("deletePreset") }] : []),
    { label: "Import AutoEq Profile…", action: () => command("import") },
  ] : [
    { header: "Built-in" },
    ...PRESETS.map((name, i) => ({ label: name, action: () => command("preset", i) })),
    { divider: true },
    { label: "Import AutoEq Profile…", action: () => command("import") },
    { label: "Export as ParametricEQ.txt…", action: () => command("export") },
  ]);

  // DevicePicker: where the sound goes, plus (Windows) where it comes from.
  const deviceItems = $derived(app ? [
    { label: "System Default Output", checked: !app.output, action: () => command("setOutput", "") },
    { divider: true },
    ...app.outputs.map((d) => ({ label: d.name, checked: app.output === d.id, action: () => command("setOutput", d.id) })),
    { divider: true },
    { header: "Capture from" },
    ...(app.allApps ? [{ label: "All apps", checked: !app.source, action: () => command("setSource", "") }] : []),
    ...app.sources.map((d) => ({ label: d.name, checked: app.source === d.id, action: () => command("setSource", d.id) })),
  ] : []);
  const outputName = $derived(app ? (app.outputs.find((d) => d.id === app.output)?.name ?? "System Default Output") : "");
</script>

<div class="topbar">
  <button class="power" class:on={enabled} title={enabled ? "Bypass processing" : "Enable processing"}
          onclick={() => edit(P.bypass, enabled ? 1 : 0)} aria-label="power">
    <svg viewBox="0 0 24 24" width="16" height="16"><path d="M12 3v8M7.2 6.3a7 7 0 1 0 9.6 0" fill="none"
      stroke="currentColor" stroke-width="2.6" stroke-linecap="round"/></svg>
  </button>

  <div class="title">
    <div class="name">SpatialEQ</div>
    {#if !app}
      <div class="caption dim">Plug-in · {Math.round(ui.sampleRate / 1000)} kHz · {ui.latencyMs.toFixed(1)} ms</div>
    {:else if app.route.state === "failed"}
      <div class="route caption">
        <span class="failed" title={app.route.text}>{app.route.text}</span>
        {#if app.route.retry}<button class="mac-button mini" onclick={() => command("retry")}>Retry</button>{/if}
        {#if app.route.settings}
          <button class="mac-button mini" onclick={() => command("soundSettings")}>Sound Settings</button>
        {/if}
      </div>
    {:else}
      <div class="caption dim">{app.route.text}</div>
    {/if}
  </div>

  <div class="spacer"></div>

  <Menu items={presetItems} width={200}>
    <svg viewBox="0 0 16 16" width="13" height="13" class="ico"><g stroke="currentColor" stroke-width="1.4" stroke-linecap="round">
      <path d="M2 4h12M2 8h12M2 12h12"/></g><g fill="currentColor"><circle cx="5" cy="4" r="1.8"/><circle cx="11" cy="8" r="1.8"/><circle cx="7" cy="12" r="1.8"/></g></svg>
    <span class="label">{ui.preset}{ui.edited ? " (edited)" : ""}</span>
  </Menu>

  {#if app}
    <Menu items={deviceItems} width={230}>
      <svg viewBox="0 0 16 16" width="13" height="13" class="ico"><path d="M3 9.5V8a5 5 0 0 1 10 0v1.5" fill="none"
        stroke="currentColor" stroke-width="1.4"/><rect x="2" y="9" width="3" height="5" rx="1.2" fill="currentColor"/>
        <rect x="11" y="9" width="3" height="5" rx="1.2" fill="currentColor"/></svg>
      <span class="label">{outputName}</span>
    </Menu>
  {/if}

  <div class="meters">
    <div class="meter"><div class="fill" style:clip-path="inset(0 {100 - meterFrac(ui.peakL) * 100}% 0 0 round 3px)"></div></div>
    <div class="meter"><div class="fill" style:clip-path="inset(0 {100 - meterFrac(ui.peakR) * 100}% 0 0 round 3px)"></div></div>
  </div>
  {#if ui.lim < -0.5}<span class="lim mono">LIM {Math.round(ui.lim)}</span>{/if}
</div>

{#if saving}
  <Dialog title="Save Preset" field="" okLabel="Save" oncancel={() => (saving = false)}
          onok={(name) => { saving = false; command("savePreset", name.trim() || "My Preset"); }} />
{/if}
{#if app?.error}
  <Dialog title="SpatialEQ" message={app.error} cancel={false} onok={() => command("clearError")} />
{/if}

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
  .route { display: flex; align-items: center; gap: 6px; }
  .failed { color: orange; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 360px; }
  .mini { font-size: 10px; padding: 0 6px; }
</style>
