<script>
  // Mirrors macos/App/UI/ControlsSidebar.swift.
  import Switch from "./Switch.svelte";
  import Segmented from "./Segmented.svelte";
  import ParamSlider from "./ParamSlider.svelte";
  import Menu from "./Menu.svelte";
  import { ui, edit, command } from "./bridge.svelte.js";
  import { P, UPMIX } from "./params.js";

  const p = $derived(ui.p);
  const pct = (v) => `${Math.round(v * 100)}%`;
  const db1 = (v) => `${v.toFixed(1)} dB`;
  const toggle = (id) => (v) => edit(id, v ? 1 : 0);

  // Standalone app (Windows) options, the counterpart of the Mac app's Settings window.
  const app = $derived(ui.app);
  const appItems = $derived(app ? [
    { label: "Keep running in the notification area when closed", checked: app.closeToTray,
      action: () => command("closeToTray", app.closeToTray ? 0 : 1) },
    { label: "Start with Windows", checked: app.startWithWindows,
      action: () => command("startWithWindows", app.startWithWindows ? 0 : 1) },
    { divider: true },
    { label: app.appsInMix ? `Reset app volumes (${app.appsInMix} in the mix)` : "Reset app volumes",
      action: () => command("resetAppVolumes") },
    { label: "Windows sound settings…", action: () => command("soundSettings") },
  ] : []);
</script>

<div class="sidebar panel">
  <div class="scroll">
    <section>
      <div class="section-title">OUTPUT MODE</div>
      <div><Segmented items={["Headphones", "Speakers"]} selected={p[P.mode]} onchange={(i) => edit(P.mode, i)} /></div>
    </section>

    {#if p[P.mode] === 0}
      <section>
        <div class="section-title">SPATIAL AUDIO</div>
        <label class="toggle-row"><span>Virtual surround</span>
          <Switch checked={!!p[P.spatialOn]} onchange={toggle(P.spatialOn)} /></label>
        {#if p[P.spatialOn]}
          <div><Segmented items={UPMIX} selected={p[P.upmix]} onchange={(i) => edit(P.upmix, i)} /></div>
          <div><button class="mac-button" onclick={() => command("resetSources")}>Reset source positions</button></div>
        {/if}
      </section>
      <section>
        <div class="section-title">HEAD TRACKING</div>
        <ParamSlider title="Head rotation" id={P.headYaw} min={-180} max={180} defaultValue={0}
                     format={(v) => `${v >= 0 ? "+" : ""}${Math.round(v)}°`} />
        <div class="caption2 dim">{app ? "Drag to turn the listener’s head. Head tracking needs AirPods on a Mac." : "Automate this to turn the listener’s head in your DAW."}</div>
      </section>
    {:else}
      <section>
        <div class="section-title">STEREO IMAGE</div>
        <ParamSlider title="Width" id={P.width} min={0} max={2.5} defaultValue={1} format={pct} />
        <label class="toggle-row"><span>Crosstalk cancellation</span>
          <Switch checked={!!p[P.xtcOn]} onchange={toggle(P.xtcOn)} /></label>
        {#if p[P.xtcOn]}
          <ParamSlider title="Strength" id={P.xtcStrength} min={0} max={1} defaultValue={0.6} format={pct} />
          <ParamSlider title="Speaker span" id={P.speakerSpan} min={10} max={120} defaultValue={30}
                       format={(v) => `${Math.round(v)}°`} />
          <div class="caption2 dim">Works best sitting centred between the speakers.</div>
        {/if}
      </section>
    {/if}

    <section>
      <div class="section-title">ROOM</div>
      <ParamSlider title="Room size" id={P.roomSize} min={0} max={1} defaultValue={0.35} format={pct} />
      <ParamSlider title="Reverb" id={P.reverbMix} min={0} max={1} defaultValue={0} format={pct} />
    </section>

    <section>
      <div class="section-title">ENHANCE</div>
      <ParamSlider title="Bass boost" id={P.bassBoost} min={0} max={12} defaultValue={0} format={db1} />
      <ParamSlider title="Dialogue boost" id={P.dialogue} min={0} max={1} defaultValue={0} format={pct} />
      <label class="toggle-row"><span>Volume leveler</span>
        <Switch checked={!!p[P.levelerOn]} onchange={toggle(P.levelerOn)} /></label>
      {#if p[P.levelerOn]}
        <ParamSlider title="Target loudness" id={P.levelerTarget} min={-30} max={-10} defaultValue={-20}
                     format={(v) => `${Math.round(v)} dB`} />
        <div class="caption2 dim mono">Leveler gain {ui.lev >= 0 ? "+" : ""}{ui.lev.toFixed(1)} dB</div>
      {/if}
    </section>

    <section>
      <div class="section-title">OUTPUT</div>
      <label class="toggle-row"><span>Limiter</span>
        <Switch checked={!!p[P.limiterOn]} onchange={toggle(P.limiterOn)} /></label>
      {#if p[P.limiterOn]}
        <ParamSlider title="Ceiling" id={P.limiterCeiling} min={-6} max={0} defaultValue={-1} format={db1} />
      {/if}
      <ParamSlider title="Output gain" id={P.outputGain} min={-12} max={12} defaultValue={0}
                   format={(v) => `${v >= 0 ? "+" : ""}${v.toFixed(1)} dB`} />
    </section>

    <div class="footer">
      {#if app}
        <Menu items={appItems} icon up align="left">
          <span class="caption dim settings">Windows · v{ui.version}
            <svg viewBox="0 0 16 16" width="11" height="11"><path d="M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5Zm6 3.3V7.2l-1.7-.4a4.6 4.6 0 0 0-.5-1.2l.9-1.5-1.1-1.1-1.5.9a4.6 4.6 0 0 0-1.2-.5L8.8 2H7.2l-.4 1.7a4.6 4.6 0 0 0-1.2.5l-1.5-.9-1.1 1.1.9 1.5c-.2.4-.4.8-.5 1.2L2 7.2v1.6l1.7.4c.1.4.3.8.5 1.2l-.9 1.5 1.1 1.1 1.5-.9c.4.2.8.4 1.2.5l.4 1.7h1.6l.4-1.7c.4-.1.8-.3 1.2-.5l1.5.9 1.1-1.1-.9-1.5c.2-.4.4-.8.5-1.2Z"
              fill="currentColor"/></svg></span>
        </Menu>
      {:else}
        <span class="caption dim">VST3 · v{ui.version}</span>
      {/if}
      <a class="caption" href="https://savannahdsp.com" target="_blank" rel="noreferrer">Developed by Savannah DSP</a>
    </div>
  </div>
</div>

<style>
  .sidebar { width: 320px; flex: none; min-height: 0; display: flex; }
  .scroll {
    flex: 1;
    overflow-y: auto;
    padding: 12px;
    display: flex;
    flex-direction: column;
    gap: 10px;
    scrollbar-width: thin;
  }
  section {
    display: flex;
    flex-direction: column;
    gap: 8px;
    padding: 10px;
    border-radius: 8px;
    background: var(--section-bg);
  }
  .toggle-row { display: flex; align-items: center; gap: 8px; font-size: 13px; }
  .footer { display: flex; justify-content: space-between; align-items: center; padding: 0 4px; }
  .settings { display: flex; align-items: center; gap: 4px; }
</style>
