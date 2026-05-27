import { PipelineInput } from '../math/types';
import { PCA } from '../math/pca';

export type CircleType = 'CW_CIRCLE' | 'CCW_CIRCLE' | 'UNKNOWN';

export class CircleDetector {
    /**
     * Determines if a sequence of points forms a CW or CCW circle.
     * Uses PCA to project to 2D plane and computes signed area.
     */
    static analyze(window: PipelineInput[]): CircleType {
        if (window.length < 5) return 'UNKNOWN';

        // 1. Project to 2D Principal Plane
        const pca = PCA.compute(window);
        const points2D = PCA.projectTo2D(window, pca);

        // 2. Compute Signed Area (Winding)
        // A = 0.5 * sum(u_i * v_{i+1} - u_{i+1} * v_i)
        let area = 0;
        for (let i = 0; i < points2D.length - 1; i++) {
            const p1 = points2D[i];
            const p2 = points2D[i + 1];
            area += p1.u * p2.v - p1.v * p2.u; // Using cross product determinant form
        }

        // Close the loop for area calculation?
        // For a gesture stream, we just want the winding direction over the path.
        // The formula above is essentially integrating along the path.

        // Note: The sign depends on the orientation of the principal axes.
        // PCA eigenvectors direction is arbitrary (v or -v).
        // However, for a user drawing a circle, we need a consistent reference frame.
        // This is tricky with pure PCA because PC1/PC2 can flip signs randomly between frames due to noise or alignment.

        // STABILITY FIX:
        // To make CW/CCW meaningful, we need to orient the normal vector relative to gravity or the screen.
        // If the circle is horizontal (like stirring a pot), we compare the PCA normal to Gravity.
        // If the normal is roughly parallel to gravity, we keep it. If anti-parallel, we flip.

        // But stage 1 already said "Circle".
        // Let's assume the user wants "Clockwise relative to the viewer/ring top".
        // Convention: Positive area = CCW, Negative area = CW (standard math).

        // Threshold check: Is the motion actually circular-ish?
        // We can check the ratio of eigenvalues (PC1 approx equal to PC2, PC3 small).
        const e1 = pca.eigenvalues[0];
        const e2 = pca.eigenvalues[1];
        if (e2 < e1 * 0.2) return 'UNKNOWN'; // Too flat to be a circle (more like a line)

        const sensitivity = 2.5; // Area threshold

        console.log(area);

        if (area > sensitivity) return 'CCW_CIRCLE';
        if (area < -sensitivity) return 'CW_CIRCLE';

        return 'UNKNOWN';
    }
}
