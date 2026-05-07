module;
#include <cmath>

module aether.renderer;

import aether.core;

namespace aether::renderer {

math::float4x4 CameraComponent::get_view_matrix() const {
    return math::float4x4::look_at(position, target, up);
}

math::float4x4 CameraComponent::get_projection_matrix() const {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    return math::float4x4::perspective(fov * kDegToRad, aspect, nearPlane, farPlane);
}

SceneView CameraComponent::get_scene_view() const {
    SceneView view{};
    view.viewProj = get_view_matrix() * get_projection_matrix();
    view.eyePosition = position;
    view.nearPlane = nearPlane;
    view.farPlane = farPlane;
    return view;
}

} // namespace aether::renderer
