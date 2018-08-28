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
          vec3(1.0, 1.0, 1.0),    // Kd
          vec3(4.0, 4.0, 4.0),    // Ks
          10.0                   // Shininess
          );

// This is no longer a built-in variable
out vec4 FragColor;

// pseudo random functions
// Obtained from :-
// Patricio Gonzalez Vivo (2015). The Book of Shaders [online].
// [Accessed 2017]. Available from: "http://thebookofshaders.com/edit.php?log=170423184525".

float random (in vec2 st)
{
  return fract(sin(dot(st.xy, vec2(12.9898,78.233)))* 43758.5453123);
}
vec2 random2(vec2 st)
{
  st = vec2( dot(st,vec2(127.1,311.7)), dot(st,vec2(269.5,183.3)) );
  return -1.0 + 2.0*fract(sin(st)*43758.5453123);
}
// end of functions

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

  vec4 leadColour = vec4(10.0/255.0, 10.0/255.0, 10.0/255.0, 1.0);

  // create the colour from all the pattern functions
  vec4 pattern_just_noise = vec4(vec3(0.0)+fbm(FragPosition*0.0001),1.0);
  vec4 pattern_indents = vec4(vec3(0.0)+fbm(FragPosition*0.00001),1.0);
  vec4 pattern_noise_complete = mix(pattern_just_noise,pattern_indents,0.6);
  leadColour = mix(leadColour,pattern_noise_complete,0.1);

  // Compute the light from the ambient, diffuse and specular components
  vec3 lightColor = (
          Light.La * Material.Ka +
          Light.Ld * Material.Kd * max( dot(s, n), 0.0 ) +
          Light.Ls * Material.Ks * pow( max( dot(r,v), 0.0 ), Material.Shininess ));


  // obtain shadow values
  float shadeFactor=textureProj(ShadowMap,ShadowCoord).x;
  // increase shadow factor intensity by multiplying it by itself
  shadeFactor=shadeFactor*shadeFactor;

  // apply the environment map to the texture by mixing
  leadColour = mix(leadColour,colour,0.05);

  FragColor = leadColour*shadeFactor*vec4(lightColor, 1.0);
}
