import { connectToBLE, disconnectFromBLE, setNtfHandler } from './lib/shared/accelerometer/ble';
import { SleepStateMachine, type SleepState } from './lib/shared/SleepStateMachine';

// Initialize on page load
const body = document.body;
if (body && body.id === 'page-classify') {
    // Initialize Sleep State Machine
    const sleepStateMachine = new SleepStateMachine({
        activeToRestingThresholdMs: 2500, // 5 seconds no movement
        movementThresholdG: 0.1, // 100 mG
        restingToActiveThresholdMs: 3000, // 3 seconds substantial movement
        restingToDeepSleepThresholdMs: 30000, // 30 seconds minimal movement
        deepSleepToRestingThresholdMs: 2500, // 5 seconds substantial movement
        substantialMovementThresholdG: 0.25, // 500 mG
        lpfAlpha: 0.2, // LPF smoothing (lower = more smoothing)
        gravityAlpha: 0.95, // Gravity estimation (high pass filter)
        gravityWindowSize: 10, // Number of samples for gravity estimation
    });

    /**
     * Update the sleep state display on the UI
     */
    function updateSleepStateDisplay(state: SleepState): void {
        const stateDisplay = document.getElementById('sleep-state-display');
        const stateLabel = document.getElementById('sleep-state-label');
        const stateInfo = document.getElementById('sleep-state-info');

        if (!stateDisplay || !stateLabel || !stateInfo) return;

        // Set display text
        const displayText = state === 'deep-sleep' ? 'DEEP SLEEP' : state.toUpperCase();

        // Update background color and styling
        let backgroundColor = '#2d5f1e'; // Green for active
        let icon = '🟢';

        switch (state) {
            case 'active':
                backgroundColor = '#2d5f1e'; // Dark green
                icon = '🟢';
                break;
            case 'resting':
                backgroundColor = '#4a4a1e'; // Dark yellow/olive
                icon = '🟡';
                break;
            case 'deep-sleep':
                backgroundColor = '#1e2d5f'; // Dark blue
                icon = '🔵';
                break;
        }

        stateDisplay.style.backgroundColor = backgroundColor;
        stateLabel.textContent =
            // icon + ' ' +
            displayText;

        // Update info text
        const timeInState = sleepStateMachine.getTimeInCurrentState();
        const timeSinceMovement = sleepStateMachine.getTimeSinceLastMovement();
        stateInfo.textContent = `Time in state: ${(timeInState / 1000).toFixed(1)}s | Last movement: ${(timeSinceMovement / 1000).toFixed(1)}s ago`;
    }

    // Register state change callback
    sleepStateMachine.onStateChange((newState, oldState) => {
        console.log(`Sleep state changed: ${oldState} → ${newState}`);
        updateSleepStateDisplay(newState);
    });

    // Initialize display
    updateSleepStateDisplay(sleepStateMachine.getState());

    const connectToggleBtn = document.getElementById('classify-connect-toggle') as HTMLButtonElement | null;

    let isConnected = false;
    let isConnecting = false;

    function setButtonToConnect() {
        if (!connectToggleBtn) return;
        connectToggleBtn.innerHTML = 'Connect';
        connectToggleBtn.className = 'btn btn-primary shadow';
        connectToggleBtn.disabled = false;
    }

    function setButtonToDisconnect() {
        if (!connectToggleBtn) return;
        connectToggleBtn.innerHTML = 'Disconnect';
        connectToggleBtn.className = 'btn btn-danger shadow';
        connectToggleBtn.disabled = false;
    }

    function setButtonToConnecting() {
        if (!connectToggleBtn) return;
        isConnecting = true;
        connectToggleBtn.disabled = true;
        connectToggleBtn.innerHTML = `<span class="spinner-inline" aria-hidden="true"></span> Connecting...`;
        connectToggleBtn.className = 'btn btn-primary shadow';
    }

    function hideButton() {
        // We no longer hide the button, we turn it into a disconnect button
        setButtonToDisconnect();
    }

    setButtonToConnect();

    connectToggleBtn?.addEventListener('click', async () => {
        if (isConnected) {
            // Disconnect
            try {
                await disconnectFromBLE();
            } catch (e) {
                console.error('Error disconnecting:', e);
            }
            isConnected = false;
            isConnecting = false;
            setNtfHandler(() => {});
            sleepStateMachine.reset();
            updateSleepStateDisplay(sleepStateMachine.getState());
            setButtonToConnect();
        } else if (!isConnecting) {
            // Start connecting
            setButtonToConnecting();
            try {
                await connectToBLE();
                // on success
                isConnected = true;
                isConnecting = false;

                setNtfHandler((a) => {
                    // Process accelerometer data through sleep state machine
                    sleepStateMachine.processReading(a.x, a.y, a.z);
                    updateSleepStateDisplay(sleepStateMachine.getState());
                });

                // Hide connect button once connected
                hideButton();
            } catch (error) {
                console.error('Error connecting to device:', error);
                isConnected = false;
                isConnecting = false;
                // show failure state briefly
                if (connectToggleBtn) {
                    connectToggleBtn.innerHTML = 'Connection failed — Retry';
                    connectToggleBtn.disabled = false;
                    connectToggleBtn.className = 'btn btn-warning btn-lg';
                    setTimeout(() => setButtonToConnect(), 2000);
                }
            }
        }
    });
}
