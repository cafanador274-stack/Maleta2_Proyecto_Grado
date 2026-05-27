#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// ====== LIBRERÍAS Y CONFIGURACIÓN OLED ======
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_SDA 15 // Pin 15 para SDA
#define OLED_SCL 2  // Pin 2 para SCL

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
unsigned long ultimoOLEDUpdate = 0;
int oledFrame = 0;

const char* ssid     = "Maleta3";
const char* password = "123456789";

WebServer server(80);

// ====== SALIDAS HACIA PLC / RELÉS ======
const int PIN_START_OUT = 5;
// Marcha banda (pulso de arranque)
const int PIN_STOP_OUT  = 18;
// Pulso de paro banda

// ====== ENTRADAS DESDE PLC (MODOS DE COLOR) ======
const int PIN_MODE_RA = 16;
// Selección Rojo/Azul (RA)
const int PIN_MODE_VA = 4;     // Selección Verde/Amarillo (VA)

// ====== ENTRADA EXTRA DESDE PLC + RELÉ ======
const int PIN_EXTRA_IN    = 19;
// Nueva entrada desde PLC
const int PIN_EXTRA_RELAY = 23;  // Salida hacia relé (ENERGÍA DE LA BANDA)

// ====== SENSOR TCS3200 ======
const int S0 = 26;
const int S1 = 25;
const int S2 = 14;
const int S3 = 12;
const int OUT_PIN = 13;
// ====== SERVOS DE CLASIFICACIÓN ======
Servo servo1;
Servo servo2;
const int SERVO1_PIN = 21;
const int SERVO2_PIN = 22;
const int SERVO1_OFF_ANGLE = 90;   
const int SERVO1_ON_ANGLE  = 45;  

const int SERVO2_OFF_ANGLE = 90;
const int SERVO2_ON_ANGLE  = 45;  

const int SERVO1_EMPUJE_ANGLE = SERVO1_ON_ANGLE - 10;  
const int SERVO2_EMPUJE_ANGLE = SERVO2_ON_ANGLE - 10;
const unsigned long TIEMPO_ESPERA_ACTIVACION = 1700;   // Tiempo de viaje del sensor al servo
const unsigned long TIEMPO_MANTENER_ACTIVO   = 7000;
// Tiempo que el servo se queda abajo
const unsigned long TIEMPO_ANTES_EMPUJE      = 1000;
// ====== ESTADOS ======
bool bandaEncendida = false;     
String colorMode = "RA";         
String ultimoColorDetectado = "NONE";
// Variables globales para enviar los valores crudos a la App
int lastR = 0;
int lastG = 0;
int lastB = 0;

int lastStateRA = HIGH;
int lastStateVA = HIGH;
int  baseR = 0, baseG = 0, baseB = 0;
bool fondoCalibrado = false;
long fondoDiffTotalMedio = 0;
long fondoUmbralTotal    = 60;   

bool servo21Activo = false;
bool servo22Activo = false;
unsigned long servo21DetectStart = 0;
unsigned long servo22DetectStart = 0;
bool servo21EmpujeHecho = false;
bool servo22EmpujeHecho = false;

String lastColorMode = "RA";
bool servosInicializados = false;

// Control de secuencia de parada
bool esperandoQueSalgaObjeto = false;
// =======================================================
//   PÁGINA HTML (TOTALMENTE EXTENDIDA COMO EL ORIGINAL)
// =======================================================
const char MAIN_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Control Banda Transportadora</title>
  <style>
    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }

    body {
      min-height: 100vh;
      display: flex;
     
      align-items: center;
      justify-content: center;
      background: radial-gradient(circle at top, #e5f0ff 0, #f9fafb 45%, #eef2ff 100%);
      color: #111827;
    }

    .screen {
      width: 100%;
      max-width: 500px;
      padding: 20px;
      display: flex;
      align-items: center;
      justify-content: center;
    }

    .hidden {
      display: none;
    }

    .card {
      width: 100%;
      border-radius: 24px;
      background: #ffffff;
      box-shadow:
        0 18px 45px rgba(15, 23, 42, 0.15),
        0 0 0 1px rgba(148, 163, 184, 0.18);
      overflow: hidden;
    }

    .card-header-bar {
      height: 8px;
      width: 100%;
      background: linear-gradient(90deg, #2563eb, #22c55e, #f97316, #6366f1);
    }

    .card-inner {
      padding: 18px 18px 20px;
    }

    button {
      border: none;
      border-radius: 999px;
      padding: 12px 16px;
      font-size: 0.95rem;
      font-weight: 600;
      cursor: pointer;
      transition: transform 0.08s ease, box-shadow 0.1s ease, background 0.15s ease;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
      width: 100%;
      letter-spacing: 0.01em;
      white-space: nowrap;
    }

    button:active {
      transform: scale(0.97);
      box-shadow: none;
    }

    .btn-primary {
      background: linear-gradient(135deg, #2563eb, #1d4ed8);
      color: white;
      box-shadow: 0 12px 26px rgba(37, 99, 235, 0.5);
      border: 1px solid #2563eb;
    }

    .btn-primary:hover {
      background: linear-gradient(135deg, #3b82f6, #2563eb);
    }

    .btn-start {
      background: linear-gradient(135deg, #22c55e, #16a34a);
      color: white;
      box-shadow: 0 10px 22px rgba(34, 197, 94, 0.45);
      border: 1px solid #16a34a;
    }

    .btn-stop {
      background: linear-gradient(135deg, #f97373, #dc2626);
      color: white;
      box-shadow: 0 10px 22px rgba(239, 68, 68, 0.45);
      border: 1px solid #dc2626;
    }

    .btn-mode {
      background: #f9fafb;
      border-radius: 999px;
      border: 1px solid #d1d5db;
      color: #111827;
      font-weight: 500;
    }

    .btn-mode:hover {
      background: #e5f0ff;
      border-color: #93c5fd;
    }

    .btn-mode.active {
      border-color: #2563eb;
      background: linear-gradient(135deg, #e0edff, #eff6ff);
      box-shadow: 0 0 0 1px rgba(37, 99, 235, 0.4);
      color: #1d4ed8;
    }

    .welcome-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 18px;
    }

    .welcome-title {
      font-size: 1.2rem;
      font-weight: 700;
      color: #0f172a;
    }

    .welcome-subtitle {
      margin-top: 4px;
      font-size: 0.85rem;
      color: #6b7280;
    }

    .chip-online {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      padding: 4px 10px;
      border-radius: 999px;
      background: #ecfdf3;
      color: #166534;
      font-size: 0.72rem;
      border: 1px solid #bbf7d0;
    }

    .chip-dot {
      width: 8px;
      height: 8px;
      border-radius: 999px;
      background: #22c55e;
      box-shadow: 0 0 0 4px rgba(34, 197, 94, 0.38);
    }

    .welcome-hero {
      margin: 12px 0 16px;
      border-radius: 18px;
      padding: 14px 12px;
      background: radial-gradient(circle at top left, #eff6ff 0, #f9fafb 40%, #eef2ff 100%);
      border: 1px solid #e5e7eb;
      display: grid;
      grid-template-columns: 1.2fr 1fr;
      gap: 12px;
      align-items: center;
    }

    .welcome-hero-text {
      font-size: 0.86rem;
      color: #4b5563;
      line-height: 1.5;
    }

    .welcome-list {
      font-size: 0.81rem;
      color: #4b5563;
      margin-bottom: 14px;
    }

    .welcome-list ul {
      margin-top: 6px;
      margin-left: 18px;
      display: grid;
      gap: 4px;
    }

    .welcome-footer {
      margin-top: 10px;
      font-size: 0.75rem;
      color: #6b7280;
    }

    .header-main {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 16px;
      gap: 10px;
    }

    .main-title {
      font-size: 1.05rem;
      font-weight: 700;
      color: #0f172a;
    }

    .main-subtitle {
      font-size: 0.82rem;
      color: #6b7280;
      margin-top: 3px;
    }

    .status-pill {
      padding: 5px 10px;
      border-radius: 999px;
      font-size: 0.72rem;
      display: inline-flex;
      align-items: center;
      gap: 6px;
      background: #f9fafb;
      border: 1px solid #e5e7eb;
      color: #374151;
    }

    .status-dot {
      width: 9px;
      height: 9px;
      border-radius: 999px;
      background: #f97316;
      box-shadow: 0 0 0 3px rgba(249, 115, 22, 0.25);
    }

    .status-text {
      text-transform: uppercase;
      letter-spacing: 0.06em;
    }

    .section {
      margin-top: 14px;
      border-radius: 18px;
      border: 1px solid #e5e7eb;
      padding: 12px 11px 12px;
      background: #f9fafb;
    }

    .section-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 8px;
    }

    .section-title {
      font-size: 0.88rem;
      font-weight: 600;
      color: #111827;
    }

    .section-badge {
      font-size: 0.72rem;
      padding: 3px 8px;
      border-radius: 999px;
      border: 1px solid #e5e7eb;
      color: #6b7280;
      background: #ffffff;
    }

    .btn-row {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 10px;
    }

    .message {
      margin-top: 7px;
      font-size: 0.78rem;
      min-height: 16px;
      color: #2563eb;
      text-align: center;
    }

    .footer-text {
      font-size: 0.74rem;
      text-align: center;
      margin-top: 10px;
      color: #9ca3af;
    }

    .belt-container {
      margin-top: 4px;
      margin-bottom: 6px;
    }

    .belt-frame {
      border-radius: 18px;
      border: 1px solid #e5e7eb;
      padding: 10px 10px 12px;
      background: #ffffff;
    }

    .belt-wrapper {
      position: relative;
      margin-top: 4px;
      height: 82px;
      display: flex;
      align-items: center;
      justify-content: center;
    }

    .belt-track {
      position: relative;
      width: 100%;
      max-width: 360px;
      height: 38px;
      border-radius: 999px;
      background-color: #f3f4f6;
      border: 1px solid #e5e7eb;
      overflow: hidden;
      box-shadow: inset 0 0 10px rgba(148, 163, 184, 0.4);
    }

    .belt-surface {
      position: absolute;
      inset: 4px;
      border-radius: 999px;
      background-image: repeating-linear-gradient(
        135deg,
        #d1d5db 0px,
        #d1d5db 6px,
        #e5e7eb 6px,
        #e5e7eb 12px
      );
      background-size: 24px 24px;
    }

    .belt-running .belt-surface {
      animation: beltMove 0.55s linear infinite;
    }

    @keyframes beltMove {
      from { background-position: 0 0;
      }
      to   { background-position: -24px 0;
      }
    }

    .belt-item {
      position: absolute;
      top: 50%;
      width: 32px;
      height: 20px;
      border-radius: 6px;
      transform: translateY(-50%);
      box-shadow: 0 5px 8px rgba(148, 163, 184, 0.8);
    }

    .belt-item.red { background: linear-gradient(145deg, #dc2626, #f97373); }
    .belt-item.blue { background: linear-gradient(145deg, #2563eb, #60a5fa);
    }
    .belt-item.green { background: linear-gradient(145deg, #16a34a, #4ade80);
    }

    .belt-running .belt-item {
      animation: itemsMove 2.4s linear infinite;
    }

    @keyframes itemsMove {
      from { transform: translate(-20%, -50%);
      }
      to   { transform: translate(120%, -50%);
      }
    }

    .belt-item.item1 { left: 5%; }
    .belt-item.item2 { left: 35%;
    }
    .belt-item.item3 { left: 65%; }

    .color-indicator {
      margin-top: 10px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 8px;
      font-size: 0.8rem;
    }

    .color-pill {
      flex: 1;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
      padding: 6px 10px;
      border-radius: 999px;
      border: 1px solid #e5e7eb;
      background: #ffffff;
      font-size: 0.8rem;
      color: #4b5563;
    }

    .color-dot {
      width: 14px;
      height: 14px;
      border-radius: 999px;
      border: 1px solid #e5e7eb;
      background: #e5e7eb;
    }

    /* ESTILOS PARA LOS VALORES RGB */
    #rgb-values {
      margin-top: 8px;
      font-size: 0.85rem;
      font-weight: 600;
      text-align: center;
      color: #1f2937;
      background: #f3f4f6;
      padding: 6px;
      border-radius: 8px;
      border: 1px dashed #cbd5e1;
    }

    .calibrate-container {
      margin-top: 10px;
    }

    @media (max-width: 480px) {
      .welcome-hero {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>

  <div class="screen" id="screen-welcome">
    <div class="card">
      <div class="card-header-bar"></div>
      <div class="card-inner">
        <div class="welcome-header">
          <div>
            <div class="welcome-title">Panel de Control</div>
            <div class="welcome-subtitle">Banda transportadora · ESP32 · PLC</div>
          </div>
         
          <div class="chip-online">
            <span class="chip-dot"></span>
            <span>ESP32 listo</span>
          </div>
        </div>

        <div class="welcome-hero">
          <div class="welcome-hero-text">
            Pulsadores virtuales con memoria:<br>
            • <strong>Inicio</strong>: pulso de arranque hacia PLC.<br>
 
            • <strong>Paro</strong>: pulso de parada hacia PLC.<br><br>
            El sensor de color clasifica piezas en 2 colores<br>
            según el modo elegido (RA o VA).
          </div>
          <div></div>
        </div>

        <div class="welcome-list">
          Este panel te permite:
          <ul>
            <li>Enviar pulsos de inicio y paro.</li>
            <li>Usar salidas hacia relés / optoacopladores.</li>
            <li>Seleccionar el modo de 
color (App o PLC).</li>
            <li>Ver el color detectado y calibrar en tiempo real.</li>
          </ul>
        </div>

        <button id="btn-enter" class="btn-primary">
          ➜ Entrar al panel de control
        </button>

        <div class="welcome-footer">
          Conéctate a la misma red WiFi que 
el ESP32 para usar el panel.
        </div>
      </div>
    </div>
  </div>

  <div class="screen hidden" id="screen-main">
    <div class="card">
      <div class="card-header-bar"></div>
      <div class="card-inner">
        <header class="header-main">
          <div>
            <div class="main-title">Control de Banda</div>
            <div class="main-subtitle">Inicio / Paro · Modos de color</div>
     
          </div>
          <div class="status-pill">
            <span class="status-dot" id="status-dot"></span>
            <span class="status-text" id="status-text">Detenida</span>
          </div>
        </header>

        <div class="belt-container">
          <div class="belt-frame">
            <div class="belt-wrapper">
     
              <div class="belt-track" id="belt-track">
                <div class="belt-surface"></div>
                <div class="belt-item red item1"></div>
                <div class="belt-item blue item2"></div>
                <div class="belt-item green item3"></div>
              </div>
 
            </div>
          </div>
        </div>

        <section class="section">
          <div class="section-header">
            <div class="section-title">Control de banda</div>
            <div class="section-badge">Start (5) · Stop (18)</div>
          </div>
         
          <div class="btn-row">
            <button id="btn-start" class="btn-start">⏵ Inicio</button>
            <button id="btn-stop" class="btn-stop">⏹ Paro</button>
          </div>
          <div class="message" id="message"></div>
        </section>

        <section class="section">
          <div class="section-header">
            <div class="section-title">Modo de colores</div>
   
          <div class="section-badge">Lógica de clasificación</div>
          </div>
          <div class="btn-row">
            <button id="mode-ra" class="btn-mode active" data-mode="RA">🔴 Rojo / 🔵 Azul</button>
            <button id="mode-va" class="btn-mode" data-mode="VA">🟢 Verde / 🟡 Amarillo</button>
          </div>

          <div class="color-indicator">
       
            <span>Color detectado:</span>
            <div class="color-pill" id="color-pill">
              <span class="color-dot" id="color-dot"></span>
              <span id="color-label">No se detecta objeto</span>
            </div>
          </div>
          
          <div id="rgb-values">Lecturas: R: 0 
| G: 0 | B: 0</div>

          <div class="calibrate-container">
            <button id="btn-calibrar" class="btn-primary">
              Calibrar fondo (sin objeto)
            </button>
          </div>
        </section>

        <div class="footer-text">
          El PLC / relés 
externos se encargan del control de potencia de la banda.
        </div>
      </div>
    </div>
  </div>

  <script>
    const screenWelcome = document.getElementById("screen-welcome");
    const screenMain = document.getElementById("screen-main");
    const btnEnter = document.getElementById("btn-enter");

    const btnStart = document.getElementById("btn-start");
    const btnStop  = document.getElementById("btn-stop");
    const statusDot = document.getElementById("status-dot");
    const statusText = document.getElementById("status-text");
    const beltTrack = document.getElementById("belt-track");
    const msg = document.getElementById("message");
    const modeButtons = document.querySelectorAll(".btn-mode");
    const colorDot = document.getElementById("color-dot");
    const colorLabel = document.getElementById("color-label");
    const rgbValues = document.getElementById("rgb-values");
    const btnCalibrar = document.getElementById("btn-calibrar");

    let currentMode = "RA";
    let currentBeltRunning = false;
    let lastColorSensor = "NONE";

    btnEnter.addEventListener("click", () => {
      screenWelcome.classList.add("hidden");
      screenMain.classList.remove("hidden");
    });
    function setMessage(text) {
      msg.textContent = text || "";
      if (text) {
        setTimeout(() => {
          if (msg.textContent === text) msg.textContent = "";
        }, 2000);
      }
    }

    function setStatus(running) {
      currentBeltRunning = running;
      if (running) {
        statusText.textContent = "En marcha";
        statusDot.style.background = "#22c55e";
        beltTrack.classList.add("belt-running");
      } else {
        statusText.textContent = "Detenida";
        statusDot.style.background = "#f97316";
        beltTrack.classList.remove("belt-running");
      }
    }

    function updateColorIndicator(color) {
      let label = "No se detecta objeto";
      let bg = "#e5e7eb";

      if (color === "ROJO") {
        label = "Rojo";
        bg = "#ef4444";
      } else if (color === "AZUL") {
        label = "Azul";
        bg = "#3b82f6";
      } else if (color === "VERDE") {
        label = "Verde";
        bg = "#22c55e";
      } else if (color === "AMARILLO") {
        label = "Amarillo";
        bg = "#facc15";
      }

      colorLabel.textContent = label;
      colorDot.style.background = bg;
    }

    async function send(path, onOk) {
      try {
        const res = await fetch(path, { method: "GET" });
        if (res.ok && onOk) onOk();
      } catch (e) {
        console.error(e);
        setMessage("Error de comunicación.");
      }
    }

    btnStart.addEventListener("click", () => {
      send("/start", () => {
        setStatus(true);
        setMessage("Pulso de inicio enviado.");
      });
    });
    btnStop.addEventListener("click", () => {
      send("/stop", () => {
        setStatus(false);
        setMessage("Pulso de paro enviado.");
      });
    });
    modeButtons.forEach((btn) => {
      btn.addEventListener("click", () => {
        modeButtons.forEach((b) => b.classList.remove("active"));
        btn.classList.add("active");
        currentMode = btn.dataset.mode;
        setMessage(
          currentMode === "RA"
            ? "Modo Rojo/Azul seleccionado."
            : "Modo Verde/Amarillo seleccionado."
        );
   
        send("/setMode?mode=" + encodeURIComponent(currentMode));
      });
    });
    btnCalibrar.addEventListener("click", () => {
      setMessage("Calibrando fondo, retira cualquier objeto...");
      send("/calibrate", () => {
        setMessage("Fondo calibrado.");
      });
    });
    async function refreshState() {
      try {
        const res = await fetch("/state");
        if (!res.ok) return;
        const data = await res.json();

        // AQUÍ REVERTIMOS EL CAMBIO DEL RELÉ 23 COMO PEDISTE
        // La web obedece nuevamente a la variable original 'bandaEncendida'
        if (typeof data.bandaEncendida === "boolean") {
          if (data.bandaEncendida !== currentBeltRunning) {
            setStatus(data.bandaEncendida);
          }
        }

        // === AQUÍ SE ACTUALIZA SI EL PLC MANDA LA SEÑAL ===
        if (data.colorMode && data.colorMode !== currentMode) {
          currentMode = data.colorMode;
          modeButtons.forEach((b) => {
            if (b.dataset.mode === currentMode) {
              b.classList.add("active");
            } else {
              b.classList.remove("active");
            }
          });
        }

        if (data.colorSensor && data.colorSensor !== lastColorSensor) {
          lastColorSensor = data.colorSensor;
          updateColorIndicator(lastColorSensor);
        }
        
        // Actualizamos los valores RGB en pantalla
        if (data.r !== undefined && data.g !== undefined && data.b !== undefined) {
           rgbValues.textContent = "Lecturas: R: " + data.r + " | G: " + data.g + " | B: " + data.b;
        }
        
      } catch (e) {}
    }

    setInterval(refreshState, 800);
    setStatus(false);
    updateColorIndicator("NONE");
  </script>
</body>
</html>
)rawliteral";

// =======================
//   MANEJADORES HTTP
// =======================

void handleRoot() {
  server.send_P(200, "text/html", MAIN_page);
}

void handleStart() {
  bandaEncendida = true;  
  digitalWrite(PIN_STOP_OUT, HIGH);   
  digitalWrite(PIN_START_OUT, LOW);   
  delay(150);                         
  digitalWrite(PIN_START_OUT, HIGH);  
  server.send(200, "text/plain", "OK START");
}

void handleStop() {
  bandaEncendida = false;
  digitalWrite(PIN_START_OUT, HIGH);
  digitalWrite(PIN_STOP_OUT, LOW);
  delay(150);
  digitalWrite(PIN_STOP_OUT, HIGH);
  server.send(200, "text/plain", "OK STOP");
}

void handleSetMode() {
  if (server.hasArg("mode")) {
 
    colorMode = server.arg("mode");
  }
  server.send(200, "text/plain", "OK MODE");
}

void handleCalibrate() {
  calibrarFondo();
  server.send(200, "text/plain", "OK CALIBRADO");
}

void handleState() {
  String json = "{";
  json += "\"bandaEncendida\":" + String(bandaEncendida ? "true" : "false") + ",";
  json += "\"colorMode\":\"" + colorMode + "\",";
  json += "\"colorSensor\":\"" + ultimoColorDetectado + "\",";
  json += "\"r\":" + String(lastR) + ",";
  json += "\"g\":" + String(lastG) + ",";
  json += "\"b\":" + String(lastB);
  json += "}";
  server.send(200, "application/json", json);
}

// =======================
//   FUNCIONES SENSOR
// =======================

int readColorFrequency(int filterS2, int filterS3) {
  digitalWrite(S2, filterS2);
  digitalWrite(S3, filterS3);
  delay(5);

  unsigned long t = pulseIn(OUT_PIN, LOW, 50000);
  if (t == 0) t = 99999;
  return (int)t;
}

void calibrarFondo() {
  const int M = 15;
  long sumR = 0, sumG = 0, sumB = 0;
  for (int i = 0; i < M; i++) {
    sumR += readColorFrequency(LOW, LOW);
    sumB += readColorFrequency(LOW, HIGH);
    sumG += readColorFrequency(HIGH, LOW);
    delay(15);
  }

  baseR = sumR / M;
  baseG = sumG / M;
  baseB = sumB / M;

  long sumDiffTotal = 0;
  for (int i = 0; i < M; i++) {
    long dR = abs(readColorFrequency(LOW, LOW) - baseR);
    long dG = abs(readColorFrequency(HIGH, LOW) - baseG);
    long dB = abs(readColorFrequency(LOW, HIGH) - baseB);
    sumDiffTotal += (dR + dG + dB);
    delay(15);
  }

  fondoDiffTotalMedio = sumDiffTotal / M;
  fondoUmbralTotal = fondoDiffTotalMedio + 30;
  fondoCalibrado = true;
}

// =======================================================
//   DETECCIÓN DE CAMBIO BRUSCO (Filtro anti-luz)
// =======================================================
bool hayObjetoFrenteAlSensor() {
  if (!fondoCalibrado) return false;
  int rojo  = readColorFrequency(LOW, LOW);
  int azul  = readColorFrequency(LOW, HIGH);
  int verde = readColorFrequency(HIGH, LOW);

  lastR = rojo;
  lastG = verde;
  lastB = azul;

  long diffTotal = abs(rojo - baseR) + abs(verde - baseG) + abs(azul - baseB);
  return (diffTotal > fondoUmbralTotal + 100);
}

// =======================
//   DETECCIÓN DE COLOR
// =======================

String leerUnSoloColor() {
  int rojo  = readColorFrequency(LOW, LOW);
  int azul  = readColorFrequency(LOW, HIGH);
  int verde = readColorFrequency(HIGH, LOW);

  lastR = rojo;
  lastG = verde;
  lastB = azul;
  int distRojo     = abs(rojo - 126) + abs(verde - 56) + abs(azul - 192);
  int distAmarillo = abs(rojo - 100) + abs(verde - 41) + abs(azul - 166);
  int distAzul     = abs(rojo - 229) + abs(verde - 56) + abs(azul - 136);
  int distVerde    = abs(rojo - 216) + abs(verde - 64) + abs(azul - 202);
  int minimaDistancia = distRojo;
  String colorDetectado = "ROJO";

  if (distAmarillo < minimaDistancia) {
    minimaDistancia = distAmarillo;
    colorDetectado = "AMARILLO";
  }
  if (distAzul < minimaDistancia) {
    minimaDistancia = distAzul;
    colorDetectado = "AZUL";
  }
  if (distVerde < minimaDistancia) {
    minimaDistancia = distVerde;
    colorDetectado = "VERDE";
  }

  if (minimaDistancia > 80) {
    return "NONE";
  }

  return colorDetectado;
}

// Función principal que toma 3 fotos para asegurar que el cubo no se movió
String detectarColor() {
  String lectura1 = leerUnSoloColor();
  if (lectura1 == "NONE") return "NONE";

  delay(30); 
  
  String lectura2 = leerUnSoloColor();
  if (lectura1 != lectura2) return "NONE"; 

  delay(30);
  String lectura3 = leerUnSoloColor();
  if (lectura2 != lectura3) return "NONE"; 

  return lectura1;
}

// =======================================================
//   LECTURA DEL PLC (ESTO CAMBIA LA APP SI EL PLC LO MANDA)
// =======================================================
void leerEntradasPLC() {
  int stateRA = digitalRead(PIN_MODE_RA);
  int stateVA = digitalRead(PIN_MODE_VA);

  if (stateRA == LOW && lastStateRA == HIGH) {
    colorMode = "RA";
  }
  if (stateVA == LOW && lastStateVA == HIGH) {
    colorMode = "VA";
  }

  lastStateRA = stateRA;
  lastStateVA = stateVA;
}

// =======================
//   NUEVA LÓGICA SERVOS (POR EVENTOS)
// =======================
void procesarServo(
  Servo &servo,
  bool &activo,
  bool &empujeHecho,
  unsigned long &tInicio,
  int anguloOn,
  int anguloOff,
  int anguloEmpuje
) {
  if (tInicio == 0) return;
  // Si no hay cubo pendiente, no hace nada

  unsigned long ahora = millis();
  unsigned long tiempoTranscurrido = ahora - tInicio;

  // 1. Activar servo después del tiempo de viaje
  if (!activo && tiempoTranscurrido >= TIEMPO_ESPERA_ACTIVACION) {
    activo = true;
    empujeHecho = false;
    servo.write(anguloOn);
  }

  // 2. Hacer un ligero empuje antes de terminar
  if (activo && !empujeHecho) {
    if (tiempoTranscurrido >= (TIEMPO_MANTENER_ACTIVO - TIEMPO_ANTES_EMPUJE) &&
        tiempoTranscurrido < TIEMPO_MANTENER_ACTIVO) {
      servo.write(anguloEmpuje);
      empujeHecho = true;
    }
  }

  // 3. Apagar servo y terminar ciclo
  if (activo && tiempoTranscurrido >= TIEMPO_MANTENER_ACTIVO) {
    activo = false;
    servo.write(anguloOff);
    empujeHecho = false;
    tInicio = 0; // Listo para el próximo cubo
  }
}

// =======================
//  INICIALIZAR SERVOS
// =======================
void inicializarServosSiHaceFalta() {
  if (servosInicializados) return;
  if (millis() < 2000) return;  

  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo1.write(SERVO1_OFF_ANGLE);
  servo2.write(SERVO2_OFF_ANGLE);

  servosInicializados = true;
}

// =======================================================
//   ACTUALIZACIÓN DE PANTALLA OLED
// =======================================================
void actualizarOLED() {
  if (millis() - ultimoOLEDUpdate < 100) return;
  ultimoOLEDUpdate = millis();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // --- 1. MOSTRAR IP ---
  display.setCursor(0, 0);
  display.print("App: ");
  if (WiFi.status() == WL_CONNECTED) {
    display.print(WiFi.localIP());
  } else {
    display.print("Buscando red...");
  }

  // --- 2. MOSTRAR MODO ---
  display.setCursor(0, 10);
  display.print("Modo: ");
  if (colorMode == "RA") {
    display.print("Rojo/Azul");
  } else {
    display.print("Verde/Amar.");
  }

  // --- 3. MOSTRAR COLOR (Se queda arriba 5 segundos) ---
  display.setCursor(0, 20);
  display.print("Color: ");
  
  static String colorParaOLED = "NONE";
  static unsigned long tiempoColorOLED = 0;

  if (ultimoColorDetectado != "NONE") {
    colorParaOLED = ultimoColorDetectado;
    tiempoColorOLED = millis();
  }

  if (colorParaOLED != "NONE" && (millis() - tiempoColorOLED <= 5000)) {
    display.print(colorParaOLED);
  } else {
    display.print("Buscando...");
    colorParaOLED = "NONE";
  }

  // --- 4. ANIMACIÓN DE LA BANDA CON EFECTO DE MOVIMIENTO ---
  // Las poleas ahora están en Y = 48
  display.drawCircle(20, 48, 6, SSD1306_WHITE);
  display.drawCircle(108, 48, 6, SSD1306_WHITE);
  
  display.drawLine(20, 42, 108, 42, SSD1306_WHITE); // Línea superior de la banda
  display.drawLine(20, 54, 108, 54, SSD1306_WHITE);
  // Línea inferior de la banda

  bool enMovimiento = (digitalRead(PIN_EXTRA_RELAY) == HIGH);
  if (enMovimiento) {
    oledFrame++;
  }

  // EFECTO DE ROTACIÓN EN LAS POLEAS (Rayos dentro de los círculos)
  float angulo = oledFrame * 0.4;
  // Velocidad de giro
  display.drawLine(20, 48, 20 + 6 * cos(angulo), 48 + 6 * sin(angulo), SSD1306_WHITE);
  display.drawLine(108, 48, 108 + 6 * cos(angulo), 48 + 6 * sin(angulo), SSD1306_WHITE);
  // EFECTO DE TRACCIÓN EN LA BANDA (Borramos pequeños puntitos para dar efecto de movimiento)
  for (int i = 0; i < 9; i++) {
     int posMarca = (oledFrame + i * 10) % 88;
     display.drawPixel(20 + posMarca, 42, SSD1306_BLACK); // Marcas moviéndose a la derecha arriba
     display.drawPixel(20 + 88 - posMarca, 54, SSD1306_BLACK);
     // Marcas moviéndose a la izquierda abajo
  }

  // CAJITAS SOBRE LA BANDA (En Y=32, no chocan con nada)
  int separacion = 32;
  for (int i = 0; i < 3; i++) {
    int posCaja = (oledFrame * 2 + i * separacion) % 88;
    display.drawRect(20 + posCaja, 32, 10, 10, SSD1306_WHITE); // Cuadro exterior
    display.fillRect(22 + posCaja, 34, 6, 6, SSD1306_WHITE);
    // Cuadro interior (relleno)
  }

  display.display();
}

// =======================
//        SETUP
// =======================
void setup() {
  Serial.begin(115200);
  // Inicialización de la pantalla OLED usando pines 15 (SDA) y 2 (SCL)
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("Fallo al iniciar SSD1306 OLED"));
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 20);
    display.print("Iniciando Banda...");
    display.display();
  }

  pinMode(PIN_START_OUT, OUTPUT);
  pinMode(PIN_STOP_OUT, OUTPUT);

  pinMode(PIN_MODE_RA, INPUT_PULLUP);
  pinMode(PIN_MODE_VA, INPUT_PULLUP);

  pinMode(PIN_EXTRA_IN, INPUT_PULLUP);   
  pinMode(PIN_EXTRA_RELAY, OUTPUT);
  digitalWrite(PIN_EXTRA_RELAY, LOW);

  digitalWrite(PIN_START_OUT, HIGH);
  digitalWrite(PIN_STOP_OUT, HIGH);

  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(OUT_PIN, INPUT);

  digitalWrite(S0, HIGH);
  digitalWrite(S1, LOW);
  calibrarFondo();

  // === WIFI NO BLOQUEANTE ===
  // Inicia sin poner el ESP32 en pausa permanente si no hay internet
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/setMode", handleSetMode);
  server.on("/calibrate", handleCalibrate);
  server.on("/state", handleState);

  server.begin();
}

// =======================
//        LOOP
// =======================
void loop() {
  actualizarOLED(); 
  
  server.handleClient();
  leerEntradasPLC();
  int extraState = digitalRead(PIN_EXTRA_IN);

  // LECTURA NORMAL DEL PLC AL RELÉ 23 (Se ignora si estamos en la pausa obligatoria)
  if (!esperandoQueSalgaObjeto) {
    if (extraState == LOW) {
      digitalWrite(PIN_EXTRA_RELAY, HIGH);
    } else {
      digitalWrite(PIN_EXTRA_RELAY, LOW);
    }
  }

  inicializarServosSiHaceFalta();
  if (colorMode != lastColorMode) {
    lastColorMode = colorMode;

    servo21Activo = false;
    servo22Activo = false;
    servo21DetectStart = 0;
    servo22DetectStart = 0;
    servo21EmpujeHecho = false;
    servo22EmpujeHecho = false;

    if (servosInicializados) {
      servo1.write(SERVO1_OFF_ANGLE);
      servo2.write(SERVO2_OFF_ANGLE);
    }
  }

  // ========================================================
  //   SECUENCIA: CENTRAR, FRENAR ENERGÍA DIRECTO, LEER, CONTINUAR
  // ========================================================
  
  bool hayCubo = hayObjetoFrenteAlSensor();
  // === LIMPIA EL COLOR EN LA APP CUANDO YA NO HAY CUBO ===
  if (!hayCubo) {
    ultimoColorDetectado = "NONE";
  }

  if (hayCubo && !esperandoQueSalgaObjeto) {
    esperandoQueSalgaObjeto = true;
    // 1. TIEMPO DE AVANCE HACIA EL CENTRO
    delay(200); 
    
    unsigned long inicioPausa = millis();
    // 2. CORTE DE ENERGÍA INMEDIATO AL RELÉ 23
    digitalWrite(PIN_EXTRA_RELAY, LOW);
    // 3. Esperar inercia
    delay(400); 

    // 4. Tomar foto
    String color = detectarColor();
    if (color != "NONE") {
      ultimoColorDetectado = color;
    }

    // 5. DEVOLVER EL CONTROL AL PLC (Reanudar relé 23)
    if (extraState == LOW) { 
      digitalWrite(PIN_EXTRA_RELAY, HIGH);
    }

    // 6. COMPENSACIÓN DE TIEMPO A LOS CUBOS EN CAMINO
    unsigned long tiempoDetenido = millis() - inicioPausa;
    if (servo21DetectStart > 0) servo21DetectStart += tiempoDetenido;
    if (servo22DetectStart > 0) servo22DetectStart += tiempoDetenido;
    // 7. INICIAR TEMPORIZADOR PARA EL NUEVO CUBO
    if (color != "NONE") {
      unsigned long ahora = millis();
      if (colorMode == "RA") {
        if (color == "AZUL") servo21DetectStart = ahora;
        if (color == "ROJO") servo22DetectStart = ahora;
      } else if (colorMode == "VA") {
        if (color == "VERDE") servo21DetectStart = ahora;
        if (color == "AMARILLO") servo22DetectStart = ahora;
      }
    }
  }

  // Si el cubo ya pasó y el sensor ya no ve nada, reseteamos la barrera
  if (!hayCubo && esperandoQueSalgaObjeto) {
    esperandoQueSalgaObjeto = false;
  }

  // Actualización contínua de motores
  if (servosInicializados) {
    procesarServo(servo1, servo21Activo, servo21EmpujeHecho, servo21DetectStart, SERVO1_ON_ANGLE, SERVO1_OFF_ANGLE, SERVO1_EMPUJE_ANGLE);
    procesarServo(servo2, servo22Activo, servo22EmpujeHecho, servo22DetectStart, SERVO2_ON_ANGLE, SERVO2_OFF_ANGLE, SERVO2_EMPUJE_ANGLE);
  }
}