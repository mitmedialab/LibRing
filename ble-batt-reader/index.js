const noble = require('@abandonware/noble');
const fs = require('fs');

// Noble uses UUIDs without hyphens, in lowercase
const SVC_UUID = '00112233445566778899aabbccddeeff';
const BATT_UUID = 'e15175c380060d8a704808c0353e678a';
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

noble.on('discover', async (peripheral) => {
    const name = peripheral.advertisement.localName;
    console.log(name);

    if (name === TARGET_NAME) {
        console.log(`Found device with name '${TARGET_NAME}' (${peripheral.address}), connecting...`);
        await noble.stopScanningAsync();

        try {
            await peripheral.connectAsync();
            console.log('Connected! Discovering services and characteristics...');

            const { services, characteristics } = await peripheral.discoverSomeServicesAndCharacteristicsAsync(
                [SVC_UUID],
                [BATT_UUID],
            );

            if (characteristics.length > 0) {
                const characteristic = characteristics[0];
                console.log('Reading characteristic value continuously...');

                const logFile = 'battery_log.txt';
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
                        break; // Exit the while loop to trigger cleanup
                    }
                }
            } else {
                console.log('Could not find the specified characteristic.');
            }
        } catch (error) {
            console.error('Error during connection/read:', error);
        } finally {
            console.log('Disconnecting and cleaning up...');
            await peripheral.disconnectAsync();
            process.exit(0);
        }
    }
});
