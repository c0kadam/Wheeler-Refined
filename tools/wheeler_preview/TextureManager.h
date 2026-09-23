#pragma once
#include <glad/glad.h>
#include <imgui.h>

class TextureManager {
public:
    static GLuint LoadTexture(const char* path);
};
