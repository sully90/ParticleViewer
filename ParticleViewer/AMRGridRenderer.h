#pragma once

#include <vector>
#include <string>
#include <memory>

#include "include/ramses/RAMSES_info.hh"
#include "include/ramses/RAMSES_amr_data.hh"
#include "include/Shader.h"

#include <glm/glm.hpp>

// Simple AMR grid wireframe renderer. Loads amr_* from the same snapshot
// path (from info_XXXXX.txt) and draws child cell centers as lines/boxes.
class AMRGridRenderer {
public:
  enum class NormalizationMode { Auto = 0, UnitCube = 1, Boxlen = 2 };
  explicit AMRGridRenderer(const std::string& infoFilePath);
  ~AMRGridRenderer();

  // Build line geometry for a subset of levels [minLevel, maxLevel]
  void build(unsigned minLevel, unsigned maxLevel);

  // Draw with provided view/projection matrices
  void draw(const glm::mat4& view, const glm::mat4& proj);

  // Toggle visibility
  void setVisible(bool v) { m_visible = v; }
  bool isVisible() const { return m_visible; }
  void setNormalizationMode(NormalizationMode m) { m_normMode = m; }
  NormalizationMode getNormalizationMode() const { return m_normMode; }
  void setTargetBounds(const glm::vec3& minp, const glm::vec3& maxp) { m_targetMin = minp; m_targetMax = maxp; }

private:
  bool m_visible{false};

  // GL resources
  unsigned int m_vao{0}, m_vbo{0};
  size_t m_numLines{0};
  unsigned m_minBuiltLevel{1}, m_maxBuiltLevel{1};

  std::unique_ptr<RAMSES::snapshot> m_snap;

  // Minimal shader for grid lines
  std::unique_ptr<Shader> m_shader; // expects grid.vs/grid.frag


  // Helper: add AABB as 12 line segments with level value per vertex
  void appendBoxLines(const glm::vec3& minp, const glm::vec3& maxp, float level, std::vector<glm::vec4>& out);
  NormalizationMode m_normMode{NormalizationMode::Boxlen};
  glm::vec3 m_targetMin{0.0f};
  glm::vec3 m_targetMax{1.0f};
};
