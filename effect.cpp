// SPDX-License-Identifier: GPL-3.0-or-later
#include "effect.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <effect/effecthandler.h>
#include <opengl/glshader.h>
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
    m_enabled = group.readEntry(QStringLiteral("Enabled"), false);
    // Keep the compositing effect inside conservative bounds, even if kwinrc is edited by hand.
    m_brightness = std::clamp(group.readEntry(QStringLiteral("Brightness"), 0.0), -0.20, 0.20);
    m_contrast = std::clamp(group.readEntry(QStringLiteral("Contrast"), 1.0), 0.80, 1.20);
    m_gamma = std::clamp(group.readEntry(QStringLiteral("Gamma"), 1.0), 0.80, 1.20);
    m_saturation = std::clamp(group.readEntry(QStringLiteral("Saturation"), 1.0), 0.75, 1.25);
    m_hue = std::clamp(group.readEntry(QStringLiteral("Hue"), 0.0), -30.0, 30.0);
    m_temperature = std::clamp(group.readEntry(QStringLiteral("ColorTemperature"), 0.0), -0.25, 0.25);
}

void AjusteVideoEffect::loadShader()
{
    ensureResources();
    m_shader = ShaderManager::instance()->generateShaderFromFile(
        ShaderTrait::MapTexture, QString(), QStringLiteral(":/ajuste-video/shaders/adjust.frag"));
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

void AjusteVideoEffect::drawWindow(const RenderTarget &renderTarget,
                                   const RenderViewport &viewport,
                                   EffectWindow *window,
                                   int mask,
                                   const Region &region,
                                   WindowPaintData &data)
{
    if (!m_enabled || !m_shader) {
        OffscreenEffect::drawWindow(renderTarget, viewport, window, mask, region, data);
        return;
    }
    ShaderBinder binder{m_shader.get()};
    m_shader->setUniform("brightness", m_brightness);
    m_shader->setUniform("contrast", m_contrast);
    m_shader->setUniform("gamma", m_gamma);
    m_shader->setUniform("saturation", m_saturation);
    m_shader->setUniform("hue", qDegreesToRadians(m_hue));
    m_shader->setUniform("temperature", m_temperature);
    OffscreenEffect::drawWindow(renderTarget, viewport, window, mask, region, data);
}

void AjusteVideoEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)
    readSettings();
    if (m_enabled && !m_shader) {
        loadShader();
    }
    updateWindows();
}
}

#include "moc_effect.cpp"
