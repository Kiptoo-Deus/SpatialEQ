<script>
  // macOS pull-down menu button (dark): bordered button with a chevron, opening a menu panel.
  let { items = [], width = null, icon = false, children } = $props();
  let open = $state(false);
  let root;

  function pick(item) {
    open = false;
    item.action?.();
  }
  function onWindowDown(e) { if (open && root && !root.contains(e.target)) open = false; }
</script>

<svelte:window onpointerdown={onWindowDown} />

<div class="menu" bind:this={root} style:width={width ? width + "px" : null}>
  <button class:button={!icon} class:icon class:active={open} onclick={() => (open = !open)}>
    {@render children()}
    {#if !icon}
      <svg class="chev" viewBox="0 0 10 10" width="9" height="9"><path d="M2 3.5l3 3 3-3" fill="none"
        stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg>
    {/if}
  </button>
  {#if open}
    <div class="list">
      {#each items as item}
        {#if item.divider}<div class="divider"></div>
        {:else if item.header}<div class="header">{item.header}</div>
        {:else}<button class="item" disabled={item.disabled} onclick={() => pick(item)}>{item.label}</button>{/if}
      {/each}
    </div>
  {/if}
</div>

<style>
  .menu { position: relative; }
  .button {
    width: 100%;
    display: flex;
    align-items: center;
    background: rgba(255, 255, 255, 0.1);
    border: none;
    border-radius: 6px;
    padding: 3px 8px;
    font-size: 13px;
    box-shadow: 0 0.5px 0 rgba(255, 255, 255, 0.08) inset, 0 0.5px 1px rgba(0, 0, 0, 0.4);
  }
  .button.active { background: rgba(255, 255, 255, 0.16); }
  .icon { background: none; border: none; padding: 2px 4px; display: flex; align-items: center; }
  .chev { margin-left: auto; padding-left: 4px; opacity: 0.8; flex: none; }
  .list {
    position: absolute;
    top: calc(100% + 4px);
    right: 0;
    min-width: 220px;
    z-index: 50;
    padding: 5px;
    border-radius: 10px;
    background: rgba(40, 40, 46, 0.97);
    border: 0.5px solid rgba(255, 255, 255, 0.14);
    box-shadow: 0 10px 30px rgba(0, 0, 0, 0.55);
    -webkit-backdrop-filter: blur(20px);
    backdrop-filter: blur(20px);
  }
  .item {
    display: block;
    width: 100%;
    text-align: left;
    background: none;
    border: none;
    border-radius: 5px;
    padding: 3px 10px;
    font-size: 13px;
  }
  .item:hover { background: var(--control-blue); color: white; }
  .item:disabled { opacity: 0.35; background: none; }
  .header { padding: 4px 10px 2px; font-size: 11px; font-weight: 600; color: var(--text-dim); }
  .divider { height: 1px; margin: 5px 8px; background: rgba(255, 255, 255, 0.12); }
</style>
