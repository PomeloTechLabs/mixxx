#include "shaders/textureshader.h"

using namespace mixxx;

void TextureShader::init() {
    QString vertexShaderCode = QStringLiteral(R"--(
uniform highp mat4 matrix;
attribute highp vec4 position; // use vec4 here (will be padded) for matrix multiplication
attribute highp vec2 texcoord;
varying highp vec2 vTexcoord;
void main()
{
    vTexcoord = texcoord;
    gl_Position = matrix * position;
}
)--");

    // No "#version" directive on purpose, and floats carry an explicit
    // precision qualifier: the syntax below is valid in both GLSL ES 100 and
    // legacy desktop GLSL, while "#version 120" cannot be compiled by the
    // OpenGL ES compilers used on Android and HarmonyOS, and an unqualified
    // float has no default precision in a GLSL ES fragment shader.
    QString fragmentShaderCode = QStringLiteral(R"--(
uniform sampler2D texture;
varying highp vec2 vTexcoord;
uniform highp float alpha;
void main()
{
    gl_FragColor = texture2D(texture, vTexcoord) * vec4(1.0, 1.0, 1.0, alpha > .0 ? alpha : 1.0);
}
)--");

    load(vertexShaderCode, fragmentShaderCode);

    m_matrixLocation = uniformLocation("matrix");
    m_positionLocation = attributeLocation("position");
    m_texcoordLocation = attributeLocation("texcoord");
    m_textureLocation = uniformLocation("texture");
}
