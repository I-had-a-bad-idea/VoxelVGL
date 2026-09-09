#include "VGL/math.h"

Frustum extract_frustum(const glm::mat4& vp) {
    Frustum f;

    // Left
    f.planes[0] = glm::vec4(
        vp[0][3] + vp[0][0],
        vp[1][3] + vp[1][0],
        vp[2][3] + vp[2][0],
        vp[3][3] + vp[3][0]
    );

    // Right
    f.planes[1] = glm::vec4(
        vp[0][3] - vp[0][0],
        vp[1][3] - vp[1][0],
        vp[2][3] - vp[2][0],
        vp[3][3] - vp[3][0]
    );

    // Bottom
    f.planes[2] = glm::vec4(
        vp[0][3] + vp[0][1],
        vp[1][3] + vp[1][1],
        vp[2][3] + vp[2][1],
        vp[3][3] + vp[3][1]
    );

    // Top
    f.planes[3] = glm::vec4(
        vp[0][3] - vp[0][1],
        vp[1][3] - vp[1][1],
        vp[2][3] - vp[2][1],
        vp[3][3] - vp[3][1]
    );

    // Near
    f.planes[4] = glm::vec4(
        vp[0][2],
        vp[1][2],
        vp[2][2],
        vp[3][2]
    );

    // Far
    f.planes[5] = glm::vec4(
        vp[0][3] - vp[0][2],
        vp[1][3] - vp[1][2],
        vp[2][3] - vp[2][2],
        vp[3][3] - vp[3][2]
    );

    for (auto& plane : f.planes) {
        float len = glm::length(glm::vec3(plane));
        plane /= len;
    }

    return f;
}

bool sphere_in_frustum(const Frustum& frustum, const glm::vec3& center, float radius) {
    for (const glm::vec4& plane : frustum.planes) {
        float distance = glm::dot(glm::vec3(plane), center) + plane.w;

        if (distance < -radius) {
            return false;
        }
    }

    return true;
}