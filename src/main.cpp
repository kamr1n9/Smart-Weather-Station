/**
 * Smart Weather Node
 * - BME280 over I2C (SDA=9, SCL=10)
 * - Voltage divider (R1=27k top, R2=100k bottom) -> ADC on GPIO17
 * - Button on GPIO3, falling-edge ISR (placeholder)
 * - Dual-core:
 *     Core 0 → sensor acquisition + battery gauge
 *     Core 1 → WiFi SoftAP + HTTP server (HTML + /api JSON)
 * - Web UI with fetch() polling
 */

#include "main.h"

static portMUX_TYPE g_serial_mux = portMUX_INITIALIZER_UNLOCKED;

void debugPrint(const String& message) {
    String complete = message + "\r\n";  // Prepare the message with newline before entering critical section
    portENTER_CRITICAL(&g_serial_mux);
    Serial.print(complete);  // Use print instead of println since newline is already added
    Serial.flush();          // Ensure the message is sent completely
    portEXIT_CRITICAL(&g_serial_mux);
}

Telemetry    g_data;
portMUX_TYPE g_data_mux = portMUX_INITIALIZER_UNLOCKED;

Adafruit_BME280 bme;        // I2C, comment this if using BMP280
// Adafruit_BMP280 bme;        // I2C, comment this if using BME280
WebServer      server(WIFI_PORT);

static TaskHandle_t s_sensorTaskHandle  = nullptr;
static TaskHandle_t s_networkTaskHandle = nullptr;

/* ------------------------------ WEBSITE ------------------------------ */
static const char* HTML_PAGE = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
  <title>Smart Weather Node</title>
  <style>
    :root{
      --bg:#0b0f14;--card:#121923;--fg:#e9eef5;--muted:#97a7b6;--acc:#5eb1ff;
      --good:#7dd87d;--warn:#ffd166;--bad:#ff6b6b;--stroke:#1b2430
    }
    *{box-sizing:border-box}
    html,body{height:100%}
    body{
      margin:0;color:var(--fg);
      -webkit-font-smoothing:antialiased;-moz-osx-font-smoothing:grayscale;
      font:16px/1.4 system-ui,Segoe UI,Roboto,Ubuntu;
      background:
        radial-gradient(1200px 600px at 20% -10%, #132030 0%, transparent 60%),
        radial-gradient(900px 500px at 120% 10%, #09131e 0%, transparent 55%),
        linear-gradient(135deg,#0b0f14 0%,#0d131b 100%) fixed;
    }
    header{
      padding:18px 14px;text-align:center;border-bottom:1px solid var(--stroke);
      background:rgba(0,0,0,.25);backdrop-filter:blur(6px)
    }
    h1{margin:0;font-size:1.35rem;letter-spacing:.3px}
    .sub{
      font-size:.92rem;color:var(--muted);
      display:flex;gap:8px;justify-content:center;align-items:center;flex-wrap:wrap
    }
    .live{display:inline-flex;align-items:center;gap:7px}
    .dot{width:9px;height:9px;border-radius:50%;background:var(--acc);box-shadow:0 0 14px var(--acc);animation:pulse 1.8s ease-in-out infinite}
    @keyframes pulse{0%,100%{transform:scale(.9);opacity:.7}50%{transform:scale(1);opacity:1}}

    main{max-width:980px;margin:22px auto;padding:0 14px}
    .grid{display:grid;gap:16px;grid-template-columns:repeat(4,1fr)}
    @media (max-width:1100px){.grid{grid-template-columns:repeat(3,1fr)}}
    @media (max-width:900px){.grid{grid-template-columns:repeat(2,1fr)}}
    @media (max-width:520px){.grid{grid-template-columns:1fr}}

    .card{
      background:linear-gradient(180deg,rgba(18,25,35,.85),rgba(15,21,30,.85));
      border:1px solid var(--stroke);border-radius:18px;padding:14px 14px 12px;
      box-shadow:0 10px 30px rgba(0,0,0,.28);
      transition:transform .18s ease, border-color .18s ease; min-width:0;
    }
    .card:hover{transform:translateY(-2px);border-color:#243140}

    /* Header row inside card: icon + label + status chip on one line */
    .rowline{
      display:flex;align-items:center;gap:10px;margin-bottom:8px;min-width:0
    }
    .left{display:flex;align-items:center;gap:10px;min-width:0;flex:1}
    .icon{
      width:34px;height:34px;border-radius:12px;flex:0 0 auto;
      display:grid;place-items:center;
      background:radial-gradient(160% 120% at 30% 20%,#162538 0%,#0f1723 100%);
      border:1px solid #203040
    }
    .icon svg{width:20px;height:20px;opacity:.95}
    .label{
      color:var(--muted);font-size:.82rem;letter-spacing:.28px;text-transform:uppercase;
      white-space:nowrap
    }
    .chip{
      display:inline-flex;align-items:center;padding:3px 10px;border-radius:999px;
      border:1px solid #223244;background:#0e1620;color:var(--muted);
      font-size:.78rem;line-height:1;white-space:nowrap;margin-left:auto
    }
    /* status colors */
    .ok{color:var(--good);border-color:rgba(125,216,125,.35)}
    .warn{color:var(--warn);border-color:rgba(255,209,102,.35)}
    .bad{color:var(--bad);border-color:rgba(255,107,107,.35)}

    .kpi{display:flex;align-items:baseline;gap:10px;margin-top:2px;min-width:0}
    .value{font-size:2.05rem;font-weight:700;line-height:1;min-width:0}
    .unit{font-size:1rem;color:var(--muted)}
    .value.pulse{animation:flash .35s ease}
    @keyframes flash{0%{filter:brightness(1.1)}100%{filter:brightness(1)}}

    .row{display:flex;justify-content:space-between;align-items:baseline;gap:10px;margin-bottom:8px;min-width:0}
    .batterybar{height:9px;border-radius:999px;background:#0e1620;overflow:hidden;border:1px solid #1b2835}
    .batterybar>div{height:100%;width:0%;background:linear-gradient(90deg,var(--acc),#7dd3fc)}
    .brand{color:var(--acc);font-weight:600}
    a{color:var(--acc);text-decoration:none}

    /* Micro-typography for small screens */
    @media (max-width:380px){
      .value{font-size:1.8rem}
      .chip{font-size:.72rem;padding:3px 8px}
      .icon{width:30px;height:30px}
      .icon svg{width:18px;height:18px}
    }
  </style>
</head>
<body>
  <header>
    <h1>Smart Weather Node Dashboard</h1>
    <div class="sub">
      <span class="live"><span class="dot"></span> Live telemetry</span>
      <span>•</span>
      <span>Updated: <strong id="updated">—</strong></span>
      <span>•</span>
      <span id="devname" class="brand"></span>
    </div>
  </header>

  <main>
    <div class="grid">

      <!-- Temperature -->
      <div class="card">
        <div class="rowline">
          <div class="left">
            <div class="icon" aria-hidden="true">
              <!-- Thermometer -->
              <svg viewBox="0 0 24 24" fill="none"><path d="M10 13.5V5a2 2 0 1 1 4 0v8.5a4 4 0 1 1-4 0Z" stroke="currentColor" stroke-width="1.6"/><path d="M12 7v7.5" stroke="currentColor" stroke-width="1.6"/></svg>
            </div>
            <div class="label">Temperature</div>
          </div>
          <span id="t_state" class="chip">—</span>
        </div>
        <div class="kpi">
          <div id="temp" class="value">--</div><div class="unit">°C</div>
        </div>
      </div>

      <!-- Pressure -->
      <div class="card">
        <div class="rowline">
          <div class="left">
            <div class="icon" aria-hidden="true">
              <!-- Barometer / gauge -->
              <svg viewBox="0 0 24 24" fill="none"><circle cx="12" cy="12" r="7.5" stroke="currentColor" stroke-width="1.6"/><path d="M12 12l4-3" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/><circle cx="12" cy="12" r="1.2" fill="currentColor"/></svg>
            </div>
            <div class="label">Pressure</div>
          </div>
          <span id="p_state" class="chip">—</span>
        </div>
        <div class="kpi">
          <div id="press" class="value">--</div><div class="unit">hPa</div>
        </div>
      </div>

      <!-- Humidity -->
      <div class="card">
        <div class="rowline">
          <div class="left">
            <div class="icon" aria-hidden="true">
              <!-- Droplet -->
              <svg viewBox="0 0 24 24" fill="none"><path d="M12 3c4.5 5 7 8.2 7 11a7 7 0 1 1-14 0c0-2.8 2.5-6 7-11Z" stroke="currentColor" stroke-width="1.6"/></svg>
            </div>
            <div class="label">Humidity</div>
          </div>
          <span id="h_state" class="chip">—</span>
        </div>
        <div class="kpi">
          <div id="hum" class="value">--</div><div class="unit">%</div>
        </div>
      </div>

      <!-- Battery -->
      <div class="card">
        <div class="rowline">
          <div class="left">
            <div class="icon" aria-hidden="true">
              <!-- Battery -->
              <svg viewBox="0 0 24 24" fill="none"><rect x="3" y="7" width="16" height="10" rx="2" stroke="currentColor" stroke-width="1.6"/><rect x="19" y="10" width="2" height="4" rx="1" fill="currentColor"/></svg>
            </div>
            <div class="label">Battery</div>
          </div>
          <span id="b_state" class="chip">—</span>
        </div>

        <div class="row">
          <div class="kpi"><div id="battpct" class="value">--</div><div class="unit">%</div></div>
          <div class="kpi" style="margin-left:auto;"><div id="vbat" class="value">—</div><div class="unit">V</div></div>
        </div>

        <div class="batterybar"><div id="bar"></div></div>
      </div>

    </div>
  </main>

  <script>
    const DEV_NAME = "%DEVNAME%";
    document.getElementById("devname").textContent = DEV_NAME;

    function pulse(id, text){
      const el = document.getElementById(id);
      if(el.textContent !== text){
        el.textContent = text;
        el.classList.remove('pulse'); void el.offsetWidth; el.classList.add('pulse');
      }
    }
    function cls(val, low, high){
      if (isNaN(val)) return "bad";
      if (val < low || val > high) return "warn";
      return "ok";
    }
    function label(val, low, high){
      if (isNaN(val)) return "ERR";
      if (val < low || val > high) return "WARN";
      return "OK";
    }
    function setChip(id, klass, txt){
      const el = document.getElementById(id);
      el.className = "chip " + klass;
      el.textContent = txt;
    }

    async function refresh(){
      try{
        const r = await fetch("%JSONPATH%", {cache:"no-store"});
        if(!r.ok) throw new Error("HTTP " + r.status);
        const j = await r.json();

        const t  = j.tmp_c,
              p  = j.pres_hpa,
              h  = j.hum_pct,
              bp = j.bat_pct,
              vv = j.bat_v;

        pulse("temp",  (t!=null ? t.toFixed(1) : "--"));
        pulse("press", (p!=null ? p.toFixed(1) : "--"));
        pulse("hum",   (h!=null ? h.toFixed(0) : "--"));
        pulse("battpct", (bp!=null ? bp.toFixed(0) : "--"));
        pulse("vbat",  (vv!=null ? vv.toFixed(2) : "—"));

        setChip("t_state", cls(t,-10,50),  label(t,-10,50));
        setChip("p_state", cls(p,900,1100), label(p,900,1100));
        setChip("h_state", cls(h,20,80),   label(h,20,80));
        setChip("b_state", cls(bp,20,100), label(bp,20,100));

        const bar = document.getElementById("bar");
        const pct = Math.max(0, Math.min(100, bp || 0));
        bar.style.width = pct + "%";

        const now = new Date();
        document.getElementById("updated").textContent = now.toLocaleTimeString();
      }catch(e){
        // silent fail; UI remains with last known-good data
      }
    }
    refresh();
    setInterval(refresh, %POLL_MS%);
  </script>
</body>
</html>
)HTML";
/* ------------------------------------------------------------------------- */

Telemetry safeCopy() {
  Telemetry t;
  portENTER_CRITICAL(&g_data_mux);
  t = g_data;
  portEXIT_CRITICAL(&g_data_mux);
  return t;
}

/* ------------------------------ HTTP Handlers ----------------------------- */
void handleRoot() {
  String page(HTML_PAGE);
  page.replace("%DEVNAME%", DEVICE_NAME);
  page.replace("%JSONPATH%", HTTP_JSON_PATH);
  page.replace("%POLL_MS%", String(SENSOR_POLL_SECONDS * 1000));
  DEBUG_PRINT("Serving root page");
  server.send(200, "text/html; charset=utf-8", page);
}

void formatJson(String& out) {
  Telemetry t = safeCopy();
  out.reserve(256);
  out += "{";
  out += "\"tmp_c\":"; out += (isnan(t.tmp_c) ? "null" : String(t.tmp_c, 2)); out += ",";
  out += "\"pres_hpa\":";  out += (isnan(t.pres_hpa)  ? "null" : String(t.pres_hpa, 2));  out += ",";
  out += "\"hum_pct\":";  out += (isnan(t.hum_pct)  ? "null" : String(t.hum_pct, 1));  out += ",";
  out += "\"bat_v\":";    out += (isnan(t.bat_v)    ? "null" : String(t.bat_v, 3));    out += ",";
  out += "\"bat_pct\":";   out += String(t.bat_pct);
  out += "}";
}
void handleApi() {
  String js; formatJson(js);
  DEBUG_PRINT("Serving API request");
  server.send(200, "application/json; charset=utf-8", js);
}

/* ------------------------------- (OPTIONAL) Button ISR ------------------------------- */
volatile uint32_t g_buttonFallingCount = 0;
void IRAM_ATTR onButtonFalling() {
  // Add your extra button logic here if needed.
  // Placeholder ISR, keep it constant-time and minimal.
  g_buttonFallingCount++;
}

/* --------------------------- Battery / ADC Read --------------------------- */
float readBatteryVolts() {
  // Ensure correct attenuation (up to ~3.6V at pin)
  analogSetPinAttenuation(ADC_BATTERY_PIN, ADC_ATTENUATION_DB);
  int mv = analogReadMilliVolts(ADC_BATTERY_PIN); // mV at the ADC pin
  float vadc = mv / 1000.0f;
  // Recover VBAT using divider ratio
  const float ratio = (R1_OHMS + R2_OHMS) / R2_OHMS;  // = 1.27 with 27k/100k
  float vbat = vadc * ratio;
  return vbat;
}

uint8_t voltsToPercent(float vbat) {
  float pct = 100.0f * (vbat - BAT_VOLTAGE_EMPTY) / (BAT_VOLTAGE_FULL - BAT_VOLTAGE_EMPTY);
  if (pct < BAT_PCT_FLOOR) pct = BAT_PCT_FLOOR;
  if (pct > BAT_PCT_CEIL)  pct = BAT_PCT_CEIL;
  return (uint8_t)(pct + 0.5f);
}

float lastHumidity = 60.0f;  // Start with 60% as base

/* --------------------------------- Tasks ---------------------------------- */
void sensorTask(void* arg) {

  // For BME280--------------------------------BME280
  // I2C + BME280 init here to bind to this core’s context
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  bool ok = bme.begin(0x76, &Wire); // try common addr 0x76; change if your board is 0x77
  //-------------------------------------------BME280

  // For BMP280 only--------------------------------BMP280
  // bool ok = bme.begin(BMP280_ADDRESS_ALT);
  // if (!ok) {
  //     DEBUG_PRINT("BME280 sensor not found!");
  //     // Leave values as NAN; UI will show "--"
  // } else {
  //     DEBUG_PRINT("BME280 sensor initialized.");
  // }
  //------------------------------------------------BMP280

  const TickType_t delayTicks = pdMS_TO_TICKS(SENSOR_POLL_SECONDS * 1000);

  for (;;) {
    Telemetry t;
        if (ok) {
            t.tmp_c = bme.readTemperature();
            t.pres_hpa  = bme.readPressure() / 100.0f;
            t.hum_pct  = bme.readHumidity(); // Comment this if using BMP280
            String status = "Sensor: T=" + String(t.tmp_c, 1) + " C, P=" + String(t.pres_hpa, 1) + " hPa, H=" + String(t.hum_pct, 1) + " %";
            DEBUG_PRINT(status);
        } else {
            t.tmp_c = NAN;
            t.pres_hpa  = NAN;
            t.hum_pct  = NAN;
            DEBUG_PRINT("Sensor read failed (BME280 not initialized)");
        }

        t.bat_v = readBatteryVolts();
        t.bat_pct   = voltsToPercent(t.bat_v);
        t.last_ms    = millis();
        String batStatus = "Battery: V=" + String(t.bat_v, 2) + "V, %" + String(t.bat_pct);
        DEBUG_PRINT(batStatus);

        portENTER_CRITICAL(&g_data_mux);
        g_data = t;
        portEXIT_CRITICAL(&g_data_mux);

        vTaskDelay(delayTicks);
  }
}

void networkTask(void* arg) {
  // Bring up SoftAP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    DEBUG_PRINT("WiFi SoftAP started. SSID: " + String(WIFI_AP_SSID));

    // HTTP routes
    server.on(HTTP_ROOT_PATH, handleRoot);
    server.on(HTTP_JSON_PATH, handleApi);
    server.onNotFound([]() {
        DEBUG_PRINT("404 Not Found: " + server.uri());
        server.send(404, "text/plain", "Not found");
    });
    server.begin();
    DEBUG_PRINT("Web server started on port " + String(WIFI_PORT));

    for (;;) {
        server.handleClient(); // lightweight; runs on Core 1
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* --------------------------------- Setup ---------------------------------- */
void setup() {
    Serial.begin(115200);
    while (!Serial) { delay(10); }
    DEBUG_PRINT("Serial debug enabled.");
    // Basic I/O
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonFalling, FALLING);

    // Start tasks pinned to distinct cores
    xTaskCreatePinnedToCore(sensorTask,  "sensorTask",  STACK_SENSOR_TASK,  nullptr, 1, &s_sensorTaskHandle,  CORE_SENSOR);
    xTaskCreatePinnedToCore(networkTask, "networkTask", STACK_NETWORK_TASK, nullptr, 1, &s_networkTaskHandle, CORE_NETWORK);
}

/* ---------------------------------- Loop ---------------------------------- */
void loop() {
  // Intentionally empty, work happens in tasks.
}
