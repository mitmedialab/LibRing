const noble = require('@abandonware/noble');
const fs = require('fs');

// Catch any unhandled errors (like C++ binding disconnects) to force a crash
process.on('uncaughtException', (err) => {
    console.error('Uncaught exception:', err);
    process.exit(1);
});
process.on('unhandledRejection', (reason, promise) => {
    console.error('Unhandled Rejection at:', promise, 'reason:', reason);
    process.exit(1);
});

// Noble uses UUIDs without hyphens, in lowercase
const SVC_UUID = '00112233445566778899aabbccddeeff';
const BATT_UUID = 'e15175c380060d8a704808c0353e678a';
const CTRL_UUID = '2d86686a53dc25b30c4af0e10c8dee20'; // {0x20, 0xEE, 0x8D, 0x0C, 0xE1, 0xF0, 0x4A, 0x0C, 0xB3, 0x25, 0xDC, 0x53, 0x6A, 0x68, 0x86, 0x2D}
const TARGET_NAME = 'TEST';

console.log('Please wait, initializing Bluetooth...');

noble.on('stateChange', async (state) => {
    if (state === 'poweredOn') {
        console.log('Bluetooth is powered on. Starting scanning...');
        // Scan for all devices, allowDuplicates = false
        await noble.startScanningAsync([], false);
    } else {
        console.log(`Bluetooth is not powered on (state: ${state}). Stopping scan.`);
        await noble.stopScanningAsync();
    }
});

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

noble.on('discover', async (peripheral) => {
    const name = peripheral.advertisement.localName;
    console.log(name);

    if (name === TARGET_NAME) {
        console.log(`Found device with name '${TARGET_NAME}' (${peripheral.address}), connecting...`);
        await noble.stopScanningAsync();

        try {
            // Set a timeout to prevent connection hangs
            const connectPromise = peripheral.connectAsync();
            const connectTimeout = new Promise((_, reject) =>
                setTimeout(() => reject(new Error('Connection timed out')), 10000),
            );
            await Promise.race([connectPromise, connectTimeout]);

            console.log('Connected! Discovering services and characteristics...');

            // Listen for unexpected disconnects from the device
            peripheral.once('disconnect', () => {
                console.error('Device disconnected unexpectedly. Exiting program.');
                process.exit(1);
            });

            // Set a timeout to prevent native library hangs during discovery
            const discoveryPromise = peripheral.discoverSomeServicesAndCharacteristicsAsync(
                [SVC_UUID],
                [BATT_UUID, CTRL_UUID],
            );

            const timeoutPromise = new Promise((_, reject) =>
                setTimeout(() => reject(new Error('Service discovery timed out (usually a native BLE hang)')), 10000),
            );

            const { services, characteristics } = await Promise.race([discoveryPromise, timeoutPromise]);

            // const { svc, char_ctrl } = await peripheral.discoverSomeServicesAndCharacteristicsAsync(
            //     [SVC_UUID],
            //     [CTRL_UUID],
            // );

            const char_batt = characteristics.find((e) => e.uuid === BATT_UUID);
            const char_ctrl = characteristics.find((e) => e.uuid === CTRL_UUID);

            // console.log(char_batt);
            // console.log(char_ctrl);
            if (char_ctrl) {
                await char_ctrl.writeAsync(new Uint8Array([0]), false);
            }

            console.log('Waiting for characteristic transmit');
            await wait(2000);

            if (char_batt) {
                const characteristic = char_batt;
                console.log('Reading characteristic value continuously...');

                const logFile = 'battery_log_0.txt';
                fs.appendFileSync(logFile, `\n--- Started Logging at ${new Date().toISOString()} ---\n`);

                // Continuous polling loop
                while (true) {
                    try {
                        const data = await characteristic.readAsync();
                        const level = data.readUint32BE(0);
                        const logEntry = `[${new Date().toISOString()}] Battery Level: ${level}\n`;

                        process.stdout.write(logEntry); // Pipe to console
                        fs.appendFileSync(logFile, logEntry); // Pipe to file

                        await new Promise((resolve) => setTimeout(resolve, 1000)); // Read every 1 second
                    } catch (readError) {
                        console.error(
                            'Failed to read characteristic (device might have disconnected):',
                            readError.message,
                        );
                        throw readError; // Throw the error so the outer catch block triggers the restart
                    }
                }
            } else {
                console.log('Could not find the specified characteristic.');
            }
        } catch (error) {
            console.error('Error during connection/read:', error);
        } finally {
            console.log('Disconnecting. Program will exit now.');
            try {
                await peripheral.disconnectAsync();
            } catch (e) {
                // Ignore errors during disconnect if it's already gone
            }

            process.exit(1);
        }
    }
});
