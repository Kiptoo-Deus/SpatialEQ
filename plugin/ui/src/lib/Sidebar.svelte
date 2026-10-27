<script>
  // Mirrors macos/App/UI/ControlsSidebar.swift.
  import Switch from "./Switch.svelte";
  import Segmented from "./Segmented.svelte";
  import ParamSlider from "./ParamSlider.svelte";
  import { ui, edit, command } from "./bridge.svelte.js";
  import { P, UPMIX } from "./params.js";

  const p = $derived(ui.p);
  const pct = (v) => `${Math.round(v * 100)}%`;
  const db1 = (v) => `${v.toFixed(1)} dB`;
  const toggle = (id) => (v) => edit(id, v ? 1 : 0);
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
        <div class="caption2 dim">Automate this to turn the listener's head in your DAW.</div>
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
      <span class="caption dim">VST3 · v{ui.version}</span>
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
  .footer { display: flex; justify-content: space-between; padding: 0 4px; }
</style>
