let device: BluetoothDevice;
let server: BluetoothRemoteGATTServer;
let service: BluetoothRemoteGATTService;
let characteristic: BluetoothRemoteGATTCharacteristic;
let accelCharacteristic: BluetoothRemoteGATTCharacteristic;
let btnCharacteristic: BluetoothRemoteGATTCharacteristic;

export type AccelNtfHandler = (accelData: { x: number; y: number; z: number; timestamp: string }) => void;

let accelNtfHandler: AccelNtfHandler = () => {};

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

        const sens = 0; // value.getInt8(6, true);

        // Parse accelerometer data
        let x, y, z;
        x = coordValToG(value.getInt16(0, false) >> 4, sens);
        y = coordValToG(value.getInt16(2, false) >> 4, sens);
        z = coordValToG(value.getInt16(4, false) >> 4, sens);

        const reading = { x, y, z };

        // Process the single reading through the stateful filter
        // const processed = separateGravityAcceleration(reading, 0.8);

        const timestamp = new Date().toLocaleTimeString();

        if (accelNtfHandler) accelNtfHandler({ ...reading, timestamp });
    } catch (error) {
        console.error('Error processing accelerometer notification:', error);
    }
}

function handleButtonNotification(event: any) {
    const val = event.target.value;
    console.log(val.getInt8(0));
}

export async function connectToBLE() {
    // try {
    if (!navigator.bluetooth) {
        console.error('Web Bluetooth API is not supported in this browser');
        alert('Web Bluetooth API is not supported in this browser');
        return;
    }

    console.log('Requesting Bluetooth devices...');
    device = await navigator.bluetooth.requestDevice({
        // acceptAllDevices: true,
        filters: [{ name: 'TEST' }],
        optionalServices: [serviceUuid],
    });

    console.log('Selected device:', device.name || 'Unknown');
    console.log('Device ID:', device.id);

    if (!device.gatt) throw new Error('No GATT Server');

    server = await device.gatt?.connect();
    console.log('Connected to GATT server');

    service = await server?.getPrimaryService(serviceUuid);
    console.log('Service found:', service?.uuid);

    characteristic = await service?.getCharacteristic(characteristicUuid);
    console.log('Characteristic found:', characteristic?.uuid);

    // Try to get accelerometer characteristic
    try {
        accelCharacteristic = await service?.getCharacteristic(accelCharacteristicUuid);
        console.log('Accelerometer characteristic found:', accelCharacteristic?.uuid);
    } catch (error) {
        console.warn('Accelerometer characteristic not found:', error);
        // accelCharacteristic = null;
    }

    // Subscribe to notifications
    await accelCharacteristic.startNotifications();
    console.log('Started accelerometer notifications');

    // Add event listener for notifications
    accelCharacteristic.addEventListener('characteristicvaluechanged', handleAccelNotification);

    // try to get btn characteristic
    btnCharacteristic = await service?.getCharacteristic(btnCharacteristicUuid);
    console.log('Button characteristic found: ', btnCharacteristic?.uuid);

    await btnCharacteristic.startNotifications();

    btnCharacteristic.addEventListener('characteristicvaluechanged', handleButtonNotification);

    device.addEventListener('gattserverdisconnected', onDisconnected);
    // } catch (error) {
    // }
}

export async function disconnectFromBLE() {
    if (!device.gatt) return;
    if (device.gatt.connected) {
        device.gatt.disconnect();
    } else {
        onDisconnected();
    }
}

function onDisconnected() {
    console.log('Disconnected from device');

    // Stop notifications if active
    if (accelCharacteristic) {
        try {
            accelCharacteristic.removeEventListener('characteristicvaluechanged', handleAccelNotification);
        } catch (error) {
            console.warn('Error removing notification listener:', error);
        }
    }
}

export function setNtfHandler(handler: AccelNtfHandler) {
    accelNtfHandler = handler;
}
