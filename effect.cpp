// SPDX-License-Identifier: GPL-3.0-or-later
#include "effect.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <effect/effecthandler.h>
#include <opengl/glshader.h>
#include <QDateTime>
#include <QtMath>

#include <algorithm>
static void ensureResources()
{
    Q_INIT_RESOURCE(effect);
}

namespace KWin
{
AjusteVideoEffect::AjusteVideoEffect()
    : OffscreenEffect()
{
    readSettings();
    if (m_enabled) {
        loadShader();
        updateWindows();
    }
    connect(effects, &EffectsHandler::windowAdded, this, &AjusteVideoEffect::attachWindow);
    connect(effects, &EffectsHandler::windowDeleted, this, &AjusteVideoEffect::forgetWindow);
}

AjusteVideoEffect::~AjusteVideoEffect()
{
    for (EffectWindow *window : m_windows) {
        unredirect(window);
    }
}

bool AjusteVideoEffect::supported()
{
    return OffscreenEffect::supported();
}

bool AjusteVideoEffect::isActive() const
{
    return m_enabled && !m_windows.empty();
}

int AjusteVideoEffect::requestedEffectChainPosition() const
{
    return 99;
}

void AjusteVideoEffect::readSettings()
{
    const auto config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    config->reparseConfiguration();
    const KConfigGroup group(config, QStringLiteral("Effect-ajustevideo"));

    const bool previewActive = group.readEntry(QStringLiteral("PreviewActive"), false);
    const qint64 previewTimestamp = group.readEntry(QStringLiteral("PreviewTimestamp"), static_cast<qint64>(0));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 previewAge = now - previewTimestamp;
    const bool usePreview = previewActive && (previewTimestamp > 0) && (previewAge >= 0 && previewAge < 3000);
    const QString prefix = usePreview ? QStringLiteral("Preview") : QString();

    m_enabled = group.readEntry(prefix + QStringLiteral("Enabled"), false);
    // Keep the compositing effect inside conservative bounds, even if kwinrc is edited by hand.
    m_brightness = std::clamp(group.readEntry(prefix + QStringLiteral("Brightness"), 0.0), -0.20, 0.20);
    m_contrast = std::clamp(group.readEntry(prefix + QStringLiteral("Contrast"), 1.0), 0.20, 1.20);
    m_gamma = std::clamp(group.readEntry(prefix + QStringLiteral("Gamma"), 1.0), 0.50, 1.50);
    m_saturation = std::clamp(group.readEntry(prefix + QStringLiteral("Saturation"), 1.0), 0.75, 1.25);
    m_hue = std::clamp(group.readEntry(prefix + QStringLiteral("Hue"), 0.0), -30.0, 30.0);
    m_temperature = std::clamp(group.readEntry(prefix + QStringLiteral("ColorTemperature"), 0.0), -0.25, 0.25);
}

void AjusteVideoEffect::updateShaderUniforms()
{
    if (!m_shader) {
        return;
    }
    if (!effects->makeOpenGLContextCurrent()) {
        return;
    }
    ShaderBinder binder{m_shader.get()};
    m_shader->setUniform("brightness", m_brightness);
    m_shader->setUniform("contrast", m_contrast);
    m_shader->setUniform("gamma", m_gamma);
    m_shader->setUniform("saturation", m_saturation);
    m_shader->setUniform("hue", qDegreesToRadians(m_hue));
    m_shader->setUniform("temperature", m_temperature);
}

void AjusteVideoEffect::loadShader()
{
    ensureResources();
    m_shader = ShaderManager::instance()->generateShaderFromFile(
        ShaderTrait::MapTexture, QString(), QStringLiteral(":/ajuste-video/shaders/adjust.frag"));
    if (m_shader) {
        updateShaderUniforms();
    }
}

void AjusteVideoEffect::updateWindows()
{
    if (!m_enabled) {
        for (EffectWindow *window : m_windows) {
            unredirect(window);
        }
        m_windows.clear();
        effects->addRepaintFull();
        return;
    }
    if (!m_shader) {
        loadShader();
    }
    if (!m_shader) {
        return;
    }
    updateShaderUniforms();
    for (EffectWindow *window : effects->stackingOrder()) {
        attachWindow(window);
    }
    effects->addRepaintFull();
}

void AjusteVideoEffect::attachWindow(EffectWindow *window)
{
    if (!m_enabled || !m_shader || !window || m_windows.contains(window)) {
        return;
    }
    redirect(window);
    setShader(window, m_shader.get());
    m_windows.insert(window);
}

void AjusteVideoEffect::forgetWindow(EffectWindow *window)
{
    m_windows.erase(window);
}

void AjusteVideoEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)
    readSettings();
    if (m_enabled && !m_shader) {
        loadShader();
    }
    updateShaderUniforms();
    updateWindows();
}
}

#include "moc_effect.cpp"
