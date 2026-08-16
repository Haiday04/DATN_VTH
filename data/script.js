// ===== ELEMENTS =====
const form          = document.getElementById('wifi-form');
const ssidSelect    = document.getElementById('ssid-select');
const passInput     = document.getElementById('pass');
const saveBtn       = document.getElementById('save-btn');
const btnText       = document.getElementById('btn-text');
const btnSpinner    = document.getElementById('btn-spinner');
const statusBar     = document.getElementById('status-bar');
const togglePass    = document.getElementById('toggle-pass');
const eyeIcon       = document.getElementById('eye-icon');
const checkBtn      = document.getElementById('check-btn');
const currentInfo   = document.getElementById('current-info');
const currentSSID   = document.getElementById('current-ssid');
const scanBtn       = document.getElementById('scan-btn');
const scanText      = document.getElementById('scan-text');
const networkInfo   = document.getElementById('network-info');
const signalIcon    = document.getElementById('signal-icon');
const networkDetail = document.getElementById('network-detail');
const openNote      = document.getElementById('open-note');
const BASE = window.location.hostname === '192.168.4.1'
  ? 'http://esp32-vth.local'
  : '';

let networks = [];
let lastSavedIP = '';

// ===== SHOW / HIDE PASSWORD =====
togglePass.addEventListener('click', () => {
  const isPassword = passInput.type === 'password';
  passInput.type = isPassword ? 'text' : 'password';
  eyeIcon.innerHTML = isPassword
    ? `<path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94"/>
       <path d="M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19"/>
       <line x1="1" y1="1" x2="23" y2="23"/>`
    : `<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/>
       <circle cx="12" cy="12" r="3"/>`;
});

// ===== STATUS DISPLAY =====
function showStatus(message, type = 'info') {
  statusBar.textContent = message;
  statusBar.className   = `status-bar ${type}`;
  statusBar.classList.remove('hidden');
}
function hideStatus() { statusBar.classList.add('hidden'); }

// ===== LOADING STATE =====
function setLoading(loading) {
  saveBtn.disabled = loading;
  btnText.classList.toggle('hidden', loading);
  btnSpinner.classList.toggle('hidden', !loading);
}

// ===== RSSI → SIGNAL BARS =====
function signalBar(rssi) {
  if (rssi >= -50) return '▂▄▆█';
  if (rssi >= -65) return '▂▄▆_';
  if (rssi >= -75) return '▂▄__';
  return '▂___';
}

function signalColor(rssi) {
  if (rssi >= -50) return '#15803d';
  if (rssi >= -65) return '#a16207';
  if (rssi >= -75) return '#c2410c';
  return '#b91c1c';
}

// ===== SCAN WIFI =====
scanBtn.addEventListener('click', async () => {
  scanBtn.disabled = true;
  scanBtn.classList.add('scanning');
  scanText.textContent = 'Scanning...';
  hideStatus();
  networkInfo.classList.add('hidden');
  ssidSelect.innerHTML = '<option value="">Scanning for networks...</option>';
  ssidSelect.disabled = true;
  saveBtn.disabled = true;
  passInput.value = '';
  openNote.classList.add('hidden');

  try {
    const res = await fetch('/scan');
    networks  = await res.json();

    ssidSelect.innerHTML = '<option value="">-- Select a WiFi network --</option>';

    if (networks.length === 0) {
      ssidSelect.innerHTML = '<option value="">No networks found</option>';
      showStatus('No WiFi networks found nearby', 'error');
    } else {
      networks.sort((a, b) => b.rssi - a.rssi);

      networks.forEach((net, idx) => {
        const opt = document.createElement('option');
        opt.value = idx;
        opt.textContent = `${signalBar(net.rssi)}  ${net.ssid}${net.secure ? '  🔒' : '  🔓'}`;
        ssidSelect.appendChild(opt);
      });

      ssidSelect.disabled = false;
      showStatus(`Found ${networks.length} network${networks.length > 1 ? 's' : ''}`, 'success');
    }
  } catch (err) {
    ssidSelect.innerHTML = '<option value="">Scan failed</option>';
    showStatus('Failed to scan WiFi networks', 'error');
  } finally {
    scanBtn.disabled = false;
    scanBtn.classList.remove('scanning');
    scanText.textContent = 'Re-scan';
  }
});

// ===== SELECT NETWORK → UPDATE UI =====
ssidSelect.addEventListener('change', () => {
  const idx = ssidSelect.value;

  if (idx === '') {
    networkInfo.classList.add('hidden');
    openNote.classList.add('hidden');
    passInput.disabled = false;
    passInput.value    = '';
    saveBtn.disabled   = true;
    return;
  }

  const net = networks[parseInt(idx)];

  signalIcon.textContent = signalBar(net.rssi);
  signalIcon.style.color = signalColor(net.rssi);
  networkDetail.textContent = `${net.ssid} · ${net.rssi} dBm · ${net.secure ? 'Password required' : 'Open network'}`;
  networkInfo.classList.remove('hidden');

  if (!net.secure) {
    passInput.value    = '';
    passInput.disabled = true;
    openNote.classList.remove('hidden');
  } else {
    passInput.disabled = false;
    openNote.classList.add('hidden');
    passInput.focus();
  }

  saveBtn.disabled = false;
  hideStatus();
});

// ===== SAVE WIFI CONFIGURATION =====
form.addEventListener('submit', async (e) => {
  e.preventDefault();
  hideStatus();

  const idx = ssidSelect.value;
  if (idx === '') {
    showStatus('Please select a WiFi network', 'error');
    return;
  }

  const net  = networks[parseInt(idx)];
  const ssid = net.ssid;
  const pass = net.secure ? passInput.value : '';

  if (net.secure && pass.length === 0) {
    showStatus('Please enter the WiFi password', 'error');
    passInput.focus();
    return;
  }

  setLoading(true);

  try {
    const url = `/save?ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(pass)}`;
    await fetch(url).catch(() => {});
    passInput.value = '';
  } catch (_) {}

  setLoading(false);

  let countdown = 15;
  showStatus(`⏳ ESP32 restarting... (${countdown}s)`, 'info');

  const timer = setInterval(() => {
    countdown--;
    if (countdown > 0) {
      showStatus(`⏳ ESP32 restarting... (${countdown}s)`, 'info');
    } else {
      clearInterval(timer);

      statusBar.innerHTML = `
  <div style="line-height: 1.75; text-align: left;">
    <b style="font-size: 1.15em; color: #0c4a6e;">ESP32 has restarted</b><br>
    
    <div style="display: flex; flex-direction: column; gap: 18px; margin-top: 10px;">

      <!-- Success -->
      <div style="display: flex; align-items: flex-start; gap: 14px;">
        <span style="font-size: 1.7em;">✅</span>
        <div>
          <b style="font-size: 1.22em; color: #15803d;">Success</b><br>
          <span style="font-size: 1.06em; color: #0c4a6e;">
            Join WiFi <b>"${ssid}"</b> then open: 
            <a href="http://esp32-vth.local" target="_blank" 
               style="display: inline-block; margin-top: 4px;
                      color: #15803d; font-weight: 700; text-decoration: none;  
                      background: rgba(21,128,61,0.12); 
                      padding: 4px 10px; border-radius: 6px; 
                      border: 1px solid rgba(21,128,61,0.45);">
              esp32-vth.local
            </a>
          </span>
        </div>
      </div>

      <!-- Failed -->
      <div style="display: flex; align-items: flex-start; gap: 14px;">
        <span style="font-size: 1.7em;">❌</span>
        <div>
          <b style="font-size: 1.22em; color: #b91c1c;">Failed</b><br>
          <span style="font-size: 1.06em; color: #0c4a6e;">
            Join WiFi <b>"ESP32_AP"</b> then open: 
            <a href="http://192.168.4.1" target="_blank" 
               style="display: inline-block; margin-top: 4px;
                      color: #b91c1c; font-weight: 700; text-decoration: none;  
                      background: rgba(185,28,28,0.1); 
                      padding: 4px 10px; border-radius: 6px; 
                      border: 1px solid rgba(185,28,28,0.4);">
              192.168.4.1
            </a>
          </span>
        </div>
      </div>

    </div>
  </div>
`;
      statusBar.className = 'status-bar info';
      statusBar.classList.remove('hidden');
    }
  }, 1000);
});

// ===== CHECK CONNECTION STATUS =====
checkBtn.addEventListener('click', async () => {
  checkBtn.disabled    = true;
  checkBtn.textContent = 'Checking...';
  hideStatus();

  try {
    const res  = await fetch('/status');
    const data = await res.json();

    if (data.connected) {
      showStatus('ESP32 is connected to WiFi ✓', 'success');
      currentSSID.textContent = data.ssid || '---';
      currentInfo.classList.remove('hidden');
    } else {
      showStatus('ESP32 is not connected to WiFi', 'error');
      currentInfo.classList.add('hidden');
    }
  } catch (err) {
    showStatus('Failed to get connection status', 'error');
  } finally {
    checkBtn.disabled    = false;
    checkBtn.textContent = 'Check Connection Status';
  }
});

// ===== AUTO SCAN + CHECK STATUS ON PAGE LOAD =====
window.addEventListener('load', async () => {
  try {
    const res    = await fetch('/last-result');
    const raw    = await res.text();
    const [result] = raw.split('|');

    if (result === 'fail') {
      statusBar.innerHTML = `
        ❌ <b>Previous connection failed</b><br>
        → Connect to WiFi <b>ESP32_AP</b> then open
        <a href="http://192.168.4.1" target="_blank" style="color:#b91c1c; font-weight:700;">192.168.4.1</a>
      `;
      statusBar.className = 'status-bar error';
      statusBar.classList.remove('hidden');
    } else if (result === 'ok') {
      statusBar.innerHTML = `
        ✅ <b>Connected successfully!</b><br>
        → Connect to WiFi <b>your WiFi</b> then open
        <a href="http://esp32-vth.local" target="_blank" style="color:#15803d; font-weight:700;">esp32-vth.local</a>
      `;
      statusBar.className = 'status-bar success';
      statusBar.classList.remove('hidden');
    }
  } catch (_) {}

  scanBtn.click();
});