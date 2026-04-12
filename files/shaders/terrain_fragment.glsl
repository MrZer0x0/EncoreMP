#version 120

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#include "water_waves.glsl"

varying vec2 uv;

uniform sampler2D diffuseMap;

#if @normalMap
uniform sampler2D normalMap;
#endif

#if @blendMap
uniform sampler2D blendMap;
#endif

varying float euclideanDepth;
varying float linearDepth;

#define PER_PIXEL_LIGHTING (@normalMap || @forcePPL)

#if !PER_PIXEL_LIGHTING
centroid varying vec3 passLighting;
centroid varying vec3 shadowDiffuseLighting;
#endif
varying vec3 passViewPos;
varying vec3 passNormal;

uniform float osg_SimulationTime;
uniform mat4 osg_ViewMatrixInverse;

#include "helpsettings.glsl"
#include "vertexcolors.glsl"
#include "shadows_fragment.glsl"
#include "lighting.glsl"
#include "parallax.glsl"

// ==========================================================================
// ПАРАМЕТРЫ ОГРАНИЧЕНИЯ КАУСТИКИ ПО ДИСТАНЦИИ
// ==========================================================================
const float MAX_CAUSTICS_DISTANCE = 2500.0;  // Максимальная дистанция для каустики
const float CAUSTICS_FADE_START = 1500.0;    // Начало плавного затухания
// ==========================================================================

void main()
{
    vec2 adjustedUV = (gl_TextureMatrix[0] * vec4(uv, 0.0, 1.0)).xy;

#if @normalMap
    vec4 normalTex = texture2D(normalMap, adjustedUV);

    vec3 normalizedNormal = normalize(passNormal);
    vec3 tangent = vec3(1.0, 0.0, 0.0);
    vec3 binormal = normalize(cross(tangent, normalizedNormal));
    tangent = normalize(cross(normalizedNormal, binormal)); // note, now we need to re-cross to derive tangent again because it wasn't orthonormal
    mat3 tbnTranspose = mat3(tangent, binormal, normalizedNormal);

    vec3 viewNormal = normalize(gl_NormalMatrix * (tbnTranspose * (normalTex.xyz * 2.0 - 1.0)));
#endif

#if (!@normalMap && (@parallax || @forcePPL))
    vec3 viewNormal = gl_NormalMatrix * normalize(passNormal);
#endif

#if @parallax
    vec3 parallaxCameraPos = (gl_ModelViewMatrixInverse * vec4(0,0,0,1)).xyz;
    vec3 objectPos = (gl_ModelViewMatrixInverse * vec4(passViewPos, 1)).xyz;
    vec3 eyeDir = normalize(parallaxCameraPos - objectPos);
    adjustedUV += getParallaxOffset(eyeDir, tbnTranspose, normalTex.a, 1.f);

    // update normal using new coordinates
    normalTex = texture2D(normalMap, adjustedUV);
    viewNormal = normalize(gl_NormalMatrix * (tbnTranspose * (normalTex.xyz * 2.0 - 1.0)));
#endif

    vec4 diffuseTex = texture2D(diffuseMap, adjustedUV);
    gl_FragData[0] = vec4(diffuseTex.xyz, 1.0);

#if @blendMap
    vec2 blendMapUV = (gl_TextureMatrix[1] * vec4(uv, 0.0, 1.0)).xy;
    gl_FragData[0].a *= texture2D(blendMap, blendMapUV).a;
#endif

    // Convert to linear space for lighting calculations
    gl_FragData[0].xyz = preLight(gl_FragData[0].xyz);

    vec4 diffuseColor = getDiffuseColor();
    gl_FragData[0].a *= diffuseColor.a;

    float shadowing = unshadowedLightRatio(linearDepth);
    
    vec3 lighting;
#if !PER_PIXEL_LIGHTING
    lighting = passLighting + shadowDiffuseLighting * shadowing;
#else
    vec3 diffuseLight, ambientLight;
    doLighting(passViewPos, normalize(viewNormal), shadowing, diffuseLight, ambientLight);
    lighting = diffuseColor.xyz * diffuseLight + getAmbientColor().xyz * ambientLight + getEmissionColor().xyz;
    clampLightingResult(lighting);
#endif
    
    gl_FragData[0].xyz *= lighting;

#if @specularMap
    float shininess = 128.0; // TODO: make configurable
    vec3 matSpec = vec3(diffuseTex.a);
#else
    float shininess = gl_FrontMaterial.shininess;
    vec3 matSpec = getSpecularColor().xyz;
#endif

    if (matSpec != vec3(0.0))
    {
#if (!@normalMap && !@parallax && !@forcePPL)
        vec3 viewNormal = gl_NormalMatrix * normalize(passNormal);
#endif
        gl_FragData[0].xyz += getSpecular(normalize(viewNormal), normalize(passViewPos), shininess, matSpec) * shadowing;
    }

    // Apply tonemapping after all lighting calculations
    gl_FragData[0].xyz = toneMap(gl_FragData[0].xyz);

    // ==========================================================================
    // OPTIMIZED UNDERWATER WAVE EFFECTS (Caustics and Attenuation)
    // С ОГРАНИЧЕНИЕМ ПО ДИСТАНЦИИ
    // ==========================================================================
    
    // Проверяем позицию КАМЕРЫ
    vec3 cameraPos = (osg_ViewMatrixInverse * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    float cameraWaterH = zDoWaveSimple(cameraPos.xy, osg_SimulationTime);
    bool cameraUnderwater = cameraPos.z < cameraWaterH;
    
    // Check if we're in interior
    vec3 sunPos = lcalcPosition(0);
    vec3 sunDir = normalize(sunPos);
    vec3 sunWDir = (osg_ViewMatrixInverse * vec4(sunDir, 0.0)).xyz;
    bool isInterior = sunWDir.y > 0.0;
    
    vec3 wPos = (osg_ViewMatrixInverse * vec4(passViewPos, 1.0)).xyz;
    float waterH = zDoWaveSimple(wPos.xy, osg_SimulationTime);
    float waterDepth = max(-wPos.z + waterH, 0.0);

    // ==========================================================================
    // ОГРАНИЧЕНИЕ КАУСТИКИ ПО ДИСТАНЦИИ
    // ==========================================================================
    // Рассчитываем дистанцию от камеры до фрагмента
    float distanceToFragment = length(wPos.xy - cameraPos.xy);
    
    // Плавное затухание каустики на дальних расстояниях
    float causticsFade = 1.0;
    if (distanceToFragment > CAUSTICS_FADE_START) {
        causticsFade = 1.0 - smoothstep(CAUSTICS_FADE_START, MAX_CAUSTICS_DISTANCE, distanceToFragment);
    }
    // ==========================================================================

    // OPTIMIZED: Simplified caustics calculation with depth check and distance fade
#if (TERRAIN_CAUSTICS == 1)
    if (!isInterior && wPos.z < waterH && waterDepth > 5.0 && distanceToFragment < MAX_CAUSTICS_DISTANCE) {
        float causticsIntensity = zcaustics(wPos.xy * 0.01, osg_SimulationTime * 0.5) * 2.15;
        float causticsBlend = clamp(waterDepth * 0.0105, 0.0, 0.95) / (1.0 + waterDepth / 1100.0);
        
        // Применяем плавное затухание по дистанции
        causticsBlend *= causticsFade;
        
        gl_FragData[0].xyz *= mix(1.0, 0.5 + causticsIntensity, causticsBlend);
    }
#endif

    // Применяем attenuation ТОЛЬКО если камера под водой
    if (cameraUnderwater && !isInterior && waterDepth > 0.0) {
#if (ATTENUATION == 1)
        gl_FragData[0].xyz = applyUnderwaterMedium(gl_FragData[0].xyz, waterDepth, isInterior);
#endif
    }

    // ==========================================================================
    // END UNDERWATER EFFECTS
    // ==========================================================================

#if @radialFog
    float fogValue = clamp((euclideanDepth - gl_Fog.start) * gl_Fog.scale, 0.0, 1.0);
#else
    float fogValue = clamp((linearDepth - gl_Fog.start) * gl_Fog.scale, 0.0, 1.0);
#endif
    gl_FragData[0].xyz = mix(gl_FragData[0].xyz, gl_Fog.color.xyz, fogValue);

    applyShadowDebugOverlay();
}
