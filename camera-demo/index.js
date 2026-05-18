const video = document.getElementById('videoElement');
const photo = document.getElementById('photoElement');
const canvas = document.getElementById('canvasElement');
const connectBtn = document.getElementById('connectBtn');

let isConnected = false;

// Setup the camera video feed
async function setupCamera() {
    try {
        const stream = await navigator.mediaDevices.getUserMedia({
            video: {
                facingMode: 'environment',
                width: { ideal: 1920 },
                height: { ideal: 1080 },
            },
        });
        video.srcObject = stream;
    } catch (err) {
        console.error('Error accessing camera:', err);
        alert('Could not access camera. Please grant permissions.');
    }
}

// Take a picture and show it in the bottom container
function takePicture() {
    if (!video.videoWidth || !video.videoHeight) return;

    canvas.width = video.videoWidth;
    canvas.height = video.videoHeight;

    const ctx = canvas.getContext('2d');
    ctx.drawImage(video, 0, 0, canvas.width, canvas.height);

    const dataURL = canvas.toDataURL('image/png');
    photo.src = dataURL;
    photo.style.display = 'block';
}

// Handle BLE Connect button click
connectBtn.addEventListener('click', async () => {
    try {
        await BLE.connectToBLE();
        isConnected = true;
        connectBtn.style.display = 'none';
    } catch (error) {
        console.error('BLE connection failed', error);
    }
});

// Assuming btn characteristic emits '1' for pressed
BLE.onButton((pressed) => {
    if (pressed === 1) {
        // Take picture on button down
        console.log('Ring button pressed, taking picture!');
        takePicture();
    }
});

// Initialize camera feed on load
setupCamera();
