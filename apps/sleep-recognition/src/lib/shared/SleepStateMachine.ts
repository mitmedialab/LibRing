/**
 * Sleep Stage State Machine
 * Tracks sleep stages based on accelerometer movement patterns
 * Uses low-pass filtering and gravity separation for accurate linear acceleration detection
 */

export type SleepState = 'active' | 'resting' | 'deep-sleep';

export interface SleepStateConfig {
    // Time without movement to transition from active to resting (ms)
    activeToRestingThresholdMs: number;

    // Acceleration magnitude threshold to detect movement (G)
    movementThresholdG: number;

    // Time of substantial movement to transition from resting to active (ms)
    restingToActiveThresholdMs: number;

    // Time of minimal movement to transition from resting to deep sleep (ms)
    restingToDeepSleepThresholdMs: number;

    // Time of substantial movement to transition from deep sleep to resting (ms)
    deepSleepToRestingThresholdMs: number;

    // Acceleration magnitude threshold for "substantial" movement (G)
    substantialMovementThresholdG: number;

    // LPF alpha coefficient (0-1, higher = more responsive, lower = more smoothing)
    lpfAlpha: number;

    // Gravity separation alpha coefficient (0-1, higher = faster adaptation)
    gravityAlpha: number;

    // Size of sliding window for gravity estimation (samples)
    gravityWindowSize: number;
}

const DEFAULT_CONFIG: SleepStateConfig = {
    activeToRestingThresholdMs: 5000, // 5 seconds no movement
    movementThresholdG: 0.1, // 100 mG
    restingToActiveThresholdMs: 3000, // 3 seconds substantial movement
    restingToDeepSleepThresholdMs: 30000, // 30 seconds minimal movement
    deepSleepToRestingThresholdMs: 5000, // 5 seconds substantial movement
    substantialMovementThresholdG: 0.5, // 500 mG
    lpfAlpha: 0.2, // LPF smoothing (lower = more smoothing)
    gravityAlpha: 0.95, // Gravity estimation (high pass filter)
    gravityWindowSize: 10, // Number of samples to use for gravity estimation
};

export class SleepStateMachine {
    private currentState: SleepState = 'active';
    private config: SleepStateConfig;
    private lastMovementTime: number = Date.now();
    private stateChangeTime: number = Date.now();
    private movementAccumulator: number = 0;
    private sampleCount: number = 0;
    private stateChangeCallbacks: Array<(newState: SleepState, oldState: SleepState) => void> = [];

    // LPF and gravity separation
    private filteredLinearMagnitude: number = 0;
    private gravityX: number = 0;
    private gravityY: number = 0;
    private gravityZ: number = 0;
    private accelHistory: Array<{ x: number; y: number; z: number }> = [];

    constructor(config: Partial<SleepStateConfig> = {}) {
        this.config = { ...DEFAULT_CONFIG, ...config };
    }

    /**
     * Calculate magnitude of acceleration vector
     */
    private calculateAccelMagnitude(x: number, y: number, z: number): number {
        return Math.sqrt(x * x + y * y + z * z);
    }

    /**
     * Estimate and subtract gravity component from acceleration
     * Uses complementary filter approach (high-pass filter)
     */
    private separateGravityFromLinear(
        x: number,
        y: number,
        z: number,
    ): { linearX: number; linearY: number; linearZ: number } {
        const alpha = this.config.gravityAlpha;

        // Update gravity estimate (low-pass filter on raw acceleration)
        this.gravityX = alpha * this.gravityX + (1 - alpha) * x;
        this.gravityY = alpha * this.gravityY + (1 - alpha) * y;
        this.gravityZ = alpha * this.gravityZ + (1 - alpha) * z;

        // Linear acceleration is raw acceleration minus estimated gravity
        const linearX = x - this.gravityX;
        const linearY = y - this.gravityY;
        const linearZ = z - this.gravityZ;

        return { linearX, linearY, linearZ };
    }

    /**
     * Apply low-pass filter to magnitude
     */
    private applyLPF(rawMagnitude: number): number {
        const alpha = this.config.lpfAlpha;
        this.filteredLinearMagnitude = alpha * rawMagnitude + (1 - alpha) * this.filteredLinearMagnitude;
        return this.filteredLinearMagnitude;
    }

    /**
     * Process new accelerometer reading and update state machine
     */
    public processReading(x: number, y: number, z: number): SleepState {
        const now = Date.now();
        const timeSinceStateChange = now - this.stateChangeTime;
        const timeSinceLastMovement = now - this.lastMovementTime;

        // Separate gravity from linear acceleration
        const { linearX, linearY, linearZ } = this.separateGravityFromLinear(x, y, z);

        // Calculate linear acceleration magnitude
        const linearMagnitude = this.calculateAccelMagnitude(linearX, linearY, linearZ);

        // // Apply low-pass filter
        // const filteredMagnitude = this.applyLPF(linearMagnitude);

        // console.log(
        //     `Raw: ${linearMagnitude.toFixed(3)}G, Filtered: ${filteredMagnitude.toFixed(3)}G, Gravity: [${this.gravityX.toFixed(2)}, ${this.gravityY.toFixed(2)}, ${this.gravityZ.toFixed(2)}]`,
        // );

        console.log(linearMagnitude);

        // Accumulate movement data for averaging
        this.movementAccumulator += linearMagnitude;
        this.sampleCount++;

        // Update last movement time if filtered movement detected
        if (linearMagnitude > this.config.movementThresholdG) {
            this.lastMovementTime = now;
            console.log('movement');
        }

        // State machine logic
        switch (this.currentState) {
            case 'active':
                // Transition from active to resting: no movement for threshold
                if (timeSinceLastMovement > this.config.activeToRestingThresholdMs) {
                    this.setNewState('resting', now);
                }
                break;

            case 'resting':
                // Transition from resting to active: substantial movement for threshold
                const avgMovement = this.movementAccumulator / Math.max(1, this.sampleCount);
                if (
                    avgMovement > this.config.substantialMovementThresholdG &&
                    timeSinceStateChange > this.config.restingToActiveThresholdMs
                ) {
                    this.resetMovementAccumulator();
                    this.setNewState('active', now);
                }
                // Transition from resting to deep sleep: minimal movement for longer threshold
                else if (
                    timeSinceLastMovement > this.config.restingToDeepSleepThresholdMs &&
                    avgMovement < this.config.movementThresholdG * 2
                ) {
                    this.resetMovementAccumulator();
                    this.setNewState('deep-sleep', now);
                }
                break;

            case 'deep-sleep':
                // Transition from deep sleep to resting: substantial movement for threshold
                const avgMovementDeep = this.movementAccumulator / Math.max(1, this.sampleCount);
                if (
                    avgMovementDeep > this.config.substantialMovementThresholdG &&
                    timeSinceStateChange > this.config.deepSleepToRestingThresholdMs
                ) {
                    this.resetMovementAccumulator();
                    this.setNewState('resting', now);
                }
                break;
        }

        return this.currentState;
    }

    /**
     * Get current sleep state
     */
    public getState(): SleepState {
        return this.currentState;
    }

    /**
     * Get time in current state (ms)
     */
    public getTimeInCurrentState(): number {
        return Date.now() - this.stateChangeTime;
    }

    /**
     * Get time since last movement (ms)
     */
    public getTimeSinceLastMovement(): number {
        return Date.now() - this.lastMovementTime;
    }

    /**
     * Register callback for state changes
     */
    public onStateChange(callback: (newState: SleepState, oldState: SleepState) => void): void {
        this.stateChangeCallbacks.push(callback);
    }

    /**
     * Reset the state machine to active state
     */
    public reset(): void {
        this.currentState = 'active';
        this.lastMovementTime = Date.now();
        this.stateChangeTime = Date.now();
        this.resetMovementAccumulator();
        // Reset filters
        this.filteredLinearMagnitude = 0;
        this.gravityX = 0;
        this.gravityY = 0;
        this.gravityZ = 0;
        this.accelHistory = [];
    }

    /**
     * Set configuration
     */
    public setConfig(config: Partial<SleepStateConfig>): void {
        this.config = { ...this.config, ...config };
    }

    /**
     * Get current configuration
     */
    public getConfig(): SleepStateConfig {
        return { ...this.config };
    }

    private setNewState(newState: SleepState, now: number): void {
        if (newState !== this.currentState) {
            const oldState = this.currentState;
            this.currentState = newState;
            this.stateChangeTime = now;
            this.resetMovementAccumulator();

            // Trigger callbacks
            this.stateChangeCallbacks.forEach((cb) => cb(newState, oldState));
        }
    }

    private resetMovementAccumulator(): void {
        this.movementAccumulator = 0;
        this.sampleCount = 0;
    }
}
