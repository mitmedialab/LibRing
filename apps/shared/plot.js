// LibRing plots: small canvas line charts shared by the web apps (no dependency).
// Style: light vertical grid (10 divisions), auto scale with 8 % margins, round line
// joins, newest data on the right of a fixed span, optional range labels.
// Colors: pass CSS colors; the grid / label colors default to the page's --grid /
// --muted CSS variables, else a neutral grey that works on light and dark backgrounds.
//
//   import { drawSeries, LineChart } from '../shared/plot.js';
//   drawSeries(canvas, values, '#16a34a', { span: 250 });          // one trace
//   drawSeries(canvas, other, '#7c3aed', { span: 250, overlay: true }); // + another, own scale
//   const chart = new LineChart('canvas-id', [{ label: 'X', color: '#e53e3e' }, ...], { span: 500 });
//   chart.push([x, y, z]);                                          // streaming, shared scale

const cssVar = (name, fallback) => {
    const v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
    return v || fallback;
};

/**
 * Fit the canvas backing store to its CSS size (device pixels) and return a 2D
 * context drawing in CSS pixels, with the size.
 * @param {HTMLCanvasElement} canvas
 */
export function prepareCanvas(canvas) {
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth, h = canvas.clientHeight;
    if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
        canvas.width = Math.round(w * dpr);
        canvas.height = Math.round(h * dpr);
    }
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { ctx, w, h };
}

/**
 * Draw one series (a line; NaN / null values leave gaps, isolated points are dots).
 * @param {HTMLCanvasElement} canvas
 * @param {ArrayLike<number|null>} values oldest first
 * @param {string} color CSS color
 * @param {object} [opts]
 * @param {number} [opts.span] number of points across the width (default: values.length)
 * @param {number} [opts.width] line width in CSS pixels (default 1.5)
 * @param {boolean} [opts.overlay] draw over the canvas (no clear, no grid)
 * @param {number} [opts.min] scale (default: the data range)
 * @param {number} [opts.max]
 * @param {boolean} [opts.labels] write the scale range on the right
 * @param {(v: number) => string} [opts.format] label format (default: no decimals)
 * @param {string} [opts.grid] grid color, [opts.text] label color
 * @returns {{min: number, max: number, last: number} | null} the scale and last value
 */
export function drawSeries(canvas, values, color, opts = {}) {
    const { ctx, w, h } = prepareCanvas(canvas);
    const grid = opts.grid || cssVar('--grid', 'rgba(128, 128, 128, 0.18)');
    if (!opts.overlay) {
        ctx.clearRect(0, 0, w, h);
        ctx.strokeStyle = grid;
        ctx.lineWidth = 1;
        for (let s = 1; s < 10; s++) {
            const x = Math.round((w * s) / 10) + 0.5;
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, h);
            ctx.stroke();
        }
    }
    const ok = [];
    for (let i = 0; i < values.length; i++) if (Number.isFinite(values[i])) ok.push(values[i]);
    if (!ok.length) return null;
    let min = opts.min ?? Math.min(...ok), max = opts.max ?? Math.max(...ok);
    if (max - min < 1e-9) { max += 1; min -= 1; }
    const pad = (max - min) * 0.08;
    const span = Math.max(2, opts.span || values.length);
    const x0 = w * (1 - values.length / span);
    const X = (i) => x0 + (i / (span - 1)) * w;
    const Y = (v) => h - ((v - min + pad) / (max - min + 2 * pad)) * h;
    ctx.strokeStyle = color;
    ctx.fillStyle = color;
    ctx.lineWidth = opts.width || 1.5;
    ctx.lineJoin = 'round';
    ctx.beginPath();
    let pen = false;
    for (let i = 0; i < values.length; i++) {
        const v = values[i];
        if (!Number.isFinite(v)) { pen = false; continue; }
        if (pen) ctx.lineTo(X(i), Y(v)); else ctx.moveTo(X(i), Y(v));
        pen = true;
    }
    ctx.stroke();
    for (let i = 0; i < values.length; i++) {
        const v = values[i];
        if (Number.isFinite(v) && !Number.isFinite(values[i - 1]) && !Number.isFinite(values[i + 1])) {
            ctx.beginPath();
            ctx.arc(X(i), Y(v), (opts.width || 1.5) + 0.3, 0, 2 * Math.PI);
            ctx.fill();
        }
    }
    if (opts.labels) {
        const fmt = opts.format || ((v) => v.toFixed(0));
        ctx.fillStyle = opts.text || cssVar('--muted', 'rgba(128, 128, 128, 0.95)');
        ctx.font = '11px system-ui, sans-serif';
        ctx.textAlign = 'right';
        ctx.fillText(fmt(max), w - 4, 12);
        ctx.fillText(fmt(min), w - 4, h - 4);
    }
    return { min, max, last: ok[ok.length - 1] };
}

/**
 * Streaming chart of several series on a shared scale, with a legend and the range
 * on the right. Redraws at most once per animation frame.
 */
export class LineChart {
    /**
     * @param {string | HTMLCanvasElement} canvas canvas or its id
     * @param {{label: string, color: string}[]} series
     * @param {object} [opts]
     * @param {number} [opts.span] points kept and shown (default 500)
     * @param {string} [opts.unit] written after the range labels
     * @param {(v: number) => string} [opts.format] range label format (default 2 decimals)
     */
    constructor(canvas, series, opts = {}) {
        this.canvas = typeof canvas === 'string' ? document.getElementById(canvas) : canvas;
        if (!this.canvas) throw new Error('Canvas not found');
        this.series = series;
        this.span = opts.span || 500;
        this.unit = opts.unit || '';
        this.format = opts.format || ((v) => v.toFixed(2));
        this.data = series.map(() => []);
        this.pending = false;
        this.draw();
    }

    /** Add one value per series (null: gap). */
    push(values) {
        this.data.forEach((d, i) => {
            d.push(values[i]);
            if (d.length > this.span) d.shift();
        });
        this.schedule();
    }

    clear() {
        this.data.forEach((d) => (d.length = 0));
        this.schedule();
    }

    schedule() {
        if (this.pending) return;
        this.pending = true;
        requestAnimationFrame(() => {
            this.pending = false;
            this.draw();
        });
    }

    draw() {
        let min = Infinity, max = -Infinity;
        for (const d of this.data) for (const v of d) if (Number.isFinite(v)) { min = Math.min(min, v); max = Math.max(max, v); }
        const scale = Number.isFinite(min) ? { min, max } : {};
        // the first series clears the canvas and draws the grid (also without data)
        this.series.forEach((s, i) => {
            drawSeries(this.canvas, this.data[i], s.color, { span: this.span, overlay: i > 0, ...scale });
        });
        const { ctx, w, h } = prepareCanvas(this.canvas);
        ctx.font = '12px system-ui, sans-serif';
        ctx.textAlign = 'left';
        let x = 6;
        for (const s of this.series) {
            ctx.fillStyle = s.color;
            ctx.fillText(s.label, x, 14);
            x += ctx.measureText(s.label).width + 10;
        }
        if (Number.isFinite(min)) {
            const unit = this.unit ? ` ${this.unit}` : '';
            ctx.fillStyle = cssVar('--muted', 'rgba(128, 128, 128, 0.95)');
            ctx.font = '11px system-ui, sans-serif';
            ctx.textAlign = 'right';
            ctx.fillText(this.format(max) + unit, w - 4, 12);
            ctx.fillText(this.format(min) + unit, w - 4, h - 4);
        }
    }
}
