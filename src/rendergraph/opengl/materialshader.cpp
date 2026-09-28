#include "rendergraph/materialshader.h"

#include <QFile>
#include <QOpenGLContext>
#ifdef USE_QSHADER_FOR_GL
#include <rhi/qshader.h>
#endif

#include "../../util/assert.h"

using namespace rendergraph;

namespace {
#ifdef USE_QSHADER_FOR_GL
QString resource(const QString& filename) {
    return QStringLiteral(":/shaders/rendergraph/%1.qsb").arg(filename);
}

QByteArray loadShaderCodeFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODeviceBase::ReadOnly)) {
        qWarning() << "Failed to open the shader file:" << path;
        return QByteArray();
    }
    const QShader qsbShader = QShader::fromSerialized(file.readAll());
    // A .qsb bundle carries one GLSL flavour per target: GLSL ES for OpenGL ES
    // contexts (Android, HarmonyOS) and core GLSL for desktop. Which one is
    // compilable follows from the current context, not from the build target,
    // so ask the context instead of hard-coding a version. Requesting a variant
    // the bundle does not contain yields an empty shader, which only surfaces
    // later as "Link failed because of missing fragment shader".
    const QOpenGLContext* context = QOpenGLContext::currentContext();
    const bool wantsGlslEs = context && context->isOpenGLES();

    QByteArray firstGlslVariant;
    for (const QShaderKey& key : qsbShader.availableShaders()) {
        if (key.source() != QShader::GlslShader) {
            continue;
        }
        const QByteArray code = qsbShader.shader(key).shader();
        if (code.isEmpty()) {
            continue;
        }
        if (key.sourceVersion().flags().testFlag(QShaderVersion::GlslEs) == wantsGlslEs) {
            return code;
        }
        if (firstGlslVariant.isEmpty()) {
            firstGlslVariant = code;
        }
    }
    if (firstGlslVariant.isEmpty()) {
        qWarning() << "Shader bundle contains no GLSL variant:" << path;
    }
    return firstGlslVariant;
}
#else
QString resource(const QString& filename) {
    return QStringLiteral(":/shaders/rendergraph/%1.gl").arg(filename);
}

QByteArray loadShaderCodeFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODeviceBase::ReadOnly)) {
        qWarning() << "Failed to open shader file:" << path;
        return QByteArray();
    }
    return file.readAll();
}
#endif
} // namespace

MaterialShader::MaterialShader(const char* vertexShaderFilename,
        const char* fragmentShaderFilename,
        const UniformSet& uniformSet,
        const AttributeSet& attributeSet) {
    const QString vertexShaderFileFullPath = resource(vertexShaderFilename);
    const QString fragmentShaderFileFullPath = resource(fragmentShaderFilename);

    QByteArray vertexCode = loadShaderCodeFromFile(vertexShaderFileFullPath);
    QByteArray fragmentCode = loadShaderCodeFromFile(fragmentShaderFileFullPath);
    VERIFY_OR_DEBUG_ASSERT(!vertexCode.isEmpty() && !fragmentCode.isEmpty()) {
        return;
    }
    if (!addShaderFromSourceCode(QOpenGLShader::Vertex, vertexCode)) {
        qWarning() << "MaterialShader - compilation failed:"
                   << vertexShaderFileFullPath;
        qDebug() << log();
        return;
    }

    if (!addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentCode)) {
        qWarning() << "MaterialShader - compilation failed:"
                   << fragmentShaderFileFullPath;
        qDebug() << log();
        return;
    }

    if (!link()) {
        qDebug() << "MaterialShader - linking failed."
                 << log();
        return;
    }

    for (const auto& attribute : attributeSet.attributes()) {
        int location = QOpenGLShaderProgram::attributeLocation(attribute.m_name);
        m_attributeLocations.push_back(location);
    }
    for (const auto& uniform : uniformSet.uniforms()) {
        int location = QOpenGLShaderProgram::uniformLocation(uniform.m_name);
        m_uniformLocations.push_back(location);
    }
}
