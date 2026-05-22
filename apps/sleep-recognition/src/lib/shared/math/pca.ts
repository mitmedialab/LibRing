import { Point3D } from './types';

export interface PCAResult {
    mean: Point3D;
    eigenvectors: Point3D[]; // Sorted by eigenvalue descending
    eigenvalues: number[];
}

export class PCA {
    /**
     * Computes the Principal Components of a 3D point cloud.
     */
    static compute(points: Point3D[]): PCAResult {
        if (points.length < 2) {
            throw new Error('Need at least 2 points for PCA');
        }

        // 1. Compute Mean
        const mean = { x: 0, y: 0, z: 0 };
        for (const p of points) {
            mean.x += p.x;
            mean.y += p.y;
            mean.z += p.z;
        }
        mean.x /= points.length;
        mean.y /= points.length;
        mean.z /= points.length;

        // 2. Compute Covariance Matrix (3x3 Symmetric)
        // [ xx xy xz ]
        // [ xy yy yz ]
        // [ xz yz zz ]
        let xx = 0,
            yy = 0,
            zz = 0,
            xy = 0,
            xz = 0,
            yz = 0;

        for (const p of points) {
            const dx = p.x - mean.x;
            const dy = p.y - mean.y;
            const dz = p.z - mean.z;

            xx += dx * dx;
            yy += dy * dy;
            zz += dz * dz;
            xy += dx * dy;
            xz += dx * dz;
            yz += dy * dz;
        }

        const n = points.length - 1 || 1; // Unbiased estimator
        const cov = [
            [xx / n, xy / n, xz / n],
            [xy / n, yy / n, yz / n],
            [xz / n, yz / n, zz / n],
        ];

        // 3. Eigen Decomposition (Jacobi Iteration for 3x3 Real Symmetric)
        const { vectors, values } = this.jacobi3x3(cov);

        // 4. Sort by eigenvalue descending
        const indices = [0, 1, 2].sort((a, b) => values[b] - values[a]);

        return {
            mean,
            eigenvectors: indices.map((i) => ({
                x: vectors[0][i],
                y: vectors[1][i],
                z: vectors[2][i],
            })),
            eigenvalues: indices.map((i) => values[i]),
        };
    }

    /**
     * Projects points onto the 2D plane defined by the top 2 principal components.
     * @param points Input 3D points
     * @param pcaResult Precomputed PCA result
     */
    static projectTo2D(points: Point3D[], pcaResult: PCAResult): { u: number; v: number }[] {
        const uAxis = pcaResult.eigenvectors[0];
        const vAxis = pcaResult.eigenvectors[1];
        const origin = pcaResult.mean;

        return points.map((p) => {
            const dx = p.x - origin.x;
            const dy = p.y - origin.y;
            const dz = p.z - origin.z;

            // Dot product
            const u = dx * uAxis.x + dy * uAxis.y + dz * uAxis.z;
            const v = dx * vAxis.x + dy * vAxis.y + dz * vAxis.z;

            return { u, v };
        });
    }

    /**
     * Projects points into the full 3D coordinate system defined by all three eigenvectors.
     * This transforms points from world coordinates to principal component coordinates.
     * @param points Input 3D points
     * @param pcaResult Precomputed PCA result
     * @returns Points in principal component coordinate system {pc1, pc2, pc3}
     */
    static projectTo3D(points: Point3D[], pcaResult: PCAResult): { pc1: number; pc2: number; pc3: number }[] {
        const pc1Axis = pcaResult.eigenvectors[0]; // First principal component (largest eigenvalue)
        const pc2Axis = pcaResult.eigenvectors[1]; // Second principal component
        const pc3Axis = pcaResult.eigenvectors[2]; // Third principal component (smallest eigenvalue)
        const origin = pcaResult.mean;

        return points.map((p) => {
            const dx = p.x - origin.x;
            const dy = p.y - origin.y;
            const dz = p.z - origin.z;

            // Dot product with each eigenvector to get coordinates in PC space
            const pc1 = dx * pc1Axis.x + dy * pc1Axis.y + dz * pc1Axis.z;
            const pc2 = dx * pc2Axis.x + dy * pc2Axis.y + dz * pc2Axis.z;
            const pc3 = dx * pc3Axis.x + dy * pc3Axis.y + dz * pc3Axis.z;

            return { pc1, pc2, pc3 };
        });
    }

    private static jacobi3x3(matrix: number[][]): { vectors: number[][]; values: number[] } {
        // Identity matrix for eigenvectors
        let V = [
            [1, 0, 0],
            [0, 1, 0],
            [0, 0, 1],
        ];
        let D = [
            [matrix[0][0], matrix[0][1], matrix[0][2]],
            [matrix[1][0], matrix[1][1], matrix[1][2]],
            [matrix[2][0], matrix[2][1], matrix[2][2]],
        ];

        const maxIter = 50;
        const epsilon = 1e-10;

        for (let iter = 0; iter < maxIter; iter++) {
            // Find pivot (max off-diagonal element)
            let p = 0,
                q = 1;
            let maxOff = Math.abs(D[0][1]);

            if (Math.abs(D[0][2]) > maxOff) {
                p = 0;
                q = 2;
                maxOff = Math.abs(D[0][2]);
            }
            if (Math.abs(D[1][2]) > maxOff) {
                p = 1;
                q = 2;
                maxOff = Math.abs(D[1][2]);
            }

            if (maxOff < epsilon) break;

            const app = D[p][p];
            const aqq = D[q][q];
            const apq = D[p][q];

            const phi = 0.5 * Math.atan2(2 * apq, aqq - app);
            const c = Math.cos(phi);
            const s = Math.sin(phi);

            // Rotation
            // Update diagonal elements
            D[p][p] = c * c * app - 2 * s * c * apq + s * s * aqq;
            D[q][q] = s * s * app + 2 * s * c * apq + c * c * aqq;
            D[p][q] = 0; // Rotated to zero
            D[q][p] = 0;

            // Update other elements
            for (let k = 0; k < 3; k++) {
                if (k !== p && k !== q) {
                    const akp = D[k][p];
                    const akq = D[k][q];
                    D[k][p] = c * akp - s * akq;
                    D[p][k] = D[k][p];
                    D[k][q] = s * akp + c * akq;
                    D[q][k] = D[k][q];
                }
            }

            // Update eigenvectors
            // V_new = V * Rotation
            for (let k = 0; k < 3; k++) {
                const vkp = V[k][p];
                const vkq = V[k][q];
                V[k][p] = c * vkp - s * vkq;
                V[k][q] = s * vkp + c * vkq;
            }
        }

        return {
            vectors: V,
            values: [D[0][0], D[1][1], D[2][2]],
        };
    }
}
