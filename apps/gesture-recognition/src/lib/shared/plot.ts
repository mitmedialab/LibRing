// Accelerometer chart, drawn with the LibRing plot library shared by the web apps
// (apps/shared/plot.js, no dependency; same look as the PPG page).
import { LineChart } from '../../../../shared/plot.js';

export type AccelDataPoint = {
    x: number;
    y: number;
    z: number;
    timestamp: string;
};

export class AccelChart {
    private chart: LineChart;

    constructor(canvasId: string, span = 500) {
        this.chart = new LineChart(
            canvasId,
            [
                { label: 'X', color: '#e53e3e' },
                { label: 'Y', color: '#3182ce' },
                { label: 'Z', color: '#38a169' },
            ],
            { span, unit: 'g' },
        );
    }

    plot(data: AccelDataPoint) {
        this.chart.push([data.x, data.y, data.z]);
    }

    clear() {
        this.chart.clear();
    }
}
