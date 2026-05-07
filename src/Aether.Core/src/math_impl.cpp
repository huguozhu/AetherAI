module aether.core;

import <cmath>;

namespace aether::math {

float4x4 float4x4::identity() {
    return float4x4{{
        {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}
    }};
}

float4x4 float4x4::perspective(float fovY, float aspect, float nearZ, float farZ) {
    float yScale = 1.0f / std::tan(fovY * 0.5f);
    float xScale = yScale / aspect;
    return float4x4{{
        {xScale, 0, 0, 0},
        {0, yScale, 0, 0},
        {0, 0, farZ / (nearZ - farZ), -1},
        {0, 0, nearZ * farZ / (nearZ - farZ), 0}
    }};
}

float4x4 float4x4::look_at(float3 eye, float3 target, float3 up) {
    float3 f = {target.x - eye.x, target.y - eye.y, target.z - eye.z};
    float fLen = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    f = {f.x / fLen, f.y / fLen, f.z / fLen};

    float3 s = {
        f.y * up.z - f.z * up.y,
        f.z * up.x - f.x * up.z,
        f.x * up.y - f.y * up.x
    };
    float sLen = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
    s = {s.x / sLen, s.y / sLen, s.z / sLen};

    float3 u = {
        s.y * f.z - s.z * f.y,
        s.z * f.x - s.x * f.z,
        s.x * f.y - s.y * f.x
    };

    return float4x4{{
        {s.x, u.x, -f.x, 0},
        {s.y, u.y, -f.y, 0},
        {s.z, u.z, -f.z, 0},
        {-s.x * eye.x - s.y * eye.y - s.z * eye.z,
         -u.x * eye.x - u.y * eye.y - u.z * eye.z,
         f.x * eye.x + f.y * eye.y + f.z * eye.z, 1}
    }};
}

float4x4 float4x4::transpose(const float4x4& mat) {
    float4x4 result;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.m[i][j] = mat.m[j][i];
        }
    }
    return result;
}

float4x4 float4x4::operator*(const float4x4& rhs) const {
    float4x4 result{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.m[i][j] = 0;
            for (int k = 0; k < 4; ++k) {
                result.m[i][j] += m[i][k] * rhs.m[k][j];
            }
        }
    }
    return result;
}

Frustum Frustum::from_view_proj(const float4x4& vp) {
    Frustum f;
    auto& m = vp.m;

    // Extract frustum planes from combined view-projection matrix.
    // Formulas: left=m3+m0, right=m3-m0, top=m3-m1, bottom=m3+m1, near=m3+m2, far=m3-m2
    float planeCoeffs[6][4] = {
        {m[3][0] + m[0][0], m[3][1] + m[0][1], m[3][2] + m[0][2], m[3][3] + m[0][3]}, // left
        {m[3][0] - m[0][0], m[3][1] - m[0][1], m[3][2] - m[0][2], m[3][3] - m[0][3]}, // right
        {m[3][0] - m[1][0], m[3][1] - m[1][1], m[3][2] - m[1][2], m[3][3] - m[1][3]}, // top
        {m[3][0] + m[1][0], m[3][1] + m[1][1], m[3][2] + m[1][2], m[3][3] + m[1][3]}, // bottom
        {m[3][0] + m[2][0], m[3][1] + m[2][1], m[3][2] + m[2][2], m[3][3] + m[2][3]}, // near
        {m[3][0] - m[2][0], m[3][1] - m[2][1], m[3][2] - m[2][2], m[3][3] - m[2][3]}, // far
    };

    for (int i = 0; i < 6; ++i) {
        float len = std::sqrt(
            planeCoeffs[i][0] * planeCoeffs[i][0] +
            planeCoeffs[i][1] * planeCoeffs[i][1] +
            planeCoeffs[i][2] * planeCoeffs[i][2]);
        f.planes[i] = {
            planeCoeffs[i][0] / len,
            planeCoeffs[i][1] / len,
            planeCoeffs[i][2] / len,
            planeCoeffs[i][3] / len
        };
    }

    return f;
}

bool Frustum::contains(const BoundingSphere& sphere) const {
    for (const auto& plane : planes) {
        float dot = plane.x * sphere.center.x
                  + plane.y * sphere.center.y
                  + plane.z * sphere.center.z
                  + plane.w;
        if (dot < -sphere.radius) {
            return false;
        }
    }
    return true;
}

// === BoundingBox ===

float3 BoundingBox::center() const {
    return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};
}

float3 BoundingBox::extents() const {
    return {(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f};
}

BoundingBox BoundingBox::from_center_extents(const float3& center, const float3& extents) {
    return {{center.x - extents.x, center.y - extents.y, center.z - extents.z},
            {center.x + extents.x, center.y + extents.y, center.z + extents.z}};
}

BoundingBox BoundingBox::from_sphere(const BoundingSphere& sphere) {
    return {{sphere.center.x - sphere.radius, sphere.center.y - sphere.radius, sphere.center.z - sphere.radius},
            {sphere.center.x + sphere.radius, sphere.center.y + sphere.radius, sphere.center.z + sphere.radius}};
}

// === Frustum-Box containment ===

Containment Frustum::contains(const BoundingBox& box) const {
    // Corners of the bounding box
    float3 corners[8] = {
        {box.min.x, box.min.y, box.min.z},
        {box.max.x, box.min.y, box.min.z},
        {box.min.x, box.max.y, box.min.z},
        {box.max.x, box.max.y, box.min.z},
        {box.min.x, box.min.y, box.max.z},
        {box.max.x, box.min.y, box.max.z},
        {box.min.x, box.max.y, box.max.z},
        {box.max.x, box.max.y, box.max.z},
    };

    bool allInside = true;
    for (int p = 0; p < 6; ++p) {
        const auto& plane = planes[p];
        bool anyInside = false;
        for (int c = 0; c < 8; ++c) {
            float d = plane.x * corners[c].x
                    + plane.y * corners[c].y
                    + plane.z * corners[c].z
                    + plane.w;
            if (d >= 0) {
                anyInside = true;
            } else {
                allInside = false;
            }
        }
        if (!anyInside) return Containment::Outside;
    }
    return allInside ? Containment::Inside : Containment::Intersects;
}

} // namespace aether::math
