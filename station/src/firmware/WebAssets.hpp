#pragma once
// clang-format off

#include <cstddef>

namespace station {
namespace firmware {
namespace web_assets {

static constexpr const char index_html_data[] = R"WEBUI(<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>AeroMet V1 — Weather Observatory</title>
  <link rel="stylesheet" href="/app.css" />
</head>
<body>
  <!-- COMPACT TOP BAR -->
  <header class="topbar">
    <div class="brand">
      <div class="logo-mark" aria-hidden="true">
        <svg viewBox="0 0 32 32" width="24" height="24">
          <circle cx="16" cy="16" r="12" fill="none" stroke="currentColor" stroke-width="2.2" stroke-dasharray="4 2" />
          <circle cx="16" cy="16" r="3.5" fill="currentColor" />
          <path d="M16 2 L19 10 L16 8 L13 10 Z" fill="#ef4444" />
        </svg>
      </div>
      <span class="brand-title">AeroMet</span>
      <span class="pill ok" id="pill-link"><span class="pulse-dot"></span>LIVE</span>
    </div>

    <nav class="tabs" role="tablist">
      <button class="tab active" data-tab="overview">Now</button>
      <button class="tab" data-tab="trends">Trends</button>
      <button class="tab" data-tab="twin">Station Twin</button>
      <button class="tab" data-tab="settings">Setup</button>
    </nav>

    <div class="toolbar">
      <select id="scenario-select" class="scenario-select" title="Live Weather Scenario">
        <option value="breeze">☀ Spring Breeze</option>
        <option value="storm">⛈ Squall &amp; Hail</option>
        <option value="freeze">❄ Freezing Fog (-4 °C)</option>
        <option value="heatwave">🔥 Heatwave (UV 10)</option>
        <option value="plume">🌫 Urban Plume</option>
      </select>
      <button class="btn-chip" id="unit-toggle" title="Switch Units">Metric</button>
      <button class="btn-chip" id="theme-toggle" title="Switch Theme">☀</button>
    </div>
  </header>

  <main class="container">
    <!-- =========================================================
         TAB 1: GLANCEABLE VISUAL INSTRUMENT DASHBOARD
         ========================================================= -->
    <section class="panel active" id="tab-overview">
      <!-- VISUAL SKY HERO BANNER -->
      <div class="sky-hero" id="sky-hero">
        <svg class="sky-fx" id="sky-fx" viewBox="0 0 900 130" preserveAspectRatio="none"></svg>
        <div class="hero-left">
          <div class="hero-wx-icon" id="hero-wx-icon"></div>
          <div>
            <div class="hero-temp-big" id="hero-temp">18.4°</div>
            <div class="hero-cond" id="hero-cond">Fine Weather · Feels like 17.8°</div>
          </div>
        </div>
        <div class="hero-Chips">
          <div class="h-chip"><span class="h-dot wind-dot"></span><span id="hc-wind">4.8 m/s SW</span></div>
          <div class="h-chip"><span class="h-dot baro-dot"></span><span id="hc-baro">1016.8 hPa →</span></div>
          <div class="h-chip"><span class="h-dot rain-dot"></span><span id="hc-rain">Dry · 2.4 mm</span></div>
          <div class="h-chip"><span class="h-dot aqi-dot" id="hc-aqi-dot"></span><span id="hc-aqi">Air: Good (26)</span></div>
        </div>
      </div>

      <!-- 6 VISUAL INSTRUMENT TILES (CLICK ANY TILE TO INSPECT DETAILS) -->
      <div class="grid-3">
        <!-- 1. WIND COMPASS & LABELED SPEED RANGE BAR -->
        <article class="card clickable" data-inspect="wind">
          <div class="card-header">
            <span class="card-title"><span class="i-icon">🧭</span>WIND</span>
            <span class="badge info" id="wind-beaufort">Gentle Breeze</span>
          </div>

          <div class="gauge-center">
            <svg viewBox="0 0 230 230" class="wind-compass">
              <!-- Clean 360° compass ring -->
              <circle cx="115" cy="115" r="98" class="compass-track" />
              <circle cx="115" cy="115" r="72" class="compass-inner" />
              <!-- 36 degree tick marks -->
              <g class="compass-ticks" id="compass-ticks"></g>
              <!-- 16-sector wind direction history petals -->
              <g id="rose-petals"></g>
              <!-- Cardinal labels -->
              <text x="115" y="13" class="c-dir north">N</text>
              <text x="221" y="119" class="c-dir">E</text>
              <text x="115" y="226" class="c-dir">S</text>
              <text x="9" y="119" class="c-dir">W</text>
              <!-- Live wind vector needle -->
              <g id="wind-arrow" transform="rotate(215 115 115)">
                <polygon points="115,19 106,47 115,40 124,47" class="arrow-head" />
                <line x1="115" y1="39" x2="115" y2="162" class="arrow-shaft" />
                <circle cx="115" cy="162" r="4" class="arrow-tail" />
              </g>
              <circle cx="115" cy="115" r="36" class="compass-hub" />
              <text x="115" y="113" class="hub-val" id="wind-speed">4.8</text>
              <text x="115" y="128" class="hub-unit" id="wind-unit-dir">m/s · SW</text>
            </svg>
          </div>

          <!-- Self-explaining mini legend for the compass -->
          <div class="compass-key">
            <span><i class="key-arrow"></i>Live Wind Direction</span>
            <span><i class="key-petal"></i>Past 1h Direction Spread</span>
          </div>

          <!-- Clearly Labeled Min — 1m Avg — Peak Gust Speed Bar -->
          <div class="range-bar-wrap">
            <div class="range-labels">
              <span class="rl-item"><i class="dot-min"></i>Min <b id="wind-lull">2.9</b></span>
              <span class="rl-item"><i class="dot-avg"></i>1m Avg <b id="wind-avg1m">4.4</b></span>
              <span class="rl-item"><i class="dot-gust"></i>Peak Gust <b class="warn-txt" id="wind-gust">7.2 m/s</b></span>
            </div>
            <div class="range-track">
              <div class="range-span" id="wind-range-span" style="left:14%;width:22%"></div>
              <div class="range-dot" id="wind-avg-dot" style="left:22%" title="1-Minute Average Speed"></div>
              <div class="range-gust-pin" id="wind-gust-pin" style="left:36%" title="3-Second Peak Gust"></div>
            </div>
          </div>
        </article>

        <!-- 2. TEMPERATURE & HUMIDITY (PLAIN-ENGLISH, LABELED PINS) -->
        <article class="card clickable" data-inspect="temp">
          <div class="card-header">
            <span class="card-title"><span class="i-icon">🌡</span>TEMPERATURE &amp; HUMIDITY</span>
            <span class="badge ok" id="comfort-badge">Comfortable</span>
          </div>

          <div class="thermo-visual">
            <div class="thermo-readouts">
              <div>
                <span class="big-num" id="env-temp">18.4°</span>
                <span class="sub-lbl" id="env-feels">Feels like 17.8° outside</span>
              </div>
              <!-- Radial Humidity Ring -->
              <div class="rh-ring-wrap">
                <svg viewBox="0 0 88 88" class="rh-ring-svg">
                  <circle cx="44" cy="44" r="35" class="ring-bg" />
                  <circle cx="44" cy="44" r="35" class="ring-fg-cyan" id="rh-arc" stroke-dasharray="137 220" transform="rotate(-90 44 44)" />
                  <text x="44" y="43" class="ring-val" id="env-rh">62%</text>
                  <text x="44" y="56" class="ring-sub">Humidity</text>
                </svg>
              </div>
            </div>

            <!-- Thermometer Scale with Color-Matched Pin Key -->
            <div class="spectrum-box">
              <div class="pin-legend">
                <span title="Temperature where moisture condenses into dew or fog"><i class="p-dot blue"></i>Dew Point <b id="env-dew">11.1°</b></span>
                <span title="Measured air temperature"><i class="p-dot white"></i>Actual <b id="env-actual-sm">18.4°</b></span>
                <span title="What it feels like on skin with wind & sun"><i class="p-dot amber"></i>Feels <b id="env-feels-sm">17.8°</b></span>
              </div>
              <div class="temp-spectrum-track">
                <div class="dew-spread-bridge" id="dew-bridge" style="left:47%;width:13%"></div>
                <div class="spec-pin dew-pin" id="pin-dew" style="left:47%"></div>
                <div class="spec-pin temp-pin" id="pin-temp" style="left:60%"></div>
                <div class="spec-pin feels-pin" id="pin-feels" style="left:59%"></div>
              </div>
              <div class="spectrum-scale">
                <span>-15° Cold</span><span>0° Freeze</span><span>15° Mild</span><span>25° Warm</span><span>40° Hot</span>
              </div>
            </div>
          </div>

          <!-- 2 Plain-English Human Indicators (No VPD/LCL/WBGT Jargon) -->
          <div class="mini-bars-2">
            <div class="mbar">
              <div class="mbar-top"><span>Fog / Dew Risk</span><b id="fog-risk-lbl">Low (7.3° gap)</b></div>
              <div class="mbar-track"><div class="mbar-fill cyan" id="lcl-fill" style="width:28%"></div></div>
            </div>
            <div class="mbar">
              <div class="mbar-top"><span>Heat / Cold Stress</span><b id="stress-lbl">None</b></div>
              <div class="mbar-track"><div class="mbar-fill green" id="vpd-fill" style="width:22%"></div></div>
            </div>
          </div>
        </article>

        <!-- 3. BAROMETER (RATE OF CHANGE IN HPA/H + ARROW-DOWN / ARROW-UP 24H EXTREMES) -->
        <article class="card clickable" data-inspect="baro">
          <div class="card-header">
            <span class="card-title"><span class="i-icon">⏲</span>BAROMETER &amp; FORECAST</span>
            <span class="badge info" id="baro-trend-badge">Change: +0.1 hPa/h</span>
          </div>

          <!-- Calm, Neutral Factual Barometric Forecast Row (No Alert Colors) -->
          <div class="forecast-fact-row" id="baro-forecast-banner">
            <span class="fb-kicker">Forecast</span>
            <strong class="fb-text" id="env-zambretti">Fine Weather</strong>
          </div>

          <div class="baro-dial-wrap">
            <svg viewBox="0 0 240 124" class="baro-dial">
              <path d="M 24 110 A 96 96 0 0 1 216 110" fill="none" stroke="var(--border)" stroke-width="11" stroke-linecap="round" />
              <path d="M 24 110 A 96 96 0 0 1 72 27" fill="none" stroke="#f59e0b" stroke-width="5" stroke-opacity="0.5" />
              <path d="M 72 27 A 96 96 0 0 1 168 27" fill="none" stroke="#38bdf8" stroke-width="5" stroke-opacity="0.6" />
              <path d="M 168 27 A 96 96 0 0 1 216 110" fill="none" stroke="#22c55e" stroke-width="5" stroke-opacity="0.6" />
              <!-- 3h Ago Ghost Needle -->
              <g id="baro-ghost-needle" transform="rotate(9 120 110)">
                <line x1="120" y1="22" x2="120" y2="62" stroke="#94a3b8" stroke-width="2.2" stroke-dasharray="3 3" />
              </g>
              <!-- Current Pressure Needle -->
              <g id="baro-needle" transform="rotate(11 120 110)">
                <polygon points="120,14 114,32 126,32" fill="var(--accent)" />
                <line x1="120" y1="28" x2="120" y2="110" stroke="var(--accent)" stroke-width="3" stroke-linecap="round" />
              </g>
              <circle cx="120" cy="110" r="5.5" fill="var(--accent)" />
              <text x="20" y="122" class="dial-lbl">980 Rain</text>
              <text x="120" y="12" class="dial-lbl" text-anchor="middle">1010</text>
              <text x="220" y="122" class="dial-lbl" text-anchor="end">Fair 1040</text>
              <text x="120" y="84" class="baro-big" id="env-qnh">1016.8</text>
              <text x="120" y="98" class="baro-unit" id="env-qnh-unit">hPa (Sea-Level)</text>
            </svg>
          </div>

          <div class="compass-key">
            <span><i class="key-solid-line"></i>Now</span>
            <span><i class="key-dash-line"></i>3 Hours Ago (<b id="baro-3h-ago">1016.6</b>)</span>
          </div>

          <!-- 24h Sea-Level Pressure Chart with ↓ / ↑ Extremes -->
          <div class="spark-clean">
            <div class="spark-foot">
              <span>24h Pressure</span>
              <span id="baro-minmax">↓ 1015.4 · ↑ 1017.2 hPa</span>
            </div>
            <svg viewBox="0 0 320 48" class="spark-svg" id="spark-baro"></svg>
          </div>
        </article>

        <!-- 4. PRECIPITATION (AUTO-SCALING Y-AXIS + THIS SHOWER IN TOP RIGHT) -->
        <article class="card clickable" data-inspect="rain">
          <div class="card-header">
            <span class="card-title">PRECIPITATION</span>
            <span class="badge info" id="rain-state">This Shower: 0.0 mm</span>
          </div>

          <div class="rain-visual-row">
            <!-- Vertical Rain Collector Tube -->
            <div class="rain-tube-wrap">
              <div class="rain-tube">
                <div class="rain-water" id="rain-water" style="height:18%"></div>
                <div class="rain-ticks"><span></span><span></span><span></span><span></span></div>
              </div>
              <span class="tube-lbl" id="rain-daily">2.4 mm</span>
              <span class="tube-sub">Today Total</span>
            </div>

            <div class="rain-right">
              <div class="rain-hero-row">
                <div class="rain-hero-num">
                  <span class="big-num" id="rain-rate">0.0</span>
                  <span class="sub-lbl" id="rain-rate-unit">mm/h · Dry</span>
                </div>
                <div class="phase-legend" title="Stacked liquid-equivalent precipitation rate (mm/h)">
                  <span><i class="ph-dot rain-ph"></i>Rain</span>
                  <span><i class="ph-dot hail-ph"></i>Hail</span>
                </div>
              </div>
              <svg viewBox="0 0 320 76" class="spark-svg rain-chart-svg" id="spark-rain"></svg>
              <!-- Visual Hailstone Size Disdrometer Strip (Proportional Circles) -->
              <div class="hail-strip">
                <div class="hail-info">
                  <span class="hail-kicker">Hailstone Size</span>
                  <strong id="rain-hail">None (0 hits)</strong>
                </div>
                <div class="hail-scale" id="hail-scale">
                  <div class="hs-item" data-mm="5"><span class="hs-ball s5"></span><small>5mm</small></div>
                  <div class="hs-item" data-mm="10"><span class="hs-ball s10"></span><small>10mm</small></div>
                  <div class="hs-item" data-mm="15"><span class="hs-ball s15"></span><small>15mm</small></div>
                  <div class="hs-item" data-mm="20"><span class="hs-ball s20"></span><small>20+</small></div>
                </div>
              </div>
            </div>
          </div>
        </article>

        <!-- 5. SOLAR, UV & CLOUD COVER (VECTOR HORIZON ICONS + SOLAR/UV CLOUD CORRELATION) -->
        <article class="card clickable" data-inspect="solar">
          <div class="card-header">
            <span class="card-title"><span class="i-icon">☀</span>SUN, UV &amp; CLOUDS</span>
            <span class="badge info" id="daylight-badge">Daylight 11h 36m</span>
          </div>

          <!-- Sun Horizon Arc with Vector Sunrise & Sunset Icons -->
          <div class="sun-arc-wrap">
            <svg viewBox="0 0 260 112" class="sun-arc-svg">
              <defs>
                <radialGradient id="sunOrbGlow" cx="50%" cy="50%" r="50%">
                  <stop offset="0%" stop-color="#facc15" stop-opacity="0.75" />
                  <stop offset="100%" stop-color="#facc15" stop-opacity="0" />
                </radialGradient>
              </defs>
              <line x1="18" y1="82" x2="242" y2="82" stroke="var(--border)" stroke-width="1.5" stroke-dasharray="4 3" />
              <path d="M 28 82 Q 130 -16 232 82" fill="none" stroke="#f59e0b" stroke-width="2.2" stroke-opacity="0.7" />
              <circle cx="28" cy="82" r="3" fill="#f59e0b" />
              <circle cx="232" cy="82" r="3" fill="#f97316" />
              <g id="sun-orb" transform="translate(145, 25)">
                <circle cx="0" cy="0" r="18" fill="url(#sunOrbGlow)" />
                <circle cx="0" cy="0" r="7" fill="#facc15" />
              </g>
              <text x="130" y="58" class="sun-val" id="env-solar">642 W/m²</text>
              <text x="130" y="72" class="sun-sub" id="env-lux">Brightness: 81,250 lx</text>
              <!-- Clean Vector Sunrise Icon + Time -->
              <g transform="translate(14, 91)">
                <line x1="0" y1="10" x2="14" y2="10" stroke="#f59e0b" stroke-width="1.5" />
                <path d="M 3 10 A 4 4 0 0 1 11 10" fill="none" stroke="#facc15" stroke-width="1.5" />
                <polyline points="5,4 7,1.5 9,4" fill="none" stroke="#facc15" stroke-width="1.3" />
                <text x="18" y="10" class="horizon-time" id="sun-rise">06:48</text>
              </g>
              <!-- Clean Vector Sunset Icon + Time -->
              <g transform="translate(196, 91)">
                <line x1="0" y1="10" x2="14" y2="10" stroke="#f97316" stroke-width="1.5" />
                <path d="M 3 10 A 4 4 0 0 1 11 10" fill="none" stroke="#f97316" stroke-width="1.5" />
                <polyline points="5,1.5 7,4 9,1.5" fill="none" stroke="#f97316" stroke-width="1.3" />
                <text x="18" y="10" class="horizon-time" id="sun-set">18:24</text>
              </g>
            </svg>
          </div>

          <!-- Single Authoritative UV Index Rainbow Bar -->
          <div class="uv-visual">
            <div class="uv-top">
              <span>UV Index <b class="accent" id="env-uv">5.4 · Moderate</b></span>
              <span class="muted-xs" id="uv-advice">Sunscreen advised</span>
            </div>
            <div class="uv-rainbow-bar">
              <div class="uv-dot" id="uv-dot" style="left:47%"></div>
            </div>
          </div>

          <!-- Cloud Cover -->
          <div class="mbar">
            <div class="mbar-top">
              <span>Cloud Cover</span>
              <b id="cloud-cover-lbl">24% · Scattered</b>
            </div>
            <div class="mbar-track"><div class="mbar-fill cyan" id="cloud-cover-fill" style="width:24%"></div></div>
          </div>
        </article>

        <!-- 6. AIR QUALITY & CALM LIGHTNING TIMELINE (NO ALERT BORDERS, SHOWS BOLT POWER CLASS) -->
        <article class="card clickable" data-inspect="air">
          <div class="card-header">
            <span class="card-title"><span class="i-icon">🍃</span>AIR &amp; LIGHTNING</span>
            <span class="badge info" id="aq-dom-badge">Main: PM2.5</span>
          </div>

          <!-- Top Half: AQI Ring + 3 Pollutant Bars -->
          <div class="air-visual-row">
            <div class="aqi-radar-stack">
              <svg viewBox="0 0 92 92" class="aqi-ring-svg">
                <circle cx="46" cy="46" r="38" class="ring-bg" />
                <circle cx="46" cy="46" r="38" class="ring-fg-ok" id="aqi-arc" stroke-dasharray="40 239" transform="rotate(-90 46 46)" />
                <text x="46" y="44" class="aqi-big" id="aq-aqi-big">26</text>
                <text x="46" y="58" class="aqi-sub" id="aq-level-lbl">GOOD</text>
              </svg>
            </div>

            <div class="bullet-stack">
              <div class="b-row">
                <div class="b-hdr"><span>Fine Dust (PM2.5)</span><b id="aq-pm25">6.2 µg/m³</b></div>
                <div class="b-track"><div class="b-limit" style="left:42%"></div><div class="b-fill ok" id="b-pm25" style="width:18%"></div></div>
              </div>
              <div class="b-row">
                <div class="b-hdr"><span>Coarse Dust (PM10)</span><b id="aq-pm10">12.8 µg/m³</b></div>
                <div class="b-track"><div class="b-limit" style="left:50%"></div><div class="b-fill ok" id="b-pm10" style="width:14%"></div></div>
              </div>
              <div class="b-row">
                <div class="b-hdr"><span>CO₂ / VOC</span><b id="aq-co2">419 ppm · VOC 98</b></div>
                <div class="b-track"><div class="b-fill cyan" id="b-co2" style="width:24%"></div></div>
              </div>
            </div>
          </div>

          <!-- Bottom Half: Calm Neutral Lightning Timeline with Counts Over Bars -->
          <div class="lightning-panel" id="lightning-panel">
            <div class="lt-hdr">
              <span class="lt-title">⚡ Lightning</span>
              <strong class="lt-status" id="lt-dist">0 strikes · &gt;40 km</strong>
            </div>
            <svg viewBox="0 0 380 64" class="lt-timeline-svg" id="lt-timeline-svg"></svg>
          </div>
        </article>
      </div>
    </section>

    <!-- =========================================================
         TAB 2: VISUAL TRENDS (LAST HOUR / 24H) & POLAR WIND ROSE
         ========================================================= -->
    <section class="panel" id="tab-trends">
      <div class="trends-bar">
        <div class="range-btns">
          <button class="btn-chip active" data-trange="1h">Last Hour</button>
          <button class="btn-chip" data-trange="24h">Last 24 Hours</button>
        </div>
        <div class="trends-kpis">
          <span class="t-kpi">Last Hour Rain: <b id="trend-rain-1h">0.4 mm</b></span>
          <span class="t-kpi">Today Rain: <b id="trend-rain-day">2.4 mm</b></span>
          <span class="t-kpi">This Month: <b id="trend-rain-month">145.2 mm</b></span>
        </div>
      </div>

      <div class="grid-2">
        <div class="card">
          <div class="card-header">
            <span class="card-title" id="chart-temp-title">Temperature, Dew Point &amp; Feels-Like (Last Hour)</span>
            <div class="legend-pills">
              <span class="l-dot cyan">Temp</span>
              <span class="l-dot green">Dew</span>
              <span class="l-dot amber">Feels</span>
            </div>
          </div>
          <svg viewBox="0 0 560 175" class="chart-svg" id="chart-temp"></svg>
        </div>

        <div class="card">
          <div class="card-header">
            <span class="card-title" id="chart-wind-title">Wind Speed &amp; Peak 3s Gusts (Last Hour)</span>
            <div class="legend-pills">
              <span class="l-dot amber">3s Gust</span>
              <span class="l-dot cyan">10m Avg</span>
            </div>
          </div>
          <svg viewBox="0 0 560 175" class="chart-svg" id="chart-wind"></svg>
        </div>

        <div class="card">
          <div class="card-header">
            <span class="card-title" id="chart-baro-title">Sea-Level Pressure &amp; Rain Rate (Last Hour)</span>
            <span class="badge info" id="rose-dom-sector">Dominant: SW</span>
          </div>
          <svg viewBox="0 0 560 175" class="chart-svg" id="chart-baro-rain"></svg>
        </div>

        <div class="card">
          <div class="card-header">
            <span class="card-title">🧭 16-Sector Wind Direction Distribution</span>
            <span class="badge">Frequency %</span>
          </div>
          <div class="sector-bars" id="sector-bars-grid"></div>
        </div>
      </div>
    </section>

    <!-- =========================================================
         TAB 3: VISUAL STATION DIGITAL TWIN & PHYSICS CORRECTIONS
         ========================================================= -->
    <section class="panel" id="tab-twin">
      <div class="grid-2">
        <div class="card">
          <div class="card-header">
            <span class="card-title">🛰 AeroMet V1 Live Hardware Digital Twin</span>
            <span class="badge ok" id="twin-vin-badge">24.1 V · Heater Standby</span>
          </div>
          <div class="twin-wrap">
            <svg viewBox="0 0 380 310" class="twin-svg">
              <path d="M 162 34 A 28 28 0 0 1 218 34 Z" fill="rgba(56,189,248,0.18)" stroke="#38bdf8" stroke-width="1.8" />
              <rect x="172" y="32" width="36" height="5" rx="2" fill="#22c55e" />
              <text x="234" y="28" class="twin-lbl">Dome PCB (UV / Lux)</text>
              <text x="234" y="40" class="twin-val" id="tw-dome">UV 5.4 · 642 W/m²</text>

              <rect x="115" y="38" width="150" height="36" rx="8" fill="var(--card-inner)" stroke="var(--border)" stroke-width="2" />
              <rect x="142" y="50" width="96" height="6" rx="2" fill="#22c55e" />
              <polygon points="128,68 140,62 146,74 134,80" fill="#38bdf8" />
              <polygon points="252,68 240,62 234,74 246,80" fill="#38bdf8" />
              <polyline points="139,76 190,136 241,76" fill="none" stroke="#38bdf8" stroke-width="2" stroke-dasharray="4 3" />
              <text x="274" y="54" class="twin-lbl">Crown (4× Piezo + 4× US)</text>
              <text x="274" y="66" class="twin-val" id="tw-crown">c = 342.8 m/s</text>

              <rect x="122" y="74" width="8" height="64" fill="var(--border)" />
              <rect x="250" y="74" width="8" height="64" fill="var(--border)" />

              <polygon points="115,152 190,136 265,152" fill="var(--card-inner)" stroke="#f59e0b" stroke-width="2" />
              <rect x="164" y="152" width="52" height="5" rx="2" fill="#f59e0b" />
              <text x="274" y="148" class="twin-lbl">Heated Reflector (25 Ω)</text>
              <text x="274" y="160" class="twin-val" id="tw-heater">18.3 °C · 0.0 W</text>

              <rect x="120" y="164" width="140" height="64" rx="6" fill="var(--card-inner)" stroke="var(--border)" stroke-dasharray="6 3" stroke-width="1.8" />
              <rect x="184" y="168" width="12" height="56" rx="2" fill="#22c55e" />
              <text x="274" y="192" class="twin-lbl">Probe Stem (SHT45/SCD41)</text>
              <text x="274" y="204" class="twin-val" id="tw-probe">18.4 °C · 419 ppm CO₂</text>

              <rect x="115" y="232" width="150" height="62" rx="8" fill="var(--card-inner)" stroke="var(--border)" stroke-width="2" />
              <rect x="136" y="252" width="108" height="6" rx="2" fill="#22c55e" />
              <rect x="180" y="278" width="20" height="16" fill="#38bdf8" opacity="0.35" />
              <text x="274" y="256" class="twin-lbl">Main (ESP32-C6 + RS-485)</text>
              <text x="274" y="268" class="twin-val" id="tw-main">-56 dBm · 24.1 V</text>
            </svg>
          </div>
        </div>

        <div class="card">
          <div class="card-header">
            <span class="card-title">⚡ Live Physical Error Corrections (Raw → True)</span>
            <span class="badge info">6 Active Loops</span>
          </div>

          <div class="corr-visual-list">
            <div class="corr-item">
              <div class="corr-top">
                <span>☀ Solar Radiation Shield Heating (ΔT<sub>rad</sub>)</span>
                <b class="accent" id="cv-solar">-0.28 °C</b>
              </div>
              <div class="corr-bar-bg"><div class="corr-bar-fg amber" id="cb-solar" style="width:14%"></div></div>
            </div>

            <div class="corr-item">
              <div class="corr-top">
                <span>💨 Bernoulli Wind Suction Baro Comp (ΔP<sub>dyn</sub>)</span>
                <b class="accent" id="cv-bern">+0.04 hPa</b>
              </div>
              <div class="corr-bar-bg"><div class="corr-bar-fg cyan" id="cb-bern" style="width:8%"></div></div>
            </div>

            <div class="corr-item">
              <div class="corr-top">
                <span>🌫 κ-Köhler Fog Hygroscopic PM Strip</span>
                <b class="accent" id="cv-fog">1.00× (Dry)</b>
              </div>
              <div class="corr-bar-bg"><div class="corr-bar-fg green" id="cb-fog" style="width:100%"></div></div>
            </div>

            <div class="corr-item">
              <div class="corr-top">
                <span>🔊 Ultrasonic Crosswind Mach² &amp; Moist T<sub>v</sub></span>
                <b class="accent" id="cv-tof">314.8 / 319.1 µs</b>
              </div>
              <div class="corr-bar-bg"><div class="corr-bar-fg cyan" id="cb-tof" style="width:52%"></div></div>
            </div>

            <div class="corr-item">
              <div class="corr-top">
                <span>🔥 12–32 V Constant-Wattage De-Icing PWM</span>
                <b class="accent" id="cv-heater">0.0 % (0.0 W / 4.0 W)</b>
              </div>
              <div class="corr-bar-bg"><div class="corr-bar-fg amber" id="cb-heater" style="width:2%"></div></div>
            </div>
          </div>

          <div class="code-box" id="rs485-frame">$AM,1231200,4.80,215.0,0.00,2.40,18.40,62.5,1016.80,5.4,26*4F2A</div>
        </div>
      </div>
    </section>

    <!-- =========================================================
         TAB 4: CALIBRATION, MAINTENANCE & OTA
         ========================================================= -->
    <section class="panel" id="tab-settings">
      <div class="grid-2">
        <div class="card">
          <div class="card-header">
            <span class="card-title">⚙ Calibration &amp; Home Assistant MQTT</span>
            <span class="badge ok" id="cfg-save-status">NVS Synced</span>
          </div>
          <div class="form-grid">
            <label>Elevation (m MSL)<input type="number" id="cfg-alt" value="65.0" step="0.5" /></label>
            <label>Mag Declination (°)<input type="number" id="cfg-decl" value="6.2" step="0.1" /></label>
            <label>Zero-Wind Δt<sub>NS</sub> (µs)<input type="number" id="cfg-dt-ns" value="0.00" step="0.01" /></label>
            <label>Zero-Wind Δt<sub>EW</sub> (µs)<input type="number" id="cfg-dt-ew" value="0.00" step="0.01" /></label>
            <label>MQTT Broker<input type="text" id="cfg-mqtt" value="mqtt://192.168.1.10:1883" /></label>
            <label>HA Prefix<input type="text" id="cfg-ha" value="homeassistant" /></label>
          </div>
          <div class="btn-row">
            <button class="btn-primary" id="btn-save-cfg">Save Calibration</button>
            <button class="btn-secondary" id="btn-zero-wind">Zero-Wind Trim</button>
          </div>
        </div>

        <div class="card">
          <div class="card-header">
            <span class="card-title">🚀 Maintenance &amp; Wireless OTA</span>
            <span class="badge info" id="hdr-fw">fw 26.10.19</span>
          </div>
          <div class="btn-row">
            <button class="btn-secondary" id="btn-reset-rain">Reset Daily Rain</button>
            <button class="btn-secondary" id="btn-bakeout">SHT45 Bake-Out</button>
            <button class="btn-secondary" id="btn-heat-test">Toggle 4W Heater</button>
            <button class="btn-danger" id="btn-reboot">Reboot</button>
          </div>
          <div class="ota-box">
            <div class="btn-row">
              <input type="file" id="ota-file" accept=".bin" />
              <button class="btn-primary" id="btn-ota-upload">Flash OTA (.bin)</button>
            </div>
            <span class="muted-xs" id="ota-status">Partition ota_0 · LittleFS mounted</span>
          </div>
        </div>
      </div>
    </section>
  </main>

  <!-- PROGRESSIVE DISCLOSURE DRAWER MODAL (OPENS WHEN ANY CARD IS CLICKED) -->
  <div class="modal-backdrop" id="inspect-modal" style="display:none">
    <div class="modal-card">
      <div class="card-header">
        <span class="card-title" id="modal-title">Sensor Detail</span>
        <button class="btn-chip" id="modal-close">✕ Close</button>
      </div>
      <div class="modal-body" id="modal-body"></div>
    </div>
  </div>

  <script src="/app.js"></script>
</body>
</html>
)WEBUI";
static constexpr std::size_t index_html_size = sizeof(index_html_data) - 1;

static constexpr const char app_css_data[] = R"WEBUI(:root {
  --bg: #060b16;
  --bg-elev: #0c1426;
  --card: #101b31;
  --card-inner: #091120;
  --border: #1e2f50;
  --text: #f1f5f9;
  --muted: #8ca0c4;
  --accent: #38bdf8;
  --accent-soft: rgba(56, 189, 248, 0.14);
  --ok: #22c55e;
  --ok-soft: rgba(34, 197, 94, 0.15);
  --warn: #f59e0b;
  --warn-soft: rgba(245, 158, 11, 0.16);
  --danger: #ef4444;
  --danger-soft: rgba(239, 68, 68, 0.16);
  --shadow: 0 12px 28px rgba(0, 0, 0, 0.35);
}

html[data-theme="light"] {
  --bg: #eef2f7;
  --bg-elev: #ffffff;
  --card: #ffffff;
  --card-inner: #f1f5f9;
  --border: #cbd5e1;
  --text: #0f172a;
  --muted: #475569;
  --accent: #0284c7;
  --accent-soft: rgba(2, 132, 199, 0.12);
  --ok: #16a34a;
  --ok-soft: rgba(22, 163, 74, 0.12);
  --warn: #d97706;
  --warn-soft: rgba(217, 119, 6, 0.14);
  --danger: #dc2626;
  --danger-soft: rgba(220, 38, 38, 0.14);
  --shadow: 0 6px 18px rgba(15, 23, 42, 0.06);
}

* { box-sizing: border-box; margin: 0; padding: 0; }

body {
  font-family: -apple-system, BlinkMacSystemFont, "Inter", "Segoe UI", Roboto, sans-serif;
  background: var(--bg);
  color: var(--text);
  min-height: 100vh;
}

/* TOPBAR */
.topbar {
  display: flex; align-items: center; justify-content: space-between;
  padding: 10px 22px; background: var(--bg-elev);
  border-bottom: 1px solid var(--border); gap: 12px; flex-wrap: wrap;
}
.brand { display: flex; align-items: center; gap: 10px; }
.logo-mark {
  width: 32px; height: 32px; border-radius: 9px;
  background: var(--accent-soft); color: var(--accent);
  display: flex; align-items: center; justify-content: center;
}
.brand-title { font-size: 16px; font-weight: 800; letter-spacing: 0.3px; }
.tabs { display: flex; gap: 4px; background: var(--card-inner); padding: 3px; border-radius: 10px; border: 1px solid var(--border); }
.tab {
  background: transparent; color: var(--muted); border: none;
  padding: 6px 14px; border-radius: 7px; cursor: pointer;
  font-weight: 700; font-size: 12.5px; transition: all 0.15s;
}
.tab.active { background: var(--card); color: var(--accent); box-shadow: 0 2px 6px rgba(0,0,0,0.2); }
.toolbar { display: flex; align-items: center; gap: 8px; }
.scenario-select {
  background: var(--card-inner); color: var(--text); border: 1px solid var(--border);
  padding: 5px 10px; border-radius: 8px; font-weight: 700; font-size: 12px; cursor: pointer;
}
.btn-chip {
  background: var(--card-inner); color: var(--text); border: 1px solid var(--border);
  padding: 5px 11px; border-radius: 8px; font-size: 12px; font-weight: 700; cursor: pointer;
}
.pill {
  display: inline-flex; align-items: center; gap: 5px;
  padding: 3px 9px; border-radius: 99px; font-size: 10.5px; font-weight: 800;
  background: var(--ok-soft); color: var(--ok);
}
.pulse-dot { width: 6px; height: 6px; border-radius: 50%; background: var(--ok); box-shadow: 0 0 8px var(--ok); }

/* CONTAINER & VISUAL SKY HERO */
.container { max-width: 1420px; margin: 0 auto; padding: 16px 22px 28px; }
.panel { display: none; }
.panel.active { display: block; }

.sky-hero {
  position: relative; overflow: hidden;
  border-radius: 16px; padding: 18px 24px; margin-bottom: 16px;
  background: linear-gradient(125deg, #0c2348 0%, #103766 55%, #0f1b33 100%);
  border: 1px solid var(--border);
  display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 16px;
  color: #f8fafc;
}
.sky-fx { position: absolute; inset: 0; width: 100%; height: 100%; pointer-events: none; opacity: 0.45; }
.hero-left { position: relative; display: flex; align-items: center; gap: 16px; z-index: 1; }
.hero-wx-icon { width: 58px; height: 58px; display: flex; align-items: center; justify-content: center; }
.hero-temp-big { font-size: 42px; font-weight: 800; line-height: 1; letter-spacing: -1.2px; }
.hero-cond { font-size: 13.5px; color: rgba(248, 250, 252, 0.85); font-weight: 600; margin-top: 3px; }
.hero-Chips { position: relative; display: flex; gap: 10px; flex-wrap: wrap; z-index: 1; }
.h-chip {
  display: flex; align-items: center; gap: 7px;
  background: rgba(9, 17, 32, 0.55); backdrop-filter: blur(8px);
  padding: 7px 13px; border-radius: 99px; font-size: 12.5px; font-weight: 700;
  border: 1px solid rgba(255, 255, 255, 0.14);
}
.h-dot { width: 8px; height: 8px; border-radius: 50%; }
.wind-dot { background: #38bdf8; }
.baro-dot { background: #a855f7; }
.rain-dot { background: #60a5fa; }
.aqi-dot { background: #22c55e; }

/* CARDS & GRID */
.grid-3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 16px; }
.grid-2 { display: grid; grid-template-columns: repeat(2, 1fr); gap: 16px; }
@media (max-width: 1120px) { .grid-3 { grid-template-columns: repeat(2, 1fr); } }
@media (max-width: 740px) { .grid-3, .grid-2 { grid-template-columns: 1fr; } }

.card {
  background: var(--card); border: 1px solid var(--border);
  border-radius: 16px; padding: 15px 17px;
  box-shadow: var(--shadow); display: flex; flex-direction: column; justify-content: space-between; gap: 10px;
  transition: transform 0.15s ease, border-color 0.15s ease;
}
.card.clickable { cursor: pointer; }
.card.clickable:hover { transform: translateY(-2px); border-color: var(--accent); }
.card-header { display: flex; justify-content: space-between; align-items: center; }
.card-title { font-size: 11.5px; font-weight: 800; letter-spacing: 0.6px; color: var(--muted); display: flex; align-items: center; gap: 6px; }
.i-icon { font-size: 14px; }

.badge {
  font-size: 11px; padding: 3px 9px; border-radius: 99px;
  font-weight: 700; background: var(--card-inner); color: var(--muted); border: 1px solid var(--border);
}
.badge.ok { color: var(--ok); background: var(--ok-soft); border-color: transparent; }
.badge.info { color: var(--accent); background: var(--accent-soft); border-color: transparent; }
.badge.warn { color: var(--warn); background: var(--warn-soft); border-color: transparent; }
.badge.danger { color: var(--danger); background: var(--danger-soft); border-color: transparent; }

/* 1. WIND COMPASS & SELF-EXPLAINING KEY */
.gauge-center { display: flex; justify-content: center; align-items: center; }
.wind-compass { width: 186px; height: 186px; }
.compass-track { fill: var(--card-inner); stroke: var(--border); stroke-width: 4; }
.compass-inner { fill: none; stroke: var(--border); stroke-dasharray: 3 3; stroke-width: 1; }
.c-dir { fill: var(--muted); font-size: 10.5px; font-weight: 800; text-anchor: middle; }
.c-dir.north { fill: #ef4444; }
.arrow-head { fill: var(--accent); }
.arrow-shaft { stroke: var(--accent); stroke-width: 3.4; stroke-linecap: round; }
.arrow-tail { fill: var(--accent); }
.compass-hub { fill: var(--card); stroke: var(--border); stroke-width: 2; }
.hub-val { fill: var(--text); font-size: 22px; font-weight: 800; text-anchor: middle; }
.hub-unit { fill: var(--muted); font-size: 8.5px; font-weight: 700; text-anchor: middle; }

.compass-key {
  display: flex; justify-content: center; gap: 14px;
  font-size: 10.5px; color: var(--muted); font-weight: 600;
}
.compass-key span { display: inline-flex; align-items: center; gap: 5px; }
.key-arrow { width: 10px; height: 3px; background: var(--accent); border-radius: 2px; display: inline-block; }
.key-petal { width: 9px; height: 9px; background: rgba(56,189,248,0.32); border: 1px solid #38bdf8; border-radius: 2px; display: inline-block; }
.key-solid-line { width: 12px; height: 3px; background: var(--accent); border-radius: 2px; display: inline-block; }
.key-dash-line { width: 12px; height: 0; border-top: 2px dashed #94a3b8; display: inline-block; }

.range-bar-wrap { background: var(--card-inner); padding: 9px 12px; border-radius: 11px; }
.range-labels { display: flex; justify-content: space-between; font-size: 11px; color: var(--muted); margin-bottom: 6px; }
.rl-item { display: inline-flex; align-items: center; gap: 4px; }
.range-labels b { color: var(--text); }
.warn-txt { color: var(--warn) !important; }
.dot-min { width: 8px; height: 4px; background: rgba(56,189,248,0.5); border-radius: 2px; display: inline-block; }
.dot-avg { width: 8px; height: 8px; border-radius: 50%; background: var(--accent); display: inline-block; }
.dot-gust { width: 4px; height: 9px; border-radius: 1px; background: var(--warn); display: inline-block; }

.range-track { position: relative; height: 7px; border-radius: 99px; background: var(--border); }
.range-span { position: absolute; top: 0; height: 100%; border-radius: 99px; background: rgba(56, 189, 248, 0.45); }
.range-dot { position: absolute; top: -3px; width: 13px; height: 13px; border-radius: 50%; background: var(--accent); border: 2px solid var(--card); transform: translateX(-50%); }
.range-gust-pin { position: absolute; top: -2px; width: 4px; height: 11px; border-radius: 2px; background: var(--warn); transform: translateX(-50%); }

/* 2. THERMAL SPECTRUM & HUMIDITY RING */
.thermo-visual { display: flex; flex-direction: column; gap: 10px; }
.thermo-readouts { display: flex; justify-content: space-between; align-items: center; padding: 0 2px; }
.big-num { display: block; font-size: 34px; font-weight: 800; letter-spacing: -1px; line-height: 1.05; }
.sub-lbl { font-size: 12px; color: var(--muted); font-weight: 600; }
.rh-ring-svg { width: 76px; height: 76px; }
.ring-bg { fill: none; stroke: var(--card-inner); stroke-width: 8; }
.ring-fg-cyan { fill: none; stroke: var(--accent); stroke-width: 8; stroke-linecap: round; transition: stroke-dasharray 0.3s; }
.ring-fg-ok { fill: none; stroke: var(--ok); stroke-width: 8; stroke-linecap: round; transition: stroke-dasharray 0.3s; }
.ring-val { fill: var(--text); font-size: 15px; font-weight: 800; text-anchor: middle; }
.ring-sub { fill: var(--muted); font-size: 8px; font-weight: 700; text-anchor: middle; }

.spectrum-box { background: var(--card-inner); padding: 9px 11px; border-radius: 11px; }
.pin-legend { display: flex; justify-content: space-between; font-size: 10.5px; color: var(--muted); margin-bottom: 8px; }
.pin-legend span { display: inline-flex; align-items: center; gap: 4px; }
.pin-legend b { color: var(--text); }
.p-dot { width: 8px; height: 8px; border-radius: 50%; display: inline-block; }
.p-dot.blue { background: #38bdf8; }
.p-dot.white { background: #ffffff; border: 1px solid #64748b; }
.p-dot.amber { background: #f59e0b; }

.temp-spectrum-track {
  position: relative; height: 10px; border-radius: 99px;
  background: linear-gradient(90deg, #3b82f6 0%, #06b6d4 28%, #22c55e 52%, #f59e0b 76%, #ef4444 100%);
}
.dew-spread-bridge {
  position: absolute; top: 2px; height: 6px;
  background: rgba(255, 255, 255, 0.35); border-radius: 99px;
}
.spec-pin {
  position: absolute; top: -3px; width: 15px; height: 15px; border-radius: 50%;
  transform: translateX(-50%); border: 2px solid #091120;
}
.dew-pin { background: #38bdf8; }
.temp-pin { background: #ffffff; box-shadow: 0 0 8px #ffffff; z-index: 2; }
.feels-pin { background: #f59e0b; width: 12px; height: 12px; top: -1px; }
.spectrum-scale { display: flex; justify-content: space-between; font-size: 9px; color: var(--muted); margin-top: 6px; }

.mini-bars-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
.mbar { background: var(--card-inner); padding: 7px 10px; border-radius: 10px; }
.mbar-top { display: flex; justify-content: space-between; font-size: 10.5px; color: var(--muted); margin-bottom: 5px; }
.mbar-top b { color: var(--text); }
.mbar-track { height: 5px; border-radius: 99px; background: var(--border); overflow: hidden; }
.mbar-fill { height: 100%; border-radius: 99px; }
.mbar-fill.cyan { background: var(--accent); }
.mbar-fill.green { background: var(--ok); }

/* 3. BAROMETER FACTUAL FORECAST ROW & DIAL */
.forecast-fact-row {
  display: flex; align-items: center; justify-content: space-between;
  padding: 8px 12px; border-radius: 10px;
  background: var(--card-inner); border: 1px solid var(--border);
}
.fb-kicker { font-size: 10.5px; font-weight: 700; color: var(--muted); }
.fb-text { font-size: 13px; font-weight: 800; color: var(--text); }

.baro-dial-wrap { display: flex; justify-content: center; }
.baro-dial { width: 215px; height: 112px; }
.dial-lbl { fill: var(--muted); font-size: 8.5px; font-weight: 700; }
.baro-big { fill: var(--text); font-size: 21px; font-weight: 800; text-anchor: middle; }
.baro-unit { fill: var(--muted); font-size: 8.5px; font-weight: 600; text-anchor: middle; }
.spark-clean { background: var(--card-inner); padding: 7px 10px; border-radius: 11px; }
.spark-svg { width: 100%; height: auto; display: block; }
.spark-foot { display: flex; justify-content: space-between; font-size: 10px; color: var(--muted); margin-bottom: 3px; font-weight: 600; }

/* 4. RAIN CYLINDER */
.rain-visual-row { display: flex; align-items: center; gap: 15px; height: 100%; }
.rain-tube-wrap { display: flex; flex-direction: column; align-items: center; gap: 4px; }
.rain-tube {
  position: relative; width: 42px; height: 124px; border-radius: 10px 10px 16px 16px;
  background: var(--card-inner); border: 2px solid var(--border); overflow: hidden;
  display: flex; align-items: flex-end;
}
.rain-water {
  width: 100%; background: linear-gradient(180deg, #38bdf8 0%, #0284c7 100%);
  transition: height 0.4s ease; border-radius: 4px 4px 0 0;
}
.rain-ticks {
  position: absolute; inset: 12px 0; display: flex; flex-direction: column;
  justify-content: space-between; pointer-events: none;
}
.rain-ticks span { display: block; width: 10px; height: 1.5px; background: var(--muted); opacity: 0.5; }
.tube-lbl { font-size: 13px; font-weight: 800; }
.tube-sub { font-size: 9.5px; color: var(--muted); }
.rain-right { flex: 1; display: flex; flex-direction: column; gap: 5px; }
.rain-hero-row { display: flex; justify-content: space-between; align-items: baseline; }
.rain-hero-num { display: flex; align-items: baseline; gap: 8px; }
.phase-legend { display: flex; gap: 9px; font-size: 10px; color: var(--muted); font-weight: 700; }
.phase-legend span { display: inline-flex; align-items: center; gap: 4px; }
.ph-dot { width: 8px; height: 8px; border-radius: 2px; display: inline-block; }
.ph-dot.rain-ph { background: #38bdf8; }
.ph-dot.hail-ph { background: #c084fc; }
.rain-chart-svg { width: 100%; height: auto; display: block; }

/* VISUAL HAILSTONE DISDROMETER STRIP */
.hail-strip {
  display: flex; justify-content: space-between; align-items: center;
  background: var(--card-inner); padding: 6px 10px; border-radius: 9px;
}
.hail-info { display: flex; flex-direction: column; }
.hail-kicker { font-size: 9.5px; color: var(--muted); font-weight: 600; }
.hail-info strong { font-size: 11.5px; color: var(--text); }
.hail-scale { display: flex; align-items: flex-end; gap: 10px; }
.hs-item { display: flex; flex-direction: column; align-items: center; gap: 2px; opacity: 0.32; transition: all 0.2s; }
.hs-item.active { opacity: 1; }
.hs-item small { font-size: 8.5px; color: var(--muted); font-weight: 700; }
.hs-ball { display: block; border-radius: 50%; background: #c084fc; border: 1.5px solid #e9d5ff; }
.hs-item.active .hs-ball { box-shadow: 0 0 8px #c084fc; }
.hs-ball.s5  { width: 6px; height: 6px; }
.hs-ball.s10 { width: 10px; height: 10px; }
.hs-ball.s15 { width: 13px; height: 13px; }
.hs-ball.s20 { width: 16px; height: 16px; }
.rain-pills {
  display: flex; justify-content: space-between; flex-wrap: wrap; gap: 4px;
  font-size: 10.5px; color: var(--muted); background: var(--card-inner);
  padding: 7px 10px; border-radius: 9px;
}
.rain-pills b { color: var(--text); }

/* 5. SOLAR & UV */
.sun-arc-wrap { display: flex; justify-content: center; }
.sun-arc-svg { width: 248px; height: 108px; }
.sun-val { fill: var(--text); font-size: 18px; font-weight: 800; text-anchor: middle; }
.sun-sub { fill: var(--muted); font-size: 9.5px; font-weight: 600; text-anchor: middle; }
.horizon-time { fill: var(--muted); font-size: 9.5px; font-weight: 700; }
.uv-visual { background: var(--card-inner); padding: 9px 12px; border-radius: 11px; }
.uv-top { display: flex; justify-content: space-between; font-size: 11.5px; margin-bottom: 7px; }
.uv-rainbow-bar {
  position: relative; height: 10px; border-radius: 99px;
  background: linear-gradient(90deg, #22c55e 0%, #eab308 35%, #f97316 65%, #ef4444 85%, #a855f7 100%);
}
.uv-dot {
  position: absolute; top: -3px; width: 16px; height: 16px; border-radius: 50%;
  background: #ffffff; border: 3px solid #091120; transform: translateX(-50%);
  box-shadow: 0 0 8px rgba(255,255,255,0.9); transition: left 0.3s;
}
.uv-scale-labels { display: flex; justify-content: space-between; font-size: 9px; color: var(--muted); margin-top: 5px; }

/* 6. AIR QUALITY & DEDICATED LIGHTNING TIMELINE */
.air-visual-row { display: flex; align-items: center; gap: 12px; }
.aqi-radar-stack { display: flex; flex-direction: column; align-items: center; }
.aqi-ring-svg { width: 88px; height: 88px; }
.aqi-big { fill: var(--text); font-size: 21px; font-weight: 800; text-anchor: middle; }
.aqi-sub { fill: var(--ok); font-size: 8px; font-weight: 800; text-anchor: middle; }

.bullet-stack { flex: 1; display: flex; flex-direction: column; gap: 6px; }
.b-row { background: var(--card-inner); padding: 5px 9px; border-radius: 8px; }
.b-hdr { display: flex; justify-content: space-between; font-size: 10.5px; color: var(--muted); margin-bottom: 3px; }
.b-hdr b { color: var(--text); }
.b-track { position: relative; height: 5px; border-radius: 99px; background: var(--border); }
.b-fill { height: 100%; border-radius: 99px; transition: width 0.3s; }
.b-fill.ok { background: var(--ok); }
.b-fill.cyan { background: var(--accent); }
.b-fill.warn { background: var(--warn); }
.b-fill.danger { background: var(--danger); }
.b-limit { position: absolute; top: -2px; width: 2px; height: 9px; background: var(--text); opacity: 0.65; }

.lightning-panel {
  background: var(--card-inner); padding: 8px 11px; border-radius: 11px;
  border: 1px solid var(--border);
}
.lt-hdr { display: flex; justify-content: space-between; align-items: center; font-size: 11px; margin-bottom: 4px; }
.lt-title { color: var(--muted); font-weight: 700; }
.lt-status { font-size: 11px; color: var(--text); }
.lt-timeline-svg { width: 100%; height: auto; display: block; }
.lt-time-axis { display: flex; justify-content: space-between; font-size: 8.5px; color: var(--muted); margin-top: 2px; }

/* TAB 2 & TAB 3 */
.trends-bar {
  display: flex; justify-content: space-between; align-items: center;
  flex-wrap: wrap; gap: 10px; margin-bottom: 14px;
  background: var(--card); border: 1px solid var(--border);
  padding: 10px 16px; border-radius: 13px;
}
.range-btns { display: flex; gap: 6px; }
.btn-chip.active { background: var(--accent-soft); color: var(--accent); border-color: var(--accent); }
.trends-kpis { display: flex; gap: 16px; flex-wrap: wrap; font-size: 12px; color: var(--muted); }
.t-kpi b { color: var(--text); }
.legend-pills { display: flex; gap: 10px; font-size: 11px; color: var(--muted); font-weight: 700; }
.l-dot::before { content: "● "; }
.l-dot.cyan::before { color: #38bdf8; }
.l-dot.green::before { color: #22c55e; }
.l-dot.amber::before { color: #f59e0b; }
.chart-svg { width: 100%; height: 175px; background: var(--card-inner); border-radius: 11px; }
.sector-bars { display: grid; grid-template-columns: repeat(4, 1fr); gap: 8px; }
.sec-item { background: var(--card-inner); padding: 7px 9px; border-radius: 8px; font-size: 11px; }
.sec-top { display: flex; justify-content: space-between; margin-bottom: 4px; font-weight: 700; }
.sec-bar-bg { height: 5px; background: var(--border); border-radius: 99px; overflow: hidden; }
.sec-bar-fg { height: 100%; background: var(--accent); }

.twin-wrap { background: var(--card-inner); border-radius: 12px; padding: 8px; }
.twin-svg { width: 100%; height: 290px; }
.twin-lbl { fill: var(--muted); font-size: 10px; font-weight: 700; }
.twin-val { fill: var(--text); font-size: 11.5px; font-weight: 800; }

.corr-visual-list { display: flex; flex-direction: column; gap: 10px; }
.corr-item { background: var(--card-inner); padding: 9px 12px; border-radius: 10px; }
.corr-top { display: flex; justify-content: space-between; font-size: 12px; margin-bottom: 5px; }
.corr-bar-bg { height: 6px; border-radius: 99px; background: var(--border); overflow: hidden; }
.corr-bar-fg { height: 100%; border-radius: 99px; }
.corr-bar-fg.amber { background: #f59e0b; }
.corr-bar-fg.cyan { background: #38bdf8; }
.corr-bar-fg.green { background: #22c55e; }
.code-box {
  font-family: "SFMono-Regular", Consolas, monospace; font-size: 11px;
  padding: 9px 11px; border-radius: 8px; background: var(--card-inner); color: var(--accent);
}

/* SETTINGS & MODAL */
.form-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
.form-grid label { display: flex; flex-direction: column; gap: 4px; font-size: 11.5px; color: var(--muted); }
.form-grid input {
  background: var(--card-inner); border: 1px solid var(--border); color: var(--text);
  padding: 8px 10px; border-radius: 8px; font-size: 12.5px;
}
.btn-row { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
.btn-primary {
  background: var(--accent); color: #061224; border: none;
  padding: 8px 14px; border-radius: 8px; font-weight: 800; font-size: 12px; cursor: pointer;
}
.btn-secondary {
  background: var(--card-inner); color: var(--text); border: 1px solid var(--border);
  padding: 8px 13px; border-radius: 8px; font-weight: 700; font-size: 12px; cursor: pointer;
}
.btn-danger {
  background: var(--danger-soft); color: var(--danger); border: none;
  padding: 8px 13px; border-radius: 8px; font-weight: 800; font-size: 12px; cursor: pointer;
}
.ota-box { background: var(--card-inner); padding: 12px; border-radius: 10px; display: flex; flex-direction: column; gap: 8px; }
.muted-xs { font-size: 11px; color: var(--muted); }
.accent { color: var(--accent); }

.modal-backdrop {
  position: fixed; inset: 0; background: rgba(4, 9, 20, 0.72);
  backdrop-filter: blur(6px); display: flex; align-items: center; justify-content: center; z-index: 50; padding: 20px;
}
.modal-card {
  background: var(--card); border: 1px solid var(--border); border-radius: 16px;
  padding: 20px; max-width: 540px; width: 100%; box-shadow: var(--shadow);
}
.modal-body { margin-top: 12px; display: flex; flex-direction: column; gap: 8px; font-size: 13px; }
.m-kv { display: flex; justify-content: space-between; padding: 7px 10px; background: var(--card-inner); border-radius: 8px; }
)WEBUI";
static constexpr std::size_t app_css_size = sizeof(app_css_data) - 1;

static constexpr const char app_js_data[] = R"WEBUI(const COMPASS_16 = ["N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"];
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
    title.textContent = "☀ Solar Irradiance, UV & Cloud Correlation";
    rows = [
      ["Solar Irradiance (VEML7700)", `${d.environment.solar_w_m2.toFixed(1)} W/m²`],
      ["Ambient Brightness", `${Math.round(d.environment.lux).toLocaleString()} lx`],
      ["UV Index (LTR-390UV)", `${d.environment.uv_index.toFixed(2)}`],
      ["Estimated Cloud Cover (Kc + UV/Vis Ratio)", `${(d.environment.cloud_cover_pct || 0).toFixed(0)} %`],
      ["Evapotranspiration (ET₀)", `${(0.00055 * d.environment.solar_w_m2).toFixed(2)} mm/h`],
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
  if (!svg || historyPoints.length < 2) return;
  const u = UNIT_SYSTEMS[unitIdx];
  const vals = historyPoints.map(p => p.qnh * u.pMul);
  const minV = Math.min(...vals), maxV = Math.max(...vals);
  const span = Math.max(0.4 * u.pMul, maxV - minV);
  const pts = vals.map((v, i) => {
    const x = 6 + (i / (vals.length - 1)) * 308;
    const y = 42 - ((v - minV) / span) * 34;
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  }).join(" ");
  const dec = u.pUnit === "inHg" ? 2 : 1;
  document.getElementById("baro-minmax").textContent =
    `↓ ${minV.toFixed(dec)} · ↑ ${maxV.toFixed(dec)} ${u.pUnit}`;
  svg.innerHTML = `
    <polygon fill="rgba(56,189,248,0.15)" points="6,46 ${pts} 314,46" />
    <polyline fill="none" stroke="#38bdf8" stroke-width="2.2" points="${pts}" />`;
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
  const yTop = 8;
  const yMid = 32;
  const yBot = 56;

  let html = `
    <line x1="${xStart}" y1="${yTop}" x2="${xEnd}" y2="${yTop}" stroke="rgba(142,162,198,0.20)" stroke-dasharray="2 2"/>
    <text x="25" y="${yTop + 3}" fill="#8ea2c6" font-size="9" text-anchor="end">${maxR.toFixed(rDec)}</text>
    <line x1="${xStart}" y1="${yMid}" x2="${xEnd}" y2="${yMid}" stroke="rgba(142,162,198,0.16)" stroke-dasharray="2 2"/>
    <text x="25" y="${yMid + 3}" fill="#8ea2c6" font-size="9" text-anchor="end">${midR.toFixed(rDec)}</text>
    <line x1="${xStart}" y1="${yBot}" x2="${xEnd}" y2="${yBot}" stroke="rgba(142,162,198,0.28)"/>
    <text x="25" y="${yBot + 3}" fill="#8ea2c6" font-size="9" text-anchor="end">0</text>
    <text x="${xStart}" y="71" fill="#8ea2c6" font-size="9" text-anchor="start">-60m</text>
    <text x="${xMid.toFixed(1)}" y="71" fill="#8ea2c6" font-size="9" text-anchor="middle">-30m</text>
    <text x="${xEnd}" y="71" fill="#8ea2c6" font-size="9" text-anchor="end">Now</text>`;

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

  const bins = (d.lightning && Array.isArray(d.lightning.timeline_60m))
    ? d.lightning.timeline_60m
    : [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  // Dynamic Y-axis rescaling based on peak 5-minute strike count
  const peakStrikes = Math.max(...bins);
  const maxB = Math.max(2, Math.ceil(peakStrikes * 1.15));
  const midB = Math.round(maxB * 0.5);
  const xStart = 24;
  const xEnd = 374;
  const xMid = (xStart + xEnd) * 0.5;
  const w = (xEnd - xStart) / bins.length;
  const yTop = 12;
  const yMid = 29;
  const yBot = 46;

  const guides = `
    <line x1="${xStart}" y1="${yTop}" x2="${xEnd}" y2="${yTop}" stroke="rgba(142,162,198,0.18)" stroke-dasharray="2 2"/>
    <text x="19" y="${yTop + 3}" fill="#8ea2c6" font-size="9.5" text-anchor="end">${maxB}</text>
    <line x1="${xStart}" y1="${yMid}" x2="${xEnd}" y2="${yMid}" stroke="rgba(142,162,198,0.12)" stroke-dasharray="2 2"/>
    <text x="19" y="${yMid + 3}" fill="#8ea2c6" font-size="9.5" text-anchor="end">${midB}</text>
    <line x1="${xStart}" y1="${yBot}" x2="${xEnd}" y2="${yBot}" stroke="rgba(142,162,198,0.26)"/>
    <text x="19" y="${yBot + 3}" fill="#8ea2c6" font-size="9.5" text-anchor="end">0</text>
    <text x="${xStart}" y="60" fill="#8ea2c6" font-size="9.5" text-anchor="start">-60m</text>
    <text x="${xMid.toFixed(1)}" y="60" fill="#8ea2c6" font-size="9.5" text-anchor="middle">-30m</text>
    <text x="${xEnd}" y="60" fill="#8ea2c6" font-size="9.5" text-anchor="end">Now</text>`;

  const bars = bins.map((cnt, i) => {
    const h = cnt > 0 ? Math.max(6, (cnt / maxB) * (yBot - yTop)) : 2;
    const x = xStart + i * w;
    const bw = w - 4;
    const cx = x + bw * 0.5;
    const col = cnt >= 3 ? "#f59e0b" : (cnt > 0 ? "#38bdf8" : "var(--border)");
    const rect = `<rect x="${x.toFixed(1)}" y="${(yBot - h).toFixed(1)}" width="${bw.toFixed(1)}" height="${h.toFixed(1)}" rx="2.5" fill="${col}"/>`;
    const lbl = cnt > 0
      ? `<text x="${cx.toFixed(1)}" y="${Math.max(9, yBot - h - 3).toFixed(1)}" fill="var(--text)" font-size="9.5" font-weight="800" text-anchor="middle">${cnt}</text>`
      : "";
    return rect + lbl;
  }).join("");
  svg.innerHTML = guides + bars;

  if (d.lightning.strikes > 0) {
    statusEl.textContent = `${d.lightning.strikes} strikes · ~${d.lightning.distance_km} km`;
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
  document.getElementById("wind-avg1m").textContent = (d.wind.avg_1m_m_s * u.wMul).toFixed(1);
  document.getElementById("wind-gust").textContent = `${(d.wind.gust_m_s * u.wMul).toFixed(1)} ${u.wUnit}`;
  const maxScaleW = Math.max(15, d.wind.gust_m_s * 1.25);
  const lullP = clampPct(d.wind.lull_m_s, 0, maxScaleW);
  const avgP = clampPct(d.wind.avg_1m_m_s, 0, maxScaleW);
  const gustP = clampPct(d.wind.gust_m_s, 0, maxScaleW);
  document.getElementById("wind-range-span").style.left = `${lullP.toFixed(0)}%`;
  document.getElementById("wind-range-span").style.width = `${Math.max(4, gustP - lullP).toFixed(0)}%`;
  document.getElementById("wind-avg-dot").style.left = `${avgP.toFixed(0)}%`;
  document.getElementById("wind-gust-pin").style.left = `${gustP.toFixed(0)}%`;
  if (d.wind.rose) renderRosePetals(d.wind.rose);

  // 2. Thermal & Humidity (Plain-English, Self-Labeled Pins)
  document.getElementById("env-temp").textContent = `${u.tFn(d.environment.temp_c).toFixed(1)}°`;
  document.getElementById("env-feels").textContent = `Feels like ${u.tFn(d.environment.feels_like_c).toFixed(1)}° outside`;
  document.getElementById("env-rh").textContent = `${Math.round(d.environment.humidity_rh)}%`;
  const rhCirc = (d.environment.humidity_rh / 100) * 220;
  document.getElementById("rh-arc").setAttribute("stroke-dasharray", `${rhCirc.toFixed(0)} 220`);
  document.getElementById("env-dew").textContent = `${u.tFn(d.environment.dew_point_c).toFixed(1)}°`;
  document.getElementById("env-actual-sm").textContent = `${u.tFn(d.environment.temp_c).toFixed(1)}°`;
  document.getElementById("env-feels-sm").textContent = `${u.tFn(d.environment.feels_like_c).toFixed(1)}°`;

  const pDew = clampPct(d.environment.dew_point_c, -15, 40);
  const pTemp = clampPct(d.environment.temp_c, -15, 40);
  const pFeels = clampPct(d.environment.feels_like_c, -15, 40);
  document.getElementById("pin-dew").style.left = `${pDew.toFixed(1)}%`;
  document.getElementById("pin-temp").style.left = `${pTemp.toFixed(1)}%`;
  document.getElementById("pin-feels").style.left = `${pFeels.toFixed(1)}%`;
  document.getElementById("dew-bridge").style.left = `${Math.min(pDew, pTemp).toFixed(1)}%`;
  document.getElementById("dew-bridge").style.width = `${Math.max(2, Math.abs(pTemp - pDew)).toFixed(1)}%`;

  const dewSpread = Math.max(0, d.environment.temp_c - d.environment.dew_point_c);
  document.getElementById("fog-risk-lbl").textContent =
    dewSpread <= 2.0 ? `High (${dewSpread.toFixed(1)}° gap)` : `Low (${dewSpread.toFixed(1)}° gap)`;
  document.getElementById("lcl-fill").style.width = `${clampPct(15 - dewSpread, 0, 15).toFixed(0)}%`;

  const tC = d.environment.temp_c;
  const stressTxt = tC <= 0 ? "Freezing Cold" : (tC < 10 ? "Chilly" : (tC > 30 ? "Heat Stress" : "None (Pleasant)"));
  document.getElementById("stress-lbl").textContent = stressTxt;
  document.getElementById("vpd-fill").style.width = `${clampPct(Math.abs(tC - 20), 0, 25).toFixed(0)}%`;
  document.getElementById("comfort-badge").textContent =
    d.environment.frost_risk ? "Frost Warning" : (tC > 30 ? "Hot" : "Comfortable");
  document.getElementById("comfort-badge").className =
    d.environment.frost_risk || tC > 30 ? "badge warn" : "badge ok";

  // 3. Barometer (Dedicated Forecast Banner + Labeled 3h Ago Needle)
  const qnhAngle = Math.max(-88, Math.min(88, ((d.environment.qnh_hpa - 1010) / 30) * 90));
  const qnh3hAgo = d.environment.qnh_hpa - d3h;
  const ghostAngle = Math.max(-88, Math.min(88, ((qnh3hAgo - 1010) / 30) * 90));
  document.getElementById("baro-needle").setAttribute("transform", `rotate(${qnhAngle.toFixed(1)} 120 110)`);
  document.getElementById("baro-ghost-needle").setAttribute("transform", `rotate(${ghostAngle.toFixed(1)} 120 110)`);
  document.getElementById("env-qnh").textContent = (d.environment.qnh_hpa * u.pMul).toFixed(pDec);
  document.getElementById("env-qnh-unit").textContent = `${u.pUnit} (Sea-Level)`;
  document.getElementById("baro-3h-ago").textContent = `${(qnh3hAgo * u.pMul).toFixed(pDec)} ${u.pUnit}`;

  const fcast = d.environment.forecast || "Fine Weather";
  document.getElementById("env-zambretti").textContent = fcast;
  const rate1h = (d3h / 3.0) * u.pMul;
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

  // 5. Sun, UV & Solar-UV Cloud Cover Correlation
  const uvi = d.environment.uv_index;
  const uvCat = uvi >= 8 ? "Very High" : (uvi >= 6 ? "High" : (uvi >= 3 ? "Moderate" : "Low"));
  if (d.astronomy) {
    document.getElementById("daylight-badge").textContent = `Daylight ${d.astronomy.daylight}`;
    document.getElementById("sun-rise").textContent = d.astronomy.sunrise;
    document.getElementById("sun-set").textContent = d.astronomy.sunset;
  }
  document.getElementById("env-solar").textContent = `${d.environment.solar_w_m2.toFixed(0)} W/m²`;
  document.getElementById("env-lux").textContent = `Brightness: ${Math.round(d.environment.lux).toLocaleString()} lx`;
  document.getElementById("env-uv").textContent = `${uvi.toFixed(1)} · ${uvCat}`;
  document.getElementById("uv-dot").style.left = `${clampPct(uvi, 0, 11.5).toFixed(0)}%`;
  document.getElementById("uv-advice").textContent = uvi >= 6 ? "Shade & SPF 50+" : (uvi >= 3 ? "Sunscreen advised" : "No protection needed");
  const cloudPct = d.environment.cloud_cover_pct !== undefined ? d.environment.cloud_cover_pct : 20;
  const cloudDesc = cloudPct >= 80 ? "Overcast" : (cloudPct >= 50 ? "Broken Clouds" : (cloudPct >= 20 ? "Scattered" : "Clear Sky"));
  document.getElementById("cloud-cover-lbl").textContent = `${cloudPct.toFixed(0)}% · ${cloudDesc}`;
  document.getElementById("cloud-cover-fill").style.width = `${cloudPct.toFixed(0)}%`;
  const tSun = Math.min(0.88, Math.max(0.15, 0.2 + (d.environment.solar_w_m2 / 1100) * 0.6));
  const sx = (1 - tSun) * (1 - tSun) * 28 + 2 * (1 - tSun) * tSun * 130 + tSun * tSun * 232;
  const sy = (1 - tSun) * (1 - tSun) * 84 + 2 * (1 - tSun) * tSun * (-16) + tSun * tSun * 84;
  document.getElementById("sun-orb").setAttribute("transform", `translate(${sx.toFixed(1)}, ${sy.toFixed(1)})`);

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
  const compCo2 = d.air_quality.comp_co2_ppm || d.air_quality.co2_ppm;
  document.getElementById("aq-pm25").textContent = `${dryPm25.toFixed(1)} µg/m³`;
  document.getElementById("b-pm25").style.width = `${clampPct(dryPm25, 0, 35).toFixed(0)}%`;
  document.getElementById("b-pm25").className = `b-fill ${dryPm25 <= 15 ? "ok" : "warn"}`;
  document.getElementById("aq-pm10").textContent = `${dryPm10.toFixed(1)} µg/m³`;
  document.getElementById("b-pm10").style.width = `${clampPct(dryPm10, 0, 90).toFixed(0)}%`;
  document.getElementById("aq-co2").textContent = `${compCo2} ppm · VOC ${d.air_quality.voc_index}`;
  document.getElementById("b-co2").style.width = `${clampPct(compCo2, 380, 1000).toFixed(0)}%`;

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
)WEBUI";
static constexpr std::size_t app_js_size = sizeof(app_js_data) - 1;

}  // namespace web_assets
}  // namespace firmware
}  // namespace station
// clang-format on
