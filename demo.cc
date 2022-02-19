#include "demo.h"

#include "mesh.h"
#include "shaderprogram.h"

#include <glm/gtc/matrix_transform.hpp>

#include <GL/glew.h>

#include <random>
#include <cstdio>

namespace
{
bool initializeProgram(ShaderProgram *program, std::string_view vertexShader, std::string_view fragmentShader)
{
    if (!program->addShader(GL_VERTEX_SHADER, vertexShader))
    {
        fprintf(stderr, "Failed to add vertex shader %s: %s\n", std::string(vertexShader).c_str(),
                program->log().c_str());
        return false;
    }

    if (!program->addShader(GL_FRAGMENT_SHADER, fragmentShader))
    {
        std::fprintf(stderr, "Failed to add fragment shader %s: %s\n", std::string(fragmentShader).c_str(),
                     program->log().c_str());
        return false;
    }

    if (!program->link())
    {
        std::fprintf(stderr, "Failed to link shader: %s\n", program->log().c_str());
    }

    return true;
}

std::unique_ptr<Shape> initializeShape(const std::vector<int> &segments)
{
    static std::default_random_engine generator;

    auto center = glm::vec3(0);
    unsigned direction = 2;
    float side = 1;

    // 001 -> 010 100
    // 010 -> 001 100
    // 100 -> 001 010

    std::uniform_int_distribution<int> zero_or_one(0, 1);

    std::vector<glm::vec3> blocks;
    for (auto length : segments)
    {
        const auto d = 2.0f * side * glm::vec3(direction >> 2, (direction >> 1) & 1, direction & 1);
        for (int i = 0; i < length; ++i)
        {
            blocks.push_back(center);
            center += d;
        }
        switch (direction)
        {
        case 1:
            direction = zero_or_one(generator) ? 2 : 4;
            break;
        case 2:
            direction = zero_or_one(generator) ? 1 : 4;
            break;
        case 4:
            direction = zero_or_one(generator) ? 1 : 2;
            break;
        default:
            assert(false);
            break;
        }
        if (zero_or_one(generator))
            side = -side;
    }

    struct Vertex
    {
        glm::vec3 position;
        glm::vec2 texCoord;
    };
    std::vector<Vertex> vertices;
    for (const auto &center : blocks)
    {
        auto addFace = [&vertices, &center](const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2,
                                            const glm::vec3 &p3) {
            vertices.push_back({p0 + center, {0, 0}});
            vertices.push_back({p1 + center, {0, 1}});
            vertices.push_back({p2 + center, {1, 1}});

            vertices.push_back({p2 + center, {1, 1}});
            vertices.push_back({p3 + center, {1, 0}});
            vertices.push_back({p0 + center, {0, 0}});
        };

        // top
        addFace(glm::vec3(-1, 1, -1), glm::vec3(-1, 1, 1), glm::vec3(1, 1, 1), glm::vec3(1, 1, -1));

        // bottom
        addFace(glm::vec3(1, -1, -1), glm::vec3(1, -1, 1), glm::vec3(-1, -1, 1), glm::vec3(-1, -1, -1));

        // right
        addFace(glm::vec3(-1, -1, -1), glm::vec3(-1, -1, 1), glm::vec3(-1, 1, 1), glm::vec3(-1, 1, -1));

        // left
        addFace(glm::vec3(1, 1, -1), glm::vec3(1, 1, 1), glm::vec3(1, -1, 1), glm::vec3(1, -1, -1));

        // back
        addFace(glm::vec3(-1, -1, -1), glm::vec3(-1, 1, -1), glm::vec3(1, 1, -1), glm::vec3(1, -1, -1));

        // front
        addFace(glm::vec3(1, -1, 1), glm::vec3(1, 1, 1), glm::vec3(-1, 1, 1), glm::vec3(-1, -1, 1));
    }

    auto mesh = std::make_unique<Mesh>();
    mesh->setVertexCount(vertices.size());
    mesh->setVertexSize(sizeof(Vertex));
    mesh->addVertexAttribute(3, GL_FLOAT, offsetof(Vertex, position));
    mesh->addVertexAttribute(2, GL_FLOAT, offsetof(Vertex, texCoord));
    mesh->initialize();
    mesh->setVertexData(vertices.data());

    BoundingBox bb;
    bb.min = glm::vec3(std::numeric_limits<float>::max());
    bb.max = glm::vec3(std::numeric_limits<float>::min());
    for (auto &vertex : vertices)
    {
        const auto &p = vertex.position;
        bb.min = glm::min(bb.min, p);
        bb.max = glm::max(bb.max, p);
    }

    auto shape = std::make_unique<Shape>();
    shape->boundingBox = bb;
    shape->mesh = std::move(mesh);

    return shape;
}
}

Demo::Demo(int canvasWidth, int canvasHeight)
    : m_canvasWidth(canvasWidth)
    , m_canvasHeight(canvasHeight)
{
}

Demo::~Demo() = default;

void Demo::renderAndStep(float dt)
{
    render();
    m_curTime += dt;
}

void Demo::render() const
{
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    m_shapeProgram->bind();

    constexpr auto Columns = 3;

    auto viewportWidth = m_canvasWidth / Columns;
    auto viewportHeight = m_canvasHeight / ((m_shapes.size() + Columns - 1) / Columns);

    for (size_t i = 0; i < m_shapes.size(); ++i)
    {
        const auto &shape = m_shapes[i];

        const auto viewportX = (i % Columns) * viewportWidth;
        const auto viewportY = (i / Columns) * viewportHeight;
        glViewport(viewportX, viewportY, viewportWidth, viewportHeight);

        const auto projection =
            glm::perspective(glm::radians(45.0f), static_cast<float>(viewportWidth) / viewportHeight, 0.1f, 100.f);
        const auto viewPos = glm::vec3(0, 0, -18);
        const auto viewUp = glm::vec3(0, 1, 0);
        const auto view = glm::lookAt(viewPos, glm::vec3(0, 0, 0), viewUp);

        const auto center = 0.5f * (shape->boundingBox.min + shape->boundingBox.max);
        const auto t = glm::translate(glm::mat4(1.0f), -center);
        const auto angle = -1.5f * m_curTime;
        const auto r = glm::rotate(glm::mat4(1.0f), angle, glm::normalize(glm::vec3(1, 1, 1)));
        const auto model = r * t;
        const auto mvp = projection * view * model;
        m_shapeProgram->setUniform(m_shapeProgram->uniformLocation("mvp"), mvp);

        shape->mesh->render(GL_TRIANGLES);
    }
}

bool Demo::initialize()
{
    m_shapeProgram.reset(new ShaderProgram);
    if (!initializeProgram(m_shapeProgram.get(), "shaders/shape.vert", "shaders/shape.frag"))
        return false;

    const std::vector segments = {3, 3, 2, 3};
    for (int i = 0; i < 6; ++i)
        m_shapes.push_back(initializeShape(segments));

    return true;
}
