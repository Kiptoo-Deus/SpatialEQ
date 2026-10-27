// Parameter IDs and plain-value ranges. Must match plugin/source/params.h.
export const P = {
  bypass: 0, preamp: 1, eqOn: 2, mode: 3, spatialOn: 4, upmix: 5, width: 6, xtcOn: 7, xtcStrength: 8,
  speakerSpan: 9, roomSize: 10, reverbMix: 11, bassBoost: 12, dialogue: 13, levelerOn: 14,
  levelerTarget: 15, limiterOn: 16, limiterCeiling: 17, outputGain: 18, headYaw: 19, numBands: 20,
};
export const BAND_BASE = 100, SOURCE_BASE = 300;
export const BAND = { on: 0, type: 1, freq: 2, gain: 3, q: 4 };
export const SRC = { az: 0, el: 1, dist: 2, gain: 3 };
export const MAX_BANDS = 16, NUM_SOURCES = 7;

export const bandId = (b, f) => BAND_BASE + b * 10 + f;
export const srcId = (s, f) => SOURCE_BASE + s * 10 + f;

export const BAND_TYPES = ["Peak", "Low Shelf", "High Shelf", "Low Pass", "High Pass"];
export const hasGain = (type) => type <= 2;
export const UPMIX = ["Stereo", "Virtual 5.1", "Virtual 7.1"];
export const CHANNELS = ["L", "R", "C", "Ls", "Rs", "Lb", "Rb"];
export const DEFAULT_AZ = [-30, 30, 0, -110, 110, -150, 150];

export const PRESETS = ["Flat", "Bass Boost", "Treble Lift", "Loudness", "Vocal & Dialogue", "Podcast", "Cinema",
  "Music Hall", "Studio Speakers", "Gaming", "Wide Speakers", "Laptop Speakers"];

/** Plug-in defaults (same as the app's SoundSettings()). Used by the browser preview. */
export function defaultParams() {
  const p = {};
  Object.assign(p, {
    [P.bypass]: 0, [P.preamp]: 0, [P.eqOn]: 1, [P.mode]: 0, [P.spatialOn]: 0, [P.upmix]: 0, [P.width]: 1,
    [P.xtcOn]: 0, [P.xtcStrength]: 0.6, [P.speakerSpan]: 30, [P.roomSize]: 0.35, [P.reverbMix]: 0,
    [P.bassBoost]: 0, [P.dialogue]: 0, [P.levelerOn]: 0, [P.levelerTarget]: -20, [P.limiterOn]: 1,
    [P.limiterCeiling]: -1, [P.outputGain]: 0, [P.headYaw]: 0, [P.numBands]: 10,
  });
  const freqs = [32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000, 100, 300, 700, 1500, 3000, 6000];
  for (let b = 0; b < MAX_BANDS; b++) {
    p[bandId(b, BAND.on)] = 1;
    p[bandId(b, BAND.type)] = b === 0 ? 1 : b === 9 ? 2 : 0;
    p[bandId(b, BAND.freq)] = freqs[b];
    p[bandId(b, BAND.gain)] = 0;
    p[bandId(b, BAND.q)] = 1;
  }
  for (let s = 0; s < NUM_SOURCES; s++) {
    p[srcId(s, SRC.az)] = DEFAULT_AZ[s];
    p[srcId(s, SRC.el)] = 0;
    p[srcId(s, SRC.dist)] = 1.5;
    p[srcId(s, SRC.gain)] = 0;
  }
  return p;
}

/** Magnitude response (dB) of preamp + enabled bands: RBJ biquads, the same maths as the engine. */
export function eqResponse(p, freqs, fs = 48000) {
  const out = new Float32Array(freqs.length).fill(p[P.preamp]);
  if (!p[P.eqOn]) return out.fill(0);
  const n = p[P.numBands];
  for (let b = 0; b < n; b++) {
    if (!p[bandId(b, BAND.on)]) continue;
    const type = p[bandId(b, BAND.type)];
    const f0 = Math.min(Math.max(p[bandId(b, BAND.freq)], 10), fs * 0.49);
    const g = p[bandId(b, BAND.gain)], q = Math.max(0.05, p[bandId(b, BAND.q)]);
    const A = Math.pow(10, g / 40), w0 = 2 * Math.PI * f0 / fs, c = Math.cos(w0), al = Math.sin(w0) / (2 * q), sA = Math.sqrt(A);
    let b0, b1, b2, a0, a1, a2;
    if (type === 0) { b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; }
    else if (type === 1) {
      b0 = A * ((A + 1) - (A - 1) * c + 2 * sA * al); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - 2 * sA * al);
      a0 = (A + 1) + (A - 1) * c + 2 * sA * al; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - 2 * sA * al;
    } else if (type === 2) {
      b0 = A * ((A + 1) + (A - 1) * c + 2 * sA * al); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - 2 * sA * al);
      a0 = (A + 1) - (A - 1) * c + 2 * sA * al; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - 2 * sA * al;
    } else if (type === 3) { b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; }
    else { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; }
    for (let i = 0; i < freqs.length; i++) {
      const w = 2 * Math.PI * freqs[i] / fs, cw = Math.cos(w), sw = Math.sin(w), c2 = Math.cos(2 * w), s2 = Math.sin(2 * w);
      const nr = b0 + b1 * cw + b2 * c2, ni = -(b1 * sw + b2 * s2);
      const dr = a0 + a1 * cw + a2 * c2, di = -(a1 * sw + a2 * s2);
      out[i] += 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di) + 1e-30);
    }
  }
  return out;
}
