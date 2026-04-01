#version 440

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// ── 纹理绑定 ─────────────────────────────────────────────────────────────────
// YUV420P 模式（isNv12=0）：
//   binding 1 = Y  平面（GL_R8，全分辨率）
//   binding 2 = U  平面（GL_R8，1/2 分辨率）
//   binding 3 = V  平面（GL_R8，1/2 分辨率）
//
// NV12 模式（isNv12=1）：
//   binding 1 = Y  平面（GL_R8，全分辨率）
//   binding 2 = UV 平面（GL_RG8，1/2 分辨率；.r=U，.g=V）
//   binding 3 = 未使用
layout(binding = 1) uniform sampler2D yTex;
layout(binding = 2) uniform sampler2D uvTex; // U 或 UV 交织
layout(binding = 3) uniform sampler2D vTex;  // V（YUV420P only）

// ── Uniform Block ─────────────────────────────────────────────────────────────
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;      // MVP（Qt Scene Graph 注入，offset 0，64 bytes）
    int  isNv12;         // 0 = YUV420P，1 = NV12（offset 64，padded to 4 bytes）
    float opacity;       // Qt Scene Graph 透明度（offset 68）
} ubuf;

// ── BT.709 Limited Range YUV → RGB ───────────────────────────────────────────
// Y ∈ [16/255, 235/255]（studio swing）
// U,V ∈ [16/255, 240/255]，中心 128/255
vec3 yuv2rgb_bt709(float y, float u, float v)
{
    // 去偏置
    y = (y - 16.0 / 255.0) * (255.0 / 219.0);
    u =  u - 128.0 / 255.0;
    v =  v - 128.0 / 255.0;

    // BT.709 矩阵
    float r = y + 1.5748 * v;
    float g = y - 0.1873 * u - 0.4681 * v;
    float b = y + 1.8556 * u;

    return clamp(vec3(r, g, b), 0.0, 1.0);
}

void main()
{
    float y, u, v;

    if (ubuf.isNv12 == 1) {
        // NV12：UV 交织平面，UV 采用半分辨率坐标
        y = texture(yTex,  vTexCoord).r;
        vec2 uv = texture(uvTex, vTexCoord).rg;
        u = uv.r;  // NV12：U 在低字节（DRM_FORMAT_GR88 G 通道 → GL .r）
        v = uv.g;  // NV12：V 在高字节（DRM_FORMAT_GR88 R 通道 → GL .g）
    } else {
        // YUV420P：三个独立平面
        y = texture(yTex,  vTexCoord).r;
        u = texture(uvTex, vTexCoord).r;
        v = texture(vTex,  vTexCoord).r;
    }

    fragColor = vec4(yuv2rgb_bt709(y, u, v), 1.0) * ubuf.opacity;
}
