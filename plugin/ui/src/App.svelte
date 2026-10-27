<script>
  // Mirrors MainView in macos/App/UI/MainView.swift.
  import TopBar from "./lib/TopBar.svelte";
  import Scene3D from "./lib/Scene3D.svelte";
  import Sidebar from "./lib/Sidebar.svelte";
  import EQEditor from "./lib/EQEditor.svelte";
</script>

<main>
  <TopBar />
  <div class="middle">
    <Scene3D />
    <Sidebar />
  </div>
  <EQEditor />
</main>

<style>
  main {
    height: 100%;
    display: flex;
    flex-direction: column;
    gap: 10px;
    padding: 12px;
    background: var(--background);
  }
  .middle { flex: 1; min-height: 0; display: flex; gap: 10px; }
</style>
