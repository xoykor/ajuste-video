const {Clutter, Gio, GLib} = imports.gi;

const EFFECT_NAME = 'ajuste-video-color';
const CONFIG = GLib.build_filenamev([GLib.get_user_config_dir(), 'ajuste-video', 'settings.json']);
const SHADER = `uniform float u_brightness; uniform float u_contrast; uniform float u_gamma;
uniform float u_saturation; uniform float u_hue; uniform float u_temperature;
void main() {
vec3 av = cogl_color_out.rgb;
av += vec3(u_temperature * 0.12, 0.0, -u_temperature * 0.12);
float avY = dot(av, vec3(0.299, 0.587, 0.114)); av = mix(vec3(avY), av, u_saturation);
float avC = cos(u_hue), avS = sin(u_hue);
mat3 avRot = mat3(0.213+avC*0.787-avS*0.213, 0.715-avC*0.715-avS*0.715, 0.072-avC*0.072+avS*0.928,
0.213-avC*0.213+avS*0.143, 0.715+avC*0.285+avS*0.140, 0.072-avC*0.072-avS*0.283,
0.213-avC*0.213-avS*0.787, 0.715-avC*0.715+avS*0.715, 0.072+avC*0.928+avS*0.072);
av = avRot * av; av = (av-vec3(0.5))*u_contrast+vec3(0.5+u_brightness);
av = sign(av)*pow(abs(av), vec3(1.0/u_gamma)); cogl_color_out=vec4(clamp(av,0.0,1.0),cogl_color_out.a);
}`;

function init() {}

function enable() {
    this._effect = new Clutter.ShaderEffect({shader_type: Clutter.ShaderType.FRAGMENT_SHADER});
    this._effect.set_shader_source(SHADER);
    global.stage.add_effect_with_name(EFFECT_NAME, this._effect);
    this._readConfig();
    this._poll = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 1, () => {
        this._readConfig();
        return GLib.SOURCE_CONTINUE;
    });
}

function _readConfig() {
    try {
        const [, bytes] = Gio.File.new_for_path(CONFIG).load_contents(null);
        const settings = JSON.parse(imports.byteArray.toString(bytes));
        this._effect.enabled = settings.enabled === true;
        const clamp = (v, lo, hi, fallback) => Math.max(lo, Math.min(hi, Number(v) || fallback));
        const uniforms = {
            u_brightness: clamp(settings.brightness, -0.20, 0.20, 0),
            u_contrast: clamp(settings.contrast, 0.80, 1.20, 1),
            u_gamma: clamp(settings.gamma, 0.80, 1.20, 1),
            u_saturation: clamp(settings.saturation, 0.75, 1.25, 1),
            u_hue: clamp(settings.hue, -30, 30, 0) * Math.PI / 180,
            u_temperature: clamp(settings.temperature, -0.25, 0.25, 0),
        };
        for (const [name, value] of Object.entries(uniforms))
            this._effect.set_uniform_value(name, value);
    } catch (_) {
        if (this._effect) this._effect.enabled = false;
    }
}

function disable() {
    if (this._poll) {
        GLib.source_remove(this._poll);
        this._poll = 0;
    }
    global.stage.remove_effect_by_name(EFFECT_NAME);
    this._effect = null;
}
