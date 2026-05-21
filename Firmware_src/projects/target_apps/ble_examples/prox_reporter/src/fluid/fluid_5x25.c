#include "fluid_5x25.h"

#include <string.h>

static float clamp01(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static uint32_t mass_sum_u8(const uint8_t d[FLUID_5X25_W][FLUID_5X25_H])
{
    uint32_t s = 0;
    for (int x = 0; x < FLUID_5X25_W; x++) {
        for (int y = 0; y < FLUID_5X25_H; y++) {
            s += (uint32_t)d[x][y];
        }
    }
    return s;
}

void fluid_5x25_init(fluid_5x25_t *f)
{
    if (!f) return;
    memset(f, 0, sizeof(*f));

    // Seed a small blob roughly centered.
    const int cx = 2;
    const int cy = 12;
    f->d[cx][cy] = 220;
    f->d[cx][cy - 1] = 120;
    f->d[cx][cy + 1] = 120;
    f->d[cx - 1][cy] = 100;
    f->d[cx + 1][cy] = 100;

    f->mass0 = mass_sum_u8(f->d);
    if (f->mass0 == 0) {
        f->mass0 = 1;
    }
    f->initialized = 1;
}

uint8_t fluid_5x25_get(const fluid_5x25_t *f, int x, int y)
{
    if (!f) return 0;
    if (x < 0 || x >= FLUID_5X25_W || y < 0 || y >= FLUID_5X25_H) {
        return 0;
    }
    return f->d[x][y];
}

void fluid_5x25_sample(const fluid_5x25_t *f,
                       const uint8_t *xs,
                       const uint8_t *ys,
                       uint8_t n,
                       uint8_t *out)
{
    if (!f || !xs || !ys || !out) return;
    for (uint8_t i = 0; i < n; i++) {
        out[i] = fluid_5x25_get(f, (int)xs[i], (int)ys[i]);
    }
}

void fluid_5x25_to_segments(const fluid_5x25_t *f, uint8_t LED_segments[5])
{
    if (!LED_segments) return;

    // Existing 7-seg encoding in this firmware:
    // bit0=A, bit1=B, bit2=C, bit3=D, bit4=E, bit5=F, bit6=G
    const uint8_t SEG_A = (1u << 0); // top
    const uint8_t SEG_B = (1u << 1); // top left
    const uint8_t SEG_C = (1u << 2); // top right
    const uint8_t SEG_D = (1u << 3); // middle
    const uint8_t SEG_E = (1u << 4); // bottom left
    const uint8_t SEG_F = (1u << 5); // bottom right
    const uint8_t SEG_G = (1u << 6); // bottom

    const uint8_t FLUID_THRESHOLD = 4; 

    for (int digit = 0; digit < 5; digit++) {
        const int xoff = digit * 5;
        uint8_t seg = 0;

        // User sampling coordinates are defined on a logical 25x5 layout.
        // fluid_5x25_t is stored as 5x25, so read them transposed.
        if (fluid_5x25_get(f, 0, xoff + 2) > FLUID_THRESHOLD) seg |= SEG_G; // (2,0)
        if (fluid_5x25_get(f, 1, xoff + 0) > FLUID_THRESHOLD) seg |= SEG_F; // (0,1)
        if (fluid_5x25_get(f, 3, xoff + 0) > FLUID_THRESHOLD) seg |= SEG_C; // (0,3)
        if (fluid_5x25_get(f, 4, xoff + 2) > FLUID_THRESHOLD) seg |= SEG_A; // (2,5)
        if (fluid_5x25_get(f, 2, xoff + 2) > FLUID_THRESHOLD) seg |= SEG_D; // (2,2)
        if (fluid_5x25_get(f, 1, xoff + 4) > FLUID_THRESHOLD) seg |= SEG_E; // (4,1)
        if (fluid_5x25_get(f, 3, xoff + 4) > FLUID_THRESHOLD) seg |= SEG_B; // (4,3)

        LED_segments[digit] = seg;
    }
}

void fluid_5x25_step(fluid_5x25_t *f, float left_0_1, float down_0_1)
{
    if (!f) return;
    if (!f->initialized) {
        fluid_5x25_init(f);
    }

    const float l = clamp01(left_0_1);
    const float d = clamp01(down_0_1);

    // Convert to signed drive in [-1, +1]
    const float fx = (l * 2.0f) - 1.0f;
    const float fy = (d * 2.0f) - 1.0f;

    // Move a small fraction each step. Keep <= 0.5 to avoid needing multi-hop.
    const float base = 0.45f; // overall "slosh"
    const float ax = (fx < 0.0f) ? -fx : fx;
    const float ay = (fy < 0.0f) ? -fy : fy;
    float mx = base * ax;
    float my = base * ay;
    if (mx > 0.45f) mx = 0.45f;
    if (my > 0.45f) my = 0.45f;

    // Integer transport: compute outgoing amounts to up to 2 neighbors (x and y),
    // then keep the remainder. If a move would go out of bounds, it is kept.
    uint8_t next[FLUID_5X25_W][FLUID_5X25_H];
    memset(next, 0, sizeof(next));

    for (int x = 0; x < FLUID_5X25_W; x++) {
        for (int y = 0; y < FLUID_5X25_H; y++) {
            const uint8_t m = f->d[x][y];
            if (m == 0) continue;

            // Compute desired outflow (integer) along x and y.
            uint16_t outx = (uint16_t)((float)m * mx);
            uint16_t outy = (uint16_t)((float)m * my);
            if (outx + outy > m) {
                // scale down proportionally
                uint16_t tot = outx + outy;
                if (tot != 0) {
                    outx = (uint16_t)((uint32_t)outx * m / tot);
                    outy = (uint16_t)m - outx;
                } else {
                    outx = 0;
                    outy = 0;
                }
            }

            uint16_t keep = (uint16_t)m - outx - outy;

            // X move
            int nx = x;
            if (outx) {
                if (fx > 0.0f) nx = x + 1;
                else if (fx < 0.0f) nx = x - 1;
            }
            if (nx < 0 || nx >= FLUID_5X25_W) {
                keep += outx;
                outx = 0;
            }

            // Y move
            int ny = y;
            if (outy) {
                if (fy > 0.0f) ny = y + 1;
                else if (fy < 0.0f) ny = y - 1;
            }
            if (ny < 0 || ny >= FLUID_5X25_H) {
                keep += outy;
                outy = 0;
            }

            // Accumulate with saturation-safe 16-bit temp
            uint16_t v = next[x][y];
            v += keep;
            next[x][y] = (v > 255u) ? 255u : (uint8_t)v;

            if (outx) {
                uint16_t vx = next[nx][y];
                vx += outx;
                next[nx][y] = (vx > 255u) ? 255u : (uint8_t)vx;
            }
            if (outy) {
                uint16_t vy = next[x][ny];
                vy += outy;
                next[x][ny] = (vy > 255u) ? 255u : (uint8_t)vy;
            }
        }
    }

    // Simple relaxation to reduce jaggedness: pull 1/8 toward neighbor average.
    uint8_t sm[FLUID_5X25_W][FLUID_5X25_H];
    for (int x = 0; x < FLUID_5X25_W; x++) {
        for (int y = 0; y < FLUID_5X25_H; y++) {
            uint16_t a = next[x][y];
            uint16_t n = 1;
            if (x > 0) { a += next[x - 1][y]; n++; }
            if (x + 1 < FLUID_5X25_W) { a += next[x + 1][y]; n++; }
            if (y > 0) { a += next[x][y - 1]; n++; }
            if (y + 1 < FLUID_5X25_H) { a += next[x][y + 1]; n++; }
            uint16_t avg = (uint16_t)(a / n);
            uint16_t cur = next[x][y];
            // cur = cur*7/8 + avg*1/8
            uint16_t blended = (uint16_t)((cur * 7u + avg) / 8u);
            sm[x][y] = (blended > 255u) ? 255u : (uint8_t)blended;
        }
    }

    memcpy(f->d, sm, sizeof(f->d));

    // Conserve total mass: scale densities so sum == mass0.
    uint32_t s = mass_sum_u8(f->d);
    if (s == 0) {
        // Reset if completely lost (shouldn't happen with bounded moves).
        fluid_5x25_init(f);
        return;
    }
    if (s != f->mass0) {
        // Fixed-point scale to avoid floats
        // scaleQ16 = mass0/s in Q16.
        uint32_t scaleQ16 = (uint32_t)((f->mass0 << 16) / s);
        uint32_t acc = 0;
        for (int x = 0; x < FLUID_5X25_W; x++) {
            for (int y = 0; y < FLUID_5X25_H; y++) {
                uint32_t v = (uint32_t)f->d[x][y];
                uint32_t sv = (v * scaleQ16) >> 16;
                if (sv > 255u) sv = 255u;
                f->d[x][y] = (uint8_t)sv;
                acc += sv;
            }
        }
        // Small remainder correction: nudge the max cell up/down to match exactly.
        if (acc != f->mass0) {
            int mx = 0, my2 = 0;
            uint8_t best = 0;
            for (int x = 0; x < FLUID_5X25_W; x++) {
                for (int y = 0; y < FLUID_5X25_H; y++) {
                    if (f->d[x][y] >= best) {
                        best = f->d[x][y];
                        mx = x;
                        my2 = y;
                    }
                }
            }
            if (acc < f->mass0 && f->d[mx][my2] < 255u) {
                f->d[mx][my2] += 1;
            } else if (acc > f->mass0 && f->d[mx][my2] > 0u) {
                f->d[mx][my2] -= 1;
            }
        }
    }
}

