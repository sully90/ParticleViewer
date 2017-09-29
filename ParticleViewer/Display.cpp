#include "Display.h"
#include <iostream>

Display::Display(const GLuint width, const GLuint height, const char* title)
{
	std::cout << "Starting GLFW context, OpenGL 3.3" << std::endl;
	// Init GLFW
	glfwInit();
	// Set all the required options for GLFW
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
	this->mWidth = width;
	this->mHeight = height;
	this->mTitle = title;
}

void Display::Create()
{
	// Create a GLFWwindow object that we can use for GLFW's functions
	//this->window = glfwCreateWindow(this->mWidth, this->mHeight, this->mTitle, glfwGetPrimaryMonitor(), nullptr);
	this->window = glfwCreateWindow(this->mWidth, this->mHeight, this->mTitle, nullptr, nullptr);
	glfwMakeContextCurrent(this->window);
	if (this->window == NULL)
	{
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
	}
	// Set this to true so GLEW knows to use a modern approach to retrieving function pointers and extensions
	glewExperimental = GL_TRUE;
	// Initialize GLEW to setup the OpenGL Function pointers
	if (glewInit() != GLEW_OK)
	{
		std::cout << "Failed to initialize GLEW" << std::endl;
	}

	// Define the viewport dimensions
	glViewport(0, 0, this->mWidth, this->mHeight);

	//glEnable(GL_DEPTH_TEST);
}

Display::~Display()
{
	glfwTerminate();
}

void Display::Clear(float r, float g, float b, float a)
{
	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Display::SwapBuffers()
{
	glfwSwapBuffers(this->window);
}

void Display::KeyCallback(GLFWkeyfun keyfun)
{
	glfwSetKeyCallback(this->window, keyfun);
}

bool Display::ShouldClose()
{
	return glfwWindowShouldClose(this->window);
}

GLFWwindow* Display::getWindow()
{
	return this->window;
}
