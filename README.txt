Best-effort backport bundle for EncoreMP/OpenMW 0.47 water improvements.

Included:
- Stage 1: rain ripple detail setting (0/1/2) with denser/better raindrop ripples on shader water
- Stage 2: procedural shader-side actor ripples via RippleSimulation -> water shader uniforms

Notes:
- This is not an exact cherry-pick from upstream OpenMW 0.48/0.49. It is a reconstruction/backport-inspired implementation for the 0.47-era renderer.
- It keeps the old particle ripples for non-shader water and hides them when shader water ripples are enabled.
- If compilation fails around osg::Uniform array API, the most likely fallback is to replace the array uniforms with individually named uniforms.
