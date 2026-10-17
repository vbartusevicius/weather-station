document.querySelectorAll(".tab").forEach((btn) => {
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
