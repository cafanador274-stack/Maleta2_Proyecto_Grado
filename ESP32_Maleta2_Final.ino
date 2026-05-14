#include <WiFi.h>

// =========================
// Wi-Fi
// =========================
const char* ssid     = "Maleta2";
const char* password = "123456789";
WiFiServer server(80);

// =========================
// UART2 (ESP32 <-> Mega)
// =========================
#define RXD2 16
#define TXD2 17

// =========================
// Estado global
// =========================
bool   st_led = false;     // LED ON/OFF
int    st_ledDuty = -1;    // 0..255
bool   st_tv  = false;     // TV ON/OFF
int    st_tvVol = -1;      // 0..100
int    st_fan   = -1;      // 0..3
double st_dist  = NAN;     // cm
double st_lux   = NAN;     // lx
double st_temp  = NAN;     // °C
String st_curt  = "--";    // OPEN/CLOSED

String rxBuf; // buffer RX Mega

// --- utils ---
void handleClient(WiFiClient &client);
void route(WiFiClient &client, const String& path, const String& query);
void sendHTML(WiFiClient &client);
void sendJSON(WiFiClient &client, const String& json, int code = 200);
String urlDecode(const String& s);
String getLocalIP();
String getParam(const String& query, const String& key);

// ===== NUEVO: enviar IP al Mega =====
void sendIpToMega() {
  if (WiFi.status() == WL_CONNECTED) {
    String ipStr = WiFi.localIP().toString();
    Serial.println("Enviando IP a Mega: " + ipStr);
    Serial2.print("IP ");
    Serial2.println(ipStr);
  }
}

// =========================
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado!");
  Serial.print("IP del ESP32: ");
  Serial.println(WiFi.localIP());

  // ===== Enviar IP al Mega una vez conectado =====
  sendIpToMega();

  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

// =========================
void loop() {
  // RX asíncrono desde Mega (por líneas)
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\r') continue;
    if (c == '\n') {
      String s = rxBuf;
      rxBuf = "";
      s.trim();
      if (s.length()) {
        Serial.print("⟵ Mega: ");
        Serial.println(s);

        // Atajos LED
        if (s.equalsIgnoreCase("LED ON") || s.equalsIgnoreCase("LED_ON")) {
          st_led = true;
          if (st_ledDuty < 0) st_ledDuty = 255;
        }
        if (s.equalsIgnoreCase("LED OFF") || s.equalsIgnoreCase("LED_OFF")) {
          st_led = false;
          st_ledDuty = 0;
        }
        if (s.startsWith("LED ")) {
          int v = s.substring(4).toInt();
          v = constrain(v,0,255);
          st_ledDuty = v;
          st_led     = (v>0);
        }

        // Quitar prefijos STATE / EVT
        if (s.startsWith("STATE") || s.startsWith("EVT")) {
          int i = s.indexOf(' ');
          if (i != -1) {
            s = s.substring(i + 1);
            s.trim();
          }
        }

        // Parse k=v separados por ';'
        int start = 0;
        while (start < (int)s.length()) {
          int end = s.indexOf(';', start);
          if (end == -1) end = s.length();
          String tok = s.substring(start, end);
          tok.trim();

          int eq = tok.indexOf('=');
          if (eq != -1) {
            String k = tok.substring(0, eq);
            k.trim();
            k.toUpperCase();
            String v = tok.substring(eq + 1);
            v.trim();

            if (k == "DIST") { st_dist = v.toDouble(); }
            else if (k == "LUX") { st_lux = v.toDouble(); }
            else if (k == "TEMP" || k == "TC") { st_temp = v.toDouble(); }
            else if (k == "F") { st_fan = v.toInt(); }
            else if (k == "L") { st_ledDuty = v.toInt(); st_led = (st_ledDuty > 0); }
            else if (k == "CURT") { st_curt = v; st_curt.toUpperCase(); }
            else if (k == "TV") { st_tv = (v.equalsIgnoreCase("ON")); }
            else if (k == "VOL") { st_tvVol = v.toInt(); }
            else if (k == "LED") {
              if (v.equalsIgnoreCase("ON") || v == "1") {
                st_led = true;
                if (st_ledDuty < 0) st_ledDuty = 255;
              } else if (v.equalsIgnoreCase("OFF") || v == "0") {
                st_led = false;
                st_ledDuty = 0;
              } else {
                int n = v.toInt();
                if (n>=0 && n<=255){
                  st_ledDuty=n;
                  st_led=(n>0);
                }
              }
            }
          }
          start = end + 1;
        }
      }
    } else {
      rxBuf += c;
    }
  }

  // HTTP
  WiFiClient client = server.available();
  if (client) {
    handleClient(client);
    client.stop();
  }
}

// ========================= HTTP =========================
void handleClient(WiFiClient &client) {
  String line;
  unsigned long t0 = millis();
  while (client.connected() && (millis() - t0 < 2000)) {
    if (client.available()) {
      char c = client.read();
      if (c == '\r') continue;
      if (c == '\n') break;
      line += c;
    }
  }

  int sp1 = line.indexOf(' ');
  int sp2 = line.indexOf(' ', sp1 + 1);
  if (sp1 > 0 && sp2 > sp1) {
    String method = line.substring(0, sp1);
    String raw    = line.substring(sp1 + 1, sp2);

    String path, query;
    int q = raw.indexOf('?');
    if (q >= 0) {
      path  = raw.substring(0, q);
      query = raw.substring(q + 1);
    } else {
      path = raw;
    }

    path = urlDecode(path);
    if (method != "GET") {
      sendJSON(client, "{\"error\":\"only GET\"}", 405);
      return;
    }
    route(client, path, query);
  } else {
    sendJSON(client, "{\"error\":\"bad request\"}", 400);
  }
}

void route(WiFiClient &client, const String& path, const String& query) {
  if (path == "/" || path == "/index.html") {
    sendHTML(client);
    return;
  }

  if (path == "/api/state") {
    String json = "{";
    json += "\"ip\":\""   + getLocalIP() + "\",";   // <-- IP
    json += "\"led\":"    + String(st_led ? "true" : "false") + ",";
    json += (isnan(st_dist)? "\"dist\":null," : "\"dist\":" + String(st_dist,0) + ",");
    json += (isnan(st_lux)?  "\"lux\":null,"  : "\"lux\":"  + String(st_lux,1)  + ",");
    json += (isnan(st_temp)? "\"temp\":null," : "\"temp\":" + String(st_temp,1) + ",");
    json += "\"fan\":"    + String(st_fan) + ",";
    json += "\"ledDuty\":"+ String(st_ledDuty) + ",";
    json += "\"curt\":\"" + st_curt + "\",";
    json += "\"tv\":"     + String(st_tv ? "true" : "false") + ",";
    json += "\"vol\":"    + String(st_tvVol);
    json += "}";
    sendJSON(client, json);
    return;
  }

  if (path == "/api/led") {
    String on = getParam(query, "on");
    if (on.length() == 0) {
      sendJSON(client, "{\"ok\":false,\"msg\":\"use on=1|0\"}", 400);
      return;
    }
    bool v = (on == "1" || on.equalsIgnoreCase("true"));
    Serial2.println(v ? "LED_ON" : "LED_OFF");
    st_led = v;
    st_ledDuty = v ? 255 : 0;
    sendJSON(client, String("{\"ok\":true,\"led\":") + (st_led ? "true}" : "false}"));
    return;
  }

  if (path == "/api/cmd") {
    String line = getParam(query, "line");
    if (line.length() == 0) {
      sendJSON(client, "{\"ok\":false,\"msg\":\"use line=...\"}", 400);
      return;
    }

    String probe = line;
    probe.trim();
    if (probe.startsWith("LED ")) {
      int v = probe.substring(4).toInt();
      v = constrain(v,0,255);
      st_ledDuty = v;
      st_led     = (v>0);
    }

    if (!line.endsWith("\n")) line += "\n";
    Serial2.print(line);
    sendJSON(client, "{\"ok\":true}");
    return;
  }

  // compat
  if (path == "/led/on") {
    Serial2.println("LED_ON");
    st_led      = true;
    st_ledDuty  = 255;
    sendHTML(client);
    return;
  }
  if (path == "/led/off") {
    Serial2.println("LED_OFF");
    st_led      = false;
    st_ledDuty  = 0;
    sendHTML(client);
    return;
  }

  sendJSON(client, "{\"error\":\"not found\"}", 404);
}

// ========================= HTML =========================
void sendHTML(WiFiClient &client) {
  String html = R"HTML(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>SmartHome</title>
<style>
:root{
  --bg1:#e9f5ff; --bg2:#b7dbff;
  --accent1:#68b8ff; --accent2:#007bff;
  --text:#03315d; --mut:#4b6b8b;
  --ok1:#34d399; --ok2:#059669;
}
*{box-sizing:border-box} html,body{margin:0;min-height:100%}
body{
  font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu;
  background:linear-gradient(180deg,var(--bg1),var(--bg2));
  color:var(--text);
}
.wrap{max-width:880px;margin:0 auto;padding:18px}
.top{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px}
.brand{font-weight:900;font-size:22px;letter-spacing:.4px}
.state{display:inline-flex;align-items:center;gap:8px}
.dot{width:10px;height:10px;border-radius:50%}
.badge{padding:6px 10px;border-radius:999px;font-size:12px;border:1px solid rgba(0,0,0,.08);color:var(--text);background:transparent}

/* Secciones */
.section{display:none} .section.show{display:block}
.center{text-align:center}
h1{margin:0 0 4px} p{margin:0;color:var(--mut)}

/* ===== Botón Volver ===== */
.backRow{display:flex;align-items:center;gap:10px;margin:4px 0 10px}
.backBtn{
  appearance:none;border:none;cursor:pointer;border-radius:12px;font-weight:800;
  padding:12px 14px; display:inline-flex; align-items:center; gap:8px;
  background:transparent;color:var(--text); border:1px solid #9cc9ff;
}
.backBtn svg{width:18px;height:18px;fill:currentColor}

/* ===== HERO de INICIO ===== */
.homeHero{
  display:flex;align-items:center;gap:26px;justify-content:space-between;
  padding:22px 6px 6px 6px; margin:10px 0 18px 0;
}
.homeCopy{flex:1;display:flex;flex-direction:column;gap:12px}
.homeTitle{font-weight:900;font-size:40px;line-height:1.05;letter-spacing:.2px}
.homeSubtitle{font-size:15px;color:#275a8e}
.homeBtns{display:flex;flex-wrap:wrap;gap:12px}
.hBtn{
  appearance:none;border:none;cursor:pointer;border-radius:16px;font-weight:900;
  padding:16px 20px; display:inline-flex; align-items:center; gap:10px;
  background:linear-gradient(90deg,var(--accent1),var(--accent2));
  color:#fff; box-shadow:0 10px 24px rgba(0,100,255,.25); transition:.2s; font-size:16px;
}
.hBtn.ghost{background:transparent;color:var(--text);border:1px solid #9cc9ff;box-shadow:none}
.hBtn svg{width:22px;height:22px;fill:currentColor}
.homeArt{flex:1; min-width:280px; display:flex; align-items:center; justify-content:center;}
.homeArt svg{width:100%; max-width:360px; height:auto}

/* ===== Estilo unificado ===== */
.stack{display:grid;gap:14px}
.art{display:flex;align-items:center;justify-content:center}
.art svg{width:100%;max-width:360px;height:auto}
.title{font-weight:900;font-size:30px;line-height:1.06}
.sub{font-size:14px;color:#275a8e;margin-top:2px}
.kv{display:flex;gap:8px;align-items:center;justify-content:center;flex-wrap:wrap}
.pill, .value{
  display:inline-flex;gap:6px;align-items:center;padding:8px 12px;
  border:1px solid rgba(0,0,0,.12);border-radius:999px;background:transparent;
}
.value.plain{background:transparent;border:1px solid rgba(0,0,0,.12)}
.buttons{display:flex;flex-wrap:wrap;gap:12px;justify-content:center;margin-top:6px}
.xBtn{
  appearance:none;border:none;cursor:pointer;border-radius:18px;font-weight:900;
  padding:16px 20px; display:inline-flex; align-items:center; gap:10px;
  background:linear-gradient(90deg,var(--accent1),var(--accent2));
  color:#fff; box-shadow:0 12px 28px rgba(0,0,255,.28); transition:.2s; font-size:16px;
}
.xBtn svg{width:20px;height:20px;fill:#fff}
.xBtn.ghost{background:transparent;color:var(--text);border:1px solid #9cc9ff;box-shadow:none}
.xBtn.red{background:linear-gradient(90deg,#ff7b7b,#ff3737)}
.xBtn.active,
.gBtn.active,
.hBtn.active{
  background:linear-gradient(90deg,var(--ok1),var(--ok2)) !important;
  color:#fff !important;
  border-color:transparent !important;
  box-shadow:0 12px 28px rgba(5,150,105,.28) !important;
}
.xBtn.ghost.active{
  background:linear-gradient(90deg,var(--ok1),var(--ok2)) !important;
  color:#fff !important;
  border-color:transparent !important;
  box-shadow:0 12px 28px rgba(5,150,105,.28) !important;
}

/* ===== MENÚ MANUAL ===== */
.manualHero{display:flex;flex-direction:column;align-items:center;gap:6px;margin:6px 0 14px}
.manualHero .sh{font-weight:800;color:#2b86d9;font-size:18px;letter-spacing:.2px}
.manualHero svg{width:76px;height:76px;stroke:#2b86d9;fill:none;stroke-width:2.5}
.menuList{max-width:720px;margin:0 auto;padding:0 4px}
.menuList .btn{
  width:100%;
  display:flex;align-items:center;justify-content:center;
  gap:12px;text-align:center;
  padding:14px 18px;margin:8px 0;font-size:16px;font-weight:700;
  color:#fff;border:none;cursor:pointer;border-radius:12px;
  background:linear-gradient(90deg,var(--accent1),var(--accent2));
  box-shadow:0 4px 12px rgba(0,100,255,.25);
  transition:transform .15s ease,box-shadow .15s ease;
}
.menuList .btn:hover{transform:translateY(-1px);box-shadow:0 8px 18px rgba(0,100,255,.28)}
.menuList .btn:active{transform:translateY(0)}
.menuList .btn svg{width:24px;height:24px;fill:#fff}

/* ===== GARAJE ===== */
.garageHero{display:flex;align-items:center;gap:26px;justify-content:space-between;margin:8px 0 14px}
.garageCopy{flex:1;display:flex;flex-direction:column;gap:10px}
.garageTitle{font-weight:900;font-size:34px;line-height:1.06}
.garageSubtitle{font-size:14px;color:#275a8e}
.garageArt{flex:1;min-width:260px;display:flex;align-items:center;justify-content:center}
.garageArt svg{width:100%;max-width:360px;height:auto}
.gButtons{display:flex;flex-wrap:wrap;gap:12px;margin-top:6px;justify-content:center}
.gBtn{
  appearance:none;border:none;cursor:pointer;border-radius:18px;font-weight:900;
  padding:18px 22px; display:inline-flex; align-items:center; gap:12px;
  background:linear-gradient(90deg,var(--accent1),var(--accent2));
  color:#fff; box-shadow:0 12px 28px rgba(0,0,255,.28); transition:.2s; font-size:18px;
}
.gBtn svg{width:22px;height:22px;fill:#fff}
.gBtn.red{background:linear-gradient(90deg,#ff7b7b,#ff3737)}
.gBtn.ghost{background:transparent;color:var(--text);border:1px solid #9cc9ff;box-shadow:none}
.gBtn.red.active{
  background:linear-gradient(90deg,#ff7b7b,#ff3737) !important;
  box-shadow:0 12px 28px rgba(255,55,55,.28) !important;
}

/* Grids + utilidades */
.grid.two{display:grid;gap:14px;grid-template-columns:repeat(2,1fr)}
@media(max-width:760px){
  .grid.two{grid-template-columns:1fr}
  .homeHero{flex-direction:column-reverse;text-align:center}
  .homeBtns{justify-content:center}
  .garageHero{flex-direction:column-reverse;text-align:center}
  .gButtons{justify-content:center}
}

.small{font-size:13px;color:#6b86a6}
.row{display:flex;gap:12px;flex-wrap:wrap}
.sep{border:0;border-top:1px solid rgba(0,0,0,.08);margin:12px 0}
.slider{width:100%}
.toast{position:fixed;right:16px;bottom:16px;background:#03315d;color:#eaf3ff;padding:12px 14px;border-radius:12px;border:1px solid #89baff;opacity:0;transform:translateY(8px);transition:.25s;z-index:9}
.toast.show{opacity:1;transform:none}

/* ===== Botón 3 puntos + menú ===== */
.iconBtn{
  display:inline-flex;align-items:center;justify-content:center;
  width:40px;height:40px;border:none;border-radius:12px;cursor:pointer;
  background:transparent;border:1px solid rgba(0,0,0,.12);
  box-shadow:0 4px 10px rgba(0,0,0,.08);
}
.iconBtn svg{width:20px;height:20px}
.more{position:relative}
.menu{
  position:absolute;right:0;top:46px;min-width:220px;z-index:10;
  background:transparent;border:1px solid rgba(0,0,0,.1);border-radius:12px;
  box-shadow:0 10px 24px rgba(0,0,0,.15);padding:10px 10px;display:none;
  backdrop-filter: blur(2px);
}
.menu.show{display:block}
.menuItem{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:8px 6px}
.menu hr{border:0;border-top:1px solid rgba(0,0,0,.08);margin:6px 0}

/* Oculta la fila vieja (IP/estado) del top */
.top .row{ display:none; }

/* Mic activo */
.iconBtn.listening{outline:2px solid #17a34a; box-shadow:0 0 0 3px rgba(23,163,74,.25)}
</style>
</head>
<body>
<div class="wrap">
  <div class="top">
    <div class="brand">SmartHome</div>

    <!-- 3 puntos + Mic -->
    <div style="display:flex; gap:8px; align-items:center">
      <!-- Botón micrófono -->
      <button id="micBtn" class="iconBtn" aria-label="Voz" title="Comandos por voz">
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <path d="M12 3a3 3 0 00-3 3v6a3 3 0 006 0V6a3 3 0 00-3-3zM5 11a7 7 0 0014 0h-2a5 5 0 01-10 0H5zm6 7v3h2v-3h-2z"/>
        </svg>
      </button>

      <div class="more">
        <button id="moreBtn" class="iconBtn" aria-label="Más opciones" title="Más">
          <svg viewBox="0 0 24 24" aria-hidden="true">
            <circle cx="12" cy="5" r="2"></circle>
            <circle cx="12" cy="12" r="2"></circle>
            <circle cx="12" cy="19" r="2"></circle>
          </svg>
        </button>
        <div id="moreMenu" class="menu" role="menu" aria-label="Estado">
          <div class="menuItem"><span><b>ESP32</b></span><span class="badge" id="ipMenu">(--)</span></div>
          <div class="menuItem">
            <span>Conexión</span>
            <span class="state">
              <span id="connDotMenu" class="dot" style="background:#ef4444"></span>
              <span id="connLabMenu">Desconectado</span>
            </span>
          </div>
          <hr/>
          <div class="menuItem"><small style="color:#6b7280">Toca fuera para cerrar</small></div>
        </div>
      </div>
    </div>

    <!-- (queda, pero oculto por CSS) -->
    <div class="row">
      <span class="badge">ESP32 <span id="ip"></span></span>
      <span class="state"><span id="connDot" class="dot" style="background:#ef4444"></span><span id="connLab">Desconectado</span></span>
    </div>
  </div>

  <!-- ===== INICIO ===== -->
  <div id="sec-home" class="section show">
    <div class="homeHero">
      <div class="homeCopy">
        <div class="homeTitle">Bienvenido a tu Smart Home</div>
        <div class="homeSubtitle">Controla tu casa desde este panel.</div>
        <div class="homeBtns">
          <button class="hBtn" onclick="show('manual-menu')">
            <svg viewBox="0 0 24 24"><path d="M4 4h16v4H4V4zm0 6h10v4H4v-4zm0 6h16v4H4v-4z"/></svg>
            Modo manual
          </button>
          <button class="hBtn ghost" onclick="show('auto')">
            <svg viewBox="0 0 24 24"><path d="M12 2l3 3-3 3-3-3 3-3zm0 14l3 3-3 3-3-3 3-3zM2 12l3-3 3 3-3 3-3-3zm16 0l3-3 3 3-3 3-3-3z"/></svg>
            Monitoreo
          </button>
        </div>
      </div>
      <div class="homeArt" aria-hidden="true">
        <svg viewBox="0 0 340 280">
          <defs>
            <linearGradient id="g1" x1="0" y1="0" x2="1" y2="1">
              <stop offset="0" stop-color="#9cd5ff"/>
              <stop offset="1" stop-color="#3d96ff"/>
            </linearGradient>
          </defs>
          <path d="M40 120c0-60 70-90 130-90s130 30 130 90-70 110-130 110S40 180 40 120z"
                fill="url(#g1)" opacity="0.28"/>
          <path d="M80 150 L170 90 L260 150 V240 H205 V190 H135 V240 H80 Z"
                fill="#ffffff" opacity="0.95" stroke="#7bb8ff" stroke-width="3"/>
          <circle cx="170" cy="168" r="16" fill="none" stroke="#7bb8ff" stroke-width="3"/>
          <g stroke="#2b86d9" stroke-width="3" fill="none" stroke-linecap="round">
            <path d="M170 60 v-18"/><circle cx="170" cy="38" r="5" fill="#2b86d9"/>
            <path d="M290 170 h18"/><circle cx="312" cy="170" r="5" fill="#2b86d9"/>
            <path d="M50 170 h-18"/><circle cx="30" cy="170" r="5" fill="#2b86d9"/>
            <path d="M110 245 v18"/><circle cx="110" cy="268" r="5" fill="#2b86d9"/>
            <path d="M230 245 v18"/><circle cx="230" cy="268" r="5" fill="#2b86d9"/>
          </g>
        </svg>
      </div>
    </div>
  </div>

  <!-- ===== MENÚ MANUAL ===== -->
  <div id="sec-manual-menu" class="section">
    <div class="backRow">
      <button class="backBtn" onclick="show('home')">
        <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6"/></svg>
        Volver
      </button>
    </div>
    <div class="manualHero">
      <div class="sh">Smart Home</div>
      <svg viewBox="0 0 64 64" aria-hidden="true">
        <path d="M6 28 L32 10 L58 28 V56 H38 V40 H26 V56 H6 Z"></path>
        <circle cx="32" cy="35" r="5"></circle>
      </svg>
    </div>
    <div class="menuList">
      <button class="btn" onclick="show('garaje')">
        <svg viewBox="0 0 24 24"><path d="M3 9l9-6 9 6v11a1 1 0 01-1 1h-5v-5H9v5H4a1 1 0 01-1-1V9z"/></svg>
        Garaje
      </button>
      <button class="btn" onclick="show('luces')">
        <svg viewBox="0 0 24 24"><path d="M9 21h6v-2H9v2zm3-19a7 7 0 00-7 7c0 2.67 2 5.33 4 6.67V17h6v-1.33C14 14.33 16 11.67 16 9a7 7 0 00-7-7z"/></svg>
        Luces
      </button>
      <button class="btn" onclick="show('tv')">
        <svg viewBox="0 0 24 24"><path d="M21 17H3a2 2 0 01-2-2V6a2 2 0 012-2h18a2 2 0 012 2v9a2 2 0 01-2 2zM8 20h8v1H8z"/></svg>
        TV
      </button>
      <button class="btn" onclick="show('cortinas')">
        <svg viewBox="0 0 24 24"><path d="M4 3h16v18H4V3zm2 2v14h12V5H6z"/></svg>
        Cortinas
      </button>
      <button class="btn" onclick="show('ventilador')">
        <svg viewBox="0 0 24 24"><path d="M12 12a3 3 0 100-6 3 3 0 000 6zm0 1.5A4.5 4.5 0 117.5 9H5a7 7 0 1014 0h-2.5A4.5 4.5 0 0112 13.5z"/></svg>
        Ventilador
      </button>
      <button class="btn" onclick="show('puerta')">
        <svg viewBox="0 0 24 24">
          <path d="M7 3h10v18H7zM9 5h6v14H9z"/>
          <circle cx="10.5" cy="12" r="0.8"/>
        </svg>
        Puerta
      </button>
    </div>
  </div>

  <!-- ===== GARAJE ===== -->
  <div id="sec-garaje" class="section">
    <div class="backRow">
      <button class="backBtn" onclick="show('manual-menu')">
        <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6"/></svg>
        Volver
      </button>
    </div>
    <div class="garageHero">
      <div class="garageCopy">
        <div class="garageTitle">Garaje</div>
        <div class="garageSubtitle">Controla la puerta y verifica la proximidad.</div>
        <div class="kv" style="margin-top:6px">
          <span class="value plain">Distancia: <b id="gDist">--</b> cm</span>
        </div>
        <div class="gButtons">
          <button id="garageOpenBtn" class="gBtn" onclick="cmd('GARAGE OPEN')">
            <svg viewBox="0 0 24 24"><path d="M3 11h18v10H3zM3 9l9-6 9 6"/></svg>
            Abrir
          </button>
          <button id="garageCloseBtn" class="gBtn red" onclick="cmd('GARAGE CLOSE')">
            <svg viewBox="0 0 24 24"><path d="M3 11h18v10H3zM5 9h14"/></svg>
            Cerrar
          </button>
        </div>
      </div>
      <div class="garageArt" aria-hidden="true">
        <svg viewBox="0 0 360 240">
          <defs><linearGradient id="gg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#9cd5ff"/><stop offset="1" stop-color="#3d96ff"/></linearGradient></defs>
          <path d="M40 120c0-50 70-80 140-80s140 30 140 80-70 90-140 90S40 170 40 120z" fill="url(#gg)" opacity="0.28"/>
          <g stroke="#7bb8ff" stroke-width="4" fill="none" opacity="0.96">
            <path d="M70 110 L180 50 L290 110 V210 H70 Z" />
            <rect x="110" y="130" width="140" height="70" rx="2"/>
            <line x1="110" y1="140" x2="250" y2="140"/>
            <line x1="110" y1="155" x2="250" y2="155"/>
            <line x1="110" y1="170" x2="250" y2="170"/>
            <line x1="110" y1="185" x2="250" y2="185"/>
            <circle cx="180" cy="120" r="10"/>
          </g>
        </svg>
      </div>
    </div>
  </div>

  <!-- ===== MONITOREO ===== -->
  <div id="sec-auto" class="section">
    <div class="backRow">
      <button class="backBtn" onclick="show('manual-menu')">
        <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6"/></svg>
        Volver
      </button>
    </div>
    <div class="stack">
      <div class="art" aria-hidden="true">
        <svg viewBox="0 0 360 240">
          <defs><linearGradient id="autoG" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#9cd5ff"/><stop offset="1" stop-color="#3d96ff"/></linearGradient></defs>
          <path d="M30 120c0-55 80-85 150-85s150 30 150 85-80 100-150 100S30 175 30 120z" fill="url(#autoG)" opacity=".28"/>
          <g stroke="#7bb8ff" stroke-width="4" fill="none" opacity=".96">
            <circle cx="110" cy="110" r="22"/><circle cx="180" cy="110" r="22"/><circle cx="250" cy="110" r="22"/>
          </g>
        </svg>
      </div>
      <div>
        <div class="title">Monitoreo</div>
        <div class="sub">Monitoreo general</div>
        <div class="kv" style="margin-top:8px">
          <span class="pill">Lux: <b id="aLux">--.-</b> lx</span>
          <span class="pill">Temp: <b id="aTemp">--.-</b> °C</span>
          <span class="pill">Distancia: <b id="aDist">--</b> cm</span>
        </div>
        <p class="small" style="text-align:center;margin-top:8px">
          Sensores en tiempo real
        </p>
      </div>
    </div>
  </div>

  <!-- ===== LUCES ===== -->
  <div id="sec-luces" class="section">
    <div class="backRow">
      <button class="backBtn" onclick="show('manual-menu')">
        <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6"/></svg>
        Volver
      </button>
    </div>
    <div class="stack">
      <div class="art" aria-hidden="true">
        <svg viewBox="0 0 360 240">
          <defs><linearGradient id="luG" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ffe08a"/><stop offset="1" stop-color="#ffc14d"/></linearGradient></defs>
          <path d="M30 120c0-55 80-85 150-85s150 30 150 85-80 100-150 100S30 175 30 120z" fill="url(#luG)" opacity=".28"/>
          <g stroke="#ffd166" stroke-width="4" fill="none" opacity=".96">
            <path d="M160 70a20 20 0 0 1 40 0c0 10-6 17-10 24h-20c-4-7-10-14-10-24z"/>
            <rect x="172" y="118" width="16" height="10" rx="2"/>
          </g>
        </svg>
      </div>
      <div>
        <div class="title">Luces</div>
        <div class="sub">Lectura y brillo del LED</div>

        <div class="kv" style="margin-top:8px">
          <span class="pill">Lux: <b id="lLux">--.-</b> lx</span>
          <span class="pill">Luz: <b id="ledRangeLab">-- / 100%</b></span>
        </div>

        <div style="max-width:560px;margin:10px auto 0">
          <input id="ledRange" class="slider" type="range" min="0" max="255" value="0"
                 oninput="setLedDuty(this.value)"/>
        </div>
      </div>
    </div>
  </div>

  <!-- ===== TV ===== -->
  <div id="sec-tv" class="section">
    <div class="backRow">
      <button class="backBtn" onclick="show('manual-menu')">
        <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6"/></svg>Volver
      </button>
    </div>
    <div class="stack">
      <div class="art" aria-hidden="true">
        <svg viewBox="0 0 360 240">
          <defs><linearGradient id="tv" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#9cd5ff"/><stop offset="1" stop-color="#3d96ff"/></linearGradient></defs>
          <path d="M30 120c0-55 80-85 150-85s150 30 150 85-80 100-150 100S30 175 30 120z" fill="url(#tv)" opacity=".28"/>
          <g stroke="#7bb8ff" stroke-width="4" fill="none" opacity=".96">
            <rect x="80" y="70" width="200" height="120" rx="10"/>
            <rect x="150" y="195" width="60" height="8" rx="4"/>
          </g>
        </svg>
      </div>
      <div>
        <div class="title">TV</div>
        <div class="sub">Controles básicos</div>

        <div class="buttons" style="margin-top:8px">
          <button id="tvPowerBtn" class="xBtn red" onclick="cmd('TV POWER')">
            <svg viewBox="0 0 24 24"><path d="M12 2v10m7.07-4.93A8 8 0 1112 4"/></svg> Encender / apagar
          </button>
        </div>

        <div class="buttons">
          <button class="xBtn ghost" onclick="cmd('TV VOL+')">
            <svg viewBox="0 0 24 24"><path d="M4 14h4l5 5V5l-5 5H4zM17 9v6"/></svg> Vol +
          </button>
          <button class="xBtn ghost" onclick="cmd('TV VOL-')">
            <svg viewBox="0 0 24 24"><path d="M4 14h4l5 5V5l-5 5H4zM17 12h4"/></svg> Vol -
          </button>
        </div>

        <div class="buttons">
          <button class="xBtn ghost" onclick="cmd('TV CH+')">
            <svg viewBox="0 0 24 24"><path d="M12 5v14M5 12h14"/></svg> Canal +
          </button>
          <button class="xBtn ghost" onclick="cmd('TV CH-')">
            <svg viewBox="0 0 24 24"><path d="M5 12h14"/></svg> Canal -
          </button>
        </div>

        <div class="kv" style="margin-top:8px">
          <span class="pill">Volumen: <b id="tvVol">--</b></span>
        </div>
      </div>
    </div>
  </div>

  <!-- ===== CORTINAS ===== -->
  <div id="sec-cortinas" class="section">
    <div class="backRow">
      <button class="backBtn" onclick="show('manual-menu')">
        <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6"/></svg>
        Volver
      </button>
    </div>
    <div class="stack">
      <div class="art" aria-hidden="true">
        <svg viewBox="0 0 360 240">
          <defs><linearGradient id="curG" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#bff0ff"/><stop offset="1" stop-color="#66c2ff"/></linearGradient></defs>
          <path d="M30 120c0-55 80-85 150-85s150 30 150 85-80 100-150 100S30 175 30 120z" fill="url(#curG)" opacity=".28"/>
          <g stroke="#7bb8ff" stroke-width="4" fill="none" opacity=".96">
            <rect x="90" y="70" width="180" height="120" rx="8"/>
            <line x1="180" y1="70" x2="180" y2="190"/>
          </g>
        </svg>
      </div>
      <div>
        <div class="title">Cortinas</div>
        <div class="sub">Control de apertura</div>

        <div class="buttons">
          <button id="curtainOpenBtn" class="xBtn" onclick="cmd('CURTAIN OPEN')">Abrir</button>
          <button id="curtainCloseBtn" class="xBtn ghost" onclick="cmd('CURTAIN CLOSE')">Cerrar</button>
        </div>

        <div class="kv" style="margin-top:8px">
          <span class="pill">Estado: <b id="curtSt">--</b></span>
          <span class="pill">Lux: <b id="cLux">--.-</b> lx</span>
        </div>
      </div>
    </div>
  </div>

  <!-- ===== VENTILADOR ===== -->
  <div id="sec-ventilador" class="section">
    <div class="backRow">
      <button class="backBtn" onclick="show('manual-menu')">
        <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6"/></svg>
        Volver
      </button>
    </div>
    <div class="stack">
      <div class="art" aria-hidden="true">
        <svg viewBox="0 0 360 240">
          <defs><linearGradient id="fanG" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#d1ffe6"/><stop offset="1" stop-color="#5be3a1"/></linearGradient></defs>
          <path d="M30 120c0-55 80-85 150-85s150 30 150 85-80 100-150 100S30 175 30 120z" fill="url(#fanG)" opacity=".28"/>
          <g stroke="#7bb8ff" stroke-width="4" fill="none" opacity=".96">
            <circle cx="180" cy="120" r="26"/>
            <path d="M180 94c-10-22 24-22 14 0M206 120c22-10 22 24 0 14M154 120c-22 10-22-24 0-14M180 146c10 22-24 22-14 0" />
          </g>
        </svg>
      </div>
      <div>
        <div class="title">Ventilador</div>
        <div class="sub">Velocidades</div>

        <div class="buttons">
          <button id="fan0Btn" class="xBtn ghost" onclick="cmd('FAN 0')">Apagado</button>
          <button id="fan1Btn" class="xBtn ghost" onclick="cmd('FAN 1')">Bajo</button>
          <button id="fan2Btn" class="xBtn ghost" onclick="cmd('FAN 2')">Medio</button>
          <button id="fan3Btn" class="xBtn ghost" onclick="cmd('FAN 3')">Alto</button>
        </div>

        <div class="kv" style="margin-top:8px">
          <span class="pill">Nivel: <b id="fanLv">--</b></span>
          <span class="pill">Temp: <b id="vTemp">--.-</b> °C</span>
        </div>
      </div>
    </div>
  </div>

  <!-- ===== PUERTA ===== -->
  <div id="sec-puerta" class="section">
    <div class="backRow">
      <button class="backBtn" onclick="show('manual-menu')">
        <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6"/></svg>
        Volver
      </button>
    </div>
    <div class="stack">
      <div class="art" aria-hidden="true">
        <svg viewBox="0 0 360 240">
          <defs><linearGradient id="doorG" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ffe3c2"/><stop offset="1" stop-color="#ffb36b"/></linearGradient></defs>
          <path d="M40 120c0-55 80-85 150-85s150 30 150 85-80 100-150 100S40 175 40 120z" fill="url(#doorG)" opacity=".28"/>
          <g stroke="#ffb36b" stroke-width="4" fill="none" opacity=".96">
            <rect x="140" y="60" width="80" height="120" rx="6" />
            <circle cx="155" cy="120" r="3" />
          </g>
        </svg>
      </div>
      <div>
        <div class="title">Puerta</div>
        <div class="sub">Servo de acceso principal</div>

        <div class="buttons">
          <button id="doorOpenBtn" class="xBtn" onclick="cmd('DOOR OPEN')">Abrir</button>
          <button id="doorCloseBtn" class="xBtn ghost" onclick="cmd('DOOR CLOSE')">Cerrar</button>
        </div>
      </div>
    </div>
  </div>

</div><!-- /wrap -->

<div id="toast" class="toast">Hecho</div>

<script>
// ===== Navegación
function show(id){
  const map = {
    'home':'sec-home',
    'manual-menu':'sec-manual-menu',
    'auto':'sec-auto',
    'garaje':'sec-garaje',
    'luces':'sec-luces',
    'tv':'sec-tv',
    'cortinas':'sec-cortinas',
    'ventilador':'sec-ventilador',
    'puerta':'sec-puerta',
  };
  document.querySelectorAll('.section').forEach(s=>s.classList.remove('show'));
  document.getElementById(map[id]).classList.add('show');
  setTimeout(pollState,120);
}

function showToast(msg){
  const t=document.getElementById('toast');
  t.textContent=msg;
  t.classList.add('show');
  setTimeout(()=>t.classList.remove('show'),1500);
}
function uiConn(ok){
  const d=document.getElementById('connDot');
  const l=document.getElementById('connLab');
  const dm=document.getElementById('connDotMenu');
  const lm=document.getElementById('connLabMenu');
  if(d) d.style.background=ok?'#17a34a':'#ef4444';
  if(l) l.textContent=ok?'Conectado':'Desconectado';
  if(dm) dm.style.background=ok?'#17a34a':'#ef4444';
  if(lm) lm.textContent=ok?'Conectado':'Desconectado';
}

// ===== API helpers
async function api(path){
  const r = await fetch(path,{cache:'no-store'});
  if(!r.ok) throw new Error(r.status);
  return r.json();
}
async function cmd(line){
  try{
    applyOptimisticState(line);
    await api('/api/cmd?line='+encodeURIComponent(line));
    if(!/STATE\?/.test(line)) setTimeout(pollState,150);
  }catch(e){
    showToast('Error');
  }
}

// ===== Slider LED con hold
let ledHoldUntil = 0;
function uiLedDuty(v){
  const r = document.getElementById('ledRange');
  const lab = document.getElementById('ledRangeLab');
  if (r){ r.value = v; }
  if (lab){
    const pct = Math.round((v * 100) / 255);
    lab.textContent = `${pct} / 100%`;
  }
}
async function setLedDuty(v){
  v=parseInt(v);
  ledHoldUntil=Date.now()+800;
  await cmd('LED '+v);
  await cmd('STATE?');
  uiLedDuty(v);
}

function markActive(ids, activeId){
  ids.forEach(id=>{
    const el = document.getElementById(id);
    if(!el) return;
    el.classList.toggle('active', id === activeId);
  });
}

function updateUiStates(j){
  if (j.tv !== undefined) {
    const b = document.getElementById('tvPowerBtn');
    if (b) b.classList.toggle('active', !!j.tv);
  }

  const curtain = String(j.curt || '').toUpperCase();
  if (curtain.includes('OPEN') || curtain.includes('ABIERTA') || curtain.includes('ABIERTO')) {
    markActive(['curtainOpenBtn','curtainCloseBtn'],'curtainOpenBtn');
  } else if (curtain.includes('CLOSE') || curtain.includes('CERRADA') || curtain.includes('CERRADO')) {
    markActive(['curtainOpenBtn','curtainCloseBtn'],'curtainCloseBtn');
  }

  if (j.fan != null && j.fan >= 0 && j.fan <= 3) {
    markActive(['fan0Btn','fan1Btn','fan2Btn','fan3Btn'], 'fan'+j.fan+'Btn');
  }
}

function applyOptimisticState(line){
  const s = String(line || '').trim().toUpperCase();
  if (s === 'GARAGE OPEN')  markActive(['garageOpenBtn','garageCloseBtn'], 'garageOpenBtn');
  if (s === 'GARAGE CLOSE') markActive(['garageOpenBtn','garageCloseBtn'], 'garageCloseBtn');
  if (s === 'CURTAIN OPEN') markActive(['curtainOpenBtn','curtainCloseBtn'], 'curtainOpenBtn');
  if (s === 'CURTAIN CLOSE')markActive(['curtainOpenBtn','curtainCloseBtn'], 'curtainCloseBtn');
  if (s === 'DOOR OPEN')    markActive(['doorOpenBtn','doorCloseBtn'], 'doorOpenBtn');
  if (s === 'DOOR CLOSE')   markActive(['doorOpenBtn','doorCloseBtn'], 'doorCloseBtn');
  if (s === 'TV POWER') {
    const b = document.getElementById('tvPowerBtn');
    if (b) b.classList.toggle('active');
  }
  if (s === 'FAN 0') markActive(['fan0Btn','fan1Btn','fan2Btn','fan3Btn'],'fan0Btn');
  if (s === 'FAN 1') markActive(['fan0Btn','fan1Btn','fan2Btn','fan3Btn'],'fan1Btn');
  if (s === 'FAN 2') markActive(['fan0Btn','fan1Btn','fan2Btn','fan3Btn'],'fan2Btn');
  if (s === 'FAN 3') markActive(['fan0Btn','fan1Btn','fan2Btn','fan3Btn'],'fan3Btn');
}

// ===== Polling
const ipEl   = document.getElementById('ip');
const ipMenu = document.getElementById('ipMenu');

async function pollState(){
  try{
    const j = await api('/api/state');
    uiConn(true);
    if (ipEl)   ipEl.textContent   = '('+(j.ip||'')+')';
    if (ipMenu) ipMenu.textContent = '('+(j.ip||'')+')';

    setText('aLux',  j.lux==null?'--.-':j.lux.toFixed(1));
    setText('aTemp', j.temp==null?'--.-':j.temp.toFixed(1));
    setText('aDist', j.dist==null?'--':Math.round(j.dist));
    setText('gDist', j.dist==null?'--':Math.round(j.dist));
    setText('lLux',  j.lux==null?'--.-':j.lux.toFixed(1));
    if (j.ledDuty != null && j.ledDuty >= 0 && Date.now() > ledHoldUntil) uiLedDuty(j.ledDuty);
    setText('tvVol', j.vol==null?'--':j.vol);
    setText('curtSt', j.curt || '--');
    setText('cLux',  j.lux==null?'--.-':j.lux.toFixed(1));
    setText('vTemp', j.temp==null?'--.-':j.temp.toFixed(1));
    setText('fanLv', (j.fan==null||j.fan<0)?'--':(['Apagado','Bajo','Medio','Alto'][j.fan]||j.fan));
    updateUiStates(j);
  }catch(e){
    uiConn(false);
  }
}
function setText(id,val){
  const el=document.getElementById(id);
  if(el) el.textContent=val;
}
setInterval(pollState,1000);
pollState();

// ===== Sensores periódicos
setInterval(()=>cmd('SENSE?'),1000);
setInterval(()=>cmd('LUX?'),1000);
setInterval(()=>cmd('TEMP?'),1000);
setInterval(()=>cmd('DIST?'),800);

// ===== Dropdown 3 puntos
const moreBtn = document.getElementById('moreBtn');
const moreMenu = document.getElementById('moreMenu');
if(moreBtn && moreMenu){
  moreBtn.addEventListener('click',(e)=>{
    e.stopPropagation();
    moreMenu.classList.toggle('show');
  });
  document.addEventListener('click',()=>{ moreMenu.classList.remove('show'); });
  moreMenu.addEventListener('click',(e)=>e.stopPropagation());
}

/* =========================
   Comandos por voz
   ========================= */
(function(){
  const MicAPI = window.SpeechRecognition || window.webkitSpeechRecognition;
  const micBtn = document.getElementById('micBtn');

  if (!micBtn) return;

  if (!MicAPI) {
    micBtn.disabled = true;
    micBtn.title = "Voz no soportada en este navegador";
    return;
  }

  const rec = new MicAPI();
  rec.lang = 'es-CO';
  rec.interimResults = false;
  rec.continuous = false;

  let listening = false;
  function setMicUI(active){
    listening = active;
    micBtn.classList.toggle('listening', active);
    micBtn.title = active ? "Escuchando... toca para detener" : "Comandos por voz";
  }

  async function ensureMicPermission(){
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      throw new Error("getUserMedia no disponible en este navegador");
    }
    const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
    stream.getTracks().forEach(t => t.stop());
  }

  micBtn.addEventListener('click', async () => {
    if (!listening){
      try {
        await ensureMicPermission();
        rec.start();
        setMicUI(true);
      } catch(e) {
        setMicUI(false);
        alert("No se pudo acceder al micrófono: " + (e && (e.message || e.name) ? (e.message || e.name) : e));
        console.log("Mic start error:", e);
      }
    } else {
      try { rec.stop(); setMicUI(false); } catch(e) {}
    }
  });

  rec.onresult = (ev) => {
    const text = (ev.results[0][0].transcript || '').toLowerCase().trim();
    handleSpeech(text);
  };
  rec.onend = () => setMicUI(false);

  rec.onerror = (ev) => {
    setMicUI(false);
    alert("Error de voz: " + (ev.error || "desconocido"));
    console.log("Speech error event:", ev);
  };

  function norm(s){
    return s
      .replace(/luces?/g,'luz')
      .replace(/ventiladores?/g,'ventilador')
      .replace(/sube|subir/g,'subir')
      .replace(/baja|bajar/g,'bajar')
      .replace(/enciende|encender|prender|prende/g,'encender')
      .replace(/apaga|apagar/g,'apagar')
      .replace(/abre|abrir/g,'abrir')
      .replace(/cierra|cerrar/g,'cerrar')
      .replace(/television|televisión|tele/g,'tv')
      .replace(/\s+/g,' ')
      .trim();
  }

  async function handleSpeech(raw){
    const t = norm(raw);
    showToast('Voz: ' + t);

    if (/^ir a /.test(t) || /^ve a /.test(t) || /^abrir (menu|menú) /.test(t)){
      if (/\bgaraje\b/.test(t))     { show('garaje'); return; }
      if (/\bluz|luces\b/.test(t))  { show('luces'); return; }
      if (/\btv\b/.test(t))         { show('tv'); return; }
      if (/\bcortin/.test(t))       { show('cortinas'); return; }
      if (/\bventilador\b/.test(t)) { show('ventilador'); return; }
      if (/\bpuerta\b/.test(t))     { show('puerta'); return; }
      if (/\bmonitoreo\b/.test(t))  { show('auto'); return; }
      if (/\binicio|home\b/.test(t)){ show('home'); return; }
      if (/\bmanual\b/.test(t))     { show('manual-menu'); return; }
    }

    if (/\b(encender)\b.*\bluz\b/.test(t) || /\bluz\b.*\bencender\b/.test(t)) {
      await cmd('LED 255'); await cmd('STATE?'); return;
    }
    if (/\b(apagar)\b.*\bluz\b/.test(t) || /\bluz\b.*\bapagar\b/.test(t)) {
      await cmd('LED 0'); await cmd('STATE?'); return;
    }
    let m = t.match(/luz.*?(\d{1,3}) ?%/) || t.match(/(\d{1,3}) ?%.*?luz/);
    if (m) {
      let pct  = Math.max(0, Math.min(100, parseInt(m[1],10)));
      let duty = Math.round(pct * 255 / 100);
      await cmd('LED ' + duty); await cmd('STATE?'); return;
    }

    if (/\babrir\b.*\bgaraje\b/.test(t)) { await cmd('GARAGE OPEN'); return; }
    if (/\bcerrar\b.*\bgaraje\b/.test(t)){ await cmd('GARAGE CLOSE'); return; }

    if (/\babrir\b.*\bcortin/.test(t))  { await cmd('CURTAIN OPEN'); return; }
    if (/\bcerrar\b.*\bcortin/.test(t)) { await cmd('CURTAIN CLOSE'); return; }

    if ((/\bventilador\b.*\balto\b/.test(t)) || (/\balto\b.*\bventilador\b/.test(t)) || (/\bventilador\b.*\bmax(imo)?\b/.test(t))) { await cmd('FAN 3'); return; }
    if ((/\bventilador\b.*\bmedio\b/.test(t)) || (/\bmedio\b.*\bventilador\b/.test(t))) { await cmd('FAN 2'); return; }
    if ((/\bventilador\b.*\bbajo\b/.test(t)) || (/\bbajo\b.*\bventilador\b/.test(t)) || (/\bventilador\b.*\blow\b/.test(t))) { await cmd('FAN 1'); return; }
    if ((/\bventilador\b.*\b(apagar|off)\b/.test(t)) || (/\b(apagar|off)\b.*\bventilador\b/.test(t))) { await cmd('FAN 0'); return; }

    if (/\btv\b.*\b(encender|apagar|power)\b/.test(t) || /\b(encender|apagar|power)\b.*\btv\b/.test(t)) { await cmd('TV POWER'); return; }
    if (/\b(subir volumen|tv .* subir .* volumen)\b/.test(t)) { await cmd('TV VOL+'); return; }
    if (/\b(bajar volumen|tv .* bajar .* volumen)\b/.test(t)) { await cmd('TV VOL-'); return; }
    if (/\b(canal siguiente|tv .* canal .* siguiente)\b/.test(t)) { await cmd('TV CH+'); return; }
    if (/\b(canal anterior|tv .* canal .* anterior)\b/.test(t)) { await cmd('TV CH-'); return; }

    if (/\babrir\b.*\bpuerta\b/.test(t) || /\bpuerta\b.*\babrir\b/.test(t)) { await cmd('DOOR OPEN'); return; }
    if (/\bcerrar\b.*\bpuerta\b/.test(t) || /\bpuerta\b.*\bcerrar\b/.test(t)) { await cmd('DOOR CLOSE'); return; }

    showToast('No entendí el comando');
  }
})();
</script>
</body>
</html>)HTML";

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=utf-8");
  client.println("Cache-Control: no-cache");
  client.println("Connection: close");
  client.println();
  client.print(html);
}

// ========================= Helpers =========================
void sendJSON(WiFiClient &client, const String& json, int code) {
  client.print("HTTP/1.1 ");
  client.print(code);
  client.println(" OK");
  client.println("Content-Type: application/json; charset=utf-8");
  client.println("Cache-Control: no-cache");
  client.println("Connection: close");
  client.println();
  client.print(json);
}

String urlDecode(const String& s){
  String out;
  out.reserve(s.length());
  for (size_t i=0;i<s.length();i++){
    char c = s[i];
    if (c=='%' && i+2<s.length()){
      auto hex = [](char h)->int{
        if (h>='0'&&h<='9') return h-'0';
        if (h>='A'&&h<='F') return 10+(h-'A');
        if (h>='a'&&h<='f') return 10+(h-'a');
        return -1;
      };
      int a = hex(s[i+1]), b = hex(s[i+2]);
      if (a!=-1 && b!=-1){
        out += char(a*16+b);
        i+=2;
        continue;
      }
    }
    if (c=='+') out+=' '; else out += c;
  }
  return out;
}

String getParam(const String& query, const String& key) {
  int p = query.indexOf(key + "=");
  if (p < 0) return "";
  int s = p + key.length() + 1;
  int e = query.indexOf('&', s);
  String v = (e < 0) ? query.substring(s) : query.substring(s, e);
  return urlDecode(v);
}

String getLocalIP(){
  return WiFi.localIP().toString();
}