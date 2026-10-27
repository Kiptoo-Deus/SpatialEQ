<script>
  // macOS segmented control, small size.
  let { items = [], selected = 0, onchange = () => {} } = $props();
</script>

<div class="segmented">
  {#each items as item, i}
    {#if i > 0 && i !== selected && i - 1 !== selected}<span class="divider"></span>{/if}
    <button class:selected={i === selected} onclick={() => onchange(i)}>{item}</button>
  {/each}
</div>

<style>
  .segmented {
    display: inline-flex;
    align-items: center;
    background: rgba(255, 255, 255, 0.07);
    border-radius: 6px;
    padding: 1px;
    box-shadow: inset 0 0 0 0.5px rgba(255, 255, 255, 0.06);
  }
  button {
    border: none;
    background: none;
    font-size: 11px;
    line-height: 16px;
    padding: 1px 11px;
    border-radius: 5px;
    color: var(--text);
  }
  button.selected {
    background: var(--control-blue);
    color: white;
    box-shadow: 0 0.5px 1px rgba(0, 0, 0, 0.4);
  }
  .divider {
    width: 1px;
    height: 12px;
    background: rgba(255, 255, 255, 0.15);
  }
</style>
