#include "waveform/renderers/allshader/waveformrenderbackground.h"

#ifdef MIXXX_OS_OHOS
#include "rendergraph/geometrynode.h"
#include "rendergraph/material/rgbmaterial.h"
#include "rendergraph/vertexupdaters/rgbvertexupdater.h"
#endif

#include "waveform/renderers/waveformwidgetrenderer.h"

namespace allshader {

WaveformRenderBackground::WaveformRenderBackground(
        WaveformWidgetRenderer* waveformWidgetRenderer)
        : WaveformRenderer(waveformWidgetRenderer),
          m_backgroundColor(0, 0, 0) {
#ifdef MIXXX_OS_OHOS
    auto background = std::make_unique<rendergraph::GeometryNode>();
    background->initForRectangles<rendergraph::RGBMaterial>(1);
    appendChildNode(background.release());
    setUsePreprocess(true);
#endif
}

void WaveformRenderBackground::setup(const QDomNode& node,
        const SkinContext& skinContext) {
    m_backgroundColor = m_waveformRenderer->getWaveformSignalColors()->getBgColor();

    QString backgroundPixmapPath = skinContext.selectString(node, "BgPixmap");
    if (!backgroundPixmapPath.isEmpty()) {
        qWarning() << "WaveformView BgPixmap is not supported by "
                      "allshader::WaveformRenderBackground";
    }
}

void WaveformRenderBackground::paintGL() {
    glClearColor(static_cast<float>(m_backgroundColor.redF()),
            static_cast<float>(m_backgroundColor.greenF()),
            static_cast<float>(m_backgroundColor.blueF()),
            1.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

#ifdef MIXXX_OS_OHOS
void WaveformRenderBackground::preprocess() {
    auto* background = static_cast<rendergraph::GeometryNode*>(firstChild());
    rendergraph::RGBVertexUpdater vertices{
            background->geometry().vertexDataAs<rendergraph::Geometry::RGBColoredPoint2D>()};
    vertices.addRectangle({0, 0},
            {static_cast<float>(m_waveformRenderer->getWidth()),
                    static_cast<float>(m_waveformRenderer->getHeight())},
            {static_cast<float>(m_backgroundColor.redF()),
                    static_cast<float>(m_backgroundColor.greenF()),
                    static_cast<float>(m_backgroundColor.blueF())});
    background->markDirtyGeometry();
}
#endif

} // namespace allshader
