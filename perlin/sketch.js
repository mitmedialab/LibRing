// TODO: check history:
// https://github.com/generative-design/Code-Package-p5.js/commits/master/02_M/M_1_5_02/sketch.js
//
// M_1_5_02
//
// Generative Gestaltung – Creative Coding im Web
// ISBN: 978-3-87439-902-9, First Edition, Hermann Schmidt, Mainz, 2018
// Benedikt Groß, Hartmut Bohnacker, Julia Laub, Claudius Lazzeroni
// with contributions by Joey Lee and Niels Poldervaart
// Copyright 2018
//
// http://www.generative-gestaltung.de
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * noise values (noise 2d) are used to animate a bunch of agents.
 *
 * KEYS
 * 1-2                 : switch noise mode
 * space               : new noise seed
 * backspace           : clear screen
 * s                   : save png
 */

'use strict';

var sketch = function (p) {
    var agents = [];
    var agentCount = 3000;
    var noiseScale = 150;
    var noiseStrength = 6;
    var overlayAlpha = 10;
    var agentAlpha = 90;
    var strokeWidth = 0.3;
    var drawMode = 1;
    var isAgentBlack = 0;

    // BLE accelerometer data (updated via notifications)
    var accel = { x: 0, y: 0, z: 0, timestamp: '' };
    var isBleConnected = false;

    p.setup = function () {
        p.fullscreen(true);

        p.createCanvas(2000, 1200);

        for (var i = 0; i < agentCount; i++) {
            agents[i] = new Agent();
        }

        // Only fetch accelerometer data here — do not change agent update behavior.
        if (window.BLE) {
            window.BLE.onAccel(function (data) {
                accel = data;
                // Uncomment for debugging:
                // console.log('accel', accel);
            });
        } else {
            console.warn('BLE helper not loaded. Did ble.js fail to load?');
        }
    };

    p.draw = function () {
        p.fill(255 * isAgentBlack, overlayAlpha);
        p.noStroke();
        p.rect(0, 0, p.width, p.height);

        // Draw agents
        p.stroke(255 * (1 - isAgentBlack), agentAlpha);

        // Compute angle from -Z axis (radians).
        // Treat the accel vector as a direction; angle is 0 when aligned with -Z.
        // Clamp to avoid NaNs if accel data is momentarily zero.
        var len = Math.sqrt(accel.x * accel.x + accel.y * accel.y + accel.z * accel.z);
        var cosTheta = 1; // default: aligned
        if (len > 1e-9) {
            cosTheta = -accel.z / len;
            cosTheta = Math.max(-1, Math.min(1, cosTheta));
        }
        // let angle = Math.asin(Math.max(-1, Math.min(1, accel.z)));
        // if (accel.x < 0) {
        //     angle *= -1;
        //     angle += Math.PI;
        // }
        let angle = Math.atan2(accel.x, accel.z);
        // Math.acos(cosTheta) +

        // angle *= p.noise(p.frameCount / 300); // add a touch of randomness (kept from original)

        for (var i = 0; i < agentCount; i++) {
            if (drawMode == 1) {
                agents[i].update1(noiseScale, noiseStrength, strokeWidth, angle);
            } else agents[i].update2(noiseScale, noiseStrength, strokeWidth);
        }

        // Tiny on-canvas status (optional)
        p.noStroke();
        p.fill(0, 140);
        p.rect(20, 20, 420, 62);
        p.fill(255);
        p.textSize(14);
        p.text(isBleConnected ? 'BLE connected' : 'Click to connect BLE', 30, 45);
        p.text(
            'x:' +
                accel.x.toFixed(3) +
                ' y:' +
                accel.y.toFixed(3) +
                ' z:' +
                accel.z.toFixed(3) +
                ' angle: ' +
                angle.toFixed(3),
            30,
            65,
        );
    };

    // Web Bluetooth requires a user gesture.
    p.mousePressed = async function () {
        if (!window.BLE) return;
        if (isBleConnected) return;
        try {
            await window.BLE.connectToBLE();
            isBleConnected = true;
        } catch (e) {
            isBleConnected = false;
            console.error(e);
        }
    };

    p.keyReleased = function () {
        if (p.key == 's' || p.key == 'S') p.saveCanvas(gd.timestamp(), 'png');
        if (p.key == '1') drawMode = 1;
        if (p.key == '2') drawMode = 2;
        if (p.key == ' ') {
            var newNoiseSeed = p.floor(p.random(10000));
            p.noiseSeed(newNoiseSeed);
        }
        if (p.keyCode == p.DELETE || p.keyCode == p.BACKSPACE) {
            isAgentBlack ^= 1; // toggle
        }
    };
};

var myp5 = new p5(sketch);
