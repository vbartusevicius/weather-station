"""Live Mocked Backend Server for AeroMet V1 LittleFS WebUI.

Serves station/data/{index.html,app.css,app.js}, REST endpoints (/api/state,
/api/history, /api/config, /api/action), and a native RFC 6455 WebSocket (/ws)
streaming 1 Hz physics-modeled telemetry across 5 interactive weather presets.
"""

from __future__ import annotations

import base64
import hashlib
import inspect
import json
import math
import socket
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

_SCRIPT_FILE = Path(inspect.getfile(lambda: None)).resolve()
DATA_DIR = _SCRIPT_FILE.parents[1] / "data"

SCENARIOS = {
    "breeze": {
        "wind_ms": 4.8,
        "wind_dir": 215.0,
        "gust_ms": 7.2,
        "temp_c": 18.4,
        "rh": 62.5,
        "baro_hpa": 1009.0,
        "delta3h": 0.25,
        "lux": 81250.0,
        "uv": 5.4,
        "rain_rate": 0.0,
        "rain_daily": 2.4,
        "rain_state": "Dry",
        "raining": False,
        "wet": False,
        "hail": 0,
        "pm25": 6.2,
        "pm10": 12.8,
        "voc": 98,
        "nox": 2,
        "co2": 418,
        "strikes": 0,
        "dist_km": 0,
    },
    "storm": {
        "wind_ms": 18.6,
        "wind_dir": 255.0,
        "gust_ms": 27.4,
        "temp_c": 14.2,
        "rh": 93.0,
        "baro_hpa": 988.5,
        "delta3h": -4.6,
        "lux": 6200.0,
        "uv": 0.6,
        "rain_rate": 38.4,
        "rain_daily": 28.6,
        "rain_state": "Hail",
        "raining": True,
        "wet": True,
        "hail": 7,
        "pm25": 19.0,
        "pm10": 32.0,
        "voc": 82,
        "nox": 4,
        "co2": 414,
        "strikes": 14,
        "dist_km": 6,
    },
    "freeze": {
        "wind_ms": 6.4,
        "wind_dir": 22.0,
        "gust_ms": 9.8,
        "temp_c": -4.2,
        "rh": 96.5,
        "baro_hpa": 1018.2,
        "delta3h": 1.6,
        "lux": 14500.0,
        "uv": 1.1,
        "rain_rate": 0.0,
        "rain_daily": 0.4,
        "rain_state": "Dry",
        "raining": False,
        "wet": True,
        "hail": 0,
        "pm25": 31.0,
        "pm10": 44.0,
        "voc": 90,
        "nox": 6,
        "co2": 424,
        "strikes": 0,
        "dist_km": 0,
    },
    "heatwave": {
        "wind_ms": 2.1,
        "wind_dir": 165.0,
        "gust_ms": 3.5,
        "temp_c": 34.8,
        "rh": 46.0,
        "baro_hpa": 1006.4,
        "delta3h": -0.5,
        "lux": 118000.0,
        "uv": 10.2,
        "rain_rate": 0.0,
        "rain_daily": 0.0,
        "rain_state": "Dry",
        "raining": False,
        "wet": False,
        "hail": 0,
        "pm25": 14.5,
        "pm10": 29.0,
        "voc": 145,
        "nox": 12,
        "co2": 416,
        "strikes": 0,
        "dist_km": 0,
    },
    "plume": {
        "wind_ms": 3.2,
        "wind_dir": 95.0,
        "gust_ms": 5.1,
        "temp_c": 21.0,
        "rh": 58.0,
        "baro_hpa": 1011.2,
        "delta3h": 0.1,
        "lux": 48000.0,
        "uv": 3.8,
        "rain_rate": 0.0,
        "rain_daily": 0.0,
        "rain_state": "Dry",
        "raining": False,
        "wet": False,
        "hail": 0,
        "pm25": 42.0,
        "pm10": 78.0,
        "voc": 265,
        "nox": 185,
        "co2": 468,
        "strikes": 0,
        "dist_km": 0,
    },
}


class MockState:
    def __init__(self) -> None:
        self.scenario = "breeze"
        self.start_ts = time.time()
        self.rain_daily_override: float | None = None
        self.force_heater = False
        self.config = {
            "altitude_m": 65.0,
            "declination_deg": 6.2,
            "dt_ns_offset_us": 0.0,
            "dt_ew_offset_us": 0.0,
            "mqtt_uri": "mqtt://192.168.1.10:1883",
            "ha_prefix": "homeassistant",
        }


STATE = MockState()


def compute_snapshot() -> dict:
    sc = SCENARIOS.get(STATE.scenario, SCENARIOS["breeze"])
    t_now = time.time()
    uptime_s = int(t_now - STATE.start_ts) + 1231200
    wave = math.sin(t_now * 0.7)
    wave2 = math.cos(t_now * 0.45)

    wind_ms = max(0.0, sc["wind_ms"] + 0.35 * wave)
    wind_dir = (sc["wind_dir"] + 4.0 * wave2) % 360.0
    gust_ms = max(wind_ms, sc["gust_ms"] + 0.4 * abs(wave))
    lull_ms = max(0.2, wind_ms * 0.62)

    solar_w = sc["lux"] * 0.0079
    solar_offset_c = (0.0006 * solar_w) / (max(0.25, wind_ms) ** 0.65) if solar_w > 10 else 0.0
    temp_c = sc["temp_c"] + 0.08 * wave
    raw_temp_c = temp_c + solar_offset_c
    rh = sc["rh"] + 0.3 * wave2

    gamma = math.log(rh / 100.0) + (17.625 * temp_c) / (243.04 + temp_c)
    dew_c = (243.04 * gamma) / (17.625 - gamma)
    wet_c = (
        temp_c * math.atan(0.151977 * math.sqrt(rh + 8.313659))
        + math.atan(temp_c + rh)
        - math.atan(rh - 1.676331)
        + 0.00391838 * (rh**1.5) * math.atan(0.023101 * rh)
        - 4.686035
    )
    tg_c = temp_c + (0.014 * solar_w) / (max(0.5, wind_ms) ** 0.45)
    wbgt_c = 0.7 * wet_c + 0.2 * tg_c + 0.1 * temp_c

    es_kpa = 0.61078 * math.exp((17.27 * temp_c) / (temp_c + 237.3))
    vpd_kpa = max(0.0, es_kpa * (1.0 - rh / 100.0))
    cloud_base_m = max(0.0, 124.7 * (temp_c - dew_c))

    if temp_c <= 10.0 and wind_ms * 3.6 > 4.8:
        v_pow = (wind_ms * 3.6) ** 0.16
        feels_c = 13.12 + 0.6215 * temp_c - 11.37 * v_pow + 0.3965 * temp_c * v_pow
    else:
        e_hpa = (rh / 100.0) * 6.105 * math.exp((17.27 * temp_c) / (237.7 + temp_c))
        feels_c = temp_c + 0.348 * e_hpa - 0.70 * wind_ms + solar_w * 0.0045 - 4.25

    rho = (sc["baro_hpa"] * 100.0) / (287.058 * (temp_c + 273.15))
    dyn_offset_hpa = 0.22 * 0.5 * rho * (wind_ms**2) * 0.01 if wind_ms > 0.5 else 0.0
    static_hpa = sc["baro_hpa"] + dyn_offset_hpa
    alt_m = float(STATE.config["altitude_m"])
    qnh_hpa = static_hpa * math.exp(0.0341626 * alt_m / (temp_c + 273.15 + 0.0065 * alt_m * 0.5))

    delta3h = sc["delta3h"]
    if delta3h <= -3.0:
        trend_str = "Falling Rapidly"
        forecast = "Stormy, Heavy Rain & Gusts"
    elif delta3h <= -1.1:
        trend_str = "Falling"
        forecast = "Showery, Becoming Unsettled"
    elif delta3h >= 1.1:
        trend_str = "Rising"
        forecast = "Fine, Improving"
    else:
        trend_str = "Steady"
        forecast = "Fine Weather"

    if rh > 70.0:
        aw = min(0.94, rh / 100.0)
        gf = 1.0 + 0.35 * (aw / (1.0 - aw))
        dry_pm25 = sc["pm25"] / gf
        dry_pm10 = sc["pm10"] / gf
    else:
        dry_pm25 = sc["pm25"]
        dry_pm10 = sc["pm10"]

    aqi_pm25 = round((dry_pm25 / 9.0) * 50) if dry_pm25 <= 9.0 else round(51 + (dry_pm25 - 9.0) / 26.4 * 49)
    aqi_nox = round((sc["nox"] / 20.0) * 50) if sc["nox"] <= 20 else round(101 + (sc["nox"] - 75) / 75.0 * 49)
    aqi = max(aqi_pm25, aqi_nox)
    dom = "NOx" if aqi_nox > aqi_pm25 else "PM2.5"
    level = "Good" if aqi <= 50 else ("Moderate" if aqi <= 100 else "Unhealthy")
    eaqi = 1 if aqi <= 50 else (2 if aqi <= 100 else 4)
    comp_co2 = round(sc["co2"] * (1013.25 / static_hpa))

    heating = temp_c <= 2.5 or STATE.force_heater
    vin_v = 24.12 + 0.03 * wave
    p_full = (vin_v**2) / 25.0
    duty_pct = (4.0 / p_full) * 100.0 if heating else 0.0
    power_w = 4.0 if heating else 0.0

    dom_sec = int(((wind_dir + 11.25) % 360.0) / 22.5) % 16
    rose = [2.0] * 16
    rose[dom_sec] = 36.5
    rose[(dom_sec - 1) % 16] = 21.0
    rose[(dom_sec + 1) % 16] = 18.5
    rose[(dom_sec - 2) % 16] = 8.0

    c_sound = math.sqrt(1.4 * 287.058 * (temp_c + 273.15 + 0.45))
    rad = math.radians(wind_dir)
    vn = -wind_ms * math.cos(rad)
    ve = -wind_ms * math.sin(rad)
    l_eff = 0.11015
    cos_a = 0.027 / 0.05507
    t_ns = (l_eff / (c_sound - vn * cos_a)) * 1e6
    t_sn = (l_eff / (c_sound + vn * cos_a)) * 1e6
    t_ew = (l_eff / (c_sound - ve * cos_a)) * 1e6
    t_we = (l_eff / (c_sound + ve * cos_a)) * 1e6

    kc = max(0.05, min(1.0, solar_w / 850.0))
    n_kc = 100.0 * (((1.0 - kc) / 0.75) ** (1.0 / 3.4)) if kc < 1.0 else 0.0
    uv_vis = (sc["uv"] * 105.0) / max(20.0, solar_w)
    cloud_cover_pct = max(0.0, min(100.0, n_kc + max(-10.0, min(25.0, (uv_vis - 1.05) * 45.0))))

    rain_daily = STATE.rain_daily_override if STATE.rain_daily_override is not None else sc["rain_daily"]
    payload = f"$AM,{uptime_s},{wind_ms:.2f},{wind_dir:.1f},{sc['rain_rate']:.2f},{rain_daily:.2f},{temp_c:.2f},{rh:.1f},{qnh_hpa:.2f},{sc['uv']:.1f},{aqi}"

    return {
        "fw": "26.10.19",
        "uptime_s": uptime_s,
        "wifi": {"external_ufl": True, "tx_power_dbm": 20.5, "rssi_dbm": -56},
        "tof": {"ns": t_ns, "sn": t_sn, "ew": t_ew, "we": t_we},
        "rs485_frame": f"{payload}*4F2A",
        "wind": {
            "speed_m_s": wind_ms,
            "dir_deg": wind_dir,
            "avg_1m_m_s": wind_ms * 0.94,
            "avg_10m_m_s": wind_ms * 0.89,
            "gust_m_s": gust_ms,
            "lull_m_s": lull_ms,
            "mean_dir_deg": sc["wind_dir"],
            "beaufort": min(12, int(wind_ms / 2.2) + 1),
            "turbulence_pct": 14.2 if wind_ms < 10 else 24.8,
            "gust_factor": gust_ms / max(0.5, wind_ms * 0.89),
            "crosswind_corr_pct": 50.0 * ((wind_ms / c_sound) ** 2),
            "sound_m_s": c_sound,
            "acoustic_temp_c": temp_c + 0.45,
            "tilt_deg": 0.38,
            "north_offset_deg": float(STATE.config["declination_deg"]),
            "rose": rose,
        },
        "rain": {
            "state": sc["rain_state"],
            "raining": sc["raining"],
            "wet": sc["wet"],
            "rate_mm_h": sc["rain_rate"],
            "event_mm": sc["rain_daily"] * 0.65,
            "hourly_mm": sc["rain_rate"] * 0.4,
            "daily_mm": rain_daily,
            "total_mm": 142.8 + rain_daily,
            "drops": 4812 + (uptime_s % 500 if sc["raining"] else 0),
            "hail_hits": sc["hail"],
            "hail_diam_mm": 9.4 if sc["hail"] > 0 else 0.0,
            "rejected_shake": 19,
        },
        "environment": {
            "temp_c": temp_c,
            "raw_temp_c": raw_temp_c,
            "solar_offset_c": solar_offset_c,
            "humidity_rh": rh,
            "dew_point_c": dew_c,
            "wet_bulb_c": wet_c,
            "wbgt_c": wbgt_c,
            "feels_like_c": feels_c,
            "wind_chill_c": feels_c,
            "heat_index_c": feels_c,
            "vpd_kpa": vpd_kpa,
            "cloud_base_m": cloud_base_m,
            "frost_risk": temp_c <= 3.0 and dew_c <= 0.5,
            "pressure_hpa": static_hpa,
            "dyn_offset_hpa": dyn_offset_hpa,
            "qnh_hpa": qnh_hpa,
            "pressure_3h_delta_hpa": delta3h,
            "pressure_trend": trend_str,
            "forecast": forecast,
            "lux": sc["lux"],
            "solar_w_m2": solar_w,
            "uv_index": sc["uv"],
            "cloud_cover_pct": cloud_cover_pct,
            "abs_humidity_g_m3": 9.8,
        },
        "air_quality": {
            "aqi": aqi,
            "eaqi": eaqi,
            "level": level,
            "dominant": dom,
            "pm1_0": sc["pm25"] * 0.6,
            "pm2_5": sc["pm25"],
            "dry_pm2_5": dry_pm25,
            "pm4_0": sc["pm25"] * 1.3,
            "pm10": sc["pm10"],
            "dry_pm10": dry_pm10,
            "voc_index": sc["voc"],
            "nox_index": sc["nox"],
            "co2_ppm": sc["co2"],
            "comp_co2_ppm": comp_co2,
        },
        "lightning": {
            "strikes": sc["strikes"],
            "distance_km": sc["dist_km"],
            "energy": 48200 if sc["strikes"] > 0 else 0,
            "timeline_60m": [0, 0, 1, 0, 2, 1, 3, 2, 4, 3, 2, 1] if sc["strikes"] > 0 else [0] * 12,
        },
        "astronomy": {
            "sunrise": "06:48",
            "sunset": "18:24",
            "daylight": "11h 36m",
            "sun_progress": 0.62 if sc["lux"] > 20000 else 0.35,
        },
        "heater": {
            "state": 1 if heating else 0,
            "vin_v": vin_v,
            "reflector_c": 6.5 if heating else temp_c,
            "duty_pct": duty_pct,
            "power_w": power_w,
        },
    }


def build_history() -> list[dict]:
    sc = SCENARIOS.get(STATE.scenario, SCENARIOS["breeze"])
    snap = compute_snapshot()
    base_qnh = snap["environment"]["qnh_hpa"]
    pts = []
    for i in range(48):
        phase = (i / 48.0) * 2.0 * math.pi
        t = sc["temp_c"] - 2.5 * math.cos(phase)
        dew = t - 6.2 + 0.6 * math.sin(phase)
        feels = t - 0.8 + 0.5 * math.sin(phase)
        w = max(0.4, sc["wind_ms"] + 1.8 * math.sin(phase * 1.5))
        g = w * 1.48 + 0.6 * abs(math.cos(phase * 2.0))
        qnh = base_qnh - sc["delta3h"] * ((47 - i) / 16.0) + 0.6 * math.sin(phase)
        if sc["raining"]:
            # Continuous squall building over the last 60 minutes right up to Now (i=47)
            env = 0.35 + 0.65 * (0.5 + 0.5 * math.cos((47 - i) * 0.14))
            rr = sc["rain_rate"] * env
            hail_lwe = rr * 0.28 if (sc["hail"] > 0 and i >= 36) else 0.0
        else:
            rr = 1.6 * math.sin(((i - 10) / 10.0) * math.pi) if 10 <= i <= 20 else 0.0
            hail_lwe = 0.0
        pts.append(
            {
                "temp": t,
                "dew": dew,
                "feels": feels,
                "wind": w,
                "gust": g,
                "qnh": qnh,
                "rain_rate": max(0.0, rr),
                "hail_lwe_rate": max(0.0, hail_lwe),
            }
        )
    return pts


def send_ws_text(sock: socket.socket, text: str) -> None:
    raw = text.encode("utf-8")
    n = len(raw)
    if n < 126:
        hdr = struct.pack("!BB", 0x81, n)
    elif n < 65536:
        hdr = struct.pack("!BBH", 0x81, 126, n)
    else:
        hdr = struct.pack("!BBQ", 0x81, 127, n)
    sock.sendall(hdr + raw)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args: object) -> None:
        return

    def _send_json(self, obj: object) -> None:
        raw = json.dumps(obj).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _send_file(self, filename: str, mime: str) -> None:
        raw = (DATA_DIR / filename).read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self) -> None:
        if self.path == "/ws" and self.headers.get("Upgrade", "").lower() == "websocket":
            key = self.headers.get("Sec-WebSocket-Key", "")
            accept = base64.b64encode(
                hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()
            ).decode("ascii")
            self.send_response(101, "Switching Protocols")
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", accept)
            self.end_headers()
            try:
                while True:
                    send_ws_text(self.request, json.dumps(compute_snapshot()))
                    time.sleep(1.0)
            except OSError:
                return
        if self.path in ("/", "/index.html"):
            self._send_file("index.html", "text/html; charset=utf-8")
        elif self.path == "/app.css":
            self._send_file("app.css", "text/css; charset=utf-8")
        elif self.path == "/app.js":
            self._send_file("app.js", "application/javascript; charset=utf-8")
        elif self.path == "/api/state":
            self._send_json(compute_snapshot())
        elif self.path == "/api/history":
            self._send_json(build_history())
        elif self.path == "/api/config":
            self._send_json(STATE.config)
        else:
            self.send_error(404)

    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8") if length > 0 else "{}"
        try:
            data = json.loads(body)
        except json.JSONDecodeError:
            data = {}
        if self.path == "/api/config":
            STATE.config.update(data)
            self._send_json({"ok": True, "config": STATE.config})
        elif self.path == "/api/action":
            act = data.get("action", "")
            if act == "set_scenario":
                STATE.scenario = data.get("scenario", "breeze")
                STATE.rain_daily_override = None
                STATE.force_heater = False
            elif act in ("reset_rain", "btn-reset-rain"):
                STATE.rain_daily_override = 0.0
            elif act == "btn-heat-test":
                STATE.force_heater = not STATE.force_heater
            self._send_json({"ok": True, "action": act, "scenario": STATE.scenario})
        else:
            self.send_error(404)


def main() -> None:
    srv = ThreadingHTTPServer(("127.0.0.1", 8765), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print("AeroMet V1 Mocked Observatory WebUI listening on http://127.0.0.1:8765", flush=True)
    while True:
        time.sleep(3600)


if __name__ == "__main__":
    main()
