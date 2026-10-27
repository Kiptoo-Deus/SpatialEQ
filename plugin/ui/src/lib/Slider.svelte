<script>
  // macOS slider (small): thin track, blue fill from the left, white capsule knob.
  // Calls onedit(value, phase) with phase "begin" | "set" | "end" so the host records one undo step.
  let { value = 0, min = 0, max = 1, defaultValue = null, width = null, onedit = () => {} } = $props();
  let track;
  const frac = $derived(Math.max(0, Math.min(1, (value - min) / (max - min))));

  function valueAt(e) {
    const r = track.getBoundingClientRect();
    const k = Math.max(0, Math.min(1, (e.clientX - r.left - 9) / (r.width - 18)));
    return min + k * (max - min);
  }
  function down(e) {
    if (e.detail === 2 && defaultValue !== null) { onedit(defaultValue, "once"); return; }
    track.setPointerCapture(e.pointerId);
    onedit(value, "begin");
    onedit(valueAt(e), "set");
  }
  function move(e) { if (track.hasPointerCapture(e.pointerId)) onedit(valueAt(e), "set"); }
  function up(e) {
    if (!track.hasPointerCapture(e.pointerId)) return;
    track.releasePointerCapture(e.pointerId);
    onedit(value, "end");
  }
</script>

<div class="slider" bind:this={track} style:width={width ? width + "px" : null}
     onpointerdown={down} onpointermove={move} onpointerup={up} role="slider" aria-valuenow={value} tabindex="-1">
  <div class="track"></div>
  <div class="fill" style:width="calc(9px + {frac} * (100% - 18px))"></div>
  <div class="knob" style:left="calc({frac} * (100% - 18px))"></div>
</div>

<style>
  .slider { position: relative; height: 16px; width: 100%; touch-action: none; }
  .track, .fill {
    position: absolute;
    top: 6.5px;
    height: 3px;
    border-radius: 2px;
  }
  .track { left: 0; right: 0; background: rgba(255, 255, 255, 0.2); }
  .fill { left: 0; background: var(--control-blue); }
  .knob {
    position: absolute;
    top: 2px;
    width: 18px;
    height: 12px;
    border-radius: 999px;
    background: #ececf0;
    box-shadow: 0 0.5px 2px rgba(0, 0, 0, 0.55);
  }
</style>
