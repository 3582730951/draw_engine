#version 450
layout(push_constant) uniform Push { mat4 mvp; } pc;
layout(location=0) in vec4 inRect;
layout(location=1) in vec4 inUV;
layout(location=2) in vec4 inColor;
layout(location=0) out vec2 fragUV;
layout(location=1) out vec4 fragColor;
void main() {
  int vid = gl_VertexIndex;
  int idx = (vid == 0) ? 0 : (vid == 1) ? 1 : (vid == 2) ? 2 : (vid == 3) ? 0 : (vid == 4) ? 2 : 3;
  vec2 corner = (idx == 0) ? vec2(0.0, 0.0) :
                (idx == 1) ? vec2(1.0, 0.0) :
                (idx == 2) ? vec2(1.0, 1.0) : vec2(0.0, 1.0);
  vec2 pos = inRect.xy + corner * inRect.zw;
  fragUV = mix(inUV.xy, inUV.zw, corner);
  fragColor = inColor;
  gl_Position = pc.mvp * vec4(pos, 0.0, 1.0);
}
