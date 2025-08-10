#version 330 core
layout (location = 0) in vec2 aPos;

out vec2 vUV;
out vec2 vNdc;

void main() {
  vUV = (aPos * 0.5) + 0.5;
  vNdc = aPos;
  gl_Position = vec4(aPos, 0.0, 1.0);
}
