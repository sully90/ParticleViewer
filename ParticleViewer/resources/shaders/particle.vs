#version 330 core
layout (location = 0) in vec3 position;

out float vIntensity;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float uPointBaseSize; // base sprite size in pixels at unit distance
uniform float uPointScale;    // pixel scale factor (depends on FOV and viewport)

void main()
{
    vec4 posEye4 = view * model * vec4(position, 1.0);
    vec3 posEye = posEye4.xyz;
    float dist = max(0.0001, length(posEye));

    // Projected size attenuation by distance
    float pointSize = clamp(uPointBaseSize * (uPointScale / dist), 1.0, 64.0);
    gl_PointSize = pointSize;

    // Simple intensity falloff with distance for coloring
    vIntensity = clamp(1.0 / (0.1 + 0.3 * dist), 0.0, 1.0);

    gl_Position = projection * vec4(posEye, 1.0);
}