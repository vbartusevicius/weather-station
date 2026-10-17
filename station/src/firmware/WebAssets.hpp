#pragma once
// clang-format off

#include <cstddef>

namespace station::firmware::web_assets {

inline constexpr const char index_html_data[] = R"WEBUI(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>AeroMet V1 — Roof Weather Station</title>
  <link rel="stylesheet" href="/app.css" />
</head>
<body>
  <header class="topbar">
    <div class="brand">
      <span class="logo-dot"></span>
      <div>
        <h1>AeroMet V1</h1>
        <span class="subtitle">Solid-State Ultrasonic &amp; Haptic Roof Weather Station</span>
      </div>
    </div>
    <nav class="tabs">
      <button class="tab active" data-tab="overview">Overview &amp; Wind</button>
      <button class="tab" data-tab="airquality">Air Quality &amp; Storm</button>
      <button class="tab" data-tab="diagnostics">Ultrasonic &amp; Rain DSP</button>
      <button class="tab" data-tab="settings">Settings &amp; OTA</button>
    </nav>
    <div class="status-pills">
      <span class="pill ok" id="pill-link">LIVE WS</span>
      <span class="pill" id="pill-vin">24.0 V</span>
      <span class="pill" id="pill-heater">HEATER IDLE</span>
    </div>
  </header>

  <main class="container">
    <!-- TAB 1: OVERVIEW & WIND ROSE -->
    <section class="panel active" id="tab-overview">
      <div class="grid-3">
        <!-- WIND ROSE CARD -->
        <div class="card">
          <div class="card-header">
            <h2>Ultrasonic Wind &amp; Auto-North</h2>
            <span class="badge" id="wind-beaufort">Beaufort 2 (Light Breeze)</span>
          </div>
          <div class="wind-rose-wrap">
            <svg viewBox="0 0 220 220" class="wind-rose">
              <circle cx="110" cy="110" r="92" class="rose-ring" />
              <circle cx="110" cy="110" r="64" class="rose-ring-inner" />
              <line x1="110" y1="18" x2="110" y2="202" class="rose-axis" />
              <line x1="18" y1="110" x2="202" y2="110" class="rose-axis" />
              <text x="110" y="14" class="rose-dir north">N</text>
              <text x="209" y="114" class="rose-dir">E</text>
              <text x="110" y="216" class="rose-dir">S</text>
              <text x="11" y="114" class="rose-dir">W</text>
              <g id="wind-arrow" transform="rotate(215 110 110)">
                <polygon points="110,24 102,52 110,46 118,52" class="arrow-head" />
                <line x1="110" y1="44" x2="110" y2="150" class="arrow-shaft" />
              </g>
              <circle cx="110" cy="110" r="32" class="rose-center" />
              <text x="110" y="108" class="rose-speed" id="wind-speed">4.8</text>
              <text x="110" y="123" class="rose-unit">m/s (215° SW)</text>
            </svg>
          </div>
          <div class="metric-row">
            <div><span class="lbl">1m Avg</span><strong id="wind-avg1m">4.4 m/s</strong></div>
            <div><span class="lbl">10m Avg</span><strong id="wind-avg10m">4.1 m/s</strong></div>
            <div><span class="lbl">3s Gust</span><strong class="accent" id="wind-gust">7.2 m/s</strong></div>
            <div><span class="lbl">Lull</span><strong id="wind-lull">2.9 m/s</strong></div>
          </div>
          <div class="sub-footer">
            <span>Auto-North Offset: <b id="wind-north">+14.2°</b></span>
            <span>Mast Tilt: <b id="wind-tilt">0.4°</b></span>
          </div>
        </div>

        <!-- TEMPERATURE, HUMIDITY, PRESSURE, UV -->
        <div class="card">
          <div class="card-header">
            <h2>Thermo-Hygro, Baro &amp; Solar</h2>
            <span class="badge ok">SHT45 + BMP581</span>
          </div>
          <div class="hero-temp">
            <div>
              <span class="big-val" id="env-temp">18.4 °C</span>
              <span class="feels" id="env-feels">Feels like 17.8 °C</span>
            </div>
            <div class="rh-box">
              <span class="big-rh" id="env-rh">62.5 %</span>
              <span class="lbl">Relative Humidity</span>
            </div>
          </div>
          <div class="metric-grid">
            <div class="m-box"><span class="lbl">Dew Point</span><strong id="env-dew">11.1 °C</strong></div>
            <div class="m-box"><span class="lbl">Wet Bulb</span><strong id="env-wet">14.2 °C</strong></div>
            <div class="m-box"><span class="lbl">Sea-Level QNH</span><strong id="env-qnh">1016.8 hPa</strong></div>
            <div class="m-box"><span class="lbl">Station Baro</span><strong id="env-baro">1009.4 hPa</strong></div>
            <div class="m-box"><span class="lbl">Solar Irradiance</span><strong id="env-solar">642 W/m²</strong></div>
            <div class="m-box"><span class="lbl">Illuminance</span><strong id="env-lux">81,250 lx</strong></div>
          </div>
          <div class="uv-bar-wrap">
            <div class="uv-header">
              <span>UV Index (LTR-390UV)</span>
              <strong id="env-uv">5.4 (Moderate)</strong>
            </div>
            <div class="uv-track"><div class="uv-fill" id="uv-fill" style="width: 45%"></div></div>
          </div>
        </div>

        <!-- HAPTIC RAIN & PRECIPITATION -->
        <div class="card">
          <div class="card-header">
            <h2>Hybrid Haptic + Cap Rain</h2>
            <span class="badge info" id="rain-state">Light Rain</span>
          </div>
          <div class="hero-temp">
            <div>
              <span class="big-val" id="rain-rate">1.8 mm/h</span>
              <span class="feels">Instantaneous Rain Rate</span>
            </div>
            <div class="rh-box">
              <span class="big-rh" id="rain-daily">6.4 mm</span>
              <span class="lbl">Daily Accumulation</span>
            </div>
          </div>
          <div class="metric-grid">
            <div class="m-box"><span class="lbl">Event Rain</span><strong id="rain-event">2.1 mm</strong></div>
            <div class="m-box"><span class="lbl">Hourly Rain</span><strong id="rain-hourly">1.4 mm</strong></div>
            <div class="m-box"><span class="lbl">Total Rain</span><strong id="rain-total">142.8 mm</strong></div>
            <div class="m-box"><span class="lbl">Raindrops</span><strong id="rain-drops">4,812</strong></div>
            <div class="m-box"><span class="lbl">Hail Hits</span><strong id="rain-hail">0</strong></div>
            <div class="m-box"><span class="lbl">Shake Rejected</span><strong id="rain-rejected">19</strong></div>
          </div>
          <div class="sub-footer">
            <span>Capacitive Dome: <b class="ok-txt" id="rain-wet">WET (245 mV)</b></span>
            <span>IMU Filter: <b>Active</b></span>
          </div>
        </div>
      </div>
    </section>

    <!-- TAB 2: AIR QUALITY & LIGHTNING -->
    <section class="panel" id="tab-airquality">
      <div class="grid-2">
        <div class="card">
          <div class="card-header">
            <h2>Outdoor Air Quality (SEN55 + SGP41 + SCD41)</h2>
            <span class="badge ok" id="aq-badge">US AQI 28 · Good (EAQI 1)</span>
          </div>
          <div class="metric-grid">
            <div class="m-box"><span class="lbl">PM1.0 (Ultrafine)</span><strong id="aq-pm1">3.8 µg/m³</strong></div>
            <div class="m-box"><span class="lbl">PM2.5 (Fine)</span><strong id="aq-pm25">6.4 µg/m³</strong></div>
            <div class="m-box"><span class="lbl">PM4.0 (Respirable)</span><strong id="aq-pm4">8.1 µg/m³</strong></div>
            <div class="m-box"><span class="lbl">PM10 (Coarse)</span><strong id="aq-pm10">13.2 µg/m³</strong></div>
            <div class="m-box"><span class="lbl">VOC Index (SGP41)</span><strong id="aq-voc">98 (Normal)</strong></div>
            <div class="m-box"><span class="lbl">NOx Index (SGP41)</span><strong id="aq-nox">2 (Clean Air)</strong></div>
            <div class="m-box"><span class="lbl">True CO₂ (SCD41)</span><strong id="aq-co2">418 ppm</strong></div>
            <div class="m-box"><span class="lbl">Dominant Pollutant</span><strong id="aq-dom">PM2.5</strong></div>
          </div>
        </div>

        <div class="card">
          <div class="card-header">
            <h2>Franklin Lightning Detector (AS3935)</h2>
            <span class="badge" id="lt-badge">Clear · No Storm Front</span>
          </div>
          <div class="metric-grid">
            <div class="m-box"><span class="lbl">Storm Distance</span><strong id="lt-dist">-- km</strong></div>
            <div class="m-box"><span class="lbl">Strike Count</span><strong id="lt-count">0</strong></div>
            <div class="m-box"><span class="lbl">Last Strike Energy</span><strong id="lt-energy">0</strong></div>
            <div class="m-box"><span class="lbl">500 kHz Tank Tuning</span><strong class="ok-txt">Calibrated (Q=18)</strong></div>
          </div>
        </div>
      </div>
    </section>

    <!-- TAB 3: ULTRASONIC & RAIN DSP DIAGNOSTICS -->
    <section class="panel" id="tab-diagnostics">
      <div class="grid-2">
        <div class="card">
          <div class="card-header">
            <h2>40 kHz Bidirectional Ultrasonic ToF Matrix</h2>
            <span class="badge ok">CD4052 + TLV9062 + LM393</span>
          </div>
          <div class="metric-grid">
            <div class="m-box"><span class="lbl">North → South ToF</span><strong id="tof-ns">301.42 µs</strong></div>
            <div class="m-box"><span class="lbl">South → North ToF</span><strong id="tof-sn">304.18 µs</strong></div>
            <div class="m-box"><span class="lbl">East → West ToF</span><strong id="tof-ew">301.95 µs</strong></div>
            <div class="m-box"><span class="lbl">West → East ToF</span><strong id="tof-we">303.65 µs</strong></div>
            <div class="m-box"><span class="lbl">Speed of Sound (c)</span><strong id="tof-c">342.6 m/s</strong></div>
            <div class="m-box"><span class="lbl">Acoustic Virtual Temp</span><strong id="tof-tv">18.7 °C</strong></div>
          </div>
        </div>

        <div class="card">
          <div class="card-header">
            <h2>12–32 V Power &amp; Constant-Wattage De-Icing Heater</h2>
            <span class="badge" id="ht-state">Thermostatic Standby</span>
          </div>
          <div class="metric-grid">
            <div class="m-box"><span class="lbl">Roof Supply (VIN_ADC)</span><strong id="ht-vin">24.12 V</strong></div>
            <div class="m-box"><span class="lbl">Reflector Cone NTC</span><strong id="ht-temp">18.3 °C</strong></div>
            <div class="m-box"><span class="lbl">PWM Duty (AO3400A)</span><strong id="ht-duty">0.0 %</strong></div>
            <div class="m-box"><span class="lbl">Regulated Power</span><strong id="ht-power">0.00 W (Max 4.0 W)</strong></div>
          </div>
        </div>
      </div>
    </section>

    <!-- TAB 4: SETTINGS & OTA -->
    <section class="panel" id="tab-settings">
      <div class="grid-2">
        <div class="card">
          <div class="card-header"><h2>Station Calibration &amp; MQTT Home Assistant</h2></div>
          <div class="form-grid">
            <label>Station Altitude (m MSL)<input type="number" value="65" /></label>
            <label>Magnetic Declination (°)<input type="number" value="6.2" step="0.1" /></label>
            <label>MQTT Broker URI<input type="text" value="mqtt://192.168.1.10:1883" /></label>
            <label>HA Discovery Prefix<input type="text" value="homeassistant" /></label>
          </div>
          <button class="btn-primary">Save &amp; Republish HA Discovery</button>
        </div>
        <div class="card">
          <div class="card-header"><h2>Dual Wireless OTA Update</h2></div>
          <p class="muted">Upload <code>firmware.bin</code> (ESP32-C6 application) or <code>littlefs.bin</code> (Web UI assets).</p>
          <button class="btn-primary">Select .bin Image for OTA</button>
        </div>
      </div>
    </section>
  </main>
  <script src="/app.js"></script>
</body>
</html>
)WEBUI";
inline constexpr std::size_t index_html_size = sizeof(index_html_data) - 1;

inline constexpr const char app_css_data[] = R"WEBUI(:root {
  --bg: #0b111e;
  --card: #131c2e;
  --border: #22314d;
  --text: #e6edf7;
  --muted: #8b9bb4;
  --accent: #38bdf8;
  --ok: #22c55e;
  --warn: #f59e0b;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  background: var(--bg);
  color: var(--text);
  min-height: 100vh;
}
.topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 24px;
  background: #0f172a;
  border-bottom: 1px solid var(--border);
}
.brand { display: flex; align-items: center; gap: 12px; }
.logo-dot {
  width: 12px; height: 12px; border-radius: 50%;
  background: var(--accent);
  box-shadow: 0 0 12px var(--accent);
}
.brand h1 { font-size: 18px; font-weight: 700; letter-spacing: 0.3px; }
.subtitle { font-size: 12px; color: var(--muted); }
.tabs { display: flex; gap: 8px; }
.tab {
  background: transparent;
  color: var(--muted);
  border: 1px solid transparent;
  padding: 8px 14px;
  border-radius: 8px;
  cursor: pointer;
  font-weight: 600;
  font-size: 13px;
}
.tab.active {
  background: #1e293b;
  color: var(--accent);
  border-color: var(--border);
}
.status-pills { display: flex; gap: 8px; }
.pill {
  padding: 5px 10px;
  border-radius: 999px;
  font-size: 11px;
  font-weight: 700;
  background: #1e293b;
  border: 1px solid var(--border);
}
.pill.ok { color: var(--ok); border-color: rgba(34, 197, 94, 0.4); }
.container { padding: 22px 24px; max-width: 1440px; margin: 0 auto; }
.panel { display: none; }
.panel.active { display: block; }
.grid-3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 18px; }
.grid-2 { display: grid; grid-template-columns: repeat(2, 1fr); gap: 18px; }
.card {
  background: var(--card);
  border: 1px solid var(--border);
  border-radius: 14px;
  padding: 18px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}
.card-header { display: flex; justify-content: space-between; align-items: center; }
.card-header h2 { font-size: 15px; font-weight: 600; }
.badge {
  font-size: 11px; padding: 4px 9px; border-radius: 6px;
  background: #1e293b; color: var(--muted); font-weight: 600;
}
.badge.ok { color: var(--ok); background: rgba(34, 197, 94, 0.12); }
.badge.info { color: var(--accent); background: rgba(56, 189, 248, 0.14); }
.wind-rose-wrap { display: flex; justify-content: center; }
.wind-rose { width: 205px; height: 205px; }
.rose-ring { fill: #0f172a; stroke: #2b3d5f; stroke-width: 2; }
.rose-ring-inner { fill: none; stroke: #1e293b; stroke-dasharray: 4 4; }
.rose-axis { stroke: #1e293b; stroke-width: 1; }
.rose-dir { fill: var(--muted); font-size: 11px; font-weight: 700; text-anchor: middle; }
.rose-dir.north { fill: #ef4444; }
.arrow-head { fill: var(--accent); }
.arrow-shaft { stroke: var(--accent); stroke-width: 3.5; stroke-linecap: round; }
.rose-center { fill: #131c2e; stroke: var(--accent); stroke-width: 1.5; }
.rose-speed { fill: var(--text); font-size: 18px; font-weight: 800; text-anchor: middle; }
.rose-unit { fill: var(--muted); font-size: 8.5px; text-anchor: middle; }
.metric-row {
  display: grid; grid-template-columns: repeat(4, 1fr);
  background: #0f172a; padding: 10px; border-radius: 10px; text-align: center;
}
.lbl { display: block; font-size: 11px; color: var(--muted); margin-bottom: 3px; }
.accent { color: var(--accent); }
.hero-temp {
  display: flex; justify-content: space-between; align-items: center;
  background: #0f172a; padding: 14px 16px; border-radius: 12px;
}
.big-val { display: block; font-size: 28px; font-weight: 800; color: var(--text); }
.feels { font-size: 12px; color: var(--muted); }
.rh-box { text-align: right; }
.big-rh { font-size: 24px; font-weight: 700; color: var(--accent); }
.metric-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; }
.m-box {
  background: #0f172a; padding: 10px 12px; border-radius: 9px;
  border: 1px solid rgba(255, 255, 255, 0.03);
}
.m-box strong { font-size: 15px; }
.uv-bar-wrap { background: #0f172a; padding: 10px 12px; border-radius: 9px; }
.uv-header { display: flex; justify-content: space-between; font-size: 12px; margin-bottom: 6px; }
.uv-track { height: 7px; border-radius: 999px; background: #1e293b; overflow: hidden; }
.uv-fill { height: 100%; background: linear-gradient(90deg, #22c55e, #eab308, #ef4444); }
.sub-footer {
  display: flex; justify-content: space-between; font-size: 12px;
  color: var(--muted); padding-top: 4px; border-top: 1px solid var(--border);
}
.ok-txt { color: var(--ok); }
.form-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.form-grid label { display: flex; flex-direction: column; gap: 5px; font-size: 12px; color: var(--muted); }
.form-grid input {
  background: #0f172a; border: 1px solid var(--border); color: var(--text);
  padding: 8px 10px; border-radius: 7px;
}
.btn-primary {
  background: var(--accent); color: #081120; border: none;
  padding: 10px 16px; border-radius: 8px; font-weight: 700; cursor: pointer;
}
.muted { font-size: 13px; color: var(--muted); }
)WEBUI";
inline constexpr std::size_t app_css_size = sizeof(app_css_data) - 1;

inline constexpr const char app_js_data[] = R"WEBUI(document.querySelectorAll(".tab").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll(".tab").forEach((b) => b.classList.remove("active"));
    document.querySelectorAll(".panel").forEach((p) => p.classList.remove("active"));
    btn.classList.add("active");
    const target = document.getElementById("tab-" + btn.dataset.tab);
    if (target) target.classList.add("active");
  });
});

function applyTelemetry(d) {
  if (!d || !d.wind) return;
  document.getElementById("wind-speed").textContent = d.wind.speed_m_s.toFixed(1);
  document.getElementById("wind-arrow").setAttribute("transform", `rotate(${d.wind.dir_deg} 110 110)`);
  document.getElementById("wind-avg1m").textContent = `${d.wind.avg_1m_m_s.toFixed(1)} m/s`;
  document.getElementById("wind-avg10m").textContent = `${d.wind.avg_10m_m_s.toFixed(1)} m/s`;
  document.getElementById("wind-gust").textContent = `${d.wind.gust_m_s.toFixed(1)} m/s`;
  document.getElementById("wind-lull").textContent = `${d.wind.lull_m_s.toFixed(1)} m/s`;
  document.getElementById("env-temp").textContent = `${d.environment.temp_c.toFixed(1)} °C`;
  document.getElementById("env-rh").textContent = `${d.environment.humidity_rh.toFixed(1)} %`;
  document.getElementById("env-dew").textContent = `${d.environment.dew_point_c.toFixed(1)} °C`;
  document.getElementById("env-qnh").textContent = `${d.environment.qnh_hpa.toFixed(1)} hPa`;
  document.getElementById("rain-rate").textContent = `${d.rain.rate_mm_h.toFixed(1)} mm/h`;
  document.getElementById("rain-daily").textContent = `${d.rain.daily_mm.toFixed(1)} mm`;
  document.getElementById("pill-vin").textContent = `${d.heater.vin_v.toFixed(1)} V`;
}

function connectWs() {
  if (location.protocol === "file:") return;
  try {
    const proto = location.protocol === "https:" ? "wss:" : "ws:";
    const ws = new WebSocket(`${proto}//${location.host}/ws`);
    ws.onmessage = (ev) => {
      try { applyTelemetry(JSON.parse(ev.data)); } catch (_) {}
    };
    ws.onclose = () => setTimeout(connectWs, 3000);
  } catch (_) {}
}
connectWs();
)WEBUI";
inline constexpr std::size_t app_js_size = sizeof(app_js_data) - 1;

}
// clang-format on
