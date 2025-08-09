// Std. Includes
#include <string>
#include <iostream>

// GLEW
#include <GL/glew.h>

// GLFW
#include <GLFW/glfw3.h>

// GL includes
#include "Display.h"
#include "Shader.h"
#include "Camera.h"
#include "RAMSES_Particle_Manager.h"

// GLM Mathemtics
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Other Libs
// (SOIL was previously included but is not used and has been removed.)

// Properties
GLuint screenWidth = 1920/2, screenHeight = 1080/2;

// Function prototypes
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void Do_Movement();

// Camera
Camera camera(glm::vec3(0.5f, 0.5f, 1.5f));
bool keys[1024];
GLfloat lastX = 400, lastY = 300;
bool firstMouse = true;

GLfloat deltaTime = 0.0f;
GLfloat lastFrame = 0.0f;
const GLfloat POINT_SIZE = 4.0f;

// The MAIN function, from here we start our application and run our Game loop
int main()
{
	// Init GLFW
	Display display(screenWidth, screenHeight, "ParticleViewer");

	std::string fname = "C:\\Users\\dsull\\Downloads\\output_00101\\info_00101.txt";
	RAMSES_Particle_Manager partManager(fname);
	display.Create();

	// Set the required callback functions
	glfwSetKeyCallback(display.getWindow(), key_callback);
	glfwSetCursorPosCallback(display.getWindow(), mouse_callback);
	glfwSetScrollCallback(display.getWindow(), scroll_callback);

	// Options
	glfwSetInputMode(display.getWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// Setup some OpenGL options
	glEnable(GL_DEPTH_TEST);

	// Setup and compile our shaders
	Shader ourShader("./resources/shaders/particle.vs", "./resources/shaders/particle.frag");

	GLfloat *vertices = partManager.particlesArray();

	glEnable(GL_POINTS);
	//glTexEnvi(GL_POINTS, GL_COORD_REPLACE, GL_TRUE);
	//glEnable(GL_VERTEX_PROGRAM_POINT_SIZE_NV);
	glPointSize(POINT_SIZE);

	GLuint VBO, VAO;
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	// Bind our Vertex Array Object first, then bind and set our buffers and pointers.
	glBindVertexArray(VAO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	std::cout << "Preparing VBO" << std::endl;
	//glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * partManager.mParticleArray.size(), vertices, GL_STATIC_DRAW);
	glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 3 * partManager.npartDraw, vertices, GL_STATIC_DRAW);
	std::cout << "Successfully called glBufferData" << std::endl;
	delete [] vertices;

	// Position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (GLvoid*)0);
	glEnableVertexAttribArray(0);

	glBindVertexArray(0); // Unbind VAO

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
		glClearColor(0.9f, 0.9f, 0.9f, 1.0f);
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
		// Pass the matrices to the shader
		glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

		glBindVertexArray(VAO);
		// Draw all points directly from the VBO in one call
		glm::mat4 model(1.0f);
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
		glDrawArrays(GL_POINTS, 0, partManager.npartDraw);
		glBindVertexArray(0);
		// Swap the buffers
		display.SwapBuffers();
	}
	// Properly de-allocate all resources once they've outlived their purpose
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
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
	if (keys[GLFW_KEY_Q])
		camera.ProcessKeyboard(UP, deltaTime);
	if (keys[GLFW_KEY_Z])
		camera.ProcessKeyboard(DOWN, deltaTime);
	if (keys[GLFW_KEY_P])
		camera.printStats();
	if (keys[GLFW_KEY_R])
		camera.reset();
}

// Is called whenever a key is pressed/released via GLFW
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
	//cout << key << endl;
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GL_TRUE);

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