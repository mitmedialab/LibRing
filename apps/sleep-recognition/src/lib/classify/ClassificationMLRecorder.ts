import { separateGravityAndLinearAccel } from '../shared/ml/filters';
import { preprocessWindow, resampleAccelData } from '../shared/ml/preprocessing';
import { AccelDataPoint } from '../shared/plot';
import { AccelNtfHandler } from '../shared/accelerometer/ble';
import { CircleDetector } from '../shared/detection/circleDetection';
import { TapDetector } from '../shared/detection/tapDetection';
import { createReadStream } from 'node:original-fs';
import * as tf from '@tensorflow/tfjs';

export type ClassifyHandlerData = string;
// = {
//     results: any[];
//     segment: AccelDataPoint[];
// };

export default class ClassificationMLRecorder {
    // RAW accel data (needs to be processed)
    private datapoints: AccelDataPoint[] = [];
    private model: any; // tf.LayersModel
    private labels: string[] = ['circle', 'double-tap', 'idle']; // Default, updated on load
    private classifyHandler: (data: ClassifyHandlerData) => void = () => {};

    public constructor(
        private resampleSize: number = 20, // Match training default
        private sampleSize: number = 15, // ~6 seconds at 10Hz, or 1.2s at 50Hz. Large enough window.
        private ignoredAccelMagnitude: number = 0.1, // Threshold
    ) {
        // Init logic if needed
    }

    public getMLRecorderNtfHandler() {
        // Majority vote buffer
        const labelBuffer: string[] = [];
        const bufferSize = 4;

        let lastLabel: string = '';

        const handler = (d: AccelDataPoint) => {
            // Keep sliding window
            this.datapoints.push(d);
            if (this.datapoints.length > this.sampleSize) {
                this.datapoints.shift();
            }

            // if (!this.model) return;

            // Detect active segment based on Linear Accel Magnitude
            // We use the whole buffer to separate gravity properly first?
            // separateGravityAndLinearAccel uses logical filtering which needs history.
            // So separating on the whole sliding window is good.
            const { linear, gravity } = separateGravityAndLinearAccel(this.datapoints, 0.8);

            // Find active segment at the end of the window
            // We look backwards from end
            let startIdx = linear.length;
            let activeCount = 0;
            const silenceThreshold = 0.05; // Drop lower if needed
            // Simple logic: Trigger if last N points are active?
            // Or find the start of the current "action".

            // Simplified: Just take the last 'resampleSize' equivalent duration?
            // Or use the threshold logic from previous code:
            for (let i = linear.length - 1; i >= 0; i--) {
                const magn = Math.hypot(linear[i].x, linear[i].y, linear[i].z);
                if (magn >= this.ignoredAccelMagnitude) {
                    startIdx = i;
                    activeCount++;
                } else {
                    // Stop if we hit silence?
                    // gestures might have pauses.
                    // For now, let's take everything from the last silence gap.
                    // Or just strict active tail.
                    if (activeCount > 0) break; // We found the end of the segment
                }
            }
            // If startIdx is linear.length, no high energy found.
            if (startIdx === linear.length) return 'idle';

            // We want [startIdx ... end]
            // But we broke on the *first* silence after activity (going backwards).
            // So startIdx is the index of silence. Segment is startIdx+1 to end.
            // Wait, loop goes backwards.
            // If linear[end] is active, startIdx becomes end.
            // If linear[end-1] is active, startIdx becomes end-1.
            // If linear[end-2] is quiet, we break. startIdx is end-1.
            // So segment is linear.slice(startIdx).

            // Adjust startIdx to ensure we have enough points?
            // If segment is too short, maybe ignore or pad?
            // preprocessWindow resamples, so length doesn't strictly matter as long as it's a valid shape.

            const rawSegment = this.datapoints.slice(startIdx);
            const linearSegment = linear.slice(startIdx);

            if (rawSegment.length < 3) return 'idle'; // Too short

            // // Preprocess for NN
            // const features = preprocessWindow(rawSegment, this.resampleSize);
            // const tensor = tf.tensor2d([features], [1, this.resampleSize * 6]).reshape([1, this.resampleSize, 6]);

            // const pred = this.model.predict(tensor);
            // const values = await pred.data();
            // const idx = await pred.argMax(1).data();
            // const maxIdx = idx[0];
            // const confidence = values[maxIdx];
            // const predictedLabel = this.labels[maxIdx];

            // tensor.dispose();
            // pred.dispose();

            let finalLabel;

            // // Hybrid Logic
            // if (predictedLabel === 'circle') {
            // Determine CW/CCW
            // CircleDetector needs LINEAR data (user prompt: "Use original (non-PCA-rotated) linear acceleration")
            // We should resample it to get a clean path
            const resampledLinear = resampleAccelData(linearSegment, this.resampleSize);
            const subtype = CircleDetector.analyze(resampledLinear);
            console.log(`SUBTYPE: ${subtype}`);
            if (subtype !== 'UNKNOWN') {
                finalLabel = subtype === 'CW_CIRCLE' ? 'circle-ccw' : 'circle-cw'; // swapped due to ring orientation
            } else {
                finalLabel = 'idle';
            }
            // } // else if (predictedLabel === 'double-tap') {
            //     // Determine Horiz/Vert
            //     // TapDetector needs RAW data (user prompt: "Direction Classification: Use gravity-aligned frame")
            //     // And TapDetector implementation `analyze` calls `separateGravityAndLinear`.
            //     // So passed window should be RAW.
            //     const resampledRaw = resampleAccelData(rawSegment, this.resampleSize);
            //     const subtype = TapDetector.analyze(resampledRaw);
            //     if (subtype !== 'UNKNOWN') {
            //         // finalLabel = subtype === 'VERTICAL_DOUBLE_TAP' ? 'vert-tap' : 'horiz-tap';
            //         finalLabel = 'double-tap';
            //     } else {
            //         finalLabel = 'idle';
            //     }

            //     // if (finalLabel == 'vert-tap' || finalLabel == 'horiz-tap') finalLabel = ''
            // }

            console.log(finalLabel);

            // Majority Vote etc
            labelBuffer.push(finalLabel);
            if (labelBuffer.length > bufferSize) labelBuffer.shift();

            // Compute majority
            const counts: Record<string, number> = {};
            for (const l of labelBuffer) counts[l] = (counts[l] || 0) + 1;

            let majorityLabel = finalLabel;
            let maxCount = 0;
            let lastLabelCount = 0;
            for (const l in counts) {
                if (counts[l] > maxCount) {
                    maxCount = counts[l];
                    majorityLabel = l;
                }
                if (l === lastLabel) lastLabelCount = counts[l];
            }

            if (maxCount < 3 && majorityLabel != 'double-tap') majorityLabel = 'idle';

            if (lastLabel === 'circle-cw' || lastLabel == 'circle-ccw') {
                if (lastLabelCount > 0) majorityLabel = lastLabel;
            }

            console.log(majorityLabel, labelBuffer);

            // // Construct result object
            // const results = this.labels.map((l, i) => ({
            //     label: l,
            //     confidence: values[i],
            // }));
            const results = [
                {
                    label: majorityLabel,
                    confidence: 1, // Math.max(...values),
                },
            ];

            lastLabel = majorityLabel;

            // console.log(results);

            // // Inject sub-classification override
            // if (majorityLabel !== predictedLabel) {
            //     // If the majority says "circle-cw" but model said "circle", we verify "circle" is consistent
            //     // Actually this logic combines pure model classes with hybrid sub-classes.
            //     // We should add the hybrid class to the results or replace the main one?
            //     // Let's prepend it as the "Top" result
            //     results.unshift({ label: majorityLabel, confidence: 1.0 });
            // }

            // console.log(majorityLabel);

            // this.classifyHandler({ results, segment: rawSegment });
            return majorityLabel;
        };

        return {
            ntfHandler: (d: AccelDataPoint) => this.classifyHandler(handler(d)),
        };
    }

    public setClassifyHandler(handler: (data: ClassifyHandlerData) => void) {
        this.classifyHandler = handler;
    }

    public getData() {
        return this.datapoints;
    }

    // public async loadModel(files: FileList | string) {
    //     // TF.js loadLayersModel expects a URL or IOHandler.
    //     // From FileList: users usually provide .json and .bin.
    //     // tf.loadLayersModel(tf.io.browserFiles([file1, file2]))

    //     let fileArray: File[] = [];
    //     if (files instanceof FileList) {
    //         for (let i = 0; i < files.length; i++) fileArray.push(files[i]);
    //     }

    //     // Sort files: .json first, .bin second
    //     fileArray.sort((a, b) => {
    //         if (a.name.endsWith('.json') && !b.name.endsWith('.json')) return -1;
    //         if (!a.name.endsWith('.json') && b.name.endsWith('.json')) return 1;
    //         if (a.name.endsWith('.bin') && !b.name.endsWith('.bin')) return 1;
    //         if (!a.name.endsWith('.bin') && b.name.endsWith('.bin')) return -1;
    //         return 0;
    //     });

    //     const modelsArray = fileArray.filter((e) => !e.name.endsWith('labels.json'));

    //     if (fileArray.length > 0) {
    //         try {
    //             this.model = await tf.loadLayersModel(tf.io.browserFiles(modelsArray));
    //             // Try to compile if needed
    //             this.model.compile({
    //                 optimizer: tf.train.adam(1e-3),
    //                 loss: 'categoricalCrossentropy',
    //                 metrics: ['accuracy'],
    //             });

    //             // Try to load labels if provided
    //             // HACK: user needs to select labels.json too.
    //             const labelFile = fileArray.find((f) => f.name.includes('labels.json'));
    //             if (labelFile) {
    //                 const text = await labelFile.text();
    //                 this.labels = JSON.parse(text);
    //                 console.log('Loaded labels:', this.labels);
    //             } else {
    //                 // Default fallback or error
    //                 console.warn('No labels.json found, using default order if compatible');
    //                 this.labels = ['circle', 'double-tap', 'idle']; // hopeful default
    //             }
    //         } catch (e) {
    //             console.error('Failed to load model', e);
    //         }
    //     }
    // }
}
