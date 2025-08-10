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
  enum class NormalizationMode { Auto = 0, UnitCube = 1, Boxlen = 2 };
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
  // Runtime controls for density scaling
  void scaleMinDensity(float factor);
  float getRhoMin() const { return m_rhoMin; }
  float getRhoMax() const { return m_rhoMax; }

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
    float level;       // AMR level (for sorting/culling heuristics)
  };

  std::vector<Instance> m_instances;
  // Cache of instances per level to avoid re-reading disk on min/max changes
  std::vector<std::vector<Instance>> m_instancesByLevel;
  bool m_cacheReady{false};

  unsigned int m_vao{0}, m_vbo{0};
  unsigned int m_instanceVBO{0};
  size_t m_numInstances{0};
  long long m_maxCells{-1};
  float m_rhoMin{1e30f};
  float m_rhoMax{0.0f};
  float m_tempMin{1e30f};
  float m_tempMax{0.0f};
  // Domain normalization to [0,1]^3
  glm::vec3 m_domainMin{0.0f};
  glm::vec3 m_domainScale{1.0f};

  // Track whether values stored in volume are overdensity or SI kg/m^3
  bool m_isOverdensity{true};

  // Overdensity filtering (dimensionless)
  float m_minOverdensity{0.0f};   // filter out very low-density cells
  float m_maxOverdensity{1e12f};  // cap extreme values if desired

  std::unique_ptr<RAMSES::snapshot> m_snap;
  std::unique_ptr<Shader> m_shader; // volume.vs/volume.frag

  void upload();
  void createVolumeTexturesFromCells();
  void createAdaptiveVolumeTextures();
  void buildLevelCache(unsigned minLevel, unsigned maxLevel);

  float m_pointScale{1.0f};
  float m_baseSize{6.0f};
  
  // Rendering parameters
  float m_sigma{10.0f};  // Reduced since we emphasize high density more
  float m_exposure{3.0f};  // Slightly reduced for better balance

  // 3D volume textures for density and temperature
  unsigned int m_volumeTexDensity{0};
  unsigned int m_volumeTexTemp{0};
  int m_volumeResolution{64};  // Start with lower resolution
  
  // Multi-resolution volume support
  struct VolumeLevel {
    unsigned int texDensity{0};
    unsigned int texTemp{0};
    int resolution;
    unsigned int amrLevel;
    glm::vec3 minBounds;
    glm::vec3 maxBounds;
  };
  std::vector<VolumeLevel> m_volumeLevels;
  bool m_useAdaptiveResolution{true};  // Default to adaptive for better quality
  int m_baseResolution{256};  // Lower base resolution
  int m_maxResolution{2048};  // Cap maximum resolution

  // Robust color scaling (percentile-based)
  bool m_useRobustRange{true};
  float m_lowQuantile{0.05f};   // 5% percentile - less extreme
  float m_highQuantile{0.95f};  // 95% percentile - less extreme

  // Toggle rendering of temperature instead of density
  bool m_showTemperature{false};
  
  // AMR level visualization
  bool m_showAMRLevels{false};
  float m_levelOpacity{0.7f};
  bool m_debugMode{false};
  
public:
  void toggleTemperature() { m_showTemperature = !m_showTemperature; }
  bool isShowingTemperature() const { return m_showTemperature; }
  void toggleAMRLevels() { m_showAMRLevels = !m_showAMRLevels; }
  bool isShowingAMRLevels() const { return m_showAMRLevels; }
  void setAdaptiveResolution(bool enable) { m_useAdaptiveResolution = enable; }
  bool isAdaptiveResolution() const { return m_useAdaptiveResolution; }
  void toggleDebugMode() { m_debugMode = !m_debugMode; }
  bool isDebugMode() const { return m_debugMode; }
  void adjustSigma(float factor) { m_sigma = std::max(0.1f, m_sigma * factor); }
  void adjustExposure(float factor) { m_exposure = std::max(0.1f, m_exposure * factor); }
  float getSigma() const { return m_sigma; }
  float getExposure() const { return m_exposure; }
  void setNormalizationMode(NormalizationMode m) { m_normMode = m; }
  NormalizationMode getNormalizationMode() const { return m_normMode; }

private:
  // Per-frame culled instances buffer
  std::vector<Instance> m_culled; // unused in fullscreen raymarch path
  NormalizationMode m_normMode{NormalizationMode::Auto};
};
