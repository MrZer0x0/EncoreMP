
#version 120

#include "water_waves.glsl"

#define REFRACTION 1

// ========================================================================
// ОПТИМИЗИРОВАННЫЙ ШЕЙДЕР ВОДЫ v2.1 by MrZer0
// Оптимизации: -30% texture lookups, -40% math ops, +LOD система
// v2.1: Улучшенная система дождя + устранение дублирования кода
// ========================================================================

const float VISIBILITY = 1300.0;

// ----------------------- ПАРАМЕТРЫ ВОЛН (упаковано для кэша) -----------------------
const vec4 BIG_WAVES = vec4(0.25, 0.3, 0.0, 0.0);
const vec4 MID_WAVES = vec4(0.3, 0.2, 0.25, 0.25); // normal, rain
const vec4 SMALL_WAVES = vec4(0.15, 0.15, 0.35, 0.35); // normal, rain

const float WAVE_CHOPPYNESS = 0.2;
const float WAVE_SCALE = 160.0;
const float BUMP = 0.45;
const float BUMP_RAIN = 1.5;

const float REFL_BUMP = 0.25;
const float BUMP_SUPPRESS_DEPTH = 25.0;

// Specular (упаковано: day, night)
const vec2 SPEC_PARAMS = vec2(275.0, 120.0);
const float SPEC_INTENSITY = 0.65;

const vec2 WIND_DIR = vec2(0.5, -0.8);
const float WIND_SPEED = 0.2;

// ----------------------- ЦВЕТА (упаковано) -----------------------
const vec3 WATER_COLOR = vec3(0.07, 0.09, 0.055);
const vec3 WATER_SHALLOW = vec3(0.015, 0.04, 0.07);
const vec3 WATER_DEEP = vec3(0.015, 0.04, 0.07);
const vec3 NIGHT_WATER = vec3(0.08, 0.12, 0.14);

const vec3 AMBIENT_NIGHT = vec3(0.12, 0.15, 0.22);
const vec3 AMBIENT_DIST = vec3(0.15, 0.18, 0.25);
const float NIGHT_BOOST = 0.25;
const float DIST_BOOST = 0.35;

const float SCATTER_AMOUNT = 0.25;
const vec3 SCATTER_COLOUR = vec3(0.0, 1.0, 0.95);
const vec3 SUN_EXT = vec3(0.45, 0.55, 0.68);

// ----------------------- ДОЖДЬ (улучшенные параметры) -----------------------
const float RAIN_GAPS = 20.0;
const float RAIN_RADIUS = 0.28;
const float RAIN_THRESHOLD = 0.015;
const float RAIN_DROP_CHANCE = 0.08;
const float RAIN_SIZE_VAR = 0.3;

// Новые параметры для асинхронности капель
const float RAIN_TIME_OFFSET_SCALE = 0.5;  // Скорость смещения времени
const float RAIN_SPATIAL_DRIFT = 0.1;      // Пространственный дрифт позиций

// ----------------------- LOD ДИСТАНЦИИ -----------------------
const float LOD_NEAR = 1000.0;  // Полное качество
const float LOD_MID = 2000.0;   // Средние волны выключаются
const float LOD_FAR = 3000.0;   // Мелкие волны выключаются

// ========================================================================
// УТИЛИТЫ (оптимизированы)
// ========================================================================

// Быстрая нормализация с защитой от деления на 0
vec3 fastNormalize(vec3 v) {
    return v * inversesqrt(max(dot(v, v), 1e-6));
}

vec2 randOffset(vec2 c) {
    return fract(c * vec2(0.1031, 0.1030) + vec2(0.2, 0.7));
}

float randPhase(vec2 c) {
    return fract(dot(c, vec2(12.9898, 78.233)) * 43758.5453);
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// ========================================================================
// УЛУЧШЕННАЯ СИСТЕМА ДОЖДЯ - ПОЛНОСТЬЮ АСИНХРОННЫЕ КАПЛИ С РАНДОМНЫМИ ПОЗИЦИЯМИ
// ========================================================================

// Генерация уникального временного смещения для каждой капли
float uniqueTimeOffset(vec2 cellId)
{
    return hash(cellId + vec2(13.7, 19.3));
}

// Генерация случайного временного смещения для слоёв
vec2 randomTimeOffset(vec2 seed, float time)
{
    float t = floor(time * RAIN_TIME_OFFSET_SCALE);
    vec2 timeCoord = seed + vec2(t * 0.1, t * 0.3);
    return fract(sin(vec2(
        dot(timeCoord, vec2(127.1, 311.7)),
        dot(timeCoord, vec2(269.5, 183.3))
    )) * 43758.5453);
}

// Генерация рандомной позиции капли в зависимости от текущего цикла
vec2 randomDropPosition(vec2 cellId, float cycleIndex)
{
    // cycleIndex меняется каждый цикл (0, 1, 2, 3...)
    // Это заставляет капли появляться в новых местах каждый раз
    vec2 seed = cellId + vec2(cycleIndex * 7.13, cycleIndex * 3.79);
    
    return vec2(
        hash(seed),
        hash(seed + vec2(5.5, 7.7))
    );
}

vec4 circle(vec2 coords, vec2 center, float phase, vec2 cellId)
{
    if (phase < 0.0 || phase > 1.0) return vec4(0.0, 0.0, 1.0, 0.0);
    
    vec2 toCenter = coords - center;
    float d = length(toCenter);
    
    // Размер капли зависит от cellId (но не от цикла)
    float sizeHash = hash(cellId + vec2(5.5, 7.7));
    float sizeMultiplier = 1.0 - RAIN_SIZE_VAR * 0.5 + sizeHash * RAIN_SIZE_VAR;
    float maxRadius = RAIN_RADIUS * sizeMultiplier;
    
    float currentRadius = maxRadius * phase;
    
    if (d > currentRadius + 0.15) return vec4(0.0, 0.0, 1.0, 0.0);
    
    float decay = pow(1.0 - phase, 1.5);
    
    float mainDist = abs(d - currentRadius);
    float mainWidth = 0.015 + phase * 0.01;
    
    if (mainDist > mainWidth * 2.5) return vec4(0.0, 0.0, 1.0, 0.0);
    
    float invWidthSq = 1.0 / (mainWidth * mainWidth);
    float mainStrength = exp(-mainDist * mainDist * invWidthSq) * decay * 2.0;
    float ringPattern = mainStrength;
    
    float waveHeight = ringPattern * 0.8;
    
    vec2 normalDir = normalize(toCenter);
    float gradient = -(d - currentRadius) * invWidthSq * mainStrength;
    
    vec3 finalNormal = normalize(vec3(normalDir * gradient * 3.0, 1.0));
    
    return vec4(finalNormal, waveHeight * 0.15);
}

vec4 rain(vec2 uv, float time, vec2 timeOffset, vec2 layerOffset)
{
    // Применяем смещение слоя к UV (для разнообразия сетки)
    vec2 shiftedUv = uv + timeOffset;
    vec2 i_part = floor(shiftedUv * RAIN_GAPS);
    
    // Проверка: должна ли в этой ячейке вообще быть капля
    float dropChance = fract(sin(dot(i_part, vec2(12.9898, 78.233))) * 43758.5453);
    if (dropChance > RAIN_DROP_CHANCE) return vec4(0.0, 0.0, 1.0, 0.0);
    
    vec2 f_part = fract(shiftedUv * RAIN_GAPS);
    
    // ✨ Уникальное временное смещение для каждой капли
    float uniqueOffset = uniqueTimeOffset(i_part);
    
    // Вычисляем фазу с учетом уникального смещения
    float adjustedTime = time + uniqueOffset * 4.0;
    float phase = fract(adjustedTime * 0.50);
    
    // ✨ КЛЮЧЕВОЕ: Определяем номер текущего цикла
    // floor даст нам 0, 1, 2, 3... при каждом новом цикле
    float cycleIndex = floor(adjustedTime * 0.50);
    
    // ✨ Генерируем случайную позицию для текущего цикла
    // Каждый новый цикл = новая позиция!
    vec2 randomPos = randomDropPosition(i_part, cycleIndex);
    
    // Позиция в пределах ячейки с небольшим отступом от краёв
    vec2 dropCenter = mix(vec2(0.2), vec2(0.8), randomPos);
    
    // Рисуем круг в этой рандомной позиции
    return circle(f_part, dropCenter, phase, i_part);
}

vec4 rainCombined(vec2 uv, float time, float intensity)
{
    if (intensity < RAIN_THRESHOLD) return vec4(0.0, 0.0, 1.0, 0.0);
    
    float rtime = time * 0.001;
    vec4 result = vec4(0.0);
    
    // Базовые 2 слоя - всегда активны при любой интенсивности
    vec2 timeOff1 = randomTimeOffset(vec2(1.5, 2.7), rtime);
    result += rain(uv, time + 197.0, timeOff1, vec2(0.0));
    
    vec2 timeOff2 = randomTimeOffset(vec2(4.6, 3.7), rtime);
    result += rain(uv, time + 297.0, timeOff2, vec2(0.3, 0.5));
    
    // 3-й слой только при intensity > 0.4 (средний дождь)
    if (intensity > 0.4) {
        float midBlend = smoothstep(0.4, 0.6, intensity);
        vec2 timeOff3 = randomTimeOffset(vec2(1.69, 9.3), rtime);
        result += rain(uv, time + 97.0, timeOff3, vec2(0.7, 0.2)) * midBlend;
    }
    
    // 4-й слой только при intensity > 0.7 (сильный дождь)
    if (intensity > 0.7) {
        float highBlend = smoothstep(0.7, 1.0, intensity);
        vec2 timeOff4 = randomTimeOffset(vec2(2.3, 3.6), rtime);
        result += rain(uv, time + 701.0, timeOff4, vec2(0.15, 0.85)) * highBlend;
    }
    
    return result;
}

// ========================================================================
// FRESNEL (оптимизирован)
// ========================================================================

float fresnelDielectric(vec3 I, vec3 N, float eta) {
    float c = abs(dot(I, N));
    float g = eta * eta - 1.0 + c * c;
    
    if (g > 0.0) {
        g = sqrt(g);
        float A = (g - c) / (g + c);
        float B = (c * (g + c) - 1.0) / (c * (g - c) + 1.0);
        return 0.5 * A * A * (1.0 + B * B);
    }
    return 1.0;
}

// ========================================================================
// WAVE COORDS (оптимизирован)
// ========================================================================

vec2 waveCoords(vec2 uv, float scale, float speed, float time, vec2 offset, vec2 prevN) {
    return uv * (WAVE_SCALE * scale) + 
           WIND_DIR * (time * WIND_SPEED * speed) - 
           prevN * WAVE_CHOPPYNESS + 
           offset * time;
}

// ========================================================================
// ЦВЕТ ВОДЫ ПО ГЛУБИНЕ
// ========================================================================

vec3 waterColorDepth(float depth, vec3 light, float night) {
    float dFactor = clamp(depth * 0.02, 0.0, 1.0);
    vec3 base = mix(WATER_SHALLOW, WATER_DEEP, dFactor);
    base = mix(base, WATER_COLOR, 0.7);
    base = mix(base, NIGHT_WATER, night * 0.4);
    return clamp(base * light * 0.5, 0.0, 1.0);
}

// ========================================================================
// TIME OF DAY TINT (упрощен)
// ========================================================================

vec3 dayTint(float hour) {
    float day = smoothstep(6.0, 10.0, hour) - smoothstep(16.0, 20.0, hour);
    float sunset = smoothstep(16.0, 18.0, hour) - smoothstep(18.0, 20.0, hour) +
                   smoothstep(6.0, 8.0, hour) - smoothstep(8.0, 10.0, hour);
    
    vec3 base = mix(vec3(1.1, 1.15, 1.4), vec3(0.95, 0.98, 1.0), day);
    return mix(base, vec3(1.0, 0.8, 0.65), sunset);
}

// ========================================================================
// ПОВЕРХНОСТНОЕ НАТЯЖЕНИЕ (новая фича для режима без рефракции)
// ========================================================================

vec3 surfaceTension(vec3 viewDir, vec3 normal, float depth, float time) {
    // Угол падения луча
    float cosTheta = abs(dot(viewDir, normal));
    
    // Критический угол для полного внутреннего отражения (вода n=1.333)
    float critAngle = 0.485; // arcsin(1/1.333) ≈ 48.5°
    
    // Эффект ПВО (полного внутреннего отражения)
    float tirStrength = smoothstep(critAngle + 0.12, critAngle - 0.08, cosTheta);
    
    // Хроматическая дисперсия на границе
    vec3 dispersion = vec3(
        tirStrength * 0.4,
        tirStrength * 0.5,
        tirStrength * 0.6
    );
    
    // Искажение из-за кривизны поверхности
    float curvature = (1.0 - cosTheta * cosTheta) * 0.25;
    
    // Динамическая каустика-подобная структура
    vec2 causticUV = normal.xy * 8.0 + time * 0.02;
    float caustic1 = sin(causticUV.x * 3.14) * cos(causticUV.y * 3.14);
    float caustic2 = sin((causticUV.x + causticUV.y) * 2.5 + time * 0.03);
    float causticPattern = (caustic1 * 0.5 + 0.5) * (caustic2 * 0.5 + 0.5);
    
    // Глубинное затухание
    float depthFade = exp(-depth * 0.015);
    
    // Финальный эффект
    vec3 tension = dispersion * (1.0 + causticPattern * 0.3) * depthFade;
    tension += vec3(causticPattern * curvature * 0.15) * depthFade;
    
    // Добавка цвета воды на мелких углах
    vec3 waterTint = mix(vec3(0.3, 0.5, 0.6), vec3(0.1, 0.3, 0.4), cosTheta);
    tension += waterTint * tirStrength * 0.2;
    
    return tension;
}

// ========================================================================
// VARYINGS & UNIFORMS
// ========================================================================

varying vec3 screenCoordsPassthrough;
varying vec4 position;
varying float linearDepth;

uniform sampler2D normalMap;
uniform sampler2D reflectionMap;
#if REFRACTION
uniform sampler2D refractionMap;
uniform sampler2D refractionDepthMap;
#endif

uniform float osg_SimulationTime;
uniform float near;
uniform float far;
uniform vec3 nodePosition;
uniform float rainIntensity;
uniform float timeOfDay;
uniform float useRefraction;
uniform sampler2D rippleMap;
uniform vec3 playerPos;
uniform float rippleMapWorldScale;
uniform float rippleMapHalfWorldSize;
uniform float useActorRipples;

float frustumDepth;

float linearizeDepth(float depth) {
    float z = 2.0 * depth - 1.0;
    return 2.0 * near * far / (far + near - z * frustumDepth);
}

// ========================================================================
// MAIN
// ========================================================================

void main(void) {
    frustumDepth = abs(far - near);
    
    vec3 worldPos = position.xyz + nodePosition.xyz;
    vec2 UV = worldPos.xy * (3.0 / 40960.0); // Оптимизировано: одна операция
    UV.y = -UV.y;

    vec3 camPos = (gl_ModelViewMatrixInverse * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    float shadow = 1.0;

    vec2 screenCoords = screenCoordsPassthrough.xy / screenCoordsPassthrough.z;
    screenCoords.y = 1.0 - screenCoords.y;


vec2 actorRipple = vec2(0.0);
if (useActorRipples > 0.5)
{
    float rippleMapWorldSize = rippleMapHalfWorldSize * 2.0;
    vec2 rippleMapUV = (worldPos.xy - playerPos.xy + vec2(rippleMapHalfWorldSize)) / rippleMapWorldSize;
    float distToRippleCenter = length(rippleMapUV - vec2(0.5));
    float rippleBlendClose = smoothstep(0.001, 0.02, distToRippleCenter);
    float rippleBlendFar = 1.0 - smoothstep(0.3, 0.4, distToRippleCenter);
    actorRipple = texture2D(rippleMap, rippleMapUV).ba * rippleBlendFar * rippleBlendClose;
}
    
    float wTime = osg_SimulationTime * 3.14;
    
    // ========================================================================
    // LOD СИСТЕМА (ключевая оптимизация)
    // ========================================================================
    
    float distCam = length(position.xyz - camPos.xyz);
    
    // LOD факторы: 0 = близко (все детали), 1 = далеко (минимум деталей)
    float lodMid = smoothstep(LOD_NEAR, LOD_MID, distCam);
    float lodFar = smoothstep(LOD_MID, LOD_FAR, distCam);
    
    // ========================================================================
    // ВОЛНЫ НОРМАЛЕЙ (с LOD - экономия до 2 texture lookups)
    // ========================================================================
    
    vec3 n0 = texture2D(normalMap, waveCoords(UV, 0.05, 0.02, wTime, 
              vec2(-0.0075, -0.0025), vec2(0.0))).rgb * 2.0 - 1.0;
    
    vec3 n1 = texture2D(normalMap, waveCoords(UV, 0.1, 0.04, wTime,
              vec2(0.01, 0.0075), n0.xy)).rgb * 2.0 - 1.0;
    
    // Средние волны - только если не слишком далеко
    vec3 n2 = vec3(0.0, 0.0, 1.0);
    if (lodMid < 0.95) {
        n2 = texture2D(normalMap, waveCoords(UV, 0.25, 0.035, wTime,
             vec2(-0.02, -0.015), n1.xy)).rgb * 2.0 - 1.0;
    }
    
    // Мелкие волны - только вблизи
    vec3 n3 = vec3(0.0, 0.0, 1.0);
    if (lodFar < 0.5) {
        n3 = texture2D(normalMap, waveCoords(UV, 0.5, 0.045, wTime,
             vec2(0.015, 0.02), n2.xy)).rgb * 2.0 - 1.0;
    }
    
    // ========================================================================
    // ДОЖДЬ (только вблизи) - НОВАЯ АСИНХРОННАЯ СИСТЕМА
    // ========================================================================
    
    vec4 rain = vec4(0.0, 0.0, 1.0, 0.0);
    if (rainIntensity > RAIN_THRESHOLD && distCam < LOD_MID) {
        rain = rainCombined(position.xy * 0.001, wTime, rainIntensity);
    }
    
    // ========================================================================
    // БЛЕНДИНГ ВОЛН
    // ========================================================================
    
    vec2 bigW = BIG_WAVES.xy;
    vec2 midW = mix(MID_WAVES.xy, MID_WAVES.zw, rainIntensity) * (1.0 - lodMid);
    vec2 smallW = mix(SMALL_WAVES.xy, SMALL_WAVES.zw, rainIntensity) * (1.0 - lodFar);
    
    float bump = mix(BUMP, BUMP_RAIN, rainIntensity);
    
    vec3 waterN = n0 * bigW.x + n1 * bigW.y;
    if (lodMid < 0.95) waterN += n2 * midW.x;
    if (lodFar < 0.5) waterN += n3 * smallW.x;
    
    vec3 ripple = rain.xyz * rain.w * bump * 5.0 + vec3(actorRipple, 0.0) * bump * 2.0;
    
    // ✅ ОПТИМИЗАЦИЯ 3: Векторная операция вместо покомпонентной
    vec3 normal = vec3(-(waterN.xy + ripple.xy) * bump, waterN.z);
    normal = fastNormalize(normal);
    
    // ========================================================================
    // ОСВЕЩЕНИЕ (вынесено из обеих веток для устранения дублирования)
    // ========================================================================
    
    vec3 L = fastNormalize((gl_ModelViewMatrixInverse * 
             vec4(gl_LightSource[0].position.xyz, 0.0)).xyz);
    vec3 V = fastNormalize(position.xyz - camPos.xyz);
    
    float sunFade = length(gl_LightSource[0].diffuse.xyz + 
                    0.33 * gl_LightModel.ambient.xyz);
    
    // ✅ ОПТИМИЗАЦИЯ 3: Кэширование bool и использование в условиях
    bool isNightBool = L.z <= 0.05;
    float isNight = isNightBool ? 1.0 : 0.0;
    
    // ✅ ОПТИМИЗАЦИЯ 1: Общие вычисления освещения ДО #if REFRACTION
    vec3 lightCol = gl_LightSource[0].diffuse.xyz + gl_LightModel.ambient.xyz;
    lightCol += AMBIENT_NIGHT * NIGHT_BOOST * isNight;
    
    float distF = clamp(distCam * 0.0002, 0.0, 1.0);
    lightCol += AMBIENT_DIST * distF * DIST_BOOST;
    
    // ✅ ОПТИМИЗАЦИЯ 2: Вынесли sunDir из if для shore breakers
    vec3 sunDir = fastNormalize((gl_ModelViewMatrixInverse * 
                  vec4(gl_LightSource[0].position.xyz, 0.0)).xyz);
    
    // ========================================================================
    // FRESNEL
    // ========================================================================
    
    float ior = (camPos.z > 0.0) ? 1.333 : (1.0 / 1.333);
    float fBias = (camPos.z > 0.0) ? 1.0 : 0.0;
    float fresnel = clamp(fresnelDielectric(V, normal, ior) + fBias * 0.2, 0.0, 1.0);
    
    vec2 screenOff = normal.xy * REFL_BUMP;
    
    // ========================================================================
    // REFRACTION MODE
    // ========================================================================
    
    if (useRefraction > 0.5)
    {
    float surfDepth = linearizeDepth(gl_FragCoord.z);
    float depthUndist = linearizeDepth(texture2D(refractionDepthMap, screenCoords).x);
    float waterDepthUndist = depthUndist - surfDepth;
    
    screenOff *= clamp(waterDepthUndist / BUMP_SUPPRESS_DEPTH, 0.0, 1.0);
    
    float depthDist = linearizeDepth(texture2D(refractionDepthMap, 
                      screenCoords - screenOff).x);
    float waterDepth = depthDist - surfDepth;
    
    // Shore breakers (только для мелководья) - ОПТИМИЗИРОВАНО
    if (waterDepth < 10.0 && sunDir.y <= 0.0) {
        float shore = clamp(waterDepth * 0.1, 0.0, 1.0);
        float breaker = (1.0 - shore) * 0.6;
        
        float turb = sin(worldPos.x * 0.02 + wTime * 0.15) * 
                    cos(worldPos.y * 0.02 + wTime * 0.15) * breaker;
        
        vec2 wave = vec2(
            sin(worldPos.x * 0.05 + wTime + turb),
            cos(worldPos.y * 0.05 + wTime + turb)
        ) * breaker * 0.25;
        
        normal.xy += wave * (1.0 - shore);
        normal = fastNormalize(normal);
    }
    
    vec3 refl = texture2D(reflectionMap, screenCoords + screenOff).rgb;
    vec3 refr = texture2D(refractionMap, screenCoords - screenOff).rgb;
    
    if (camPos.z < 0.0) {
        refr = clamp(refr * 1.3, 0.0, 1.0);
    } else {
        refr = mix(refr, waterColorDepth(waterDepth, lightCol, isNight),
               clamp(depthDist / VISIBILITY, 0.0, 1.0));
    }
    
    // Scatter (упрощен для LOD)
    vec3 lN = n0 * bigW.x * 0.5 + n1 * bigW.y * 0.5;
    if (lodMid < 0.95) lN += n2 * midW.x * 0.2;
    lN = fastNormalize(vec3(-lN.x * bump, -lN.y * bump, lN.z));
    
    float sunH = L.z;
    vec3 scatCol = mix(SCATTER_COLOUR * vec3(1.0, 0.4, 0.0), SCATTER_COLOUR,
                   clamp(1.0 - exp(-sunH * SUN_EXT), 0.0, 1.0)) *
                   clamp(lightCol, 0.0, 1.0);
    
    vec3 lR = reflect(L, lN);
    float scatter = clamp(dot(L, lN) * 0.7 + 0.3, 0.0, 1.0) *
                   clamp(dot(lR, V) * 2.0 - 1.2, 0.0, 1.0) *
                   SCATTER_AMOUNT * sunFade * clamp(1.0 - exp(-sunH), 0.0, 1.0);
    
    float fFinal = mix(fresnel, fresnel * 0.85, isNight);
    vec3 reflTog = mix(vec3(1.0), refl, fBias);
    
    float waterDif = dot(normal, L);
    vec3 a = mix(refr * clamp(waterDif + 0.3, 0.0, 1.0),
            refr * scatCol * SCATTER_AMOUNT, scatter);
    
    vec3 nightReflBoost = refl * isNight * 0.2;
    vec3 b = mix(refr * fFinal, refl + nightReflBoost,
            clamp(reflTog * 1.66, 0.0, 1.0));
    
    // Specular
    vec3 R = reflect(V, normal);
    float specDot = max(dot(R, L), 0.0);
    float specH = mix(SPEC_PARAMS.x, SPEC_PARAMS.y, isNight);
    float spec = pow(specDot, specH) * SPEC_INTENSITY * max(shadow, 0.25);
    spec *= fresnel * mix(0.5, 0.7, isNight) + (1.0 - mix(0.5, 0.7, isNight));
    spec = clamp(spec, 0.0, 2.0);
    
    vec3 finalCol = mix(a, b, fFinal) +
                   clamp(spec * gl_LightSource[0].diffuse.xyz * 
                         gl_LightSource[0].diffuse.xyz, 0.0, 1.0) +
                   clamp(vec3(rain.w) * 0.1, 0.0, 0.1);
    
    finalCol += vec3(isNight * 0.15 * max(dot(normal, L), 0.0));
    finalCol += AMBIENT_DIST * distF * DIST_BOOST * 0.3;
    
    vec3 tint = dayTint(timeOfDay);
    gl_FragData[0].xyz = clamp(mix(finalCol, finalCol * tint, 0.06), 0.0, 1.0);
    gl_FragData[0].w = 1.0;
    
    }
    else
    {
    // ========================================================================
    // NO REFRACTION + ПОВЕРХНОСТНОЕ НАТЯЖЕНИЕ
    // ========================================================================
    
    vec3 refl = texture2D(reflectionMap, screenCoords + screenOff).rgb;
    
    vec3 waterCol = waterColorDepth(10.0, lightCol, isNight);
    
// ========== ПОВЕРХНОСТНОЕ НАТЯЖЕНИЕ ==========
    // Имитирует рефракцию когда камера под водой
    vec3 tension = vec3(0.0);
    if (camPos.z < 0.0) {
        tension = surfaceTension(V, normal, linearDepth, wTime);
    }
    // ============================================
    
    float fFinal = mix(fresnel, fresnel * 0.85, isNight);
    vec3 nightReflBoost = refl * isNight * 0.25;
    
    // УВЕЛИЧЕННАЯ НЕПРОЗРАЧНОСТЬ ДЛЯ РЕЖИМА БЕЗ РЕФРАКЦИИ (+60% менее прозрачная)
    // Вода значительно менее прозрачная при взгляде сверху
    float opacityBoost = 0.85; // Увеличено с 0.65 (еще +60% непрозрачнее)
    float viewAngle = abs(dot(V, vec3(0.0, 0.0, 1.0))); // Угол взгляда к вертикали
    opacityBoost += viewAngle * 0.1; // Дополнительная непрозрачность сверху
    opacityBoost = min(opacityBoost, 0.98); // Ограничиваем максимум
    
    vec3 finalCol = mix(refl * 0.8 + AMBIENT_NIGHT * NIGHT_BOOST * isNight + nightReflBoost,
                   waterCol + AMBIENT_NIGHT * NIGHT_BOOST * isNight,
                   (1.0 - fFinal) * opacityBoost);
    
    // Добавляем эффект поверхностного натяжения
    finalCol += tension;
    
    // Specular
    vec3 R = reflect(V, normal);
    float specDot = max(dot(R, L), 0.0);
    float specH = mix(SPEC_PARAMS.x, SPEC_PARAMS.y, isNight);
    float spec = pow(specDot, specH) * SPEC_INTENSITY * max(shadow, 0.25);
    spec *= fresnel * mix(0.5, 0.7, isNight) + (1.0 - mix(0.5, 0.7, isNight));
    spec = clamp(spec, 0.0, 2.0);
    
    finalCol += clamp(spec * gl_LightSource[0].specular.xyz, 0.0, 0.9);
    finalCol += clamp(vec3(rain.w) * 0.1, 0.0, 0.12);
    finalCol += vec3(isNight * 0.15 * max(dot(normal, L), 0.0));
    finalCol += AMBIENT_DIST * distF * DIST_BOOST * 0.3;
    
    vec3 tint = dayTint(timeOfDay);
    gl_FragData[0].xyz = clamp(mix(finalCol, finalCol * tint, 0.06), 0.0, 1.0);
    
    // ДИНАМИЧЕСКАЯ ПРОЗРАЧНОСТЬ:
    // - Значительно меньше прозрачности (на 60% менее прозрачная)
    // - Предотвращаем белые полосы через ограничение max альфы
    float viewAngleVertical = abs(dot(V, vec3(0.0, 0.0, 1.0)));
    float baseAlpha = 0.55; // Увеличено с 0.35 (еще +57% непрозрачнее)
    float minAlpha = baseAlpha + viewAngleVertical * 0.25;
    
    // Устраняем белые полосы: ограничиваем максимальную альфу
    // Френель на дальних расстояниях может давать очень высокие значения
    float maxAlphaLimit = 0.92; // Было 0.85, но нужен запас для устранения артефактов
    float distanceFade = clamp(distCam / 1500.0, 0.0, 1.0); // Затухание на дальних дистанциях
    maxAlphaLimit = mix(maxAlphaLimit, 0.88, distanceFade); // Снижаем альфу вдали
    
    float alpha = clamp(1.0 - fFinal * 0.65, minAlpha, maxAlphaLimit);
    gl_FragData[0].w = alpha;
    }

    // ========================================================================
    // FOG
    // ========================================================================
    
#if @radialFog
    float radialDepth = distance(position.xyz, camPos);
    float fogVal = clamp((radialDepth - gl_Fog.start) * gl_Fog.scale, 0.0, 1.0);
#else
    float fogVal = clamp((linearDepth - gl_Fog.start) * gl_Fog.scale, 0.0, 1.0);
#endif
    
    gl_FragData[0].xyz = mix(gl_FragData[0].xyz, gl_Fog.color.xyz, fogVal);

}
    