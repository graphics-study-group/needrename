#include "Render/RenderSystem.h"

#include <glad/glad.h>

namespace Engine
{
    void RenderSystem::render()
    {
        // TODO: render mesh, texture, light; the following codes are copied for debugging

        // 三角形的顶点数据
        float vertices[] = {
            -0.5f, -0.5f, 0.0f, // 左下角
            0.5f, -0.5f, 0.0f, // 右下角
            0.0f,  0.5f, 0.0f  // 顶部
        };

        // 顶点着色器源码
        const char* vertexShaderSource = "#version 330 core\n"
            "layout (location = 0) in vec3 aPos;\n"
            "void main()\n"
            "{\n"
            "   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
            "}\0";

        // 片段着色器源码
        const char* fragmentShaderSource = "#version 330 core\n"
            "out vec4 FragColor;\n"
            "void main()\n"
            "{\n"
            "   FragColor = vec4(1.0f, 0.5f, 0.2f, 1.0f);\n"
            "}\0";
        unsigned int vertexShader;
        vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
        glCompileShader(vertexShader);

        unsigned int fragmentShader;
        fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
        glCompileShader(fragmentShader);

        unsigned int shaderProgram;
        shaderProgram = glCreateProgram();

        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);

        // 链接后，不再需要单独的着色器对象
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        unsigned int VBO, VAO;
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        // 绑定VAO
        glBindVertexArray(VAO);

        // 把我们的顶点数组复制到一个顶点缓冲中，供OpenGL使用
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        // 设置顶点属性指针
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // 注意：这是允许的，调用glVertexAttribPointer时绑定的是VBO
        glBindBuffer(GL_ARRAY_BUFFER, 0); 

        // 可以解绑VAO后进行其他VAO配置
        glBindVertexArray(0); 

        // 在每次渲染循环中
        glUseProgram(shaderProgram);
        glBindVertexArray(VAO); // 因为我们只有一个VAO，所以不需要每次都绑定它，但这里我们这样做以便更清晰
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
}