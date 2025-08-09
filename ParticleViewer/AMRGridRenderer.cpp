#include "AMRGridRenderer.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

AMRGridRenderer::AMRGridRenderer(const std::string& infoFilePath) {
  m_snap = std::make_unique<RAMSES::snapshot>(infoFilePath, RAMSES::version3);
  m_shader = std::make_unique<Shader>("./resources/shaders/grid.vs", "./resources/shaders/grid.frag");

  glGenVertexArrays(1, &m_vao);
  glGenBuffers(1, &m_vbo);
}

AMRGridRenderer::~AMRGridRenderer() {
  if (m_vbo) glDeleteBuffers(1, &m_vbo);
  if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

void AMRGridRenderer::appendBoxLines(const glm::vec3& minp, const glm::vec3& maxp, std::vector<glm::vec3>& out) {
  glm::vec3 v[8] = {
    {minp.x, minp.y, minp.z}, {maxp.x, minp.y, minp.z}, {maxp.x, maxp.y, minp.z}, {minp.x, maxp.y, minp.z},
    {minp.x, minp.y, maxp.z}, {maxp.x, minp.y, maxp.z}, {maxp.x, maxp.y, maxp.z}, {minp.x, maxp.y, maxp.z}
  };
  int e[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
  for (int i=0;i<12;++i){ out.push_back(v[e[i][0]]); out.push_back(v[e[i][1]]);} }

void AMRGridRenderer::build(unsigned minLevel, unsigned maxLevel) {
  // Build AMR tree for a single domain at a time (domain 1 here for simplicity)
  unsigned ilevelMax = std::min<unsigned>(maxLevel, m_snap->m_header.levelmax);
  RAMSES::AMR::tree<RAMSES::AMR::cell_locally_essential<>, RAMSES::AMR::level<RAMSES::AMR::cell_locally_essential<>>> tree(*m_snap, 1, ilevelMax, minLevel);
  tree.read();

  std::vector<glm::vec3> lineVerts;
  // Iterate levels and add each cell's AABB as lines
  for (unsigned lvl=minLevel; lvl<=ilevelMax; ++lvl) {
    for (auto it = tree.begin(lvl); it != tree.end(lvl); ++it) {
      auto gc = tree.grid_pos<float>(it);
      glm::vec3 gc3(gc.x, gc.y, gc.z);
      float dx2 = 0.5f / std::pow(2.0f, (float)lvl);
      glm::vec3 minp = gc3 - glm::vec3(dx2);
      glm::vec3 maxp = gc3 + glm::vec3(dx2);
      appendBoxLines(minp, maxp, lineVerts);
    }
  }

  m_numLines = lineVerts.size() / 2;

  glBindVertexArray(m_vao);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferData(GL_ARRAY_BUFFER, lineVerts.size() * sizeof(glm::vec3), lineVerts.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
}

void AMRGridRenderer::draw(const glm::mat4& view, const glm::mat4& proj) {
  if (!m_visible || m_numLines == 0) return;
  m_shader->Use();
  GLint viewLoc = glGetUniformLocation(m_shader->Program, "view");
  GLint projLoc = glGetUniformLocation(m_shader->Program, "projection");
  GLint colorLoc = glGetUniformLocation(m_shader->Program, "uColor");
  glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &view[0][0]);
  glUniformMatrix4fv(projLoc, 1, GL_FALSE, &proj[0][0]);
  glUniform3f(colorLoc, 0.15f, 0.8f, 0.2f); // greenish grid

  glBindVertexArray(m_vao);
  glDrawArrays(GL_LINES, 0, (GLsizei)(m_numLines * 2));
  glBindVertexArray(0);
}
