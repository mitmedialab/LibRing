import { AccelDataPoint } from '../plot';

export type GravitySeparationResult = {
    gravity: AccelDataPoint[];
    linear: AccelDataPoint[];
};

function normalize(v: { x: number; y: number; z: number }) {
    const mag = Math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return mag === 0 ? { x: 0, y: 0, z: 0 } : { x: v.x / mag, y: v.y / mag, z: v.z / mag };
}

function cross(a: { x: number; y: number; z: number }, b: { x: number; y: number; z: number }) {
    return {
        x: a.y * b.z - a.z * b.y,
        y: a.z * b.x - a.x * b.z,
        z: a.x * b.y - a.y * b.x,
    };
}

function dot(a: { x: number; y: number; z: number }, b: { x: number; y: number; z: number }) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

export function rotateToAlignGravity(
    vec: { x: number; y: number; z: number },
    gravity: { x: number; y: number; z: number },
) {
    // Rotate so gravity aligns with (0,0,-1)
    const gNorm = normalize(gravity);
    const target = { x: 0, y: 0, z: -1 };
    const axis = cross(gNorm, target);
    const axisMag = Math.sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (axisMag < 1e-6) return vec; // Already aligned
    const angle = Math.acos(Math.max(-1, Math.min(1, dot(gNorm, target))));
    // Rodrigues' rotation formula
    const u = { x: axis.x / axisMag, y: axis.y / axisMag, z: axis.z / axisMag };
    const cosA = Math.cos(angle);
    const sinA = Math.sin(angle);
    const oneMinusCosA = 1 - cosA;
    const x = vec.x,
        y = vec.y,
        z = vec.z;
    return {
        x:
            (cosA + u.x * u.x * oneMinusCosA) * x +
            (u.x * u.y * oneMinusCosA - u.z * sinA) * y +
            (u.x * u.z * oneMinusCosA + u.y * sinA) * z,
        y:
            (u.y * u.x * oneMinusCosA + u.z * sinA) * x +
            (cosA + u.y * u.y * oneMinusCosA) * y +
            (u.y * u.z * oneMinusCosA - u.x * sinA) * z,
        z:
            (u.z * u.x * oneMinusCosA - u.y * sinA) * x +
            (u.z * u.y * oneMinusCosA + u.x * sinA) * y +
            (cosA + u.z * u.z * oneMinusCosA) * z,
    };
}

export function separateGravityAndLinearAccel(data: AccelDataPoint[], alpha: number = 0.8): GravitySeparationResult {
    let gravityX = data[0].x,
        gravityY = data[0].y,
        gravityZ = data[0].z;
    const gravity: AccelDataPoint[] = [];
    const linear: AccelDataPoint[] = [];

    for (const point of data) {
        gravityX = alpha * gravityX + (1 - alpha) * point.x;
        gravityY = alpha * gravityY + (1 - alpha) * point.y;
        gravityZ = alpha * gravityZ + (1 - alpha) * point.z;

        gravity.push({
            x: gravityX,
            y: gravityY,
            z: gravityZ,
            timestamp: point.timestamp,
        });
        linear.push({
            x: point.x - gravityX,
            y: point.y - gravityY,
            z: point.z - gravityZ,
            timestamp: point.timestamp,
        });
    }

    return { gravity, linear };
}

/**
 * Rotates each linear acceleration vector so the corresponding gravity vector aligns with (0,0,-1).
 * Returns a new array of AccelDataPoint in the world frame.
 */
export function toWorldFrame({ linear, gravity }: GravitySeparationResult): AccelDataPoint[] {
    if (gravity.length !== linear.length) throw new Error('Array lengths must match');
    return linear.map((vec, i) => {
        const rotated = rotateToAlignGravity(vec, gravity[i]);
        return { ...rotated, timestamp: vec.timestamp };
    });
}
