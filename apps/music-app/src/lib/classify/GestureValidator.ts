import { AccelDataPoint } from '../shared/plot';

/**
 * Gesture-specific validators to reduce false positives.
 * Each validator checks if the raw accelerometer segment matches expected motion characteristics.
 */

// Utility: find zero crossings in a 1D signal
function findZeroCrossings(values: number[]): number[] {
    const crossings: number[] = [];
    for (let i = 1; i < values.length; i++) {
        if ((values[i - 1] < 0 && values[i] >= 0) || (values[i - 1] >= 0 && values[i] < 0)) {
            crossings.push(i);
        }
    }
    return crossings;
}

// Utility: compute jerk (derivative of acceleration)
function computeJerk(segment: AccelDataPoint[]): number[] {
    const jerks: number[] = [];
    for (let i = 1; i < segment.length; i++) {
        const dt = 1; // Assume uniform sampling; adjust if timestamps differ
        const jx = (segment[i].x - segment[i - 1].x) / dt;
        const jy = (segment[i].y - segment[i - 1].y) / dt;
        const jz = (segment[i].z - segment[i - 1].z) / dt;
        jerks.push(Math.hypot(jx, jy, jz));
    }
    return jerks;
}

// Utility: compute magnitude of each point
function computeMagnitudes(segment: AccelDataPoint[]): number[] {
    return segment.map((p) => Math.hypot(p.x, p.y, p.z));
}

/**
 * Validates circle gestures by checking for oscillatory patterns in X-Y plane.
 * Circles have approximately 90° phase shift between X and Y components.
 */
export function validateCircle(segment: AccelDataPoint[]): boolean {
    if (segment.length < 10) return false;

    const xVals = segment.map((p) => p.x);
    const yVals = segment.map((p) => p.y);

    const xCrossings = findZeroCrossings(xVals);
    const yCrossings = findZeroCrossings(yVals);

    // A circle should have at least 2 zero crossings in both X and Y (half a rotation)
    if (xCrossings.length < 2 || yCrossings.length < 2) return false;

    // Check for similar oscillation frequency (crossings should be roughly similar count)
    const crossingRatio = Math.min(xCrossings.length, yCrossings.length) / Math.max(xCrossings.length, yCrossings.length);
    if (crossingRatio < 0.5) return false;

    // Check amplitude is significant in both X and Y
    const xRange = Math.max(...xVals) - Math.min(...xVals);
    const yRange = Math.max(...yVals) - Math.min(...yVals);
    const amplitudeRatio = Math.min(xRange, yRange) / Math.max(xRange, yRange);

    // Circle should have relatively balanced X/Y amplitude (at least 30% ratio)
    return amplitudeRatio > 0.3;
}

/**
 * Validates tap gestures by checking for sharp impulse followed by damping.
 * Taps have high jerk (sudden change in acceleration) and short duration.
 */
export function validateTap(segment: AccelDataPoint[], minJerkRatio: number = 3): boolean {
    if (segment.length < 3) return false;

    const jerks = computeJerk(segment);
    if (jerks.length === 0) return false;

    const maxJerk = Math.max(...jerks);
    const avgJerk = jerks.reduce((a, b) => a + b, 0) / jerks.length;

    // Tap should have a sharp spike (high max/avg ratio)
    if (avgJerk === 0) return false;
    if (maxJerk / avgJerk < minJerkRatio) return false;

    // Tap should be relatively short (check duration based on segment length)
    // Assuming ~50Hz sampling, a tap should be < 0.5s = 25 samples
    if (segment.length > 30) return false;

    return true;
}

/**
 * Validates horizontal tap - expects dominant motion on horizontal plane (X axis typically)
 * after converting to world frame.
 */
export function validateHorizontalTap(segment: AccelDataPoint[]): boolean {
    if (!validateTap(segment)) return false;

    // Check if X-axis motion is dominant
    const xRange = Math.max(...segment.map((p) => p.x)) - Math.min(...segment.map((p) => p.x));
    const yRange = Math.max(...segment.map((p) => p.y)) - Math.min(...segment.map((p) => p.y));
    const zRange = Math.max(...segment.map((p) => p.z)) - Math.min(...segment.map((p) => p.z));

    const maxRange = Math.max(xRange, yRange, zRange);

    // Horizontal tap should have X or Y as dominant axis (not Z which is vertical)
    return (xRange === maxRange || yRange === maxRange) && zRange < maxRange * 0.7;
}

/**
 * Validates vertical tap - expects dominant motion on Z axis (vertical).
 */
export function validateVerticalTap(segment: AccelDataPoint[]): boolean {
    if (!validateTap(segment)) return false;

    const xRange = Math.max(...segment.map((p) => p.x)) - Math.min(...segment.map((p) => p.x));
    const yRange = Math.max(...segment.map((p) => p.y)) - Math.min(...segment.map((p) => p.y));
    const zRange = Math.max(...segment.map((p) => p.z)) - Math.min(...segment.map((p) => p.z));

    const maxRange = Math.max(xRange, yRange, zRange);

    // Vertical tap should have Z as dominant axis
    return zRange === maxRange && zRange > Math.max(xRange, yRange) * 1.2;
}

export type GestureType = 'circle-cw' | 'circle-ccw' | 'horiz-tap' | 'vert-tap' | 'idle' | string;

/**
 * Main validator that checks if the segment characteristics match the predicted gesture.
 * Returns true if the gesture passes validation, false otherwise.
 */
export function validateGesture(gesture: GestureType, segment: AccelDataPoint[]): boolean {
    switch (gesture) {
        case 'circle-cw':
        case 'circle-ccw':
            return validateCircle(segment);
        case 'horiz-tap':
            return validateHorizontalTap(segment);
        case 'vert-tap':
            return validateVerticalTap(segment);
        case 'idle':
            return true; // Idle always passes
        default:
            return true; // Unknown gestures pass by default
    }
}
