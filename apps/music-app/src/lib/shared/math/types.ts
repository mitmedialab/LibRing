export interface Point3D {
    x: number;
    y: number;
    z: number;
}

export interface PipelineInput extends Point3D {
    timestamp: string;
}
