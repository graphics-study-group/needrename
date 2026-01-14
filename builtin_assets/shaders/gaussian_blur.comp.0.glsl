#version 450 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 1, rgba8) uniform restrict readonly image2D inputImage;
layout(set = 0, binding = 2, rgba8) uniform restrict writeonly image2D outputImage;

// Binominal gaussian coefficients, separated.
const float kernel[3] = float[3](0.25, 0.50, 0.25);

void main() {
    ivec2 uv = ivec2(gl_GlobalInvocationID.xy);

    vec4 outputColor = vec4(0.0);
    for (int i = 0; i <= 2; i++) {
        for (int j = 0; j <= 2; j++) {
            vec4 inputColor = imageLoad(inputImage, ivec2(uv.x + i - 1, uv.y + j - 1));
            outputColor += kernel[i] * kernel[j] * inputColor;
        }
    }
    imageStore(outputImage, uv, outputColor);
}
