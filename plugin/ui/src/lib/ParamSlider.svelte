<script>
  // "Title ............ value" over a slider, as in Theme.swift's ParamSlider.
  import Slider from "./Slider.svelte";
  import { ui, edit } from "./bridge.svelte.js";
  let { title, id, min, max, format = (v) => v.toFixed(1), defaultValue = null } = $props();
  const value = $derived(ui.p[id]);
</script>

<div class="param">
  <div class="row">
    <span class="caption dim">{title}</span>
    <span class="caption mono value">{format(value)}</span>
  </div>
  <Slider {value} {min} {max} {defaultValue} onedit={(v, phase) => edit(id, v, phase)} />
</div>

<style>
  .param { display: flex; flex-direction: column; gap: 2px; }
  .row { display: flex; justify-content: space-between; }
  .value { color: rgba(255, 255, 255, 0.85); }
</style>
