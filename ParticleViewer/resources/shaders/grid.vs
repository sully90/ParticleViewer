#version 330 core
layout (location = 0) in vec3 position;
layout (location = 1) in float level;

out float vDistance;
out float vLevel;

uniform mat4 view;
uniform mat4 projection;

void main() {
  vec4 posEye = view * vec4(position, 1.0);
  vDistance = length(posEye.xyz);
  vLevel = level;
  gl_Position = projection * posEye;
}
