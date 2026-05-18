// Web Bluetooth integration for Perlin visualization
// Exposes a minimal API similar to the old serial input path:
// - connectToBLE()
// - disconnectFromBLE()
// - onAccel(handler)
// - onButton(handler)
//
// NOTE: Web Bluetooth requires a secure context (HTTPS) or localhost.

(function (global) {
  'use strict';

  /** @type {BluetoothDevice | null} */
  let device = null;
  /** @type {BluetoothRemoteGATTServer | null} */
  let server = null;
  /** @type {BluetoothRemoteGATTService | null} */
  let service = null;

  /** @type {BluetoothRemoteGATTCharacteristic | null} */
  let accelCharacteristic = null;
  /** @type {BluetoothRemoteGATTCharacteristic | null} */
  let btnCharacteristic = null;

  const serviceUuid = '00112233-4455-6677-8899-aabbccddeeff';
  const accelCharacteristicUuid = 'f2909165-4ce5-8da2-4c10-8b38c19f65cc';
  const btnCharacteristicUuid = '368d553d-154a-1aa8-3f46-2e87b5e5cd54';

  /** @type {(data: {x:number,y:number,z:number,timestamp:string}) => void} */
  let accelHandler = function () {};
  /** @type {(value: number) => void} */
  let buttonHandler = function () {};

  function coordValToG(coord, sens) {
    let sensMultiplier = 0;
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
    return sensMultiplier * 0.001 * coord;
  }

  function handleAccelNotification(event) {
    try {
      const value = event.target.value;
      const sens = value.getInt8(6, true);

      const x = coordValToG(value.getInt16(0, false) >> 4, sens);
      const y = coordValToG(value.getInt16(2, false) >> 4, sens);
      const z = coordValToG(value.getInt16(4, false) >> 4, sens);

      const timestamp = new Date().toLocaleTimeString();
      accelHandler({ x, y, z, timestamp });
    } catch (error) {
      // Keep errors from breaking the notification pipeline
      console.error('Error processing accelerometer notification:', error);
    }
  }

  function handleButtonNotification(event) {
    try {
      const val = event.target.value;
      const pressed = val.getInt8(0);
      buttonHandler(pressed);
    } catch (error) {
      console.error('Error processing button notification:', error);
    }
  }

  function onDisconnected() {
    console.log('Disconnected from device');

    if (accelCharacteristic) {
      try {
        accelCharacteristic.removeEventListener('characteristicvaluechanged', handleAccelNotification);
      } catch (_) {}
    }
    if (btnCharacteristic) {
      try {
        btnCharacteristic.removeEventListener('characteristicvaluechanged', handleButtonNotification);
      } catch (_) {}
    }

    accelCharacteristic = null;
    btnCharacteristic = null;
    service = null;
    server = null;
  }

  async function connectToBLE() {
    try {
      if (!navigator.bluetooth) {
        alert('Web Bluetooth API is not supported in this browser');
        return;
      }

      device = await navigator.bluetooth.requestDevice({
        filters: [{ name: 'TEST' }],
        optionalServices: [serviceUuid]
      });

      if (!device.gatt) throw new Error('No GATT Server');
      server = await device.gatt.connect();

      service = await server.getPrimaryService(serviceUuid);

      accelCharacteristic = await service.getCharacteristic(accelCharacteristicUuid);
      await accelCharacteristic.startNotifications();
      accelCharacteristic.addEventListener('characteristicvaluechanged', handleAccelNotification);

      btnCharacteristic = await service.getCharacteristic(btnCharacteristicUuid);
      await btnCharacteristic.startNotifications();
      btnCharacteristic.addEventListener('characteristicvaluechanged', handleButtonNotification);

      device.addEventListener('gattserverdisconnected', onDisconnected);

      console.log('BLE connected:', device.name || device.id);
    } catch (error) {
      console.error('Error connecting to device:', error);
      throw error;
    }
  }

  async function disconnectFromBLE() {
    if (device && device.gatt && device.gatt.connected) {
      device.gatt.disconnect();
    } else {
      onDisconnected();
    }
  }

  function onAccel(handler) {
    accelHandler = typeof handler === 'function' ? handler : function () {};
  }

  function onButton(handler) {
    buttonHandler = typeof handler === 'function' ? handler : function () {};
  }

  global.BLE = {
    connectToBLE,
    disconnectFromBLE,
    onAccel,
    onButton
  };
})(window);
