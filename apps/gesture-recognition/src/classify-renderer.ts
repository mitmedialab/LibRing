import { connectToBLE, disconnectFromBLE, setNtfHandler, setBtnHandler, setDisconnectHandler } from './lib/shared/accelerometer/ble';
import { AccelChart } from './lib/shared/plot';
import ClassificationMLRecorder, { ClassifyHandlerData } from './lib/classify/ClassificationMLRecorder';
// import { GestureHandler } from './lib/classify/GestureHandler';

// Only initialize if classify page is present
const classifyPage = document.getElementById('page-classify');
if (classifyPage) {
    const chart = new AccelChart('classify-accel-chart');

    const recorder = new ClassificationMLRecorder();
    const handlers = recorder.getMLRecorderNtfHandler();

    // Initialize GestureHandler with registered gestures
    // const gestureHandler = new GestureHandler();

    // // Register gestures with their configurations
    // gestureHandler.registerGesture('circle-cw', {
    //     mode: 'continuous',
    //     cooldownMs: 100, // Rate limit volume changes
    //     minConfidence: 0.7,
    //     onTrigger: () => {
    //         // window.electronAPI.incrementSystemVolume(0.03);
    //         console.log('Circle CW gesture started');
    //     },
    //     onEnd: () => {
    //         console.log('Circle CW gesture ended');
    //     },
    // });

    // gestureHandler.registerGesture('circle-ccw', {
    //     mode: 'continuous',
    //     cooldownMs: 100,
    //     minConfidence: 0.7,
    //     onTrigger: () => {
    //         console.log('Circle CCW gesture started');
    //         // window.electronAPI.incrementSystemVolume(-0.03);
    //     },
    //     onEnd: () => {
    //         console.log('Circle CCW gesture ended');
    //     },
    // });

    // gestureHandler.registerGesture('double-tap', {
    //     mode: 'discrete',
    //     cooldownMs: 500, // Prevent accidental double-taps
    //     minConfidence: 0.85, // Higher threshold for discrete actions
    //     onTrigger: () => {
    //         console.log('double tap');
    //         // window.electronAPI.minimizeForegroundWindow();
    //     },
    // });

    // gestureHandler.registerGesture('vert-tap', {
    //     mode: 'discrete',
    //     cooldownMs: 500,
    //     minConfidence: 0.85,
    //     onTrigger: () => {
    //         console.log('vert tap');
    //         // window.electronAPI.maximizeForegroundWindow();
    //     },
    // });

    // Register idle to reset state when no gesture detected
    // gestureHandler.registerGesture('idle', {
    //     mode: 'discrete',
    //     cooldownMs: 0,
    //     minConfidence: 0,
    //     onTrigger: () => {
    //         gestureHandler.handleIdle();
    //     },
    // });

    const connectToggleBtn = document.getElementById('classify-connect-toggle') as HTMLButtonElement;
    const recordingToggleBtn = document.getElementById('classify-recording-toggle') as HTMLButtonElement;
    const loadModelBtn = document.getElementById('classify-load-model') as HTMLButtonElement;
    const btnIndicator = document.getElementById('btn-indicator') as HTMLDivElement;
    const bleMessage = document.getElementById('ble-message') as HTMLSpanElement;

    function showMessage(text: string) {
        if (!bleMessage) return;
        bleMessage.textContent = text;
        bleMessage.style.display = text ? 'inline-block' : 'none';
    }

    let isConnected = false;
    let isRecording = true;
    let isModelLoaded = true;

    function updateButtonStates() {
        // Update connect button
        connectToggleBtn.textContent = isConnected ? 'Disconnect' : 'Connect';
        connectToggleBtn.className = isConnected ? 'btn btn-danger' : 'btn btn-primary';

        // // Update recording button
        // recordingToggleBtn.textContent = isRecording ? 'Stop Recording' : 'Start Recording';
        // recordingToggleBtn.className = isRecording ? 'btn btn-danger' : 'btn btn-success';
        // recordingToggleBtn.disabled = !(isConnected && isModelLoaded);
    }

    updateButtonStates();

    // The ring dropped the connection by itself
    setDisconnectHandler(() => {
        isConnected = false;
        setNtfHandler(() => {});
        setBtnHandler(() => {});
        if (btnIndicator) btnIndicator.style.backgroundColor = 'red';
        updateButtonStates();
        showMessage('The ring disconnected (out of range or battery?): click Connect to reconnect.');
    });

    connectToggleBtn?.addEventListener('click', () => {
        if (connectToggleBtn.disabled) return;
        if (isConnected) {
            disconnectFromBLE().then(() => {
                isConnected = false;
                // isRecording = false;
                setNtfHandler(() => {});
                setBtnHandler(() => {});
                if (btnIndicator) btnIndicator.style.backgroundColor = 'red';
                updateButtonStates();
            });
        } else {
            connectToggleBtn.disabled = true;
            showMessage('');
            connectToBLE((message) => {
                connectToggleBtn.textContent = message;
            })
                .then(() => {
                    isConnected = true;

                    setNtfHandler((a) => {
                        chart.plot(a);
                        handlers.ntfHandler(a);
                    });

                    setBtnHandler((pressed) => {
                        if (btnIndicator) {
                            btnIndicator.style.backgroundColor = pressed ? '#008000' : 'red';
                        }
                    });
                    updateButtonStates();
                })
                .catch((error) => {
                    console.error('Error connecting to device:', error);
                    // NotFoundError: the user closed the ring chooser
                    if (!(error instanceof DOMException && error.name === 'NotFoundError')) {
                        showMessage(error instanceof Error ? error.message : String(error));
                    }
                })
                .finally(() => {
                    connectToggleBtn.disabled = false;
                    updateButtonStates();
                });
        }
    });

    // recordingToggleBtn?.addEventListener('click', () => {
    //     if (isRecording) {
    //         setNtfHandler((a) => {});
    //         isRecording = false;
    //     } else {
    //         chart.clear();
    //         isRecording = true;
    //     }
    //     updateButtonStates();
    // });

    // loadModelBtn?.addEventListener('click', () => {
    //     const input = document.createElement('input');
    //     input.type = 'file';
    //     input.multiple = true;
    //     input.onchange = (e: any) => {
    //         var files = e.target.files;
    //         recorder.loadModel(files).then(() => {
    //             isModelLoaded = true;
    //             updateButtonStates();
    //         });
    //     };
    //     input.click();
    // });

    function handleClassificationResult(highest: string) {
        // if (!results || results.length === 0) return;

        // Pass to gesture handler for validation and action triggering
        // gestureHandler.handleClassification(results, segment);
        // const highest = results[0].label;
        console.log(highest);

        // Note: System volume control not available in browser environment
        // if (highest === 'circle-cw') {
        //     window.electronAPI.incrementSystemVolume(0.03);
        // } else if (highest === 'circle-ccw') {
        //     window.electronAPI.incrementSystemVolume(-0.03);
        // }
        //  else if (highest === 'double-tap' && Date.now() - lastDoubleTap > 500) {
        //     if (doubleTapConfirm) {
        //         window.electronAPI.minimizeForegroundWindow();
        //         doubleTapConfirm = false;
        //     } else {
        //         doubleTapConfirm = true;
        //     }
        //     // window.electronAPI.setSystemVolume(100);
        //     lastDoubleTap = Date.now();
        // }

        // Display results for debugging
        displayClassificationResult(highest);
    }

    // Utility to display classification result
    function displayClassificationResult(result: any) {
        console.log(result);

        const stateDisplay = document.getElementById('sleep-state-display');
        const stateLabel = document.getElementById('sleep-state-label');

        if (!stateDisplay || !stateLabel) return;

        const label = typeof result === 'string' ? result : result && result.length ? result[0].label : 'idle';

        let displayText = 'IDLE';
        let backgroundColor = '#222222'; // Dark gray for idle

        switch (label) {
            case 'circle-cw':
                displayText = 'VOLUME UP 🔊🔊🔊';
                backgroundColor = '#2d5f1e'; // Green
                break;
            case 'circle-ccw':
                displayText = 'VOLUME DOWN 🔈';
                backgroundColor = '#5f1e1e'; // Red
                break;
            case 'idle':
            default:
                displayText = 'IDLE';
                backgroundColor = '#222222';
                break;
        }

        stateLabel.textContent = displayText;
        stateDisplay.style.backgroundColor = backgroundColor;

        // Still update the hidden pre element if it's there
        let pre = document.getElementById('classify-ml-classify-result') as HTMLPreElement;
        if (!pre) {
            pre = document.createElement('pre');
            pre.id = 'classify-ml-classify-result';
            pre.style.display = 'none';
            document.body.appendChild(pre);
        }

        pre.innerHTML = `<div class="alert alert-success py-1 mb-2">Current Gesture: <strong>${displayText}</strong></div>`;
    }

    recorder.setClassifyHandler((d) => handleClassificationResult(d));
}
