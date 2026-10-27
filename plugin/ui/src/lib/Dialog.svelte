<script>
  // A macOS-style sheet: title, optional message or text field, and Cancel / OK buttons.
  // Mirrors the "Save Preset" sheet in macos/App/UI/MainView.swift.
  let { title, message = "", field = null, okLabel = "OK", cancel = true, onok, oncancel } = $props();
  let value = $state(field ?? "");
  let input;

  $effect(() => { input?.focus(); input?.select(); });
  function key(e) {
    if (e.key === "Enter") onok?.(value);
    else if (e.key === "Escape") (oncancel ?? onok)?.(value);
  }
</script>

<div class="backdrop">
  <div class="sheet" role="dialog" aria-label={title} tabindex="-1" onkeydown={key}>
    <div class="title">{title}</div>
    {#if message}<div class="message">{message}</div>{/if}
    {#if field !== null}<input bind:this={input} bind:value placeholder="Name" />{/if}
    <div class="buttons">
      {#if cancel}<button class="mac-button" onclick={() => oncancel?.()}>Cancel</button>{/if}
      <button class="mac-button default" onclick={() => onok?.(value)}>{okLabel}</button>
    </div>
  </div>
</div>

<style>
  .backdrop {
    position: fixed; inset: 0; z-index: 100;
    display: flex; justify-content: center; align-items: flex-start;
    background: rgba(0, 0, 0, 0.25);
  }
  .sheet {
    margin-top: 52px; width: 300px; padding: 20px;
    display: flex; flex-direction: column; gap: 12px;
    border-radius: 12px;
    background: rgb(36, 37, 42);
    border: 0.5px solid rgba(255, 255, 255, 0.14);
    box-shadow: 0 16px 40px rgba(0, 0, 0, 0.6);
    outline: none;
  }
  .title { font-size: 13px; font-weight: 700; }
  .message { font-size: 12px; color: var(--text-dim); white-space: pre-wrap; }
  input {
    width: 100%; font: inherit; font-size: 13px; color: white;
    padding: 4px 7px; border-radius: 5px;
    background: rgba(255, 255, 255, 0.08);
    border: 0.5px solid rgba(255, 255, 255, 0.18);
    outline: none;
  }
  input:focus { border-color: var(--control-blue); box-shadow: 0 0 0 3px rgba(10, 132, 255, 0.35); }
  .buttons { display: flex; justify-content: flex-end; gap: 8px; }
  .default { background: var(--control-blue); color: white; }
</style>
