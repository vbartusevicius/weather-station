// AeroMet Web UI: display-only. Every number shown here is computed by the station firmware
// (src/core pipeline) and published in /api/state, /api/history and /api/history/minutes. The browser
// only converts units, formats text and draws. JSON null (sensor missing / not computable) => "—".

const COMPASS_16 = ["N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"];
const BEAUFORT_NAMES = [
  "Calm","Light Air","Light Breeze","Gentle Breeze","Moderate Breeze","Fresh Breeze",
  "Strong Breeze","Near Gale","Gale","Strong Gale","Storm","Violent Storm","Hurricane"
];

const UNIT_SYSTEMS = [
  { id: "metric",   label: "Metric",   wMul: 1.0,     wUnit: "m/s",  tFn: c => c,            tUnit: "°", pMul: 1.0,     pUnit: "hPa", rMul: 1.0,     rUnit: "mm", lMul: 1.0,    lUnit: "kg/m²", dMul: 1.0,      dUnit: "km", hMul: 1.0,     hUnit: "m" },
  { id: "imperial", label: "Imperial", wMul: 2.23694, wUnit: "mph",  tFn: c => c*1.8 + 32.0, tUnit: "°", pMul: 0.02953, pUnit: "inHg",rMul: 0.03937, rUnit: "in", lMul: 0.2048, lUnit: "lb/ft²", dMul: 0.621371, dUnit: "mi", hMul: 3.28084, hUnit: "ft" },
  { id: "nautical", label: "Knots",    wMul: 1.94384, wUnit: "kt",   tFn: c => c,            tUnit: "°", pMul: 1.0,     pUnit: "hPa", rMul: 1.0,     rUnit: "mm", lMul: 1.0,    lUnit: "kg/m²", dMul: 0.539957, dUnit: "nmi", hMul: 3.28084, hUnit: "ft" },
];

let unitIdx = 0;
let lastState = null;
let minuteHist = null;      // /api/history/minutes: 60 x 1-minute chart series for the cards
let trendTier = "minute";   // Trends tab: minute (1 h) | hour (24 h) | day (31 d)
let trendData = null;       // /api/history?tier=...
let rain31d = null;
let stationCfg = null;
let clockPushed = false;

const el = (id) => document.getElementById(id);
const isNum = (v) => typeof v === "number" && Number.isFinite(v);
// Format a nullable number: fv(v, decimals, suffix) => "12.3 suffix" or "—"
function fv(v, dec = 1, suffix = "") {
  return isNum(v) ? `${v.toFixed(dec)}${suffix}` : "—";
}
function setText(id, text) {
  const node = el(id);
  if (node) node.textContent = text;
}
function clampPct(v, lo, hi) {
  if (!isNum(v)) return 0;
  return Math.max(0, Math.min(100, ((v - lo) / (hi - lo)) * 100));
}
function degToCardinal(deg) {
  return isNum(deg) ? COMPASS_16[Math.floor(((deg + 11.25) % 360) / 22.5)] : "—";
}
function hhmm(min) {
  if (!isNum(min)) return "—";
  const m = Math.round(min) % 1440;
  return `${String(Math.floor(m / 60)).padStart(2, "0")}:${String(m % 60).padStart(2, "0")}`;
}
const conv = {
  t: (c) => (isNum(c) ? UNIT_SYSTEMS[unitIdx].tFn(c) : null),
  w: (v) => (isNum(v) ? v * UNIT_SYSTEMS[unitIdx].wMul : null),
  p: (v) => (isNum(v) ? v * UNIT_SYSTEMS[unitIdx].pMul : null),
  r: (v) => (isNum(v) ? v * UNIT_SYSTEMS[unitIdx].rMul : null),
  l: (v) => (isNum(v) ? v * UNIT_SYSTEMS[unitIdx].lMul : null),
  d: (v) => (isNum(v) ? v * UNIT_SYSTEMS[unitIdx].dMul : null),
  h: (v) => (isNum(v) ? v * UNIT_SYSTEMS[unitIdx].hMul : null),
};
const postJson = (url, body) =>
  fetch(url, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }).then(r => r.json());

// ---------------------------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------------------------
document.querySelectorAll("[data-trange]").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll("[data-trange]").forEach((b) => b.classList.remove("active"));
    btn.classList.add("active");
    trendTier = btn.dataset.trange;
    loadTrends();
  });
});

(function initCompassTicks() {
  const g = el("compass-ticks");
  if (!g) return;
  let s = "";
  for (let d = 0; d < 360; d += 10) {
    const rad = (d * Math.PI) / 180;
    const r1 = d % 30 === 0 ? 84 : 88;
    const r2 = 93;
    const x1 = 115 + r1 * Math.sin(rad), y1 = 115 - r1 * Math.cos(rad);
    const x2 = 115 + r2 * Math.sin(rad), y2 = 115 - r2 * Math.cos(rad);
    s += `<line x1="${x1.toFixed(1)}" y1="${y1.toFixed(1)}" x2="${x2.toFixed(1)}" y2="${y2.toFixed(1)}" stroke="var(--border)" stroke-width="${d % 90 === 0 ? 2 : 1}"/>`;
  }
  g.innerHTML = s;
})();

function showTab(name) {
  document.querySelectorAll(".tab").forEach((b) => b.classList.toggle("active", b.dataset.tab === name));
  document.querySelectorAll(".panel").forEach((p) => p.classList.toggle("active", p.id === "tab-" + name));
  if (name === "trends") loadTrends();
  if (name === "settings") loadConfig();
}
document.querySelectorAll(".tab").forEach((btn) => btn.addEventListener("click", () => showTab(btn.dataset.tab)));
el("loc-banner-btn").addEventListener("click", () => showTab("settings"));

el("unit-toggle").addEventListener("click", () => {
  unitIdx = (unitIdx + 1) % UNIT_SYSTEMS.length;
  el("unit-toggle").textContent = UNIT_SYSTEMS[unitIdx].label;
  if (lastState) applyTelemetry(lastState);
  renderTrends();
});

el("theme-toggle").addEventListener("click", () => {
  const html = document.documentElement;
  const next = html.getAttribute("data-theme") === "light" ? "dark" : "light";
  html.setAttribute("data-theme", next);
  el("theme-toggle").textContent = next === "light" ? "☾" : "☀";
});

// Development server only (/api/config reports "simulated": true): pick a simulated scenario.
el("scenario-select").addEventListener("change", (ev) => {
  postJson("/api/action", { action: "set_scenario", scenario: ev.target.value })
    .then(() => Promise.all([loadMinuteHistory(), loadRain31d(), loadTrends()]))
    .then(() => fetch("/api/state").then(r => r.json()))
    .then(applyTelemetry)
    .catch(() => {});
});

document.querySelectorAll(".card.clickable").forEach((card) => {
  card.addEventListener("click", () => {
    if (lastState) openInspectModal(card.dataset.inspect, lastState);
  });
});
el("modal-close").addEventListener("click", () => { el("inspect-modal").style.display = "none"; });
el("inspect-modal").addEventListener("click", (ev) => {
  if (ev.target.id === "inspect-modal") ev.target.style.display = "none";
});

function openInspectModal(kind, d) {
  const e = d.environment, w = d.wind, r = d.rain, s = d.solar, a = d.astronomy, q = d.air_quality, l = d.lightning;
  let title = "";
  let rows = [];
  if (kind === "wind") {
    title = "🧭 Ultrasonic Wind (WMO statistics)";
    rows = [
      ["3-s wind / direction", `${fv(w.speed_m_s, 2, " m/s")} · ${fv(w.dir_deg, 0, "°")}`],
      ["2-min / 10-min mean", `${fv(w.avg_2m_m_s, 2)} / ${fv(w.avg_10m_m_s, 2, " m/s")} @ ${fv(w.dir_10m_deg, 0, "°")}`],
      ["Gust / lull (max / min 3-s mean, 10 min)", `${fv(w.gust_m_s, 2)} / ${fv(w.lull_m_s, 2, " m/s")}`],
      ["Turbulence intensity", fv(w.turbulence_pct, 0, " %")],
      ["Direction change 3 h", fv(w.dir_change_3h_deg, 0, "°")],
      ["Speed of sound / sonic temperature", `${fv(w.sound_m_s, 1, " m/s")} / ${fv(w.acoustic_temp_c, 1, " °C")}`],
      ["Mast tilt", fv(w.tilt_deg, 2, "°")],
    ];
  } else if (kind === "temp") {
    title = "🌡 Temperature & Humidity";
    rows = [
      ["Air temperature (radiation-corrected)", fv(e.temp_c, 2, " °C")],
      ["Shield solar-heating correction", fv(e.solar_offset_c, 2, " °C")],
      ["Dew point / wet-bulb", `${fv(e.dew_point_c, 2)} / ${fv(e.wet_bulb_c, 2, " °C")}`],
      ["Wind chill / heat index", `${fv(e.wind_chill_c, 1)} / ${fv(e.heat_index_c, 1, " °C")}`],
      ["Absolute humidity / VPD", `${fv(e.abs_humidity_g_m3, 1, " g/m³")} / ${fv(e.vpd_kpa, 2, " kPa")}`],
      ["Cloud base (LCL)", fv(e.cloud_base_m, 0, " m")],
      ["24 h range / trend", `${fv(e.temp_min_24h_c, 1)} … ${fv(e.temp_max_24h_c, 1, " °C")} · ${fv(e.temp_trend_c_per_h, 1, " °C/h")}`],
    ];
  } else if (kind === "baro") {
    title = "⏲ Barometer";
    rows = [
      ["Sea-level pressure (QNH)", fv(e.qnh_hpa, 2, " hPa")],
      ["Station pressure (wind-compensated)", `${fv(e.pressure_hpa, 2, " hPa")} (port correction ${fv(e.pressure_wind_corr_hpa, 2, " hPa")})`],
      ["3 h change = synoptic + solar tide", `${fv(e.pressure_3h_delta_hpa, 2)} = ${fv(e.synoptic_3h_delta_hpa, 2)} + ${fv(e.solar_tide_hpa, 2, " hPa")}`],
      ["Trend / forecast", `${e.pressure_trend} · ${e.forecast}`],
      ["Weather system / wind shift", `${e.front_type} · ${e.wind_shift}`],
      ["Air density", fv(e.air_density_kg_m3, 3, " kg/m³")],
      ["Pressure ripple (gusts / infrasound)", fv(e.pressure_ripple_pa, 2, " Pa")],
    ];
  } else if (kind === "rain") {
    title = "🌧 Piezo Rain & Hail";
    rows = [
      ["State / rate", `${r.state} · ${fv(r.rate_mm_h, 2, " mm/h")}`],
      ["Last hour / today / this shower", `${fv(r.last_hour_mm, 2)} / ${fv(r.today_mm, 2)} / ${fv(r.event_mm, 2, " mm")}`],
      ["Total since install", fv(r.total_mm, 1, " mm")],
      ["Drops / hail hits / max hail", `${r.drops} / ${r.hail_hits} / ${fv(r.hail_diam_mm, 1, " mm")}`],
      ["Mast-shake impacts rejected", String(r.rejected_shake)],
      ["Surface", r.wet ? "Wet" : "Dry"],
    ];
  } else if (kind === "solar") {
    title = a.is_night ? "🌙 Night Sky & Moon" : "☀ Solar Radiation & UV";
    rows = [
      ["Global irradiance / clear-sky", `${fv(s.irradiance_w_m2, 0)} / ${fv(s.clear_sky_w_m2, 0, " W/m²")}`],
      ["Visible / near-infrared", `${fv(s.visible_w_m2, 0)} / ${fv(s.nir_w_m2, 0, " W/m²")}`],
      ["UV index (fused) / source", `${fv(s.uv_index, 1)} · ${s.uv_source}`],
      ["UV index GUVB-C31SM (UV-B) / LTR-390", `${fv(s.uv_index_guvb, 1)} / ${fv(s.uv_index_ltr, 1)}`],
      ["UV-A / UV-B (estimated from UVI)", `${fv(s.uva_w_m2, 1)} / ${fv(s.uvb_w_m2, 2, " W/m²")}`],
      ["Illuminance", isNum(s.lux) ? (s.lux < 1 ? `${(s.lux * 1000).toFixed(2)} mlx` : `${Math.round(s.lux).toLocaleString()} lx`) : "—"],
      ["Sun altitude / azimuth", `${fv(a.sun_alt_deg, 1, "°")} / ${fv(a.sun_az_deg, 0, "°")}`],
      ["Moon", `${fv(a.moon_illum_pct, 0, " %")} · ${a.moon_phase} · age ${fv(a.moon_age_days, 1, " d")}`],
      ["Sky quality / Bortle (estimate)", `${fv(s.sqm, 2, " mag/arcsec²")} · ${fv(s.bortle, 0)}`],
    ];
  } else {
    title = "🍃 Air Quality & Lightning";
    rows = [
      ["US AQI (NowCast PM) / EU EAQI band", `${fv(q.aqi, 0)} ${q.level} / ${fv(q.eaqi, 0)}`],
      ["PM2.5 dry / raw (humidity correction)", `${fv(q.pm2_5, 1)} / ${fv(q.pm2_5_raw, 1, " µg/m³")}`],
      ["PM1.0 / PM10 (PMSA003I)", `${fv(q.pm1_0, 1)} / ${fv(q.pm10, 1, " µg/m³")}`],
      ["CO₂ (SCD41, pressure-compensated in sensor)", fv(q.co2_ppm, 0, " ppm")],
      ["VOC / NOx index (SGP41, relative to this site's last ~24 h)", `${fv(q.voc_index, 0)} / ${fv(q.nox_index, 0)}${q.gas_learning ? " (learning)" : ""}`],
      ["Lightning: last hour / today / total", `${l.strikes_1h} / ${l.strikes_today} / ${l.strikes_total}`],
      ["Distance (method) / closest 1 h", `${fv(l.distance_km, 2, " km")} (${l.method}) / ${fv(l.closest_1h_km, 1, " km")}`],
    ];
  }
  el("modal-title").textContent = title;
  el("modal-body").innerHTML = rows.map(([k, v]) => `<div class="m-kv"><span class="sub-lbl">${k}</span><b>${v}</b></div>`).join("");
  el("inspect-modal").style.display = "flex";
}

function renderSkyHeroVisual(d) {
  const hero = document.getElementById("sky-hero");
  const fx = document.getElementById("sky-fx");
  const icon = document.getElementById("hero-wx-icon");
  if (!hero || !fx || !icon) return;

  if (d.rain.raining) {
    hero.style.background = "linear-gradient(125deg, #0f172a 0%, #1e293b 60%, #091120 100%)";
    icon.innerHTML = `<svg viewBox="0 0 56 56" width="54" height="54"><path d="M16 34 a10 10 0 0 1 2 -19 a13 13 0 0 1 24 3 a9 9 0 0 1 2 16 z" fill="#94a3b8"/><line x1="20" y1="39" x2="16" y2="50" stroke="#38bdf8" stroke-width="3" stroke-linecap="round"/><line x1="30" y1="39" x2="26" y2="50" stroke="#38bdf8" stroke-width="3" stroke-linecap="round"/><line x1="40" y1="39" x2="36" y2="50" stroke="#38bdf8" stroke-width="3" stroke-linecap="round"/></svg>`;
    let drops = "";
    for (let i = 0; i < 24; i++) {
      const x = (i * 39) % 880;
      const y = (i * 19) % 95;
      drops += `<line x1="${x}" y1="${y}" x2="${x - 8}" y2="${y + 22}" stroke="#38bdf8" stroke-width="1.5" stroke-opacity="0.5"/>`;
    }
    fx.innerHTML = drops;
  } else if (d.environment.temp_c !== null && d.environment.temp_c <= 0.5) {
    hero.style.background = "linear-gradient(125deg, #082f49 0%, #0369a1 60%, #0c192c 100%)";
    icon.innerHTML = `<svg viewBox="0 0 56 56" width="54" height="54"><circle cx="28" cy="28" r="14" fill="none" stroke="#7dd3fc" stroke-width="3" stroke-dasharray="4 4"/><line x1="28" y1="8" x2="28" y2="48" stroke="#e0f2fe" stroke-width="2.5"/><line x1="8" y1="28" x2="48" y2="28" stroke="#e0f2fe" stroke-width="2.5"/></svg>`;
    fx.innerHTML = `<circle cx="760" cy="40" r="65" fill="#38bdf8" fill-opacity="0.12"/>`;
  } else {
    hero.style.background = "linear-gradient(125deg, #0b2956 0%, #0284c7 58%, #0f1b33 100%)";
    icon.innerHTML = `<svg viewBox="0 0 56 56" width="54" height="54"><circle cx="24" cy="22" r="11" fill="#facc15"/><path d="M22 38 a9 9 0 0 1 2 -17 a11 11 0 0 1 20 3 a8 8 0 0 1 1 14 z" fill="#f8fafc" fill-opacity="0.92"/></svg>`;
    fx.innerHTML = `<circle cx="780" cy="26" r="85" fill="#facc15" fill-opacity="0.18"/><circle cx="780" cy="26" r="42" fill="#facc15" fill-opacity="0.22"/>`;
  }
}

function renderRosePetals(roseArr) {
  const g = document.getElementById("rose-petals");
  if (!g || !Array.isArray(roseArr)) return;
  const maxPct = Math.max(10, ...roseArr);
  let html = "";
  roseArr.forEach((pct, i) => {
    const r = 36 + (pct / maxPct) * 44;
    const a0 = ((i * 22.5 - 8.5 - 90) * Math.PI) / 180;
    const a1 = ((i * 22.5 + 8.5 - 90) * Math.PI) / 180;
    const x0 = 115 + 36 * Math.cos(a0), y0 = 115 + 36 * Math.sin(a0);
    const x1 = 115 + r * Math.cos(a0),  y1 = 115 + r * Math.sin(a0);
    const x2 = 115 + r * Math.cos(a1),  y2 = 115 + r * Math.sin(a1);
    const x3 = 115 + 36 * Math.cos(a1), y3 = 115 + 36 * Math.sin(a1);
    html += `<polygon points="${x0.toFixed(1)},${y0.toFixed(1)} ${x1.toFixed(1)},${y1.toFixed(1)} ${x2.toFixed(1)},${y2.toFixed(1)} ${x3.toFixed(1)},${y3.toFixed(1)}" fill="rgba(56,189,248,0.28)" stroke="#38bdf8" stroke-width="0.7"/>`;
  });
  g.innerHTML = html;

  const secGrid = document.getElementById("sector-bars-grid");
  if (secGrid) {
    let bestIdx = 0;
    roseArr.forEach((v, idx) => { if (v > roseArr[bestIdx]) bestIdx = idx; });
    const domEl = document.getElementById("rose-dom-sector");
    if (domEl) domEl.textContent = `Dominant: ${COMPASS_16[bestIdx]} (${roseArr[bestIdx].toFixed(0)}%)`;
    secGrid.innerHTML = roseArr.map((pct, i) => `
      <div class="sec-item">
        <div class="sec-top"><span>${COMPASS_16[i]}</span><span>${pct.toFixed(0)}%</span></div>
        <div class="sec-bar-bg"><div class="sec-bar-fg" style="width:${Math.min(100, (pct / maxPct) * 100).toFixed(0)}%"></div></div>
      </div>`).join("");
  }
}

// Shared frame for all card charts (Rain, Lightning, Seismic): identical geometry, grid & axis labels.
// The Y axis sits just right of the widest tick label, so that label starts at x=0 (flush with the
// chart title above) and every gridline is attached to the axis like a conventional chart.
const CH = { x1: 316, yTop: 8, yMid: 32, yBot: 56, xLblY: 71 };
function chartFrame(yLabels, xLabels, zeroAtMid = false) {
  const labels = yLabels.map(String);
  const textW = (txt) => [...txt].reduce((w, c) => w + (".,".includes(c) ? 2.6 : (c === "+" || c === "-" ? 5.3 : 5.1)), 0);
  const x0 = Math.ceil(Math.max(...labels.map(textW))) + 4;
  const g = (y, solid) => `<line x1="${x0}" y1="${y}" x2="${CH.x1}" y2="${y}" stroke="rgba(142,162,198,${solid ? 0.28 : 0.18})"${solid ? "" : ' stroke-dasharray="2 2"'}/>`;
  const t = (y, txt) => `<text x="${x0 - 4}" y="${y + 3}" fill="#8ea2c6" font-size="9" text-anchor="end">${txt}</text>`;
  const xm = (x0 + CH.x1) * 0.5;
  const html =
    `<line x1="${x0}" y1="${CH.yTop}" x2="${x0}" y2="${CH.yBot}" stroke="rgba(142,162,198,0.28)"/>` +
    g(CH.yTop, false) + t(CH.yTop, labels[0]) +
    g(CH.yMid, zeroAtMid) + t(CH.yMid, labels[1]) +
    g(CH.yBot, !zeroAtMid) + t(CH.yBot, labels[2]) +
    `<text x="${x0}" y="${CH.xLblY}" fill="#8ea2c6" font-size="9" text-anchor="start">${xLabels[0]}</text>` +
    `<text x="${xm.toFixed(1)}" y="${CH.xLblY}" fill="#8ea2c6" font-size="9" text-anchor="middle">${xLabels[1]}</text>` +
    `<text x="${CH.x1}" y="${CH.xLblY}" fill="#8ea2c6" font-size="9" text-anchor="end">${xLabels[2]}</text>`;
  return { html, x0 };
}

// All card charts use 60 x 1-minute bars (index 0 = -59 min, index 59 = current minute)
const MINUTE_BINS = 60;
function minuteSeries(arr, liveValue) {
  const out = (Array.isArray(arr) ? arr.slice(-MINUTE_BINS) : []).map(v => Number(v) || 0);
  while (out.length < MINUTE_BINS) out.unshift(0);
  if (liveValue !== undefined) out[MINUTE_BINS - 1] = Math.max(out[MINUTE_BINS - 1], liveValue);
  return out;
}
function minuteBarGeom(frame) {
  const xStart = frame.x0 + 2;
  const w = (CH.x1 - xStart) / MINUTE_BINS;
  return { xStart, w, bw: Math.max(1.5, w - 1.0) };
}


function renderWaveAndSeismic(d) {
  const svg = el("spark-seismic");
  const z = d.seismic;
  if (!svg || !z) return;
  const ppv = z.ppv_mm_s;
  const maxAxisV = isNum(ppv) && ppv >= 1.0 ? 3.0 : 0.5;
  setText("seismic-badge", z.mmi);
  el("seismic-badge").className = z.mmi === "MMI I" || z.mmi === "—" ? "badge ok" : "badge warn";
  setText("seis-pga-big", fv(ppv, 2));
  setText("seis-freq-sub", isNum(z.dominant_hz) ? `Dominant ${z.dominant_hz.toFixed(0)} Hz · ${z.source}` : "—");
  setText("seis-ground-lbl", fv(z.pga_cm_s2, 1, " cm/s²"));
  el("seis-ground-fill").style.width = `${clampPct(z.pga_cm_s2, 0, 8.0).toFixed(0)}%`;
  setText("seis-freq-lbl", isNum(ppv) ? `Peak ±${ppv.toFixed(2)} mm/s` : "—");

  // 60-minute peak-velocity envelope: one mirrored ±PPV bar per minute.
  const frame = chartFrame([`+${maxAxisV.toFixed(1)}`, "0", `-${maxAxisV.toFixed(1)}`], ["-60m", "-30m", "Now"], true);
  const { xStart, w, bw } = minuteBarGeom(frame);
  const halfH = CH.yMid - CH.yTop;
  const peaks = minuteSeries(minuteHist && minuteHist.ppv_mm_s, isNum(ppv) ? ppv : undefined);
  const col = z.source !== "Ambient" && z.source !== "—" ? "#f59e0b" : "#22c55e";
  svg.innerHTML = frame.html + peaks.map((v, i) => {
    const h = Math.max(1.0, Math.min(1.0, v / maxAxisV) * halfH);
    const x = xStart + i * w;
    return `<rect x="${x.toFixed(1)}" y="${(CH.yMid - h).toFixed(1)}" width="${bw.toFixed(1)}" height="${(2 * h).toFixed(1)}" rx="0.8" fill="${col}" fill-opacity="${i === MINUTE_BINS - 1 ? 1.0 : 0.75}"/>`;
  }).join("");
}

function niceCeiling(val, minFloor) {
  const v = Math.max(minFloor, val);
  const mag = Math.pow(10, Math.floor(Math.log10(v)));
  const norm = v / mag;
  if (norm <= 1.2) return 1.2 * mag;
  if (norm <= 2.0) return 2.0 * mag;
  if (norm <= 3.0) return 3.0 * mag;
  if (norm <= 5.0) return 5.0 * mag;
  if (norm <= 8.0) return 8.0 * mag;
  return 10.0 * mag;
}

function renderSparkRain() {
  const svg = document.getElementById("spark-rain");
  if (!svg || !lastState || !lastState.rain) return;
  const u = UNIT_SYSTEMS[unitIdx];
  const r = lastState.rain;
  const rates = minuteSeries(minuteHist && minuteHist.rain_mm_h, r.rate_mm_h).map(v => v * u.rMul);
  const hail = minuteSeries(minuteHist && minuteHist.hail_mm_h).map(v => v * u.rMul);
  // Dynamic Y-axis rescaling: adapts smoothly from light drizzle (1-2 mm/h) to heavy downpour (40+ mm/h)
  const maxR = niceCeiling(Math.max(...rates) * 1.1, 1.0 * u.rMul);
  const rDec = u.rUnit === "in" ? 2 : (maxR < 5 ? 1 : 0);
  const frame = chartFrame([maxR.toFixed(rDec), (maxR * 0.5).toFixed(rDec), "0"], ["-60m", "-30m", "Now"]);
  const { xStart, w, bw } = minuteBarGeom(frame);
  const yTop = CH.yTop;
  const yBot = CH.yBot;

  // Stacked Liquid-Equivalent Precipitation Rate (mm/h): Blue = Liquid Rain, Violet = Hail Liquid-Water Equivalent (mm/h)
  svg.innerHTML = frame.html + rates.map((val, i) => {
    const x = xStart + i * w;
    if (val <= 0.01 * u.rMul) {
      return `<rect x="${x.toFixed(1)}" y="${(yBot - 3).toFixed(1)}" width="${bw.toFixed(1)}" height="3" rx="0.8" fill="rgba(142,162,198,0.38)"/>`;
    }
    const h = Math.max(3.0, (val / maxR) * (yBot - yTop));
    const hailH = hail[i] > 0.02 * u.rMul ? Math.min(h * 0.45, Math.max(2.0, (hail[i] / maxR) * (yBot - yTop))) : 0;
    const rainH = h - hailH;
    let bars = `<rect x="${x.toFixed(1)}" y="${(yBot - rainH).toFixed(1)}" width="${bw.toFixed(1)}" height="${rainH.toFixed(1)}" rx="0.8" fill="${val >= 10 * u.rMul ? "#38bdf8" : "#60a5fa"}"/>`;
    if (hailH > 0) {
      bars += `<rect x="${x.toFixed(1)}" y="${(yBot - h).toFixed(1)}" width="${bw.toFixed(1)}" height="${hailH.toFixed(1)}" rx="0.8" fill="#c084fc"/>`;
    }
    return bars;
  }).join("");
}

function renderLightningTimeline(d) {
  const svg = el("lt-timeline-svg");
  if (!svg) return;
  const bins = minuteSeries(minuteHist && minuteHist.strikes);
  const peakStrikes = Math.max(...bins);
  const maxB = Math.max(2, Math.ceil(peakStrikes * 1.15));
  const midB = Math.round(maxB * 0.5);
  const frame = chartFrame([maxB, midB, "0"], ["-60m", "-30m", "Now"]);
  const { xStart, w, bw } = minuteBarGeom(frame);
  const yTop = CH.yTop;
  const yBot = CH.yBot;
  setText("lt-peak-lbl", `Peak ${peakStrikes}`);
  svg.innerHTML = frame.html + bins.map((cnt, i) => {
    const h = cnt > 0 ? Math.max(5.0, (cnt / maxB) * (yBot - yTop)) : 3.0;
    const x = xStart + i * w;
    const col = cnt >= 3 ? "#f59e0b" : (cnt > 0 ? "#38bdf8" : "rgba(142,162,198,0.38)");
    return `<rect x="${x.toFixed(1)}" y="${(yBot - h).toFixed(1)}" width="${bw.toFixed(1)}" height="${h.toFixed(1)}" rx="0.8" fill="${col}"/>`;
  }).join("");

  const l = d.lightning;
  const u = UNIT_SYSTEMS[unitIdx];
  setText("lt-lock-badge", `${l.strikes_1h} ${l.strikes_1h === 1 ? "Strike" : "Strikes"} / h`);
  el("lt-lock-badge").className = l.strikes_1h > 0 ? "badge warn" : "badge ok";
  if (isNum(l.distance_km)) {
    const dist = conv.d(l.distance_km);
    setText("lt-dist-big", l.method === "acoustic" ? dist.toFixed(2) : `~${dist.toFixed(0)}`);
    setText("lt-dist-unit", u.dUnit);
    const age = isNum(l.last_strike_age_s) ? (l.last_strike_age_s < 120 ? `${l.last_strike_age_s.toFixed(0)} s ago` : `${(l.last_strike_age_s / 60).toFixed(0)} min ago`) : "";
    setText("lt-thunder-sub", isNum(l.thunder_delay_s) ? `Δt ${l.thunder_delay_s.toFixed(1)} s · ${age}` : `RF estimate · ${age}`);
  } else {
    setText("lt-dist-big", ">40");
    setText("lt-dist-unit", u.dUnit === "km" ? "km" : u.dUnit);
    setText("lt-thunder-sub", "Δt — s");
  }
}

// Trends tab: tier history from /api/history (null slots break the line).
function renderLineChart(svgId, seriesList, yUnit) {
  const svg = el(svgId);
  if (!svg) return;
  const all = [];
  seriesList.forEach(s => s.values.forEach(v => { if (isNum(v)) all.push(v); }));
  if (all.length < 2) {
    svg.innerHTML = `<text x="280" y="92" fill="#8ea2c6" font-size="11" text-anchor="middle">No history yet</text>`;
    return;
  }
  const minV = Math.min(...all), maxV = Math.max(...all);
  const span = Math.max(1.0, maxV - minV);
  let html = "";
  for (let g = 0; g <= 3; g++) {
    const y = 18 + (g / 3) * 136;
    const val = maxV - (g / 3) * span;
    html += `<line x1="42" y1="${y}" x2="548" y2="${y}" stroke="rgba(142,162,198,0.14)" stroke-dasharray="3 3"/>`;
    html += `<text x="38" y="${y + 4}" fill="#8ea2c6" font-size="9.5" text-anchor="end">${val.toFixed(1)}${yUnit}</text>`;
  }
  seriesList.forEach(s => {
    const n = s.values.length;
    let seg = [];
    const flush = () => {
      if (seg.length > 1) {
        html += `<polyline fill="none" stroke="${s.color}" stroke-width="${s.width || 2}" ${s.dash ? `stroke-dasharray="${s.dash}"` : ""} points="${seg.join(" ")}"/>`;
      }
      seg = [];
    };
    s.values.forEach((v, i) => {
      if (!isNum(v)) { flush(); return; }
      const x = 44 + (i / Math.max(1, n - 1)) * 502;
      const y = 154 - ((v - minV) / span) * 136;
      seg.push(`${x.toFixed(1)},${y.toFixed(1)}`);
    });
    flush();
  });
  const lbl = { minute: ["-60 min", "-30 min", "Now"], hour: ["-24 h", "-12 h", "Now"], day: ["-31 d", "-15 d", "Today"] }[trendTier];
  html += `<text x="44" y="172" fill="#8ea2c6" font-size="9.5">${lbl[0]}</text><text x="296" y="172" fill="#8ea2c6" font-size="9.5" text-anchor="middle">${lbl[1]}</text><text x="546" y="172" fill="#8ea2c6" font-size="9.5" text-anchor="end">${lbl[2]}</text>`;
  svg.innerHTML = html;
}

function renderTrends() {
  if (!trendData || !trendData.series) return;
  const u = UNIT_SYSTEMS[unitIdx];
  const S = trendData.series;
  const arr = (key, f = "v") => (S[key] && S[key][f]) ? S[key][f] : [];
  const lbl = { minute: "Last Hour", hour: "Last 24 h", day: "Last 31 Days" }[trendTier];
  setText("chart-temp-title", `Temperature & Dew Point (${lbl})`);
  setText("chart-wind-title", `Wind Mean & Gust (${lbl})`);
  setText("chart-baro-title", `Sea-Level Pressure (${lbl})`);
  renderLineChart("chart-temp", [
    { values: arr("temp_c").map(conv.t), color: "#38bdf8", width: 2.3 },
    { values: arr("dew_point_c").map(conv.t), color: "#22c55e", dash: "5 3", width: 1.8 },
  ], u.tUnit);
  renderLineChart("chart-wind", [
    { values: arr("wind_m_s", "max").map(conv.w), color: "#f59e0b", width: 2.0 },
    { values: arr("wind_m_s").map(conv.w), color: "#38bdf8", width: 2.3 },
  ], "");
  renderLineChart("chart-baro-rain", [{ values: arr("qnh_hpa").map(conv.p), color: "#a855f7", width: 2.3 }], "");
}

function applyTelemetry(d) {
  if (!d || !d.wind) return;
  lastState = d;
  const u = UNIT_SYSTEMS[unitIdx];
  const e = d.environment, w = d.wind, r = d.rain, s = d.solar, a = d.astronomy, q = d.air_quality;
  const pDec = u.pUnit === "inHg" ? 2 : 0;
  const rDec = u.rUnit === "in" ? 2 : 1;
  const wDec = 1;

  // Browser clock fallback when the station has no internet time (SNTP) yet.
  if (d.clock_valid === false && !clockPushed) {
    clockPushed = true;
    postJson("/api/action", { action: "set_time", unix: Math.floor(Date.now() / 1000) }).catch(() => {});
  }
  el("loc-banner").style.display = d.location_set ? "none" : "flex";
  setText("hdr-fw", `fw ${d.fw}`);

  // ---- Hero ----
  renderSkyHeroVisual(d);
  setText("hero-temp", isNum(e.temp_c) ? `${conv.t(e.temp_c).toFixed(1)}°` : "—");
  setText("hero-cond", `${e.forecast} · Feels like ${fv(conv.t(e.feels_like_c), 1, "°")}`);
  setText("hc-wind", isNum(w.avg_2m_m_s) ? `${conv.w(w.avg_2m_m_s).toFixed(wDec)} ${u.wUnit} ${degToCardinal(w.dir_10m_deg)}` : "—");
  const d3h = e.pressure_3h_delta_hpa;
  const arrowP = !isNum(d3h) ? "" : (d3h < -1 ? "↓" : (d3h > 1 ? "↑" : "→"));
  setText("hc-baro", isNum(e.qnh_hpa) ? `${conv.p(e.qnh_hpa).toFixed(pDec)} ${u.pUnit} ${arrowP}` : "—");
  setText("hc-rain", `${r.state} · ${conv.r(r.today_mm).toFixed(rDec)} ${u.rUnit}`);
  setText("hc-aqi", `Air: ${q.level} (${fv(q.aqi, 0)})`);
  el("hc-aqi-dot").style.background = !isNum(q.aqi) ? "#64748b" : (q.aqi <= 50 ? "#22c55e" : (q.aqi <= 100 ? "#f59e0b" : "#ef4444"));

  // ---- 1. Wind ----
  const bft = isNum(w.beaufort) ? Math.min(12, Math.max(0, Math.round(w.beaufort))) : null;
  setText("wind-beaufort", bft === null ? "—" : BEAUFORT_NAMES[bft]);
  setText("wind-speed", fv(conv.w(w.speed_m_s), 1));
  setText("wind-unit-dir", `${u.wUnit} · ${isNum(w.dir_deg) ? degToCardinal(w.dir_deg) : "Calm"}`);
  const arrowDir = isNum(w.dir_deg) ? w.dir_deg : (isNum(w.dir_10m_deg) ? w.dir_10m_deg : 0);
  el("wind-arrow").setAttribute("transform", `rotate(${arrowDir} 115 115)`);
  setText("wind-lull", fv(conv.w(w.lull_m_s), 1));
  setText("wind-gust", isNum(w.gust_m_s) ? `${conv.w(w.gust_m_s).toFixed(1)} ${u.wUnit}` : "—");
  const tr = conv.w(w.trend_m_s_per_h);
  setText("wind-trend-lbl", isNum(tr) ? `${tr > 0.2 ? "↗ +" : (tr < -0.2 ? "↘ " : "→ ")}${tr.toFixed(1)} ${u.wUnit}/h` : "—");
  const scaleW = Math.max(12, (w.gust_m_s || 0) * 1.28);
  const lullP = clampPct(w.lull_m_s, 0, scaleW);
  const gustP = clampPct(w.gust_m_s, 0, scaleW);
  el("wind-range-span").style.left = `${lullP.toFixed(0)}%`;
  el("wind-range-span").style.width = `${Math.max(6, gustP - lullP).toFixed(0)}%`;
  el("wind-avg-dot").style.left = `${clampPct(w.speed_m_s, 0, scaleW).toFixed(0)}%`;
  const ldDec = unitIdx === 1 ? 2 : 1;
  setText("wload-min", fv(conv.l(w.load_min_1h_kg_m2), ldDec));
  setText("wload-trend-lbl", fv(conv.l(w.load_kg_m2), ldDec));
  setText("wload-max", isNum(w.load_max_1h_kg_m2) ? `${conv.l(w.load_max_1h_kg_m2).toFixed(ldDec)} ${u.lUnit}` : "—");
  const ldCeil = Math.max(4.0, (w.load_max_1h_kg_m2 || 0) * 1.25);
  const ldMinP = clampPct(w.load_min_1h_kg_m2, 0, ldCeil);
  const ldMaxP = clampPct(w.load_max_1h_kg_m2, 0, ldCeil);
  el("wload-span").style.left = `${ldMinP.toFixed(0)}%`;
  el("wload-span").style.width = `${Math.max(6, ldMaxP - ldMinP).toFixed(0)}%`;
  el("wload-now-dot").style.left = `${clampPct(w.load_kg_m2, 0, ldCeil).toFixed(0)}%`;
  if (Array.isArray(w.rose)) renderRosePetals(w.rose);

  // ---- 2. Temperature & humidity ----
  setText("env-temp", isNum(e.temp_c) ? `${conv.t(e.temp_c).toFixed(1)}°` : "—");
  setText("env-feels", `Feels like ${fv(conv.t(e.feels_like_c), 1, "°")} outside`);
  setText("env-rh", fv(e.humidity_rh, 0, "%"));
  el("rh-arc").setAttribute("stroke-dasharray", `${((e.humidity_rh || 0) / 100 * 220).toFixed(0)} 220`);
  setText("temp-min-lbl", fv(conv.t(e.temp_min_24h_c), 1, "°"));
  setText("temp-max-lbl", fv(conv.t(e.temp_max_24h_c), 1, "°"));
  const tt = isNum(e.temp_trend_c_per_h) ? e.temp_trend_c_per_h * (unitIdx === 1 ? 1.8 : 1.0) : null;
  setText("temp-trend-lbl", !isNum(tt) ? "—" : (tt > 0.15 ? `↗ +${tt.toFixed(1)}°/h · Warming` : (tt < -0.15 ? `↘ ${tt.toFixed(1)}°/h · Cooling` : `→ ${tt.toFixed(1)}°/h · Steady`)));
  if (isNum(e.temp_min_24h_c) && isNum(e.temp_max_24h_c) && isNum(e.temp_c)) {
    const lo = e.temp_min_24h_c - 3.5, hi = e.temp_max_24h_c + 3.5;
    const pMin = clampPct(e.temp_min_24h_c, lo, hi), pMax = clampPct(e.temp_max_24h_c, lo, hi);
    el("t24-bridge").style.left = `${pMin.toFixed(1)}%`;
    el("t24-bridge").style.width = `${Math.max(8, pMax - pMin).toFixed(1)}%`;
    el("pin-temp").style.left = `${clampPct(e.temp_c, lo, hi).toFixed(1)}%`;
  }
  setText("env-dew", fv(conv.t(e.dew_point_c), 1, "°"));
  const spread = isNum(e.temp_c) && isNum(e.dew_point_c) ? e.temp_c - e.dew_point_c : null;
  const spreadDisp = isNum(spread) ? spread * (unitIdx === 1 ? 1.8 : 1.0) : null;
  setText("fog-risk-lbl", !isNum(spread) ? "—" : (spread <= 2.0 ? `Fog risk (${spreadDisp.toFixed(1)}°)` : `${spreadDisp.toFixed(1)}° gap`));
  setText("env-wetbulb", fv(conv.t(e.wet_bulb_c), 1, "°"));
  setText("wetbulb-sub", !isNum(e.wet_bulb_c) ? "—" : (e.wet_bulb_c <= 0.5 ? "Snow (< 0.5°)" : (e.wet_bulb_c >= 24.0 ? "Heat limit" : "Rain phase")));
  setText("abs-hum-lbl", fv(e.abs_humidity_g_m3, 1, " g/m³"));
  setText("abs-hum-sub", !isNum(e.abs_humidity_g_m3) ? "—" : (e.abs_humidity_g_m3 >= 16.5 ? "Humid air" : (e.abs_humidity_g_m3 >= 9.0 ? "Pleasant" : "Crisp air")));
  setText("comfort-badge", e.comfort);
  el("comfort-badge").className = ["Heat Stress", "Cold Stress", "Frost Risk", "Freezing"].includes(e.comfort) ? "badge warn" : "badge ok";

  // ---- 3. Barometer ----
  const needle = (hpa) => Math.max(-88, Math.min(88, ((hpa - 1010) / 30) * 90));
  if (isNum(e.qnh_hpa)) {
    el("baro-needle").setAttribute("transform", `rotate(${needle(e.qnh_hpa).toFixed(1)} 120 110)`);
    el("baro-ghost-needle").setAttribute("transform", `rotate(${needle(e.qnh_hpa - (isNum(d3h) ? d3h : 0)).toFixed(1)} 120 110)`);
    el("baro-ghost-needle").style.display = isNum(d3h) ? "" : "none";
  }
  setText("env-qnh", fv(conv.p(e.qnh_hpa), pDec));
  setText("env-qnh-unit", u.pUnit);
  setText("env-zambretti", e.forecast);
  setText("baro-rain-prob", `Rain Chance ${fv(e.rain_prob_pct, 0, "%")}`);
  el("baro-rain-prob-fill").style.width = `${isNum(e.rain_prob_pct) ? e.rain_prob_pct : 0}%`;
  setText("baro-front-lbl", `${e.front_type} · Wind ${e.wind_shift}`);
  setText("baro-minmax", isNum(e.qnh_min_24h_hpa) ? `24h: ${conv.p(e.qnh_min_24h_hpa).toFixed(pDec)}–${conv.p(e.qnh_max_24h_hpa).toFixed(pDec)} ${u.pUnit}` : "24h: —");
  setText("baro-outlook-lbl", isNum(e.synoptic_3h_delta_hpa)
    ? `3h: ${e.synoptic_3h_delta_hpa >= 0 ? "+" : ""}${conv.p(e.synoptic_3h_delta_hpa).toFixed(u.pUnit === "inHg" ? 3 : 1)} ${u.pUnit} weather` + (isNum(e.solar_tide_hpa) ? ` (tide ${e.solar_tide_hpa >= 0 ? "+" : ""}${conv.p(e.solar_tide_hpa).toFixed(u.pUnit === "inHg" ? 3 : 1)} removed)` : "")
    : "3h tendency available after 3 h of data");
  const rate1h = isNum(e.synoptic_3h_delta_hpa) ? conv.p(e.synoptic_3h_delta_hpa / 3.0) : null;
  setText("baro-trend-badge", isNum(rate1h) ? `Change: ${rate1h >= 0 ? "+" : ""}${rate1h.toFixed(u.pUnit === "inHg" ? 3 : 1)} ${u.pUnit}/h` : "Change: —");

  // ---- 4. Precipitation ----
  setText("rain-state", `This Shower: ${conv.r(r.event_mm).toFixed(rDec)} ${u.rUnit}`);
  el("rain-state").className = r.raining ? "badge info" : "badge ok";
  setText("rain-rate", r.sensor_ok ? conv.r(r.rate_mm_h).toFixed(rDec) : "—");
  setText("rain-rate-unit", `${u.rUnit}/h · ${r.state}`);
  setText("rain-daily", `${conv.r(r.today_mm).toFixed(rDec)} ${u.rUnit}`);
  el("rain-water").style.height = `${Math.max(6, clampPct(r.today_mm, 0, 35)).toFixed(0)}%`;
  setText("trend-rain-1h", `${conv.r(r.last_hour_mm).toFixed(rDec)} ${u.rUnit}`);
  setText("trend-rain-day", `${conv.r(r.today_mm).toFixed(rDec)} ${u.rUnit}`);
  setText("trend-rain-month", isNum(rain31d) ? `${conv.r(rain31d).toFixed(rDec)} ${u.rUnit}` : "—");
  const hail = r.hail_hits > 0 ? r.hail_diam_mm : 0;
  setText("rain-hail", r.hail_hits > 0 ? `${r.hail_hits} hits · Max Ø ${hail.toFixed(1)} mm` : "0 hits · Max Ø 0.0 mm");
  document.querySelectorAll("#hail-scale .hs-item").forEach((item) => {
    const mm = parseFloat(item.dataset.mm);
    item.className = hail > 0 && Math.abs(hail - mm) <= 3.5 ? "hs-item active" : "hs-item";
  });

  // ---- 5. Solar & UV ----
  const night = Boolean(a.is_night);
  setText("daylight-badge", isNum(s.burn_time_min) ? `Burn ~${s.burn_time_min.toFixed(0)} min` : "No Burn Risk");
  el("daylight-badge").className = isNum(s.uv_index) && s.uv_index >= 6 ? "badge warn" : "badge ok";
  if (night && isNum(s.lux)) {
    const mlx = s.lux * 1000.0;
    setText("env-solar", mlx < 10 ? mlx.toFixed(2) : mlx.toFixed(0));
    setText("env-solar-unit", "mlx (Night)");
    setText("env-lux", `Night Illuminance (${s.lux.toFixed(4)} lx)`);
  } else {
    setText("env-solar", fv(s.irradiance_w_m2, 0));
    setText("env-solar-unit", "W/m²");
    setText("env-lux", isNum(s.lux) ? `Brightness: ${Math.round(s.lux).toLocaleString()} lx` : "Brightness: —");
  }
  const uvi = s.uv_index;
  const uvCat = !isNum(uvi) ? "—" : (uvi >= 11 ? "Extreme" : (uvi >= 8 ? "Very High" : (uvi >= 6 ? "High" : (uvi >= 3 ? "Moderate" : "Low"))));
  el("uv-left-lbl").innerHTML = `UV Index <b class="accent">${fv(uvi, 1)} · ${uvCat}</b>`;
  setText("uv-sub-row", `UVA ${fv(s.uva_w_m2, 1)} · UVB ${fv(s.uvb_w_m2, 1, " W/m²")}`);
  el("uv-dot").style.left = `${clampPct(uvi, 0, 11.5).toFixed(0)}%`;
  const uvW = (s.uva_w_m2 || 0) + (s.uvb_w_m2 || 0), visW = s.visible_w_m2 || 0, nirW = s.nir_w_m2 || 0;
  const sumW = Math.max(1.0, uvW + visW + nirW);
  setText("sp-uv", uvW.toFixed(0));
  setText("sp-vis", visW.toFixed(0));
  setText("sp-nir", `${nirW.toFixed(0)} W/m²`);
  el("bar-sp-uv").style.width = `${Math.max(uvW > 0 ? 5 : 0, (uvW / sumW) * 100).toFixed(0)}%`;
  el("bar-sp-vis").style.width = `${((visW / sumW) * 100).toFixed(0)}%`;
  el("bar-sp-nir").style.width = `${((nirW / sumW) * 100).toFixed(0)}%`;

  // ---- 6. Sky, clouds & moon ----
  const cc = s.cloud_cover_pct;
  setText("cloud-cover-lbl", fv(cc, 0, "%"));
  const ccDesc = !isNum(cc) ? (night ? "No estimate at night" : "No estimate (low sun)")
    : (cc >= 80 ? (r.raining ? "Heavy Wet Cloud" : "Overcast") : (cc >= 50 ? "Broken Clouds" : (cc >= 20 ? "Scattered" : "Clear Sky")));
  setText("sky-cond-big", isNum(cc) ? `Clouds · ${ccDesc}` : ccDesc);
  setText("sky-base-badge", isNum(e.cloud_base_m) ? `Cloud Base: ${Math.round(conv.h(e.cloud_base_m)).toLocaleString()} ${u.hUnit}` : "Cloud Base: —");
  setText("sky-moon-sub", isNum(a.moon_illum_pct) ? `Moon ${a.moon_illum_pct.toFixed(0)}% Illuminated · ${a.moon_phase}` : "Moon — (set the station location)");
  setText("sun-window", `${hhmm(a.sunrise_min)}–${hhmm(a.sunset_min)}`);
  setText("moon-window", `${hhmm(a.moonrise_min)}–${hhmm(a.moonset_min)} · ${fv(a.moon_illum_pct, 0, "%")}`);
  const span = (rise, set, node) => {
    if (!isNum(rise) || !isNum(set)) { node.style.display = "none"; return; }
    node.style.display = "";
    const l = rise / 1440 * 100, rr = set / 1440 * 100;
    node.style.left = `${(set >= rise ? l : 0).toFixed(1)}%`;
    node.style.width = `${(set >= rise ? rr - l : rr).toFixed(1)}%`;
  };
  span(a.sunrise_min, a.sunset_min, el("sky24-sun-span"));
  span(a.moonrise_min, a.moonset_min, el("sky24-moon-span"));
  const nowMin = isNum(d.unix_s) ? (((d.unix_s + d.utc_offset_s) % 86400) / 60) : null;
  const nowPct = isNum(nowMin) ? `${(nowMin / 1440 * 100).toFixed(1)}%` : "50%";
  el("sky24-sun-dot").style.left = nowPct;
  el("sky24-moon-dot").style.left = nowPct;
  el("sky24-sun-dot").style.opacity = isNum(a.sun_alt_deg) && a.sun_alt_deg > 0 ? "1" : "0.25";
  el("sky24-moon-dot").style.opacity = isNum(a.moon_alt_deg) && a.moon_alt_deg > 0 ? "1" : "0.25";
  const sg = el("stargazing-box");
  if (night && isNum(s.sqm)) {
    sg.style.display = "block";
    setText("sky-bortle-val", `Bortle ${s.bortle} · ${s.bortle <= 3 ? "Milky Way" : (s.bortle <= 6 ? "Night Sky" : "Bright Sky")}`);
    setText("sky-sqm-val", `SQM ${s.sqm.toFixed(1)} mag/arcsec² (est.)`);
    el("bortle-dot").style.left = `${clampPct(s.bortle, 1, 9).toFixed(0)}%`;
  } else {
    sg.style.display = "none";
  }

  // ---- 7. Air quality ----
  const aqi = q.aqi;
  setText("aq-dom-badge", `Main: ${q.dominant}`);
  el("aq-dom-badge").className = !isNum(aqi) || aqi <= 50 ? "badge ok" : (aqi <= 100 ? "badge warn" : "badge danger");
  setText("aq-aqi-big", fv(aqi, 0));
  setText("aq-level-lbl", q.level.toUpperCase());
  const aqiArc = el("aqi-arc");
  aqiArc.setAttribute("stroke-dasharray", `${Math.min(239, ((aqi || 0) / 200) * 239).toFixed(0)} 239`);
  aqiArc.setAttribute("class", !isNum(aqi) || aqi <= 50 ? "ring-fg-ok" : "ring-fg-cyan");
  const bar = (valId, barId, v, max, limit, dec = 1, suffix = "") => {
    setText(valId, fv(v, dec, suffix));
    el(barId).style.width = `${clampPct(v, 0, max).toFixed(0)}%`;
    el(barId).className = `b-fill ${!isNum(v) || v <= limit ? "ok" : "warn"}`;
  };
  bar("aq-pm1", "b-pm1", q.pm1_0, 25, 10);
  bar("aq-pm25", "b-pm25", q.pm2_5, 35, 15);
  bar("aq-pm10", "b-pm10", q.pm10, 90, 45);
  bar("aq-bio", "b-bio", q.coarse, 45, 20);
  setText("aq-co2", fv(q.co2_ppm, 0, " ppm"));
  el("b-co2").style.width = `${clampPct(q.co2_ppm, 380, 1600).toFixed(0)}%`;
  el("b-co2").className = `b-fill ${!isNum(q.co2_ppm) || q.co2_ppm <= 1000 ? "cyan" : "warn"}`;
  bar("aq-voc", "b-voc", q.voc_index, 300, 150, 0, " idx");
  bar("aq-nox", "b-nox", q.nox_index, 80, 20, 0, " idx");

  renderSparkRain();
  renderLightningTimeline(d);
  renderWaveAndSeismic(d);

  // ---- Station twin ----
  const h = d.heater;
  setText("twin-vin-badge", `${fv(h.vin_v, 1, " V")} · ${h.state === 1 ? `DE-ICING ${h.power_w.toFixed(1)} W` : "Heater Standby"}`);
  setText("tw-dome", `UV ${fv(uvi, 1)} · ${fv(s.irradiance_w_m2, 0, " W/m²")}`);
  setText("tw-crown", `c = ${fv(w.sound_m_s, 1, " m/s")}`);
  setText("tw-heater", `${fv(h.reflector_c, 1, " °C")} · ${fv(h.reflector_w, 1, " W")} measured`);
  setText("tw-probe", `${fv(e.temp_c, 1, " °C")} · ${fv(q.co2_ppm, 0, " ppm CO₂")} · heater ${fv(h.probe_c, 1, " °C")} ${fv(h.probe_w, 2, " W")}`);
  setText("tw-main", `${d.wifi.rssi_dbm} dBm · ${fv(h.vin_v, 1, " V")} in · 5V ${fv(h.rail_logic_v, 2)} / heat ${fv(h.rail_heat_v, 2)}`);
  setText("cv-solar", isNum(e.solar_offset_c) ? `-${e.solar_offset_c.toFixed(2)} °C` : "—");
  el("cb-solar").style.width = `${clampPct(e.solar_offset_c, 0, 1.5).toFixed(0)}%`;
  setText("cv-bern", isNum(e.pressure_wind_corr_hpa) ? `+${e.pressure_wind_corr_hpa.toFixed(2)} hPa` : "—");
  el("cb-bern").style.width = `${clampPct(e.pressure_wind_corr_hpa, 0, 1.0).toFixed(0)}%`;
  const pmRat = isNum(q.pm2_5) && isNum(q.pm2_5_raw) && q.pm2_5_raw > 0.1 ? q.pm2_5 / q.pm2_5_raw : null;
  setText("cv-fog", isNum(pmRat) ? `${pmRat.toFixed(2)}× (Dry ${q.pm2_5.toFixed(1)} vs Raw ${q.pm2_5_raw.toFixed(1)})` : "—");
  el("cb-fog").style.width = `${((pmRat || 1) * 100).toFixed(0)}%`;
  setText("cv-tof", isNum(w.sound_m_s) ? `${w.sound_m_s.toFixed(1)} m/s → ${fv(w.acoustic_temp_c, 1, " °C")}` : "—");
  el("cb-tof").style.width = `${clampPct(w.sound_m_s, 320, 360).toFixed(0)}%`;
  setText("cv-heater", `${h.duty_pct.toFixed(1)} % (${h.power_w.toFixed(1)} W set, ${fv(h.reflector_w, 1, " W")} measured, ${h.reflector_status})`);
  el("cb-heater").style.width = `${Math.max(2, h.duty_pct).toFixed(0)}%`;
}

// ---------------------------------------------------------------------------------------------
// Setup tab
// ---------------------------------------------------------------------------------------------
// Browser time zone -> POSIX TZ for the station (EU / US DST rules, otherwise fixed offset).
function browserPosixTz() {
  const year = new Date().getFullYear();
  const jan = -new Date(year, 0, 1).getTimezoneOffset();   // minutes east of UTC
  const jul = -new Date(year, 6, 1).getTimezoneOffset();
  const std = Math.min(jan, jul), dst = Math.max(jan, jul);
  const fmt = (m) => {
    const sign = m > 0 ? "-" : "+";  // POSIX: positive = west of UTC
    const a = Math.abs(m);
    return `${sign}${Math.floor(a / 60)}${a % 60 ? ":" + String(a % 60).padStart(2, "0") : ""}`;
  };
  const zone = Intl.DateTimeFormat().resolvedOptions().timeZone || "";
  if (std === dst) return `STD${fmt(std)}`;
  if (zone.startsWith("Europe/")) {
    const startH = 1 + std / 60, endH = 1 + dst / 60;  // EU: switch at 01:00 UTC
    return `STD${fmt(std)}DST,M3.5.0/${startH},M10.5.0/${endH}`;
  }
  if (zone.startsWith("America/")) return `STD${fmt(std)}DST,M3.2.0,M11.1.0`;
  return `STD${fmt(std)}DST`;
}

function loadConfig() {
  return fetch("/api/config").then(r => r.json()).then(c => {
    stationCfg = c;
    el("scenario-select").style.display = c.simulated ? "" : "none";
    if (c.simulated && c.scenario) el("scenario-select").value = c.scenario;
    const set = (id, v) => { if (el(id) && v !== undefined && v !== null) el(id).value = v; };
    set("cfg-lat", c.location_set ? c.latitude : "");
    set("cfg-lon", c.location_set ? c.longitude : "");
    set("cfg-alt", c.altitude_m);
    set("cfg-tz", c.tz || browserPosixTz());
    set("cfg-decl", c.declination_deg);
    set("cfg-dt-ns", c.dt_ns_offset_us);
    set("cfg-dt-ew", c.dt_ew_offset_us);
    set("cfg-us-delay", c.us_delay_us);
    set("cfg-pcoef", c.pressure_wind_coef);
    set("cfg-rain-gain", c.rain_gain);
    set("cfg-rain-scale", c.rain_impact_scale);
    set("cfg-wind-gain", c.wind_speed_gain);
    set("cfg-rs485-addr", c.rs485_address);
    set("cfg-optics-tr", c.optics_transmission);
    setText("wind-cal-status", `Wind calibration: ${c.wind_cal_status || "—"}`);
    set("cfg-tuncap", c.as3935_tun_cap);
    set("cfg-ssid", c.wifi_ssid);
    set("cfg-mqtt", c.mqtt_uri);
    set("cfg-ha", c.ha_prefix);
    setText("loc-status", c.location_set ? `${(+c.latitude).toFixed(4)}, ${(+c.longitude).toFixed(4)}` : "Not set");
    setText("net-status", lastState && lastState.wifi ? (lastState.wifi.connected ? `${lastState.wifi.ip} · ${lastState.wifi.rssi_dbm} dBm` : (lastState.wifi.ap_mode ? "Setup AP 192.168.4.1" : "Offline")) : "—");
  }).catch(() => {});
}

el("btn-geolocate").addEventListener("click", () => {
  if (!navigator.geolocation) {
    setText("geo-status", "This browser has no location service: enter latitude / longitude manually.");
    return;
  }
  setText("geo-status", "Asking the browser for this device's location…");
  navigator.geolocation.getCurrentPosition((pos) => {
    el("cfg-lat").value = pos.coords.latitude.toFixed(4);
    el("cfg-lon").value = pos.coords.longitude.toFixed(4);
    if (isNum(pos.coords.altitude)) el("cfg-alt").value = pos.coords.altitude.toFixed(0);
    el("cfg-tz").value = browserPosixTz();
    setText("geo-status", `Location found (±${pos.coords.accuracy.toFixed(0)} m). Check the elevation, then save.`);
  }, (err) => {
    setText("geo-status", `Location unavailable (${err.message}). Browsers allow it only over HTTPS or localhost: enter it manually.`);
  }, { enableHighAccuracy: true, timeout: 15000 });
});

el("btn-save-loc").addEventListener("click", () => {
  const lat = parseFloat(el("cfg-lat").value), lon = parseFloat(el("cfg-lon").value);
  if (!isNum(lat) || !isNum(lon) || Math.abs(lat) > 90 || Math.abs(lon) > 180) {
    setText("geo-status", "Enter a valid latitude (-90…90) and longitude (-180…180).");
    return;
  }
  postJson("/api/config", { latitude: lat, longitude: lon, altitude_m: parseFloat(el("cfg-alt").value) || 0, tz: el("cfg-tz").value || browserPosixTz() })
    .then(() => { setText("geo-status", "Location saved ✓"); loadConfig(); })
    .catch(() => setText("geo-status", "Saving failed"));
});

el("btn-save-net").addEventListener("click", () => {
  const body = { mqtt_uri: el("cfg-mqtt").value, ha_prefix: el("cfg-ha").value };
  if (el("cfg-ssid").value) { body.wifi_ssid = el("cfg-ssid").value; body.wifi_pass = el("cfg-pass").value; }
  postJson("/api/config", body)
    .then((res) => setText("net-status", res.reboot_required ? "Saved: reboot to apply" : "Saved ✓"))
    .catch(() => {});
});

el("btn-save-cfg").addEventListener("click", () => {
  const num = (id) => parseFloat(el(id).value);
  postJson("/api/config", {
    declination_deg: num("cfg-decl"), dt_ns_offset_us: num("cfg-dt-ns"), dt_ew_offset_us: num("cfg-dt-ew"),
    us_delay_us: num("cfg-us-delay"), pressure_wind_coef: num("cfg-pcoef"), rain_gain: num("cfg-rain-gain"),
    as3935_tun_cap: parseInt(el("cfg-tuncap").value, 10),
    rain_impact_scale: num("cfg-rain-scale"), wind_speed_gain: num("cfg-wind-gain"), rs485_address: parseInt(el("cfg-rs485-addr").value, 10) || 0,
    optics_transmission: num("cfg-optics-tr"),
  }).then(() => setText("cfg-save-status", "Saved ✓")).catch(() => {});
});

el("btn-wind-cal").addEventListener("click", () => {
  if (!confirm("Cover the station (closed box) or make sure the air is completely still for 60 s, then press OK.")) return;
  postJson("/api/action", { action: "wind_calibrate" })
    .then(() => setText("wind-cal-status", "Wind calibration: running (60 s) — reload Settings afterwards"))
    .catch(() => {});
});

el("btn-reset-rain").addEventListener("click", () => {
  postJson("/api/action", { action: "reset_rain" }).then(() => setText("ota-status", "Today's rain reset ✓")).catch(() => {});
});
el("btn-reboot").addEventListener("click", () => {
  if (confirm("Reboot the station? History is saved first.")) postJson("/api/action", { action: "reboot" }).catch(() => {});
});

// ---------------------------------------------------------------------------------------------
// Data loading
// ---------------------------------------------------------------------------------------------
function loadMinuteHistory() {
  return fetch("/api/history/minutes").then(r => r.json()).then(m => {
    if (m && Array.isArray(m.rain_mm_h)) {
      minuteHist = m;
      if (lastState) applyTelemetry(lastState);
    }
  }).catch(() => {});
}

function loadTrends() {
  return fetch(`/api/history?tier=${trendTier}&metrics=temp_c,dew_point_c,wind_m_s,qnh_hpa`).then(r => r.json()).then(h => {
    trendData = h;
    renderTrends();
  }).catch(() => {});
}

function loadRain31d() {
  return fetch("/api/history?tier=day&metrics=rain_mm").then(r => r.json()).then(h => {
    const v = h.series && h.series.rain_mm ? h.series.rain_mm.v : [];
    rain31d = v.reduce((acc, x) => acc + (isNum(x) ? x : 0), 0);
  }).catch(() => {});
}

function connectWs() {
  if (location.protocol === "file:") return;
  try {
    const proto = location.protocol === "https:" ? "wss:" : "ws:";
    const ws = new WebSocket(`${proto}//${location.host}/ws`);
    ws.onmessage = (ev) => {
      try { applyTelemetry(JSON.parse(ev.data)); } catch (_) {}
    };
    ws.onclose = () => setTimeout(connectWs, 2500);
  } catch (_) {}
}

loadConfig()
  .then(() => Promise.all([loadMinuteHistory(), loadRain31d()]))
  .then(() => fetch("/api/state").then(r => r.json()).then(applyTelemetry))
  .catch(() => {})
  .then(() => {
    setInterval(loadMinuteHistory, 15000);
    setInterval(loadRain31d, 300000);
    connectWs();
  });
