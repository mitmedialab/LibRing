import { AccelDataPoint } from '../plot';

/**
 * Rotates an array of AccelDataPoint about the z axis in n steps (360/n degrees per step).
 * Returns an array of arrays, each being the rotated version at a given angle.
 */
export function augmentByZRotation(data: AccelDataPoint[], n: number): AccelDataPoint[][] {
    const radiansPerStep = (2 * Math.PI) / n;
    const augmented: AccelDataPoint[][] = [];
    for (let i = 0; i < n; i++) {
        const angle = i * radiansPerStep;
        const cosA = Math.cos(angle);
        const sinA = Math.sin(angle);
        const rotated = data.map((pt) => ({
            x: pt.x * cosA - pt.y * sinA,
            y: pt.x * sinA + pt.y * cosA,
            z: pt.z,
            timestamp: pt.timestamp,
        }));
        augmented.push(rotated);
    }
    return augmented;
}

/**
 * Rotates an array of AccelDataPoint about the x axis in n steps (360/n degrees per step).
 * Returns an array of arrays, each being the rotated version at a given angle.
 *
 * X axis is the principle axis of the ring, applying this on the linear frame before
 * transforming it into world coordinates will
 */
export function augmentByXRotation(data: AccelDataPoint[], n: number): AccelDataPoint[][] {
    const radiansPerStep = (2 * Math.PI) / n;
    const augmented: AccelDataPoint[][] = [];
    for (let i = 0; i < n; i++) {
        const angle = i * radiansPerStep;
        const cosA = Math.cos(angle);
        const sinA = Math.sin(angle);
        const rotated = data.map((pt) => ({
            x: pt.x,
            y: pt.y * cosA - pt.z * sinA,
            z: pt.y * sinA + pt.z * cosA,
            timestamp: pt.timestamp,
        }));
        augmented.push(rotated);
    }
    return augmented;
}

/**
 * Adds Gaussian noise to the data.
 * @param data Input data
 * @param n Number of augmented copies to generate
 * @param std Standard deviation of noise (relative to signal conceptual magnitude, e.g. 0.05gs)
 */
export function augmentByNoise(data: AccelDataPoint[], n: number, std: number = 0.05): AccelDataPoint[][] {
    const augmented: AccelDataPoint[][] = [];
    for (let k = 0; k < n; k++) {
        const noisy = data.map(pt => ({
            x: pt.x + (Math.random() - 0.5) * 2 * std,
            y: pt.y + (Math.random() - 0.5) * 2 * std,
            z: pt.z + (Math.random() - 0.5) * 2 * std,
            timestamp: pt.timestamp
        }));
        augmented.push(noisy);
    }
    return augmented;
}
