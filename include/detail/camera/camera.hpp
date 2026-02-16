#pragma once

#include "../__glm_import.hpp"

namespace mcs::vulkan::camera
{
    struct camera_interface
    {
        struct views_matrix
        {
            glm::vec3 eye, center, up; // NOLINT
            constexpr auto operator()() const noexcept
            {
                return glm::lookAt(eye, center, up);
            }
        };
        struct ortho_matrix
        {
            float left, right, bottom, top, zNear, zFar; // NOLINT
            constexpr auto operator()() const noexcept
            {
                return glm::ortho(left, right, bottom, top, zNear, zFar);
            }
        };
        struct perspective_matrix
        {
            float fovy, aspect, zNear, zFar; // NOLINT
            constexpr auto operator()() const noexcept
            {
                return glm::perspective(fovy, aspect, zNear, zFar);
            }
        };
        [[nodiscard]] constexpr auto viewsMatrix() const noexcept
        {
            return viewsMatrix_();
        }
        [[nodiscard]] constexpr auto &refViewsMatrix() noexcept
        {
            return viewsMatrix_;
        }
        auto &setViewsMatrix(const views_matrix &viewsMatrix) noexcept
        {
            viewsMatrix_ = viewsMatrix;
            return *this;
        }
        [[nodiscard]] constexpr auto orthoMatrix() const noexcept
        {
            return orthoMatrix_();
        }
        [[nodiscard]] constexpr auto &refOrthoMatrix() noexcept
        {
            return orthoMatrix_;
        }
        auto &setOrthoMatrix(const ortho_matrix &orthoMatrix) noexcept
        {
            orthoMatrix_ = orthoMatrix;
            return *this;
        }
        [[nodiscard]] constexpr auto perspectiveMatrix() const noexcept
        {
            return perspectiveMatrix_();
        }
        [[nodiscard]] constexpr auto &refPerspectiveMatrix() noexcept
        {
            return perspectiveMatrix_;
        }
        auto &setPerspectiveMatrix(const perspective_matrix &perspectiveMatrix) noexcept
        {
            perspectiveMatrix_ = perspectiveMatrix;
            return *this;
        }

      private:
        views_matrix viewsMatrix_;
        ortho_matrix orthoMatrix_;
        perspective_matrix perspectiveMatrix_;
    };
}; // namespace mcs::vulkan::camera