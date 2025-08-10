#version 330 core
layout (location = 0) in vec3 aPos;          // unit cube vertex in [-1,1]
layout (location = 1) in vec3 iCenter;       // instance center
layout (location = 2) in float iHalfSize;    // instance half size (world units)
layout (location = 3) in float iDensity;     // instance density (overdensity)

out float vDensity;
out vec3 vLocal; // interpolated local position in [-1,1]

uniform mat4 view;
uniform mat4 projection;
uniform float uInflate; // slight scale to promote overlap

void main() {
  // Expand unit cube by half size around center
  vec3 worldPos = iCenter + aPos * (iHalfSize * uInflate);
  vDensity = iDensity;
  vLocal = aPos;
  gl_Position = projection * view * vec4(worldPos, 1.0);
}
