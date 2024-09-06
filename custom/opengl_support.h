#pragma once

#if defined(INSTRONIMBUS_OS_WEB)
#define GLFW_INCLUDE_ES3
#include <GLES3/gl3.h>
#else
#include <imgui_impl_opengl3_loader.h>
#endif
