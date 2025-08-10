#include "HydroRenderer.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <algorithm>

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

void HydroRenderer::build(unsigned minLevel, unsigned maxLevel) {
  unsigned ilevelMax = std::min<unsigned>(maxLevel, m_snap->m_header.levelmax);
  using Cell = RAMSES::AMR::cell_locally_essential<>;
  using Level = RAMSES::AMR::level<Cell>;

  // Box normalization factor to map to unit cube
  const float invBoxlen = (m_snap->m_header.boxlen > 0.0)
                            ? static_cast<float>(1.0 / m_snap->m_header.boxlen)
                            : 1.0f;

  m_instances.clear();
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
  const bool useOverdensity = (Omega_b0 > 0.0);
  m_isOverdensity = useOverdensity;
  // Adjust robust range quantiles for SI densities to avoid getting swamped by outliers
  if (!m_isOverdensity) {
    m_lowQuantile = 0.05f;
    m_highQuantile = 0.995f;
  } else {
    m_lowQuantile = 0.02f;
    m_highQuantile = 0.98f;
  }
  const double rho_bar_b = useOverdensity ? (Omega_b0 * rho_crit0 / (a * a * a)) : 1.0; // avoid zero
  const double unit_d = m_snap->m_header.unit_d; // code density to kg/m^3 (may be 0 for non-cosmo)
  const bool useCodeUnits = !(unit_d > 0.0 && std::isfinite(unit_d));
  const double densityScale = useCodeUnits ? 1.0 : unit_d;

  m_rhoMin = 1e30f; m_rhoMax = 0.0f;
  m_tempMin = 1e30f; m_tempMax = 0.0f;
  // Collect log-values for robust percentile range selection
  std::vector<float> logDensities;
  std::vector<float> logTemps;
  unsigned ncpu = m_snap->m_header.ncpu;
  for (int icpu = 1; icpu <= (int)ncpu; ++icpu) {
    RAMSES::AMR::tree<Cell, Level> tree(*m_snap, icpu, ilevelMax, minLevel);
    tree.read();
    RAMSES::HYDRO::data<decltype(tree)> hydro(tree);
    hydro.read("density");
    RAMSES::HYDRO::data<decltype(tree)> press(tree);
    press.read("pressure");
    for (unsigned lvl=minLevel; lvl<=ilevelMax; ++lvl) {
      // Child cell half-size at level lvl in unit-cube coordinates
      float halfSize = (0.5f / std::pow(2.0f, (float)lvl + 1.0f)) * invBoxlen;
      for (auto it = tree.begin(lvl); it != tree.end(lvl); ++it) {
        // 8 child cells per grid
        for (int c=0; c<8; ++c) {
          auto cp = tree.cell_pos<float>(it, c);
        // Cell center in simulation box coordinates [0, boxlen]. Grid and particles are in same units.
          glm::vec3 cc(cp.x * invBoxlen, cp.y * invBoxlen, cp.z * invBoxlen);
        // retrieve density value for this cell
          float rho_code = hydro(it, c);
          double rho_phys = (double)rho_code * densityScale;  // kg/m^3 or code units if scale==1
          float value = useOverdensity ? (float)(rho_phys / rho_bar_b)
                                       : (float)rho_phys;     // overdensity or SI
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
          // Half-size in box coordinates (child cell)
          m_instances.push_back({cc, halfSize, value, (float)T});
          if (m_maxCells >= 0) {
            ++cellsAdded;
            if (cellsAdded >= m_maxCells) {
              goto finish_build;
            }
          }
        }
      }
    }
  }
finish_build:
  if (m_useRobustRange && !logDensities.empty()) {
    // Compute robust min/max via percentiles in log space, then exponentiate
    std::sort(logDensities.begin(), logDensities.end());
    size_t n = logDensities.size();
    size_t ilow = static_cast<size_t>(std::floor(m_lowQuantile * (n - 1)));
    size_t ihigh = static_cast<size_t>(std::ceil(m_highQuantile * (n - 1)));
    float logMin = logDensities[ilow];
    float logMax = logDensities[ihigh];
    m_rhoMin = std::max(1e-30f, std::exp(logMin));
    m_rhoMax = std::max(m_rhoMin * 1.0001f, std::exp(logMax));
  } else if (!m_useRobustRange) {
    // Ensure a sane non-degenerate range
    m_rhoMin = std::max(1e-30f, m_rhoMin);
    m_rhoMax = std::max(m_rhoMin * 1.0001f, m_rhoMax);
  } else {
    // Robust requested but no positive samples; choose a default display range
    m_rhoMin = 1e-30f;
    m_rhoMax = 1e-24f;
  }

  // If the spread is still too narrow, widen it to ensure visibility
  if (m_rhoMax <= m_rhoMin * 1.5f) {
    float center = std::sqrt(m_rhoMin * m_rhoMax);
    m_rhoMin = std::max(1e-30f, center / 10.0f);
    m_rhoMax = center * 10.0f;
  }

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

  upload();
  // Build/update 3D volume textures for ray-marching
  createVolumeTexturesFromCells();
}

void HydroRenderer::upload() {
  m_numInstances = m_instances.size();

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
  GLint isOverLoc = glGetUniformLocation(m_shader->Program, "uIsOverdensity");
  GLint useTempLoc = glGetUniformLocation(m_shader->Program, "uUseTemperature");
  GLint volRhoLoc = glGetUniformLocation(m_shader->Program, "uVolumeDensity");
  GLint volTempLoc = glGetUniformLocation(m_shader->Program, "uVolumeTemp");
  GLint stepsLoc = glGetUniformLocation(m_shader->Program, "uSteps");
  GLint tfLoc = glGetUniformLocation(m_shader->Program, "uExposure");
  GLint sigmaLoc = glGetUniformLocation(m_shader->Program, "uSigma");
  glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &view[0][0]);
  glUniformMatrix4fv(projLoc, 1, GL_FALSE, &proj[0][0]);
  float rmin = std::max(1e-30f, m_rhoMin);
  float rmax = std::max(rmin * 1.0001f, m_rhoMax);
  glUniform1f(rhoMinLoc, rmin);
  glUniform1f(rhoMaxLoc, rmax);
  glUniform1f(tMinLoc, std::max(1e-30f, m_tempMin));
  glUniform1f(tMaxLoc, std::max(m_tempMin * 1.0001f, m_tempMax));
  glUniform1i(isOverLoc, m_isOverdensity ? 1 : 0);
  glUniform1i(useTempLoc, m_showTemperature ? 1 : 0);
  glUniform1i(stepsLoc, m_isOverdensity ? 256 : 384);
  glUniform1f(tfLoc, m_isOverdensity ? 1.5f : 2.5f);
  // Use a tuned sigma in SI mode because values may span many decades
  glUniform1f(sigmaLoc, m_isOverdensity ? 6.0f : 12.0f);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, m_volumeTexDensity);
  glUniform1i(volRhoLoc, 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_3D, m_volumeTexTemp);
  glUniform1i(volTempLoc, 1);

  // Disable depth for volume, enable alpha-additive compositing
  glDisable(GL_DEPTH_TEST);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);

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
  for (const auto &inst : m_instances) {
    float gx = inst.center.x * N;
    float gy = inst.center.y * N;
    float gz = inst.center.z * N;
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
        }
      }
    }
  }

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
