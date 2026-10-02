// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <effect/offscreeneffect.h>
#include <opengl/glshadermanager.h>

#include <memory>
#include <unordered_set>

namespace KWin
{
class GLShader;

class AjusteVideoEffect final : public OffscreenEffect
{
    Q_OBJECT
public:
    AjusteVideoEffect();
    ~AjusteVideoEffect() override;

    static bool supported();
    bool isActive() const override;
    void reconfigure(ReconfigureFlags flags) override;
    int requestedEffectChainPosition() const override;

private Q_SLOTS:
    void attachWindow(KWin::EffectWindow *window);
    void forgetWindow(KWin::EffectWindow *window);

private:
    void readSettings();
    void updateWindows();
    void updateShaderUniforms();
    void loadShader();

    bool m_enabled = false;
    float m_brightness = 0.0f;
    float m_contrast = 1.0f;
    float m_gamma = 1.0f;
    float m_saturation = 1.0f;
    float m_hue = 0.0f;
    float m_temperature = 0.0f;
    std::unordered_set<EffectWindow *> m_windows;
    std::unique_ptr<GLShader> m_shader;
};
}

