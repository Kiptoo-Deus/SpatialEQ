<script>
  // Three.js port of shared/Swift/UI/SceneController.swift + macos/App/UI/SceneView3D.swift:
  // listener head, draggable virtual sources (or two speakers), spectrum terrain, EQ ribbon.
  import { onMount } from "svelte";
  import * as THREE from "three";
  import { EffectComposer } from "three/examples/jsm/postprocessing/EffectComposer.js";
  import { RenderPass } from "three/examples/jsm/postprocessing/RenderPass.js";
  import { UnrealBloomPass } from "three/examples/jsm/postprocessing/UnrealBloomPass.js";
  import { OutputPass } from "three/examples/jsm/postprocessing/OutputPass.js";
  import { ui, edit, spectrum, BANDS, HISTORY } from "./bridge.svelte.js";
  import { P, SRC, srcId, CHANNELS, UPMIX, eqResponse } from "./params.js";

  let host;
  const p = $derived(ui.p);

  const CH_COLORS = [[0.30, 0.70, 1.00], [1.00, 0.40, 0.45], [0.95, 0.95, 0.95], [0.55, 0.45, 1.00],
    [1.00, 0.55, 0.85], [0.35, 0.95, 0.65], [1.00, 0.80, 0.30]];
  // The app's colours are sRGB values; Three.js works in linear light, so convert them.
  const srgb = (r, g, b) => new THREE.Color().setRGB(r, g, b, THREE.SRGBColorSpace);
  const lin = (rgb) => { const c = srgb(...rgb); return [c.r, c.g, c.b]; };
  const ACCENT = srgb(0.36, 0.85, 0.95);
  const BG = srgb(0.035, 0.04, 0.06);
  const LIN_CH = CH_COLORS.map(lin);
  const FLOOR_Y = -1.25, T_W = 7, T_D = 6.5, T_FRONT = 2.2, COLS = 64, ROWS = 48;

  const position = (az, el, d) => {
    const a = (az * Math.PI) / 180, e = (el * Math.PI) / 180;
    return new THREE.Vector3(d * Math.sin(a) * Math.cos(e), d * Math.sin(e), -d * Math.cos(a) * Math.cos(e));
  };

  function heat(v) {
    const stops = [[0, [0.03, 0.05, 0.15]], [0.35, [0.10, 0.35, 0.75]], [0.6, [0.30, 0.85, 0.95]],
      [0.8, [0.98, 0.60, 0.25]], [1, [1.0, 0.95, 0.85]]];
    for (let i = 1; i < stops.length; i++) {
      if (v <= stops[i][0]) {
        const [t0, c0] = stops[i - 1], [t1, c1] = stops[i], t = (v - t0) / (t1 - t0);
        return c0.map((c, k) => c + (c1[k] - c) * t);
      }
    }
    return stops[stops.length - 1][1];
  }
  // heat() in linear light, tabulated: the terrain looks up 3072 colours per frame.
  const HEAT_N = 256;
  const HEAT_LUT = new Float32Array(HEAT_N * 3);
  for (let i = 0; i < HEAT_N; i++) HEAT_LUT.set(lin(heat(i / (HEAT_N - 1))), i * 3);

  function labelSprite(text) {
    const c = document.createElement("canvas");
    c.width = 128; c.height = 64;
    const g = c.getContext("2d");
    g.font = "600 44px -apple-system, 'Segoe UI', sans-serif";
    g.fillStyle = "white";
    g.textBaseline = "middle";
    g.fillText(text, 4, 34);
    const tex = new THREE.CanvasTexture(c);
    tex.colorSpace = THREE.SRGBColorSpace;
    const s = new THREE.Sprite(new THREE.SpriteMaterial({ map: tex, depthWrite: false, transparent: true }));
    s.scale.set(0.5, 0.25, 1);
    s.center.set(0, 0);
    return s;
  }

  onMount(() => {
    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    renderer.toneMapping = THREE.NoToneMapping;
    host.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    scene.background = BG;
    scene.fog = new THREE.Fog(BG, 9, 18);

    const camera = new THREE.PerspectiveCamera(45, 1, 0.05, 60);
    const rig = new THREE.Object3D();
    rig.rotation.order = "YXZ";
    rig.add(camera);
    scene.add(rig);
    const resetCamera = () => { rig.rotation.set(-0.5, 0, 0); camera.position.set(0, 0, 8); };
    resetCamera();

    scene.add(new THREE.AmbientLight(0xffffff, 0.25));
    const key = new THREE.DirectionalLight(0xffffff, 1.6);
    key.position.set(-3, 4, 4);
    scene.add(key);
    const rim = new THREE.PointLight(ACCENT, 6, 0, 1.2);
    rim.position.set(0, 1.5, -3);
    scene.add(rim);

    // ---- head
    const skin = new THREE.MeshStandardMaterial({ color: srgb(0.32, 0.32, 0.32), metalness: 0.25, roughness: 0.35 });
    const head = new THREE.Group();
    const skull = new THREE.Mesh(new THREE.SphereGeometry(0.36, 48, 32), skin);
    skull.scale.set(0.85, 1.05, 1.0);
    head.add(skull);
    for (const side of [-1, 1]) {
      const ear = new THREE.Mesh(new THREE.SphereGeometry(0.1, 24, 16), skin);
      ear.scale.set(0.35, 1.0, 0.65);
      ear.position.set(side * 0.31, 0, 0.02);
      head.add(ear);
    }
    const nose = new THREE.Mesh(new THREE.ConeGeometry(0.06, 0.14, 24), skin);
    nose.rotation.x = -Math.PI / 2;
    nose.position.set(0, -0.02, -0.38);
    head.add(nose);
    const arrow = new THREE.Mesh(new THREE.ConeGeometry(0.08, 0.3, 24),
      new THREE.MeshBasicMaterial({ color: ACCENT, transparent: true, opacity: 0.8 }));
    arrow.rotation.x = -Math.PI / 2;
    arrow.position.set(0, -0.55, -0.6);
    head.add(arrow);
    scene.add(head);

    // ---- rings
    const ringMat = new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.12 });
    for (const r of [1, 2, 3]) {
      const ring = new THREE.Mesh(new THREE.TorusGeometry(r, 0.006, 6, 160), ringMat);
      ring.rotation.x = Math.PI / 2;
      ring.position.y = -0.55;
      scene.add(ring);
    }

    // ---- sources
    const sources = CH_COLORS.map((rgb, i) => {
      const color = srgb(...rgb);
      const node = new THREE.Group();
      const orb = new THREE.Mesh(new THREE.SphereGeometry(0.13, 32, 20), new THREE.MeshBasicMaterial({ color }));
      orb.userData.source = i;
      const halo = new THREE.Mesh(new THREE.SphereGeometry(0.22, 32, 20),
        new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.15, depthWrite: false }));
      halo.userData.source = i;
      const label = labelSprite(CHANNELS[i]);
      label.position.set(-0.08, 0.2, 0);
      node.add(orb, halo, label);
      node.userData = { halo };
      scene.add(node);
      return node;
    });

    // ---- speakers (speaker mode / stereo headphones)
    const speakers = [0, 1].map((i) => {
      const node = new THREE.Group();
      const box = new THREE.Mesh(new THREE.BoxGeometry(0.38, 0.6, 0.32),
        new THREE.MeshStandardMaterial({ color: srgb(0.18, 0.18, 0.18), roughness: 0.5 }));
      box.userData.speaker = i;
      const cone = new THREE.Mesh(new THREE.CylinderGeometry(0.11, 0.11, 0.02, 32),
        new THREE.MeshBasicMaterial({ color: srgb(...CH_COLORS[i]) }));
      cone.rotation.x = Math.PI / 2;
      cone.position.set(0, -0.08, 0.17);
      node.add(box, cone);
      node.userData = { cone };
      scene.add(node);
      return node;
    });

    // ---- rays
    const rayGeo = new THREE.BufferGeometry();
    rayGeo.setAttribute("position", new THREE.BufferAttribute(new Float32Array(14 * 3), 3));
    rayGeo.setAttribute("color", new THREE.BufferAttribute(new Float32Array(14 * 4), 4));
    const rays = new THREE.LineSegments(rayGeo, new THREE.LineBasicMaterial({ vertexColors: true, transparent: true,
      blending: THREE.AdditiveBlending, depthWrite: false }));
    scene.add(rays);

    // ---- terrain
    const tPos = new Float32Array(COLS * ROWS * 3), tCol = new Float32Array(COLS * ROWS * 3);
    const idx = [];
    for (let r = 0; r < ROWS - 1; r++) for (let c = 0; c < COLS - 1; c++) {
      const a = r * COLS + c, b = a + 1, d = a + COLS, e = d + 1;
      idx.push(a, d, b, b, d, e);
    }
    const terrainGeo = new THREE.BufferGeometry();
    terrainGeo.setAttribute("position", new THREE.BufferAttribute(tPos, 3));
    terrainGeo.setAttribute("color", new THREE.BufferAttribute(tCol, 3));
    terrainGeo.setIndex(idx);
    const terrain = new THREE.Mesh(terrainGeo, new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true,
      opacity: 0.55, side: THREE.DoubleSide, depthWrite: false }));
    terrain.renderOrder = -1;
    const terrainLines = new THREE.Mesh(terrainGeo, new THREE.MeshBasicMaterial({ color: 0xffffff, wireframe: true,
      transparent: true, opacity: 0.06 }));
    scene.add(terrain, terrainLines);

    // ---- EQ ribbon
    const RN = 160;
    const ribbonGeo = new THREE.BufferGeometry();
    const rPos = new Float32Array(RN * 2 * 3), rCol = new Float32Array(RN * 2 * 3), rIdx = [];
    for (let i = 0; i < RN - 1; i++) { const a = i * 2; rIdx.push(a, a + 1, a + 2, a + 1, a + 3, a + 2); }
    for (let i = 0; i < RN; i++) rCol.set([...lin([1, 0.62, 0.27]), ...lin([1, 0.75, 0.4])], i * 6);
    ribbonGeo.setAttribute("position", new THREE.BufferAttribute(rPos, 3));
    ribbonGeo.setAttribute("color", new THREE.BufferAttribute(rCol, 3));
    ribbonGeo.setIndex(rIdx);
    const ribbon = new THREE.Mesh(ribbonGeo, new THREE.MeshBasicMaterial({ vertexColors: true, side: THREE.DoubleSide,
      toneMapped: false }));
    scene.add(ribbon);
    const ribbonFreqs = Array.from({ length: RN }, (_, i) => 20 * Math.pow(1000, i / (RN - 1)));
    let ribbonVersion = -1, terrainFrame = -1;

    // ---- post: bloom like SCNCamera (threshold 0.55, intensity 1.4)
    const composer = new EffectComposer(renderer);
    composer.addPass(new RenderPass(scene, camera));
    const bloom = new UnrealBloomPass(new THREE.Vector2(256, 256), 0.75, 0.45, 0.62);
    composer.addPass(bloom);
    composer.addPass(new OutputPass());

    let dirty = true; // something other than audio changed (camera, size): render the next frame

    function resize() {
      const w = host.clientWidth, h = host.clientHeight;
      if (!w || !h) return;
      renderer.setSize(w, h);
      composer.setSize(w, h);
      bloom.setSize(Math.ceil(w / 2), Math.ceil(h / 2)); // bloom is blurry anyway: half resolution is ~4x cheaper
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
      dirty = true;
    }
    const ro = new ResizeObserver(resize);
    ro.observe(host);
    resize();

    let mode = "none", last = [0, 0], dragSource = -1; // pointer interaction state

    // ---- per-frame update
    function update() {
      const s = ui.p;
      const headphones = s[P.mode] === 0, spatial = headphones && !!s[P.spatialOn];
      const active = spatial ? [2, 5, 7][s[P.upmix]] : 0;
      const pulse = 1 + ui.level * 0.6;
      head.rotation.y = (-s[P.headYaw] * Math.PI) / 180;

      const rp = rayGeo.attributes.position.array, rc = rayGeo.attributes.color.array;
      rp.fill(0); rc.fill(0);
      sources.forEach((node, i) => {
        node.visible = i < active;
        if (!node.visible) return;
        const v = position(s[srcId(i, SRC.az)], s[srcId(i, SRC.el)], s[srcId(i, SRC.dist)]);
        node.position.copy(v);
        const k = i === dragSource ? pulse * 1.4 : pulse;
        node.userData.halo.scale.setScalar(k);
        rp.set([0, 0, 0, v.x, v.y, v.z], i * 6);
        rc.set([...LIN_CH[i], 0, ...LIN_CH[i], 0.5], i * 8);
      });
      rays.visible = active > 0;
      rayGeo.attributes.position.needsUpdate = rayGeo.attributes.color.needsUpdate = true;

      speakers.forEach((node, i) => {
        node.visible = !spatial;
        const half = (headphones ? 30 : s[P.speakerSpan] / 2) * (i === 0 ? -1 : 1);
        node.position.copy(position(half, 0, 2.2));
        node.rotation.y = (-half * Math.PI) / 180;
        node.userData.cone.scale.set(pulse, 1, pulse);
      });

      // terrain from the spectrum history, only when a new spectrum frame arrived
      if (spectrum.frame !== terrainFrame) {
        terrainFrame = spectrum.frame;
        const hist = spectrum.history;
        for (let r = 0; r < ROWS; r++) {
          const row = ((spectrum.head + r) % HISTORY) * BANDS;
          const z = T_FRONT - (r / (ROWS - 1)) * T_D, fade = 1 - r / ROWS;
          for (let c = 0; c < COLS; c++) {
            const v = hist[row + c], o = (r * COLS + c) * 3;
            tPos[o] = (c / (COLS - 1) - 0.5) * T_W; tPos[o + 1] = FLOOR_Y + v * 1.3; tPos[o + 2] = z;
            const k = Math.round(Math.min(1, Math.max(0, v)) * (HEAT_N - 1)) * 3;
            tCol[o] = HEAT_LUT[k] * fade; tCol[o + 1] = HEAT_LUT[k + 1] * fade; tCol[o + 2] = HEAT_LUT[k + 2] * fade;
          }
        }
        terrainGeo.attributes.position.needsUpdate = terrainGeo.attributes.color.needsUpdate = true;
      }
      terrain.material.opacity = s[P.bypass] ? 0.35 * 0.55 : 0.55;

      // ribbon only when the EQ changes
      if (spectrum.paramsVersion !== ribbonVersion) {
        ribbonVersion = spectrum.paramsVersion;
        const db = eqResponse(s, ribbonFreqs);
        for (let i = 0; i < RN; i++) {
          const x = (i / (RN - 1) - 0.5) * T_W, y = FLOOR_Y + 0.9 + (Math.max(-18, Math.min(18, db[i])) / 18) * 0.8;
          rPos.set([x, y - 0.025, T_FRONT + 0.15, x, y + 0.025, T_FRONT + 0.15], i * 6);
        }
        ribbonGeo.attributes.position.needsUpdate = true;
      }
    }

    let raf, visible = true;
    const io = new IntersectionObserver((e) => (visible = e[0].isIntersecting));
    io.observe(host);
    // Render only when something changed (a spectrum frame at 30 Hz, an edit, the camera), at most 60 fps.
    let lastFrame = -1, lastParams = -1, lastRender = 0;
    const loop = (ms = 0) => {
      raf = requestAnimationFrame(loop);
      if (!visible || document.hidden) return;
      const changed = dirty || spectrum.frame !== lastFrame || spectrum.paramsVersion !== lastParams;
      if (!changed || ms - lastRender < 15) return;
      dirty = false; lastFrame = spectrum.frame; lastParams = spectrum.paramsVersion; lastRender = ms;
      update();
      composer.render();
    };
    loop();

    // ---- interaction (mirrors OrbitSceneView)
    const ray = new THREE.Raycaster();
    const ndc = (e) => {
      const r = renderer.domElement.getBoundingClientRect();
      return new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1);
    };
    const ground = (e, height) => {
      ray.setFromCamera(ndc(e), camera);
      const o = ray.ray.origin, d = ray.ray.direction;
      if (Math.abs(d.y) < 1e-6) return null;
      const t = (height - o.y) / d.y;
      return t > 0 ? o.clone().addScaledVector(d, t) : null;
    };
    const el = renderer.domElement;
    el.addEventListener("pointerdown", (e) => {
      last = [e.clientX, e.clientY];
      dirty = true;
      if (e.detail === 2) { resetCamera(); return; }
      el.setPointerCapture(e.pointerId);
      ray.setFromCamera(ndc(e), camera);
      const hits = ray.intersectObjects([...sources.filter((s) => s.visible), ...speakers.filter((s) => s.visible)], true);
      const hit = hits.find((h) => h.object.userData.source !== undefined || h.object.userData.speaker !== undefined);
      if (hit && hit.object.userData.source !== undefined) {
        mode = "source"; dragSource = hit.object.userData.source;
        for (const f of [SRC.az, SRC.el, SRC.dist]) edit(srcId(dragSource, f), ui.p[srcId(dragSource, f)], "begin");
      } else if (hit && hit.object.userData.speaker !== undefined && ui.p[P.mode] === 1) {
        mode = "speaker"; edit(P.speakerSpan, ui.p[P.speakerSpan], "begin");
      } else mode = "orbit";
    });
    el.addEventListener("pointermove", (e) => {
      if (mode === "none") return;
      dirty = true;
      const dx = e.clientX - last[0], dy = e.clientY - last[1];
      last = [e.clientX, e.clientY];
      if (mode === "orbit") {
        rig.rotation.y -= dx * 0.008;
        rig.rotation.x = Math.max(-1.45, Math.min(0.2, rig.rotation.x - dy * 0.008));
      } else if (mode === "source") {
        const i = dragSource, s = ui.p;
        let az = s[srcId(i, SRC.az)], elv = s[srcId(i, SRC.el)], dist = s[srcId(i, SRC.dist)];
        if (e.altKey) elv = Math.max(-60, Math.min(75, elv - dy * 0.5));
        else {
          const hit = ground(e, position(az, elv, dist).y);
          if (!hit) return;
          const horiz = Math.hypot(hit.x, hit.z), cosEl = Math.cos((elv * Math.PI) / 180);
          az = (Math.atan2(hit.x, -hit.z) * 180) / Math.PI;
          dist = Math.max(0.5, Math.min(4, horiz / Math.max(cosEl, 0.2)));
        }
        edit(srcId(i, SRC.az), az, "set"); edit(srcId(i, SRC.el), elv, "set"); edit(srcId(i, SRC.dist), dist, "set");
      } else if (mode === "speaker") {
        const hit = ground(e, 0);
        if (hit) edit(P.speakerSpan, Math.max(10, Math.min(120, Math.abs((Math.atan2(hit.x, -hit.z) * 180) / Math.PI) * 2)), "set");
      }
    });
    const end = () => {
      if (mode === "source") for (const f of [SRC.az, SRC.el, SRC.dist]) edit(srcId(dragSource, f), ui.p[srcId(dragSource, f)], "end");
      if (mode === "speaker") edit(P.speakerSpan, ui.p[P.speakerSpan], "end");
      mode = "none"; dragSource = -1; dirty = true;
    };
    el.addEventListener("pointerup", end);
    el.addEventListener("pointercancel", end);
    el.addEventListener("wheel", (e) => {
      e.preventDefault();
      camera.position.z = Math.max(3.5, Math.min(16, camera.position.z * (1 + e.deltaY * 0.004)));
      dirty = true;
    }, { passive: false });

    return () => { cancelAnimationFrame(raf); ro.disconnect(); io.disconnect(); renderer.dispose(); };
  });

  const modeTitle = $derived(p[P.mode] === 1 ? "Speakers" : p[P.spatialOn] ? `Headphones · ${UPMIX[p[P.upmix]]}` : "Headphones · Stereo");
  const hint = $derived(p[P.mode] === 1 ? "Drag a speaker to set the span · drag to orbit · scroll to zoom"
    : p[P.spatialOn] ? "Drag sources to move them · ⌥-drag for height · double-click resets view"
    : "Turn on Spatial to place sound around you");
</script>

<div class="scene">
  <div class="canvas" bind:this={host}></div>
  <div class="hint">
    <div class="title">{modeTitle}</div>
    <div class="caption dim">{hint}</div>
  </div>
</div>

<style>
  .scene {
    position: relative; flex: 1; min-width: 0; min-height: 0;
    border-radius: 12px; overflow: hidden; border: 1px solid var(--panel-border);
  }
  .canvas { position: absolute; inset: 0; }
  .canvas :global(canvas) { display: block; }
  .hint {
    position: absolute; top: 12px; left: 12px; padding: 10px; border-radius: 8px;
    background: rgba(0, 0, 0, 0.35); pointer-events: none; display: flex; flex-direction: column; gap: 4px;
  }
  .title { font-size: 13px; font-weight: 600; }
</style>
