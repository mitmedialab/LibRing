// Distance estimation from the RSSI, helped by the accelerometer (experimental).
//
// The RSSI gives the distance through the log-distance path-loss model
//   RSSI = P0 − 10 n log10(d)        (P0: RSSI at 1 m, n: path-loss exponent)
// but one reading is off by several dB (fading, hand and body in the way): averaging
// helps only while the distance doesn't change. The accelerometer says when it does:
// gravity removed, the acceleration is integrated into a speed (reset to 0 whenever the
// ring is still: zero-velocity updates), and the path length of the current move bounds
// how much the distance may have changed. Without a gyroscope or a compass the ring
// can't know the direction of the other end, so the accelerometer can't give the distance
// itself: it only sets the process noise of a Kalman filter on log10(d), a random walk
// that barely moves while the rings are still (long averaging, little noise) and opens up
// during a move (the RSSI is followed right away).
//
// Also measured: the displacement of each move, integrated twice between two still
// moments, with the velocity left at the end removed as a constant drift.
//
// Plain module, no DOM (node can run it, for simulations).

export const FUSION = {
  sampleMs: 90,           // ms between accelerometer samples until measured (rate over
                          // the last periodSamples arrivals: 11.1 / s, 10 / s with the
                          // firmware averaging the FIFO). The time step, not the arrival
                          // times: notifications can stall for ~3-4 s, then the queued
                          // samples (20-40) arrive within a few ms (2026-11-20: a busy
                          // gnome-panel loop on this computer). No sample is lost.
  periodSamples: 300,
  resetGapMs: 8000,       // nothing for that long (disconnected...): start again
  stillAccel: 0.05,       // g: acceleration (gravity removed) below that, and |a| within that
                          // of 1 g: a quiet sample
  moveAccel: 0.1,         // g: a move starts above that only (hysteresis: noise, tremor)
  stillMs: 400,           // quiet that long: still (speed reset to 0, end of a move)
  gravityTauStill: 300,   // ms: gravity estimate follows fast while quiet...
  gravityTauMoving: 3000, // ... and slowly during a move (else it would absorb the motion)
  normTau: 5000,          // ms: average |a| while still, which should be 1 g (scale)
  speedLeakMs: 1500,      // leaky speed integration (bounded drift) for the uncertainty
  motionGain: 1.5,        // speed from the accelerometer × this (rough at 10 samples/s)
  maxMoveMs: 4000,        // longer moves: the double integration has drifted (flagged)
  maxTurn: 15,            // degrees: turned more during a move, same (gravity taken for motion)
  sigmaRssi: 4,           // dB: noise of one RSSI reading
  floorQ: 0.0001,         // decade² / s: what the accelerometer can't see (people moving...). Real
                          // data (2026-11-20): 0.0001 halves the jitter while still (±4 -> ±2 %), same lag
  fastTau: 500,           // ms: RSSI-only estimates, low-pass filtered (as in the charts)...
  slowTau: 1000,          // ... and smoother (3 s was too slow to follow a hand)
  calTau: 3000,           // ms: RSSI averaged for a calibration point (steady)
  changeZ: 1.2,           // innovations beyond that many sigmas, all on the same side...
  changeMs: 600,          // ... for that long (2 readings at least): a move, accelerometer or not
  n: 2,                   // path-loss exponent until 2 calibration distances fit it
  p0Link: -60,            // dBm at 1 m until calibrated: ring -> this computer
  p0Peer: -65,            // ring -> ring
};

const G = 9.81;
const norm = (v) => Math.hypot(v[0], v[1], v[2]);

// First-order low-pass filter for irregularly timed samples: { value, time }
export function lowPass(f, x, now, tau) {
  if (f.value == null || now - f.time > 5 * tau) f.value = x;
  else f.value += (1 - Math.exp(-(now - f.time) / tau)) * (x - f.value);
  f.time = now;
  return f.value;
}

// Motion of one ring, from its accelerometer samples (x, y, z in g, as sent)
export class Motion {
  constructor() { this.reset(); this.gNorm = null; }
  // ms between samples: long-run arrival rate (bursts average out), sampleMs at first
  get period() {
    const t = this.arrivals;
    if (!t || t.length < 30 || t.at(-1) - t[0] < 5000) return FUSION.sampleMs;
    return Math.min(140, Math.max(60, (t.at(-1) - t[0]) / (t.length - 1)));
  }
  reset() {
    this.g = null;            // gravity estimate (raw units)
    this.v = [0, 0, 0];       // leaky speed (m/s): uncertainty
    this.vMove = [0, 0, 0];   // speed of the current move, not leaky (m/s): displacement
    this.p = [0, 0, 0];       // displacement of the current move (m)
    this.L = 0;               // path length of the current move (m, from the leaky speed)
    this.A = this.A || 0;     // accumulated variance (m²), only grows: links read differences
    this.quietSince = null;
    this.prev = null;
    this.moving = false;
    this.moveStart = null;
    this.speed = 0;           // m/s (× motionGain)
    this.lin = 0;             // g, acceleration without gravity
    this.last = null;         // arrival time of the last sample
    this.clock = 0;           // sample time (ms): sampleMs per sample
  }
  get still() { return !this.moving; }
  get scale() { return this.gNorm ? 1 / this.gNorm : 1; } // raw units -> g
  sample(xyz, now) {
    const F = FUSION;
    if (this.last != null && now - this.last > F.resetGapMs) { this.reset(); this.arrivals = []; } // start again
    this.last = now;
    (this.arrivals ??= []).push(now);
    if (this.arrivals.length > F.periodSamples) this.arrivals.shift();
    const period = this.period;
    const dt = period / 1000;
    const clock = this.clock += period; // the time of this sample, not of its arrival
    const a = norm(xyz);
    if (!this.g) { this.g = [...xyz]; if (this.gNorm == null) this.gNorm = a; return; }
    const lin = xyz.map((x, i) => x - this.g[i]);
    this.lin = norm(lin) * this.scale;
    // quiet: 1 g, and steady (from the previous sample: a ring that has just been turned
    // over is quiet too, its gravity estimate catches up then)
    const step = this.prev ? norm(xyz.map((x, i) => x - this.prev[i])) * this.scale : 0;
    this.prev = xyz;
    const quiet = step < F.stillAccel && Math.abs(a * this.scale - 1) < F.stillAccel;
    if (quiet) this.quietSince ??= clock; else this.quietSince = null;
    const k = 1 - Math.exp(-dt * 1000 / (quiet ? F.gravityTauStill : F.gravityTauMoving));
    for (let i = 0; i < 3; i++) this.g[i] += k * (xyz[i] - this.g[i]);
    const still = this.quietSince != null && clock - this.quietSince >= F.stillMs;
    if (still) {
      this.gNorm += (1 - Math.exp(-dt * 1000 / F.normTau)) * (a - this.gNorm);
      if (this.moving) { this.g = [...xyz]; this.endMove(now, clock); } // steady: that is gravity
      return;
    }
    if (!this.moving) {
      if (this.lin < F.moveAccel) return; // not quiet, but too weak to start a move
      this.moving = true; this.moveStart = clock; this.gStart = [...this.g];
    }
    const acc = lin.map((x) => x * this.scale * G); // m/s²
    const leak = Math.exp(-dt * 1000 / F.speedLeakMs);
    for (let i = 0; i < 3; i++) {
      this.vMove[i] += acc[i] * dt;
      this.p[i] += this.vMove[i] * dt;
      this.v[i] = this.v[i] * leak + acc[i] * dt;
    }
    this.speed = norm(this.v) * F.motionGain;
    const dL = this.speed * dt;
    // the variances of the steps of one move add up to (path length)²: the distance can
    // have changed by up to the whole path (straight at the other end), not a random walk
    this.A += (this.L + dL) ** 2 - this.L ** 2;
    this.L += dL;
  }
  endMove(now, clock) {
    const T = (clock - this.moveStart) / 1000;
    // a constant bias leaves the speed at b T at the end (it should be 0), and adds b T²/2
    // to the displacement: removed
    const p = this.p.map((x, i) => x - this.vMove[i] * T / 2);
    // turned during the move: part of gravity was taken for motion (no gyroscope)
    const g0 = this.gStart, g1 = this.g;
    const cos = (g0[0] * g1[0] + g0[1] * g1[1] + g0[2] * g1[2]) / (norm(g0) * norm(g1));
    const turned = Math.acos(Math.min(1, Math.max(-1, cos))) * 180 / Math.PI;
    this.lastMove = { distance: norm(p), raw: norm(this.p), residual: norm(this.vMove), duration: T,
                      path: this.L, time: now, turned,
                      unreliable: T * 1000 > FUSION.maxMoveMs || turned > FUSION.maxTurn };
    this.moves = (this.moves || 0) + 1;
    this.moving = false;
    this.v = [0, 0, 0]; this.vMove = [0, 0, 0]; this.p = [0, 0, 0];
    this.L = 0; this.speed = 0;
  }
}

// Calibrations (RSSI at known distances) per link, kept in this browser
const CAL_KEY = 'multiRingDistanceCal';
function loadCals() { try { return JSON.parse(localStorage.getItem(CAL_KEY)) || {}; } catch { return {}; } }
function saveCal(id, points) {
  try { const all = loadCals(); all[id] = points; localStorage.setItem(CAL_KEY, JSON.stringify(all)); } catch {}
}

// One distance: a ring and this computer, or two rings
export class Link {
  constructor(id, label, peer) {
    this.id = id; this.label = label; this.peer = peer;
    this.points = loadCals()[id] || []; // [{ d, rssi }]
    this.fit();
    this.fast = {}; this.slow = {}; this.cal = {};
    this.s = null; this.P = null; this.t = null; // Kalman filter: log10(d), variance
    this.seen = new Map(); // Motion -> its A at the last update
    this.motions = [];
    this.hist = { fast: [], slow: [], fused: [], moving: [] };
  }
  // P0 (and n with 2 distances or more, a decade apart enough) from the calibration points
  fit() {
    const pts = this.points;
    this.n = FUSION.n;
    this.p0 = this.peer ? FUSION.p0Peer : FUSION.p0Link;
    if (!pts.length) return;
    const xs = pts.map((p) => Math.log10(p.d)), ys = pts.map((p) => p.rssi);
    const mx = xs.reduce((a, b) => a + b) / xs.length, my = ys.reduce((a, b) => a + b) / ys.length;
    const sxx = xs.reduce((a, x) => a + (x - mx) ** 2, 0);
    if (pts.length >= 2 && Math.max(...xs) - Math.min(...xs) > 0.15) { // > ×1.4 apart
      const sxy = xs.reduce((a, x, i) => a + (x - mx) * (ys[i] - my), 0);
      this.n = Math.min(6, Math.max(1, -sxy / sxx / 10));
    }
    this.p0 = my + 10 * this.n * mx;
  }
  get calibrated() { return this.points.length > 0; }
  dist(rssi) { return rssi == null ? null : 10 ** ((this.p0 - rssi) / (10 * this.n)); }
  get dFast() { return this.dist(this.fast.value); }
  get dSlow() { return this.dist(this.slow.value); }
  get dFused() { return this.s == null ? null : 10 ** this.s; }
  get spread() { return this.P == null ? null : 10 ** Math.sqrt(this.P); } // ×/÷, 1 σ
  get moving() { return this.motions.some((m) => m.moving); }
  // One RSSI reading; motions: the Motion of each end with an accelerometer.
  // weight: > 1 for an average of several readings (ring-to-ring reports)
  measure(rssi, now, motions, weight = 1) {
    this.motions = motions;
    lowPass(this.fast, rssi, now, FUSION.fastTau);
    lowPass(this.slow, rssi, now, FUSION.slowTau);
    lowPass(this.cal, rssi, now, FUSION.calTau);
    this.rssiTime = now;
    const H = -10 * this.n;
    const R = FUSION.sigmaRssi ** 2 * (0.5 + 0.5 / weight); // fading: averages help little
    if (this.s == null || now - this.t > 10000) { // start (again)
      this.s = (this.p0 - rssi) / (10 * this.n); this.P = R / H ** 2;
    } else {
      const d = 10 ** this.s;
      let qd = 0; // m², from the motion since the last update
      for (const m of motions) if (this.seen.has(m)) qd += Math.max(0, m.A - this.seen.get(m));
      this.P += qd / (d * Math.LN10) ** 2 + FUSION.floorQ * (now - this.t) / 1000;
      this.P = Math.min(this.P, 1);
      const nu = rssi - (this.p0 + H * this.s);
      let S = H * H * this.P + R;
      let z = Math.abs(nu) / Math.sqrt(S);
      // a move the accelerometer missed (smooth, < moveAccel): the RSSI keeps disagreeing
      // on the same side for changeMs -> as after a move, the filter forgets (without
      // it, those readings look like outliers and the estimate creeps for seconds)
      // (averaged readings only: ring-to-ring reports, groups; single readings of the
      // computer link fade for seconds, which would look like moves)
      const side = weight > 1 && z > FUSION.changeZ ? Math.sign(nu) : 0;
      if (!side || side !== this.changeSide) { this.changeSide = side; this.changeSince = now; this.changeCount = 0; }
      if (side && ++this.changeCount >= 2 && now - this.changeSince >= FUSION.changeMs) {
        this.P = Math.min(1, this.P + (nu / H) ** 2);
        S = H * H * this.P + R; z = Math.abs(nu) / Math.sqrt(S);
        this.changeSide = 0;
      }
      const r = z > 2.5 ? R * (z / 2.5) ** 2 : R; // outlier: weighs less (not dropped)
      S = H * H * this.P + r;
      const K = this.P * H / S;
      this.s = Math.min(1.7, Math.max(-1.3, this.s + K * nu)); // 5 cm .. 50 m
      this.P *= 1 - K * H;
    }
    this.t = now;
    for (const m of motions) this.seen.set(m, m.A);
  }
  // Calibration point at the true distance d: the smooth RSSI (hold still first)
  calibrate(d) {
    if (this.cal.value == null) return false;
    this.points.push({ d, rssi: +this.cal.value.toFixed(1) });
    saveCal(this.id, this.points);
    this.fit(); this.s = null;
    return true;
  }
  clearCalibration() { this.points = []; saveCal(this.id, []); this.fit(); this.s = null; }
}
