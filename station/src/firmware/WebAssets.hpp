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
        <option value="breeze">☀ Spring Breeze (Day)</option>
        <option value="storm">⛈ Squall &amp; Hail</option>
        <option value="freeze">❄ Freezing Fog (-4 °C)</option>
        <option value="heatwave">🔥 Heatwave (UV 10.2)</option>
        <option value="plume">🌫 Urban Plume</option>
        <option value="train">🚛 Freight Train &amp; Tremor (24 Hz)</option>
        <option value="night_dark">🌙 Clear Starry Night (Bortle 2)</option>
        <option value="night_moon">🌕 Full Moon &amp; Night Clouds</option>
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

          <div class="gauge-aside-row">
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

            <!-- Subtle vertical legend aside -->
            <div class="aside-key">
              <span><i class="key-arrow"></i>Now</span>
              <span><i class="key-petal"></i>1h</span>
            </div>
          </div>

          <!-- Dual-Lane Bar: Wind Speed (Upper Lane) & Surface Wind Load (Lower Lane), like Sun/Moon -->
          <div class="range-bar-wrap">
            <div class="range-labels">
              <span class="rl-item"><i class="lane-key speed"></i>Min <b id="wind-lull">2.9</b></span>
              <span class="rl-item" id="wind-trend-lbl">↗ +0.6 m/s/h</span>
              <span class="rl-item">Gust <b id="wind-gust">7.2 m/s</b></span>
            </div>
            <div class="dual-track">
              <div class="dual-span speed" id="wind-range-span" style="left:14%;width:32%"></div>
              <div class="dual-span load" id="wload-span" style="left:8%;width:38%"></div>
              <div class="dual-dot speed" id="wind-avg-dot" style="left:26%" title="Current Wind Speed"></div>
              <div class="dual-dot load" id="wload-now-dot" style="left:22%" title="Current Surface Wind Load"></div>
            </div>
            <div class="range-labels range-labels-bottom">
              <span class="rl-item"><i class="lane-key load"></i>Load <b id="wload-min">0.5</b></span>
              <span class="rl-item">Now <b id="wload-trend-lbl">1.4</b></span>
              <span class="rl-item">Peak <b id="wload-max">3.2 kg/m²</b></span>
            </div>
          </div>
        </article>

        <!-- 2. TEMPERATURE & HUMIDITY (SINGLE 24H BAR + 3-COLUMN DEW / WET-BULB / ABS HUMIDITY ROW) -->
        <article class="card clickable" data-inspect="temp">
          <div class="card-header">
            <span class="card-title"><span class="i-icon">🌡</span>TEMPERATURE &amp; HUMIDITY</span>
            <span class="badge ok" id="comfort-badge">Comfortable</span>
          </div>

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

          <!-- Unified Range-Capsule Bar: 24h Temperature (Low — 1h Trend — High) -->
          <div class="range-bar-wrap">
            <div class="range-labels">
              <span class="rl-item">24h Low <b id="temp-min-lbl">14.2°</b></span>
              <span class="rl-item" id="temp-trend-lbl">↗ +0.8°/h · Warming</span>
              <span class="rl-item">24h High <b id="temp-max-lbl">19.6°</b></span>
            </div>
            <div class="range-track">
              <div class="range-span temp-grad" id="t24-bridge" style="left:45%;width:24%"></div>
              <div class="range-dot" id="pin-temp" style="left:62%" title="Current Outdoor Temperature"></div>
            </div>
          </div>

          <!-- 3-Column Single-Row Micro-Climate Strip (Dew Point · Wet-Bulb · Absolute Humidity) -->
          <div class="thermo-bento-3">
            <div class="tb-cell">
              <span class="tb-kicker">Dew Point</span>
              <strong class="tb-val-sm" id="env-dew">11.1°</strong>
              <small class="tb-sub" id="fog-risk-lbl">7.3° gap</small>
            </div>
            <div class="tb-cell">
              <span class="tb-kicker">Wet-Bulb</span>
              <strong class="tb-val-sm" id="env-wetbulb">14.4°</strong>
              <small class="tb-sub" id="wetbulb-sub">Rain phase</small>
            </div>
            <div class="tb-cell">
              <span class="tb-kicker">Abs. Humidity</span>
              <strong class="tb-val-sm" id="abs-hum-lbl">9.8 g/m³</strong>
              <small class="tb-sub" id="abs-hum-sub">Crisp air</small>
            </div>
          </div>
        </article>

        <!-- 3. BAROMETER & SAGER 12–24H PREDICTION (NO EXTRA BOXES BELOW GAUGE) -->
        <article class="card clickable" data-inspect="baro">
          <div class="card-header">
            <span class="card-title"><span class="i-icon">⏲</span>BAROMETER &amp; FORECAST</span>
            <span class="badge info" id="baro-trend-badge">Change: +0.1 hPa/h</span>
          </div>

          <div class="gauge-aside-row">
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
              <text x="120" y="84" class="baro-big" id="env-qnh">1017</text>
              <text x="120" y="98" class="baro-unit" id="env-qnh-unit">hPa</text>
            </svg>

            <!-- Subtle vertical legend aside -->
            <div class="aside-key">
              <span><i class="key-solid-line"></i>Now</span>
              <span><i class="key-dash-line"></i>3h Ago</span>
            </div>
          </div>

          <!-- Enriched Sager 12–24h Prediction Box (System + Wind Shift + Outlook + Rain Probability %) -->
          <div class="sager-forecast-box" id="baro-forecast-banner">
            <div class="sager-top">
              <strong class="fb-text" id="env-zambretti">Fine Weather</strong>
              <span class="rain-prob-pill" id="baro-rain-prob">Rain Chance 12%</span>
            </div>
            <div class="sager-sub">
              <span id="baro-front-lbl">High-Pressure Ridge · Wind Steady</span>
              <span id="baro-minmax">24h: 1015–1017 hPa</span>
            </div>
            <div class="sager-sub">
              <span id="baro-outlook-lbl">12h Outlook: Light / Steady Breeze · Steady &amp; Fair</span>
            </div>
            <div class="mbar-track"><div class="mbar-fill cyan" id="baro-rain-prob-fill" style="width:12%"></div></div>
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
              <div class="rain-hero-num">
                <span class="big-num" id="rain-rate">0.0</span>
                <span class="sub-lbl" id="rain-rate-unit">mm/h · Dry</span>
              </div>
              <div class="spark-clean">
                <div class="spark-foot">
                  <span>Precipitation Rate (mm/h)</span>
                  <span class="phase-legend" title="Stacked liquid-equivalent precipitation rate (mm/h)">
                    <span><i class="ph-dot rain-ph"></i>Rain</span>
                    <span><i class="ph-dot hail-ph"></i>Hail</span>
                  </span>
                </div>
                <svg viewBox="0 0 320 76" class="spark-svg" id="spark-rain"></svg>
              </div>
              <!-- Visual Hailstone Size & Impact Energy Strip (Proportional Circles) -->
              <div class="hail-strip">
                <div class="hail-info">
                  <span class="hail-kicker">Hailstone Size &amp; Impact</span>
                  <strong id="rain-hail">0 hits · Max Ø 0.0 mm</strong>
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

        <!-- 5. SOLAR & UV RADIATION (DEDICATED CARD: IRRADIANCE, LUX, UV INDEX, BURN TIME & VIS/IR HEAT) -->
        <article class="card clickable" data-inspect="solar">
          <div class="card-header">
            <span class="card-title"><span class="i-icon">☀</span>SOLAR &amp; UV INDEX</span>
            <span class="badge info" id="daylight-badge">Burn ~37 min</span>
          </div>

          <div class="thermo-readouts">
            <div>
              <div class="rain-hero-num">
                <span class="big-num" id="env-solar">642</span>
                <span class="sub-lbl" id="env-solar-unit">W/m²</span>
              </div>
              <span class="sub-lbl" id="env-lux">Brightness: 81,250 lx</span>
            </div>
          </div>

          <!-- UV Index Rainbow Bar -->
          <div class="spectrum-box">
            <div class="uv-top">
              <span id="uv-left-lbl">UV Index <b class="accent" id="env-uv">5.4 · Moderate</b></span>
              <span class="muted-xs" id="uv-sub-row">UVA 25.9 · UVB 1.8 W/m²</span>
            </div>
            <div class="uv-rainbow-bar" id="uv-or-bortle-bar">
              <div class="uv-dot" id="uv-dot" style="left:47%"></div>
            </div>
            <div class="spectrum-scale" id="uv-scale-ticks">
              <span>0 Low</span><span>3 Mod</span><span>6 High</span><span>8 Very High</span><span>11+ Ext</span>
            </div>
          </div>

          <!-- Bottom Stacked Horizontal UV — Visible — IR Power Bar -->
          <div class="mbar">
            <div class="mbar-top" id="spec-split-lbls">
              <span><i class="sp-dot uv"></i>UV <b id="sp-uv">28</b></span>
              <span><i class="sp-dot vis"></i>Visible <b id="sp-vis">292</b></span>
              <span><i class="sp-dot nir"></i>IR Heat <b id="sp-nir">322 W/m²</b></span>
            </div>
            <div class="spec-split-bar">
              <div class="sp-seg uv" id="bar-sp-uv" style="width:6%"></div>
              <div class="sp-seg vis" id="bar-sp-vis" style="width:45%"></div>
              <div class="sp-seg nir" id="bar-sp-nir" style="width:49%"></div>
            </div>
          </div>
        </article>

        <!-- 6. SKY, CLOUDS & SUN / MOON (CLOUD % AS HERO, CLOUD BASE IN TOP RIGHT, STARGAZING AT NIGHT) -->
        <article class="card clickable" data-inspect="solar">
          <div class="card-header">
            <span class="card-title"><span class="i-icon">☁</span>SKY, CLOUDS &amp; MOON</span>
            <span class="badge info" id="sky-base-badge">Cloud Base: 910 m</span>
          </div>

          <div class="thermo-readouts">
            <div>
              <div class="rain-hero-num">
                <span class="big-num" id="cloud-cover-lbl">24%</span>
                <span class="sub-lbl" id="sky-cond-big">Clouds · Scattered</span>
              </div>
              <span class="sub-lbl" id="sky-moon-sub">Moon 74% Illuminated · Waxing Gibbous</span>
            </div>
          </div>

          <!-- Night Sky Darkness / Stargazing Scale (Visible After Sunset / Dark Mode Scenarios) -->
          <div class="spectrum-box compact" id="stargazing-box" style="display:none">
            <div class="uv-top">
              <span id="bortle-left-lbl">Stargazing <b class="accent" id="sky-bortle-val">Bortle 2 · Milky Way</b></span>
              <span class="muted-xs" id="sky-sqm-val">SQM 21.4 mag/arcsec²</span>
            </div>
            <div class="uv-rainbow-bar bortle-mode">
              <div class="uv-dot" id="bortle-dot" style="left:18%"></div>
            </div>
            <div class="spectrum-scale">
              <span>1 Dark</span><span>3 Rural</span><span>5 Suburb</span><span>7 Bright</span><span>9 City</span>
            </div>
          </div>

          <!-- Spacious 24h Sun & Moon Dual-Lane Horizon Bar -->
          <div class="mbar sky24-box">
            <div class="mbar-top">
              <span class="hz-icon-lbl" title="Sunrise & Sunset">
                <svg viewBox="0 0 16 12" width="15" height="12" class="hz-svg"><line x1="1" y1="10" x2="15" y2="10" stroke="#f59e0b" stroke-width="1.5"/><path d="M 4 10 A 4 4 0 0 1 12 10" fill="none" stroke="#facc15" stroke-width="1.5"/><polyline points="6,4 8,1.5 10,4" fill="none" stroke="#facc15" stroke-width="1.4"/></svg>
                <span>Sun <b id="sun-window">06:48–18:24</b></span>
              </span>
              <span class="hz-icon-lbl" title="Moonrise, Moonset & Illumination">
                <svg viewBox="0 0 14 14" width="13" height="13" class="hz-svg"><path d="M 10.5 9.8 A 4.8 4.8 0 0 1 5.2 2.2 A 5.0 5.0 0 1 0 11.8 8.8 Z" fill="#93c5fd"/></svg>
                <span>Moon <b id="moon-window">14:15–02:40 · 74%</b></span>
              </span>
            </div>
            <div class="sky24-track">
              <div class="sky24-sun-span" id="sky24-sun-span" style="left:28%; width:48%"></div>
              <div class="sky24-moon-span" id="sky24-moon-span" style="left:59%; width:36%"></div>
              <div class="sky24-dot sun" id="sky24-sun-dot" style="left:58%" title="Sun Position"></div>
              <div class="sky24-dot moon" id="sky24-moon-dot" style="left:72%" title="Moon Position"></div>
            </div>
            <div class="spectrum-scale">
              <span>00:00</span><span>06:00</span><span>12:00</span><span>18:00</span><span>24:00</span>
            </div>
          </div>
        </article>

        <!-- 7. AIR QUALITY & PARTICLES (COMPACT NARROW BARS, EXPLICIT IDX UNITS, WHO HEALTH THRESHOLDS) -->
        <article class="card clickable" data-inspect="air">
          <div class="card-header">
            <span class="card-title"><span class="i-icon">🍃</span>AIR QUALITY &amp; PARTICLES</span>
            <span class="badge info" id="aq-dom-badge">Main: PM2.5</span>
          </div>

          <!-- Top Section: AQI Ring + Primary Regulated Particulates (PM2.5, PM10, Pollen/Bio) -->
          <div class="air-visual-row">
            <div class="aqi-radar-stack">
              <svg viewBox="0 0 92 92" class="aqi-ring-svg">
                <circle cx="46" cy="46" r="38" class="ring-bg" />
                <circle cx="46" cy="46" r="38" class="ring-fg-ok" id="aqi-arc" stroke-dasharray="40 239" transform="rotate(-90 46 46)" />
                <text x="46" y="44" class="aqi-big" id="aq-aqi-big">26</text>
                <text x="46" y="58" class="aqi-sub" id="aq-level-lbl">GOOD</text>
              </svg>
            </div>

            <!-- Dense Bullet-Graph List (Label | Bar with Health Tick | Value), Grouped: Particles -->
            <div class="aq-list">
              <div class="aq-group-hdr"><span>Particles</span><span>µg/m³</span></div>
              <div class="aq-row">
                <span class="aq-lbl">PM1.0</span>
                <div class="b-track" title="Tick = 10 µg/m³"><div class="b-limit" style="left:40%"></div><div class="b-fill ok" id="b-pm1" style="width:15%"></div></div>
                <b class="aq-val" id="aq-pm1">3.7</b>
              </div>
              <div class="aq-row">
                <span class="aq-lbl">PM2.5</span>
                <div class="b-track" title="Tick = WHO 24h limit (15 µg/m³)"><div class="b-limit" style="left:43%"></div><div class="b-fill ok" id="b-pm25" style="width:18%"></div></div>
                <b class="aq-val" id="aq-pm25">6.2</b>
              </div>
              <div class="aq-row">
                <span class="aq-lbl">PM4.0</span>
                <div class="b-track" title="Tick = 25 µg/m³"><div class="b-limit" style="left:50%"></div><div class="b-fill ok" id="b-pm4" style="width:16%"></div></div>
                <b class="aq-val" id="aq-pm4">8.1</b>
              </div>
              <div class="aq-row">
                <span class="aq-lbl">PM10</span>
                <div class="b-track" title="Tick = WHO 24h limit (45 µg/m³)"><div class="b-limit" style="left:50%"></div><div class="b-fill ok" id="b-pm10" style="width:14%"></div></div>
                <b class="aq-val" id="aq-pm10">12.8</b>
              </div>
              <div class="aq-row">
                <span class="aq-lbl">Pollen</span>
                <div class="b-track" title="Tick = 20 µg/m³ coarse bio-aerosol"><div class="b-limit" style="left:44%"></div><div class="b-fill ok" id="b-bio" style="width:15%"></div></div>
                <b class="aq-val" id="aq-bio">6.6</b>
              </div>
            </div>
          </div>

          <!-- Dense Bullet-Graph List, Grouped: Gases -->
          <div class="aq-list">
            <div class="aq-group-hdr"><span>Gases</span></div>
            <div class="aq-row">
              <span class="aq-lbl">CO₂</span>
              <div class="b-track" title="Tick = 1000 ppm"><div class="b-limit" style="left:52%"></div><div class="b-fill cyan" id="b-co2" style="width:18%"></div></div>
              <b class="aq-val" id="aq-co2">418 ppm</b>
            </div>
            <div class="aq-row">
              <span class="aq-lbl">VOC</span>
              <div class="b-track" title="Sensirion VOC Index (100 = avg, tick = 150)"><div class="b-limit" style="left:50%"></div><div class="b-fill ok" id="b-voc" style="width:33%"></div></div>
              <b class="aq-val" id="aq-voc">98 idx</b>
            </div>
            <div class="aq-row">
              <span class="aq-lbl">NOx</span>
              <div class="b-track" title="Sensirion NOx Index (1 = clean, tick = 20)"><div class="b-limit" style="left:25%"></div><div class="b-fill ok" id="b-nox" style="width:5%"></div></div>
              <b class="aq-val" id="aq-nox">2 idx</b>
            </div>
          </div>
        </article>

        <!-- 8. LIGHTNING (DISTANCE HERO, STRIKES IN TOP RIGHT, STORM TRACKER STRIP & 60M BAR GRAPH) -->
        <article class="card clickable" data-inspect="air">
          <div class="card-header">
            <span class="card-title"><span class="i-icon">⚡</span>LIGHTNING</span>
            <span class="badge ok" id="lt-lock-badge">0 Strikes</span>
          </div>

          <div class="thermo-readouts">
            <div>
              <div class="rain-hero-num">
                <span class="big-num" id="lt-dist-big">&gt;40</span>
                <span class="sub-lbl" id="lt-dist-unit">km</span>
              </div>
              <span class="sub-lbl" id="lt-thunder-sub">Δt — s</span>
            </div>
          </div>

          <!-- Full-Width Lightning Strikes Bar Graph (Zero Left Gap) -->
          <div class="spark-clean" id="lightning-panel">
            <div class="spark-foot">
              <span>Lightning Strikes</span>
              <span id="lt-peak-lbl">Peak 0</span>
            </div>
            <svg viewBox="0 0 320 76" class="spark-svg" id="lt-timeline-svg"></svg>
          </div>
        </article>

        <!-- 9. SEISMIC & VIBRATION (STANDARD PPV IN MM/S, AMPLITUDE & FREQUENCY HZ, LABELED X/Y VELOCITY CHART) -->
        <article class="card clickable" data-inspect="wind">
          <div class="card-header">
            <span class="card-title"><span class="i-icon">〰</span>SEISMIC &amp; VIBRATION</span>
            <span class="badge ok" id="seismic-badge">Quiet · MMI I</span>
          </div>

          <div class="thermo-readouts">
            <div>
              <div class="rain-hero-num">
                <span class="big-num" id="seis-pga-big">0.12</span>
                <span class="sub-lbl">mm/s</span>
              </div>
              <span class="sub-lbl" id="seis-freq-sub">Dominant 18 Hz</span>
            </div>
          </div>

          <!-- Peak Acceleration Amplitude (cm/s²) -->
          <div class="mbar">
            <div class="mbar-top"><span>Peak Acceleration</span><b id="seis-ground-lbl">0.4 cm/s²</b></div>
            <div class="mbar-track"><div class="mbar-fill cyan" id="seis-ground-fill" style="width:8%"></div></div>
          </div>

          <!-- Ground Vibration Velocity Chart (mm/s) with Numeric Y-Axis & Time X-Axis -->
          <div class="spark-clean">
            <div class="spark-foot">
              <span id="seis-wave-title">Ground Vibration Velocity (mm/s)</span>
              <span id="seis-freq-lbl">18 Hz</span>
            </div>
            <svg viewBox="0 0 320 76" class="spark-svg" id="spark-seismic"></svg>
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

/* CARDS & 3x3 (3+3+3) SYMMETRIC BENTO GRID */
.grid-3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 16px; }
.grid-2 { display: grid; grid-template-columns: repeat(2, 1fr); gap: 16px; }
@media (max-width: 1120px) { .grid-3 { grid-template-columns: repeat(2, 1fr); } }
@media (max-width: 740px) { .grid-3, .grid-2 { grid-template-columns: 1fr; } }

.card {
  background: var(--card); border: 1px solid var(--border);
  border-radius: 16px; padding: 13px 16px;
  box-shadow: var(--shadow); display: flex; flex-direction: column; justify-content: space-between; gap: 8px;
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

/* 1. WIND COMPASS & ZERO-FOOTPRINT CORNER KEY */
.gauge-aside-row { position: relative; display: flex; justify-content: center; align-items: center; }
.wind-compass { width: 178px; height: 178px; }
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

.aside-key {
  position: absolute; right: 0; bottom: 2px;
  display: flex; flex-direction: column; gap: 3px;
  font-size: 8.5px; color: var(--muted); opacity: 0.68; font-weight: 600;
  pointer-events: none;
}
.aside-key span { display: inline-flex; align-items: center; gap: 4px; white-space: nowrap; }
.aside-key b { color: var(--text); font-weight: 700; }
.key-arrow { width: 8px; height: 2px; background: var(--accent); border-radius: 2px; display: inline-block; }
.key-petal { width: 7px; height: 7px; background: rgba(56,189,248,0.32); border: 1px solid #38bdf8; border-radius: 2px; display: inline-block; }
.key-solid-line { width: 8px; height: 2px; background: var(--accent); border-radius: 2px; display: inline-block; }
.key-dash-line { width: 8px; height: 0; border-top: 1.5px dashed #94a3b8; display: inline-block; }

/* UNIFIED RANGE-CAPSULE BAR (MIN — ● NOW/AVG — MAX FOR WIND SPEED, WIND LOAD & TEMPERATURE) */
.range-bar-wrap { background: var(--card-inner); padding: 9px 12px; border-radius: 11px; }
.range-labels { display: flex; justify-content: space-between; font-size: 10.5px; color: var(--muted); margin-bottom: 7px; }
.rl-item { display: inline-flex; align-items: center; gap: 4px; }
.range-labels b { color: var(--text); font-weight: 800; }
.warn-txt { color: var(--warn) !important; }
.dot-now { width: 8px; height: 8px; border-radius: 50%; background: #ffffff; border: 2px solid #091120; box-shadow: 0 0 4px #ffffff; display: inline-block; }

.range-track { position: relative; height: 8px; border-radius: 99px; background: var(--border); }
.range-span {
  position: absolute; top: 0; height: 100%; border-radius: 99px;
  background: linear-gradient(90deg, rgba(56,189,248,0.32), rgba(56,189,248,0.85));
  border: 1px solid rgba(56,189,248,0.55);
  transition: left 0.3s, width 0.3s;
}
.range-span.temp-grad {
  background: linear-gradient(90deg, #38bdf8 0%, #22c55e 50%, #f59e0b 100%);
  border: none;
}
.range-dot {
  position: absolute; top: -3px; width: 14px; height: 14px; border-radius: 50%;
  background: #ffffff; border: 2.5px solid #091120; transform: translateX(-50%);
  box-shadow: 0 0 7px rgba(255,255,255,0.9); z-index: 2; transition: left 0.3s;
}

/* DUAL-LANE WIND BAR (SPEED UPPER LANE + LOAD LOWER LANE, SAME GRAMMAR AS SUN/MOON) */
.range-labels-bottom { margin: 7px 0 0; font-size: 10px; }
.lane-key { display: inline-block; width: 9px; height: 4px; border-radius: 99px; }
.lane-key.speed { background: #38bdf8; }
.lane-key.load { background: #a78bfa; }
.dual-track { position: relative; height: 16px; border-radius: 8px; background: var(--border); }
.dual-span { position: absolute; height: 5.5px; border-radius: 99px; transition: left 0.3s, width 0.3s; }
.dual-span.speed { top: 1.5px; background: linear-gradient(90deg, rgba(56,189,248,0.45), #38bdf8); }
.dual-span.load { bottom: 1.5px; background: linear-gradient(90deg, rgba(167,139,250,0.45), #a78bfa); }
.dual-dot {
  position: absolute; width: 12px; height: 12px; border-radius: 50%;
  border: 2px solid #091120; transform: translateX(-50%); z-index: 2; transition: left 0.3s;
}
.dual-dot.speed { top: -1.5px; background: #e0f2fe; box-shadow: 0 0 6px #38bdf8; }
.dual-dot.load { bottom: -1.5px; background: #ede9fe; box-shadow: 0 0 6px #a78bfa; }

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
.spectrum-box.compact { padding: 6px 10px; }
.spectrum-box.compact .uv-top { margin-bottom: 4px; font-size: 10px; }
.spectrum-box.compact .uv-rainbow-bar { height: 6px; }
.spectrum-box.compact .spectrum-scale { margin-top: 3px; font-size: 8.5px; }
.today-range-box { background: var(--card-inner); padding: 9px 12px; border-radius: 11px; }
.today-range-hdr { display: flex; justify-content: space-between; align-items: center; font-size: 10.5px; color: var(--muted); margin-bottom: 7px; }
.today-range-hdr b { color: var(--text); font-weight: 800; }
.today-range-track { position: relative; height: 8px; border-radius: 99px; background: var(--border); }
.today-range-fill {
  position: absolute; top: 0; height: 100%; border-radius: 99px;
  background: linear-gradient(90deg, #38bdf8 0%, #22c55e 50%, #f59e0b 100%);
}
.spec-pin {
  position: absolute; top: -3px; width: 14px; height: 14px; border-radius: 50%;
  transform: translateX(-50%); border: 2.5px solid #091120;
}
.temp-pin { background: #ffffff; box-shadow: 0 0 7px #ffffff; z-index: 2; }

.thermo-bento-2x2 {
  display: grid; grid-template-columns: 1fr 1fr; gap: 8px;
}
.thermo-bento-3 {
  display: grid; grid-template-columns: repeat(3, 1fr); gap: 7px;
}
.tb-cell {
  background: var(--card-inner); padding: 7px 9px; border-radius: 10px;
  display: flex; flex-direction: column; gap: 2px;
}
.tb-kicker { font-size: 9.5px; color: var(--muted); font-weight: 700; }
.tb-val { font-size: 16px; font-weight: 800; color: var(--text); line-height: 1.15; }
.tb-val-sm { font-size: 12.5px; font-weight: 800; color: var(--text); line-height: 1.2; }
.tb-sub { font-size: 9.5px; color: var(--muted); font-weight: 600; }
.spectrum-scale { display: flex; justify-content: space-between; font-size: 9px; color: var(--muted); margin-top: 6px; }

.curious-strip {
  display: grid; grid-template-columns: repeat(3, 1fr); gap: 6px;
  background: var(--card-inner); padding: 7px 10px; border-radius: 10px;
}
.cs-item { display: flex; flex-direction: column; gap: 2px; }
.cs-lbl { font-size: 9.5px; color: var(--muted); font-weight: 600; }
.cs-item strong { font-size: 11px; color: var(--text); font-weight: 700; }

.mini-bars-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
.mbar { background: var(--card-inner); padding: 7px 10px; border-radius: 10px; }
.mbar-top { display: flex; justify-content: space-between; font-size: 10.5px; color: var(--muted); margin-bottom: 5px; }
.mbar-top b { color: var(--text); }
.mbar-track { height: 5px; border-radius: 99px; background: var(--border); overflow: hidden; }
.mbar-fill { height: 100%; border-radius: 99px; }
.mbar-fill.cyan { background: var(--accent); }
.mbar-fill.green { background: var(--ok); }

/* 3. BAROMETER SAGER 4-SENSOR FORECAST BOX & DIAL */
.sager-forecast-box {
  display: flex; flex-direction: column; gap: 5px;
  padding: 8px 11px; border-radius: 10px;
  background: var(--card-inner); border: 1px solid var(--border);
}
.sager-top { display: flex; justify-content: space-between; align-items: center; }
.fb-text { font-size: 12.5px; font-weight: 800; color: var(--text); }
.rain-prob-pill { font-size: 10.5px; font-weight: 800; color: var(--accent); }
.sager-sub { display: flex; justify-content: space-between; font-size: 9.5px; color: var(--muted); font-weight: 600; }

.baro-dial-wrap { display: flex; justify-content: center; }
.baro-dial { width: 232px; height: 120px; }
.dial-lbl { fill: var(--muted); font-size: 8.5px; font-weight: 700; }
.baro-big { fill: var(--text); font-size: 21px; font-weight: 800; text-anchor: middle; }
.baro-unit { fill: var(--muted); font-size: 8.5px; font-weight: 600; text-anchor: middle; }
/* Unified card chart block (Rain, Lightning, Seismic): title row + fixed-aspect SVG, never stretched */
.spark-clean { padding: 0; flex: 0 0 auto; }
.spark-svg { width: 100%; height: auto; display: block; }
.spark-foot { display: flex; justify-content: space-between; align-items: center; font-size: 10.5px; color: var(--muted); margin-bottom: 3px; font-weight: 600; }

/* Vertical distribution: first block pinned top, last block pinned bottom, free space shared between */
.grid-3 > .card > :first-child { margin-top: 0; }
.grid-3 > .card > :last-child { margin-bottom: 0; }

/* 4. RAIN CYLINDER (COMPACT HEIGHT-AWARE FLEX STRETCH) */
.rain-visual-row { flex: 1; display: flex; align-items: stretch; gap: 14px; }
.rain-tube-wrap { display: flex; flex-direction: column; align-items: center; gap: 3px; align-self: stretch; }
.rain-tube {
  position: relative; width: 38px; flex: 1; min-height: 92px; border-radius: 10px 10px 16px 16px;
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
.rain-right { flex: 1; display: flex; flex-direction: column; justify-content: space-between; gap: 6px; align-self: stretch; }
.rain-hero-row { display: flex; justify-content: space-between; align-items: baseline; }
.rain-hero-num { display: flex; align-items: baseline; gap: 8px; }
.phase-legend { display: flex; gap: 9px; font-size: 10px; color: var(--muted); font-weight: 700; }
.phase-legend span { display: inline-flex; align-items: center; gap: 4px; }
.ph-dot { width: 8px; height: 8px; border-radius: 2px; display: inline-block; }
.ph-dot.rain-ph { background: #38bdf8; }
.ph-dot.hail-ph { background: #c084fc; }


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

/* 5. SOLAR, UV & NIGHT SKY (COMPACT BENTO LAYOUT MATCHING CARD 2) */
.sp-dot { width: 7px; height: 7px; border-radius: 2px; display: inline-block; margin-right: 3px; }
.sp-dot.uv,  .sp-seg.uv  { background: #a855f7; }
.sp-dot.uvb, .sp-seg.uvb { background: #f43f5e; }
.sp-dot.vis, .sp-seg.vis { background: #38bdf8; }
.sp-dot.nir, .sp-seg.nir { background: #f59e0b; }
.spec-split-bar { display: flex; height: 5px; border-radius: 99px; overflow: hidden; background: var(--border); gap: 1px; }
.sp-seg { height: 100%; transition: width 0.3s; }

.uv-top { display: flex; justify-content: space-between; align-items: center; font-size: 10.5px; color: var(--muted); margin-bottom: 8px; }
.uv-top b { color: var(--text); }
.uv-rainbow-bar {
  position: relative; height: 10px; border-radius: 99px;
  background: linear-gradient(90deg, #22c55e 0%, #eab308 35%, #f97316 65%, #ef4444 85%, #a855f7 100%);
}
.uv-rainbow-bar.bortle-mode {
  background: linear-gradient(90deg, #1e293b 0%, #1d4ed8 32%, #38bdf8 62%, #facc15 86%, #f97316 100%);
}
.uv-dot {
  position: absolute; top: -2.5px; width: 15px; height: 15px; border-radius: 50%;
  background: #ffffff; border: 2.5px solid #091120; transform: translateX(-50%);
  box-shadow: 0 0 8px rgba(255,255,255,0.9); transition: left 0.3s;
}
.uv-band-row {
  display: flex; justify-content: space-between; font-size: 10.5px;
  color: var(--muted); margin-top: 7px;
}
.uv-band-row span { display: inline-flex; align-items: center; gap: 4px; }
.uv-band-row b { color: var(--text); }
.p-dot.uv-a-dot { background: #a855f7; }
.p-dot.uv-b-dot { background: #f43f5e; }

.hz-icon-lbl { display: inline-flex; align-items: center; gap: 4px; }
.hz-svg { display: inline-block; vertical-align: middle; }
.sky24-box { padding: 10px 12px; }
.sky24-track {
  position: relative; height: 16px; border-radius: 8px;
  background: var(--border); margin: 8px 0 6px;
}
.sky24-sun-span {
  position: absolute; top: 1.5px; height: 5.5px; border-radius: 99px;
  background: linear-gradient(90deg, #f59e0b, #facc15, #f97316);
}
.sky24-moon-span {
  position: absolute; bottom: 1.5px; height: 5.5px; border-radius: 99px;
  background: linear-gradient(90deg, #38bdf8, #818cf8);
}
.sky24-dot {
  position: absolute; width: 12px; height: 12px; border-radius: 50%;
  transform: translateX(-50%); transition: left 0.3s; z-index: 2;
}
.sky24-dot.sun {
  top: -1.5px; background: #facc15; border: 2px solid #091120; box-shadow: 0 0 6px #facc15;
}
.sky24-dot.moon {
  bottom: -1.5px; background: #93c5fd; border: 2px solid #091120; box-shadow: 0 0 6px #60a5fa;
}

/* 7. AIR QUALITY & NARROW HEALTH-THRESHOLD BARS */
.air-visual-row { display: flex; align-items: center; gap: 11px; }
.aqi-radar-stack { display: flex; flex-direction: column; align-items: center; }
.aqi-ring-svg { width: 82px; height: 82px; }
.aqi-big { fill: var(--text); font-size: 21px; font-weight: 800; text-anchor: middle; }
.aqi-sub { fill: var(--ok); font-size: 8px; font-weight: 800; text-anchor: middle; }

.bullet-stack { flex: 1; display: flex; flex-direction: column; gap: 5px; }
.aq-gases-3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 6px; }
.b-row { background: var(--card-inner); padding: 4px 8px; border-radius: 7px; }
.b-hdr { display: flex; justify-content: space-between; font-size: 9.5px; color: var(--muted); margin-bottom: 3px; white-space: nowrap; }
.b-hdr b { color: var(--text); font-weight: 700; }
.b-track { position: relative; height: 4px; border-radius: 99px; background: var(--border); }
.b-fill { height: 100%; border-radius: 99px; transition: width 0.3s; }
.b-fill.ok { background: var(--ok); }
.b-fill.cyan { background: var(--accent); }
.b-fill.warn { background: var(--warn); }
.b-fill.danger { background: var(--danger); }
.b-limit { position: absolute; top: -2px; width: 2px; height: 8px; background: var(--text); opacity: 0.7; }

/* Dense bullet-graph list (Few / Tufte): label | bar + health tick | right-aligned value, no per-row boxes */
.aq-list { flex: 1; display: flex; flex-direction: column; gap: 3px; background: var(--card-inner); border-radius: 10px; padding: 6px 10px; }
.card > .aq-list { flex: 0 0 auto; }
.aq-group-hdr { display: flex; justify-content: space-between; font-size: 9px; font-weight: 800; color: var(--muted); letter-spacing: 0.4px; text-transform: uppercase; margin-bottom: 1px; }
.aq-row { display: grid; grid-template-columns: 44px 1fr 58px; align-items: center; gap: 8px; height: 15px; }
.aq-lbl { font-size: 10px; color: var(--muted); font-weight: 700; white-space: nowrap; }
.aq-val { font-size: 10.5px; color: var(--text); font-weight: 800; text-align: right; font-variant-numeric: tabular-nums; white-space: nowrap; }

.lt-hdr { display: flex; justify-content: space-between; align-items: center; font-size: 10.5px; margin-bottom: 3px; }
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
// 60 x 1-minute chart series from the station (/api/history/minutes); Home Assistant keeps its own history
let minuteHist = null;
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
      return loadMinuteHistory();
    })
    .then(() => fetch("/api/state").then(r => r.json()))
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
  const minMaxEl = document.getElementById("baro-minmax");
  if (!minMaxEl || historyPoints.length < 2) return;
  const u = UNIT_SYSTEMS[unitIdx];
  const recent = historyPoints.slice(-24);
  const vals = recent.map(p => p.qnh * u.pMul);
  const minV = Math.min(...vals), maxV = Math.max(...vals);
  const dec = u.pUnit === "inHg" ? 2 : 0;
  minMaxEl.textContent = `24h: ${minV.toFixed(dec)}–${maxV.toFixed(dec)} ${u.pUnit}`;
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

// Modified Mercalli Intensity from peak ground acceleration (Wald et al. 1999 PGA bands)
function mmiFromPgaMg(mg) {
  if (mg < 1.7) return "MMI I";
  if (mg < 14) return "MMI II–III";
  if (mg < 39) return "MMI IV";
  if (mg < 92) return "MMI V";
  if (mg < 180) return "MMI VI";
  return "MMI VII+";
}

function renderWaveAndSeismic(d) {
  const seisSvg = document.getElementById("spark-seismic");
  if (seisSvg && d) {
    const s = d.seismic || { ground_pga_mg: 0.4, wind_sway_mg: 0.8, gyro_mdps: 8.2, dominant_hz: 18.0, source: "Quiet Foundation", mmi: "Quiet · MMI I", tilt_deg: 0.38 };
    // Convert acceleration (mg) & dominant frequency (Hz) to standard DIN 4150 / Raspberry Shake Peak Particle Velocity PPV (mm/s)
    // and Peak Ground Acceleration (cm/s^2 = Gal, where 1 mg = 0.981 cm/s^2)
    const pgaGal = s.ground_pga_mg * 0.981;
    const freqHz = Math.max(1.0, s.dominant_hz);
    const ppvMmS = Math.max(0.05, (pgaGal * 10.0) / (2.0 * Math.PI * freqHz));
    const maxAxisV = ppvMmS >= 1.0 ? 3.0 : 0.5;

    document.getElementById("seismic-badge").textContent = mmiFromPgaMg(s.ground_pga_mg);
    document.getElementById("seismic-badge").className = s.ground_pga_mg >= 2.0 ? "badge warn" : "badge ok";
    document.getElementById("seis-pga-big").textContent = ppvMmS.toFixed(2);
    const srcLbl = (!s.source || s.source === "Quiet Foundation") ? "Ambient" : s.source;
    document.getElementById("seis-freq-sub").textContent = `Dominant ${freqHz.toFixed(0)} Hz · ${srcLbl}`;
    document.getElementById("seis-ground-lbl").textContent = `${pgaGal.toFixed(1)} cm/s²`;
    document.getElementById("seis-ground-fill").style.width = `${clampPct(pgaGal, 0, 8.0).toFixed(0)}%`;
    document.getElementById("seis-freq-lbl").textContent = `Peak ±${ppvMmS.toFixed(2)} mm/s`;

    // 60-minute peak-velocity envelope: one mirrored ±PPV bar per minute.
    // A raw 18–24 Hz waveform is unreadable over 60 min, so seismic/vibration monitors plot per-interval peaks.
    const frame = chartFrame([`+${maxAxisV.toFixed(1)}`, "0", `-${maxAxisV.toFixed(1)}`], ["-60m", "-30m", "Now"], true);
    const { xStart, w, bw } = minuteBarGeom(frame);
    const halfH = CH.yMid - CH.yTop;
    const peaks = minuteSeries(minuteHist && minuteHist.ppv_mm_s, ppvMmS);
    const col = s.ground_pga_mg >= 1.5 ? "#f59e0b" : "#22c55e";
    seisSvg.innerHTML = frame.html + peaks.map((v, i) => {
      const h = Math.max(1.0, Math.min(1.0, v / maxAxisV) * halfH);
      const x = xStart + i * w;
      return `<rect x="${x.toFixed(1)}" y="${(CH.yMid - h).toFixed(1)}" width="${bw.toFixed(1)}" height="${(2 * h).toFixed(1)}" rx="0.8" fill="${col}" fill-opacity="${i === MINUTE_BINS - 1 ? 1.0 : 0.75}"/>`;
    }).join("");
  }
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
  const svg = document.getElementById("lt-timeline-svg");
  if (!svg) return;

  const bins = minuteSeries(minuteHist && minuteHist.strikes);
  const peakStrikes = Math.max(...bins);
  const maxB = Math.max(2, Math.ceil(peakStrikes * 1.15));
  const midB = Math.round(maxB * 0.5);
  const frame = chartFrame([maxB, midB, "0"], ["-60m", "-30m", "Now"]);
  const { xStart, w, bw } = minuteBarGeom(frame);
  const yTop = CH.yTop;
  const yBot = CH.yBot;
  document.getElementById("lt-peak-lbl").textContent = `Peak ${peakStrikes}`;

  let html = frame.html;

  html += bins.map((cnt, i) => {
    const h = cnt > 0 ? Math.max(5.0, (cnt / maxB) * (yBot - yTop)) : 3.0;
    const x = xStart + i * w;
    const col = cnt >= 3 ? "#f59e0b" : (cnt > 0 ? "#38bdf8" : "rgba(142,162,198,0.38)");
    return `<rect x="${x.toFixed(1)}" y="${(yBot - h).toFixed(1)}" width="${bw.toFixed(1)}" height="${h.toFixed(1)}" rx="0.8" fill="${col}"/>`;
  }).join("");
  svg.innerHTML = html;

  const strikes = d.lightning ? d.lightning.strikes : 0;
  document.getElementById("lt-lock-badge").textContent = `${strikes} ${strikes === 1 ? "Strike" : "Strikes"}`;
  document.getElementById("lt-lock-badge").className = strikes > 0 ? "badge warn" : "badge ok";
  if (strikes > 0) {
    if (d.environment && d.environment.thunder_locked && d.environment.thunder_dist_km > 0) {
      const tDelay = d.environment.thunder_delay_s || 16.9;
      const distKm = d.environment.thunder_dist_km;
      document.getElementById("lt-dist-big").textContent = distKm.toFixed(2);
      document.getElementById("lt-dist-unit").textContent = "km";
      document.getElementById("lt-thunder-sub").textContent = `Δt ${tDelay.toFixed(1)} s`;
    } else {
      document.getElementById("lt-dist-big").textContent = `~${d.lightning.distance_km}`;
      document.getElementById("lt-dist-unit").textContent = "km";
      document.getElementById("lt-thunder-sub").textContent = "Δt — s";
    }
  } else {
    document.getElementById("lt-dist-big").textContent = ">40";
    document.getElementById("lt-dist-unit").textContent = "km";
    document.getElementById("lt-thunder-sub").textContent = "Δt — s";
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
  const wlCeil = Math.max(4.0 * wlMul, wlMax * 1.25);
  document.getElementById("wload-min").textContent = wlMin.toFixed(wlDec);
  document.getElementById("wload-trend-lbl").textContent = wlNow.toFixed(wlDec);
  document.getElementById("wload-max").textContent = `${wlMax.toFixed(wlDec)} ${wlUnit}`;
  const wlMinP = clampPct(wlMin, 0, wlCeil);
  const wlMaxP = clampPct(wlMax, 0, wlCeil);
  document.getElementById("wload-span").style.left = `${wlMinP.toFixed(0)}%`;
  document.getElementById("wload-span").style.width = `${Math.max(6, wlMaxP - wlMinP).toFixed(0)}%`;
  document.getElementById("wload-now-dot").style.left = `${clampPct(wlNow, 0, wlCeil).toFixed(0)}%`;
  if (d.wind.rose) renderRosePetals(d.wind.rose);

  // 2. Thermal & Humidity (Single 24h Range Bar + 3-Column Dew / Wet-Bulb / Abs Humidity Row)
  const curT = u.tFn(d.environment.temp_c);
  const feelsT = u.tFn(d.environment.feels_like_c);
  const dewT = u.tFn(d.environment.dew_point_c);
  const wetT = u.tFn(d.environment.wet_bulb_c !== undefined ? d.environment.wet_bulb_c : (d.environment.temp_c * 0.65 + d.environment.dew_point_c * 0.35));

  document.getElementById("env-temp").textContent = `${curT.toFixed(1)}°`;
  document.getElementById("env-feels").textContent = `Feels like ${feelsT.toFixed(1)}° outside`;
  document.getElementById("env-rh").textContent = `${Math.round(d.environment.humidity_rh)}%`;
  const rhCirc = (d.environment.humidity_rh / 100) * 220;
  document.getElementById("rh-arc").setAttribute("stroke-dasharray", `${rhCirc.toFixed(0)} 220`);

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

  // 3-Column Single Row: Dew Point, Wet-Bulb, Absolute Humidity
  document.getElementById("env-dew").textContent = `${dewT.toFixed(1)}°`;
  const dewSpread = Math.max(0, d.environment.temp_c - d.environment.dew_point_c);
  const dewSpreadDisp = Math.abs(curT - dewT);
  document.getElementById("fog-risk-lbl").textContent =
    dewSpread <= 2.0 ? `Fog risk (${dewSpreadDisp.toFixed(1)}°)` : `${dewSpreadDisp.toFixed(1)}° gap`;

  document.getElementById("env-wetbulb").textContent = `${wetT.toFixed(1)}°`;
  const wbC = d.environment.wet_bulb_c !== undefined ? d.environment.wet_bulb_c : 14.0;
  document.getElementById("wetbulb-sub").textContent =
    wbC <= 0.5 ? "Snow (< 0.5°)" : (wbC >= 24.0 ? "Heat limit" : "Rain phase");

  const esHpa = 6.112 * Math.exp((17.67 * d.environment.temp_c) / (d.environment.temp_c + 243.5));
  const eHpa = esHpa * (d.environment.humidity_rh / 100.0);
  const absHum = (216.7 * eHpa) / (d.environment.temp_c + 273.15);
  document.getElementById("abs-hum-lbl").textContent = `${absHum.toFixed(1)} g/m³`;
  const muggyTxt = absHum >= 16.5 ? "Humid air" : (absHum >= 9.0 ? "Pleasant" : "Crisp air");
  document.getElementById("abs-hum-sub").textContent = muggyTxt;

  const tC = d.environment.temp_c;
  document.getElementById("comfort-badge").textContent =
    d.environment.frost_risk ? "Frost Warning" : (tC > 30 ? "Heat Stress" : (absHum > 16.5 ? "Humid" : "Comfortable"));
  document.getElementById("comfort-badge").className =
    d.environment.frost_risk || tC > 30 ? "badge warn" : "badge ok";

  // 3. Barometer & Enriched Sager Forecast Box (No Extra Boxes Below Gauge)
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

  const synD3h = d.environment.synoptic_3h_delta_hpa !== undefined ? d.environment.synoptic_3h_delta_hpa : d3h;
  const windOut = synD3h <= -2.5 ? `Gales / Squalls (${cardDir})` : (synD3h <= -0.8 ? `Freshening (${cardDir})` : (synD3h >= 1.2 ? "Brisk, Then Easing" : `Light / Steady (${cardDir})`));
  const tempOut = synD3h <= -2.0 ? "Cooling w/ Rain" : (synD3h >= 1.2 ? "Cooler & Clearing" : "Steady & Fair");
  document.getElementById("baro-outlook-lbl").textContent = `12h Outlook: ${windOut} · ${tempOut}`;
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
    d.rain.hail_hits > 0 ? `${d.rain.hail_hits} hits · Max Ø ${hailDiam.toFixed(1)} mm` : "0 hits · Max Ø 0.0 mm";
  document.querySelectorAll("#hail-scale .hs-item").forEach((el) => {
    const mm = parseFloat(el.dataset.mm);
    const active = hailDiam > 0 && Math.abs(hailDiam - mm) <= 3.5;
    el.className = active ? "hs-item active" : "hs-item";
  });

  // 5. Solar & UV Index (Card 5) + 6. Sky, Clouds & Moon (Card 6)
  const isNight = Boolean(d.environment.is_night);
  const uvi = d.environment.uv_index;
  const cloudPct = d.environment.cloud_cover_pct !== undefined ? d.environment.cloud_cover_pct : 20;
  document.getElementById("cloud-cover-lbl").textContent = `${Math.round(cloudPct)}%`;
  const moonIllum = (d.astronomy && d.astronomy.moon_illum_pct !== undefined) ? d.astronomy.moon_illum_pct : 74;
  const moonPhase = (d.astronomy && d.astronomy.moon_phase) ? d.astronomy.moon_phase : "Waxing Gibbous";
  document.getElementById("sky-moon-sub").textContent = `Moon ${moonIllum}% Illuminated · ${moonPhase}`;
  document.getElementById("sun-window").textContent = "06:48–18:24";
  document.getElementById("moon-window").textContent = `14:15–02:40 · ${moonIllum}%`;
  document.getElementById("sky24-sun-dot").style.left = isNight ? "12%" : "58%";
  document.getElementById("sky24-moon-dot").style.left = isNight ? "82%" : "66%";

  const cBaseM = d.environment.cloud_base_m || Math.max(80, (d.environment.temp_c - d.environment.dew_point_c) * 125);
  const cBaseStr = unitIdx === 1 ? `${Math.round(cBaseM * 3.28084).toLocaleString()} ft` : `${Math.round(cBaseM)} m`;
  document.getElementById("sky-base-badge").textContent = `Cloud Base: ${cBaseStr}`;
  const cloudDesc = cloudPct >= 80 ? (d.rain.raining ? "Heavy Wet Cloud" : "Overcast") : (cloudPct >= 50 ? "Broken Clouds" : (cloudPct >= 20 ? "Scattered" : "Clear Sky"));
  document.getElementById("sky-cond-big").textContent = `Clouds · ${cloudDesc}`;

  // Stargazing box only shown when it's dark / after sunset!
  const sgBox = document.getElementById("stargazing-box");
  if (isNight) {
    sgBox.style.display = "block";
    const bortle = d.environment.bortle || 2;
    const sqm = d.environment.sqm || 21.4;
    const bDesc = bortle <= 2 ? `Bortle ${bortle} · Milky Way` : `Bortle ${bortle} · Night Sky`;
    document.getElementById("sky-bortle-val").textContent = bDesc;
    document.getElementById("sky-sqm-val").textContent = `SQM ${sqm.toFixed(1)} mag/arcsec²`;
    document.getElementById("bortle-dot").style.left = `${clampPct(bortle, 1, 9).toFixed(0)}%`;
  } else {
    sgBox.style.display = "none";
  }

  const burnMin = d.environment.burn_time_min || 0;
  document.getElementById("daylight-badge").textContent =
    burnMin > 0 ? `Burn ~${burnMin} min` : "No Burn Risk";
  document.getElementById("daylight-badge").className = uvi >= 6 ? "badge warn" : "badge ok";

  if (isNight) {
    const mlx = d.environment.lux * 1000.0;
    document.getElementById("env-solar").textContent = mlx < 10 ? mlx.toFixed(2) : mlx.toFixed(0);
    document.getElementById("env-solar-unit").textContent = "mlx (Night)";
    document.getElementById("env-lux").textContent = `Night Illuminance (${d.environment.lux.toFixed(4)} lx)`;
  } else {
    document.getElementById("env-solar").textContent = d.environment.solar_w_m2.toFixed(0);
    document.getElementById("env-solar-unit").textContent = "W/m²";
    document.getElementById("env-lux").textContent = `Brightness: ${Math.round(d.environment.lux).toLocaleString()} lx`;
  }

  const uvCat = uvi >= 8 ? "Very High" : (uvi >= 6 ? "High" : (uvi >= 3 ? "Moderate" : "Low"));
  const uvaW = d.environment.uva_w_m2 !== undefined ? d.environment.uva_w_m2 : uvi * 4.8;
  const uvbW = d.environment.uvb_w_m2 !== undefined ? d.environment.uvb_w_m2 : uvi * 0.32;
  document.getElementById("uv-left-lbl").innerHTML = `UV Index <b class="accent">${uvi.toFixed(1)} · ${uvCat}</b>`;
  document.getElementById("uv-sub-row").textContent = `UVA ${uvaW.toFixed(1)} · UVB ${uvbW.toFixed(1)} W/m²`;
  document.getElementById("uv-dot").style.left = `${clampPct(uvi, 0, 11.5).toFixed(0)}%`;

  const uvTot = uvaW + uvbW;
  const visW = d.environment.visible_w_m2 !== undefined ? d.environment.visible_w_m2 : d.environment.solar_w_m2 * 0.45;
  const nirW = d.environment.nir_w_m2 !== undefined ? d.environment.nir_w_m2 : d.environment.solar_w_m2 * 0.50;
  const sumW = Math.max(1.0, uvTot + visW + nirW);
  document.getElementById("sp-uv").textContent = uvTot.toFixed(0);
  document.getElementById("sp-vis").textContent = visW.toFixed(0);
  document.getElementById("sp-nir").textContent = `${nirW.toFixed(0)} W/m²`;
  document.getElementById("bar-sp-uv").style.width = `${Math.max(5, (uvTot / sumW) * 100).toFixed(0)}%`;
  document.getElementById("bar-sp-vis").style.width = `${((visW / sumW) * 100).toFixed(0)}%`;
  document.getElementById("bar-sp-nir").style.width = `${((nirW / sumW) * 100).toFixed(0)}%`;

  // 7. Air Quality & Particles (Grouped Particles vs Separated Gases with Health Thresholds)
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
  const compCo2 = d.air_quality.comp_co2_ppm || d.air_quality.co2_ppm;
  const vocVal = d.air_quality.voc_index || 100;
  const noxVal = d.air_quality.nox_index || 1;

  document.getElementById("aq-pm1").textContent = `${pm1Val.toFixed(1)}`;
  document.getElementById("b-pm1").style.width = `${clampPct(pm1Val, 0, 25).toFixed(0)}%`;
  document.getElementById("b-pm1").className = `b-fill ${pm1Val <= 10 ? "ok" : "warn"}`;
  document.getElementById("aq-pm25").textContent = `${dryPm25.toFixed(1)}`;
  document.getElementById("b-pm25").style.width = `${clampPct(dryPm25, 0, 35).toFixed(0)}%`;
  document.getElementById("b-pm25").className = `b-fill ${dryPm25 <= 15 ? "ok" : "warn"}`;
  document.getElementById("aq-pm4").textContent = `${pm4Val.toFixed(1)}`;
  document.getElementById("b-pm4").style.width = `${clampPct(pm4Val, 0, 50).toFixed(0)}%`;
  document.getElementById("b-pm4").className = `b-fill ${pm4Val <= 25 ? "ok" : "warn"}`;
  document.getElementById("aq-pm10").textContent = `${dryPm10.toFixed(1)}`;
  document.getElementById("b-pm10").style.width = `${clampPct(dryPm10, 0, 90).toFixed(0)}%`;
  document.getElementById("b-pm10").className = `b-fill ${dryPm10 <= 45 ? "ok" : "warn"}`;
  document.getElementById("aq-bio").textContent = `${coarseBio.toFixed(1)}`;
  document.getElementById("b-bio").style.width = `${clampPct(coarseBio, 0, 45).toFixed(0)}%`;
  document.getElementById("b-bio").className = `b-fill ${coarseBio < 20 ? "ok" : "warn"}`;

  document.getElementById("aq-co2").textContent = `${compCo2} ppm`;
  document.getElementById("b-co2").style.width = `${clampPct(compCo2, 380, 1600).toFixed(0)}%`;
  document.getElementById("b-co2").className = `b-fill ${compCo2 <= 1000 ? "cyan" : "warn"}`;
  document.getElementById("aq-voc").textContent = `${vocVal} idx`;
  document.getElementById("b-voc").style.width = `${clampPct(vocVal, 0, 300).toFixed(0)}%`;
  document.getElementById("b-voc").className = `b-fill ${vocVal <= 150 ? "ok" : "warn"}`;
  document.getElementById("aq-nox").textContent = `${noxVal} idx`;
  document.getElementById("b-nox").style.width = `${clampPct(noxVal, 0, 80).toFixed(0)}%`;
  document.getElementById("b-nox").className = `b-fill ${noxVal <= 20 ? "ok" : "warn"}`;

  renderSparkBaro();
  renderSparkRain();
  renderLightningTimeline(d);
  renderWaveAndSeismic(d);

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

function loadMinuteHistory() {
  return fetch("/api/history/minutes").then(r => r.json()).then(m => {
    if (m && Array.isArray(m.rain_mm_h)) {
      minuteHist = m;
      if (lastState) applyTelemetry(lastState);
    }
  }).catch(() => {});
}

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
  loadMinuteHistory();
  setInterval(loadMinuteHistory, 15000);
  connectWs();
});
)WEBUI";
static constexpr std::size_t app_js_size = sizeof(app_js_data) - 1;

}  // namespace web_assets
}  // namespace firmware
}  // namespace station
// clang-format on
