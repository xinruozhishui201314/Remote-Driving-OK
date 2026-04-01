#version 440

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 vTexCoord;

// Uniform block 必须与 frag shader 声明一致（binding 0）
layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;  // MVP（64 bytes）
    int   isNv12;     // 像素格式标志（4 bytes）
    float opacity;    // 透明度（4 bytes）
};

void main()
{
    gl_Position = qt_Matrix * position;
    vTexCoord   = texcoord;
}
