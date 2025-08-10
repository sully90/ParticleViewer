#version 330 core
in float vDistance;
out vec4 fragColor;

uniform vec3 uColor;

void main() {
  // Base faintness
  float baseAlpha = 0.10;
  // Depth falloff: further lines get fainter
  float falloff = 1.0 / (1.0 + 0.25 * vDistance);
  float alpha = clamp(baseAlpha * falloff, 0.02, 0.18);
  fragColor = vec4(uColor, alpha);
}
