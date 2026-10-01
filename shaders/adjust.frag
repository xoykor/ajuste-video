#version 140
#include "colormanagement.glsl"

uniform sampler2D sampler;
uniform vec4 modulation;
uniform float brightness;
uniform float contrast;
uniform float gamma;
uniform float saturation;
uniform float hue;
uniform float temperature;
in vec2 texcoord0;
out vec4 fragColor;

vec3 rotateHueYiq(vec3 c, float angle)
{
    float y = dot(c, vec3(0.299, 0.587, 0.114));
    float i = dot(c, vec3(0.596, -0.274, -0.322));
    float q = dot(c, vec3(0.211, -0.523, 0.312));
    float cs = cos(angle);
    float sn = sin(angle);
    float ir = i * cs - q * sn;
    float qr = i * sn + q * cs;
    return vec3(y + 0.956 * ir + 0.621 * qr,
                y - 0.272 * ir - 0.647 * qr,
                y - 1.106 * ir + 1.703 * qr);
}

void main()
{
    vec4 color = texture2D(sampler, texcoord0);
    color = sourceEncodingToNitsInDestinationColorspace(color);
    float alpha = max(color.a, 0.001);

    // KWin's color-management helpers work in nits; 100 nits is the SDR reference white.
    vec3 rgb = color.rgb / alpha + vec3(brightness * 100.0);
    rgb = (rgb - vec3(18.0)) * contrast + vec3(18.0);
    rgb = pow(max(rgb, vec3(0.0)) / 100.0, vec3(1.0 / gamma)) * 100.0;
    rgb *= vec3(1.0 + temperature * 0.15, 1.0, 1.0 - temperature * 0.15);

    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = mix(vec3(luma), rgb, saturation);
    rgb = rotateHueYiq(rgb, hue);
    color.rgb = max(rgb, vec3(0.0)) * color.a;
    color *= modulation;
    fragColor = nitsToDestinationEncoding(color);
}
