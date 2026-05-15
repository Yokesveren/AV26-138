import asyncio
import json
import threading
import serial
import time
import pandas as pd
import numpy as np
from contextlib import asynccontextmanager
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse
from datetime import datetime
from sklearn.ensemble import RandomForestClassifier
from sklearn.preprocessing import LabelEncoder
import joblib
import os

# ------------------- CONFIGURATION -------------------
SERIAL_PORT = 'COM10'         # Change to your Monitoring Node's COM port
BAUD_RATE = 115200
USE_MOCK = False
LOG_FILE = "sensor_log.csv"
MODEL_FILE = "disaster_predictor.pkl"
ENCODER_FILE = "label_encoder.pkl"

# Node information and locations
NODES = {
    'C': {"name": "Earthquake Sensor", "type": "Seismic", "unit": "Vibration", "icon": "fa-mountain", "color": "#ff6b6b", "lat": 13.0524, "lng": 80.2507},
    'D': {"name": "Ocean/Storm Sensor", "type": "Weather", "unit": "Temp/Hum", "icon": "fa-water", "color": "#4d9de0", "lat": 13.0024, "lng": 80.2707},
    'E': {"name": "Landslide Sensor", "type": "Geotech", "unit": "Motion (m/s²)", "icon": "fa-arrow-down", "color": "#f0b27a", "lat": 13.0674, "lng": 80.2376},
    'F': {"name": "Medical Node", "type": "Health", "unit": "BPM / SpO₂", "icon": "fa-heartbeat", "color": "#a855f7", "lat": 13.0827, "lng": 80.2103}
}
DRONE_BASE = {"lat": 13.0827, "lng": 80.2707}

# Global data storage
node_data = {
    'C': {"value": 0, "sos": False, "msg": "Stable", "last_seen": ""},
    'D': {"value": 0, "sos": False, "msg": "Environment OK", "last_seen": "", "temp": 0, "hum": 0},
    'E': {"value": 0, "sos": False, "msg": "Stable", "last_seen": ""},
    'F': {"value": 0, "sos": False, "msg": "Waiting...", "last_seen": "", "bpm": 0, "spo2": 0}
}
active_alert = None
alert_acknowledged = False
alert_ack_time = 0
connected_clients = set()
loop = None

# ------------------- AI Model (Disaster Predictor) -------------------
# Rolling windows: store last 10 readings per sensor
rolling_data = {node: [] for node in NODES}
rolling_humidity = []            # separate list for humidity (from node D)
predicted_disaster = None
prediction_confidence = 0
last_training_time = time.time()

def load_or_train_model():
    global model, encoder
    if os.path.exists(MODEL_FILE) and os.path.exists(ENCODER_FILE):
        model = joblib.load(MODEL_FILE)
        encoder = joblib.load(ENCODER_FILE)
        print("✅ Loaded existing AI disaster predictor")
    else:
        # Train on simulated historical data
        train_with_historical_data()
        joblib.dump(model, MODEL_FILE)
        joblib.dump(encoder, ENCODER_FILE)
        print("✅ Trained and saved new AI predictor")

def train_with_historical_data():
    global model, encoder
    print("🔄 Training AI on simulated historical disaster patterns...")
    np.random.seed(42)
    data = []
    labels = []
    for _ in range(5000):
        vib = np.random.uniform(0, 3)
        temp = np.random.uniform(20, 40)
        hum = np.random.uniform(30, 90)
        motion = np.random.uniform(0, 5)
        bpm = np.random.uniform(60, 120)
        # Label based on thresholds
        if vib > 2.0:
            label = 'Earthquake'
        elif motion > 3.0:
            label = 'Landslide'
        elif temp > 35 or hum > 80:
            label = 'Storm'
        elif bpm > 130:
            label = 'Medical'
        else:
            label = 'None'
        data.append([vib, temp, hum, motion, bpm])
        labels.append(label)
    X = np.array(data)
    y = np.array(labels)
    encoder = LabelEncoder()
    y_enc = encoder.fit_transform(y)
    model = RandomForestClassifier(n_estimators=50, max_depth=5, random_state=42)
    model.fit(X, y_enc)
    print("✅ Synthetic training complete")

def update_rolling_data(node_id, val1, val2):
    """Store recent readings for AI prediction"""
    global rolling_humidity
    if node_id == 'C':
        rolling_data['C'].append(val1)
    elif node_id == 'D':
        rolling_data['D'].append(val1)      # temperature
        rolling_humidity.append(val2)       # humidity
        if len(rolling_humidity) > 10:
            rolling_humidity = rolling_humidity[-10:]
    elif node_id == 'E':
        rolling_data['E'].append(val1)
    elif node_id == 'F':
        rolling_data['F'].append(val1)      # bpm
    # Keep only last 10 readings per node
    for k in rolling_data:
        if len(rolling_data[k]) > 10:
            rolling_data[k] = rolling_data[k][-10:]

def predict_disaster():
    """Use current rolling averages to predict next disaster type (5 features)"""
    global predicted_disaster, prediction_confidence
    # Compute averages
    avg_vib = np.mean(rolling_data['C']) if rolling_data['C'] else 0
    avg_temp = np.mean(rolling_data['D']) if rolling_data['D'] else 25
    avg_hum = np.mean(rolling_humidity) if rolling_humidity else 50
    avg_motion = np.mean(rolling_data['E']) if rolling_data['E'] else 0
    avg_bpm = np.mean(rolling_data['F']) if rolling_data['F'] else 70
    
    # Feature vector must match training: [vib, temp, hum, motion, bpm]
    features = np.array([[avg_vib, avg_temp, avg_hum, avg_motion, avg_bpm]])
    
    try:
        proba = model.predict_proba(features)[0]
        pred_idx = model.predict(features)[0]
        predicted_disaster = encoder.inverse_transform([pred_idx])[0]
        prediction_confidence = round(max(proba) * 100, 1)
    except Exception as e:
        print(f"Prediction error: {e}")
        predicted_disaster = "Unknown"
        prediction_confidence = 0

# Load or train AI model
model = None
encoder = None
load_or_train_model()

# ------------------- SERIAL READER -------------------
def read_serial():
    global node_data, active_alert, loop, rolling_humidity
    ser = None
    if not USE_MOCK:
        try:
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.01)
            print(f"✅ Connected to {SERIAL_PORT}")
            time.sleep(2)
            ser.reset_input_buffer()
        except Exception as e:
            print(f"❌ Serial error: {e}. Set USE_MOCK=True to test.")
            return

    # Initialize mock data if needed
    if USE_MOCK:
        for node in rolling_data:
            rolling_data[node] = list(np.random.uniform(0, 3, 5))
        rolling_humidity = list(np.random.uniform(40, 80, 5))

    while True:
        line = None
        if not USE_MOCK and ser and ser.is_open:
            try:
                if ser.in_waiting:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if not line:
                        continue
                    if line.startswith(('ets', 'rst:', 'configsip', 'clk_drv', 'q_drv', 'load:', 'entry:')):
                        continue
                    if line.count(',') < 4:
                        continue
                    if line[0] not in NODES:
                        continue
                    print(f"📡 {line}")
            except:
                continue

        if line:
            parts = line.split(',')
            if len(parts) >= 5:
                node_id = parts[0].strip()
                try:
                    val1 = float(parts[1])
                    val2 = float(parts[2])
                    is_sos = parts[3].strip() == '1'
                    msg = ','.join(parts[4:]).strip()
                except ValueError:
                    continue

                now_str = datetime.now().strftime("%H:%M:%S")
                # Update node data and rolling history
                if node_id == 'C':
                    node_data['C'] = {"value": val1, "sos": is_sos, "msg": msg, "last_seen": now_str}
                    update_rolling_data('C', val1, 0)
                elif node_id == 'D':
                    node_data['D'] = {"value": val1, "sos": is_sos, "msg": msg, "last_seen": now_str, "temp": val1, "hum": val2}
                    update_rolling_data('D', val1, val2)
                elif node_id == 'E':
                    node_data['E'] = {"value": val1, "sos": is_sos, "msg": msg, "last_seen": now_str}
                    update_rolling_data('E', val1, 0)
                elif node_id == 'F':
                    node_data['F'] = {"value": val1, "sos": is_sos, "msg": msg, "last_seen": now_str, "bpm": int(val1), "spo2": int(val2)}
                    update_rolling_data('F', val1, val2)

                # Log to CSV for historical learning (append)
                log_row = {
                    'timestamp': datetime.now().isoformat(),
                    'node': node_id,
                    'val1': val1,
                    'val2': val2,
                    'is_sos': is_sos,
                    'msg': msg
                }
                df_log = pd.DataFrame([log_row])
                if not os.path.isfile(LOG_FILE):
                    df_log.to_csv(LOG_FILE, index=False)
                else:
                    df_log.to_csv(LOG_FILE, mode='a', header=False, index=False)

                # Update global alert if SOS
                global alert_acknowledged, alert_ack_time
                if is_sos and not (alert_acknowledged and time.time() - alert_ack_time < 10):
                    active_alert = {"node": node_id, **NODES[node_id]}
                elif not is_sos and active_alert and active_alert['node'] == node_id:
                    active_alert = None

                # Run AI prediction every 5 seconds
                global last_training_time
                if time.time() - last_training_time > 5:
                    predict_disaster()
                    last_training_time = time.time()
        else:
            # Still run prediction occasionally even if no new data
            if time.time() - last_training_time > 5:
                predict_disaster()
                last_training_time = time.time()

        # Build state for frontend
        state = {
            "nodes": node_data,
            "active_alert": active_alert,
            "ai_prediction": {
                "disaster": predicted_disaster,
                "confidence": prediction_confidence
            }
        }
        if loop:
            asyncio.run_coroutine_threadsafe(broadcast_state(state), loop)

        time.sleep(0.02)

async def broadcast_state(state):
    if connected_clients:
        msg = json.dumps({"type": "state", "data": state})
        await asyncio.gather(*[ws.send_text(msg) for ws in list(connected_clients)], return_exceptions=True)

# ------------------- FASTAPI -------------------
@asynccontextmanager
async def lifespan(app: FastAPI):
    global loop
    loop = asyncio.get_running_loop()
    threading.Thread(target=read_serial, daemon=True).start()
    yield

app = FastAPI(lifespan=lifespan)

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    connected_clients.add(websocket)
    await websocket.send_text(json.dumps({"type": "state", "data": {"nodes": node_data, "active_alert": active_alert, "ai_prediction": {"disaster": predicted_disaster, "confidence": prediction_confidence}}}))
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        connected_clients.remove(websocket)

@app.get("/ack_alert")
async def ack_alert():
    global alert_acknowledged, alert_ack_time, active_alert
    alert_acknowledged = True
    alert_ack_time = time.time()
    active_alert = None
    return {"status": "ok"}

@app.get("/emergency_call")
async def emergency_call():
    return {"status": "called", "message": "Emergency services notified"}

@app.get("/retrain_model")
async def retrain_model():
    # Re-train using the logged CSV data
    global model, encoder
    if os.path.exists(LOG_FILE):
        df = pd.read_csv(LOG_FILE)
        # For hackathon, we retrain on synthetic data again (you can enhance with real logs)
        train_with_historical_data()
        joblib.dump(model, MODEL_FILE)
        joblib.dump(encoder, ENCODER_FILE)
        return {"status": "retrained", "samples": len(df)}
    else:
        return {"status": "no data yet"}

@app.get("/")
async def get():
    return HTMLResponse("""
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>AI-Powered Decentralized Offline Disaster Response System</title>
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
    <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0-beta3/css/all.min.css">
    <link href="https://fonts.googleapis.com/css2?family=Inter:opsz,wght@14..32,300;400;500;600;700;800&display=swap" rel="stylesheet">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: 'Inter', sans-serif; background: #0a0f1e; color: #eef2ff; overflow: hidden; height: 100vh; }
        .dashboard { display: flex; height: 100%; flex-direction: row; }
        .sidebar {
            width: 380px;
            background: rgba(18, 25, 45, 0.85);
            backdrop-filter: blur(16px);
            border-right: 1px solid rgba(96, 165, 250, 0.2);
            padding: 20px;
            overflow-y: auto;
            z-index: 10;
            display: flex;
            flex-direction: column;
            gap: 16px;
        }
        .logo {
            font-size: 1.3rem;
            font-weight: 800;
            background: linear-gradient(135deg, #60a5fa, #a855f7);
            -webkit-background-clip: text;
            background-clip: text;
            color: transparent;
            display: flex;
            align-items: center;
            gap: 10px;
            margin-bottom: 8px;
        }
        .card {
            background: rgba(30, 41, 59, 0.6);
            border-radius: 28px;
            padding: 18px;
            border: 1px solid rgba(96, 165, 250, 0.2);
            transition: all 0.2s;
            backdrop-filter: blur(4px);
        }
        .ai-card {
            background: linear-gradient(135deg, rgba(96,165,250,0.2), rgba(168,85,247,0.2));
            border: 1px solid #a855f7;
        }
        .node-card {
            border-left: 4px solid;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        .node-card:hover { transform: translateY(-3px); box-shadow: 0 8px 20px rgba(0,0,0,0.3); }
        .node-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; }
        .node-title { font-weight: 700; font-size: 1.1rem; display: flex; align-items: center; gap: 8px; }
        .sos-badge { background: #ef4444; border-radius: 40px; padding: 4px 12px; font-size: 0.7rem; font-weight: 700; animation: pulse 1s infinite; }
        .value-row { display: flex; justify-content: space-between; margin: 10px 0; font-size: 0.9rem; }
        .timestamp { font-size: 0.7rem; color: #94a3b8; margin-top: 8px; text-align: right; }
        .button-group { display: flex; gap: 12px; margin-top: 8px; flex-wrap: wrap; }
        .btn {
            background: rgba(96, 165, 250, 0.2);
            border: 1px solid #60a5fa;
            padding: 8px 16px;
            border-radius: 40px;
            color: white;
            font-weight: 500;
            cursor: pointer;
            transition: 0.2s;
            font-size: 0.8rem;
            display: inline-flex;
            align-items: center;
            gap: 6px;
        }
        .btn:hover { background: #60a5fa; color: #0a0f1e; transform: scale(1.02); }
        .btn-danger { background: rgba(239,68,68,0.2); border-color: #ef4444; }
        .btn-danger:hover { background: #ef4444; color: white; }
        .map-container { flex: 1; position: relative; }
        #disasterMap { height: 100%; width: 100%; background: #0f172a; }
        .drone-status { position: absolute; bottom: 20px; right: 20px; background: rgba(0,0,0,0.7); backdrop-filter: blur(8px); padding: 8px 18px; border-radius: 40px; font-size: 0.85rem; z-index: 1000; pointer-events: none; font-weight: 500; }
        @keyframes pulse { 0% { opacity: 0.7; transform: scale(1); } 50% { opacity: 1; transform: scale(1.05); } 100% { opacity: 0.7; transform: scale(1); } }
        footer { font-size: 0.7rem; text-align: center; margin-top: 12px; color: #64748b; }
        .prediction-text { font-size: 1.2rem; font-weight: bold; color: #a855f7; }
        @media (max-width: 900px) { .sidebar { width: 100%; height: 50%; position: absolute; bottom: 0; left: 0; right: 0; border-right: none; border-top: 1px solid rgba(96,165,250,0.2); } .dashboard { flex-direction: column; } .map-container { height: 50%; } }
    </style>
</head>
<body>
<div class="dashboard">
    <div class="sidebar">
        <div class="logo"><i class="fas fa-robot"></i> AI-Powered Decentralized<br>Disaster Response System</div>
        <div class="button-group" style="justify-content: space-between;">
            <button class="btn" id="ackBtn"><i class="fas fa-check-circle"></i> Acknowledge Alert</button>
            <button class="btn" id="centerMapBtn"><i class="fas fa-location-dot"></i> Center Map</button>
            <button class="btn btn-danger" id="emergencyBtn"><i class="fas fa-phone-alt"></i> Emergency Call</button>
        </div>
        <div class="card ai-card">
            <h3><i class="fas fa-brain"></i> AI Disaster Forecast</h3>
            <div class="value-row"><span>🔮 Next predicted event:</span><span class="prediction-text" id="aiPrediction">--</span></div>
            <div class="value-row"><span>📊 Confidence:</span><span id="aiConfidence">--%</span></div>
            <div style="font-size:0.75rem; margin-top:8px;">Based on recent sensor trends & historical patterns</div>
        </div>
        <div id="nodesContainer"></div>
        <footer>Real‑time ESP‑NOW | AI predicts disasters before they happen</footer>
    </div>
    <div class="map-container">
        <div id="disasterMap"></div>
        <div class="drone-status"><i class="fas fa-drone"></i> Drone: <span id="droneState">Idle</span></div>
    </div>
</div>

<script>
    var map = L.map('disasterMap').setView([13.0827, 80.2707], 12);
    L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
        attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OSM</a> & CartoDB',
        subdomains: 'abcd'
    }).addTo(map);
    
    var droneIcon = L.divIcon({ html: '<div style="font-size: 34px; filter: drop-shadow(0 0 5px #0ea5e9);"><i class="fas fa-drone"></i></div>', iconSize: [34,34] });
    var nodeIcons = {
        'C': L.divIcon({ html: '<i class="fas fa-mountain" style="font-size: 28px; color: #ff6b6b;"></i>', iconSize: [28,28] }),
        'D': L.divIcon({ html: '<i class="fas fa-water" style="font-size: 28px; color: #4d9de0;"></i>', iconSize: [28,28] }),
        'E': L.divIcon({ html: '<i class="fas fa-arrow-down" style="font-size: 28px; color: #f0b27a;"></i>', iconSize: [28,28] }),
        'F': L.divIcon({ html: '<i class="fas fa-heartbeat" style="font-size: 28px; color: #a855f7;"></i>', iconSize: [28,28] })
    };
    var locations = { 'C': [13.0524,80.2507], 'D': [13.0024,80.2707], 'E': [13.0674,80.2376], 'F': [13.0827,80.2103] };
    for (let node in locations) L.marker(locations[node], { icon: nodeIcons[node] }).addTo(map).bindPopup(`<b>Node ${node}</b>`);
    
    var droneMarker = L.marker([13.0827, 80.2707], { icon: droneIcon }).addTo(map).bindPopup("Rescue Drone");
    L.marker([13.0827, 80.2707], { icon: L.divIcon({ html: '<i class="fas fa-home" style="font-size: 24px; color:#38bdf8;"></i>', iconSize: [24,24] }) }).addTo(map).bindPopup("Drone Base");
    var flightPath = L.polyline([], { color: '#38bdf8', weight: 3, dashArray: '6,8' }).addTo(map);
    
    var isDroneMoving = false;
    var animFrame = null;
    function flyDroneTo(lat, lng, name) {
        if (isDroneMoving) { if (animFrame) cancelAnimationFrame(animFrame); }
        isDroneMoving = true;
        document.getElementById("droneState").innerText = "En Route to " + name;
        var start = droneMarker.getLatLng();
        var end = L.latLng(lat, lng);
        var duration = 2000;
        var startTime = null;
        flightPath.setLatLngs([start, end]);
        function animate(t) {
            if (!startTime) startTime = t;
            var elapsed = t - startTime;
            var prog = Math.min(1, elapsed / duration);
            var lat = start.lat + (end.lat - start.lat) * prog;
            var lng = start.lng + (end.lng - start.lng) * prog;
            droneMarker.setLatLng([lat, lng]);
            if (prog < 1) animFrame = requestAnimationFrame(animate);
            else {
                droneMarker.setLatLng(end);
                document.getElementById("droneState").innerText = "On Scene – " + name;
                isDroneMoving = false;
                setTimeout(() => { if (!isDroneMoving) flyDroneTo(13.0827, 80.2707, "Base"); }, 5000);
                animFrame = null;
            }
        }
        animFrame = requestAnimationFrame(animate);
    }
    
    function renderNodes(data) {
        const container = document.getElementById("nodesContainer");
        if (!container) return;
        let html = "";
        const order = ['C','D','E','F'];
        for (let node of order) {
            const info = data.nodes[node];
            const meta = window.nodeMeta[node];
            if (!info) continue;
            const sosClass = info.sos ? '<span class="sos-badge">SOS ACTIVE</span>' : '';
            let valueDisplay = "";
            if (node === 'C') valueDisplay = `Vibration: ${info.value}`;
            else if (node === 'D') valueDisplay = `Temp: ${info.temp}°C / Hum: ${info.hum}%`;
            else if (node === 'E') valueDisplay = `Motion: ${info.value.toFixed(2)} m/s²`;
            else if (node === 'F') valueDisplay = `BPM: ${info.bpm} | SpO₂: ${info.spo2}%`;
            html += `
                <div class="card node-card" style="border-left-color: ${meta.color};">
                    <div class="node-header">
                        <div class="node-title"><i class="fas ${meta.icon}"></i> ${meta.name}</div>
                        ${sosClass}
                    </div>
                    <div class="value-row"><span>📊 ${meta.unit}</span><span><strong>${valueDisplay}</strong></span></div>
                    <div class="value-row"><span>💬 Message</span><span>${info.msg}</span></div>
                    <div class="timestamp"><i class="far fa-clock"></i> ${info.last_seen || '--'}</div>
                </div>
            `;
        }
        container.innerHTML = html;
    }
    
    window.nodeMeta = {
        'C': { name: "Earthquake Sensor", unit: "Vibration", icon: "fa-mountain", color: "#ff6b6b" },
        'D': { name: "Ocean/Storm Sensor", unit: "Temp/Humidity", icon: "fa-water", color: "#4d9de0" },
        'E': { name: "Landslide Sensor", unit: "Motion", icon: "fa-arrow-down", color: "#f0b27a" },
        'F': { name: "Medical Node", unit: "BPM / SpO₂", icon: "fa-heartbeat", color: "#a855f7" }
    };
    
    var ws;
    function connectWebSocket() {
        ws = new WebSocket(`ws://${window.location.host}/ws`);
        ws.onmessage = (event) => {
            const data = JSON.parse(event.data);
            if (data.type === "state") {
                renderNodes(data.data);
                if (data.data.ai_prediction) {
                    document.getElementById("aiPrediction").innerHTML = data.data.ai_prediction.disaster || "None";
                    document.getElementById("aiConfidence").innerHTML = (data.data.ai_prediction.confidence || 0) + "%";
                }
                if (data.data.active_alert) {
                    const alertNode = data.data.active_alert;
                    flyDroneTo(alertNode.lat, alertNode.lng, alertNode.name);
                }
            }
        };
        ws.onclose = () => setTimeout(connectWebSocket, 2000);
        ws.onerror = (err) => console.error("WS error", err);
    }
    connectWebSocket();
    
    document.getElementById("ackBtn").addEventListener("click", async () => {
        await fetch("/ack_alert");
        if (animFrame) cancelAnimationFrame(animFrame);
        isDroneMoving = false;
        document.getElementById("droneState").innerText = "Idle (Acknowledged)";
        flyDroneTo(13.0827, 80.2707, "Base");
    });
    document.getElementById("centerMapBtn").addEventListener("click", () => map.setView([13.0827, 80.2707], 12));
    document.getElementById("emergencyBtn").addEventListener("click", async () => {
        await fetch("/emergency_call");
        alert("🚑 Emergency services notified (simulated).");
    });
</script>
</body>
</html>
    """)

if __name__ == "__main__":
    import uvicorn
    print("="*60)
    print("🤖 AI-Powered Decentralized Offline Disaster Response System")
    print(f"Serial port: {SERIAL_PORT if not USE_MOCK else 'MOCK MODE'}")
    print("🌐 Open http://localhost:8000")
    print("="*60)
    uvicorn.run(app, host="0.0.0.0", port=8000)
