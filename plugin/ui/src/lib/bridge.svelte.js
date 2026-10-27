// Link between the Svelte UI and the C++ plug-in.
//
// C++ -> JS:  window.sqParams({id: plain, ...})   parameter values (on load and whenever any change)
//             window.sqFrame({bands, level, peakL, peakR, lim, lev})   ~30 times a second
//             window.sqInfo({sampleRate, latencyMs, version, preset})
// JS -> C++:  sqEdit(id, plain, phase)   phase: "begin" | "set" | "end" | "once"
//             sqCommand(name, arg)       preset, import, export, addBand, removeBand, resetEq, resetSources
//
// Without the plug-in (vite dev / preview), the UI runs on local state with simulated audio.
import { defaultParams, PRESETS } from "./params.js";

export const ui = $state({
  p: defaultParams(),
  bands: new Array(64).fill(0),
  history: [],
  level: 0, peakL: 0, peakR: 0, lim: 0, lev: 0,
  sampleRate: 48000, latencyMs: 1.5, version: "1.1.1", preset: "Flat", edited: false,
  selectedBand: -1,
  connected: false,
});

const HISTORY = 48;
function pushHistory(bands) {
  ui.history.unshift(bands.slice());
  if (ui.history.length > HISTORY) ui.history.length = HISTORY;
}

export function edit(id, value, phase = "once") {
  ui.p[id] = value;
  if (phase !== "begin" && phase !== "end") ui.edited = true;
  if (ui.connected) window.sqEdit(id, value, phase);
}

export function command(name, arg = 0) {
  if (ui.connected) window.sqCommand(name, arg);
  else if (name === "preset") { ui.preset = PRESETS[arg]; ui.edited = false; }
}

if (typeof window !== "undefined") {
  window.sqParams = (values) => { for (const k in values) ui.p[k] = values[k]; };
  window.sqInfo = (info) => {
    if (info.preset !== undefined) { ui.preset = info.preset; ui.edited = false; }
    for (const k of ["sampleRate", "latencyMs", "version"]) if (info[k] !== undefined) ui[k] = info[k];
  };
  window.sqFrame = (f) => {
    ui.bands = f.bands;
    ui.level = f.level; ui.peakL = f.peakL; ui.peakR = f.peakR; ui.lim = f.lim; ui.lev = f.lev;
    pushHistory(f.bands);
  };

  // The plug-in's native functions can appear just after this module runs, so keep checking.
  const connect = () => {
    if (typeof window.sqEdit !== "function") return false;
    ui.connected = true;
    window.sqCommand("ready", 0);
    return true;
  };
  if (!connect()) {
    // Browser preview: a moving, music-like spectrum until the plug-in connects.
    let t = 0;
    const timer = setInterval(() => {
      if (connect()) { clearInterval(timer); return; }
      t += 1 / 30;
      const bands = Array.from({ length: 64 }, (_, i) => {
        const u = i / 63;
        const v = 0.62 - 0.35 * u + 0.12 * Math.sin(i * 0.45 + t * 3) + 0.1 * Math.sin(i * 0.13 - t * 1.7)
          + (u < 0.15 ? 0.15 * Math.max(0, Math.sin(t * 7.5)) : 0);
        return Math.max(0, Math.min(1, v));
      });
      window.sqFrame({ bands, level: 0.6, peakL: 0.55 + 0.1 * Math.sin(t * 5), peakR: 0.5 + 0.1 * Math.sin(t * 4), lim: 0, lev: 0 });
    }, 33);
  }
}
