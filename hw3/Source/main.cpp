#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include "parser.h"

using parser::Vec3f;
// Sample usage for reading an XML scene file
parser::Scene scene;
static GLFWwindow* win = NULL;
static bool lights_enabled = true;
static bool light_disabled[10];

char gRendererInfo[512] = {0};
char gWindowTitle[512] = {0};

int gWidth, gHeight;
void Init() {
  glEnable(GL_DEPTH_TEST);

  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_NORMAL_ARRAY);
}

static void errorCallback(int error, const char* description) {
  fprintf(stderr, "Error(%d): %s\n", error, description);
}

void SetCamera() {
  const parser::Camera camera = scene.camera;
  const Vec3f eye = camera.position;
  const Vec3f center = eye + camera.gaze * camera.near_distance;
  const Vec3f v = camera.up;
  const parser::Vec4f near_plane = camera.near_plane;
  glLoadIdentity();
  gluLookAt(eye.x, eye.y, eye.z, center.x, center.y, center.z, v.x, v.y, v.z);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glFrustum(near_plane.x, near_plane.y, near_plane.z, near_plane.w,
            camera.near_distance, camera.far_distance);
  glMatrixMode(GL_MODELVIEW);
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action,
                        int mods) {
  (void)scancode;
  (void)mods;
  if (action == GLFW_PRESS || action == GLFW_REPEAT) {
    if (GLFW_KEY_0 < key && key < GLFW_KEY_9) {
      const size_t light_index = key - GLFW_KEY_0 - 1;
      std::cout << "Toggling: " << light_index << std::endl;
      light_disabled[light_index] ? glEnable(GL_LIGHT0 + light_index)
                                  : glDisable(GL_LIGHT0 + light_index);
      light_disabled[light_index] ^= 1;
    }
    switch (key) {
      case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
      case GLFW_KEY_0:
        lights_enabled ? glDisable(GL_LIGHTING) : glEnable(GL_LIGHTING);
        lights_enabled ^= 1;
        break;
      case GLFW_KEY_W:
        scene.camera.position += scene.camera.gaze.Normalized() * .05;
        SetCamera();
        break;
      case GLFW_KEY_S:
        scene.camera.position += scene.camera.gaze.Normalized() * -.05;
        SetCamera();
        break;
      case GLFW_KEY_A:
        parser::Camera& camera = scene.camera;
        const parser::Rotation rot = {0.5f, camera.up.x, camera.up.y,
                                      camera.up.z};
        camera.gaze = rot.ToMatrix().MultiplyVector(camera.gaze);
        camera.right = camera.gaze.CrossProduct(camera.up);
        camera.up = camera.right.CrossProduct(camera.gaze);
        SetCamera();
        break;
      case GLFW_KEY_D:
        parser::Camera& camera = scene.camera;
        const parser::Rotation rot = {-0.5f, camera.up.x, camera.up.y,
                                      camera.up.z};
        camera.gaze = rot.ToMatrix().MultiplyVector(camera.gaze);
        camera.right = camera.gaze.CrossProduct(camera.up);
        camera.up = camera.right.CrossProduct(camera.gaze);
        SetCamera();
        break;
    }
  }
}
void applyTransformation(const parser::Transformation& transformation) {
  const std::string& type = transformation.transformation_type;
  if (type[0] == 'T') {
    const Vec3f& translation = scene.translations[transformation.id];
    glTranslatef(translation.x, translation.y, translation.z);
  } else if (type[0] == 'S') {
    const Vec3f& scaling = scene.scalings[transformation.id];
    glScalef(scaling.x, scaling.y, scaling.z);
  } else {
    const parser::Vec4f& rotation = scene.rotations[transformation.id];
    glRotatef(rotation.x, rotation.y, rotation.z, rotation.w);
  }
}

void display() {
  static bool firstTime = true;
  static size_t vertexPosDataSizeInBytes = 0;

  if (firstTime) {
    firstTime = false;

    // Store vertex data.
    GLuint vertexAttribBuffer;
    const std::vector<Vec3f> vertex_data = scene.vertex_data;
    vertexPosDataSizeInBytes = vertex_data.size() * 3 * sizeof(GLfloat);
    const size_t vertex_count = vertex_data.size();
    std::vector<Vec3f> normal_data(vertex_count);
    std::vector<size_t> normal_count(vertex_count);
    GLfloat* vertex_pos = new GLfloat[vertex_count * 3];
    int idx = 0;
    for (const Vec3f& vertex : vertex_data) {
      vertex_pos[idx++] = vertex.x;
      vertex_pos[idx++] = vertex.y;
      vertex_pos[idx++] = vertex.z;
    }
    glGenBuffers(1, &vertexAttribBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexAttribBuffer);
    glBufferData(GL_ARRAY_BUFFER, 2 * vertexPosDataSizeInBytes, 0,
                 GL_STATIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexPosDataSizeInBytes, vertex_pos);
    delete[] vertex_pos;

    // Store indices.
    GLuint index_buffer;
    glGenBuffers(1, &index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
    GLuint* indices = new GLuint[scene.face_count * 3];
    GLfloat* vertex_normals = new GLfloat[vertex_data.size() * 3];
    int indices_idx = 0;
    const std::vector<parser::Mesh> meshes = scene.meshes;
    for (const parser::Mesh& mesh : meshes) {
      const std::vector<parser::Face> faces = mesh.faces;
      for (const parser::Face& face : faces) {
        indices[indices_idx++] = face.v0_id;
        indices[indices_idx++] = face.v1_id;
        indices[indices_idx++] = face.v2_id;
        normal_data[face.v0_id] += face.normal;
        normal_data[face.v1_id] += face.normal;
        normal_data[face.v2_id] += face.normal;
        normal_count[face.v0_id]++;
        normal_count[face.v1_id]++;
        normal_count[face.v2_id]++;
      }
    }
    for (size_t i = 0; i < vertex_count; i++) {
      const Vec3f normal = normal_data[i].Normalized();
      vertex_normals[3 * i] = normal.x;
      vertex_normals[3 * i + 1] = normal.y;
      vertex_normals[3 * i + 2] = normal.z;
    }
    int indexDataSizeInBytes = scene.face_count * 3 * sizeof(GLuint);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexDataSizeInBytes, indices,
                 GL_STATIC_DRAW);
    delete[] indices;
    glBufferSubData(GL_ARRAY_BUFFER, vertexPosDataSizeInBytes,
                    vertexPosDataSizeInBytes, vertex_normals);
    delete[] vertex_normals;
  }

  glVertexPointer(3, GL_FLOAT, 0, 0);
  glNormalPointer(GL_FLOAT, 0, (const void*)vertexPosDataSizeInBytes);
  size_t mesh_face_offset = 0;
  for (const parser::Mesh& mesh : scene.meshes) {
    glPushMatrix();
    for (int index = mesh.transformations.size() - 1; index >= 0; index--) {
      const parser::Transformation& transformation =
          mesh.transformations[index];
      applyTransformation(transformation);
    }
    const parser::Material material = scene.materials[mesh.material_id];
    const GLfloat ambient[4] = {material.ambient.x, material.ambient.y,
                                material.ambient.z, 1.};
    const GLfloat diffuse[4] = {material.diffuse.x, material.diffuse.y,
                                material.diffuse.z, 1.};
    const GLfloat specular[4] = {material.specular.x, material.specular.y,
                                 material.specular.z, 1.};
    const GLfloat phong_exponent[1] = {material.phong_exponent};

    glMaterialfv(GL_FRONT, GL_AMBIENT, ambient);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, diffuse);
    glMaterialfv(GL_FRONT, GL_SPECULAR, specular);
    glMaterialfv(GL_FRONT, GL_SHININESS, phong_exponent);
    if (mesh.mesh_type == "Solid") {
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    } else {
      glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    glDrawElements(GL_TRIANGLES, mesh.faces.size() * 3, GL_UNSIGNED_INT,
                   (const void*)(mesh_face_offset * 3 * sizeof(GLuint)));
    mesh_face_offset += mesh.faces.size();
    glPopMatrix();
  }
}

void render() {
  static int framesRendered = 0;
  static std::chrono::time_point<std::chrono::system_clock> start =
      std::chrono::system_clock::now();

  glClearColor(scene.background_color.x, scene.background_color.y,
               scene.background_color.z, 1);
  glClearDepth(1.0f);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  ++framesRendered;
  display();

  std::chrono::time_point<std::chrono::system_clock> end =
      std::chrono::system_clock::now();
  std::chrono::duration<double> elapsedTime = end - start;
  if (elapsedTime.count() > 1.) {
    start = std::chrono::system_clock::now();

    std::stringstream stream;
    stream << framesRendered;
    framesRendered = 0;

    strcpy(gWindowTitle, gRendererInfo);
    strcat(gWindowTitle, " - ");
    strcat(gWindowTitle, stream.str().c_str());
    strcat(gWindowTitle, " FPS");
    std::cout << gWindowTitle << std::endl;
  }
}

void SetLightSources() {
  glEnable(GL_LIGHTING);
  glShadeModel(GL_SMOOTH);
  const GLfloat ambient[4] = {scene.ambient_light.x, scene.ambient_light.y,
                              scene.ambient_light.z, 1.};
  glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
  const std::vector<parser::PointLight> point_lights = scene.point_lights;
  for (size_t i = 0; i < point_lights.size(); i++) {
    const parser::PointLight light = point_lights[i];
    GLfloat light_position[4] = {light.position.x, light.position.y,
                                 light.position.z, 1.};
    glLightfv(GL_LIGHT0 + i, GL_POSITION, light_position);
    const GLfloat intensity[4] = {light.intensity.x, light.intensity.y,
                                  light.intensity.z, 1.};
    glLightfv(GL_LIGHT0 + i, GL_DIFFUSE, intensity);
    glLightfv(GL_LIGHT0 + i, GL_SPECULAR, intensity);
    glEnable(GL_LIGHT0 + i);
    std::cout << "Enabling: " << i << std::endl;
  }
}

void reshape(GLFWwindow* win, int w, int h) {
  w = w < 1 ? 1 : w;
  h = h < 1 ? 1 : h;

  gWidth = w;
  gHeight = h;

  glViewport(0, 0, w, h);
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "No scene file provided." << std::endl;
    exit(1);
  }
  scene.loadFromXml(argv[1]);

  glfwSetErrorCallback(errorCallback);

  if (!glfwInit()) {
    exit(EXIT_FAILURE);
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

  win = glfwCreateWindow(scene.camera.image_width, scene.camera.image_height,
                         "CENG477", NULL, NULL);
  if (!win) {
    glfwTerminate();
    exit(EXIT_FAILURE);
  }
  glfwMakeContextCurrent(win);

  GLenum err = glewInit();
  if (err != GLEW_OK) {
    fprintf(stderr, "Error: %s\n", glewGetErrorString(err));
    exit(EXIT_FAILURE);
  }

  glfwSetKeyCallback(win, keyCallback);
  glfwSetWindowSizeCallback(win, reshape);

  Init();
  SetLightSources();
  SetCamera();
  // reshape(win, 640, 480);
  while (!glfwWindowShouldClose(win)) {
    glfwPollEvents();
    render();
    glfwSwapBuffers(win);
  }
  glfwDestroyWindow(win);
  glfwTerminate();

  exit(EXIT_SUCCESS);

  return 0;
}
