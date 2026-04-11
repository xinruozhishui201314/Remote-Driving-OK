#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D planeY;
layout(binding = 2) uniform sampler2D planeUV;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float _pad[3];
} ubuf;

// 与 shaders/video.frag 一致：BT.709 有限幅 → 全范围 RGB（与 CPU sws 路径 videoSwsConfigureYuvToRgbaColorspace 对齐）
vec3 yuv2rgb_bt709(float y, float u, float v)
{
    y = (y - 16.0 / 255.0) * (255.0 / 219.0);
    u = u - 128.0 / 255.0;
    v = v - 128.0 / 255.0;
    float r = y + 1.5748 * v;
    float g = y - 0.1873 * u - 0.4681 * v;
    float b = y + 1.8556 * u;
    return clamp(vec3(r, g, b), 0.0, 1.0);
}

void main() {
    float y = texture(planeY, vTexCoord).r;
    vec2 uvPacked = texture(planeUV, vTexCoord).rg;
    float u = uvPacked.r;
    float v = uvPacked.g;
    vec3 rgb = yuv2rgb_bt709(y, u, v);
    fragColor = vec4(rgb, ubuf.qt_Opacity);
}
