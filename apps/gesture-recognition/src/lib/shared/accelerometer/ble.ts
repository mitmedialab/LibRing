import '../../../../../shared/last-ring.js'; // globalThis.libringLastRing (automatic connection)

// apps/shared/last-ring.js
const lastRing = (globalThis as any).libringLastRing as {
    remember(device: BluetoothDevice): void;
    gattConnect(device: BluetoothDevice): Promise<BluetoothRemoteGATTServer>;
    autoConnect(start: (device: BluetoothDevice) => void): void;
    releaseOnLeave(getDevice: () => BluetoothDevice | null): void;
};

let device: BluetoothDevice;
let pendingDevice: BluetoothDevice | null = null; // ring for the next connectToBLE() (automatic)
let server: BluetoothRemoteGATTServer;
let service: BluetoothRemoteGATTService;
let characteristic: BluetoothRemoteGATTCharacteristic;
let accelCharacteristic: BluetoothRemoteGATTCharacteristic;
let btnCharacteristic: BluetoothRemoteGATTCharacteristic;

export type AccelNtfHandler = (accelData: { x: number; y: number; z: number; timestamp: string }) => void;
export type BtnNtfHandler = (pressed: number) => void;

let accelNtfHandler: AccelNtfHandler = () => {};
let btnNtfHandler: BtnNtfHandler = () => {};

const serviceUuid = '00112233-4455-6677-8899-aabbccddeeff';
const characteristicUuid = '2d86686a-53dc-25b3-0c4a-f0e10c8dee20';
const accelCharacteristicUuid = 'f2909165-4ce5-8da2-4c10-8b38c19f65cc'; // Reversed byte order for BLE
const btnCharacteristicUuid = '368d553d-154a-1aa8-3f46-2e87b5e5cd54';

const coordValToG = (coord: number, sens: number) => {
    let sensMultiplier = 0;
    // find sens multiplier in milli-Gs
    switch (sens) {
        case 0:
            sensMultiplier = 1;
            break;
        case 1:
            sensMultiplier = 2;
            break;
        case 2:
            sensMultiplier = 4;
            break;
        case 3:
        default:
            sensMultiplier = 8;
            break;
    }
    // convert to G's and apply multiplier
    return sensMultiplier * 0.001 * coord;
};

function handleAccelNotification(event: any) {
    try {
        const value = event.target.value;

        // Sensitivity sent by the ring (byte 6, 3 = +-16 g). It used to be hard-coded to 0
        // (+-2 g) because the firmware never set the range and many rings stayed at the
        // sensor's +-2 g default; the firmware now sets +-16 g and reports it.
        const sens = value.byteLength > 6 ? value.getUint8(6) : 0;

        // Parse accelerometer data
        let x, y, z;
        x = coordValToG(value.getInt16(0, false) >> 4, sens);
        y = coordValToG(value.getInt16(2, false) >> 4, sens);
        z = coordValToG(value.getInt16(4, false) >> 4, sens);

        const reading = { x, y, z };

        // Process the single reading through the stateful filter
        // const processed = separateGravityAcceleration(reading, 0.8);

        const timestamp = new Date().toLocaleTimeString();

        lastAccelTime = Date.now();
        if (accelNtfHandler) accelNtfHandler({ ...reading, timestamp });
    } catch (error) {
        console.error('Error processing accelerometer notification:', error);
    }
}

function handleButtonNotification(event: any) {
    const val = event.target.value;
    const pressed = val.getInt8(0);
    if (btnNtfHandler) btnNtfHandler(pressed);
}

export type StatusHandler = (message: string) => void;
export type DisconnectHandler = () => void;

let disconnectHandler: DisconnectHandler = () => {};
let userDisconnect = false;
let lastAccelTime = 0;

const ATTEMPTS = 3;
const CONNECT_TIMEOUT_MS = 10000; // a connection that isn't up by then rarely comes up
const DATA_TIMEOUT_MS = 4000; // the ring streams ~11 samples/s as soon as subscribed
const NOTIFY_WAIT_MS = 1500; // no notification by then: poll instead (see startPolling)
let polling = false;

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

function withTimeout<T>(promise: Promise<T>, ms: number, what: string): Promise<T> {
    return new Promise<T>((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error(`${what}: no answer after ${ms / 1000} s`)), ms);
        promise.then(
            (v) => { clearTimeout(timer); resolve(v); },
            (e) => { clearTimeout(timer); reject(e); },
        );
    });
}

// Chrome on Linux (BlueZ) only delivers notifications in the first connection after the
// page loads: after a disconnection, the ring sends them and BlueZ receives them, but the
// page never gets the events (until it is reloaded). Reading the values still works, so
// then the accelerometer is read in a loop (~11 reads/s, the ring's rate) and the button
// every 4th read.
async function startPolling() {
    polling = true;
    console.log('No notifications (Chrome reconnection issue): reading the values instead');
    let lastButton: number | null = null;
    for (let i = 0; polling && device?.gatt?.connected; i++) {
        try {
            const value = await accelCharacteristic.readValue();
            handleAccelNotification({ target: { value } });
            if (i % 4 === 0) {
                const b = await btnCharacteristic.readValue();
                const pressed = b.getInt8(0);
                if (lastButton !== null && pressed !== lastButton) handleButtonNotification({ target: { value: b } });
                lastButton = pressed;
            }
        } catch (error) {
            if (!device?.gatt?.connected) break;
            console.warn('Read failed:', error);
            await sleep(200);
        }
    }
    polling = false;
}

// One connection attempt: GATT connection, characteristics, notifications, first data
async function connectOnce() {
    server = await withTimeout(lastRing.gattConnect(device), CONNECT_TIMEOUT_MS, 'Connection');
    console.log('Connected to GATT server');
    service = await server.getPrimaryService(serviceUuid);
    characteristic = await service.getCharacteristic(characteristicUuid);
    accelCharacteristic = await service.getCharacteristic(accelCharacteristicUuid);
    btnCharacteristic = await service.getCharacteristic(btnCharacteristicUuid);

    accelCharacteristic.removeEventListener('characteristicvaluechanged', handleAccelNotification);
    accelCharacteristic.addEventListener('characteristicvaluechanged', handleAccelNotification);
    btnCharacteristic.removeEventListener('characteristicvaluechanged', handleButtonNotification);
    btnCharacteristic.addEventListener('characteristicvaluechanged', handleButtonNotification);

    lastAccelTime = 0;
    polling = false;
    const subscribed = Date.now();
    await accelCharacteristic.startNotifications();
    await btnCharacteristic.startNotifications();
    console.log('Started accelerometer and button notifications');

    while (!lastAccelTime) {
        const waited = Date.now() - subscribed;
        if (waited > DATA_TIMEOUT_MS) throw new Error('Connected, but no accelerometer data');
        if (waited > NOTIFY_WAIT_MS && !polling) startPolling(); // runs until disconnection
        await sleep(100);
    }
    console.log(`First accelerometer data ${lastAccelTime - subscribed} ms after subscribing`);
}

/**
 * Choose a ring (browser dialog) and connect, retrying up to ATTEMPTS times when
 * the connection doesn't come up or no data arrives. onStatus gets progress
 * messages for the page. Throws (with a message for the user) if all attempts fail.
 */
export async function connectToBLE(onStatus: StatusHandler = () => {}) {
    if (!navigator.bluetooth) {
        throw new Error('Web Bluetooth is not available in this browser: use Chrome or Edge (iPhone / iPad: the Bluefy app)');
    }

    if (pendingDevice) { // the last ring (autoConnect()): no chooser
        device = pendingDevice;
        pendingDevice = null;
    } else {
        console.log('Requesting Bluetooth devices...');
        device = await navigator.bluetooth.requestDevice({
            // acceptAllDevices: true,
            filters: [{ namePrefix: 'LibRing' }, { name: 'TEST' }], // LibRing_XX (XX: ring ID), LibRing, TEST: older firmware
            optionalServices: [serviceUuid],
        });
    }
    console.log('Selected device:', device.name || 'Unknown');
    if (!device.gatt) throw new Error('No GATT Server');
    device.removeEventListener('gattserverdisconnected', onDisconnected);
    device.addEventListener('gattserverdisconnected', onDisconnected);

    let lastError: unknown = null;
    for (let attempt = 1; attempt <= ATTEMPTS; attempt++) {
        onStatus(attempt === 1 ? 'Connecting…' : `Connecting… (attempt ${attempt}/${ATTEMPTS})`);
        const t0 = Date.now();
        try {
            userDisconnect = true; // a failed attempt's disconnection isn't a ring drop
            await connectOnce();
            userDisconnect = false;
            console.log(`Connected in ${((Date.now() - t0) / 1000).toFixed(1)} s (attempt ${attempt})`);
            onStatus('Connected');
            lastRing.remember(device);
            return;
        } catch (error) {
            lastError = error;
            console.warn(`Connection attempt ${attempt}/${ATTEMPTS} failed after ${((Date.now() - t0) / 1000).toFixed(1)} s:`, error);
            device.gatt.disconnect(); // also aborts a connection still pending
            await sleep(500);
        }
    }
    userDisconnect = false;
    const reason = lastError instanceof Error ? lastError.message : String(lastError);
    throw new Error(
        `Could not connect to ${device.name || 'the ring'} (${reason}). ` +
            'Check that it is charged, close by, and not connected to another app or tab, then try again.',
    );
}

/**
 * On page load, connect to the last ring if the browser still allows it, by clicking the
 * app's Connect button (its usual connection path, without the chooser).
 */
export function autoConnect(connectButton: HTMLElement | null) {
    if (!connectButton || !navigator.bluetooth) return;
    lastRing.autoConnect((d) => {
        pendingDevice = d;
        connectButton.click();
    });
}

// Leaving or reloading the page: release the ring at once
lastRing.releaseOnLeave(() => {
    if (device?.gatt?.connected) userDisconnect = true; // not a ring drop
    return device ?? null;
});

export async function disconnectFromBLE() {
    if (!device?.gatt) return;
    userDisconnect = true;
    if (device.gatt.connected) {
        device.gatt.disconnect();
    } else {
        onDisconnected();
    }
}

function onDisconnected() {
    console.log('Disconnected from device');
    polling = false;
    if (accelCharacteristic) {
        try {
            accelCharacteristic.removeEventListener('characteristicvaluechanged', handleAccelNotification);
        } catch (error) {
            console.warn('Error removing notification listener:', error);
        }
    }
    // the ring dropped the connection (not the user, not a failed attempt): tell the page
    if (!userDisconnect) disconnectHandler();
    userDisconnect = false;
}

/** Called when the ring drops the connection by itself (out of range, battery, ...) */
export function setDisconnectHandler(handler: DisconnectHandler) {
    disconnectHandler = handler;
}

export function setNtfHandler(handler: AccelNtfHandler) {
    accelNtfHandler = handler;
}

export function setBtnHandler(handler: BtnNtfHandler) {
    btnNtfHandler = handler;
}
