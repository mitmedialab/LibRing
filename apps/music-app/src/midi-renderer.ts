import { WebMidi, Output } from 'webmidi';
import { connectToBLE, disconnectFromBLE, setNtfHandler, setBtnHandler } from './lib/shared/accelerometer/ble';
import { AccelChart } from './lib/shared/plot';

// ─── DOM refs ────────────────────────────────────────────────────────────────

const connectBtn     = document.getElementById('connect-btn')      as HTMLButtonElement;
const toggleChartBtn = document.getElementById('toggle-chart-btn') as HTMLButtonElement;
const chartWrapper   = document.getElementById('chart-wrapper')    as HTMLDivElement;
const midiOutputSel  = document.getElementById('midi-output')      as HTMLSelectElement;
const xCCInput       = document.getElementById('x-cc')             as HTMLInputElement;
const yCCInput       = document.getElementById('y-cc')             as HTMLInputElement;
const zCCInput       = document.getElementById('z-cc')             as HTMLInputElement;
const midiChInput    = document.getElementById('midi-channel')     as HTMLInputElement;
const bleStatusEl    = document.getElementById('ble-status')       as HTMLElement;
const midiStatusEl   = document.getElementById('midi-status')      as HTMLElement;
const btnRow         = document.getElementById('btn-row')          as HTMLElement;
const btnIndicator   = document.getElementById('btn-indicator')    as HTMLDivElement;

const xValEl  = document.getElementById('x-cc-val') as HTMLElement;
const yValEl  = document.getElementById('y-cc-val') as HTMLElement;
const zValEl  = document.getElementById('z-cc-val') as HTMLElement;
const xBarEl  = document.getElementById('x-bar')    as HTMLElement;
const yBarEl  = document.getElementById('y-bar')    as HTMLElement;
const zBarEl  = document.getElementById('z-bar')    as HTMLElement;

// ─── State ───────────────────────────────────────────────────────────────────

let isConnected  = false;
let midiOutput: Output | null = null;
let lastSendTime = 0;
const SEND_INTERVAL_MS = 20; // 50 Hz cap

const chart = new AccelChart('accel-chart');

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Map ±2 g → 0..127
function accelToCC(g: number): number {
    return Math.min(127, Math.max(0, Math.round((g + 2.0) / 4.0 * 127)));
}

function clampCC(v: number)      { return Math.min(127, Math.max(0, Math.round(v))); }
function clampChannel(v: number) { return Math.min(15,  Math.max(0, Math.round(v))); }

function updateMeter(barEl: HTMLElement, valEl: HTMLElement, cc: number) {
    barEl.style.width = (cc / 127 * 100) + '%';
    valEl.textContent = String(cc);
    valEl.classList.remove('text-secondary');
}

function sendCC(controller: number, value: number, channel: number) {
    if (!midiOutput) return;
    try {
        // WebMIDI.js uses channels 1–16; user input is 0–15
        midiOutput.sendControlChange(controller, value, { channels: channel + 1 });
    } catch (_) { /* ignore */ }
}

// ─── WebMIDI init ─────────────────────────────────────────────────────────────

function populateMidiOutputs() {
    midiOutputSel.innerHTML = '';
    if (WebMidi.outputs.length === 0) {
        const opt = document.createElement('option');
        opt.value = '';
        opt.textContent = 'No MIDI outputs found';
        midiOutputSel.appendChild(opt);
        midiOutput = null;
        return;
    }
    WebMidi.outputs.forEach((out) => {
        const opt = document.createElement('option');
        opt.value = out.id;
        opt.textContent = out.name;
        midiOutputSel.appendChild(opt);
    });
    midiOutput = WebMidi.outputs[0];
}

async function initMidi() {
    try {
        await WebMidi.enable();
        midiStatusEl.textContent = 'MIDI: Ready';
        midiStatusEl.className = 'badge bg-success';
        populateMidiOutputs();
        WebMidi.addListener('connected',    populateMidiOutputs);
        WebMidi.addListener('disconnected', populateMidiOutputs);
    } catch (err) {
        midiStatusEl.textContent = 'MIDI: Unavailable';
        midiStatusEl.className = 'badge bg-danger';
        console.error('WebMIDI init failed:', err);
    }
}

midiOutputSel.addEventListener('change', () => {
    midiOutput = WebMidi.getOutputById(midiOutputSel.value) ?? null;
});

// ─── BLE connect / disconnect ────────────────────────────────────────────────

connectBtn.addEventListener('click', async () => {
    if (isConnected) {
        await disconnectFromBLE();
        isConnected = false;
        setNtfHandler(() => {});
        setBtnHandler(() => {});
        bleStatusEl.textContent = 'BLE: Disconnected';
        bleStatusEl.className = 'badge bg-secondary';
        connectBtn.textContent = 'Connect Ring';
        connectBtn.className = 'btn btn-primary';
        btnRow.style.display = 'none';
    } else {
        connectBtn.disabled = true;
        connectBtn.textContent = 'Connecting…';
        try {
            await connectToBLE();
            isConnected = true;
            bleStatusEl.textContent = 'BLE: Connected';
            bleStatusEl.className = 'badge bg-success';
            connectBtn.textContent = 'Disconnect';
            connectBtn.className = 'btn btn-danger';
            btnRow.style.removeProperty('display');

            setNtfHandler((accel) => {
                chart.plot(accel);

                const now = Date.now();
                if (now - lastSendTime < SEND_INTERVAL_MS) return;
                lastSendTime = now;

                const xCC = accelToCC(accel.x);
                const yCC = accelToCC(accel.y);
                const zCC = accelToCC(accel.z);

                const xCtrl = clampCC(parseInt(xCCInput.value) || 0);
                const yCtrl = clampCC(parseInt(yCCInput.value) || 0);
                const zCtrl = clampCC(parseInt(zCCInput.value) || 0);
                const ch    = clampChannel(parseInt(midiChInput.value));

                sendCC(xCtrl, xCC, ch);
                sendCC(yCtrl, yCC, ch);
                sendCC(zCtrl, zCC, ch);

                updateMeter(xBarEl, xValEl, xCC);
                updateMeter(yBarEl, yValEl, yCC);
                updateMeter(zBarEl, zValEl, zCC);
            });

            setBtnHandler((pressed) => {
                if (btnIndicator) {
                    btnIndicator.style.backgroundColor = pressed ? '#198754' : '#dc3545';
                }
            });
        } catch (err) {
            console.error('BLE connection failed:', err);
            bleStatusEl.textContent = 'BLE: Failed';
            bleStatusEl.className = 'badge bg-danger';
            connectBtn.textContent = 'Connect Ring';
            connectBtn.className = 'btn btn-primary';
        } finally {
            connectBtn.disabled = false;
        }
    }
});

// ─── Chart toggle ─────────────────────────────────────────────────────────────

toggleChartBtn.addEventListener('click', () => {
    const visible = chartWrapper.style.display !== 'none';
    chartWrapper.style.display = visible ? 'none' : 'block';
    toggleChartBtn.textContent = visible ? 'Show Chart' : 'Hide Chart';
});

// ─── Boot ─────────────────────────────────────────────────────────────────────

initMidi();
