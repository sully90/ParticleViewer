// Std. Includes
#include <string>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <cstring>
#include <algorithm>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

// GLEW
#include <GL/glew.h>

// GLFW
#include <GLFW/glfw3.h>

// GL includes
#include "Display.h"
#include "Shader.h"
#include "Camera.h"
#include "RAMSES_Particle_Manager.h"
#include "AMRGridRenderer.h"
#include "HydroRenderer.h"
// Global flags/pointers for toggles
static AMRGridRenderer* g_grid = nullptr;
static HydroRenderer* g_hydro = nullptr;
// Shared AMR level controls
static int g_minLevel = 1;
static int g_maxLevel = 7;
static bool g_particlesVisible = true;
static bool g_starsVisible = true;
static int g_starCount = 0;
static bool g_showUnitCube = false;

// GLM Mathemtics
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Other Libs
// (SOIL was previously included but is not used and has been removed.)

// Properties
GLuint screenWidth = 1920, screenHeight = 1080;

// Function prototypes
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void Do_Movement();
static void SaveScreenshotPPM(const std::string& directory, GLFWwindow* window);

// Camera
Camera camera(glm::vec3(0.5f, 0.5f, 1.5f));
bool keys[1024];
GLfloat lastX = 400, lastY = 300;
bool firstMouse = true;
static bool g_requestScreenshot = false;

GLfloat deltaTime = 0.0f;
GLfloat lastFrame = 0.0f;
const GLfloat POINT_SIZE = 6.0f;

// The MAIN function, from here we start our application and run our Game loop
int main()
{
	// Init GLFW
	Display display(screenWidth, screenHeight, "ParticleViewer");

	std::string fname = "C:\\Users\\dsull\\Downloads\\output_00080\\info_00080.txt";
	//std::string fname = "C:\\Users\\dsull\\Downloads\\DICEGalaxyDisk_nonCosmological\\output_00002\\info_00002.txt";
    RAMSES_Particle_Manager partManager(fname);
    // Limits: -1 means no limit. Adjust to throttle rendering load.
    const long long kMaxParticles = -1;
    const long long kMaxHydroCells = -1;
    partManager.setMaxParticles(kMaxParticles);
	display.Create();

	// Set the required callback functions
	glfwSetKeyCallback(display.getWindow(), key_callback);
	glfwSetCursorPosCallback(display.getWindow(), mouse_callback);
	glfwSetScrollCallback(display.getWindow(), scroll_callback);

	// Options
	glfwSetInputMode(display.getWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// Setup some OpenGL options
	glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    // Additive blending to accumulate density contributions
    glBlendFunc(GL_ONE, GL_ONE);

	// Setup and compile our shaders
	Shader ourShader("./resources/shaders/particle.vs", "./resources/shaders/particle.frag");
    Shader unitGridShader("./resources/shaders/grid.vs", "./resources/shaders/grid.frag");

    // Initialize hydro first to get default AMR levels from snapshot header
    HydroRenderer hydro(fname);
    g_minLevel = std::max((int)hydro.defaultMinLevel() - 2, 1);
    g_maxLevel = (int)hydro.defaultMaxLevel();
    hydro.setMaxCells(kMaxHydroCells);
    // Include coarse levels so regions without refinement still show gas
    hydro.build(g_minLevel, g_maxLevel);
    hydro.setVisible(true);
    g_hydro = &hydro;

    // AMR grid renderer, use same initial levels for alignment
    AMRGridRenderer grid(fname);
    // Keep hydro and grid normalization modes aligned (auto by default)
    grid.setNormalizationMode(AMRGridRenderer::NormalizationMode::Auto);
    grid.build(g_minLevel, g_maxLevel);
    grid.setVisible(false);
    g_grid = &grid;
    
    // Print controls help
    std::cout << "\n========== CONTROLS ==========\n";
    std::cout << "Camera:\n";
    std::cout << "  WASD - Move horizontally\n";
    std::cout << "  Shift/Ctrl - Move up/down\n";
    std::cout << "  Q/E - Rotate left/right\n";
    std::cout << "  Mouse - Look around\n";
    std::cout << "\nVisualization:\n";
    std::cout << "  H - Toggle hydro density\n";
    std::cout << "  G - Toggle AMR grid\n";
    std::cout << "  P - Toggle particles\n";
    std::cout << "  T - Toggle stars\n";
    std::cout << "  J - Toggle temperature/density\n";
    std::cout << "  L - Show AMR levels\n";
    std::cout << "  R - Toggle adaptive resolution\n";
    std::cout << "\nDensity Adjustments:\n";
    std::cout << "  9/0 - Decrease/increase min threshold\n";
    std::cout << "  ,/. - Decrease/increase opacity (sigma)\n";
    std::cout << "  ;/' - Decrease/increase brightness\n";
    std::cout << "  V - Debug visualization mode\n";
    std::cout << "\nAMR Levels:\n";
    std::cout << "  +/- - Adjust max level\n";
    std::cout << "  [/] - Adjust min level\n";
    std::cout << "==============================\n\n";

	GLfloat *vertices = partManager.particlesArray();

	glEnable(GL_POINTS);
	//glTexEnvi(GL_POINTS, GL_COORD_REPLACE, GL_TRUE);
	//glEnable(GL_VERTEX_PROGRAM_POINT_SIZE_NV);
	glPointSize(POINT_SIZE);

GLuint VBO, VAO;
GLuint VBOStars = 0, VAOStars = 0;
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	// Bind our Vertex Array Object first, then bind and set our buffers and pointers.
	glBindVertexArray(VAO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
    std::cout << "Preparing VBOs for DM and stars" << std::endl;
    // Build compact arrays for DM and star particles
    std::vector<GLfloat> dmPos; dmPos.reserve(partManager.npartDraw * 3);
    std::vector<GLfloat> starPos; starPos.reserve(partManager.npartDraw * 3);
    for (int i = 0; i < partManager.npartDraw; ++i) {
        const auto &p = partManager.mParticleArray[i];
        if (p.isStar) {
            starPos.push_back(p.position.x);
            starPos.push_back(p.position.y);
            starPos.push_back(p.position.z);
        } else {
            dmPos.push_back(p.position.x);
            dmPos.push_back(p.position.y);
            dmPos.push_back(p.position.z);
        }
    }
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * dmPos.size(), dmPos.data(), GL_STATIC_DRAW);
    std::cout << "Loaded DM positions: " << dmPos.size() / 3 << ", stars: " << starPos.size() / 3 << std::endl;
    delete [] vertices;

	// Position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (GLvoid*)0);
	glEnableVertexAttribArray(0);

    glBindVertexArray(0); // Unbind VAO

    // Unit cube wireframe (24 vertices = 12 line segments)
    GLuint VAOUnit = 0, VBOUnit = 0;
    glGenVertexArrays(1, &VAOUnit);
    glGenBuffers(1, &VBOUnit);
    glBindVertexArray(VAOUnit);
    struct Vec4 { float x,y,z,w; };
    std::vector<Vec4> unitLines = {
        // Bottom square (z=0)
        {0,0,0,(float)g_maxLevel}, {1,0,0,(float)g_maxLevel},
        {1,0,0,(float)g_maxLevel}, {1,1,0,(float)g_maxLevel},
        {1,1,0,(float)g_maxLevel}, {0,1,0,(float)g_maxLevel},
        {0,1,0,(float)g_maxLevel}, {0,0,0,(float)g_maxLevel},
        // Top square (z=1)
        {0,0,1,(float)g_maxLevel}, {1,0,1,(float)g_maxLevel},
        {1,0,1,(float)g_maxLevel}, {1,1,1,(float)g_maxLevel},
        {1,1,1,(float)g_maxLevel}, {0,1,1,(float)g_maxLevel},
        {0,1,1,(float)g_maxLevel}, {0,0,1,(float)g_maxLevel},
        // Vertical edges
        {0,0,0,(float)g_maxLevel}, {0,0,1,(float)g_maxLevel},
        {1,0,0,(float)g_maxLevel}, {1,0,1,(float)g_maxLevel},
        {1,1,0,(float)g_maxLevel}, {1,1,1,(float)g_maxLevel},
        {0,1,0,(float)g_maxLevel}, {0,1,1,(float)g_maxLevel}
    };
    glBindBuffer(GL_ARRAY_BUFFER, VBOUnit);
    glBufferData(GL_ARRAY_BUFFER, unitLines.size()*sizeof(Vec4), unitLines.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vec4), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Vec4), (void*)offsetof(Vec4,w));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // VAO/VBO for star particles
    glGenVertexArrays(1, &VAOStars);
    glGenBuffers(1, &VBOStars);
    glBindVertexArray(VAOStars);
    glBindBuffer(GL_ARRAY_BUFFER, VBOStars);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * starPos.size(), starPos.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (GLvoid*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    GLsizei dmCount = static_cast<GLsizei>(dmPos.size() / 3);
    GLsizei starCount = static_cast<GLsizei>(starPos.size() / 3);
    g_starCount = (int)starCount;

	// Game loop
	while (!display.ShouldClose())
	{
		// Set frame time
		GLfloat currentFrame = glfwGetTime();
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		// Check and call events
		glfwPollEvents();
		Do_Movement();

		// Clear the colorbuffer
		glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Draw our first triangle
		ourShader.Use();

		// Create camera transformation
		glm::mat4 view;
		view = camera.GetViewMatrix();
		glm::mat4 projection;
		projection = glm::perspective(camera.Zoom, (float)screenWidth / (float)screenHeight, 0.1f, 1000.0f);
		// Get the uniform locations
		GLint modelLoc = glGetUniformLocation(ourShader.Program, "model");
		GLint viewLoc = glGetUniformLocation(ourShader.Program, "view");
		GLint projLoc = glGetUniformLocation(ourShader.Program, "projection");
        // Compute point scale from FOV and viewport height (approximate)
        float fovRadians = camera.Zoom;
        float pointScale = (float)screenHeight / (2.0f * tanf(fovRadians * 0.5f));
        GLint baseSizeLoc = glGetUniformLocation(ourShader.Program, "uPointBaseSize");
        GLint scaleLoc = glGetUniformLocation(ourShader.Program, "uPointScale");
		glUniform1f(baseSizeLoc, POINT_SIZE);
		glUniform1f(scaleLoc, pointScale);

        // Density splat shader params
        GLint sigmaLoc = glGetUniformLocation(ourShader.Program, "uSigma");
        GLint intenLoc = glGetUniformLocation(ourShader.Program, "uIntensityScale");
        glUniform1f(sigmaLoc, 6.0f);         // Slightly tighter core
        glUniform1f(intenLoc, 0.050f);       // Lower particle brightness so gas dominates

		// Pass the matrices to the shader
		glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

        if (g_particlesVisible) {
            glm::mat4 model(1.0f);
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

            // Draw non-star particles (warm orange)
            glBindVertexArray(VAO);
            GLint colorLocP = glGetUniformLocation(ourShader.Program, "uColor");
            glUniform3f(colorLocP, 1.00f, 0.55f, 0.10f);
            glDrawArrays(GL_POINTS, 0, dmCount);
            glBindVertexArray(0);

            // Optionally draw star particles differently (bright white/blue)
            if (g_starsVisible && VAOStars && starCount > 0) {
                glBindVertexArray(VAOStars);
                // Larger point size and blue color for stars
                glUniform3f(colorLocP, 0.65f, 0.80f, 1.0f);
                GLint baseSizeLoc2 = glGetUniformLocation(ourShader.Program, "uPointBaseSize");
                GLint timeLoc = glGetUniformLocation(ourShader.Program, "uTime");
                GLint twinkleLoc = glGetUniformLocation(ourShader.Program, "uTwinkleAmp");
                glUniform1f(baseSizeLoc2, POINT_SIZE * 1.8f);
                glUniform1f(timeLoc, (float)glfwGetTime());
                glUniform1f(twinkleLoc, 0.25f);
                glDrawArrays(GL_POINTS, 0, starCount);
                // Restore base size and twinkle off for next draws
                glUniform1f(baseSizeLoc2, POINT_SIZE);
                glUniform1f(twinkleLoc, 0.0f);
                glBindVertexArray(0);
            }
        }

        // Draw hydro first so particles glow over gas
        hydro.draw(view, projection);

        // Draw AMR grid if visible (overlay lines)
        grid.draw(view, projection);

        // Optional unit-cube wireframe overlay
        if (g_showUnitCube) {
            unitGridShader.Use();
            GLint viewLocG = glGetUniformLocation(unitGridShader.Program, "view");
            GLint projLocG = glGetUniformLocation(unitGridShader.Program, "projection");
            GLint colorLocG = glGetUniformLocation(unitGridShader.Program, "uColor");
            GLint lminLocG = glGetUniformLocation(unitGridShader.Program, "uLevelMin");
            GLint lmaxLocG = glGetUniformLocation(unitGridShader.Program, "uLevelMax");
            glUniformMatrix4fv(viewLocG, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(projLocG, 1, GL_FALSE, glm::value_ptr(projection));
            glUniform3f(colorLocG, 0.2f, 0.8f, 0.2f);
            glUniform1f(lminLocG, (float)g_minLevel);
            glUniform1f(lmaxLocG, (float)g_maxLevel);
            glBindVertexArray(VAOUnit);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDrawArrays(GL_LINES, 0, (GLsizei)unitLines.size());
            glBindVertexArray(0);
            glBlendFunc(GL_ONE, GL_ONE); // restore additive for other passes
        }

		// Handle requested screenshot (capture before swapping buffers)
		if (g_requestScreenshot)
		{
			SaveScreenshotPPM("images", display.getWindow());
			g_requestScreenshot = false;
		}

		// Swap the buffers
		display.SwapBuffers();
	}
	// Properly de-allocate all resources once they've outlived their purpose
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
    glDeleteVertexArrays(1, &VAOUnit);
    glDeleteBuffers(1, &VBOUnit);
	//glfwTerminate();  // Called in display destructor
	return 0;
}

// Moves/alters the camera positions based on user input
void Do_Movement()
{
	// Camera controls
	if (keys[GLFW_KEY_W])
		camera.ProcessKeyboard(FORWARD, deltaTime);
	if (keys[GLFW_KEY_S])
		camera.ProcessKeyboard(BACKWARD, deltaTime);
	if (keys[GLFW_KEY_A])
		camera.ProcessKeyboard(LEFT, deltaTime);
	if (keys[GLFW_KEY_D])
		camera.ProcessKeyboard(RIGHT, deltaTime);
	if (keys[GLFW_KEY_LEFT_SHIFT] || keys[GLFW_KEY_RIGHT_SHIFT])
		camera.ProcessKeyboard(UP, deltaTime);
    if (keys[GLFW_KEY_LEFT_CONTROL] || keys[GLFW_KEY_RIGHT_CONTROL])
        camera.ProcessKeyboard(DOWN, deltaTime);
	if (keys[GLFW_KEY_Q])
		camera.ProcessKeyboard(ROTATE_LEFT, deltaTime);
	if (keys[GLFW_KEY_E])
		camera.ProcessKeyboard(ROTATE_RIGHT, deltaTime);
}

// Is called whenever a key is pressed/released via GLFW
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
	//cout << key << endl;
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GL_TRUE);

    // Toggle AMR grid with 'G'
    if (key == GLFW_KEY_G && action == GLFW_PRESS)
    {
        if (g_grid) g_grid->setVisible(!g_grid->isVisible());
    }

    // Toggle Hydro (density) with 'H'
    if (key == GLFW_KEY_H && action == GLFW_PRESS)
    {
        if (g_hydro) g_hydro->setVisible(!g_hydro->isVisible());
    }

    // Toggle Particles with 'P'
    if (key == GLFW_KEY_P && action == GLFW_PRESS)
    {
        g_particlesVisible = !g_particlesVisible;
    }

    // Adjust max AMR level with + and - keys
    if ((key == GLFW_KEY_KP_ADD || key == GLFW_KEY_EQUAL) && action == GLFW_PRESS)
    {
        // '=' is often shift+'+' on keyboards; accept it as increase
        g_maxLevel = std::min(g_maxLevel + 1, 25); // hard cap
        // Rebuild with new levels
        if (g_grid) g_grid->build(g_minLevel, g_maxLevel);
        if (g_hydro) g_hydro->build(g_minLevel, g_maxLevel);
        std::cout << "AMR max level -> " << g_maxLevel << std::endl;
    }
    if ((key == GLFW_KEY_KP_SUBTRACT || key == GLFW_KEY_MINUS) && action == GLFW_PRESS)
    {
        g_maxLevel = std::max(g_maxLevel - 1, g_minLevel);
        if (g_grid) g_grid->build(g_minLevel, g_maxLevel);
        if (g_hydro) g_hydro->build(g_minLevel, g_maxLevel);
        std::cout << "AMR max level -> " << g_maxLevel << std::endl;
    }

    // Adjust min AMR level with [ and ]
    if (key == GLFW_KEY_LEFT_BRACKET && action == GLFW_PRESS)
    {
        g_minLevel = std::max(g_minLevel - 1, 1);
        g_maxLevel = std::max(g_maxLevel, g_minLevel);
        if (g_grid) g_grid->build(g_minLevel, g_maxLevel);
        if (g_hydro) g_hydro->build(g_minLevel, g_maxLevel);
        std::cout << "AMR min level -> " << g_minLevel << std::endl;
    }
    if (key == GLFW_KEY_RIGHT_BRACKET && action == GLFW_PRESS)
    {
        g_minLevel = std::min(g_minLevel + 1, g_maxLevel);
        if (g_grid) g_grid->build(g_minLevel, g_maxLevel);
        if (g_hydro) g_hydro->build(g_minLevel, g_maxLevel);
        std::cout << "AMR min level -> " << g_minLevel << std::endl;
    }

    // Toggle stars with 'T'; toggle hydro temperature/density with 'J'
    if (key == GLFW_KEY_T && action == GLFW_PRESS)
    {
        g_starsVisible = !g_starsVisible;
        if (g_starsVisible) {
            std::cout << "Stars: ON (" << g_starCount << ")" << std::endl;
        } else {
            std::cout << "Stars: OFF" << std::endl;
        }
    }
    if (key == GLFW_KEY_J && action == GLFW_PRESS)
    {
        if (g_hydro) {
            g_hydro->toggleTemperature();
            std::cout << (g_hydro->isShowingTemperature() ? "Hydro: Temperature mode" : "Hydro: Density mode") << std::endl;
        }
    }
    // 'N' cycles normalization: Auto -> UnitCube -> Boxlen
    if (key == GLFW_KEY_N && action == GLFW_PRESS)
    {
        if (g_hydro && g_grid) {
            auto cur = g_hydro->getNormalizationMode();
            HydroRenderer::NormalizationMode next = HydroRenderer::NormalizationMode::Auto;
            if (cur == HydroRenderer::NormalizationMode::Auto) next = HydroRenderer::NormalizationMode::UnitCube;
            else if (cur == HydroRenderer::NormalizationMode::UnitCube) next = HydroRenderer::NormalizationMode::Boxlen;
            else next = HydroRenderer::NormalizationMode::Auto;
            g_hydro->setNormalizationMode(next);
            // Mirror to grid
            AMRGridRenderer::NormalizationMode nextG = AMRGridRenderer::NormalizationMode::Auto;
            if (next == HydroRenderer::NormalizationMode::UnitCube) nextG = AMRGridRenderer::NormalizationMode::UnitCube;
            else if (next == HydroRenderer::NormalizationMode::Boxlen) nextG = AMRGridRenderer::NormalizationMode::Boxlen;
            else nextG = AMRGridRenderer::NormalizationMode::Auto;
            g_grid->setNormalizationMode(nextG);
            // Rebuild with new normalization
            g_grid->build(g_minLevel, g_maxLevel);
            g_hydro->build(g_minLevel, g_maxLevel);
            const char* label = (next == HydroRenderer::NormalizationMode::Auto) ? "Auto" : (next == HydroRenderer::NormalizationMode::UnitCube ? "UnitCube" : "Boxlen");
            std::cout << "Normalization mode -> " << label << std::endl;
        }
    }
    // 'R' toggle adaptive resolution
    if (key == GLFW_KEY_R && action == GLFW_PRESS)
    {
        if (g_hydro) {
            g_hydro->setAdaptiveResolution(!g_hydro->isAdaptiveResolution());
            g_hydro->build(g_minLevel, g_maxLevel);
            std::cout << "Adaptive resolution: " << (g_hydro->isAdaptiveResolution() ? "ON" : "OFF") << std::endl;
        }
    }
    
    // 'L' toggle AMR level visualization
    if (key == GLFW_KEY_L && action == GLFW_PRESS)
    {
        if (g_hydro) {
            g_hydro->toggleAMRLevels();
            std::cout << "AMR level visualization: " << (g_hydro->isShowingAMRLevels() ? "ON" : "OFF") << std::endl;
        }
    }
    
    // 'V' toggle debug/visualization mode for density
    if (key == GLFW_KEY_V && action == GLFW_PRESS)
    {
        if (g_hydro) {
            g_hydro->toggleDebugMode();
            std::cout << "Debug visualization: " << (g_hydro->isDebugMode() ? "ON - showing raw density" : "OFF") << std::endl;
        }
    }
    
    // '9' and '0' to adjust density min threshold
    if (key == GLFW_KEY_9 && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        if (g_hydro) {
            g_hydro->scaleMinDensity(0.5f);  // Decrease min (wider range)
            std::cout << "Density min: " << std::scientific << g_hydro->getRhoMin() 
                      << " (range: " << (g_hydro->getRhoMax() / g_hydro->getRhoMin()) << "x)" << std::fixed << std::endl;
        }
    }
    if (key == GLFW_KEY_0 && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        if (g_hydro) {
            g_hydro->scaleMinDensity(2.0f);  // Increase min (narrower range)
            std::cout << "Density min: " << std::scientific << g_hydro->getRhoMin() 
                      << " (range: " << (g_hydro->getRhoMax() / g_hydro->getRhoMin()) << "x)" << std::fixed << std::endl;
        }
    }
    
    // ',' and '.' to adjust opacity (sigma)
    if (key == GLFW_KEY_COMMA && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        if (g_hydro) {
            g_hydro->adjustSigma(0.7f);  // Decrease opacity
            std::cout << "Opacity (sigma): " << g_hydro->getSigma() << std::endl;
        }
    }
    if (key == GLFW_KEY_PERIOD && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        if (g_hydro) {
            g_hydro->adjustSigma(1.5f);  // Increase opacity
            std::cout << "Opacity (sigma): " << g_hydro->getSigma() << std::endl;
        }
    }
    
    // ';' and ''' to adjust brightness (exposure)
    if (key == GLFW_KEY_SEMICOLON && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        if (g_hydro) {
            g_hydro->adjustExposure(0.7f);  // Decrease brightness
            std::cout << "Brightness (exposure): " << g_hydro->getExposure() << std::endl;
        }
    }
    if (key == GLFW_KEY_APOSTROPHE && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        if (g_hydro) {
            g_hydro->adjustExposure(1.5f);  // Increase brightness
            std::cout << "Brightness (exposure): " << g_hydro->getExposure() << std::endl;
        }
    }
    
    // 'U' toggle unit cube wireframe
    if (key == GLFW_KEY_U && action == GLFW_PRESS)
    {
        g_showUnitCube = !g_showUnitCube;
        std::cout << (g_showUnitCube ? "Unit cube: ON" : "Unit cube: OFF") << std::endl;
    }

    // Adjust density minimum threshold: ',' decrease, '.' increase
    if (key == GLFW_KEY_COMMA && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        if (g_hydro) {
            g_hydro->scaleMinDensity(0.8f);
            std::cout << "rhoMin -> " << g_hydro->getRhoMin() << std::endl;
        }
    }
    if (key == GLFW_KEY_PERIOD && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        if (g_hydro) {
            g_hydro->scaleMinDensity(1.25f);
            std::cout << "rhoMin -> " << g_hydro->getRhoMin() << std::endl;
        }
    }

    // Screenshot with 'C'
    if (key == GLFW_KEY_C && action == GLFW_PRESS)
    {
        g_requestScreenshot = true;
    }

	if (action == GLFW_PRESS)
		keys[key] = true;
	else if (action == GLFW_RELEASE)
		keys[key] = false;
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
	if (firstMouse)
	{
		lastX = xpos;
		lastY = ypos;
		firstMouse = false;
	}

	GLfloat xoffset = xpos - lastX;
	GLfloat yoffset = lastY - ypos;  // Reversed since y-coordinates go from bottom to left

	lastX = xpos;
	lastY = ypos;

	camera.ProcessMouseMovement(xoffset, yoffset);
}


void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	camera.ProcessMouseScroll(yoffset);
}

// Reads back the current back buffer and writes a binary PPM to directory
static void SaveScreenshotPPM(const std::string& directory, GLFWwindow* window)
{
    // Ensure output directory exists
#ifdef _WIN32
    _mkdir(directory.c_str()); // no error if exists
#else
    mkdir(directory.c_str(), 0755);
#endif

    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        std::cerr << "Screenshot skipped: invalid framebuffer size" << std::endl;
        return;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically to top-left origin
    std::vector<unsigned char> flipped(pixels.size());
    const size_t rowSize = static_cast<size_t>(width) * 3u;
    for (int y = 0; y < height; ++y) {
        const size_t srcRow = static_cast<size_t>(y) * rowSize;
        const size_t dstRow = static_cast<size_t>(height - 1 - y) * rowSize;
        std::memcpy(flipped.data() + dstRow, pixels.data() + srcRow, rowSize);
    }

    // Timestamped filename
    std::time_t t = std::time(nullptr);
    std::tm tmStruct;
#ifdef _WIN32
    localtime_s(&tmStruct, &t);
#else
    localtime_r(&t, &tmStruct);
#endif
    std::ostringstream name;
    name << directory << "/screenshot_"
         << std::put_time(&tmStruct, "%Y%m%d_%H%M%S")
         << ".ppm";

    std::ofstream out(name.str(), std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open screenshot file: " << name.str() << std::endl;
        return;
    }
    out << "P6\n" << width << ' ' << height << "\n255\n";
    out.write(reinterpret_cast<const char*>(flipped.data()), static_cast<std::streamsize>(flipped.size()));
    out.close();
    std::cout << "Saved screenshot: " << name.str() << std::endl;
}