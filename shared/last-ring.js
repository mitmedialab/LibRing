// Automatic connection to the last ring, shared by the web apps (plain script: include it
// with <script src="../shared/last-ring.js"></script>, or import it for its side effect in
// the Vite apps; it defines globalThis.libringLastRing). The multi-ring app has its own
// version, for all the rings.
//
// The browser keeps the rings a site was allowed to use (navigator.bluetooth.getDevices()):
// across page reloads, and across Chrome restarts only with
// chrome://flags/#enable-web-bluetooth-new-permissions-backend. Without them (or another
// browser), nothing happens: the app works as before, with its Connect button.
(function () {
    const KEY = 'libring-last-ring';
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

    // After a successful connection: the ring to connect to next time
    function remember(device) {
        try { localStorage.setItem(KEY, device.id); } catch (e) { /* private mode */ }
    }

    // The last ring, if the browser still allows this site to use it, else null
    async function find() {
        if (!navigator.bluetooth || !navigator.bluetooth.getDevices) return null;
        let id = null;
        try { id = localStorage.getItem(KEY); } catch (e) { /* private mode */ }
        if (!id) return null;
        const devices = await navigator.bluetooth.getDevices();
        return devices.find((d) => d.id === id) || null;
    }

    // device.gatt.connect(), for a ring the page didn't just pick in the chooser. Linux:
    // BlueZ forgets the rings ~30 s after its last scan, then Chrome refuses at once ("no
    // longer in range"); watching the ring's advertisements for 3 s makes BlueZ scan again.
    async function gattConnect(device) {
        try {
            return await device.gatt.connect();
        } catch (e) {
            if (!/no longer in range/i.test(e.message) || !device.watchAdvertisements) throw e;
            try { await device.watchAdvertisements(); } catch (e2) { /* not supported */ }
            await sleep(3000);
            try { device.unwatchAdvertisements(); } catch (e2) { /* not watching */ }
            return device.gatt.connect();
        }
    }

    // On page load: start(device) with the last ring, 2 s after the load (right after a
    // reload, Chrome may still be closing the previous page's link to the ring, and a ring
    // connected then gets no notifications)
    function autoConnect(start) {
        setTimeout(async () => {
            try {
                const device = await find();
                if (device && !(device.gatt && device.gatt.connected)) {
                    console.log(`Connecting to the last ring: ${device.name}`);
                    start(device);
                }
            } catch (e) {
                console.warn('Automatic connection:', e);
            }
        }, 2000);
    }

    // Leaving or reloading the page: release the ring at once (else the next page can't
    // connect to it for a while). getDevice(): the connected device, or null.
    function releaseOnLeave(getDevice) {
        window.addEventListener('pagehide', () => {
            const device = getDevice();
            try { if (device && device.gatt.connected) device.gatt.disconnect(); } catch (e) { /* gone */ }
        });
    }

    globalThis.libringLastRing = { remember, find, gattConnect, autoConnect, releaseOnLeave };
})();
