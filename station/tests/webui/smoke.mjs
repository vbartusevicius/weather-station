// Web UI smoke test: runs the real station/data/app.js (Node vm + a stub DOM built from index.html's ids) against
// the simulator (station/scripts/mock_webui_server.py) in every scenario and both unit systems. Fails on any
// exception and on renderers that produce nothing (applyTelemetry errors are swallowed in the browser, so a throw
// there only shows up as cards that silently stop updating).
//
//   python3 station/scripts/mock_webui_server.py &      # http://127.0.0.1:8765
//   node station/tests/webui/smoke.mjs [base-url]
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const BASE = process.argv[2] || process.env.SMOKE_URL || "http://127.0.0.1:8765";
const DATA = join(dirname(fileURLToPath(import.meta.url)), "../../data");
const SCENARIOS = ["breeze", "storm", "freeze", "heatwave", "plume", "train", "night_dark", "night_moon"];

// ---- stub DOM: every id resolves (ids created inside innerHTML, e.g. the chart crosshair, too)
function makeNode(id) {
  return {
    id, textContent: "", innerHTML: "", value: "", checked: false, disabled: false, placeholder: "", className: "",
    style: {}, dataset: {}, files: [],
    classList: { add() {}, remove() {}, toggle() {}, contains() { return false; } },
    addEventListener() {}, removeEventListener() {}, setAttribute() {}, getAttribute() { return null; },
    appendChild() {}, focus() {}, getContext() { return null; },
    getBoundingClientRect() { return { left: 0, top: 0, width: 1100, height: 400 }; },
    querySelector() { return makeNode("q"); }, querySelectorAll() { return []; }, closest() { return null; },
  };
}
const nodes = {};
const el = (id) => (nodes[id] ??= makeNode(id));
for (const m of readFileSync(join(DATA, "index.html"), "utf8").matchAll(/id="([^"]+)"/g)) el(m[1]);
const store = {};
const context = vm.createContext({
  console, Math, JSON, Date, Number, String, Array, Object, Set, Map, Promise, isFinite, parseFloat, parseInt,
  document: {
    getElementById: el, querySelector: () => null, querySelectorAll: () => [], createElement: () => makeNode("x"), addEventListener() {},
    documentElement: makeNode("html"), body: makeNode("body"),
  },
  location: { protocol: "http:", host: "127.0.0.1", hash: "", reload() {} },
  navigator: {},
  localStorage: { getItem: (k) => store[k] ?? null, setItem: (k, v) => { store[k] = String(v); } },
  fetch: () => new Promise(() => {}),   // the page's own requests never resolve; the test feeds data directly
  WebSocket: function WebSocket() { return { addEventListener() {}, close() {} }; },
  XMLHttpRequest: function XMLHttpRequest() { return { open() {}, send() {}, upload: {} }; },
  setInterval: () => 0, clearInterval() {}, setTimeout: () => 0, clearTimeout() {}, requestAnimationFrame: () => 0,
  confirm: () => true, alert() {}, matchMedia: () => ({ matches: false, addEventListener() {} }),
  addEventListener() {},
});
context.window = context;
vm.runInContext(readFileSync(join(DATA, "app.js"), "utf8"), context, { filename: "app.js" });
const run = (code) => vm.runInContext(code, context);

// ---- helpers
let failures = 0;
function check(cond, what) {
  if (!cond) {
    failures += 1;
    console.error(`  FAIL ${what}`);
  }
}
async function get(path) {
  const r = await fetch(BASE + path);
  if (!r.ok) throw new Error(`${path}: HTTP ${r.status}`);
  return r.json();
}
async function post(path, body) {
  const r = await fetch(BASE + path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  return r.json();
}

const ranges = run("Object.entries(TREND_RANGES)");
const allSeries = run("TREND_SERIES.map((s) => s.id)");
const metricKeys = run("[...new Set(TREND_SERIES.filter((s) => s.key).map((s) => s.key))].join(',')");

for (const scenario of SCENARIOS) {
  console.log(`scenario ${scenario}`);
  await post("/api/action", { action: "set_scenario", scenario });
  context.__state = await get("/api/state");
  context.__config = await get("/api/config");
  context.__cal = await get("/api/calibration");
  context.__records = await get("/api/records");
  context.__daily = await get("/api/history?tier=day&n=31&metrics=temp_c,rain_mm,wind_m_s,wind_dir_deg,qnh_hpa,uv_index,solar_w_m2,strikes");
  for (const unit of [0, 1]) {
    try {
      run(`unitIdx = ${unit}; applyTelemetry(__state); renderSetupStatus(__config); renderCal(__cal);`);
    } catch (e) {
      check(false, `applyTelemetry / setup / calibration (units ${unit}): ${e.stack || e}`);
      continue;
    }
    check(el("health-list").innerHTML.includes("health-item"), `health list rendered (units ${unit})`);
    check(el("rs485-decode").innerHTML.includes("rs-field"), "RS-485 frame decoded");
  }
  run("unitIdx = 0;");
  // Trends: every range, all lane series (capped to 6 units by the picker logic) + the wind rose
  for (const [range, [tier, n]] of ranges) {
    context.__hist = await get(`/api/history?tier=${tier}&n=${n}&metrics=${metricKeys}`);
    context.__rose = await get(`/api/windrose?range=${range}`);
    check(context.__hist.points === n + 1, `${range}: ${n + 1} points (got ${context.__hist.points})`);
    try {
      run(`trendRange = "${range}"; trendTier = "${tier}"; trendN = ${n};
           trendSel = ["temp", "dew", "rh", "wind", "gust", "dir", "rain", "raintot", "qnh", "rose"];
           trendData = __hist; dailyData = __daily; recordsData = __records; roseData = __rose;
           renderTrends(); showHover(3);`);
    } catch (e) {
      check(false, `${range}: Trends render: ${e.stack || e}`);
      continue;
    }
    const chart = el("trend-chart").innerHTML;
    check((chart.match(/class="lane-bg"/g) || []).length === 6, `${range}: 6 lanes drawn`);
    check(chart.includes("<polyline"), `${range}: temperature line drawn`);
    check(el("trend-tip").innerHTML.includes("Temperature <b>"), `${range}: hover read-out`);
    check(context.__rose.minutes === 0 || el("rose-svg").innerHTML.includes("<polygon"), `${range}: wind rose drawn`);
  }
  check(el("daily-table").innerHTML.includes("<td"), "daily summary rows");
  check(el("records-table").innerHTML.includes("Highest temperature"), "records table");
}
// picker: chips = legend, a 7th unit is refused, a whole family can be added from the popover
run(`trendSel = ["temp", "rh", "wind", "dir", "rain", "qnh"]; renderPicker();`);
check(el("add-list").innerHTML.includes("disabled"), "7th unit blocked in the Add metric list");
check((el("trend-chips").innerHTML.match(/class="sel-chip"/g) || []).length === 6, "selected metrics shown as chips");
run(`trendSel = ["temp"]; pickerClick({ target: { closest: (q) => (q === "[data-family]" ? { dataset: { family: "air" } } : null) } });`);
check(run("['pm1', 'pm25', 'pm10', 'co2', 'voc', 'nox'].every((x) => trendSel.includes(x))"), "Add metric: whole family added");
check(allSeries.includes("rose"), "wind rose is a picker entry");
// connection badge reflects real data, and no mock-up numbers remain in the page source
check(/LIVE/.test(el("pill-link-txt").textContent), "connection badge LIVE after station data");
const html = readFileSync(join(DATA, "index.html"), "utf8");
check(!/id="(hero-temp|hc-wind|env-qnh|aq-co2)"[^>]*>[^<]*\d/.test(html), "no hard-coded readings in index.html");

if (failures) {
  console.error(`${failures} check(s) failed`);
  process.exit(1);
}
console.log("Web UI smoke test passed");
