import { Point3D, PipelineInput } from './types';

export interface GravityLinearSeparation {
    linear: PipelineInput[];
    gravity: PipelineInput[];
}

export function separateGravityAndLinear(data: PipelineInput[], alpha: number = 0.85): GravityLinearSeparation {
    let gravityX = data[0].x,
        gravityY = data[0].y,
        gravityZ = data[0].z;
    const gravity: PipelineInput[] = [];
    const linear: PipelineInput[] = [];

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

export function decomposeHorizontalVertical(
    separation: GravityLinearSeparation,
): { vertical: number; horizontal: number }[] {
    const out = [];
    let weightedAngleSum = 0;
    let totalWeight = 0;

    for (let i = 0; i < separation.gravity.length; i++) {
        const grav = separation.gravity[i];
        const linear = separation.linear[i];

        const gMagSq = grav.x ** 2 + grav.y ** 2 + grav.z ** 2;

        if (gMagSq < 1e-6) {
            out.push({ vertical: 0, horizontal: 0 });
            continue;
        }

        const gMag = Math.sqrt(gMagSq);

        const dot = linear.x * grav.x + linear.y * grav.y + linear.z * grav.z;

        const verticalComponent = dot / gMag;

        // Vertical vector
        const vX = (verticalComponent * grav.x) / gMag;
        const vY = (verticalComponent * grav.y) / gMag;
        const vZ = (verticalComponent * grav.z) / gMag;

        const hX = linear.x - vX;
        const hY = linear.y - vY;
        const hZ = linear.z - vZ;

        // Compute angle between linear acceleration vector and gravity vector
        const linearMag = Math.sqrt(linear.x ** 2 + linear.y ** 2 + linear.z ** 2);
        let angleWithGravity = 0;
        if (linearMag > 1e-6) {
            // cos(θ) = (a⃗ · g⃗) / (|a⃗| * |g⃗|)
            const cosTheta = dot / (linearMag * gMag);
            // Clamp to [-1, 1] to avoid floating point errors in acos, and clamp from 0 to 90 deg
            const clampedCosTheta = Math.abs(Math.max(-1, Math.min(1, cosTheta)));
            angleWithGravity = Math.acos(clampedCosTheta) * (180 / Math.PI); // Convert to degrees

            // Add to weighted average (weight by linear acceleration magnitude)
            weightedAngleSum += angleWithGravity * linearMag;
            totalWeight += linearMag;
        }

        console.log(
            'gravity: ',
            grav,
            ', dot: ',
            dot,
            ', vertical: ',
            Math.hypot(vX, vY, vZ),
            ', horizontal: ',
            Math.hypot(hX, hY, hZ),
            ', angle with gravity: ',
            angleWithGravity.toFixed(2) + '°',
        );

        // Horizontal magnitude
        const horizontalComponent = Math.sqrt(hX ** 2 + hY ** 2 + hZ ** 2);

        out.push({ vertical: verticalComponent, horizontal: horizontalComponent });
    }

    // Compute and log weighted average angle
    const weightedAverageAngle = totalWeight > 0 ? weightedAngleSum / totalWeight : 0;
    console.log('Weighted average angle with gravity:', weightedAverageAngle.toFixed(2) + '°');

    return out;
}
