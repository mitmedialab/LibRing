import { AccelDataPoint } from '../plot';
import { augmentByZRotation, augmentByNoise } from './datagen';
import { separateGravityAndLinearAccel } from './filters';
import { PCA, PCAResult } from '../math/pca';
import { Point3D } from '../math/types';

/**
 * Resample a list of AccelDataPoint's to n evenly spaced points (linear interpolation).
 * Returns a new array of length n.
 */
export function resampleAccelData(data: AccelDataPoint[], n: number): AccelDataPoint[] {
    if (data.length === 0 || n <= 0) return [];
    if (data.length === 1) return Array(n).fill(data[0]);

    const result: AccelDataPoint[] = [];
    const lastIdx = data.length - 1;
    for (let i = 0; i < n; i++) {
        // Find fractional index in original data
        const t = (i * lastIdx) / (n - 1);
        const idx = Math.floor(t);
        const frac = t - idx;
        const a = data[idx];
        const b = data[Math.min(idx + 1, lastIdx)];
        // Linear interpolation for x, y, z
        result.push({
            x: a.x + (b.x - a.x) * frac,
            y: a.y + (b.y - a.y) * frac,
            z: a.z + (b.z - a.z) * frac,
            timestamp: a.timestamp,
        });
    }
    return result;
}

function normalizeChannel(data: number[]): number[] {
    const mean = data.reduce((a, b) => a + b, 0) / data.length;
    const variance = data.reduce((a, b) => a + Math.pow(b - mean, 2), 0) / data.length;
    const std = Math.sqrt(variance);
    const divisor = std < 1e-6 ? 1 : std; // Avoid division by zero

    // Normalize and clip outliers
    return data.map((v) => {
        let norm = (v - mean) / divisor;
        if (norm > 3) norm = 3;
        if (norm < -3) norm = -3;
        return norm;
    });
}

/**
 * Preprocesses a window of accelerometer data for the neural network.
 * Steps:
 * 1. Resample to fixed size
 * 2. Separate gravity and linear acceleration
 * 3. Compute PCA on linear acceleration
 * 4. Project linear and gravity onto PCA basis (PC1, PC2, PC3)
 * 5. Normalize each channel (zero mean, unit variance, clip outliers)
 * 6. Flatten into interleaved array [t0_c0, t0_c1, ... t1_c0...] where features are (PC1, PC2, PC3, G1, G2, G3)
 */
export function preprocessWindow(data: AccelDataPoint[], resampleSize: number): number[] {
    // 1. Resample
    const resampled = resampleAccelData(data, resampleSize);

    // 2. Separate Gravity
    const { linear, gravity } = separateGravityAndLinearAccel(resampled, 0.8);

    // 3. PCA on Linear
    const points: Point3D[] = linear.map((p) => ({ x: p.x, y: p.y, z: p.z }));
    let pca: PCAResult;
    try {
        pca = PCA.compute(points);
    } catch (e) {
        // Fallback for insufficient data or singular matrix
        // Return zeros
        return new Array(resampleSize * 6).fill(0);
    }

    // 4. Project to PCA frame
    const linearPCA = PCA.projectTo3D(points, pca);
    // Gravity is projected using the SAME basis (linear motion's PCA)
    const gravityPoints = gravity.map((p) => ({ x: p.x, y: p.y, z: p.z }));
    const gravityPCA = PCA.projectTo3D(gravityPoints, pca);

    // 5. Gather Channels
    // Channels: PC1, PC2, PC3, G1, G2, G3
    const channels = [
        linearPCA.map((p) => p.pc1),
        linearPCA.map((p) => p.pc2),
        linearPCA.map((p) => p.pc3),
        gravityPCA.map((p) => p.pc1),
        gravityPCA.map((p) => p.pc2),
        gravityPCA.map((p) => p.pc3),
    ];

    // 6. Normalize
    const normalized = channels.map(normalizeChannel);

    // 7. Flatten (Time-major)
    // TF.js expects [samples, time, features]
    // We return a flat array representing one sample: [time * features]
    const result: number[] = [];
    for (let i = 0; i < resampleSize; i++) {
        for (let c = 0; c < 6; c++) {
            result.push(normalized[c][i]);
        }
    }
    return result;
}

export type RingOrientationType = 'world-order' | 'ring-order' | 'ring-and-world-order';

export function prepareTrainingData(
    datapoints: { data: AccelDataPoint[]; label: string }[],
    resampleSize: number,
    type: RingOrientationType,
): { data: number[]; label: string }[] {
    let allData: { data: AccelDataPoint[]; label: string }[] = [];

    // 1. Original Data
    allData.push(...datapoints);

    // 2. Rotation Augmentation (Random Z rotations - simulating Yaw)
    // 8 rotations (every 45 degrees)
    const rotated = datapoints.flatMap((e) =>
        augmentByZRotation(e.data, 8).map((a) => ({ label: e.label, data: a })),
    );
    allData.push(...rotated);

    // 3. Noise Augmentation
    // Add noise to both original and rotated data
    // 1 copy of noise per sample
    const noisyOriginal = datapoints.flatMap((e) => 
        augmentByNoise(e.data, 1, 0.05).map(a => ({ label: e.label, data: a }))
    );
    
    // Also add noise to a subset of rotated data (e.g., just 1 noise version per rotated sample? Too much data maybe?)
    // Let's just augment the original data with noise for now to avoid explosion.
    allData.push(...noisyOriginal);

    console.log(`Prepared ${allData.length} samples from ${datapoints.length} originals.`);

    // 4. Preprocess all
    return allData.map((e) => ({
        data: preprocessWindow(e.data, resampleSize),
        label: e.label,
    }));
}

export function prepareClassificationData(data: AccelDataPoint[], resampleSize: number) {
    return preprocessWindow(data, resampleSize);
}
