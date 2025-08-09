#ifndef DISPLAY_H
#define DISPLAY_H

// GLEW
#include <GL/glew.h>

// GLFW
#include <GLFW/glfw3.h>

#include <string>

class Display
{
public:
	Display(const GLuint width, const GLuint height, const char* title);

	void Create();
	void Clear(float r, float g, float b, float a);
	void SwapBuffers();
	void KeyCallback(GLFWkeyfun keyfun);
	bool ShouldClose();
	GLFWwindow* getWindow();

	virtual ~Display();
protected:
private:
	int mWidth, mHeight;
	const char* mTitle;
	void operator=(const Display& display) {}
	Display(const Display& display) {}

	GLFWwindow* window;
};

#endif // !DISPLAY_H