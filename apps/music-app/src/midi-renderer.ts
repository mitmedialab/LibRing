import '../../shared/webble-notice.js';
import { WebMidi, Output } from 'webmidi';
import { connectToBLE, disconnectFromBLE, setNtfHandler, setBtnHandler, setDisconnectHandler } from './lib/shared/accelerometer/ble';
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
const bleMessageEl   = document.getElementById('ble-message')      as HTMLElement;
const btnModeSel     = document.getElementById('btn-mode')         as HTMLSelectElement;
const btnNumberInput = document.getElementById('btn-number')       as HTMLInputElement;

const xValEl  = document.getElementById('x-cc-val') as HTMLElement;
const yValEl  = document.getElementById('y-cc-val') as HTMLElement;
const zValEl  = document.getElementById('z-cc-val') as HTMLElement;
const xBarEl  = document.getElementById('x-bar')    as HTMLElement;
const yBarEl  = document.getElementById('y-bar')    as HTMLElement;
const zBarEl  = document.getElementById('z-bar')    as HTMLElement;
const btnValEl = document.getElementById('btn-val') as HTMLElement;
const btnBarEl = document.getElementById('btn-bar') as HTMLElement;

// ─── State ───────────────────────────────────────────────────────────────────

let isConnected  = false;
let midiOutput: Output | null = null;
let lastSendTime = 0;
const SEND_INTERVAL_MS = 20; // 50 Hz cap

const chart = new AccelChart('accel-chart');

// Button touch: what the press sent, so that the release turns it off even if the
// settings changed in between (null: not pressed)
let touchSent: { mode: 'note' | 'cc'; number: number; channel: number } | null = null;

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

// Button touch -> MIDI: note-on (velocity 127) / note-off, or CC 127 / 0
function handleTouch(pressed: boolean) {
    if (btnIndicator) btnIndicator.style.backgroundColor = pressed ? '#198754' : '#dc3545';
    if (pressed && !touchSent) {
        const mode = btnModeSel.value === 'cc' ? 'cc' : 'note';
        const number = clampCC(parseInt(btnNumberInput.value) || 0);
        const channel = clampChannel(parseInt(midiChInput.value));
        touchSent = { mode, number, channel };
        try {
            if (mode === 'note') midiOutput?.sendNoteOn(number, { channels: channel + 1, rawAttack: 127 });
            else sendCC(number, 127, channel);
        } catch (_) { /* ignore */ }
        btnValEl.textContent = mode === 'note' ? `note ${number}` : `CC ${number}: 127`;
        btnValEl.classList.remove('text-secondary');
        btnBarEl.style.width = '100%';
    } else if (!pressed && touchSent) {
        const { mode, number, channel } = touchSent;
        touchSent = null;
        try {
            if (mode === 'note') midiOutput?.sendNoteOff(number, { channels: channel + 1 });
            else sendCC(number, 0, channel);
        } catch (_) { /* ignore */ }
        btnValEl.textContent = mode === 'note' ? 'off' : `CC ${number}: 0`;
        btnBarEl.style.width = '0%';
    }
}

// Default number when switching between note (middle C) and CC (64)
btnModeSel.addEventListener('change', () => {
    btnNumberInput.value = btnModeSel.value === 'cc' ? '64' : '60';
});

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

// Back to the disconnected state (user disconnection or the ring dropping out)
function showDisconnected(status: string, badgeClass: string) {
    handleTouch(false); // release a held note / CC
    isConnected = false;
    setNtfHandler(() => {});
    setBtnHandler(() => {});
    bleStatusEl.textContent = status;
    bleStatusEl.className = badgeClass;
    connectBtn.textContent = 'Connect Ring';
    connectBtn.className = 'btn btn-primary';
    btnRow.style.display = 'none';
}

setDisconnectHandler(() => {
    showDisconnected('BLE: Connection lost', 'badge bg-warning text-dark');
    bleMessageEl.textContent = 'The ring disconnected (out of range or battery?): click Connect Ring to reconnect.';
});

connectBtn.addEventListener('click', async () => {
    if (isConnected) {
        await disconnectFromBLE();
        showDisconnected('BLE: Disconnected', 'badge bg-secondary');
    } else {
        connectBtn.disabled = true;
        connectBtn.textContent = 'Connecting…';
        bleMessageEl.textContent = '';
        try {
            await connectToBLE((message) => {
                connectBtn.textContent = message;
                bleStatusEl.textContent = `BLE: ${message}`;
                bleStatusEl.className = 'badge bg-info text-dark';
            });
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

            setBtnHandler((pressed) => handleTouch(!!pressed));
        } catch (err) {
            console.error('BLE connection failed:', err);
            // NotFoundError: the user closed the ring chooser
            const cancelled = err instanceof DOMException && err.name === 'NotFoundError';
            bleMessageEl.textContent = cancelled ? '' : (err instanceof Error ? err.message : String(err));
            bleStatusEl.textContent = cancelled ? 'BLE: Disconnected' : 'BLE: Failed';
            bleStatusEl.className = cancelled ? 'badge bg-secondary' : 'badge bg-danger';
            connectBtn.textContent = 'Connect Ring';
            connectBtn.className = 'btn btn-primary';
        } finally {
            connectBtn.disabled = false;
        }
    }
});

// ─── Graph toggle ─────────────────────────────────────────────────────────────

toggleChartBtn.addEventListener('click', () => {
    const visible = chartWrapper.style.display !== 'none';
    chartWrapper.style.display = visible ? 'none' : 'block';
    toggleChartBtn.textContent = visible ? 'Show Graph' : 'Hide Graph';
});

// ─── Boot ─────────────────────────────────────────────────────────────────────

initMidi();
