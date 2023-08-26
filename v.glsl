#version 330 core

uniform mat4 mvp;

in vec3 vtxPos;
in vec2 texPos;
out vec2 texCoord;

void main() {
  gl_Position = mvp * vec4(vtxPos, 1.0);
  texCoord = texPos;
}