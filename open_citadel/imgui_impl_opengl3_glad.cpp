#include <GLES2/gl2.h>

/* Let GLAD provide the entry-point macros used by ImGui's GL backend. */
#undef GL_ES_VERSION_2_0
#include "khronos/glad.h"

/* Use the same SDL-loaded GLES entry points as the guest renderer. */
#include "backends/imgui_impl_opengl3.cpp"
