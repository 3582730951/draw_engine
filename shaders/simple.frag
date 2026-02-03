#version 450
layout(location=0) in vec2 fragUV;
layout(location=1) in vec4 fragColor;
layout(location=0) out vec4 outColor;
layout(set=0, binding=0) uniform sampler2D uTex;
void main() {
  if (fragUV.y > 1.5) {
    vec2 uv = vec2(fragUV.x, fragUV.y - 2.0);
    vec4 tex = texture(uTex, uv);
    outColor = tex * fragColor;
  } else {
    float sdf = texture(uTex, fragUV).a;
    float w = fwidth(sdf);
    float alpha = smoothstep(0.5 - w, 0.5 + w, sdf);
    outColor = vec4(fragColor.rgb, fragColor.a * alpha);
  }
}
