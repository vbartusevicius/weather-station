const COMPASS_16 = ["N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"];
const BEAUFORT_NAMES = [
  "Calm","Light Air","Light Breeze","Gentle Breeze","Moderate Breeze","Fresh Breeze",
  "Strong Breeze","Near Gale","Gale","Strong Gale","Storm","Violent Storm","Hurricane"
];

const UNIT_SYSTEMS = [
  { id: "metric",   label: "Metric",   wMul: 1.0,     wUnit: "m/s",  tFn: c => c,            tUnit: "°", pMul: 1.0,     pUnit: "hPa", rMul: 1.0,     rUnit: "mm" },
  { id: "imperial", label: "Imperial", wMul: 2.23694, wUnit: "mph",  tFn: c => c*1.8 + 32.0, tUnit: "°", pMul: 0.02953, pUnit: "inHg",rMul: 0.03937, rUnit: "in" },
  { id: "nautical", label: "Knots",    wMul: 1.94384, wUnit: "kt",   tFn: c => c,            tUnit: "°", pMul: 1.0,     pUnit: "hPa", rMul: 1.0,     rUnit: "mm" },
];

let unitIdx = 0;
let lastState = null;
let historyPoints = [];
let trendRange = "1h";

// Trends range buttons (Last Hour vs Last 24 Hours)
document.querySelectorAll("[data-trange]").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll("[data-trange]").forEach((b) => b.classList.remove("active"));
    btn.classList.add("active");
    trendRange = btn.dataset.trange;
    const lbl = trendRange === "1h" ? "Last Hour" : "Last 24h";
    document.getElementById("chart-temp-title").textContent = `Temperature, Dew Point & Feels-Like (${lbl})`;
    document.getElementById("chart-wind-title").textContent = `Wind Speed & Peak 3s Gusts (${lbl})`;
    document.getElementById("chart-baro-title").textContent = `Sea-Level Pressure (${lbl})`;
    renderAllCharts();
  });
});

// Draw 36 compass degree ticks once
(function initCompassTicks() {
  const g = document.getElementById("compass-ticks");
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

// Tab switching
document.querySelectorAll(".tab").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll(".tab").forEach((b) => b.classList.remove("active"));
    document.querySelectorAll(".panel").forEach((p) => p.classList.remove("active"));
    btn.classList.add("active");
    const target = document.getElementById("tab-" + btn.dataset.tab);
    if (target) target.classList.add("active");
    if (btn.dataset.tab === "trends") renderAllCharts();
  });
});

// Unit switcher
document.getElementById("unit-toggle").addEventListener("click", () => {
  unitIdx = (unitIdx + 1) % UNIT_SYSTEMS.length;
  document.getElementById("unit-toggle").textContent = UNIT_SYSTEMS[unitIdx].label;
  if (lastState) applyTelemetry(lastState);
  renderSparkBaro();
  renderAllCharts();
});

// Theme switcher
document.getElementById("theme-toggle").addEventListener("click", () => {
  const html = document.documentElement;
  const next = html.getAttribute("data-theme") === "light" ? "dark" : "light";
  html.setAttribute("data-theme", next);
  document.getElementById("theme-toggle").textContent = next === "light" ? "☾" : "☀";
});

// Scenario selector: load 24h history first so low/high range matches the new scenario immediately
document.getElementById("scenario-select").addEventListener("change", (ev) => {
  fetch("/api/action", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ action: "set_scenario", scenario: ev.target.value })
  })
    .then(() => fetch("/api/history").then(r => r.json()))
    .then(arr => {
      if (Array.isArray(arr) && arr.length > 1) {
        historyPoints = arr;
      }
      return fetch("/api/state").then(r => r.json());
    })
    .then((d) => {
      applyTelemetry(d);
      renderSparkBaro();
      renderSparkRain();
      renderAllCharts();
    })
    .catch(() => {});
});

// Progressive disclosure: click any tile to inspect secondary technical metrics
document.querySelectorAll(".card.clickable").forEach((card) => {
  card.addEventListener("click", () => {
    if (!lastState) return;
    openInspectModal(card.dataset.inspect, lastState);
  });
});
document.getElementById("modal-close").addEventListener("click", () => {
  document.getElementById("inspect-modal").style.display = "none";
});
document.getElementById("inspect-modal").addEventListener("click", (ev) => {
  if (ev.target.id === "inspect-modal") ev.target.style.display = "none";
});

function openInspectModal(kind, d) {
  const modal = document.getElementById("inspect-modal");
  const title = document.getElementById("modal-title");
  const body = document.getElementById("modal-body");
  let rows = [];
  if (kind === "wind") {
    title.textContent = "🧭 Ultrasonic Wind & Orientation Details";
    rows = [
      ["Instantaneous Speed", `${d.wind.speed_m_s.toFixed(2)} m/s (${d.wind.dir_deg.toFixed(1)}°)`],
      ["1-Min / 10-Min Average", `${d.wind.avg_1m_m_s.toFixed(2)} / ${d.wind.avg_10m_m_s.toFixed(2)} m/s`],
      ["3s Peak Gust / Lull", `${d.wind.gust_m_s.toFixed(2)} / ${d.wind.lull_m_s.toFixed(2)} m/s`],
      ["Turbulence Intensity (TI)", `${(d.wind.turbulence_pct || 0).toFixed(1)} % (Gust Factor ${(d.wind.gust_factor || 1).toFixed(2)})`],
      ["Speed of Sound (c)", `${d.wind.sound_m_s.toFixed(2)} m/s (Acoustic Temp ${d.wind.acoustic_temp_c.toFixed(2)} °C)`],
      ["Auto-North / Mast Tilt", `+${d.wind.north_offset_deg.toFixed(1)}° / ${d.wind.tilt_deg.toFixed(2)}°`],
    ];
  } else if (kind === "temp") {
    title.textContent = "🌡 Temperature, Humidity & Comfort Physics";
    rows = [
      ["Air Temperature (Compensated)", `${d.environment.temp_c.toFixed(2)} °C`],
      ["Raw SHT45 / Solar Shield Offset", `${(d.environment.raw_temp_c || d.environment.temp_c).toFixed(2)} °C / -${(d.environment.solar_offset_c || 0).toFixed(2)} °C`],
      ["Dew Point (Condensation Temp)", `${d.environment.dew_point_c.toFixed(2)} °C`],
      ["Wet-Bulb Temp (Evaporative Floor)", `${d.environment.wet_bulb_c.toFixed(2)} °C`],
      ["Outdoor WBGT (Heat-Stress Index)", `${(d.environment.wbgt_c || 16).toFixed(2)} °C`],
      ["Vapor Pressure Deficit (Plant Drying)", `${(d.environment.vpd_kpa || 0.8).toFixed(2)} kPa`],
      ["Estimated Cloud Base (LCL)", `${(d.environment.cloud_base_m || 900).toFixed(0)} m above ground`],
    ];
  } else if (kind === "baro") {
    title.textContent = "⏲ Barometric Pressure & Forecast";
    rows = [
      ["Sea-Level Pressure (QNH)", `${d.environment.qnh_hpa.toFixed(2)} hPa`],
      ["Raw Station Pressure (QFE)", `${d.environment.pressure_hpa.toFixed(2)} hPa`],
      ["Bernoulli Wind Suction Correction", `+${(d.environment.dyn_offset_hpa || 0).toFixed(2)} hPa`],
      ["3-Hour Pressure Change", `${d.environment.pressure_trend} (${(d.environment.pressure_3h_delta_hpa || 0).toFixed(2)} hPa/3h)`],
      ["Barometric Forecast", d.environment.forecast || "Fine Weather"],
    ];
  } else if (kind === "rain") {
    title.textContent = "🌧 4-Piezo Haptic Rain & Hail Telemetry";
    rows = [
      ["Precipitation State", `${d.rain.state} (${d.rain.rate_mm_h.toFixed(2)} mm/h)`],
      ["Today / Current Shower / Season", `${d.rain.daily_mm.toFixed(2)} / ${d.rain.event_mm.toFixed(2)} / ${d.rain.total_mm.toFixed(1)} mm`],
      ["Raindrop Impacts / Hailstones", `${d.rain.drops.toLocaleString()} drops / ${d.rain.hail_hits} hailstones`],
      ["Max Hailstone Diameter (PZT Impulse)", d.rain.hail_hits > 0 ? `Ø ${(d.rain.hail_diam_mm || 9.4).toFixed(1)} mm` : "0.0 mm (No hail)"],
      ["IMU Wind-Shake Rejected", `${d.rain.rejected_shake} false vibrations ignored`],
      ["Crown Surface State", d.rain.wet ? "WET (Water Film Present)" : "DRY"],
    ];
  } else if (kind === "solar") {
    title.textContent = d.environment.is_night ? "🌙 Night Sky Quality, Moon & NIR Cloud Telemetry" : "☀ 3-Band Solar Spectrum, UV-A/B & Cloud Correlation";
    rows = [
      ["Total Solar Irradiance", `${d.environment.solar_w_m2.toFixed(1)} W/m²`],
      ["Visible / Near-Infrared (NIR)", `${(d.environment.visible_w_m2 || 0).toFixed(1)} W/m² / ${(d.environment.nir_w_m2 || 0).toFixed(1)} W/m²`],
      ["UV-A (315–400 nm) / UV-B (280–315 nm)", `${(d.environment.uva_w_m2 || 0).toFixed(2)} W/m² / ${(d.environment.uvb_w_m2 || 0).toFixed(2)} W/m²`],
      ["Ambient Illuminance (TSL25911 + LTR390)", d.environment.lux < 1.0 ? `${(d.environment.lux * 1000).toFixed(2)} mlx (${d.environment.lux.toFixed(5)} lx)` : `${Math.round(d.environment.lux).toLocaleString()} lx`],
      ["Sky Quality (SQM) / Bortle Class", `${(d.environment.sqm || 21.4).toFixed(2)} mag/arcsec² · Bortle ${d.environment.bortle || 2}`],
      ["Estimated Cloud Cover", `${(d.environment.cloud_cover_pct || 0).toFixed(0)} %`],
    ];
  } else {
    title.textContent = "🍃 Multi-Gas Air Quality & Lightning Detector";
    const pwrClass = d.lightning.energy > 35000 ? "Strong Cloud-to-Ground (CG)" : (d.lightning.energy > 0 ? "In-Cloud (IC)" : "None");
    rows = [
      ["US EPA AQI / EU EAQI", `${d.air_quality.aqi} (${d.air_quality.level}) / EAQI ${d.air_quality.eaqi}`],
      ["Primary Pollutant", d.air_quality.dominant],
      ["PM2.5 (Fog-Corrected / Raw)", `${(d.air_quality.dry_pm2_5 || d.air_quality.pm2_5).toFixed(1)} / ${d.air_quality.pm2_5.toFixed(1)} µg/m³`],
      ["PM1.0 / PM4.0 / PM10", `${d.air_quality.pm1_0.toFixed(1)} / ${d.air_quality.pm4_0.toFixed(1)} / ${d.air_quality.pm10.toFixed(1)} µg/m³`],
      ["True CO₂ (Pressure-Compensated)", `${d.air_quality.comp_co2_ppm || d.air_quality.co2_ppm} ppm`],
      ["VOC Index / NOx Index", `${d.air_quality.voc_index} / ${d.air_quality.nox_index}`],
      ["Lightning Strikes / Closest", `${d.lightning.strikes} strikes / ${d.lightning.distance_km} km`],
      ["Peak Discharge Power (AS3935)", `${pwrClass} (${d.lightning.energy.toLocaleString()})`],
    ];
  }
  body.innerHTML = rows.map(([k, v]) => `<div class="m-kv"><span class="sub-lbl">${k}</span><b>${v}</b></div>`).join("");
  modal.style.display = "flex";
}

function degToCardinal(deg) {
  return COMPASS_16[Math.floor(((deg + 11.25) % 360) / 22.5)];
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
  } else if (d.environment.temp_c <= 0.5) {
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

function renderSparkBaro() {
  const svg = document.getElementById("spark-baro");
  if (!svg) return;
  const u = UNIT_SYSTEMS[unitIdx];
  const recent = historyPoints.slice(-24);
  const vals = recent.length > 1 ? recent.map(p => p.qnh * u.pMul) : [1015 * u.pMul, 1017 * u.pMul];
  const minV = Math.min(...vals), maxV = Math.max(...vals);
  const dec = u.pUnit === "inHg" ? 2 : 0;

  const d = lastState;
  const locked = d && d.environment && d.environment.thunder_locked;
  const titleEl = document.getElementById("seismo-title");
  if (locked) {
    const tDist = d.environment.thunder_dist_km || 5.8;
    const tDelay = d.environment.thunder_delay_s || 16.9;
    const tPeak = d.environment.thunder_peak_pa || 14.8;
    if (titleEl) titleEl.textContent = `Thunder Shockwave (${tPeak.toFixed(1)} Pa)`;
    document.getElementById("baro-minmax").textContent =
      `Δt ${tDelay.toFixed(1)}s → ${tDist.toFixed(2)} km Exact`;
  } else {
    if (titleEl) titleEl.textContent = "Microbarom Wave (240 Hz)";
    document.getElementById("baro-minmax").textContent =
      `24h: ↓ ${minV.toFixed(dec)} · ↑ ${maxV.toFixed(dec)} ${u.pUnit}`;
  }

  // Render high-rate BMP581 240 Hz acoustic / gravity-wave seismograph trace
  const wavePa = (d && d.environment && d.environment.microbarom_pa) ? d.environment.microbarom_pa : 0.08;
  const nPts = 64;
  const pts = [];
  for (let i = 0; i < nPts; i++) {
    const x = 6 + (i / (nPts - 1)) * 308;
    let dy = Math.sin(i * 0.55) * Math.cos(i * 0.19) * Math.min(14, 3.5 + wavePa * 14);
    // If thunder shockwave is locked, draw the N-wave compression + rarefaction spike around i=42..48!
    if (locked) {
      if (i === 43) dy = -20;
      else if (i === 44) dy = 20;
      else if (i === 45) dy = -12;
      else if (i === 46) dy = 9;
      else if (i === 47) dy = -4;
    }
    pts.push(`${x.toFixed(1)},${(26 + dy).toFixed(1)}`);
  }
  const strokeCol = locked ? "#f59e0b" : "#38bdf8";
  svg.innerHTML = `
    <line x1="6" y1="26" x2="314" y2="26" stroke="rgba(142,162,198,0.22)" stroke-dasharray="2 2"/>
    <polyline fill="none" stroke="${strokeCol}" stroke-width="1.8" points="${pts.join(" ")}"/>
    ${locked ? `<circle cx="213" cy="6" r="3" fill="#f59e0b"/><text x="206" y="11" fill="#f59e0b" font-size="8.5" font-weight="800" text-anchor="end">N-Wave</text>` : ""}`;
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
  if (!svg || historyPoints.length < 2) return;
  const u = UNIT_SYSTEMS[unitIdx];
  const recent = historyPoints.slice(-24);
  const rates = recent.map(p => (p.rain_rate || 0) * u.rMul);
  const peakRate = Math.max(...rates);
  // Dynamic Y-axis rescaling: adapts smoothly from light drizzle (1-2 mm/h) to heavy downpour (40+ mm/h)
  const maxR = niceCeiling(peakRate * 1.1, 1.0 * u.rMul);
  const midR = maxR * 0.5;
  const rDec = u.rUnit === "in" ? 2 : (maxR < 5 ? 1 : 0);

  const xStart = 30;
  const xEnd = 316;
  const xMid = (xStart + xEnd) * 0.5;
  const w = (xEnd - xStart) / recent.length;
  const yTop = 10;
  const yMid = 46;
  const yBot = 82;

  let html = `
    <line x1="${xStart}" y1="${yTop}" x2="${xEnd}" y2="${yTop}" stroke="rgba(142,162,198,0.20)" stroke-dasharray="2 2"/>
    <text x="25" y="${yTop + 3}" fill="#8ea2c6" font-size="9" text-anchor="end">${maxR.toFixed(rDec)}</text>
    <line x1="${xStart}" y1="${yMid}" x2="${xEnd}" y2="${yMid}" stroke="rgba(142,162,198,0.16)" stroke-dasharray="2 2"/>
    <text x="25" y="${yMid + 3}" fill="#8ea2c6" font-size="9" text-anchor="end">${midR.toFixed(rDec)}</text>
    <line x1="${xStart}" y1="${yBot}" x2="${xEnd}" y2="${yBot}" stroke="rgba(142,162,198,0.28)"/>
    <text x="25" y="${yBot + 3}" fill="#8ea2c6" font-size="9" text-anchor="end">0</text>
    <text x="${xStart}" y="99" fill="#8ea2c6" font-size="9" text-anchor="start">-60m</text>
    <text x="${xMid.toFixed(1)}" y="99" fill="#8ea2c6" font-size="9" text-anchor="middle">-30m</text>
    <text x="${xEnd}" y="99" fill="#8ea2c6" font-size="9" text-anchor="end">Now</text>`;

  // Stacked Liquid-Equivalent Precipitation Rate (mm/h): Blue = Liquid Rain, Violet = Hail Liquid-Water Equivalent (mm/h)
  html += rates.map((val, i) => {
    const h = val > 0.01 * u.rMul ? Math.max(3.5, (val / maxR) * (yBot - yTop)) : 1.5;
    const x = xStart + i * w;
    const bw = Math.max(3.0, w - 2.2);
    const hailRate = (recent[i].hail_lwe_rate || 0) * u.rMul;
    if (hailRate > 0.02 * u.rMul && h > 5) {
      const hailH = Math.min(h * 0.45, Math.max(3.5, (hailRate / maxR) * (yBot - yTop)));
      const rainH = h - hailH;
      return `
        <rect x="${x.toFixed(1)}" y="${(yBot - rainH).toFixed(1)}" width="${bw.toFixed(1)}" height="${rainH.toFixed(1)}" rx="1" fill="#38bdf8"/>
        <rect x="${x.toFixed(1)}" y="${(yBot - h).toFixed(1)}" width="${bw.toFixed(1)}" height="${hailH.toFixed(1)}" rx="1.5" fill="#c084fc"/>`;
    }
    const col = val >= 10 * u.rMul ? "#38bdf8" : (val > 0.01 * u.rMul ? "#60a5fa" : "var(--border)");
    return `<rect x="${x.toFixed(1)}" y="${(yBot - h).toFixed(1)}" width="${bw.toFixed(1)}" height="${h.toFixed(1)}" rx="1.5" fill="${col}"/>`;
  }).join("");

  svg.innerHTML = html;
}

function renderLightningTimeline(d) {
  const svg = document.getElementById("lt-timeline-svg");
  const statusEl = document.getElementById("lt-dist");
  if (!svg || !statusEl) return;

  const bins12 = (d.lightning && Array.isArray(d.lightning.timeline_60m))
    ? d.lightning.timeline_60m
    : [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  // Expand 12 bins to 24 bars (matching Precipitation & Pressure bar density!)
  const bins = [];
  bins12.forEach(c => { bins.push(Math.floor(c * 0.6), c); });
  const peakStrikes = Math.max(...bins);
  const maxB = Math.max(2, Math.ceil(peakStrikes * 1.15));
  const midB = Math.round(maxB * 0.5);
  const xStart = 30;
  const xEnd = 316;
  const xMid = (xStart + xEnd) * 0.5;
  const w = (xEnd - xStart) / bins.length;
  const yTop = 8;
  const yMid = 32;
  const yBot = 56;

  let html = `
    <line x1="${xStart}" y1="${yTop}" x2="${xEnd}" y2="${yTop}" stroke="rgba(142,162,198,0.20)" stroke-dasharray="2 2"/>
    <text x="25" y="${yTop + 3}" fill="#8ea2c6" font-size="9" text-anchor="end">${maxB}</text>
    <line x1="${xStart}" y1="${yMid}" x2="${xEnd}" y2="${yMid}" stroke="rgba(142,162,198,0.16)" stroke-dasharray="2 2"/>
    <text x="25" y="${yMid + 3}" fill="#8ea2c6" font-size="9" text-anchor="end">${midB}</text>
    <line x1="${xStart}" y1="${yBot}" x2="${xEnd}" y2="${yBot}" stroke="rgba(142,162,198,0.28)"/>
    <text x="25" y="${yBot + 3}" fill="#8ea2c6" font-size="9" text-anchor="end">0</text>
    <text x="${xStart}" y="71" fill="#8ea2c6" font-size="9" text-anchor="start">-60m</text>
    <text x="${xMid.toFixed(1)}" y="71" fill="#8ea2c6" font-size="9" text-anchor="middle">-30m</text>
    <text x="${xEnd}" y="71" fill="#8ea2c6" font-size="9" text-anchor="end">Now</text>`;

  html += bins.map((cnt, i) => {
    const h = cnt > 0 ? Math.max(4.5, (cnt / maxB) * (yBot - yTop)) : 1.5;
    const x = xStart + i * w;
    const bw = Math.max(3.0, w - 2.2);
    const col = cnt >= 3 ? "#f59e0b" : (cnt > 0 ? "#38bdf8" : "var(--border)");
    return `<rect x="${x.toFixed(1)}" y="${(yBot - h).toFixed(1)}" width="${bw.toFixed(1)}" height="${h.toFixed(1)}" rx="1.5" fill="${col}"/>`;
  }).join("");
  svg.innerHTML = html;

  if (d.lightning.strikes > 0) {
    if (d.environment && d.environment.thunder_locked && d.environment.thunder_dist_km > 0) {
      statusEl.textContent = `${d.lightning.strikes} strikes · ${d.environment.thunder_dist_km.toFixed(2)} km (Thunder Lock)`;
    } else {
      statusEl.textContent = `${d.lightning.strikes} strikes · ~${d.lightning.distance_km} km`;
    }
  } else {
    statusEl.textContent = "0 strikes · >40 km";
  }
}

function renderLineChart(svgId, seriesList, yUnit) {
  const svg = document.getElementById(svgId);
  if (!svg || historyPoints.length < 2) return;
  let allVals = [];
  seriesList.forEach(s => { allVals = allVals.concat(s.values); });
  const minV = Math.min(...allVals), maxV = Math.max(...allVals);
  const span = Math.max(1.0, maxV - minV);
  let html = "";
  for (let g = 0; g <= 3; g++) {
    const y = 18 + (g / 3) * 136;
    const val = maxV - (g / 3) * span;
    html += `<line x1="42" y1="${y}" x2="548" y2="${y}" stroke="rgba(142,162,198,0.14)" stroke-dasharray="3 3"/>`;
    html += `<text x="38" y="${y + 4}" fill="#8ea2c6" font-size="9.5" text-anchor="end">${val.toFixed(1)}${yUnit}</text>`;
  }
  seriesList.forEach(s => {
    const pts = s.values.map((v, i) => {
      const x = 44 + (i / (s.values.length - 1)) * 502;
      const y = 154 - ((v - minV) / span) * 136;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    }).join(" ");
    if (s.fill) html += `<polygon fill="${s.fill}" points="44,154 ${pts} 546,154"/>`;
    html += `<polyline fill="none" stroke="${s.color}" stroke-width="${s.width || 2}" ${s.dash ? `stroke-dasharray="${s.dash}"` : ""} points="${pts}"/>`;
  });
  svg.innerHTML = html;
}

function renderAllCharts() {
  if (historyPoints.length < 2) return;
  const u = UNIT_SYSTEMS[unitIdx];
  const pts = trendRange === "1h" ? historyPoints.slice(-16) : historyPoints;
  renderLineChart("chart-temp", [
    { values: pts.map(p => u.tFn(p.temp)), color: "#38bdf8", fill: "rgba(56,189,248,0.12)", width: 2.3 },
    { values: pts.map(p => u.tFn(p.dew)),  color: "#22c55e", dash: "5 3", width: 1.8 },
    { values: pts.map(p => u.tFn(p.feels)),color: "#f59e0b", dash: "2 3", width: 1.6 }
  ], u.tUnit);
  renderLineChart("chart-wind", [
    { values: pts.map(p => p.gust * u.wMul), color: "#f59e0b", width: 2.0 },
    { values: pts.map(p => p.wind * u.wMul), color: "#38bdf8", fill: "rgba(56,189,248,0.14)", width: 2.3 }
  ], "");
  renderLineChart("chart-baro-rain", [
    { values: pts.map(p => p.qnh * u.pMul), color: "#a855f7", fill: "rgba(168,85,247,0.12)", width: 2.3 }
  ], "");
}

function clampPct(v, lo, hi) {
  return Math.max(0, Math.min(100, ((v - lo) / (hi - lo)) * 100));
}

function applyTelemetry(d) {
  if (!d || !d.wind) return;
  lastState = d;
  const u = UNIT_SYSTEMS[unitIdx];
  const pDec = u.pUnit === "inHg" ? 2 : 1;
  const rDec = u.rUnit === "in" ? 2 : 1;
  const cardDir = degToCardinal(d.wind.dir_deg);

  // Sky Hero
  renderSkyHeroVisual(d);
  document.getElementById("hero-temp").textContent = `${u.tFn(d.environment.temp_c).toFixed(1)}°`;
  document.getElementById("hero-cond").textContent =
    `${d.environment.forecast || "Fine Weather"} · Feels like ${u.tFn(d.environment.feels_like_c).toFixed(1)}°`;
  document.getElementById("hc-wind").textContent =
    `${(d.wind.speed_m_s * u.wMul).toFixed(1)} ${u.wUnit} ${cardDir}`;
  const d3h = d.environment.pressure_3h_delta_hpa || 0;
  const arrowP = d3h < -1 ? "↓" : (d3h > 1 ? "↑" : "→");
  document.getElementById("hc-baro").textContent =
    `${(d.environment.qnh_hpa * u.pMul).toFixed(pDec)} ${u.pUnit} ${arrowP}`;
  document.getElementById("hc-rain").textContent =
    `${d.rain.state} · ${(d.rain.daily_mm * u.rMul).toFixed(rDec)} ${u.rUnit}`;
  document.getElementById("hc-aqi").textContent = `Air: ${d.air_quality.level} (${d.air_quality.aqi})`;
  document.getElementById("hc-aqi-dot").style.background =
    d.air_quality.aqi <= 50 ? "#22c55e" : (d.air_quality.aqi <= 100 ? "#f59e0b" : "#ef4444");

  // 1. Wind Compass & Labeled Speed Bar (No Bft jargon)
  const bft = Math.min(12, Math.max(0, Math.round(d.wind.beaufort || 0)));
  document.getElementById("wind-beaufort").textContent = BEAUFORT_NAMES[bft];
  document.getElementById("wind-speed").textContent = (d.wind.speed_m_s * u.wMul).toFixed(1);
  document.getElementById("wind-unit-dir").textContent = `${u.wUnit} · ${cardDir}`;
  document.getElementById("wind-arrow").setAttribute("transform", `rotate(${d.wind.dir_deg} 115 115)`);
  document.getElementById("wind-lull").textContent = (d.wind.lull_m_s * u.wMul).toFixed(1);
  document.getElementById("wind-gust").textContent = `${(d.wind.gust_m_s * u.wMul).toFixed(1)} ${u.wUnit}`;
  const wDelta1h = (d.wind.speed_m_s - d.wind.avg_10m_m_s) * u.wMul;
  const wArrow = wDelta1h > 0.2 ? "↗ +" : (wDelta1h < -0.2 ? "↘ " : "→ +");
  document.getElementById("wind-trend-lbl").textContent = `${wArrow}${wDelta1h.toFixed(1)} ${u.wUnit}/h`;
  const maxScaleW = Math.max(12, d.wind.gust_m_s * 1.28);
  const lullP = clampPct(d.wind.lull_m_s, 0, maxScaleW);
  const avgP = clampPct(d.wind.speed_m_s, 0, maxScaleW);
  const gustP = clampPct(d.wind.gust_m_s, 0, maxScaleW);
  document.getElementById("wind-range-span").style.left = `${lullP.toFixed(0)}%`;
  document.getElementById("wind-range-span").style.width = `${Math.max(6, gustP - lullP).toFixed(0)}%`;
  document.getElementById("wind-avg-dot").style.left = `${avgP.toFixed(0)}%`;

  // Dynamic Surface Wind Load (Last 1h Min — 1h Trend — 1h Max): q = 0.0625 * v^2 (kg/m²)
  const wlMul = unitIdx === 1 ? 0.2048 : 1.0;
  const wlUnit = unitIdx === 1 ? "lb/ft²" : "kg/m²";
  const wlDec = unitIdx === 1 ? 2 : 1;
  const recent1h = historyPoints.slice(-30);
  const minV1h = recent1h.length > 1 ? Math.min(d.wind.lull_m_s, ...recent1h.map(p => p.wind * 0.65)) : d.wind.lull_m_s;
  const maxG1h = recent1h.length > 1 ? Math.max(d.wind.gust_m_s, ...recent1h.map(p => p.gust)) : d.wind.gust_m_s;
  const wlMin = 0.0625 * minV1h * minV1h * wlMul;
  const wlNow = 0.0625 * d.wind.speed_m_s * d.wind.speed_m_s * wlMul;
  const wlAvg10 = 0.0625 * d.wind.avg_10m_m_s * d.wind.avg_10m_m_s * wlMul;
  const wlMax = 0.0625 * maxG1h * maxG1h * wlMul;
  const wlDelta = wlNow - wlAvg10;
  const wlArrow = wlDelta > 0.05 ? "↗ +" : (wlDelta < -0.05 ? "↘ " : "→ +");
  const wlCeil = Math.max(4.0 * wlMul, wlMax * 1.25);
  document.getElementById("wload-min").textContent = wlMin.toFixed(wlDec);
  document.getElementById("wload-trend-lbl").textContent = `${wlArrow}${wlDelta.toFixed(wlDec)} ${wlUnit}/h`;
  document.getElementById("wload-max").textContent = `${wlMax.toFixed(wlDec)} ${wlUnit}`;
  const wlMinP = clampPct(wlMin, 0, wlCeil);
  const wlNowP = clampPct(wlNow, 0, wlCeil);
  const wlMaxP = clampPct(wlMax, 0, wlCeil);
  document.getElementById("wload-span").style.left = `${wlMinP.toFixed(0)}%`;
  document.getElementById("wload-span").style.width = `${Math.max(6, wlMaxP - wlMinP).toFixed(0)}%`;
  document.getElementById("wload-now-dot").style.left = `${wlNowP.toFixed(0)}%`;
  if (d.wind.rose) renderRosePetals(d.wind.rose);

  // 2. Thermal & Humidity (Unified Range-Capsule Bar + 2x2 Micro-Climate Bento Grid)
  const curT = u.tFn(d.environment.temp_c);
  const feelsT = u.tFn(d.environment.feels_like_c);
  const dewT = u.tFn(d.environment.dew_point_c);
  const wetT = u.tFn(d.environment.wet_bulb_c !== undefined ? d.environment.wet_bulb_c : (d.environment.temp_c * 0.65 + d.environment.dew_point_c * 0.35));

  document.getElementById("env-temp").textContent = `${curT.toFixed(1)}°`;
  document.getElementById("env-feels").textContent = `Feels like ${feelsT.toFixed(1)}° outside`;
  document.getElementById("env-rh").textContent = `${Math.round(d.environment.humidity_rh)}%`;
  const rhCirc = (d.environment.humidity_rh / 100) * 220;
  document.getElementById("rh-arc").setAttribute("stroke-dasharray", `${rhCirc.toFixed(0)} 220`);

  // Unified Range-Capsule Bar 3: 24h Low — 1h Trend — 24h High
  const tHistC = historyPoints.length > 1 ? historyPoints.map(p => p.temp) : [d.environment.temp_c - 2.4, d.environment.temp_c + 1.2];
  const minTC = Math.min(...tHistC, d.environment.temp_c - 1.8);
  const maxTC = Math.max(...tHistC, d.environment.temp_c + 1.2);
  const prev1hTC = tHistC.length > 6 ? tHistC[tHistC.length - 6] : d.environment.temp_c - 0.6;
  const tRate1h = u.tFn(d.environment.temp_c) - u.tFn(prev1hTC);
  const tTrendStr = tRate1h > 0.15 ? `↗ +${tRate1h.toFixed(1)}°/h · Warming` : (tRate1h < -0.15 ? `↘ ${tRate1h.toFixed(1)}°/h · Cooling` : `→ ${tRate1h >= 0 ? "+" : ""}${tRate1h.toFixed(1)}°/h · Steady`);
  document.getElementById("temp-min-lbl").textContent = `${u.tFn(minTC).toFixed(1)}°`;
  document.getElementById("temp-trend-lbl").textContent = tTrendStr;
  document.getElementById("temp-max-lbl").textContent = `${u.tFn(maxTC).toFixed(1)}°`;
  const winLo = minTC - 3.5;
  const winHi = maxTC + 3.5;
  const pMinT = clampPct(minTC, winLo, winHi);
  const pMaxT = clampPct(maxTC, winLo, winHi);
  const pNowT = clampPct(d.environment.temp_c, winLo, winHi);
  document.getElementById("t24-bridge").style.left = `${pMinT.toFixed(1)}%`;
  document.getElementById("t24-bridge").style.width = `${Math.max(8, pMaxT - pMinT).toFixed(1)}%`;
  document.getElementById("pin-temp").style.left = `${pNowT.toFixed(1)}%`;

  // 2x2 Bento Cells: Feels Like, Dew Point, Wet-Bulb, Absolute Humidity
  document.getElementById("env-feels-sm").textContent = `${feelsT.toFixed(1)}°`;
  const feelsDelta = feelsT - curT;
  const fSign = feelsDelta >= 0 ? "+" : "";
  document.getElementById("feels-delta-lbl").textContent =
    Math.abs(feelsDelta) < 0.3 ? "Matches actual air" : `${fSign}${feelsDelta.toFixed(1)}° (${feelsDelta < 0 ? "wind chill" : "sun & humidity"})`;

  document.getElementById("env-dew").textContent = `${dewT.toFixed(1)}°`;
  const dewSpread = Math.max(0, d.environment.temp_c - d.environment.dew_point_c);
  const dewSpreadDisp = Math.abs(curT - dewT);
  document.getElementById("fog-risk-lbl").textContent =
    dewSpread <= 2.0 ? `High fog risk (${dewSpreadDisp.toFixed(1)}° gap)` : `Low fog risk (${dewSpreadDisp.toFixed(1)}° gap)`;

  document.getElementById("env-wetbulb").textContent = `${wetT.toFixed(1)}°`;
  const wbC = d.environment.wet_bulb_c !== undefined ? d.environment.wet_bulb_c : 14.0;
  document.getElementById("wetbulb-sub").textContent =
    wbC <= 0.5 ? "Snow phase (< 0.5°)" : (wbC >= 24.0 ? "High heat-stress limit" : "Evaporative cool limit");

  const esHpa = 6.112 * Math.exp((17.67 * d.environment.temp_c) / (d.environment.temp_c + 243.5));
  const eHpa = esHpa * (d.environment.humidity_rh / 100.0);
  const absHum = (216.7 * eHpa) / (d.environment.temp_c + 273.15);
  document.getElementById("abs-hum-lbl").textContent = `${absHum.toFixed(1)} g/m³`;
  const vpd = d.environment.vpd_kpa || 0.8;
  const dryTxt = d.rain.raining ? "Wet" : (vpd >= 0.55 ? "Fast dry" : "Slow dry");
  const muggyTxt = absHum >= 16.5 ? "Humid air" : (absHum >= 9.0 ? "Pleasant air" : "Crisp air");
  document.getElementById("abs-hum-sub").textContent = `${muggyTxt} · ${dryTxt}`;

  const tC = d.environment.temp_c;
  document.getElementById("comfort-badge").textContent =
    d.environment.frost_risk ? "Frost Warning" : (tC > 30 ? "Heat Stress" : (absHum > 16.5 ? "Humid" : "Comfortable"));
  document.getElementById("comfort-badge").className =
    d.environment.frost_risk || tC > 30 ? "badge warn" : "badge ok";

  // 3. Barometer (Dedicated Forecast Banner + Labeled 3h Ago Needle + Integer hPa Precision)
  const pIntDec = u.pUnit === "inHg" ? 2 : 0;
  const qnhAngle = Math.max(-88, Math.min(88, ((d.environment.qnh_hpa - 1010) / 30) * 90));
  const qnh3hAgo = d.environment.qnh_hpa - d3h;
  const ghostAngle = Math.max(-88, Math.min(88, ((qnh3hAgo - 1010) / 30) * 90));
  document.getElementById("baro-needle").setAttribute("transform", `rotate(${qnhAngle.toFixed(1)} 120 110)`);
  document.getElementById("baro-ghost-needle").setAttribute("transform", `rotate(${ghostAngle.toFixed(1)} 120 110)`);
  document.getElementById("env-qnh").textContent = (d.environment.qnh_hpa * u.pMul).toFixed(pIntDec);
  document.getElementById("env-qnh-unit").textContent = u.pUnit;

  const fcast = d.environment.forecast || "Fine Weather";
  document.getElementById("env-zambretti").textContent = fcast;
  const rainProb = d.environment.rain_prob_pct !== undefined ? d.environment.rain_prob_pct : 12;
  document.getElementById("baro-rain-prob").textContent = `Rain Chance ${rainProb}%`;
  document.getElementById("baro-rain-prob-fill").style.width = `${rainProb}%`;
  const frontType = d.environment.front_type || "Steady Air Mass";
  const windShift = d.environment.wind_shift || "Steady Bearing";
  document.getElementById("baro-front-lbl").textContent = `${frontType} · ${windShift}`;
  const wavePa = d.environment.microbarom_pa !== undefined ? d.environment.microbarom_pa : 0.08;
  const waveDesc = wavePa >= 0.45 ? "Squall Wave" : (wavePa >= 0.20 ? "Frontal Ripple" : "Calm");
  document.getElementById("baro-wave-lbl").textContent = `Wave ${wavePa.toFixed(2)} Pa · ${waveDesc}`;

  const airRho = d.environment.air_density_kg_m3 !== undefined ? d.environment.air_density_kg_m3 : 1.208;
  document.getElementById("baro-density").textContent = `${airRho.toFixed(3)} kg/m³`;
  document.getElementById("baro-density-fill").style.width = `${clampPct(airRho, 1.10, 1.35).toFixed(0)}%`;
  const soundC = d.wind.sound_m_s || 343.1;
  document.getElementById("baro-sound").textContent = `${soundC.toFixed(1)} m/s`;
  document.getElementById("baro-sound-fill").style.width = `${clampPct(soundC, 325, 358).toFixed(0)}%`;

  const synD3h = d.environment.synoptic_3h_delta_hpa !== undefined ? d.environment.synoptic_3h_delta_hpa : d3h;
  const rate1h = (synD3h / 3.0) * u.pMul;
  const sign1h = rate1h >= 0 ? "+" : "";
  const rateDec = u.pUnit === "inHg" ? 3 : 1;
  document.getElementById("baro-trend-badge").textContent =
    `Change: ${sign1h}${rate1h.toFixed(rateDec)} ${u.pUnit}/h`;

  // 4. Precipitation (This Shower in top right + Meaningful state next to mm/h + Hailstone Ø mm)
  const showerDepth = (d.rain.event_mm || 0.0) * u.rMul;
  document.getElementById("rain-state").textContent = `This Shower: ${showerDepth.toFixed(rDec)} ${u.rUnit}`;
  document.getElementById("rain-state").className = d.rain.raining ? "badge info" : "badge ok";
  document.getElementById("rain-rate").textContent = (d.rain.rate_mm_h * u.rMul).toFixed(rDec);
  document.getElementById("rain-rate-unit").textContent = `${u.rUnit}/h · ${d.rain.state}`;
  document.getElementById("rain-daily").textContent = `${(d.rain.daily_mm * u.rMul).toFixed(rDec)} ${u.rUnit}`;
  document.getElementById("rain-water").style.height = `${Math.max(6, clampPct(d.rain.daily_mm, 0, 35)).toFixed(0)}%`;
  const hailDiam = d.rain.hail_hits > 0 ? (d.rain.hail_diam_mm || 9.4) : 0.0;

  // Update Trends tab KPIs (Last Hour, Today, This Month)
  const r1hEl = document.getElementById("trend-rain-1h");
  if (r1hEl) {
    r1hEl.textContent = `${((d.rain.hourly_mm || 0) * u.rMul).toFixed(rDec)} ${u.rUnit}`;
    document.getElementById("trend-rain-day").textContent = `${(d.rain.daily_mm * u.rMul).toFixed(rDec)} ${u.rUnit}`;
    document.getElementById("trend-rain-month").textContent = `${((d.rain.total_mm || 145.2) * u.rMul).toFixed(rDec)} ${u.rUnit}`;
  }
  document.getElementById("rain-hail").textContent =
    d.rain.hail_hits > 0 ? `Ø ${hailDiam.toFixed(1)} mm (${d.rain.hail_hits} hits)` : "None (0 hits)";
  document.querySelectorAll("#hail-scale .hs-item").forEach((el) => {
    const mm = parseFloat(el.dataset.mm);
    const active = hailDiam > 0 && Math.abs(hailDiam - mm) <= 3.5;
    el.className = active ? "hs-item active" : "hs-item";
  });

  // 5. Sun, UV & Clouds (Day) <-> Night Sky, Moon & Bortle Stargazing (Night)
  const isNight = Boolean(d.environment.is_night);
  const uvi = d.environment.uv_index;
  const cloudPct = d.environment.cloud_cover_pct !== undefined ? d.environment.cloud_cover_pct : 20;
  const cloudCirc = (cloudPct / 100.0) * 220.0;
  document.getElementById("cloud-arc").setAttribute("stroke-dasharray", `${cloudCirc.toFixed(0)} 220`);
  document.getElementById("cloud-cover-lbl").textContent = `${Math.round(cloudPct)}%`;
  const moonIllum = (d.astronomy && d.astronomy.moon_illum_pct !== undefined) ? d.astronomy.moon_illum_pct : 74;
  const moonPhase = (d.astronomy && d.astronomy.moon_phase) ? d.astronomy.moon_phase : "Waxing Gibbous";
  document.getElementById("sun-window").textContent = "06:48–18:24";
  document.getElementById("moon-window").textContent = `14:15–02:40 · ${moonIllum}%`;
  document.getElementById("sky24-sun-dot").style.left = isNight ? "12%" : "58%";
  document.getElementById("sky24-moon-dot").style.left = isNight ? "82%" : "66%";

  if (isNight) {
    document.getElementById("solar-card-title").textContent = "NIGHT SKY & MOON";
    document.getElementById("daylight-badge").textContent = `Moon ${moonIllum}% · ${moonPhase}`;
    document.getElementById("daylight-badge").className = "badge info";

    const mlx = d.environment.lux * 1000.0;
    const nCloudDesc = cloudPct >= 60 ? "Night Clouds" : (cloudPct >= 20 ? "Scattered" : "Clear Starry");
    document.getElementById("env-solar").textContent = mlx < 10 ? mlx.toFixed(2) : mlx.toFixed(0);
    document.getElementById("env-solar-unit").textContent = `mlx · ${nCloudDesc}`;
    document.getElementById("env-lux").textContent = "Night Sky Illuminance";

    const bortle = d.environment.bortle || 2;
    const sqm = d.environment.sqm || 21.4;
    const bDesc = bortle <= 2 ? "Milky Way" : (bortle <= 4 ? "Rural Starry" : "Bright Moon");
    document.getElementById("uv-left-lbl").innerHTML = `Sky Darkness <b class="accent">Bortle ${bortle} · ${bDesc}</b>`;
    document.getElementById("uv-sub-row").textContent = `${sqm.toFixed(1)} mag/arcsec²`;
    document.getElementById("uv-or-bortle-bar").className = "uv-rainbow-bar bortle-mode";
    document.getElementById("uv-dot").style.left = `${clampPct(bortle, 1, 9).toFixed(0)}%`;
    document.getElementById("uv-scale-ticks").innerHTML = `
      <span>1 Dark</span><span>3 Rural</span><span>5 Suburb</span><span>7 Bright</span><span>9 City</span>`;
  } else {
    document.getElementById("solar-card-title").textContent = "SUN, UV & CLOUDS";
    const burnMin = d.environment.burn_time_min || 0;
    document.getElementById("daylight-badge").textContent =
      burnMin > 0 ? `Burn ~${burnMin} min` : "No Burn Risk";
    document.getElementById("daylight-badge").className = uvi >= 6 ? "badge warn" : "badge ok";

    const cloudDesc = cloudPct >= 80 ? (d.rain.raining ? "Heavy Wet Cloud" : "Overcast") : (cloudPct >= 50 ? "Broken Clouds" : (cloudPct >= 20 ? "Scattered" : "Clear Sky"));
    document.getElementById("env-solar").textContent = d.environment.solar_w_m2.toFixed(0);
    document.getElementById("env-solar-unit").textContent = `W/m² · ${cloudDesc}`;
    document.getElementById("env-lux").textContent = `Brightness: ${Math.round(d.environment.lux).toLocaleString()} lx`;

    const uvCat = uvi >= 8 ? "Very High" : (uvi >= 6 ? "High" : (uvi >= 3 ? "Moderate" : "Low"));
    document.getElementById("uv-left-lbl").innerHTML = `UV Index <b class="accent">${uvi.toFixed(1)} · ${uvCat}</b>`;
    document.getElementById("uv-sub-row").textContent = "";
    document.getElementById("uv-or-bortle-bar").className = "uv-rainbow-bar";
    document.getElementById("uv-dot").style.left = `${clampPct(uvi, 0, 11.5).toFixed(0)}%`;
    document.getElementById("uv-scale-ticks").innerHTML = `
      <span>0 Low</span><span>3 Mod</span><span>6 High</span><span>8 Very High</span><span>11+ Ext</span>`;
  }

  // 6. Air Quality & Dedicated 60m Lightning Timeline (Zero AQI Duplication)
  const aqi = d.air_quality.aqi;
  document.getElementById("aq-dom-badge").textContent = `Main: ${d.air_quality.dominant}`;
  document.getElementById("aq-dom-badge").className = aqi <= 50 ? "badge ok" : (aqi <= 100 ? "badge warn" : "badge danger");
  document.getElementById("aq-aqi-big").textContent = aqi;
  document.getElementById("aq-level-lbl").textContent = d.air_quality.level.toUpperCase();
  const aqiCirc = Math.min(239, (aqi / 200) * 239);
  const aqiArcEl = document.getElementById("aqi-arc");
  aqiArcEl.setAttribute("stroke-dasharray", `${aqiCirc.toFixed(0)} 239`);
  aqiArcEl.setAttribute("class", aqi <= 50 ? "ring-fg-ok" : "ring-fg-cyan");

  const dryPm25 = d.air_quality.dry_pm2_5 !== undefined ? d.air_quality.dry_pm2_5 : d.air_quality.pm2_5;
  const dryPm10 = d.air_quality.dry_pm10 !== undefined ? d.air_quality.dry_pm10 : d.air_quality.pm10;
  const pm1Val = d.air_quality.pm1_0 !== undefined ? d.air_quality.pm1_0 : dryPm25 * 0.6;
  const pm4Val = d.air_quality.pm4_0 !== undefined ? d.air_quality.pm4_0 : dryPm25 * 1.3;
  const coarseBio = Math.max(0.4, dryPm10 - dryPm25);
  const bioLvl = coarseBio >= 25 ? "High" : (coarseBio >= 10 ? "Mod" : "Low");
  const compCo2 = d.air_quality.comp_co2_ppm || d.air_quality.co2_ppm;

  document.getElementById("aq-pm1").textContent = `${pm1Val.toFixed(1)} µg/m³`;
  document.getElementById("b-pm1").style.width = `${clampPct(pm1Val, 0, 25).toFixed(0)}%`;
  document.getElementById("aq-pm25").textContent = `${dryPm25.toFixed(1)} µg/m³`;
  document.getElementById("b-pm25").style.width = `${clampPct(dryPm25, 0, 35).toFixed(0)}%`;
  document.getElementById("b-pm25").className = `b-fill ${dryPm25 <= 15 ? "ok" : "warn"}`;
  document.getElementById("aq-pm4").textContent = `${pm4Val.toFixed(1)} µg/m³`;
  document.getElementById("b-pm4").style.width = `${clampPct(pm4Val, 0, 50).toFixed(0)}%`;
  document.getElementById("aq-pm10").textContent = `${dryPm10.toFixed(1)} µg/m³`;
  document.getElementById("b-pm10").style.width = `${clampPct(dryPm10, 0, 90).toFixed(0)}%`;
  document.getElementById("aq-bio").textContent = `${coarseBio.toFixed(1)} · ${bioLvl}`;
  document.getElementById("b-bio").style.width = `${clampPct(coarseBio, 0, 45).toFixed(0)}%`;
  document.getElementById("aq-co2").textContent = `${compCo2} · ${d.air_quality.voc_index} · ${d.air_quality.nox_index}`;
  document.getElementById("b-co2").style.width = `${clampPct(compCo2, 380, 1000).toFixed(0)}%`;

  renderSparkBaro();
  renderSparkRain();
  renderLightningTimeline(d);

  // Tab 3: Station Digital Twin & Visual Error-Correction Bars
  document.getElementById("twin-vin-badge").textContent =
    `${d.heater.vin_v.toFixed(1)} V · ${d.heater.state === 1 ? "DE-ICING 4.0 W" : "Heater Standby"}`;
  document.getElementById("tw-dome").textContent = `UV ${uvi.toFixed(1)} · ${d.environment.solar_w_m2.toFixed(0)} W/m²`;
  document.getElementById("tw-crown").textContent = `c = ${d.wind.sound_m_s.toFixed(1)} m/s`;
  document.getElementById("tw-heater").textContent = `${d.heater.reflector_c.toFixed(1)} °C · ${d.heater.power_w.toFixed(1)} W`;
  document.getElementById("tw-probe").textContent = `${d.environment.temp_c.toFixed(1)} °C · ${compCo2} ppm CO₂`;
  document.getElementById("tw-main").textContent = `${d.wifi.rssi_dbm} dBm · ${d.heater.vin_v.toFixed(1)} V`;

  const dtSol = d.environment.solar_offset_c || 0;
  const dpBern = d.environment.dyn_offset_hpa || 0;
  const pmRat = d.air_quality.pm2_5 > 0.1 ? (dryPm25 / d.air_quality.pm2_5) : 1.0;
  document.getElementById("cv-solar").textContent = `-${dtSol.toFixed(2)} °C`;
  document.getElementById("cb-solar").style.width = `${clampPct(dtSol, 0, 1.5).toFixed(0)}%`;
  document.getElementById("cv-bern").textContent = `+${dpBern.toFixed(2)} hPa`;
  document.getElementById("cb-bern").style.width = `${clampPct(dpBern, 0, 1.0).toFixed(0)}%`;
  document.getElementById("cv-fog").textContent = `${pmRat.toFixed(2)}× (Dry ${dryPm25.toFixed(1)} vs Raw ${d.air_quality.pm2_5.toFixed(1)})`;
  document.getElementById("cb-fog").style.width = `${(pmRat * 100).toFixed(0)}%`;
  if (d.tof) {
    document.getElementById("cv-tof").textContent = `${d.tof.ns.toFixed(1)} / ${d.tof.sn.toFixed(1)} µs`;
  }
  document.getElementById("cv-heater").textContent = `${d.heater.duty_pct.toFixed(1)} % (${d.heater.power_w.toFixed(1)} W / 4.0 W)`;
  document.getElementById("cb-heater").style.width = `${Math.max(2, d.heater.duty_pct).toFixed(0)}%`;
  if (d.rs485_frame) document.getElementById("rs485-frame").textContent = d.rs485_frame;
}

// Settings & Maintenance actions
document.getElementById("btn-save-cfg").addEventListener("click", () => {
  const payload = {
    altitude_m: parseFloat(document.getElementById("cfg-alt").value),
    declination_deg: parseFloat(document.getElementById("cfg-decl").value),
    dt_ns_offset_us: parseFloat(document.getElementById("cfg-dt-ns").value),
    dt_ew_offset_us: parseFloat(document.getElementById("cfg-dt-ew").value),
    mqtt_uri: document.getElementById("cfg-mqtt").value,
    ha_prefix: document.getElementById("cfg-ha").value,
  };
  fetch("/api/config", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(payload) })
    .then(() => { document.getElementById("cfg-save-status").textContent = "Saved ✓"; })
    .catch(() => {});
});

["btn-reset-rain", "btn-bakeout", "btn-heat-test", "btn-zero-wind", "btn-reboot"].forEach((id) => {
  const el = document.getElementById(id);
  if (!el) return;
  el.addEventListener("click", () => {
    fetch("/api/action", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ action: id }) })
      .then(() => { document.getElementById("ota-status").textContent = `Executed ${id} ✓`; })
      .catch(() => {});
  });
});

function loadHistory() {
  return fetch("/api/history").then(r => r.json()).then(arr => {
    if (Array.isArray(arr) && arr.length > 1) {
      historyPoints = arr;
      renderSparkBaro();
      renderSparkRain();
      renderAllCharts();
    }
  }).catch(() => {});
}

function connectWs() {
  if (location.protocol === "file:") return;
  try {
    const proto = location.protocol === "https:" ? "wss:" : "ws:";
    const ws = new WebSocket(`${proto}//${location.host}/ws`);
    ws.onmessage = (ev) => {
      try {
        const d = JSON.parse(ev.data);
        applyTelemetry(d);
        if (historyPoints.length > 1) {
          historyPoints[historyPoints.length - 1] = {
            temp: d.environment.temp_c,
            dew: d.environment.dew_point_c,
            feels: d.environment.feels_like_c,
            wind: d.wind.avg_10m_m_s,
            gust: d.wind.gust_m_s,
            qnh: d.environment.qnh_hpa,
            rain_rate: d.rain.rate_mm_h,
            hail_lwe_rate: d.rain.hail_hits > 0 ? d.rain.rate_mm_h * 0.28 : 0.0,
          };
          renderSparkBaro();
          renderSparkRain();
        }
      } catch (_) {}
    };
    ws.onclose = () => setTimeout(connectWs, 2500);
  } catch (_) {}
}

loadHistory().then(() => {
  fetch("/api/state").then(r => r.json()).then(applyTelemetry).catch(() => {});
  connectWs();
});
