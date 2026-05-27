// ml5-global.d.ts
declare namespace ml5 {
    function neuralNetwork(options: NeuralNetworkOptions): NeuralNetwork;

    function setBackend(option: string): void;

    interface NeuralNetworkOptions {
        inputs?: number | string[];
        outputs?: number | string[];
        task?: 'classification' | 'regression';
        debug?: boolean;
        layers?: any[];
    }

    interface NeuralNetwork {
        addData(inputs: any[], targets: any[]): void;
        normalizeData(): void;
        train(options: TrainingOptions, callback?: () => void): void;
        train(callback?: () => void): void;
        classify(inputs: any[], callback: (error: any, results: any[]) => void): void;
        predict(inputs: any[], callback: (error: any, results: any[]) => void): void;
        save(name?: string): void;
        load(files: string | any, callback?: () => void): void;
    }

    interface TrainingOptions {
        epochs?: number;
        batchSize?: number;
    }
}
