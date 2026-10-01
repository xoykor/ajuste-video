"use strict";

const shaderId = effect.addFragmentShader(Effect.MapTexture, "adjust.frag");
let attachedWindows = [];
let isEnabled = false;

function setting(name, fallback, minimum, maximum) {
    const value = Number(effect.readConfig(name, fallback));
    if (!Number.isFinite(value)) {
        return fallback;
    }
    return Math.max(minimum, Math.min(maximum, value));
}

function applyToWindow(window) {
    if (!isEnabled || !window) {
        return;
    }
    for (const entry of attachedWindows) {
        if (entry.window === window) {
            return;
        }
    }
    const animationId = effect.set({
        window: window,
        duration: 1,
        type: Effect.Shader,
        fragmentShader: shaderId,
        keepAlive: true
    });
    attachedWindows.push({window: window, animationId: animationId});
}

function updateSettings() {
    isEnabled = effect.readConfig("Enabled", false);
    effect.setUniform(shaderId, "brightness", isEnabled ? setting("Brightness", 0.0, -0.20, 0.20) : 0.0);
    effect.setUniform(shaderId, "contrast", isEnabled ? setting("Contrast", 1.0, 0.80, 1.20) : 1.0);
    effect.setUniform(shaderId, "gamma", isEnabled ? setting("Gamma", 1.0, 0.80, 1.20) : 1.0);
    effect.setUniform(shaderId, "saturation", isEnabled ? setting("Saturation", 1.0, 0.75, 1.25) : 1.0);
    effect.setUniform(shaderId, "hue", isEnabled ? setting("Hue", 0.0, -30.0, 30.0) * Math.PI / 180.0 : 0.0);
    effect.setUniform(shaderId, "temperature", isEnabled ? setting("ColorTemperature", 0.0, -0.25, 0.25) : 0.0);

    if (isEnabled) {
        for (const window of effects.stackingOrder) {
            applyToWindow(window);
        }
    } else {
        for (const entry of attachedWindows) {
            effect.cancel(entry.animationId);
        }
        attachedWindows = [];
    }
    effects.addRepaintFull();
}

effects.windowAdded.connect(applyToWindow);
effects.windowDeleted.connect(function (window) {
    attachedWindows = attachedWindows.filter(function (entry) {
        return entry.window !== window;
    });
});
effect.configChanged.connect(updateSettings);
updateSettings();
