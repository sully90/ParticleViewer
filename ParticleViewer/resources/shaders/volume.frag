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
uniform vec3 uDomainMin;   // translate from raw coords to unit cube
uniform vec3 uDomainScale; // scale from raw coords to unit cube
uniform bool uShowAMRLevels; // visualize AMR refinement levels
uniform float uLevelOpacity; // opacity for AMR level visualization
uniform bool uDebugMode;     // debug visualization mode

// Smooth jet color map with high value emphasis
vec3 jet(float x) {
  x = clamp(x, 0.0, 1.0);
  
  // Smooth color transitions using cosine interpolation
  vec3 col;
  
  if (x < 0.17) {
    // Dark blue to blue (smooth)
    float t = x / 0.17;
    t = smoothstep(0.0, 1.0, t);
    col = mix(vec3(0.0, 0.0, 0.3), vec3(0.0, 0.0, 1.0), t);
  } else if (x < 0.33) {
    // Blue to cyan
    float t = (x - 0.17) / 0.16;
    t = smoothstep(0.0, 1.0, t);
    col = mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), t);
  } else if (x < 0.5) {
    // Cyan to green
    float t = (x - 0.33) / 0.17;
    t = smoothstep(0.0, 1.0, t);
    col = mix(vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0), t);
  } else if (x < 0.67) {
    // Green to yellow
    float t = (x - 0.5) / 0.17;
    t = smoothstep(0.0, 1.0, t);
    col = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), t);
  } else if (x < 0.83) {
    // Yellow to orange/red
    float t = (x - 0.67) / 0.16;
    t = smoothstep(0.0, 1.0, t);
    col = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.3, 0.0), t);
  } else {
    // Orange/red to bright red/white
    float t = (x - 0.83) / 0.17;
    t = smoothstep(0.0, 1.0, t);
    col = mix(vec3(1.0, 0.3, 0.0), vec3(1.0, 0.6, 0.4), t);
    // Subtle glow for highest values
    col = col * (1.0 + t * 0.3);
  }
  
  return col;
}

// Convert density to normalized [0,1]
float mapDensity(float d) {
  // Handle zero/very low densities
  if (d <= 0.0) return 0.0;
  
  // More aggressive mapping for better visibility
  // Clamp to range but be more permissive
  float dClamped = max(d, uRhoMin * 0.01);  // Allow lower values
  
  // Use log scale for better dynamic range handling
  float logD = log(dClamped);
  float logMin = log(max(uRhoMin * 0.01, 1e-40));  // Extend range down
  float logMax = log(max(uRhoMax, 1e-40));
  
  // Ensure valid range
  if (logMax <= logMin) return 0.5;
  
  float v = (logD - logMin) / (logMax - logMin);
  v = clamp(v, 0.0, 1.0);
  
  // Smooth transfer function with high density emphasis
  if (uIsOverdensity) {
    // Smooth S-curve for better gradients
    // Suppress low density, emphasize high density smoothly
    float suppressed = v * v * v;  // Cubic suppression of low values
    float enhanced = 1.0 - pow(1.0 - v, 3.0);  // Cubic enhancement of high values
    
    // Blend between suppression and enhancement
    float blend = smoothstep(0.2, 0.8, v);
    return mix(suppressed * 0.5, enhanced, blend);
  } else {
    // For SI units: similar smooth curve
    float suppressed = v * v;  // Quadratic suppression
    float enhanced = 1.0 - pow(1.0 - v, 2.0);  // Quadratic enhancement
    
    float blend = smoothstep(0.3, 0.7, v);
    return mix(suppressed * 0.4, enhanced, blend);
  }
}

float mapTemperature(float T) {
  float numer = log(max(T, 1e-40)) - log(max(uTempMin, 1e-40));
  float denom = max(1e-12, log(max(uTempMax, 1e-40)) - log(max(uTempMin, 1e-40)));
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

  // Intersect ray with unit cube [0,1]^3
  vec3 boxMin = vec3(0.0);
  vec3 boxMax = vec3(1.0);
  vec3 t0 = (boxMin - camWorld) / rayDir;
  vec3 t1 = (boxMax - camWorld) / rayDir;
  vec3 tmin = min(t0, t1);
  vec3 tmax = max(t0, t1);
  float tNear = max(max(tmin.x, tmin.y), tmin.z);
  float tFar  = min(min(tmax.x, tmax.y), tmax.z);
  if (tFar <= tNear) discard;
  // If the camera starts inside the volume, clamp tNear to 0
  tNear = max(tNear, 0.0);

  // Fixed step size for predictable performance
  float steps = float(uSteps);
  float dt = max((tFar - tNear) / steps, 1e-4);
  
  // Jittered starting position for better quality
  float jitter = fract(sin(dot(vUV, vec2(12.9898, 78.233))) * 43758.5453) * 0.5;
  vec3 pos = camWorld + rayDir * (tNear + jitter * dt);

  vec3 accum = vec3(0.0);
  float transmittance = 1.0;
  
  // Track maximum density along ray for adaptive sampling
  float maxDensityAlongRay = 0.0;

  // Simple fixed-step marching for performance
  for (int i = 0; i < uSteps; ++i) {
    // Sample either density or temperature
    float vNorm;
    vec3 col;
    // Position is already in [0,1] space for texture sampling
    vec3 uvw = pos;
    
    // Skip if outside texture bounds
    if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) {
      pos += rayDir * dt;
      continue;
    }
    
    // Use trilinear filtering with gradient-based shading
    if (uUseTemperature) {
      float T = texture(uVolumeTemp, uvw).r;
      vNorm = mapTemperature(T);
      col = hot(pow(vNorm, 0.8));
    } else {
      float d = texture(uVolumeDensity, uvw).r;
      vNorm = mapDensity(d);
      
      // Debug mode: visualize any non-zero density
      if (uDebugMode) {
        if (d > 0.0) {
          // Show any non-zero density as bright color
          col = vec3(1.0, 0.5, 0.0);  // Orange
          vNorm = min(1.0, d * 100.0);  // Amplify visibility
        } else {
          col = vec3(0.0, 0.0, 0.1);  // Dark blue for zero
          vNorm = 0.01;
        }
      } else {
        // Normal coloring - apply slight smoothing
        float smoothedNorm = smoothstep(0.0, 1.0, vNorm);
        col = jet(smoothedNorm);
        
        // Very subtle brightness adjustment for high density
        if (vNorm > 0.8) {
          float boost = smoothstep(0.8, 1.0, vNorm);
          col = col * (1.0 + boost * 0.2);  // Only 20% max brightness boost
        }
      }
      
      // AMR level visualization overlay
      if (uShowAMRLevels && vNorm > 0.01) {
        // Color code by estimated refinement (higher density often means higher refinement)
        vec3 levelCol = vec3(
          smoothstep(0.0, 0.3, vNorm),
          smoothstep(0.3, 0.6, vNorm),
          smoothstep(0.6, 1.0, vNorm)
        );
        col = mix(col, levelCol, uLevelOpacity * 0.3);
      }
    }
    
    maxDensityAlongRay = max(maxDensityAlongRay, vNorm);
    
    // Smooth opacity curve to avoid blotchiness
    float opacity;
    
    // Use smooth power curve for opacity
    // Low values get less opacity, high values get more, but smoothly
    opacity = pow(vNorm, 0.8);  // Slightly emphasize high density
    
    // Add subtle boost for very high density without sharp transitions
    if (vNorm > 0.7) {
      float boost = smoothstep(0.7, 0.95, vNorm);
      opacity = opacity * (1.0 + boost * 0.5);  // Smooth boost up to 50%
    }
    
    float alpha = 1.0 - exp(-uSigma * opacity * dt);

    // Front-to-back compositing with pre-multiplied alpha
    accum += transmittance * col * alpha;
    transmittance *= (1.0 - alpha);

    // Early out if almost opaque
    if (transmittance < 0.01) break;

    // Fixed step size for performance
    pos += rayDir * dt;
  }

  // Apply exposure
  vec3 color = 1.0 - exp(-uExposure * accum);
  fragColor = vec4(color, 1.0 - transmittance);
}
