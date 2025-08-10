#version 330 core
in vec2 vUV;
in vec2 vNdc;
out vec4 fragColor;

uniform sampler3D uVolumeDensity;
uniform sampler3D uVolumeTemp;
uniform mat4 uView;
uniform mat4 uProj;
uniform float uRhoMin;
uniform float uRhoMax;
uniform bool uIsOverdensity;   // if false, values are SI kg/m^3
uniform bool uUseTemperature;  // if true, visualize temperature instead of density
uniform int uSteps;         // ray-march steps
uniform float uExposure;    // brightness/exposure scale
uniform float uSigma;       // extinction/emission scale
uniform float uTempMin;
uniform float uTempMax;

// Jet color map with highlight bias for high values
vec3 jet(float x) {
  x = clamp(x, 0.0, 1.0);
  float r = clamp(1.5 - abs(4.0 * x - 3.0), 0.0, 1.0);
  float g = clamp(1.5 - abs(4.0 * x - 2.0), 0.0, 1.0);
  float b = clamp(1.5 - abs(4.0 * x - 1.0), 0.0, 1.0);
  vec3 col = vec3(r, g, b);
  float hi = smoothstep(0.80, 1.0, x);
  col = mix(col, vec3(1.0), 0.35 * hi);
  return col;
}

// Convert density to normalized [0,1]
float mapDensity(float d) {
  // If overdensity, use log scale. If SI, map linearly in log-space but clamp to range.
  float numer = log(max(d, 1e-30)) - log(max(uRhoMin, 1e-30));
  float denom = max(1e-12, log(max(uRhoMax, 1e-30)) - log(max(uRhoMin, 1e-30)));
  float v = numer / denom;
  v = clamp(v, 0.0, 1.0);
  // Mid/high boost to emphasize dense regions
  return pow(v, 0.6);
}

float mapTemperature(float T) {
  float numer = log(max(T, 1e-30)) - log(max(uTempMin, 1e-30));
  float denom = max(1e-12, log(max(uTempMax, 1e-30)) - log(max(uTempMin, 1e-30)));
  float v = numer / denom;
  return clamp(v, 0.0, 1.0);
}

// 'Hot' colormap: black -> red -> yellow -> white
vec3 hot(float x){
  x = clamp(x, 0.0, 1.0);
  float r = clamp(3.0 * x, 0.0, 1.0);
  float g = clamp(3.0 * x - 1.0, 0.0, 1.0);
  float b = clamp(3.0 * x - 2.0, 0.0, 1.0);
  return vec3(r, g, b);
}

void main() {
  // Reconstruct primary ray in world space and march through unit cube [0,1]^3
  vec2 ndc = vNdc; // exact NDC from vertex shader
  vec4 clipNear = vec4(ndc, -1.0, 1.0);
  vec4 clipFar  = vec4(ndc,  1.0, 1.0);
  mat4 invProj = inverse(uProj);
  mat4 invView = inverse(uView);

  // Camera world position
  vec3 camWorld = (invView * vec4(0.0, 0.0, 0.0, 1.0)).xyz;

  // World-space points on near/far plane
  vec4 nearView = invProj * clipNear; nearView /= nearView.w;
  vec4 farView  = invProj * clipFar;  farView  /= farView.w;
  vec3 nearWorld = (invView * nearView).xyz;
  vec3 farWorld  = (invView * farView).xyz;

  vec3 rayDir = normalize(farWorld - camWorld);

  // Guard against invalid directions
  if (!all(greaterThan(abs(rayDir), vec3(1e-8)))) discard;

  // Intersect ray from camera with unit cube [0,1]^3 using slab method
  vec3 t0 = (vec3(0.0) - camWorld) / rayDir;
  vec3 t1 = (vec3(1.0) - camWorld) / rayDir;
  vec3 tmin = min(t0, t1);
  vec3 tmax = max(t0, t1);
  float tNear = max(max(tmin.x, tmin.y), tmin.z);
  float tFar  = min(min(tmax.x, tmax.y), tmax.z);
  if (tFar <= tNear) discard;
  // If the camera starts inside the volume, clamp tNear to 0
  tNear = max(tNear, 0.0);

  // March from near to far
  float steps = float(uSteps);
  float dt = max((tFar - tNear) / steps, 1e-4);
  vec3 pos = camWorld + rayDir * (tNear + 0.5 * dt);

  vec3 accum = vec3(0.0);
  float transmittance = 1.0;

  for (int i = 0; i < uSteps; ++i) {
    // Sample either density or temperature
    float vNorm;
    vec3 col;
    if (uUseTemperature) {
      float T = texture(uVolumeTemp, pos).r;
      vNorm = mapTemperature(T);
      col = hot(pow(vNorm, 0.8));
    } else {
      float d = texture(uVolumeDensity, pos).r;
      vNorm = mapDensity(d);
      col = jet(pow(vNorm, 0.6));
    }
    // Convert normalized value to alpha via Beer-Lambert
    float alpha = 1.0 - exp(-uSigma * pow(vNorm, 1.4) * dt);

    // Front-to-back compositing
    accum += transmittance * col * alpha;
    transmittance *= (1.0 - alpha);

    // Early out if almost opaque
    if (transmittance < 0.01) break;

    pos += rayDir * dt;
  }

  // Apply exposure
  vec3 color = 1.0 - exp(-uExposure * accum);
  fragColor = vec4(color, 1.0 - transmittance);
}
