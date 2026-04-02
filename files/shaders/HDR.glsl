// ACES Tonemapper Implementation
// Narkowicz 2015, "ACES Filmic Tone Mapping Curve"

vec3 aces(vec3 x) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float aces(float x) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 preLight(vec3 x)
{
    return pow(x, vec3(2.2));
}

// Bloom/Glow эффект - извлекает яркие участки
vec3 extractBrightness(vec3 color, float threshold)
{
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float soft = brightness - threshold + 0.5;
    soft = clamp(soft, 0.0, 1.0);
    soft = soft * soft * (3.0 - 2.0 * soft);
    float contribution = max(soft, brightness - threshold);
    contribution /= max(brightness, 0.00001);
    return color * contribution * 0.4;
}

vec3 toneMap(vec3 x)
{
    // Добавляем bloom для ярких участков
    vec3 bloom = extractBrightness(x, 1.2);
    x = x + bloom;
    
#ifdef PER_CHANEL
    vec3 col = x;
    col.x = aces(col.x);
    col.y = aces(col.y);
    col.z = aces(col.z);
#else
    vec3 col = aces(x);
#endif

    return pow(col, vec3(1.0 / 2.2));
}
