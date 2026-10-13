#version 450
layout(set = 0, binding = 0, std140) uniform View {
    vec4 viewport_sizes; // render, then logical
    vec4 line_params;
    vec4 proj[4];
    vec4 tex_mtx[30];
    vec4 channel_colors[8]; // per channel: ambient, material, alpha ambient, alpha material
    vec4 postmtx[60];
    vec4 fog[5];
    vec4 ind_mtx[6];
    vec4 depth_range; // the render viewport's znear and zfar
} uview;
layout(set = 0, binding = 4, std140) uniform Material {
    vec4 tevreg[4]; // TEVPREV, then TEVREG0-2
    vec4 kcolor[4];
    vec4 texcoord_scale[8];
    vec4 tex_size_bias[8];
} umat;
layout(set = 1, binding = 0) uniform sampler2D tex0;
layout(location = 0) in vec4 in_cc0;
layout(location = 1) in vec2 in_tex0_uv;
layout(location = 2) in vec2 in_fog_zw;
layout(location = 0) out vec4 o_color;
void main() {
    vec2 tex0_uv = in_tex0_uv;
    vec4 sampled0 = texture(tex0, tex0_uv);
    // TEV stage 0
    vec3 s0_color = clamp(((mix((umat.kcolor[0].rgb), (sampled0.rgb), (in_cc0.rgb)) + umat.tevreg[1].rgb)) * 4.0, 0.0, 1.0);
    float s0_alpha = clamp(((mix((0.0), (umat.kcolor[3].a), (sampled0.a)) + 0.0)), 0.0, 1.0);
    // Fog
    float fog_ndc = in_fog_zw.x / in_fog_zw.y;
    float fog_depth = uview.depth_range.y - fog_ndc * (uview.depth_range.y - uview.depth_range.x);
    float fog_base = uview.fog[1].x / (uview.fog[1].y - fog_depth);
    float fog_offset = gl_FragCoord.x / uview.viewport_sizes.x * 2.0 - 1.0 - uview.fog[1].w;
    float fog_index = clamp(9.0 - abs(fog_offset) * 9.0, 0.0, 9.0);
    vec4 fog_k0 = max(vec4(1.0) - abs(vec4(fog_index) - vec4(0.0, 1.0, 2.0, 3.0)), vec4(0.0));
    vec4 fog_k1 = max(vec4(1.0) - abs(vec4(fog_index) - vec4(4.0, 5.0, 6.0, 7.0)), vec4(0.0));
    vec4 fog_k2 = max(vec4(1.0) - abs(vec4(fog_index) - vec4(8.0, 9.0, 10.0, 11.0)), vec4(0.0));
    float fog_k = max(dot(uview.fog[2], fog_k0) + dot(uview.fog[3], fog_k1) + dot(uview.fog[4], fog_k2), 0.000001);
    float fog_f = clamp(fog_base * (sqrt(fog_offset * fog_offset + fog_k * fog_k) / fog_k) - uview.fog[1].z, 0.0, 1.0);
    float fog_z = fog_f;
    vec3 fog_color = mix(s0_color, uview.fog[0].rgb, clamp(fog_z, 0.0, 1.0));
    // Alpha compare
    float alpha_byte = floor(s0_alpha * 255.0 + 0.5);
    float alpha_pass = step(0.5, alpha_byte - 128.0);
    if (alpha_pass < 0.5) {
        discard;
    }
    o_color = vec4(fog_color, s0_alpha);
}
