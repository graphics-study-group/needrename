#version 450 core

// Adjustable parameters
layout(constant_id = 1) const float bloomThreshold = 1.0;
layout(constant_id = 2) const float bloomSoftKnee  = 0.5;
layout(constant_id = 3) const float bloomIntensity = 1.0;

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0, r11f_g11f_b10f) uniform restrict readonly image2D inputImage;
layout(set = 0, binding = 1, r11f_g11f_b10f) uniform image2D bloomTemp;
layout(set = 0, binding = 2, rgba8) uniform restrict writeonly image2D outputImage;

// 9 tap Gaussian kernel (sigma ≈ 3.0)
const float kernel[9] = float[](0.05, 0.09, 0.12, 0.15, 0.18, 0.15, 0.12, 0.09, 0.05);

// Calculate luminance
float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Extract bright areas (soft threshold)
vec3 extractBloom(vec3 color) {
    float l = luminance(color);
    float knee = bloomThreshold * bloomSoftKnee;
    float soft = clamp((l - bloomThreshold + knee) / (2.0 * knee), 0.0, 1.0);
    float contrib = max(l - bloomThreshold, 0.0) + soft * soft * knee;
    return color * (contrib / max(l, 1e-5));
}

// Read half-resolution bloomTemp
vec3 loadHalfRes(ivec2 coord, ivec2 size) {
    ivec2 clamped = clamp(coord, ivec2(0), size - 1);
    return imageLoad(bloomTemp, clamped).rgb;
}

// Write half-resolution bloomTemp
void storeHalfRes(ivec2 coord, vec3 color) {
    imageStore(bloomTemp, coord, vec4(color,1.0));
}

void main() {
    ivec2 fullSize = imageSize(inputImage);
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if(coord.x >= fullSize.x || coord.y >= fullSize.y) return;

    // ---------- PASS 1: Extract bright areas to half-resolution ----------
    ivec2 halfSize = imageSize(bloomTemp);
    ivec2 halfCoord = coord / 2;
    if(halfCoord.x < halfSize.x && halfCoord.y < halfSize.y){
        vec3 c = imageLoad(inputImage, coord).rgb;
        storeHalfRes(halfCoord, extractBloom(c));
    }

    // ---------- PASS 2/3: Separable Gaussian blur ----------
    // Note: We complete ping-pong directly in the same shader
    // Step1: Horizontal blur
    if(halfCoord.x < halfSize.x && halfCoord.y < halfSize.y){
        vec3 colorH = vec3(0.0);
        float wSum = 0.0;
        for(int i=-4;i<=4;i++){
            int x = clamp(halfCoord.x + i, 0, halfSize.x - 1);
            vec3 c = loadHalfRes(ivec2(x, halfCoord.y), halfSize);
            float w = kernel[i+4];
            colorH += c * w;
            wSum += w;
        }
        colorH /= wSum;
        storeHalfRes(halfCoord, colorH);
    }

    // Step2: Vertical blur
    if(halfCoord.x < halfSize.x && halfCoord.y < halfSize.y){
        vec3 colorV = vec3(0.0);
        float wSum = 0.0;
        for(int i=-4;i<=4;i++){
            int y = clamp(halfCoord.y + i, 0, halfSize.y - 1);
            vec3 c = loadHalfRes(ivec2(halfCoord.x, y), halfSize);
            float w = kernel[i+4];
            colorV += c * w;
            wSum += w;
        }
        colorV /= wSum;
        storeHalfRes(halfCoord, colorV);
    }

    // ---------- PASS 4: Add back to original image and tone mapping ----------
    vec3 baseColor = imageLoad(inputImage, coord).rgb;
    vec3 bloom = loadHalfRes(halfCoord, halfSize);

    vec3 finalColor = baseColor + bloom * bloomIntensity;
    // Simple Reinhard tone map
    finalColor = finalColor / (finalColor + vec3(1.0));

    imageStore(outputImage, coord, vec4(finalColor,1.0));
}
