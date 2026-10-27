<script>
  // macOS switch (dark appearance). size: "small" (sidebar) or "mini" (EQ header).
  let { checked = false, disabled = false, size = "small", onchange = () => {} } = $props();
</script>

<button
  class="switch {size}"
  class:on={checked}
  {disabled}
  role="switch"
  aria-checked={checked}
  aria-label="toggle"
  onclick={() => onchange(!checked)}
>
  <span class="knob"></span>
</button>

<style>
  .switch {
    position: relative;
    flex: none;
    border: none;
    padding: 0;
    border-radius: 999px;
    background: rgba(255, 255, 255, 0.16);
    box-shadow: inset 0 0 0 0.5px rgba(0, 0, 0, 0.3);
    transition: background 0.18s ease;
  }
  .small { width: 38px; height: 22px; }
  .mini { width: 32px; height: 18px; }
  .on { background: var(--control-blue); }
  .switch:disabled { opacity: 0.4; }
  .knob {
    position: absolute;
    top: 2px;
    left: 2px;
    height: calc(100% - 4px);
    aspect-ratio: 1.45;
    border-radius: 999px;
    background: #e9e9ec;
    box-shadow: 0 1px 2px rgba(0, 0, 0, 0.45);
    transition: transform 0.18s ease;
  }
  .small .knob { width: 26px; }
  .mini .knob { width: 21px; }
  .small.on .knob { transform: translateX(8px); }
  .mini.on .knob { transform: translateX(7px); }
</style>
