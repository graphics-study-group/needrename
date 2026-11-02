#version 450 core

layout(constant_id = 1) const float LUMINANCE_THRESHOLD = 0.2;

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
// The input is a sampled texture rather than a read-only storage image: the standard WebGPU
// interface has no read-only storage texture without an optional feature this build does not
// have, while a sampled texture is available everywhere. `texelFetch` keeps the per-texel read
// the storage-image form had, so the arithmetic is unchanged.
//
// Binding 1 is left free for this texture's sampler: a backend that splits the pair declares
// the sampler in the slot after the texture. The output therefore moves to binding 2.
layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 0, binding = 2, rgba8) uniform restrict writeonly image2D outputImage;

const float kernel[5] = float[5](1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0);

float Luminance(vec3 rgb)
{
    return dot(vec3(0.2125, 0.7154, 0.0721), rgb);
}

void main()
{
    ivec2 uv = ivec2(gl_GlobalInvocationID.xy);

    vec3 color = texelFetch(inputImage, uv, 0).rgb;
    // for (int i = 0; i < 5; i++) {
    //     for (int j = 0; j < 5; j++) {
    //         ivec2 nuv = uv + ivec2(i - 2, j - 2);
    //         vec3 hdr = texelFetch(inputImage, nuv, 0).rgb;
    //         float lum = Luminance(hdr);
    //         if (lum > LUMINANCE_THRESHOLD) {
    //             color += hdr * kernel[i] * kernel[j];
    //         }
    //     }
    // }
    imageStore(outputImage, uv, vec4(color, 1.0));
}
