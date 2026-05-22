import { decomposeHorizontalVertical, separateGravityAndLinear } from '../math/gravity';
import { PipelineInput } from '../math/types';
import { PCA } from '../math/pca';

export type TapType = 'HORIZONTAL_DOUBLE_TAP' | 'VERTICAL_DOUBLE_TAP' | 'UNKNOWN';

export class TapDetector {
    private static readonly TEMPLATE_SIZE = 6; // approx 600ms at 50Hz
    private static template: number[] = [];

    /**
     * Lazy initialization of the double-tap template.
     * Creates a generic "bump-bump" pattern.
     */
    private static getTemplate(): number[] {
        if (this.template.length > 0) return this.template;

        // Create a synthetic double tap shape (e.g., two Gaussian-like pulses)
        // 0..10: first tap, 10..20: gap, 20..30: second tap
        for (let i = 0; i < this.TEMPLATE_SIZE; i++) {
            let val = 0;
            // First tap peak at index 7
            val += Math.exp(-Math.pow(i - 7, 2) / 4);
            // Second tap peak at index 22
            val += Math.exp(-Math.pow(i - 22, 2) / 4);
            this.template.push(val);
        }
        return this.template;
    }

    /**
     * Determines if the segment contains a horizontal or vertical double tap.
     * @param window The accelerometer data window.
     * @param gravity The current gravity estimator instance (or vector).
     * @returns Tap type.
     */
    static analyze(window: PipelineInput[]): TapType {
        const samples = window.length;
        if (samples < this.TEMPLATE_SIZE) return 'UNKNOWN';

        const separated = separateGravityAndLinear(window, 0.99);

        // Use PCA to discover the most variant axis
        const linearPoints = separated.linear.map((p) => ({ x: p.x, y: p.y, z: p.z }));

        let pca;
        try {
            pca = PCA.compute(linearPoints);
        } catch (e) {
            console.log('PCA computation failed:', e);
            return 'UNKNOWN';
        }

        // Get the first principal component (most variant direction)
        const primaryAxis = pca.eigenvectors[0]; // { x, y, z }

        // Compute average gravity vector
        let avgGravity = { x: 0, y: 0, z: 0 };
        for (const g of separated.gravity) {
            avgGravity.x += g.x;
            avgGravity.y += g.y;
            avgGravity.z += g.z;
        }
        avgGravity.x /= separated.gravity.length;
        avgGravity.y /= separated.gravity.length;
        avgGravity.z /= separated.gravity.length;

        // Normalize average gravity vector
        const gMag = Math.sqrt(avgGravity.x ** 2 + avgGravity.y ** 2 + avgGravity.z ** 2);
        if (gMag < 1e-6) return 'UNKNOWN';

        const avgGravityNorm = {
            x: avgGravity.x / gMag,
            y: avgGravity.y / gMag,
            z: avgGravity.z / gMag,
        };

        // Compute angle between most variant axis and average gravity
        const dotProduct =
            primaryAxis.x * avgGravityNorm.x + primaryAxis.y * avgGravityNorm.y + primaryAxis.z * avgGravityNorm.z;

        // Clamp to avoid floating point errors in acos
        const clampedDot = Math.max(-1, Math.min(1, Math.abs(dotProduct)));
        const angleWithGravity = Math.acos(clampedDot) * (180 / Math.PI);

        // Classification based on angle
        // ~0°: vertical motion (aligned with gravity)
        // ~90°: horizontal motion (perpendicular to gravity)
        const threshold = 45; // degrees
        const thresholdRejectionRange = 10;

        if (Math.abs(angleWithGravity - threshold) <= thresholdRejectionRange) return 'UNKNOWN';
        if (angleWithGravity < threshold) {
            return 'VERTICAL_DOUBLE_TAP';
        } else {
            return 'HORIZONTAL_DOUBLE_TAP';
        }

        // COMMENTED OUT: Previous direct V/H computation approach
        // // Simplified approach: direct computation of vertical and horizontal components
        // let maxVertical = 0;
        // let maxHorizontal = 0;
        //
        // for (let i = 0; i < separated.gravity.length; i++) {
        //     const g = separated.gravity[i]; // gravity estimate (ĝ)
        //     const a_dyn = separated.linear[i]; // dynamic acceleration
        //
        //     // Normalize gravity vector
        //     const gMag = Math.sqrt(g.x * g.x + g.y * g.y + g.z * g.z);
        //     if (gMag < 1e-6) continue;
        //
        //     const g_hat = { x: g.x / gMag, y: g.y / gMag, z: g.z / gMag };
        //
        //     // a_vert = dot(a_dyn, ĝ)
        //     const a_vert = a_dyn.x * g_hat.x + a_dyn.y * g_hat.y + a_dyn.z * g_hat.z;
        //
        //     // a_horiz = ||a_dyn - a_vert * ĝ||
        //     const a_horiz_vec = {
        //         x: a_dyn.x - a_vert * g_hat.x,
        //         y: a_dyn.y - a_vert * g_hat.y,
        //         z: a_dyn.z - a_vert * g_hat.z,
        //     };
        //     const a_horiz = Math.sqrt(
        //         a_horiz_vec.x * a_horiz_vec.x + a_horiz_vec.y * a_horiz_vec.y + a_horiz_vec.z * a_horiz_vec.z,
        //     );
        //
        //     // Update maximums
        //     maxVertical = Math.max(maxVertical, Math.abs(a_vert));
        //     maxHorizontal = Math.max(maxHorizontal, a_horiz);
        // }
        //
        // // V = max(|a_vert|), H = max(|a_horiz|)
        // const V = maxVertical;
        // const H = maxHorizontal;
        //
        // console.log('Tap Detection - V:', V.toFixed(3), 'H:', H.toFixed(3));
        //
        // const threshold = 0.5; // minimum acceleration to consider a tap
        // if (Math.max(V, H) < threshold) return 'UNKNOWN';
        //
        // // V > H -> vertical, H > V -> horizontal
        // if (V > H) return 'VERTICAL_DOUBLE_TAP';
        // else return 'HORIZONTAL_DOUBLE_TAP';
    }

    private static maxCorrelation(signal: number[], template: number[]): number {
        // Sliding window cross-correlation
        // We only check valid overlaps where template fits inside signal??
        // Or signal is the window?
        // Signal is the full window (likely ~1s). Template is ~600ms.

        let maxCorr = -1;
        const n = signal.length;
        const m = template.length;

        if (n < m) return -1;

        // Precompute template stats
        const tMean = template.reduce((a, b) => a + b, 0) / m;
        const tStd = Math.sqrt(template.reduce((a, b) => a + Math.pow(b - tMean, 2), 0));

        if (tStd === 0) return 0;

        for (let i = 0; i <= n - m; i++) {
            let sum = 0;
            let sMean = 0;
            // Compute mean of current window slice
            for (let j = 0; j < m; j++) sMean += signal[i + j];
            sMean /= m;

            let sStd = 0;
            for (let j = 0; j < m; j++) {
                const sVal = signal[i + j];
                sStd += Math.pow(sVal - sMean, 2);
            }
            sStd = Math.sqrt(sStd);

            if (sStd === 0) continue;

            for (let j = 0; j < m; j++) {
                const sVal = signal[i + j];
                const tVal = template[j];
                sum += (sVal - sMean) * (tVal - tMean);
            }

            const corr = sum / (sStd * tStd);
            if (corr > maxCorr) maxCorr = corr;
        }

        return maxCorr;
    }
}
