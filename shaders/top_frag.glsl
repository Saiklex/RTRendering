#version 420
#extension GL_EXT_gpu_shader4 : enable

// Fragment shader adapted from code by Richard Southern

// Attributes passed on from the vertex shader
smooth in vec3 WSVertexPosition;
smooth in vec3 WSVertexNormal;
smooth in vec2 WSTexCoord;
smooth in vec3 FragPosition;

// A texture unit for storing the 3D texture
uniform samplerCube envMap;
// Set the maximum environment level of detail (cannot be queried from GLSL apparently)
uniform int envMaxLOD = 4;
// Set our gloss map texture
uniform sampler2D glossMap;
// The inverse View matrix
uniform mat4 invV;
// Specify the refractive index for refractions
uniform float refractiveIndex = 1.0;
// get details for the shadow map
uniform sampler2D ShadowMap;
in vec4 ShadowCoord;

// Structure for holding light parameters
struct LightInfo {
  vec4 Position; // Light position in eye coords.
  vec3 La; // Ambient light intensity
  vec3 Ld; // Diffuse light intensity
  vec3 Ls; // Specular light intensity
};

// values to mix between for the wood pattern
uniform vec4 w1 = vec4(0.0/255.0, 0.0/255.0, 0.0/255.0, 1.0);
uniform vec4 w2 = vec4(5.0/255.0, 5.0/255.0, 5.0/255.0, 1.0);

// We'll have a single light in the scene with some default values
uniform LightInfo Light = LightInfo(
          vec4(8.0, 4.0, 8.0, 1.0),   // position
          vec3(0.2, 0.2, 0.2),        // La
          vec3(1.0, 1.0, 1.0),        // Ld
          vec3(1.0, 1.0, 1.0)         // Ls
          );

// The material properties of our object
struct MaterialInfo {
  vec3 Ka; // Ambient reflectivity
  vec3 Kd; // Diffuse reflectivity
  vec3 Ks; // Specular reflectivity
  float Shininess; // Specular shininess factor
};

// The object has a material
uniform MaterialInfo Material = MaterialInfo(
          vec3(1.0, 1.0, 1.0),    // Ka
          vec3(0.0, 0.0, 0.0),    // Kd
          vec3(5.0, 5.0, 5.0),    // Ks
          10.0                    // Shininess
          );

// This is no longer a built-in variable
out vec4 FragColor;

// pseudo random functions
// Obtained from :-
// Patricio Gonzalez Vivo (2015). The Book of Shaders [online].
// [Accessed 2017]. Available from: "http://thebookofshaders.com/edit.php?log=170423184525".
float random (in vec2 st)
{
  return fract(sin(dot(st.xy, vec2(12.9898,78.233))) * 43758.5453123);
}
vec2 random2(vec2 st)
{
  st = vec2( dot(st,vec2(127.1,311.7)), dot(st,vec2(269.5,183.3)) );
  return -1.0 + 2.0*fract(sin(st)*43758.5453123);
}
// end of functions

// value noise functions for wood patterns
// Obtained from :-
// Inogo Quilez (2013)
// [Accessed 2017]. Available from: "https://www.shadertoy.com/view/lsf3WH"
float noise2(vec2 st) {
  vec2 i = floor(st);
  vec2 f = fract(st);

  vec2 u = f*f*(3.0-2.0*f);

  return mix( mix( dot( random2(i + vec2(0.0,0.0) ), f - vec2(0.0,0.0) ),
                   dot( random2(i + vec2(1.0,0.0) ), f - vec2(1.0,0.0) ), u.x),
              mix( dot( random2(i + vec2(0.0,1.0) ), f - vec2(0.0,1.0) ),
                   dot( random2(i + vec2(1.0,1.0) ), f - vec2(1.0,1.0) ), u.x), u.y);
}
// end of functions

// value noise functions for wood patterns
// Obtained from :-
// Inogo Quilez (2013)
// [Accessed 2017]. Available from: "https://www.shadertoy.com/view/lsf3WH"
float noiseW(vec2 st)
{
  vec2 i = floor(st);
  vec2 f = fract(st);
  vec2 u = f*f*(3.0-2.0*f);
  return mix( mix( random( i + vec2(0.0,0.0) ),
                   random( i + vec2(1.0,0.0) ), u.x),
              mix( random( i + vec2(0.0,1.0) ),
                   random( i + vec2(1.0,1.0) ), u.x), u.y);
}
// end of function

// Fractal Brownian Motion
// Modified from :-
// Patricio Gonzalez Vivo (2015). The Book of Shaders [online].
// [Accessed 2017]. Available from: "http://thebookofshaders.com/edit.php?log=170423184525".
float fbm (in vec3 st) {
  //small coordonated adjustment/variation
  st.x-=3.1;
  st.y+=3.0;

  // noise control values
  float value = 0.5;
  float amplitud = 0.500;
  vec3 frequency = st;
  const int octaves=15;

  //pre rotation - not really needed
  vec3 angle;
  angle = vec3(random(st.xy*vec2(0.270,0.320)*3.552)*0.35);
  frequency = (cos(angle),-sin(angle), sin(angle),cos(angle))*frequency;

  // Loop of octaves
  for (int i = 0; i < octaves; i++)
  {
    //rotation turbulence
    angle= vec3(random(frequency.xy*0.3562)*0.35);
    frequency = (cos(angle),-sin(angle), sin(angle),cos(angle))*frequency;
    //
    value += amplitud * random(frequency.xy);
    frequency *= 1.618;
    amplitud *= 0.75;
  }
  return value;
}
// end of function

// Splatter noise function
// Modified from :-
// Patricio Gonzalez Vivo (2015). The Book of Shaders [online].
// [Accessed 2017]. Available from: "http://thebookofshaders.com/edit.php?log=170423184525".
vec3 splatter(vec2 pos)
{
  vec2 st=pos*100.0;
  vec3 splatter = vec3(smoothstep(0.0,0.5,noise2(st*0.4))*1.); // big splatter
  splatter += smoothstep(0.0,0.7,noise2(st*1.0)); // medium drops
      splatter += smoothstep(0.3,0.6,noise2(st*2.0)); // tiny splatter
      return 1.850 - splatter;
}
// end of function

void main() {
  // Calculate the normal (this is the expensive bit in Phong)
  vec3 n = normalize( WSVertexNormal );

  // Calculate the light vector
  vec3 s = normalize( vec3(Light.Position) - WSVertexPosition );

  // Calculate the view vector
  vec3 v = normalize(vec3(-WSVertexPosition));

  // Reflect the light about the surface normal
  vec3 r = reflect( -s, n );

  vec3 lookup = reflect(v,n);

  // This call actually finds out the current LOD
  float lod = textureQueryLod(envMap, lookup).x;

  // Determine the gloss value from our input texture, and scale it by our LOD resolution
  float gloss = (1.0 - texture(glossMap, WSTexCoord*2).r) * float(envMaxLOD);

  // This call determines the current LOD value for the texture map
  vec4 colour = textureLod(envMap, lookup, gloss);

  // work out mixing value from noise and fragment co-ordinate
  float v2 = 2*(noiseW(FragPosition.zx * vec2(2000.0, 14.0)) - noiseW(FragPosition.zx * vec2(2000.0, 64.0)));

  vec4 topColour = mix(w1,w2,v2);

  // create a splatter matrix- used for dirt details on final render
  vec4 pattern_splatter = 0.1*vec4(splatter(FragPosition.xz),1.0);

  // use the FBM function to calculate a matrix that will be mixed with the colour later
  vec4 pattern_just_noise = vec4(vec3(0.0)+fbm(FragPosition*0.00011),1.0);

  // Compute the light from the ambient, diffuse and specular components
  vec3 lightColor = (
          Light.La * Material.Ka +
          Light.Ld * Material.Kd * max( dot(s, n), 0.0 ) +
          Light.Ls * Material.Ks * pow( max( dot(r,v), 0.0 ), Material.Shininess ));

  // create texture from all the inputs
  topColour = topColour+pattern_just_noise*pattern_just_noise*0.02;
  topColour = mix(topColour,pattern_splatter,0.1)*2;

  // obtain shadow values
  float shadeFactor=textureProj(ShadowMap,ShadowCoord).x;
  // increase shadow factor intensity by multiplying it by itself
  shadeFactor=shadeFactor*shadeFactor;

  // apply environment map, lighting and shadow factor then output
  FragColor = mix(topColour,colour,0.05)*shadeFactor*vec4(lightColor, 1.0);
}
