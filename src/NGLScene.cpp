#include <QMouseEvent>
#include <QGuiApplication>
#include "NGLScene.h"
#include <ngl/Camera.h>
#include <ngl/Light.h>
#include <ngl/Material.h>
#include <ngl/NGLInit.h>
#include <ngl/VAOPrimitives.h>
#include <ngl/ShaderLib.h>
#include <ngl/VAOFactory.h>
#include <ngl/MultiBufferVAO.h>
#include <array>


// set up the scene
NGLScene::NGLScene()
{
   m_lightPosition.set(8,4,8);
   setTitle("Rendering");
}


NGLScene::~NGLScene()
{
  std::cout<<"Shutting down NGL, removing VAO's and Shaders\n";
}

void NGLScene::resizeGL( int _w, int _h )
{
  m_cam.setShape( 45.0f, static_cast<float>( _w ) / _h, 0.05f, 350.0f );
  m_win.width  = static_cast<int>( _w * devicePixelRatio() );
  m_win.height = static_cast<int>( _h * devicePixelRatio() );
}

void NGLScene::initializeGL()
{
  constexpr float znear=0.1f;
  constexpr float zfar=100.0f;
  // we must call this first before any other GL commands to load and link the
  // gl commands from the lib, if this is not done program will crash
  ngl::NGLInit::instance();
  // Grey Background
  glClearColor(0.4f, 0.4f, 0.4f, 1.0f);
  // enable depth testing for drawing
  glEnable(GL_DEPTH_TEST);
  // enable multisampling for smoother drawing
  glEnable(GL_MULTISAMPLE);

  // create a basic Camera from the graphics library
  ngl::Vec3 from(0,2,6);
  ngl::Vec3 to(0,0,0);
  ngl::Vec3 up(0,1,0);
  // now load to our new camera
  m_cam.set(from,to,up);
  // set the shape using FOV 45 Aspect Ratio based on Width and Height
  // The final two are near and far clipping planes of 0.5 and 10
  m_cam.setShape(45,720.0f/576.0f,znear,zfar);

  // now load to our light POV camera
  m_lightCamera.set(m_lightPosition,to,up);
  // set the light POV camera shape
  m_lightCamera.setShape(45,float(width()/height()),znear,zfar);

  // in this case I'm only using the light to hold the position
  // it is not passed to the shader directly
  m_lightAngle=0.0f;

  // grab an instance of shader manager
  ngl::ShaderLib *shader=ngl::ShaderLib::instance();
  // load all our shaders
  shader->loadShader("Colour",     "shaders/ColourVert.glsl", "shaders/ColourFrag.glsl");
  shader->loadShader("bodyShader",     "shaders/phong_vert.glsl", "shaders/body_frag.glsl");
  shader->loadShader("leadShader",     "shaders/phong_vert.glsl", "shaders/lead_frag.glsl");
  shader->loadShader("sharpenedShader","shaders/phong_vert.glsl", "shaders/sharpened_frag.glsl");
  shader->loadShader("topShader",      "shaders/phong_vert.glsl", "shaders/top_frag.glsl");

  // create the primitive for the plane
  ngl::VAOPrimitives *prim=ngl::VAOPrimitives::instance();
  prim->createTrianglePlane("plane",14,14,80,80,ngl::Vec3(0,1,0));

  // load objs
  m_body.reset(new ngl::Obj("data/body.obj"));
  m_body->createVAO();
  m_sharpened.reset(new ngl::Obj("data/sharpened.obj"));
  m_sharpened->createVAO();
  m_lead.reset(new ngl::Obj("data/lead.obj"));
  m_lead->createVAO();
  m_top.reset(new ngl::Obj("data/top.obj"));
  m_top->createVAO();

  // initialise environment map
  initEnvironment();
  // initialise gloss texture map
  initTexture(1, m_glossMapTex, "images/gloss.png");

  // create our FBO and texture
  createFramebufferObject();

  // set the depth comparison mode
  glDepthFunc(GL_LEQUAL);
  // enable face culling this will be switch to front and back when rendering shadow or scene
  glEnable(GL_CULL_FACE);
  // set the scale and units used to calculate depth values
  glPolygonOffset(1.1f,4);
  m_text.reset(  new ngl::Text(QFont("Ariel",14)));
  glViewport(0, 0, width() * devicePixelRatio(), height() * devicePixelRatio());
}

// set these for the viewport
constexpr int TEXTURE_WIDTH=1024;
constexpr int TEXTURE_HEIGHT=768;

// the next three functions are adapted from Jon Macey's FBO Shadow demo
void NGLScene::loadMatricesToShader(std::string program)
{
  // initialise shader instance and load required shader
  ngl::ShaderLib *shader=ngl::ShaderLib::instance();
  shader->use(program);
  GLint pid = shader->getProgramID(program);

  // set uniforms for the environment map
  glUniform1i(glGetUniformLocation(pid,"glossMap"),1);
  glUniform1i(glGetUniformLocation(pid,"envMap"),2);

  ngl::Mat4 MV;
  ngl::Mat4 MVP;
  ngl::Mat3 normalMatrix;
  ngl::Mat4 M;

  // calculate matrices
  M=m_transform.getMatrix()*m_mouseGlobalTX;
  MV=  M*m_cam.getViewMatrix();
  MVP= M*m_cam.getVPMatrix();
  normalMatrix=MV;
  normalMatrix.inverse();

  // pass matrices and light data to shader
  shader->setRegisteredUniformFromMat4("MV",MV);
  shader->setRegisteredUniformFromMat4("MVP",MVP);
  shader->setRegisteredUniformFromMat3("N",normalMatrix);
  shader->setRegisteredUniform3f("LightPosition",m_lightPosition.m_x,m_lightPosition.m_y,m_lightPosition.m_z);
  shader->setRegisteredUniform4f("inColour",1,1,1,1);

  // calculate values needed for shadow texture location and apply to shader
  ngl::Mat4 bias;
  bias.scale(0.5,0.5,0.5);
  bias.translate(0.5,0.5,0.5);
  ngl::Mat4 view=m_lightCamera.getViewMatrix();
  ngl::Mat4 proj=m_lightCamera.getProjectionMatrix();
  ngl::Mat4 model=m_transform.getMatrix();
  ngl::Mat4 textureMatrix= model * view*proj * bias;
  shader->setRegisteredUniformFromMat4("textureMatrix",textureMatrix);
}

void NGLScene::loadToLightPOVShader()
{
  ngl::ShaderLib *shader=ngl::ShaderLib::instance();
  shader->use("Colour");
  ngl::Mat4 MVP=m_transform.getMatrix()* m_lightCamera.getVPMatrix();
  shader->setRegisteredUniformFromMat4("MVP",MVP);
}

void NGLScene::drawScene(bool lightRender)
{
  // Rotation based on the mouse position for our global transform
  ngl::Mat4 rotX;
  ngl::Mat4 rotY;
  // create the rotation matrices
  rotX.rotateX(m_win.spinXFace);
  rotY.rotateY(m_win.spinYFace);
  // multiply the rotations
  m_mouseGlobalTX=rotY*rotX;
  // add the translations
  m_mouseGlobalTX.m_m[3][0] = m_modelPos.m_x;
  m_mouseGlobalTX.m_m[3][1] = m_modelPos.m_y;
  m_mouseGlobalTX.m_m[3][2] = m_modelPos.m_z;

   // initialise primitives for drawing the plane later
  ngl::VAOPrimitives *prim=ngl::VAOPrimitives::instance();

  m_transform.setScale(5.0f,5.0f,5.0f);
  m_transform.setPosition(0.0f,-0.5f,0.0f);


  if(lightRender==true){
    loadToLightPOVShader();
    m_body->draw();
    m_top->draw();
    m_sharpened->draw();
    m_lead->draw();
    m_transform.reset();
    m_transform.setPosition(0.0f,-0.5f,0.0f);
    prim->draw("plane");
  }else
  {
    loadMatricesToShader("bodyShader");
    m_body->draw();
    loadMatricesToShader("topShader");
    m_top->draw();
    loadMatricesToShader("sharpenedShader");
    m_sharpened->draw();
    loadMatricesToShader("leadShader");
    m_lead->draw();
    loadMatricesToShader("bodyShader");
    m_transform.reset();
    m_transform.setPosition(0.0f,-0.5f,0.0f);
    prim->draw("plane");
  }


}

// end of functions

void NGLScene::paintGL()
{
  // Pass 1 render our Depth texture to the FBO
  // enable culling
  glEnable(GL_CULL_FACE);
  // bind the FBO and render offscreen to the texture
  glBindFramebuffer(GL_FRAMEBUFFER,m_fboID);
  // bind the texture object to 0 (off )
  glBindTexture(GL_TEXTURE_2D,0);
  glDisable(GL_CULL_FACE);
  m_text->renderText(250,60,"text");
  glViewport(0,0,TEXTURE_WIDTH,TEXTURE_HEIGHT);
  // Clear previous frame values
  glClear( GL_DEPTH_BUFFER_BIT);
  // as we are only rendering depth turn off the colour / alpha
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  // render only the back faces so we don't get too much self shadowing
  glCullFace(GL_FRONT);
  // draw the scene from the POV of the light using the function we need
  drawScene(true);



  // Pass 2 render and use the FBO
  // go back to our normal framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER,0);
  // set the viewport to the screen dimensions
  glViewport(0, 0, width() * devicePixelRatio(), height() * devicePixelRatio());
  // enable colour rendering again (as we turned it off earlier)
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  // clear the screen
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  // bind the shadow texture
  glBindTexture(GL_TEXTURE_2D,m_textureID);
  // now only cull back faces
  glDisable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  // render our scene with the shadow shader
  drawScene(false);
}

// This function was written by Jon Macey, from his FBO Shadow demo
void NGLScene::createFramebufferObject()
{

  // Try to use a texture depth component
  glGenTextures(1, &m_textureID);
  glBindTexture(GL_TEXTURE_2D, m_textureID);
  //glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  //glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE);
  //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);

  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

  glTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, TEXTURE_WIDTH, TEXTURE_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);

  glBindTexture(GL_TEXTURE_2D, 0);

  // create our FBO
  glGenFramebuffers(1, &m_fboID);
  glBindFramebuffer(GL_FRAMEBUFFER, m_fboID);
  // disable the colour and read buffers as we only want depth
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);

  // attach our texture to the FBO

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D, m_textureID, 0);

  // switch back to window-system-provided framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
// end of function

void NGLScene::keyPressEvent(QKeyEvent *_event)
{
  // this method is called every time the main window recives a key event.
  // we then switch on the key value and set the camera in the GLWindow
  switch (_event->key())
  {
  // escape key to quite
  case Qt::Key_Escape : QGuiApplication::exit(EXIT_SUCCESS); break;
  // show full screen
  case Qt::Key_F : showFullScreen(); break;
  // show windowed
  case Qt::Key_N : showNormal(); break;
  default : break;
  }

  // finally update the GLWindow and re-draw
  update();
}

void NGLScene::timerEvent(QTimerEvent *_event )
{
  // re-draw GL
  update();
}

// These three functions for the Environment map are all originally written by Richard Southern, from his Environment Mapping workshop
void NGLScene::initTexture(const GLuint& texUnit, GLuint &texId, const char *filename) {
    // Set our active texture unit
    glActiveTexture(GL_TEXTURE1 + texUnit);

    // Load up the image using NGL routine
    ngl::Image img(filename);

    // Create storage for our new texture
    glGenTextures(1, &texId);

    // Bind the current texture
    glBindTexture(GL_TEXTURE_2D, texId);

    // Transfer image data onto the GPU using the teximage2D call
    glTexImage2D (
                GL_TEXTURE_2D,    // The target (in this case, which side of the cube)
                0,                // Level of mipmap to load
                img.format(),     // Internal format (number of colour components)
                img.width(),      // Width in pixels
                img.height(),     // Height in pixels
                0,                // Border
                GL_RGB,          // Format of the pixel data
                GL_UNSIGNED_BYTE, // Data type of pixel data
                img.getPixels()); // Pointer to image data in memory

    // Set up parameters for our texture
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void NGLScene::initEnvironment() {
    // Enable seamless cube mapping
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    // Placing our environment map texture in texture unit 0
    glActiveTexture (GL_TEXTURE2);

    // Generate storage and a reference for our environment map texture
    glGenTextures (1, &m_envTex);

    // Bind this texture to the active texture unit
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envTex);

    // Now load up the sides of the cube
    initEnvironmentSide(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, "images/negz.jpg");
    initEnvironmentSide(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, "images/posz.jpg");
    initEnvironmentSide(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, "images/posy.jpg");
    initEnvironmentSide(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, "images/negy.jpg");
    initEnvironmentSide(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, "images/negx.jpg");
    initEnvironmentSide(GL_TEXTURE_CUBE_MAP_POSITIVE_X, "images/posx.jpg");

    // Generate mipmap levels
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // Set the texture parameters for the cube map
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLfloat anisotropy;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropy);
}

void NGLScene::initEnvironmentSide(GLenum target, const char *filename) {
    // Load up the image using NGL routine
    ngl::Image img(filename);


    // Transfer image data onto the GPU using the teximage2D call
    glTexImage2D (
      target,           // The target (in this case, which side of the cube)
      0,                // Level of mipmap to load
      img.format(),     // Internal format (number of colour components)
      img.width(),      // Width in pixels
      img.height(),     // Height in pixels
      0,                // Border
      GL_RGBA,          // Format of the pixel data
      GL_UNSIGNED_BYTE, // Data type of pixel data
      img.getPixels()   // Pointer to image data in memory
    );
}
// end of functions
