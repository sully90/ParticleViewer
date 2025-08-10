#include "HydroRenderer.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <algorithm>
#include <iostream>

HydroRenderer::HydroRenderer(const std::string& infoFilePath) {
  m_snap = std::make_unique<RAMSES::snapshot>(infoFilePath, RAMSES::version3);
  m_shader = std::make_unique<Shader>("./resources/shaders/volume.vs", "./resources/shaders/volume.frag");
  glGenVertexArrays(1, &m_vao);
  glGenBuffers(1, &m_vbo);
  glGenBuffers(1, &m_instanceVBO);
}

HydroRenderer::~HydroRenderer() {
  if (m_instanceVBO) glDeleteBuffers(1, &m_instanceVBO);
  if (m_vbo) glDeleteBuffers(1, &m_vbo);
  if (m_vao) glDeleteVertexArrays(1, &m_vao);
  if (m_volumeTexDensity) glDeleteTextures(1, &m_volumeTexDensity);
  if (m_volumeTexTemp) glDeleteTextures(1, &m_volumeTexTemp);
}

void HydroRenderer::buildLevelCache(unsigned minLevel, unsigned maxLevel) {
  m_instancesByLevel.clear();
  m_instancesByLevel.resize(maxLevel + 1);
  
  unsigned ilevelMax = std::min<unsigned>(maxLevel, m_snap->m_header.levelmax);
  using Cell = RAMSES::AMR::cell_locally_essential<>;
  using Level = RAMSES::AMR::level<Cell>;
  
  const double G = 6.67430e-11;
  const double Mpc_in_m = 3.085677581491367e22;
  const double H0_km_s_Mpc = m_snap->m_header.H0;
  const double H0 = H0_km_s_Mpc * 1000.0 / Mpc_in_m;
  const double PI = 3.14159265358979323846;
  const double rho_crit0 = 3.0 * H0 * H0 / (8.0 * PI * G);
  const double a = m_snap->m_header.aexp;
  const double Omega_b0 = m_snap->m_header.omega_b;
  const bool useOverdensity = (Omega_b0 > 0.0);
  m_isOverdensity = useOverdensity;
  const double rho_bar_b = useOverdensity ? (Omega_b0 * rho_crit0 / (a * a * a)) : 1.0;
  const double unit_d = m_snap->m_header.unit_d;
  const bool useCodeUnits = !(unit_d > 0.0 && std::isfinite(unit_d));
  const double densityScale = useCodeUnits ? 1.0 : unit_d;
  
  unsigned ncpu = m_snap->m_header.ncpu;
  for (int icpu = 1; icpu <= (int)ncpu; ++icpu) {
    RAMSES::AMR::tree<Cell, Level> tree(*m_snap, icpu, ilevelMax, minLevel);
    tree.read();
    RAMSES::HYDRO::data<decltype(tree)> hydro(tree);
    hydro.read("density");
    RAMSES::HYDRO::data<decltype(tree)> press(tree);
    press.read("pressure");
    
    for (unsigned lvl = minLevel; lvl <= ilevelMax; ++lvl) {
      float halfSize = (0.5f / std::pow(2.0f, (float)lvl + 1.0f));
      for (auto it = tree.begin(lvl); it != tree.end(lvl); ++it) {
        for (int c = 0; c < 8; ++c) {
          bool refined = false;
          try { refined = it.is_refined(c); } catch (...) { refined = false; }
          if (refined) continue;
          
          auto cp = tree.cell_pos<float>(it, c);
          glm::vec3 cc(cp.x, cp.y, cp.z);
          
          float rho_code = hydro(it, c);
          double rho_phys = (double)rho_code * densityScale;
          float value = useOverdensity ? (float)(rho_phys / rho_bar_b) : (float)rho_phys;
          
          float P_code = press(it, c);
          const double unit_l = m_snap->m_header.unit_l;
          const double unit_t = m_snap->m_header.unit_t;
          double unit_p = (std::isfinite(unit_d) && unit_d > 0.0 && std::isfinite(unit_l) && unit_l > 0.0 && std::isfinite(unit_t) && unit_t > 0.0)
                            ? unit_d * (unit_l * unit_l) / (unit_t * unit_t) : 1.0;
          double P_phys = (double)P_code * unit_p;
          const double kB = 1.380649e-23;
          const double mH = 1.6735575e-27;
          double T = (rho_phys > 0.0) ? (P_phys / rho_phys) * (mH / kB) : 0.0;
          
          if (useOverdensity && (value < m_minOverdensity || value > m_maxOverdensity)) continue;
          
          Instance inst{cc, halfSize, value, (float)T, (float)lvl};
          m_instancesByLevel[lvl].push_back(inst);
        }
      }
    }
  }
  m_cacheReady = true;
}

void HydroRenderer::build(unsigned minLevel, unsigned maxLevel) {
  // Limit initial load to fewer levels for performance
  unsigned ilevelMax = std::min<unsigned>(maxLevel, m_snap->m_header.levelmax);
  ilevelMax = std::min<unsigned>(ilevelMax, minLevel + 3);  // Cap to 4 levels initially
  using Cell = RAMSES::AMR::cell_locally_essential<>;
  using Level = RAMSES::AMR::level<Cell>;

  // AMR positions from cell_pos are already in unit-cube [0,1]
  float invBoxlen = 1.0f;
  // Track overall raw bounds after normalization (for logging; no additional fit)
  glm::vec3 rawMin(1e9f), rawMax(-1e9f);

  m_instances.clear();
  m_instances.reserve(500000);  // Pre-allocate for performance
  long long cellsAdded = 0;

  // Density interpretation: overdensity if cosmological (Omega_b > 0), else SI units
  const double G = 6.67430e-11;                    // m^3 kg^-1 s^-2
  const double Mpc_in_m = 3.085677581491367e22;    // m
  const double H0_km_s_Mpc = m_snap->m_header.H0;  // assume km/s/Mpc
  const double H0 = H0_km_s_Mpc * 1000.0 / Mpc_in_m; // s^-1
  const double PI = 3.14159265358979323846;
  const double rho_crit0 = 3.0 * H0 * H0 / (8.0 * PI * G); // kg/m^3
  const double a = m_snap->m_header.aexp;
  const double Omega_b0 = m_snap->m_header.omega_b;
  const double Omega_m = m_snap->m_header.omega_m;
  const bool useOverdensity = (Omega_b0 > 0.0);
  m_isOverdensity = useOverdensity;
  
  std::cout << "\n[Hydro] Cosmological parameters:\n";
  std::cout << "  H0 = " << H0_km_s_Mpc << " km/s/Mpc\n";
  std::cout << "  aexp = " << a << " (z = " << (1.0/a - 1.0) << ")\n";
  std::cout << "  Omega_b = " << Omega_b0 << "\n";
  std::cout << "  Omega_m = " << Omega_m << "\n";
  std::cout << "  rho_crit0 = " << std::scientific << rho_crit0 << " kg/m³\n" << std::fixed;
  std::cout << "  Interpretation: " << (useOverdensity ? "Overdensity" : "Physical density") << "\n";
  // Adjust robust range quantiles for SI densities to avoid getting swamped by outliers
  if (!m_isOverdensity) {
    m_lowQuantile = 0.10f;  // More aggressive filtering
    m_highQuantile = 0.99f;
  } else {
    m_lowQuantile = 0.05f;  // Less extreme
    m_highQuantile = 0.95f;
  }
  const double rho_bar_b = useOverdensity ? (Omega_b0 * rho_crit0 / (a * a * a)) : 1.0; // avoid zero
  const double unit_d = m_snap->m_header.unit_d; // code density to kg/m^3 (may be 0 for non-cosmo)
  const bool useCodeUnits = !(unit_d > 0.0 && std::isfinite(unit_d));
  const double densityScale = useCodeUnits ? 1.0 : unit_d;
  
  std::cout << "  unit_d = " << std::scientific << unit_d << " kg/m³\n";
  std::cout << "  rho_bar_b = " << rho_bar_b << " kg/m³\n";
  std::cout << "  Using code units: " << (useCodeUnits ? "Yes" : "No") << "\n" << std::fixed;

  m_rhoMin = 1e30f; m_rhoMax = 0.0f;
  m_tempMin = 1e30f; m_tempMax = 0.0f;
  // Collect log-values for robust percentile range selection
  std::vector<float> logDensities;
  std::vector<float> logTemps;
  logDensities.reserve(100000);  // Pre-allocate for performance
  logTemps.reserve(100000);
  unsigned ncpu = m_snap->m_header.ncpu;
  // Limit CPUs for initial load
  unsigned maxCpuToLoad = std::min(ncpu, 4u);  // Only load first 4 CPUs initially
  for (int icpu = 1; icpu <= (int)maxCpuToLoad; ++icpu) {
    RAMSES::AMR::tree<Cell, Level> tree(*m_snap, icpu, ilevelMax, minLevel);
    tree.read();
    RAMSES::HYDRO::data<decltype(tree)> hydro(tree);
    hydro.read("density");
    RAMSES::HYDRO::data<decltype(tree)> press(tree);
    press.read("pressure");
    // Always use 1/boxlen normalization for hydro positions
    for (unsigned lvl=minLevel; lvl<=ilevelMax; ++lvl) {
      // Child cell half-size at level lvl in unit cube
      float halfSize = (0.5f / std::pow(2.0f, (float)lvl + 1.0f));
      for (auto it = tree.begin(lvl); it != tree.end(lvl); ++it) {
        // 8 child cells per grid
        for (int c=0; c<8; ++c) {
          // Skip non-leaf cells: if child is refined, a finer level will represent it
          bool refined = false;
          try { refined = it.is_refined(c); } catch (...) { refined = false; }
          if (refined) continue;
          auto cp = tree.cell_pos<float>(it, c);
          // RAMSES positions may be offset - normalize to [0,1]
          glm::vec3 cc(cp.x, cp.y, cp.z);
          
          // Check if coordinates are outside [0,1] and need wrapping
          if (cc.x > 1.0f || cc.y > 1.0f || cc.z > 1.0f) {
            // Positions appear to be around [1,1,1], subtract integer part
            cc.x = cc.x - std::floor(cc.x);
            cc.y = cc.y - std::floor(cc.y);
            cc.z = cc.z - std::floor(cc.z);
          }
          
          rawMin = glm::min(rawMin, cc);
          rawMax = glm::max(rawMax, cc);
        // retrieve density value for this cell
          float rho_code = hydro(it, c);
          double rho_phys = (double)rho_code * densityScale;  // kg/m^3 or code units if scale==1
          float value = useOverdensity ? (float)(rho_phys / rho_bar_b)
                                       : (float)rho_phys;     // overdensity or SI
          
          // Check for invalid values
          if (!std::isfinite(value) || value < 0.0f) {
            continue;  // Skip NaN, inf, or negative densities
          }
          // Compute gas temperature T = P / rho * (mH/kB)
          float P_code = press(it, c);
          const double unit_l = m_snap->m_header.unit_l;
          const double unit_t = m_snap->m_header.unit_t;
          double unit_p = (std::isfinite(unit_d) && unit_d > 0.0 && std::isfinite(unit_l) && unit_l > 0.0 && std::isfinite(unit_t) && unit_t > 0.0)
                            ? unit_d * (unit_l * unit_l) / (unit_t * unit_t)
                            : 1.0;
          double P_phys = (double)P_code * unit_p; // Pascals if units valid
          const double kB = 1.380649e-23;      // J/K
          const double mH = 1.6735575e-27;    // kg
          double T = (rho_phys > 0.0) ? (P_phys / rho_phys) * (mH / kB) : 0.0;
          // Apply filtering window only when using overdensity
          if (useOverdensity) {
            if (value < m_minOverdensity || value > m_maxOverdensity) {
              continue;
            }
          }
          m_rhoMin = std::min(m_rhoMin, value);
          m_rhoMax = std::max(m_rhoMax, value);
          m_tempMin = std::min(m_tempMin, (float)T);
          m_tempMax = std::max(m_tempMax, (float)T);
          if (m_useRobustRange && value > 0.0f) logDensities.push_back(std::log(value));
          if (m_useRobustRange && T > 0.0) logTemps.push_back((float)std::log(T));
          
          // Ensure position is wrapped to [0,1] for the instance too
          glm::vec3 wrappedPos = cc;
          if (wrappedPos.x > 1.0f) wrappedPos.x -= std::floor(wrappedPos.x);
          if (wrappedPos.y > 1.0f) wrappedPos.y -= std::floor(wrappedPos.y);
          if (wrappedPos.z > 1.0f) wrappedPos.z -= std::floor(wrappedPos.z);
          
          // Half-size in box coordinates (child cell)
          Instance inst{wrappedPos, halfSize, value, (float)T, (float)lvl};
          m_instances.push_back(inst);
          // Limit initial cells for performance
          if (m_maxCells >= 0 || m_instances.size() > 500000) {
            ++cellsAdded;
            if (cellsAdded >= m_maxCells || m_instances.size() > 500000) {
              std::cout << "[Hydro] Limiting to " << m_instances.size() << " cells for performance\n";
              goto finish_build;
            }
          }
        }
      }
    }
  }
finish_build:
  // Print detailed statistics for debugging
  std::cout << "\n[HydroRenderer] Density Statistics:\n";
  std::cout << "  Total cells loaded: " << m_instances.size() << "\n";
  std::cout << "  Density interpretation: " << (m_isOverdensity ? "Overdensity" : "SI units (kg/m³)") << "\n";
  
  // Calculate basic statistics
  if (!m_instances.empty()) {
    double sum = 0.0;
    double minVal = 1e30, maxVal = -1e30;
    std::vector<float> allDensities;
    allDensities.reserve(m_instances.size());
    
    for (const auto& inst : m_instances) {
      sum += inst.density;
      minVal = std::min(minVal, (double)inst.density);
      maxVal = std::max(maxVal, (double)inst.density);
      allDensities.push_back(inst.density);
    }
    
    double mean = sum / m_instances.size();
    
    // Calculate median
    std::sort(allDensities.begin(), allDensities.end());
    double median = allDensities[allDensities.size() / 2];
    
    // Calculate percentiles
    auto getPercentile = [&](float p) -> double {
      size_t idx = std::min(allDensities.size() - 1, 
                           (size_t)(p * (allDensities.size() - 1)));
      return allDensities[idx];
    };
    
    std::cout << "  Raw density range: [" << std::scientific << minVal << ", " << maxVal << "]\n";
    std::cout << "  Mean density: " << mean << "\n";
    std::cout << "  Median density: " << median << "\n";
    std::cout << "  Percentiles:\n";
    std::cout << "    1%: " << getPercentile(0.01f) << "\n";
    std::cout << "    5%: " << getPercentile(0.05f) << "\n";
    std::cout << "   10%: " << getPercentile(0.10f) << "\n";
    std::cout << "   25%: " << getPercentile(0.25f) << "\n";
    std::cout << "   50%: " << getPercentile(0.50f) << "\n";
    std::cout << "   75%: " << getPercentile(0.75f) << "\n";
    std::cout << "   90%: " << getPercentile(0.90f) << "\n";
    std::cout << "   95%: " << getPercentile(0.95f) << "\n";
    std::cout << "   99%: " << getPercentile(0.99f) << "\n";
    
    // Count zeros and very low values
    int zeroCount = 0;
    int veryLowCount = 0;
    double threshold = median * 0.01;
    for (float d : allDensities) {
      if (d == 0.0f) zeroCount++;
      else if (d < threshold) veryLowCount++;
    }
    std::cout << "  Zero values: " << zeroCount << " (" 
              << (100.0 * zeroCount / allDensities.size()) << "%)\n";
    std::cout << "  Very low values (<1% of median): " << veryLowCount << " (" 
              << (100.0 * veryLowCount / allDensities.size()) << "%)\n";
  }
  
  if (m_useRobustRange && !logDensities.empty()) {
    // Compute robust min/max via percentiles in log space, then exponentiate
    std::sort(logDensities.begin(), logDensities.end());
    size_t n = logDensities.size();
    
    // Use more intelligent percentiles based on data distribution
    float effectiveLowQ = m_lowQuantile;
    float effectiveHighQ = m_highQuantile;
    
    // If overdensity, use tighter range around interesting features
    if (m_isOverdensity) {
      effectiveLowQ = 0.20f;  // Start at 20th percentile to skip voids
      effectiveHighQ = 0.99f;  // Go up to 99th to capture dense regions
    } else {
      // For SI units, use wider range
      effectiveLowQ = 0.10f;
      effectiveHighQ = 0.999f;
    }
    
    size_t ilow = static_cast<size_t>(std::floor(effectiveLowQ * (n - 1)));
    size_t ihigh = static_cast<size_t>(std::ceil(effectiveHighQ * (n - 1)));
    float logMin = logDensities[ilow];
    float logMax = logDensities[ihigh];
    m_rhoMin = std::max(1e-30f, std::exp(logMin));
    m_rhoMax = std::max(m_rhoMin * 1.0001f, std::exp(logMax));
    
    // For overdensity, ensure we capture the interesting range around 1.0
    if (m_isOverdensity && m_rhoMin > 0.1f && m_rhoMax > 1.0f) {
      m_rhoMin = std::min(m_rhoMin, 0.1f);  // Include underdense regions
    }
  } else if (!m_useRobustRange) {
    // Ensure a sane non-degenerate range
    m_rhoMin = std::max(1e-30f, m_rhoMin);
    m_rhoMax = std::max(m_rhoMin * 1.0001f, m_rhoMax);
  } else {
    // Robust requested but no positive samples; choose a default display range
    m_rhoMin = 1e-30f;
    m_rhoMax = 1e-24f;
  }

  // Ensure sufficient dynamic range for visualization
  float logRange = std::log(m_rhoMax / m_rhoMin);
  if (logRange < 2.0f) {  // Less than ~7x range
    float logCenter = 0.5f * (std::log(m_rhoMin) + std::log(m_rhoMax));
    m_rhoMin = std::exp(logCenter - 1.5f);
    m_rhoMax = std::exp(logCenter + 1.5f);
  }
  
  std::cout << "\n  Color mapping range selected:\n";
  std::cout << "    Min: " << std::scientific << m_rhoMin << "\n";
  std::cout << "    Max: " << m_rhoMax << "\n";
  std::cout << "    Dynamic range: " << (m_rhoMax / m_rhoMin) << "x\n" << std::fixed;

  // Robust/default range for temperature
  if (m_useRobustRange && !logTemps.empty()) {
    std::sort(logTemps.begin(), logTemps.end());
    size_t nT = logTemps.size();
    size_t ilowT = static_cast<size_t>(std::floor(m_lowQuantile * (nT - 1)));
    size_t ihighT = static_cast<size_t>(std::ceil(m_highQuantile * (nT - 1)));
    float logMinT = logTemps[ilowT];
    float logMaxT = logTemps[ihighT];
    m_tempMin = std::max(1e-30f, std::exp(logMinT));
    m_tempMax = std::max(m_tempMin * 1.0001f, std::exp(logMaxT));
  } else {
    m_tempMin = std::max(1e-30f, m_tempMin);
    m_tempMax = std::max(m_tempMin * 1.0001f, m_tempMax);
  }

  // After wrapping, domain should be in [0,1]
  glm::vec3 extent = rawMax - rawMin;
  if (extent.x > 0 && extent.y > 0 && extent.z > 0) {
    // Now the data should be in [0,1] after wrapping
    m_domainMin = glm::vec3(0.0f);
    m_domainScale = glm::vec3(1.0f);  // No scaling needed
    
    std::cout << "[Hydro] Domain bounds (after wrapping) min=" << rawMin.x << "," << rawMin.y << "," << rawMin.z
              << " max=" << rawMax.x << "," << rawMax.y << "," << rawMax.z << "\n";
    std::cout << "[Hydro] Domain extent: " << extent.x << " x " << extent.y << " x " << extent.z << "\n";
  } else {
    m_domainMin = glm::vec3(0.0f);
    m_domainScale = glm::vec3(1.0f);
    std::cout << "[Hydro] Warning: Domain has zero extent\n";
  }

  // Cache instances by level if needed
  if (!m_cacheReady) {
    buildLevelCache(minLevel, maxLevel);
  }
  
  // Now upload and (re)build volume textures using normalized domain
  upload();
  // Build/update 3D volume textures for ray-marching
  if (m_useAdaptiveResolution) {
    createAdaptiveVolumeTextures();
  } else {
    createVolumeTexturesFromCells();
  }
}

void HydroRenderer::upload() {
  // Fullscreen triangle for volume ray-march pass
  static const float fsTri[] = {
    -1.0f, -1.0f,
     3.0f, -1.0f,
    -1.0f,  3.0f
  };
  glBindVertexArray(m_vao);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(fsTri), fsTri, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
}

void HydroRenderer::scaleMinDensity(float factor) {
  // Scale m_rhoMin in log-space for stability; clamp to avoid degenerate ranges
  factor = std::max(0.1f, std::min(factor, 10.0f));
  float newMin = m_rhoMin * factor;
  newMin = std::max(1e-30f, std::min(newMin, m_rhoMax * 0.999f));
  m_rhoMin = newMin;
}

void HydroRenderer::draw(const glm::mat4& view, const glm::mat4& proj) {
  if (!m_visible) return;
  m_shader->Use();
  // Set uniforms for volume ray march
  GLint viewLoc = glGetUniformLocation(m_shader->Program, "uView");
  GLint projLoc = glGetUniformLocation(m_shader->Program, "uProj");
  GLint rhoMinLoc = glGetUniformLocation(m_shader->Program, "uRhoMin");
  GLint rhoMaxLoc = glGetUniformLocation(m_shader->Program, "uRhoMax");
  GLint tMinLoc = glGetUniformLocation(m_shader->Program, "uTempMin");
  GLint tMaxLoc = glGetUniformLocation(m_shader->Program, "uTempMax");
  GLint domMinLoc = glGetUniformLocation(m_shader->Program, "uDomainMin");
  GLint domScaleLoc = glGetUniformLocation(m_shader->Program, "uDomainScale");
  GLint isOverLoc = glGetUniformLocation(m_shader->Program, "uIsOverdensity");
  GLint useTempLoc = glGetUniformLocation(m_shader->Program, "uUseTemperature");
  GLint volRhoLoc = glGetUniformLocation(m_shader->Program, "uVolumeDensity");
  GLint volTempLoc = glGetUniformLocation(m_shader->Program, "uVolumeTemp");
  GLint stepsLoc = glGetUniformLocation(m_shader->Program, "uSteps");
  GLint tfLoc = glGetUniformLocation(m_shader->Program, "uExposure");
  GLint sigmaLoc = glGetUniformLocation(m_shader->Program, "uSigma");
  GLint showAMRLoc = glGetUniformLocation(m_shader->Program, "uShowAMRLevels");
  GLint levelOpacityLoc = glGetUniformLocation(m_shader->Program, "uLevelOpacity");
  GLint debugModeLoc = glGetUniformLocation(m_shader->Program, "uDebugMode");
  glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &view[0][0]);
  glUniformMatrix4fv(projLoc, 1, GL_FALSE, &proj[0][0]);
  float rmin = std::max(1e-30f, m_rhoMin);
  float rmax = std::max(rmin * 1.0001f, m_rhoMax);
  glUniform1f(rhoMinLoc, rmin);
  glUniform1f(rhoMaxLoc, rmax);
  glUniform1f(tMinLoc, std::max(1e-30f, m_tempMin));
  glUniform1f(tMaxLoc, std::max(m_tempMin * 1.0001f, m_tempMax));
  // Pass the actual domain transformation to shader
  glUniform3f(domMinLoc, m_domainMin.x, m_domainMin.y, m_domainMin.z);
  glUniform3f(domScaleLoc, m_domainScale.x, m_domainScale.y, m_domainScale.z);
  glUniform1i(isOverLoc, m_isOverdensity ? 1 : 0);
  glUniform1i(useTempLoc, m_showTemperature ? 1 : 0);
  // Increase steps for smoother rendering (reduces blotchiness)
  int baseSteps = m_useAdaptiveResolution ? 256 : 192;
  glUniform1i(stepsLoc, m_isOverdensity ? baseSteps : baseSteps + 64);
  // Use adjustable exposure and sigma
  glUniform1f(tfLoc, m_exposure);
  glUniform1f(sigmaLoc, m_sigma);
  glUniform1i(showAMRLoc, m_showAMRLevels ? 1 : 0);
  glUniform1f(levelOpacityLoc, m_levelOpacity);
  glUniform1i(debugModeLoc, m_debugMode ? 1 : 0);
  // Bind 3D textures for raymarching
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, m_volumeTexDensity);
  glUniform1i(volRhoLoc, 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_3D, m_volumeTexTemp);
  glUniform1i(volTempLoc, 1);

  // Disable depth for volume, enable alpha-additive compositing
  glDisable(GL_DEPTH_TEST);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);

  // Fullscreen ray-march pass: draw a big triangle (3 verts)
  glBindVertexArray(m_vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);

  // Restore additive blending for particles
  glBlendFunc(GL_ONE, GL_ONE);
}

void HydroRenderer::createVolumeTexturesFromCells() {
  const int N = m_volumeResolution;
  // Allocate CPU buffers
  std::vector<float> volRho(N * N * N, 0.0f);
  std::vector<float> volTemp(N * N * N, 0.0f);
  auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
  
  std::cout << "[Hydro] Creating volume texture " << N << "³, processing " << m_instances.size() << " cells\n";
  
  int cellsWritten = 0;
  float maxDensityWritten = 0.0f;
  
  for (const auto &inst : m_instances) {
    // Positions should already be in [0,1] after wrapping
    glm::vec3 c = inst.center;
    // Extra safety clamp
    c.x = std::min(std::max(c.x, 0.0f), 1.0f);
    c.y = std::min(std::max(c.y, 0.0f), 1.0f);
    c.z = std::min(std::max(c.z, 0.0f), 1.0f);
    float gx = c.x * N;
    float gy = c.y * N;
    float gz = c.z * N;
    // HalfSize is already in [0,1] space
    float gh = inst.halfSize * N;
    int xmin = clampi((int)std::floor(gx - gh), 0, N-1);
    int xmax = clampi((int)std::ceil (gx + gh), 0, N-1);
    int ymin = clampi((int)std::floor(gy - gh), 0, N-1);
    int ymax = clampi((int)std::ceil (gy + gh), 0, N-1);
    int zmin = clampi((int)std::floor(gz - gh), 0, N-1);
    int zmax = clampi((int)std::ceil (gz + gh), 0, N-1);
    for (int z=zmin; z<=zmax; ++z) {
      for (int y=ymin; y<=ymax; ++y) {
        for (int x=xmin; x<=xmax; ++x) {
          size_t idx = ((size_t)z * N + y) * N + x;
          volRho[idx] = std::max(volRho[idx], inst.density);
          volTemp[idx] = std::max(volTemp[idx], inst.temperature);
          if (inst.density > 0) {
            cellsWritten++;
            maxDensityWritten = std::max(maxDensityWritten, inst.density);
          }
        }
      }
    }
  }
  
  // Debug: Check what's actually in the volume texture
  int nonZeroVoxels = 0;
  float minNonZero = 1e30f, maxNonZero = 0.0f;
  float avgDensity = 0.0f;
  for (int i = 0; i < N * N * N; ++i) {
    if (volRho[i] > 0.0f) {
      nonZeroVoxels++;
      minNonZero = std::min(minNonZero, volRho[i]);
      maxNonZero = std::max(maxNonZero, volRho[i]);
      avgDensity += volRho[i];
    }
  }
  if (nonZeroVoxels > 0) avgDensity /= nonZeroVoxels;
  
  std::cout << "[Hydro] Volume texture stats:\n";
  std::cout << "  Resolution: " << N << "³ = " << (N*N*N) << " voxels\n";
  std::cout << "  Non-zero voxels: " << nonZeroVoxels << " (" << (100.0f * nonZeroVoxels / (N*N*N)) << "%)\n";
  std::cout << "  Density range in texture: [" << std::scientific << minNonZero << ", " << maxNonZero << "]\n";
  std::cout << "  Average density (non-zero): " << avgDensity << std::fixed << "\n";
  std::cout << "  Voxels written from cells: " << cellsWritten << "\n";
  
  // Check temperature texture too
  int nonZeroTemp = 0;
  float minTemp = 1e30f, maxTemp = 0.0f;
  for (int i = 0; i < N * N * N; ++i) {
    if (volTemp[i] > 0.0f) {
      nonZeroTemp++;
      minTemp = std::min(minTemp, volTemp[i]);
      maxTemp = std::max(maxTemp, volTemp[i]);
    }
  }
  std::cout << "  Temperature non-zero voxels: " << nonZeroTemp << "\n";
  std::cout << "  Temperature range: [" << std::scientific << minTemp << ", " << maxTemp << "] K\n" << std::fixed;

  if (m_volumeTexDensity == 0) glGenTextures(1, &m_volumeTexDensity);
  glBindTexture(GL_TEXTURE_3D, m_volumeTexDensity);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, N, N, N, 0, GL_RED, GL_FLOAT, volRho.data());

  if (m_volumeTexTemp == 0) glGenTextures(1, &m_volumeTexTemp);
  glBindTexture(GL_TEXTURE_3D, m_volumeTexTemp);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, N, N, N, 0, GL_RED, GL_FLOAT, volTemp.data());
}

void HydroRenderer::createAdaptiveVolumeTextures() {
  // Clean up old volume levels
  for (auto& lvl : m_volumeLevels) {
    if (lvl.texDensity) glDeleteTextures(1, &lvl.texDensity);
    if (lvl.texTemp) glDeleteTextures(1, &lvl.texTemp);
  }
  m_volumeLevels.clear();
  
  if (!m_cacheReady || m_instancesByLevel.empty()) {
    // Fall back to simple volume if cache not ready
    createVolumeTexturesFromCells();
    return;
  }
  
  // Determine which AMR levels have data and create adaptive textures
  unsigned minActiveLevel = 999, maxActiveLevel = 0;
  for (size_t lvl = 0; lvl < m_instancesByLevel.size(); ++lvl) {
    if (!m_instancesByLevel[lvl].empty()) {
      minActiveLevel = std::min(minActiveLevel, (unsigned)lvl);
      maxActiveLevel = std::max(maxActiveLevel, (unsigned)lvl);
    }
  }
  
  if (minActiveLevel > maxActiveLevel) {
    // No data, fall back
    createVolumeTexturesFromCells();
    return;
  }
  
  // Create a moderate-resolution master volume using AMR level information
  // Limit resolution scaling for performance
  int levelSpan = std::min(3, (int)(maxActiveLevel - minActiveLevel));
  int masterRes = std::min(m_maxResolution, m_baseResolution * (1 << levelSpan));
  masterRes = std::max(masterRes, m_baseResolution);
  
  // Further limit resolution if we have too many instances
  if (m_instances.size() > 100000) {
    masterRes = std::min(masterRes, 128);
  }
  
  std::vector<float> volRho(masterRes * masterRes * masterRes, 0.0f);
  std::vector<float> volTemp(masterRes * masterRes * masterRes, 0.0f);
  std::vector<float> volLevel(masterRes * masterRes * masterRes, -1.0f);
  
  auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
  
  // Fill volume with AMR data, prioritizing higher refinement levels
  for (unsigned lvl = minActiveLevel; lvl <= maxActiveLevel; ++lvl) {
    for (const auto& inst : m_instancesByLevel[lvl]) {
      // Positions should already be in [0,1] after wrapping
      glm::vec3 c = inst.center;
      c.x = std::min(std::max(c.x, 0.0f), 1.0f);
      c.y = std::min(std::max(c.y, 0.0f), 1.0f);
      c.z = std::min(std::max(c.z, 0.0f), 1.0f);
      
      // Use higher sampling resolution for higher AMR levels
      float levelScale = 1.0f + 0.5f * (lvl - minActiveLevel);
      int effectiveRes = (int)(masterRes * levelScale / (maxActiveLevel - minActiveLevel + 1));
      effectiveRes = std::min(effectiveRes, masterRes);
      
      float gx = c.x * masterRes;
      float gy = c.y * masterRes;
      float gz = c.z * masterRes;
      float gh = inst.halfSize * masterRes;
      
      int xmin = clampi((int)std::floor(gx - gh), 0, masterRes - 1);
      int xmax = clampi((int)std::ceil(gx + gh), 0, masterRes - 1);
      int ymin = clampi((int)std::floor(gy - gh), 0, masterRes - 1);
      int ymax = clampi((int)std::ceil(gy + gh), 0, masterRes - 1);
      int zmin = clampi((int)std::floor(gz - gh), 0, masterRes - 1);
      int zmax = clampi((int)std::ceil(gz + gh), 0, masterRes - 1);
      
      for (int z = zmin; z <= zmax; ++z) {
        for (int y = ymin; y <= ymax; ++y) {
          for (int x = xmin; x <= xmax; ++x) {
            size_t idx = ((size_t)z * masterRes + y) * masterRes + x;
            // Only overwrite if this is a higher refinement level
            if (volLevel[idx] < 0.0f || volLevel[idx] <= (float)lvl) {
              volRho[idx] = inst.density;
              volTemp[idx] = inst.temperature;
              volLevel[idx] = (float)lvl;
            }
          }
        }
      }
    }
  }
  
  // Skip smoothing for large volumes to improve performance
  bool applySmoothing = (masterRes <= 128);
  std::vector<float>* finalRho = &volRho;
  std::vector<float>* finalTemp = &volTemp;
  std::vector<float> smoothRho, smoothTemp;
  
  if (applySmoothing) {
    // Apply light smoothing to blend between levels
    smoothRho = volRho;
    smoothTemp = volTemp;
    // Only smooth at level boundaries, not entire volume
    for (int z = 1; z < masterRes - 1; z += 2) {  // Skip every other voxel for speed
      for (int y = 1; y < masterRes - 1; y += 2) {
        for (int x = 1; x < masterRes - 1; x += 2) {
          size_t idx = ((size_t)z * masterRes + y) * masterRes + x;
          if (volLevel[idx] >= 0.0f) {
            // Simple 6-neighbor average for speed
            float neighborSum = 0.0f;
            float tempSum = 0.0f;
            int count = 0;
            const int offsets[6][3] = {{-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}};
            for (int i = 0; i < 6; ++i) {
              int nx = x + offsets[i][0];
              int ny = y + offsets[i][1];
              int nz = z + offsets[i][2];
              if (nx >= 0 && nx < masterRes && ny >= 0 && ny < masterRes && nz >= 0 && nz < masterRes) {
                size_t nidx = ((size_t)nz * masterRes + ny) * masterRes + nx;
                if (volLevel[nidx] >= 0.0f) {
                  neighborSum += volRho[nidx];
                  tempSum += volTemp[nidx];
                  count++;
                }
              }
            }
            if (count > 0) {
              smoothRho[idx] = 0.8f * volRho[idx] + 0.2f * (neighborSum / count);
              smoothTemp[idx] = 0.8f * volTemp[idx] + 0.2f * (tempSum / count);
            }
          }
        }
      }
    }
    finalRho = &smoothRho;
    finalTemp = &smoothTemp;
  }
  
  // Create the high-resolution texture
  if (m_volumeTexDensity == 0) glGenTextures(1, &m_volumeTexDensity);
  glBindTexture(GL_TEXTURE_3D, m_volumeTexDensity);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, masterRes, masterRes, masterRes, 0, GL_RED, GL_FLOAT, finalRho->data());
  
  if (m_volumeTexTemp == 0) glGenTextures(1, &m_volumeTexTemp);
  glBindTexture(GL_TEXTURE_3D, m_volumeTexTemp);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, masterRes, masterRes, masterRes, 0, GL_RED, GL_FLOAT, finalTemp->data());
  
  m_volumeResolution = masterRes;
  std::cout << "[HydroRenderer] Created adaptive volume texture with resolution " << masterRes << "³\n";
  std::cout << "[HydroRenderer] AMR levels " << minActiveLevel << " to " << maxActiveLevel << " active\n";
}
