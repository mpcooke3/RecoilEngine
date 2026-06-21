#version 130

#ifdef USE_TEXTURE_ARRAY
	uniform sampler2DArray atlasTex;
#else
	uniform sampler2D      atlasTex;
#endif

uniform sampler2D depthTex;
uniform float softenThreshold;
uniform vec2 softenExponent;
uniform vec4 alphaCtrl = vec4(0.0, 0.0, 0.0, 1.0); //always pass
uniform vec3 fogColor;

in vec4 vCol;
centroid in vec4 vUV;
in float vLayer;
in float vBF;
in float fogFactor;
in vec4 vsPos;
noperspective in vec2 screenUV;

out vec4 fragColor;

#define projMatrix gl_ProjectionMatrix

#define NORM2SNORM(value) (value * 2.0 - 1.0)
#define SNORM2NORM(value) (value * 0.5 + 0.5)

float GetViewSpaceDepth(float d) {
	#ifndef DEPTH_CLIP01
		d = NORM2SNORM(d);
	#endif
	return -projMatrix[3][2] / (projMatrix[2][2] + d);
}

bool AlphaDiscard(float a) {
	float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
	float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;

	return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
}

void main() {
	#ifdef USE_TEXTURE_ARRAY
		vec4 c0 = texture(atlasTex, vec3(vUV.xy, vLayer));
		vec4 c1 = texture(atlasTex, vec3(vUV.zw, vLayer));
	#else
		vec4 c0 = texture(atlasTex, vUV.xy);
		vec4 c1 = texture(atlasTex, vUV.zw);
	#endif

	vec4 color = vec4(mix(c0, c1, vBF));
	fragColor = color * vCol;
	fragColor.rgb = mix(fragColor.rgb, fogColor * fragColor.a, (1.0 - fogFactor));

	#ifdef SMOOTH_PARTICLES
	float depthZO = texture(depthTex, screenUV).x;
	float depthVS = GetViewSpaceDepth(depthZO);

	if (softenThreshold > 0.0) {
		float edgeSmoothness = smoothstep(0.0, softenThreshold, vsPos.z - depthVS); // soften edges
		fragColor *= pow(edgeSmoothness, softenExponent.x);
	} else {
		float edgeSmoothness = smoothstep(softenThreshold, 0.0, vsPos.z - depthVS); // follow the surface up
		fragColor *= pow(edgeSmoothness, softenExponent.y);
	}
	#endif

	if (AlphaDiscard(fragColor.a))
		discard;

#ifdef MAC_FX_DARK_SAFE
	// macOS workaround: under premultiplied-alpha blend
	// (GL_ONE, GL_ONE_MINUS_SRC_ALPHA), each overlapping fragment dims dst
	// by (1 - α). For BAR CEG content like commander-spawn's groundflash_scar
	// (CBitmapMuzzleFlame with rgb fading toward 0), many overlapping dark
	// fragments compound the destination to near-zero — solid black square.
	// On macOS/Zink+KK this seems to be more aggressive than on Linux for
	// reasons we haven't fully isolated (see MAC_GL_STACK_ISSUES.md §7).
	// Workaround: when the fragment's luminance is essentially zero (dead-
	// scar phase of the colormap), output a fully-transparent fragment so
	// it contributes nothing to the framebuffer (1 - 0 = 1, dst unchanged).
	// Brighter / coloured fragments are untouched and look identical.
	float lum = dot(fragColor.rgb, vec3(0.299, 0.587, 0.114));
	if (lum < 0.04) {
		fragColor = vec4(0.0);
	}
#endif
}