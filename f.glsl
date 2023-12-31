#version 330 core

in vec2 texCoord;
uniform sampler2D tex;

void main() {
  vec4 color = texture(tex, texCoord);
  if (color.w < 0.5)
    discard;
  gl_FragColor = color;
}