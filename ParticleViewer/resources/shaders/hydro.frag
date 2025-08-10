#version 330 core
in float vDensity;
in vec3 vLocal; // from vertex shader in [-1,1]
out vec4 fragColor;

// Jet colormap: blue -> cyan -> green -> yellow -> red
vec3 jet(float x) {
  x = clamp(x, 0.0, 1.0);
  float r = clamp(1.5 - abs(4.0 * x - 3.0), 0.0, 1.0);
  float g = clamp(1.5 - abs(4.0 * x - 2.0), 0.0, 1.0);
  float b = clamp(1.5 - abs(4.0 * x - 1.0), 0.0, 1.0);
  return vec3(r, g, b);
}

uniform float uRhoMin;
uniform float uRhoMax;
uniform float uGasScale; // overall brightness for gas contribution (premultiplied)
uniform float uGasAlpha; // alpha weight for hydro blending (SRC_ALPHA, ONE)

void main() {
  // Box face fade to reduce blockiness when close: detect dominant axis (face)
  vec3 a = abs(vLocal);
  vec2 uv;
  if (a.x >= a.y && a.x >= a.z) {
    uv = vLocal.yz; // +/-X face: fade toward edges in YZ
  } else if (a.y >= a.x && a.y >= a.z) {
    uv = vLocal.xz; // +/-Y face: fade toward edges in XZ
  } else {
    uv = vLocal.xy; // +/-Z face: fade toward edges in XY
  }
  // Spherical impostor on each face to reduce blockiness
  float r2 = dot(uv, uv);
  if (r2 > 1.0) discard;
  float faceFalloff = pow(1.0 - r2, 2.0);

  // Log-scale density mapping with adaptive contrast
  float v = (log(max(vDensity, 1e-30)) - log(max(uRhoMin, 1e-30))) /
            (log(max(uRhoMax, 1e-30)) - log(max(uRhoMin, 1e-30)));
  v = clamp(v, 0.0, 1.0);
  // Contrast boost in mid-tones to counter wash-out
  v = pow(v, 0.65);
  vec3 color = jet(v);
  float alpha = clamp(v, 0.0, 1.0) * faceFalloff * uGasAlpha;
  vec3 premul = color * faceFalloff * uGasScale;
  fragColor = vec4(premul, alpha);
}
