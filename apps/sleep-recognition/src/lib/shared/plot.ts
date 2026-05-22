import Chart from 'chart.js/auto';

export type AccelDataPoint = {
    x: number;
    y: number;
    z: number;
    timestamp: string;
};

export class AccelChart {
    private chart: Chart | null = null;
    private chartData: AccelDataPoint[] = [];
    private canvasId: string;

    constructor(canvasId: string) {
        this.canvasId = canvasId;
        this.init();
    }

    private init() {
        const ctx = (document.getElementById(this.canvasId) as HTMLCanvasElement)?.getContext('2d');
        if (!ctx) throw new Error('Canvas not found');

        this.chart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    {
                        label: 'X',
                        data: [],
                        borderColor: '#e53e3e',
                        fill: false,
                    },
                    {
                        label: 'Y',
                        data: [],
                        borderColor: '#3182ce',
                        fill: false,
                    },
                    {
                        label: 'Z',
                        data: [],
                        borderColor: '#38a169',
                        fill: false,
                    },
                ],
            },
            options: {
                responsive: true,
                animation: false,
                scales: {
                    x: {
                        title: { display: true, text: 'Timestamp' },
                    },
                    y: {
                        title: { display: true, text: 'Acceleration (g)' },
                    },
                },
            },
        });
    }

    plot(data: AccelDataPoint) {
        this.chartData.push(data);
        if (!this.chart) return;
        this.chart.data.labels?.push(data.timestamp);
        this.chart.data.datasets[0].data.push(data.x);
        this.chart.data.datasets[1].data.push(data.y);
        this.chart.data.datasets[2].data.push(data.z);
        // Limit to last 100 points
        if (this.chartData.length > 100) {
            this.chartData.shift();
            this.chart.data.labels?.shift();
            this.chart.data.datasets.forEach((ds) => ds.data.shift());
        }
        this.chart.update();
    }

    clear() {
        this.chartData = [];
        if (!this.chart) return;
        this.chart.data.labels = [];
        this.chart.data.datasets.forEach((ds) => (ds.data = []));
        this.chart.update();
    }
}
