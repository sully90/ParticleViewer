#version 330 core
in float vDistance;
in float vLevel;
out vec4 fragColor;

uniform vec3 uColor;
uniform float uLevelMin;
uniform float uLevelMax;

void main() {
  // Map level to [0,1]
  float t = 0.0;
  if (uLevelMax > uLevelMin)
    t = clamp((vLevel - uLevelMin) / (uLevelMax - uLevelMin), 0.0, 1.0);
  // Higher levels more visible
  float levelAlpha = mix(0.05, 0.35, pow(t, 1.5));
  // Depth falloff: further lines get fainter
  float falloff = 1.0 / (1.0 + 0.25 * vDistance);
  float alpha = clamp(levelAlpha * falloff, 0.02, 0.5);
  fragColor = vec4(uColor, alpha);
}
