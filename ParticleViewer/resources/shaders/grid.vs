#version 330 core
layout (location = 0) in vec3 position;

out float vDistance;

uniform mat4 view;
uniform mat4 projection;

void main() {
  vec4 posEye = view * vec4(position, 1.0);
  vDistance = length(posEye.xyz);
  gl_Position = projection * posEye;
}
