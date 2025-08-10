#pragma once

#include <vector>
#include <memory>
#include <string>

#include "include/ramses/RAMSES_info.hh"
#include "include/ramses/RAMSES_amr_data.hh"
#include "include/ramses/RAMSES_hydro_data.hh"
#include "include/Shader.h"

#include <glm/glm.hpp>

// Renders RAMSES hydro density as colored boxes using a jet colormap
class HydroRenderer {
public:
  explicit HydroRenderer(const std::string& infoFilePath);
  ~HydroRenderer();

  // Build GL buffers for density across levels [minLevel, maxLevel]
  void build(unsigned minLevel, unsigned maxLevel);

  // Defaults from snapshot header
  unsigned defaultMinLevel() const { return m_snap ? (unsigned)m_snap->m_header.levelmin : 1u; }
  unsigned defaultMaxLevel() const { return m_snap ? (unsigned)m_snap->m_header.levelmax : 1u; }

  void draw(const glm::mat4& view, const glm::mat4& proj);

  void setVisible(bool v) { m_visible = v; }
  bool isVisible() const { return m_visible; }

  // Point sprite sizing
  void setPointParams(float pointScalePixelsPerUnit, float baseSizePixels) {
    m_pointScale = pointScalePixelsPerUnit;
    m_baseSize = baseSizePixels;
  }

  // Optional: adjust filtering thresholds
  void setMinOverdensity(float v) { m_minOverdensity = v; }
  void setMaxOverdensity(float v) { m_maxOverdensity = v; }

  // Limit how many hydro cells to draw (-1 = all)
  void setMaxCells(long long maxCells) { m_maxCells = maxCells; }

private:
  bool m_visible{false};

  struct Instance {
    glm::vec3 center;
    float halfSize;
    float density;     // overdensity or SI density (based on mode)
    float temperature; // Kelvin
  };

  std::vector<Instance> m_instances;

  unsigned int m_vao{0}, m_vbo{0};
  unsigned int m_instanceVBO{0};
  size_t m_numInstances{0};
  long long m_maxCells{-1};
  float m_rhoMin{1e30f};
  float m_rhoMax{0.0f};
  float m_tempMin{1e30f};
  float m_tempMax{0.0f};

  // Track whether values stored in volume are overdensity or SI kg/m^3
  bool m_isOverdensity{true};

  // Overdensity filtering (dimensionless)
  float m_minOverdensity{0.0f};   // filter out very low-density cells
  float m_maxOverdensity{1e12f};  // cap extreme values if desired

  std::unique_ptr<RAMSES::snapshot> m_snap;
  std::unique_ptr<Shader> m_shader; // volume.vs/volume.frag

  void upload();
  void createVolumeTexturesFromCells();

  float m_pointScale{1.0f};
  float m_baseSize{6.0f};

  // 3D volume textures for density and temperature
  unsigned int m_volumeTexDensity{0};
  unsigned int m_volumeTexTemp{0};
  int m_volumeResolution{128};

  // Robust color scaling (percentile-based)
  bool m_useRobustRange{true};
  float m_lowQuantile{0.02f};   // 2% percentile
  float m_highQuantile{0.98f};  // 98% percentile

  // Toggle rendering of temperature instead of density
  bool m_showTemperature{false};
public:
  void toggleTemperature() { m_showTemperature = !m_showTemperature; }
  bool isShowingTemperature() const { return m_showTemperature; }
};
