
#define GL_GLEXT_PROTOTYPES

#include <iostream>
#include <thread>
#include <chrono>
#include <GL/gl.h>
#include <png.h>
#include "flint/accel/bvh/TreeBuilder.h"
#include "flint/accel/bvh/Tree.h"
#include "flint/core/AxisAlignedBox.h"
#include "flint/core/Optional.h"
#include "flint/geometry/Triangle.h"
#include "flint/import/ObjLoader.h"
#include "flint/intersection/Ray.h"
#include "flint/intersection/bvh/Tree.h"
#include "flint/sampling/Box.h"
#include "flint/sampling/Mesh.h"
#include "flint_viewport/viewport.h"

using namespace core;

const float* sampleData = nullptr;
unsigned int sampleCount = 0;

void Loop(display::Viewport::Window* window) {
    window->Init();

    GLuint sampleBuffer;
    glGenBuffers(1, &sampleBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, sampleBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * sampleCount, sampleData, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    static std::string vsSource = R"(
        #version 130

        uniform mat4 viewProjectionMatrix;
        in vec3 position;

        void main() {
            gl_Position = viewProjectionMatrix * vec4(position, 1.0);
        }
    )";

    static std::string fsSource = R"(
        #version 130

        void main() {
            gl_FragColor  = vec4(1.0);
        }
    )";

    const char* source;
    int length;

    auto compileShader = [](GLenum type, const char* source, int length) {
        auto checkCompileStatus = [](GLuint shader) {
            GLint compileFlag;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compileFlag);
            if (compileFlag == GL_FALSE) {
                GLint maxLength = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
                std::vector<GLchar> errorLog(maxLength);
                glGetShaderInfoLog(shader, maxLength, &maxLength, &errorLog[0]);

                std::cerr << errorLog.data() << std::endl;
            }
        };

        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, &length);
        glCompileShader(shader);
        checkCompileStatus(shader);

        return shader;
    };

    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSource.c_str(), static_cast<int>(vsSource.size()));
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSource.c_str(), static_cast<int>(fsSource.size()));

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vs);
    glAttachShader(shaderProgram, fs);
    glLinkProgram(shaderProgram);

    GLint linkFlag = 0;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &linkFlag);
    if (linkFlag == GL_FALSE) {
        GLint maxLength = 0;
        glGetProgramiv(shaderProgram, GL_INFO_LOG_LENGTH, &maxLength);
        std::vector<GLchar> errorLog(maxLength);
        glGetProgramInfoLog(shaderProgram, maxLength, &maxLength, &errorLog[0]);

        std::cerr << errorLog.data() << std::endl;
    }

    GLint positionLocation = glGetAttribLocation(shaderProgram, "position");
    if (positionLocation < 0) {
        std::cerr << "Could not get shader attribute position" << std::endl;
    }

    GLint viewProjectionMatrixLocation = glGetUniformLocation(shaderProgram, "viewProjectionMatrix");
    if (viewProjectionMatrixLocation < 0) {
        std::cerr << "Could not get shader uniform viewProjectionMatrix" << std::endl;
    }

    glVertexAttribPointer(positionLocation, 3, GL_FLOAT, false, 0, 0);

    glUseProgram(shaderProgram);

    int width, height;
    glfwGetFramebufferSize(window->GetGLFWWindow(), &width, &height);

    glClearColor(0.f, 0.f, 0.f, 0.f);
    glViewport(0, 0, width, height);

    glBindBuffer(GL_ARRAY_BUFFER, sampleBuffer);

    while(!window->ShouldClose()) {
        auto mvp = Eigen::Matrix4f::Identity();
        glUniformMatrix4fv(viewProjectionMatrixLocation, 1, GL_FALSE, reinterpret_cast<const float*>(&mvp));

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDrawArrays(GL_POINTS, 0, sampleCount);

        window->SwapBuffers();

        using namespace std::chrono_literals;
        std::this_thread::sleep_for(16ms);
    }

    glDeleteBuffers(1, &sampleBuffer);
    glDeleteProgram(shaderProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Expected one argument" << std::endl;
        return 1;
    }

    using Triangle = geometry::Triangle<3, float>;

    auto* mesh = new geometry::Mesh<3, Triangle>();
    import::TriangleObjLoader<float> loader;
    loader.Load(argv[1], mesh);

    Optional<AxisAlignedBox<3, float>> boundingBox;
    for (const auto* geometry : mesh->geometries()) {
        Merge(boundingBox, geometry->getAxisAlignedBound());
    }

    auto samples = sampling::SampleMesh<float>(mesh, boundingBox->Extent(boundingBox->GreatestExtent()) / 10.f);
    sampleData = reinterpret_cast<const float*>(samples.data());
    sampleCount = samples.size();

    delete mesh;

    auto* viewport = new display::Viewport();
    std::thread viewportThread(Loop, viewport->GetWindow());

    viewport->CaptureFrame([&](const GLubyte* pixels, int width, int height) {
        if ([&](){
            if (!pixels) return 1;

            FILE *fp = fopen("capture.png", "wb");
            if (!fp) return 1;

            png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
            if (!png) return 1;

            png_infop info = png_create_info_struct(png);
            if (!info) return 1;

            if (setjmp(png_jmpbuf(png))) return 1;

            png_init_io(png, fp);

            png_set_IHDR(
                png,
                info,
                width, height,
                8,
                PNG_COLOR_TYPE_RGBA,
                PNG_INTERLACE_NONE,
                PNG_COMPRESSION_TYPE_DEFAULT,
                PNG_FILTER_TYPE_DEFAULT
            );
            png_write_info(png, info);

            unsigned int componentCount = width * height * 4;
            std::vector<png_byte> _pixels(componentCount);
            for (unsigned int i = 0; i < componentCount; ++i) {
                _pixels[i] = pixels[i];
            }

            std::vector<png_bytep> row_pointers(height);
            for (unsigned int y = 0; y < height; ++y) {
                row_pointers[y] = _pixels.data() + width * y * 4;
            }

            png_write_image(png, row_pointers.data());

            if (setjmp(png_jmpbuf(png))) return 1;

            png_write_end(png, nullptr);

            fclose(fp);

            return 0;
        }()) {
            std::cerr << "Error writing png file" << std::endl;
        } else {
            std::cout << "Saved png file" << std::endl;
        }

        viewport->Close();
    });

    viewportThread.join();
    delete viewport;

    return 0;
}