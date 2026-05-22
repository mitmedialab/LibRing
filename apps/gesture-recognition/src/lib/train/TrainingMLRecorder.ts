import { preprocessWindow, prepareTrainingData, resampleAccelData } from '../shared/ml/preprocessing';
import { AccelDataPoint } from '../shared/plot';
import { AccelNtfHandler } from '../shared/accelerometer/ble';
import { separateGravityAndLinearAccel } from '../shared/ml/filters';
import { CircleDetector } from '../shared/detection/circleDetection';
import { TapDetector } from '../shared/detection/tapDetection';
import * as tf from '@tensorflow/tfjs';

export default class DataRecorder {
    // RAW accel data (needs to be processed)
    private datapoints: { data: AccelDataPoint[]; label: string }[] = [];
    private currentLabel: string = '';
    private model: any; // tf.Sequential
    private labels: string[] = []; // Discovered labels

    public constructor(private resampleSize: number = 20) {
        this.initModel();
    }

    private initModel() {
        const model = tf.sequential();

        // Input: [batch, time, channels]
        // Time = resampleSize, Channels = 6
        model.add(
            tf.layers.conv1d({
                inputShape: [this.resampleSize, 6],
                filters: 32,
                kernelSize: 5,
                strides: 1,
                activation: 'relu',
                padding: 'same',
            }),
        );
        model.add(tf.layers.batchNormalization());
        model.add(tf.layers.maxPooling1d({ poolSize: 2 }));

        model.add(
            tf.layers.conv1d({
                filters: 64,
                kernelSize: 5,
                activation: 'relu',
                padding: 'same',
            }),
        );
        model.add(tf.layers.batchNormalization());
        model.add(tf.layers.maxPooling1d({ poolSize: 2 }));

        model.add(
            tf.layers.conv1d({
                filters: 128,
                kernelSize: 3,
                activation: 'relu',
                padding: 'same',
            }),
        );
        model.add(tf.layers.globalAveragePooling1d());

        model.add(tf.layers.dense({ units: 64, activation: 'relu' }));
        model.add(tf.layers.dropout({ rate: 0.3 }));
        // Output layer units will be dynamic based on classes?
        // For now, let's assume we rebuild/recompile head on train or just use 3 fixed classes if known?
        // "Classify each window into: circle, double-tap, idle" -> 3 classes.
        // But user might train with different labels.
        // We will finalize the model structure in `train` when we know numClasses.
        // But for init, we can put a placeholder or just wait.
        this.model = model;
    }

    public async train() {
        // 1. Prepare Data
        // Use 'ring-order' (augmentation) as base, but we rely on preprocessWindow for full pipeline
        const preppedData = prepareTrainingData(this.datapoints, this.resampleSize, 'ring-order');

        if (preppedData.length === 0) {
            console.warn('No data to train on');
            return;
        }

        // 2. Extract Labels
        const uniqueLabels = Array.from(new Set(preppedData.map((d) => d.label))).sort();
        this.labels = uniqueLabels;
        const numClasses = uniqueLabels.length;

        console.log(`Training on ${preppedData.length} samples, classes: ${uniqueLabels.join(', ')}`);

        // 3. Rebuild Model Head (last dense layer) to match numClasses
        // Ideally we re-init the whole model to reset weights
        this.initModel();
        // Remove the generic head if any?
        // Actually initModel didn't add the last dense layer logic to be dynamic logic.
        // Let's just add the final dense layer now.
        this.model.add(tf.layers.dense({ units: numClasses, activation: 'softmax' }));

        this.model.compile({
            optimizer: tf.train.adam(1e-3),
            loss: 'categoricalCrossentropy',
            metrics: ['accuracy'],
        });

        // 4. Convert to Tensors
        const xsArray = preppedData.map((d) => d.data); // Flat arrays
        const ysArray = preppedData.map((d) => uniqueLabels.indexOf(d.label));

        const xs = tf
            .tensor2d(xsArray, [xsArray.length, this.resampleSize * 6])
            .reshape([xsArray.length, this.resampleSize, 6]);
        const ys = tf.oneHot(tf.tensor1d(ysArray, 'int32'), numClasses);

        // 5. Train
        await this.model.fit(xs, ys, {
            epochs: 50,
            batchSize: 16,
            shuffle: true,
            callbacks: {
                onEpochEnd: (epoch: number, logs: any) => {
                    console.log(`Epoch ${epoch}: loss=${logs.loss.toFixed(4)}, acc=${logs.acc.toFixed(4)}`);
                },
            },
        });

        console.log('Training finished');

        // Cleanup
        xs.dispose();
        ys.dispose();
    }

    public addPoint(data: AccelDataPoint[]) {
        this.datapoints.push({ data, label: this.currentLabel });
    }

    public setCurrentLabel(label: string) {
        this.currentLabel = label;
    }

    public getMLRecorderNtfHandler() {
        let recording = false;
        const accumData: AccelDataPoint[] = [];

        const startRecording = () => {
            recording = true;
            accumData.length = 0;
        };

        const stopRecordingAndTrain = () => {
            recording = false;
            if (accumData.length == 0) return;
            console.log(`Recorded ${accumData.length} points for training`);
            this.addPoint(accumData.slice());
            accumData.length = 0;
        };

        const stopRecordingAndClassify = async () => {
            recording = false;
            if (accumData.length == 0) return null;

            // Preprocess
            // Note: resampleSize should match training
            const flatFeatures = preprocessWindow(accumData, this.resampleSize);
            const tensor = tf.tensor2d([flatFeatures], [1, this.resampleSize * 6]).reshape([1, this.resampleSize, 6]);

            const prediction = this.model.predict(tensor) as any;
            const values = await prediction.data();
            const indices = await prediction.argMax(1).data();

            // Map back to labels
            const result = Array.from(values)
                .map((v: any, i: number) => ({
                    label: this.labels[i] || `class_${i}`,
                    confidence: v,
                }))
                .sort((a: any, b: any) => b.confidence - a.confidence);

            accumData.length = 0;
            tensor.dispose();
            prediction.dispose();

            return result;
        };

        const stopRecordingAndClassifyWith = (type: 'circle' | 'tap') => {
            recording = false; // Just stop for now, logic handled in caller/other tests?
            // The prompt says "A lot of this is already implemented... do not try to re-implement"
            // But I should ensure the explicit "Stop Circle" buttons still work if they use detector directly.
            // Using existing logic:
            if (accumData.length == 0) return;

            // const linear = separateGravityAndLinearAccel(accumData, 0.8).linear;
            // const resampled = resampleAccelData(linear, this.resampleSize);

            if (type == 'circle') {
                const result = CircleDetector.analyze(accumData);
                console.log('Circle Detection:', result);
            } else if (type == 'tap') {
                const result = TapDetector.analyze(accumData);
                console.log('Tap Detection:', result);
            }
            accumData.length = 0;
        };

        const handler: AccelNtfHandler = (d) => {
            if (!recording) return;
            accumData.push(d);
        };

        return {
            start: startRecording,
            stopAndTrain: stopRecordingAndTrain,
            stopAndClassify: stopRecordingAndClassify,
            stopAndClassifyWith: stopRecordingAndClassifyWith,
            ntfHandler: handler,
        };
    }

    public getData() {
        return this.datapoints;
    }

    public async saveModel() {
        if (!this.model) return;
        // Save model to downloads
        // TF.js save returns a Result
        await this.model.save('downloads://my-gesture-model');
        // Also save labels?
        // Localstorage or download as JSON?
        // For simplicity, log labels to console or assume fixed.
        // Better: create a Blob for labels and download.
        const blob = new Blob([JSON.stringify(this.labels)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'my-gesture-model-labels.json';
        a.click();
    }

    public async loadModel(files: FileList | string) {
        // TF.js loadLayersModel expects a URL or IOHandler.
        // From FileList: users usually provide .json and .bin.
        // tf.loadLayersModel(tf.io.browserFiles([file1, file2]))

        let fileArray: File[] = [];
        if (files instanceof FileList) {
            for (let i = 0; i < files.length; i++) fileArray.push(files[i]);
        }

        // Sort files: .json first, .bin second
        fileArray.sort((a, b) => {
            if (a.name.endsWith('.json') && !b.name.endsWith('.json')) return -1;
            if (!a.name.endsWith('.json') && b.name.endsWith('.json')) return 1;
            if (a.name.endsWith('.bin') && !b.name.endsWith('.bin')) return 1;
            if (!a.name.endsWith('.bin') && b.name.endsWith('.bin')) return -1;
            return 0;
        });

        const modelsArray = fileArray.filter((e) => !e.name.endsWith('labels.json'));

        console.log(fileArray);

        if (fileArray.length > 0) {
            try {
                this.model = await tf.loadLayersModel(tf.io.browserFiles(modelsArray));
                // Try to compile if needed
                this.model.compile({
                    optimizer: tf.train.adam(1e-3),
                    loss: 'categoricalCrossentropy',
                    metrics: ['accuracy'],
                });

                // Try to load labels if provided
                // HACK: user needs to select labels.json too.
                const labelFile = fileArray.find((f) => f.name.includes('labels.json'));
                if (labelFile) {
                    const text = await labelFile.text();
                    this.labels = JSON.parse(text);
                    console.log('Loaded labels:', this.labels);
                } else {
                    // Default fallback or error
                    console.warn('No labels.json found, using default order if compatible');
                    this.labels = ['circle', 'double-tap', 'idle']; // hopeful default
                }
            } catch (e) {
                console.error('Failed to load model', e);
            }
        }
    }
}
