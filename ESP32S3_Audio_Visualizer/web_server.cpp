// THIS FILE IS NO LONGER USED — replaced by serial_cmd.cpp
// Delete this file from your project.
#error "web_server.cpp is deprecated. Delete this file and web_server.h from the project."

/*******************************************************************************
 * Web Server — WiFi AP + ESPAsyncWebServer
 * 
 * Serves a single-page settings UI from PROGMEM.
 * REST API for reading/writing settings as JSON.
 ******************************************************************************/

static AsyncWebServer server(80);

// Forward declaration — fps is defined in .ino
extern volatile float fps;

// ─── HTML UI (stored in PROGMEM) ───────────────────────────────────────────
static const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Audio Visualizer Settings</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#0f0f0f;color:#e0e0e0;padding:16px;max-width:480px;margin:0 auto}
h1{font-size:1.3em;color:#00bcd4;margin-bottom:12px;text-align:center}
h2{font-size:1em;color:#00bcd4;margin:16px 0 8px;padding-bottom:4px;border-bottom:1px solid #333}
.card{background:#1a1a1a;border-radius:8px;padding:12px;margin-bottom:10px}
.row{display:flex;justify-content:space-between;align-items:center;margin:6px 0}
.row label{font-size:0.85em;color:#aaa}
.row span{font-size:0.85em;color:#fff;font-weight:600}
input[type=range]{width:55%;accent-color:#00bcd4;height:20px}
select{background:#2a2a2a;color:#e0e0e0;border:1px solid #444;border-radius:4px;padding:4px 8px;font-size:0.85em}
button{background:#00bcd4;color:#000;border:none;border-radius:4px;padding:8px 16px;font-weight:600;cursor:pointer;font-size:0.85em}
button:active{opacity:0.7}
button.danger{background:#f44336;color:#fff}
.status{font-size:0.75em;color:#666;text-align:center;margin-top:8px}
.chip{display:inline-block;background:#2a2a2a;border:1px solid #444;border-radius:4px;padding:4px 10px;margin:2px;cursor:pointer;font-size:0.85em}
.chip.active{background:#00bcd4;color:#000;border-color:#00bcd4;font-weight:600}
.toggle{position:relative;width:42px;height:22px;display:inline-block}
.toggle input{display:none}
.toggle .slider{position:absolute;inset:0;background:#444;border-radius:11px;cursor:pointer;transition:.2s}
.toggle input:checked+.slider{background:#00bcd4}
.toggle .slider:before{content:'';position:absolute;width:18px;height:18px;left:2px;bottom:2px;background:#fff;border-radius:50%;transition:.2s}
.toggle input:checked+.slider:before{transform:translateX(20px)}
</style>
</head>
<body>
<h1>&#127925; Audio Visualizer</h1>

<div class="card">
<h2>Visualization</h2>
<div class="row"><label>Mode</label>
<div id="vizModes">
<span class="chip" data-v="0">Spectrum</span>
<span class="chip" data-v="1">VU Needle</span>
<span class="chip" data-v="2">VU LED</span>
</div></div>
<div class="row"><label>ADC Sensitivity</label><span id="sensVal">300</span></div>
<div class="row"><input type="range" id="sens" min="50" max="2000" step="10" value="300"></div>
</div>

<div class="card">
<h2>Display</h2>
<div class="row"><label>Brightness</label><span id="briVal">255</span></div>
<div class="row"><input type="range" id="bri" min="0" max="255" value="255"></div>
<div class="row"><label>FPS</label><span id="fpsVal">--</span></div>
</div>

<div class="card">
<h2>DAC (AK4493)</h2>
<div class="row"><label>Volume L</label><span id="volLVal">0</span></div>
<div class="row"><input type="range" id="volL" min="0" max="255" value="0"></div>
<div class="row"><label>Volume R</label><span id="volRVal">0</span></div>
<div class="row"><input type="range" id="volR" min="0" max="255" value="0"></div>
<div class="row"><label>Filter</label>
<select id="dacFilter">
<option value="0">Sharp roll-off</option>
<option value="1">Slow roll-off</option>
<option value="2">Short delay sharp</option>
<option value="3">Short delay slow</option>
<option value="4">Super slow</option>
</select></div>
<div class="row"><label>Mute</label>
<label class="toggle"><input type="checkbox" id="dacMute"><span class="slider"></span></label>
</div>
</div>

<div class="card">
<h2>System</h2>
<div class="row"><label>Free Heap</label><span id="heap">--</span></div>
<div class="row"><label>Uptime</label><span id="uptime">--</span></div>
<div class="row" style="justify-content:center;margin-top:8px">
<button class="danger" onclick="if(confirm('Restart device?'))post('/api/restart',{})">Restart</button>
</div>
</div>

<div class="status">Connect to device WiFi &bull; 192.168.4.1</div>

<script>
const $=s=>document.querySelector(s);
const $$=s=>document.querySelectorAll(s);

function post(url,body){
  fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
}

function refresh(){
  fetch('/api/status').then(r=>r.json()).then(d=>{
    $('#fpsVal').textContent=d.fps.toFixed(1);
    $('#heap').textContent=(d.free_heap/1024).toFixed(0)+' KB';
    let s=Math.floor(d.uptime/1000);
    let m=Math.floor(s/60); s%=60;
    let h=Math.floor(m/60); m%=60;
    $('#uptime').textContent=h+'h '+m+'m '+s+'s';
    setViz(d.viz_mode);
    $('#bri').value=d.brightness; $('#briVal').textContent=d.brightness;
    $('#sens').value=d.adc_sensitivity; $('#sensVal').textContent=d.adc_sensitivity;
    $('#volL').value=d.dac_volume_l; $('#volLVal').textContent=d.dac_volume_l;
    $('#volR').value=d.dac_volume_r; $('#volRVal').textContent=d.dac_volume_r;
    $('#dacFilter').value=d.dac_filter;
    $('#dacMute').checked=d.dac_mute;
  }).catch(()=>{});
}

function setViz(v){
  $$('.chip').forEach(c=>{c.classList.toggle('active',c.dataset.v==v)});
}

$$('.chip').forEach(c=>{
  c.addEventListener('click',()=>{
    let v=parseInt(c.dataset.v);
    setViz(v);
    post('/api/settings',{viz_mode:v});
  });
});

$('#bri').addEventListener('input',e=>{
  $('#briVal').textContent=e.target.value;
  post('/api/settings',{brightness:parseInt(e.target.value)});
});

$('#sens').addEventListener('input',e=>{
  $('#sensVal').textContent=e.target.value;
  post('/api/settings',{adc_sensitivity:parseFloat(e.target.value)});
});

$('#volL').addEventListener('input',e=>{
  $('#volLVal').textContent=e.target.value;
  post('/api/settings',{dac_volume_l:parseInt(e.target.value)});
});

$('#volR').addEventListener('input',e=>{
  $('#volRVal').textContent=e.target.value;
  post('/api/settings',{dac_volume_r:parseInt(e.target.value)});
});

$('#dacFilter').addEventListener('change',e=>{
  post('/api/settings',{dac_filter:parseInt(e.target.value)});
});

$('#dacMute').addEventListener('change',e=>{
  post('/api/settings',{dac_mute:e.target.checked});
});

refresh();
setInterval(refresh,2000);
</script>
</body>
</html>
)rawhtml";

// ─── WiFi AP Setup ─────────────────────────────────────────────────────────
static void wifi_ap_init()
{
    // Generate unique SSID from MAC
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "AudioViz-%02X%02X", mac[4], mac[5]);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, "audioviz");
    delay(100);

    Serial.printf("WiFi AP started: %s\n", ssid);
    Serial.printf("Web UI: http://%s\n", WiFi.softAPIP().toString().c_str());
}

// ─── API Handlers ──────────────────────────────────────────────────────────
static void setup_routes()
{
    // Serve HTML UI
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", INDEX_HTML);
    });

    // GET /api/status — return current state as JSON
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["viz_mode"]         = settings.viz_mode;
        doc["brightness"]       = settings.brightness;
        doc["adc_sensitivity"]  = settings.adc_sensitivity;
        doc["dac_volume_l"]     = settings.dac_volume_l;
        doc["dac_volume_r"]     = settings.dac_volume_r;
        doc["dac_filter"]       = settings.dac_filter;
        doc["dac_sound_mode"]   = settings.dac_sound_mode;
        doc["dac_mute"]         = (bool)settings.dac_mute;
        doc["mouse_sens"]       = settings.mouse_sens;
        doc["mouse_mode"]       = settings.mouse_mode;
        doc["fps"]              = fps;
        doc["free_heap"]        = ESP.getFreeHeap();
        doc["uptime"]           = millis();

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // POST /api/settings — update one or more settings
    AsyncCallbackJsonWebHandler *settingsHandler = new AsyncCallbackJsonWebHandler(
        "/api/settings",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            JsonObject obj = json.as<JsonObject>();

            if (obj["viz_mode"].is<int>()) {
                settings.viz_mode = obj["viz_mode"].as<uint8_t>();
                settings_save_field("viz_mode");
            }
            if (obj["brightness"].is<int>()) {
                settings.brightness = obj["brightness"].as<uint8_t>();
                analogWrite(TFT_BL, settings.brightness);
                settings_save_field("brightness");
            }
            if (obj["adc_sensitivity"].is<float>()) {
                settings.adc_sensitivity = obj["adc_sensitivity"].as<float>();
                settings_save_field("adc_sens");
            }
            if (obj["dac_volume_l"].is<int>()) {
                settings.dac_volume_l = obj["dac_volume_l"].as<uint8_t>();
                settings_save_field("dac_vol_l");
            }
            if (obj["dac_volume_r"].is<int>()) {
                settings.dac_volume_r = obj["dac_volume_r"].as<uint8_t>();
                settings_save_field("dac_vol_r");
            }
            if (obj["dac_filter"].is<int>()) {
                settings.dac_filter = obj["dac_filter"].as<uint8_t>();
                settings_save_field("dac_filter");
            }
            if (obj["dac_sound_mode"].is<int>()) {
                settings.dac_sound_mode = obj["dac_sound_mode"].as<uint8_t>();
                settings_save_field("dac_sound");
            }
            if (obj["dac_mute"].is<bool>()) {
                settings.dac_mute = obj["dac_mute"].as<bool>();
                settings_save_field("dac_mute");
            }
            if (obj["mouse_sens"].is<float>()) {
                settings.mouse_sens = obj["mouse_sens"].as<float>();
                settings_save_field("mouse_sens");
            }
            if (obj["mouse_mode"].is<int>()) {
                settings.mouse_mode = obj["mouse_mode"].as<uint8_t>();
                settings_save_field("mouse_mode");
            }

            request->send(200, "application/json", "{\"ok\":true}");
        }
    );
    server.addHandler(settingsHandler);

    // POST /api/restart
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"ok\":true}");
        delay(500);
        ESP.restart();
    });
}

// ─── Public Init ───────────────────────────────────────────────────────────
void web_server_init()
{
    wifi_ap_init();
    setup_routes();
    server.begin();
    Serial.println("Web server started on port 80");
}
