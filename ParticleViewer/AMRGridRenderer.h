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
  explicit AMRGridRenderer(const std::string& infoFilePath);
  ~AMRGridRenderer();

  // Build line geometry for a subset of levels [minLevel, maxLevel]
  void build(unsigned minLevel, unsigned maxLevel);

  // Draw with provided view/projection matrices
  void draw(const glm::mat4& view, const glm::mat4& proj);

  // Toggle visibility
  void setVisible(bool v) { m_visible = v; }
  bool isVisible() const { return m_visible; }

private:
  bool m_visible{false};

  // GL resources
  unsigned int m_vao{0}, m_vbo{0};
  size_t m_numLines{0};

  std::unique_ptr<RAMSES::snapshot> m_snap;

  // Minimal shader for grid lines
  std::unique_ptr<Shader> m_shader; // expects grid.vs/grid.frag


  // Helper: add AABB as 12 line segments
  void appendBoxLines(const glm::vec3& minp, const glm::vec3& maxp, std::vector<glm::vec3>& out);
};
