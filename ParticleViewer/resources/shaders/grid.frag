#version 330 core
out vec4 fragColor;

uniform vec3 uColor;

void main() {
  fragColor = vec4(uColor, 1.0);
}
