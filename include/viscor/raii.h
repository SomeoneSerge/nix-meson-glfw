#ifndef _VISCOR_RAII_H
#define _VISCOR_RAII_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <OpenImageIO/imageio.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include "viscor/utils.h"

namespace VisCor {
// FIXME:
constexpr double WINDOW_MIN_WIDTH = 800;

class NoCopy {
public:
  NoCopy() = default;
  virtual ~NoCopy() = default;
  NoCopy(NoCopy &&) = delete; /* let's delete the moves for now as well */
  NoCopy(const NoCopy &) = delete;
  NoCopy operator=(const NoCopy &) = delete;
};

class SafeGlfwCtx : NoCopy {
public:
  SafeGlfwCtx();
  ~SafeGlfwCtx();
};

class SafeGlfwWindow : NoCopy {
public:
  SafeGlfwWindow();
  ~SafeGlfwWindow();

  GLFWwindow *window() const;
  void makeContextCurrent() const;

private:
  GLFWwindow *_window;
};

class SafeGlew : NoCopy {
public:
  SafeGlew();
};

class SafeImGui : NoCopy {
public:
  SafeImGui(GLFWwindow *window);
  ~SafeImGui();
};

class SafeVBO : NoCopy {
public:
  SafeVBO(GLsizeiptr size, const void *data, GLenum target = GL_ARRAY_BUFFER,
          GLenum usage = GL_STATIC_DRAW);

  ~SafeVBO();

  GLuint vbo() const;

private:
  GLuint _vbo;
};

class SafeVAO : NoCopy {
public:
  SafeVAO() {
    glGenVertexArrays(1, &_vao);
    bind();
  }

  ~SafeVAO() { glDeleteVertexArrays(1, &_vao); }

  GLuint vao() const { return _vao; }
  void bind() { glBindVertexArray(_vao); }

private:
  GLuint _vao;
};

class SafeShader : NoCopy {
public:
  SafeShader(GLenum shaderType, const char *source);
  ~SafeShader();

  GLuint shader() const;

private:
  GLuint _shader;
};

class SafeShaderProgram : NoCopy {
public:
  SafeShaderProgram() {
    /* we could do the linking to shaders, etc, right here
     * but atm we only care about the order of initialization
     * (and destruction)
     */
    _program = glCreateProgram();
  }

  ~SafeShaderProgram() { glDeleteProgram(_program); }

  GLuint program() const { return _program; }

private:
  GLuint _program;
};

class VtxFragProgram : NoCopy {
public:
  VtxFragProgram(const char *vtxShader, const char *fragShader);

  GLuint vtxShader() const;
  GLuint fragShader() const;
  GLuint program() const;

protected:
  SafeShader _vtxShader;
  SafeShader _fragShader;
  SafeShaderProgram _program;
};

class GlfwFrame : NoCopy {
public:
  GlfwFrame(GLFWwindow *window);
  ~GlfwFrame();

private:
  GLFWwindow *window;
};

class ImGuiGlfwFrame : NoCopy {
public:
  ImGuiGlfwFrame();
  ~ImGuiGlfwFrame();
};

class SafeGlTexture : NoCopy {
public:
  SafeGlTexture(const Uint8Image &image,
                const unsigned int interpolation = GL_LINEAR);

  SafeGlTexture(SafeGlTexture &&other)
      : _texture(other._texture), _xres(other._xres), _yres(other._yres) {
    other._texture = GL_INVALID_VALUE;
  }

  ~SafeGlTexture();

  GLuint texture() const { return _texture; }
  void *textureVoidStar() const { return (void *)(intptr_t)_texture; }
  void bind() { glBindTexture(GL_TEXTURE_2D, _texture); }
  int xres() const { return _xres; }
  int yres() const { return _yres; }
  double aspect() const { return _yres * 1.0 / _xres; }

private:
  GLuint _texture;
  int _xres, _yres;
};
}; // namespace VisCor

#endif
