// Stroke widths are rounded to 8 groups (step sizes 1-5): the sketch draws the segments of a
// group as one path, a single canvas call. Drawing each agent's line separately (~2600 per
// frame) halved the frame rate in full screen (1920x1080: 27 instead of 58 frames/s).
var STROKE_GROUPS = 8;

var Agent = function() {
  this.vector = myp5.createVector(myp5.random(myp5.width), myp5.random(myp5.height));
  this.vectorOld = this.vector.copy();
  this.stepSize = myp5.random(1, 5);
  this.group = Math.min(STROKE_GROUPS - 1, Math.floor((this.stepSize - 1) / 4 * STROKE_GROUPS));
  this.angle;
};

// Moves the agent and adds its segment to segments[its group] (x0, y0, x1, y1)
Agent.prototype.update = function(segments) {
  this.vector.x += Math.cos(this.angle) * this.stepSize;
  this.vector.y += Math.sin(this.angle) * this.stepSize;
  if (this.vector.x < 0 || this.vector.x > myp5.width || this.vector.y < 0 || this.vector.y > myp5.height) {
    this.vector.set(myp5.random(myp5.width), myp5.random(myp5.height));
    this.vectorOld.set(this.vector);
  }
  segments[this.group].push(this.vectorOld.x, this.vectorOld.y, this.vector.x, this.vector.y);
  this.vectorOld.set(this.vector);
};

Agent.prototype.update1 = function(noiseScale, noiseStrength, segments, offset) {
  this.angle = myp5.noise(this.vector.x / noiseScale, this.vector.y / noiseScale) * noiseStrength + offset;
  this.update(segments);
};

Agent.prototype.update2 = function(noiseScale, noiseStrength, segments) {
  this.angle = myp5.noise(this.vector.x / noiseScale, this.vector.y / noiseScale) * 24;
  this.angle = (this.angle - Math.floor(this.angle)) * noiseStrength;
  this.update(segments);
};
