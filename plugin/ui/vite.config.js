import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";
import { viteSingleFile } from "vite-plugin-singlefile";

// The plug-in serves one self-contained index.html from inside its binary, so everything
// (JS, CSS, fonts) is inlined into a single file.
export default defineConfig({
  plugins: [svelte(), viteSingleFile()],
  build: { target: "es2020", assetsInlineLimit: 100_000_000, chunkSizeWarningLimit: 4000 },
});
