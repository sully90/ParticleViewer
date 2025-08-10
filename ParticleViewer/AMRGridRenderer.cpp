
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

void AMRGridRenderer::appendBoxLines(const glm::vec3& minp, const glm::vec3& maxp, float level, std::vector<glm::vec4>& out) {
  glm::vec3 v[8] = {
    {minp.x, minp.y, minp.z}, {maxp.x, minp.y, minp.z}, {maxp.x, maxp.y, minp.z}, {minp.x, maxp.y, minp.z},
    {minp.x, minp.y, maxp.z}, {maxp.x, minp.y, maxp.z}, {maxp.x, maxp.y, maxp.z}, {minp.x, maxp.y, maxp.z}
  };
  int e[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
  for (int i=0;i<12;++i){
    glm::vec3 a = v[e[i][0]]; glm::vec3 b = v[e[i][1]];
    out.emplace_back(a, level);
    out.emplace_back(b, level);
  }
}

void AMRGridRenderer::build(unsigned minLevel, unsigned maxLevel) {
  // Build AMR trees for all domains to cover full box
  unsigned ilevelMax = std::min<unsigned>(maxLevel, m_snap->m_header.levelmax);

  // We'll compute bounds if needed for Fit mode
  float invBoxlen_sample = 1.0f;
  bool needSample = (m_normMode != NormalizationMode::UnitCube);
  float sampleMax = 0.0f;
  glm::vec3 posMin(1e9f), posMax(-1e9f);

  std::vector<glm::vec4> lineVerts;
  lineVerts.reserve(1 << 20);
  // Iterate domains and levels to gather and append
  for (int icpu = 1; icpu <= (int)m_snap->m_header.ncpu; ++icpu) {
    RAMSES::AMR::tree<RAMSES::AMR::cell_locally_essential<>, RAMSES::AMR::level<RAMSES::AMR::cell_locally_essential<>>> tree(*m_snap, icpu, ilevelMax, minLevel);
    tree.read();
    for (unsigned lvl=minLevel; lvl<=ilevelMax; ++lvl) {
      float halfSizeBase = (0.5f / std::pow(2.0f, (float)lvl + 1.0f));
      for (auto it = tree.begin(lvl); it != tree.end(lvl); ++it) {
        for (int c = 0; c < 8; ++c) {
          auto cp = tree.cell_pos<float>(it, c);
          glm::vec3 raw(cp.x, cp.y, cp.z);
          if (needSample) {
            sampleMax = std::max(sampleMax, std::max(raw.x, std::max(raw.y, raw.z)));
            posMin = glm::min(posMin, raw);
            posMax = glm::max(posMax, raw);
          }
          // We'll append after we decide normalization
        }
      }
    }
  }

  // AMR centers are in unit cube already; map [0,1] to [m_targetMin,m_targetMax]
  float invBoxlen = 1.0f;
  glm::vec3 targetExtent = m_targetMax - m_targetMin;
  if (targetExtent.x <= 0 || targetExtent.y <= 0 || targetExtent.z <= 0) targetExtent = glm::vec3(1.0f);

  // If AMR domain appears wrapped/offset near integer boundaries, shift by floor of center
  glm::vec3 domainCenter = 0.5f * (posMin + posMax);
  glm::vec3 domainShift = glm::floor(domainCenter + glm::vec3(0.0f));

  // Second pass to actually append with normalization
  for (int icpu = 1; icpu <= (int)m_snap->m_header.ncpu; ++icpu) {
    RAMSES::AMR::tree<RAMSES::AMR::cell_locally_essential<>, RAMSES::AMR::level<RAMSES::AMR::cell_locally_essential<>>> tree(*m_snap, icpu, ilevelMax, minLevel);
    tree.read();
    for (unsigned lvl=minLevel; lvl<=ilevelMax; ++lvl) {
      float halfSizeCode = (0.5f / std::pow(2.0f, (float)lvl + 1.0f));
      float halfSize = halfSizeCode * targetExtent.x; // uniform scale
      for (auto it = tree.begin(lvl); it != tree.end(lvl); ++it) {
        for (int c = 0; c < 8; ++c) {
          auto cp = tree.cell_pos<float>(it, c);
          glm::vec3 unit = glm::vec3(cp.x, cp.y, cp.z) - domainShift;
          glm::vec3 cc = m_targetMin + unit * targetExtent;
          glm::vec3 minp = cc - glm::vec3(halfSize);
          glm::vec3 maxp = cc + glm::vec3(halfSize);
          appendBoxLines(minp, maxp, (float)lvl, lineVerts);
        }
      }
    }
  }

  m_numLines = lineVerts.size() / 2;

  glBindVertexArray(m_vao);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferData(GL_ARRAY_BUFFER, lineVerts.size() * sizeof(glm::vec4), lineVerts.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), (void*)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), (void*)offsetof(glm::vec4, w));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);

  m_minBuiltLevel = minLevel; m_maxBuiltLevel = ilevelMax;
}

void AMRGridRenderer::draw(const glm::mat4& view, const glm::mat4& proj) {
  if (!m_visible || m_numLines == 0) return;
  m_shader->Use();
  GLint viewLoc = glGetUniformLocation(m_shader->Program, "view");
  GLint projLoc = glGetUniformLocation(m_shader->Program, "projection");
  GLint colorLoc = glGetUniformLocation(m_shader->Program, "uColor");
  GLint lminLoc = glGetUniformLocation(m_shader->Program, "uLevelMin");
  GLint lmaxLoc = glGetUniformLocation(m_shader->Program, "uLevelMax");
  glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &view[0][0]);
  glUniformMatrix4fv(projLoc, 1, GL_FALSE, &proj[0][0]);
  glUniform3f(colorLoc, 1.0f, 1.0f, 1.0f); // white grid
  glUniform1f(lminLoc, (float)m_minBuiltLevel);
  glUniform1f(lmaxLoc, (float)m_maxBuiltLevel);

  glBindVertexArray(m_vao);
  // Use standard alpha blending for grid so alpha matters
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDrawArrays(GL_LINES, 0, (GLsizei)(m_numLines * 2));
  glBindVertexArray(0);
  // Restore additive blending for other passes
  glBlendFunc(GL_ONE, GL_ONE);
}
