#pragma once

#include "../__glm_import.hpp"

namespace mcs::vulkan::camera
{

    // NOLINTBEGIN
    // 物体空间 → [模型矩阵] → 世界空间 → [视图矩阵] → 观察空间 → [投影矩阵] → 裁剪空间 → [透视除法] → NDC → [视口映射] → 屏幕空间
    // MVP = 投影矩阵 × 视图矩阵 × 模型矩阵
    enum class Handedness : uint8_t
    {
        eRIGHT_HANDED,
        eLEFT_HANDED
    };

    // 组合平移、旋转、缩放，生成 TRS 矩阵（顺序：T * R * S）
    static constexpr glm::mat4 composeTRS(const glm::vec3 &translation,
                                          const glm::quat &rotation,
                                          const glm::vec3 &scale) noexcept
    {
        return glm::translate(glm::mat4(1.0F), translation) * glm::toMat4(rotation) *
               glm::scale(glm::mat4(1.0F), scale);
    }
    // 从 4x4 列主序矩阵中提取平移和缩放（假设没有剪切和投影效果）
    static constexpr std::pair<glm::vec3, glm::vec3> extractTranslationScale(
        const glm::mat4 &m) noexcept
    {
        return {
            glm::vec3(m[3]),                        // 平移
            glm::vec3(glm::length(glm::vec3(m[0])), // X轴缩放
                      glm::length(glm::vec3(m[1])), // Y轴缩放
                      glm::length(glm::vec3(m[2]))) // Z轴缩放
        };
    }

    // https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#vertexpostproc
    // 直接从 Vulkan 视口映射和裁剪不等式推导出的固定 NDC 边界
    // 引用自 VK_SPEC: Viewport Mapping & Clipping
    struct VulkanNDCConfig
    {

        // Vulkan NDC 边界（编译期常量）
        static constexpr float kLeft = -1.0F;
        static constexpr float kRight = 1.0F;
        // NDC 坐标边界（数值上：-1为下，+1为上）
        // 但在 Vulkan 默认视口下，NDC -1 映射到屏幕顶部，NDC +1 映射到屏幕底部
        static constexpr float kBottom = -1.0F; // 屏幕映射后为顶部
        static constexpr float kTop = 1.0F;     // 屏幕映射后为底部
        static constexpr float kNear = 0.0F;    // Vulkan: 深度 [0,1]
        static constexpr float kFar = 1.0F;

        // Vulkan 屏幕左上为原点，NDC 的 Y 是向下的，所以屏幕 Y 增加对应 NDC Y 减小
        static constexpr float screenYToNdcYFactor = -1.0F;
        static constexpr float projYFlipFactor = -1.0F;

        static consteval float ndcLeft()
        {
            return kLeft;
        }
        static consteval float ndcRight()
        {
            return kRight;
        }
        static consteval float ndcBottom()
        {
            return kBottom;
        }
        static consteval float ndcTop()
        {
            return kTop;
        }
        template <class T0, class T1>
        static constexpr glm::vec2 screenToNDC(T0 px, T0 py, T1 width, T1 height)
        {
            float u = ndcLeft() + (px / width) * (ndcRight() - ndcLeft()); // -1 + 2*px/w
            float v =
                ndcBottom() + (py / height) * (ndcTop() - ndcBottom()); // -1 + 2*py/h
            return {u, v};
        }

        /*
        Vulkan NDC 规格回顾
        X/Y ∈ [-1, 1]
            Y 轴：NDC 的 -1 对应屏幕顶部，+1 对应底部（默认视口不翻转）

        Z ∈ [0, 1] （深度）
            视图空间为 右手系：相机看向 -Z
        */
        // NOTE: 不再依赖GLM ，根据NDC推导。
        template <Handedness H>
        constexpr static glm::mat4 makeOrtho(float left, float right, float bottom,
                                             float top, float zNear, float zFar) noexcept
        {
            glm::mat4 Result(1.0F);
            Result[0][0] = 2.0F / (right - left);
            Result[1][1] = 2.0F / (top - bottom);
            Result[3][0] = -(right + left) / (right - left);
            Result[3][1] = -(top + bottom) / (top - bottom);

            if constexpr (H == Handedness::eRIGHT_HANDED)
            {
                Result[2][2] = -1.0F / (zFar - zNear);
                Result[3][2] = -zNear / (zFar - zNear);
            }
            else
            {
                Result[2][2] = 1.0F / (zFar - zNear);
                Result[3][2] = -zNear / (zFar - zNear);
            }
            return Result;
        }

        template <Handedness H>
        constexpr static glm::mat4 makePerspective(float fovDeg, float aspect,
                                                   float zNear, float zFar) noexcept
        {
            float fovRad = glm::radians(fovDeg);
            float h = 1.0F / tanf(fovRad * 0.5f); // cot(fov/2)
            float w = h / aspect;

            glm::mat4 Result(0.0F);
            Result[0][0] = w;
            Result[1][1] = h;

            if constexpr (H == Handedness::eRIGHT_HANDED)
            {
                Result[2][2] = -zFar / (zFar - zNear);
                Result[2][3] = -1.0F;
                Result[3][2] = -(zFar * zNear) / (zFar - zNear);
            }
            else
            {
                Result[2][2] = zFar / (zFar - zNear);
                Result[2][3] = 1.0F;
                Result[3][2] = -(zFar * zNear) / (zFar - zNear);
            }
            return Result;
        }
        template <Handedness H>
        constexpr static glm::mat4 makeFrustum(float l, float r, float b, float t,
                                               float n, float f) noexcept
        {
            glm::mat4 Result(0.0F);
            Result[0][0] = 2.0F * n / (r - l);
            Result[1][1] = 2.0F * n / (t - b);
            Result[2][0] = (r + l) / (r - l);
            Result[2][1] = (t + b) / (t - b);

            if constexpr (H == Handedness::eRIGHT_HANDED)
            {
                Result[2][2] = -f / (f - n);
                Result[2][3] = -1.0F;
                Result[3][2] = -(f * n) / (f - n);
            }
            else
            {
                Result[2][2] = f / (f - n);
                Result[2][3] = 1.0F;
                Result[3][2] = -(f * n) / (f - n);
            }
            return Result;
        }

        // 生成将单位矩形[-1,1]² 映射到 NDC 中轴对齐矩形的变换矩阵
        static constexpr glm::mat4 rectTransform(glm::vec2 topLeft, glm::vec2 bottomRight,
                                                 float depth = 0.0F) noexcept
        {
            glm::vec2 center = (topLeft + bottomRight) * 0.5F;
            glm::vec2 halfSize = (bottomRight - topLeft) * 0.5F;
            return glm::translate(glm::mat4(1.0F), glm::vec3(center, depth)) *
                   glm::scale(glm::mat4(1.0F), glm::vec3(halfSize, 1.0F));
        }

        // 基础转换：屏幕像素移动 → (水平角增量, 垂直角增量)
        // 不关心最终是相机还是模型，只做单位归一化
        static constexpr auto ScreenDragToAngleDelta(float dxPixels, float dyPixels,
                                                     float sensitivity) noexcept
        {
            // 水平：向右拖拽为正角度（绕世界 Y 轴），无需翻转
            float yaw = dxPixels * sensitivity;
            // 垂直：屏幕上移（像素 Y 减小）应产生正的俯仰角（绕世界 X/右轴）
            // 由于 NDC 的 Y 与屏幕 Y 方向关系为 screenYToNdcYFactor，这里直接用该因子
            float pitch = dyPixels * sensitivity * screenYToNdcYFactor;
            return std::pair{yaw, pitch};
        }

        static constexpr auto ScreenDragToYawPitch(float dx, float dy,
                                                   float sens) noexcept
        {
            return ScreenDragToAngleDelta(dx, dy, sens);
        }

        static constexpr auto ScreenDragToCameraYawPitch(float dx, float dy,
                                                         float sens) noexcept
        {
            auto [yaw, pitch] = ScreenDragToAngleDelta(dx, dy, sens);
            // 相机旋转方向与对象相反：左拖 -> 相机右转（yaw 为负）
            return std::pair{-yaw, -pitch};
        }

        // ---------- NDC 空间适配 ----------
        // 将标准 GLM 风格的投影矩阵（假设 OpenGL NDC，Y向上）转换为 Vulkan NDC（Y向下）
        static constexpr glm::mat4 ConvertProjectionForGLM(const glm::mat4 &proj) noexcept
        {
            glm::mat4 p = proj;
            p[1][1] *= projYFlipFactor; // Y 轴翻转
            return p;
        }

        // 将窗口像素坐标（左上角为原点）转换为世界空间射线
        // 返回 {rayOrigin, rayDir}，其中 rayDir 已归一化
        constexpr static std::pair<glm::vec3, glm::vec3> ScreenToWorldRay(
            glm::ivec2 mousePos, glm::ivec2 windowSize, const glm::mat4 &view,
            const glm::mat4 &projVulkan) noexcept
        {
            glm::mat4 proj = ConvertProjectionForGLM(projVulkan);
            glm::vec4 viewport(0.0F, 0.0F, static_cast<float>(windowSize.x),
                               static_cast<float>(windowSize.y));

            float winY = static_cast<float>(windowSize.y - mousePos.y);
            glm::vec3 nearPoint =
                glm::unProject(glm::vec3(static_cast<float>(mousePos.x), winY, 0.0F),
                               view, proj, viewport);
            glm::vec3 farPoint =
                glm::unProject(glm::vec3(static_cast<float>(mousePos.x), winY, 1.0F),
                               view, proj, viewport);

            return {nearPoint, glm::normalize(farPoint - nearPoint)};
        }

        // 静态断言函数：验证透视参数有效性
        static constexpr void AssertPerspective(float fovDeg, float aspect, float zNear,
                                                float zFar) noexcept
        {
            assert(fovDeg > 0.0F && fovDeg < 180.0F);
            assert(aspect > 0.0F);
            //透视矩阵在 zNear = 0 时会除零，makePerspective 中将出现无穷大。
            assert(zNear > 0.0F);
            assert(zFar > zNear);
            (void)fovDeg;
            (void)aspect;
            (void)zNear;
            (void)zFar;
        }

        // 静态断言函数：验证正交参数有效性
        static constexpr void AssertOrtho(float left, float right, float bottom,
                                          float top, float zNear, float zFar) noexcept
        {
            assert(left < right);
            assert(bottom < top);
            assert(zNear >= 0.0F); //NOTE: ==0 为了能生产 单位矩阵
            assert(zFar > zNear);
            (void)left;
            (void)right;
            (void)bottom;
            (void)top;
            (void)zNear;
            (void)zFar;
        }
    };

    // 当 l=-1,r=1,b=-1,t=1,n=0,f=1 且 H=LeftHanded 时，矩阵为单位阵（x,y,z 直通）
    static_assert(VulkanNDCConfig::makeOrtho<Handedness::eLEFT_HANDED>(-1, 1, -1, 1, 0,
                                                                       1) ==
                  glm::mat4(1));

    struct transform
    {
        // NOLINTBEGIN
        glm::vec3 translation = glm::vec3(0.0F, 0.0F, 0.0F);    // 平移（默认无偏移）
        glm::quat rotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F); // 旋转（默认无旋转）
        glm::vec3 scale = glm::vec3(1.0F, 1.0F, 1.0F);          // 缩放（默认无缩放）
        // NOLINTEND

        constexpr auto operator()() const noexcept
        {
            return composeTRS(translation, rotation, scale);
        }
    };

    // NOTE: 专门设计
    // 计算局部锚点在世界空间中相对于物体原点的偏移
    [[nodiscard]] static constexpr auto computeAnchorOffset(
        const glm::vec3 &anchorLocal, const transform &modelMat,
        const glm::mat4 &vertexTransform) noexcept
    {
        glm::mat4 R = glm::toMat4(modelMat.rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0F), modelMat.scale);
        return glm::vec3(R * S * vertexTransform * glm::vec4(anchorLocal, 1.0F));
    }
    [[nodiscard]] constexpr glm::vec3 transformPointToWorld(
        const glm::vec3 &localPoint,
        const glm::mat4 &modelMatrix, // 已包含平移的完整模型矩阵
        const glm::mat4 &vertexTransform) noexcept
    {
        return glm::vec3(modelMatrix * vertexTransform * glm::vec4(localPoint, 1.0F));
    }

    // 核心模板类：手系由模板参数在编译期确定
    template <Handedness H>
    class ViewMatrixObject
    {
      private:
        // -------- 数据 --------
        glm::vec3 m_position = glm::vec3(0.0F, 0.0F, 5.0F);
        glm::quat m_orientation = glm::identity<glm::quat>();

        // -------- 手系相关的局部基向量（编译期常量）--------
        // 右手系：Z轴向外，前方向为 -Z；左手系：Z轴向内，前方向为 +Z
        static constexpr glm::vec3 LOCAL_FORWARD = (H == Handedness::eRIGHT_HANDED)
                                                       ? glm::vec3(0.0F, 0.0F, -1.0F)
                                                       : glm::vec3(0.0F, 0.0F, 1.0F);
        static constexpr glm::vec3 LOCAL_RIGHT = glm::vec3(1.0F, 0.0F, 0.0F);
        static constexpr glm::vec3 LOCAL_UP = glm::vec3(0.0F, 1.0F, 0.0F);

      public:
        // -------- 构造函数 --------
        constexpr ViewMatrixObject() = default;
        constexpr ViewMatrixObject(const glm::vec3 &pos, const glm::quat &ori) noexcept
            : m_position(pos), m_orientation(ori)
        {
        }

        // -------- 链式 Setter（保留你的 deducing this 风格）--------
        constexpr auto &&setPosition(this auto &&self, const glm::vec3 &pos) noexcept
        {
            self.m_position = pos;
            return std::forward<decltype(self)>(self);
        }
        constexpr auto &&setOrientation(this auto &&self, const glm::quat &ori) noexcept
        {
            self.m_orientation = ori;
            return std::forward<decltype(self)>(self);
        }

        // -------- Getter --------
        [[nodiscard]] constexpr const glm::vec3 &getPosition() const noexcept
        {
            return m_position;
        }
        [[nodiscard]] constexpr const glm::quat &getOrientation() const noexcept
        {
            return m_orientation;
        }

        // -------- 视图矩阵生成（与手系无关，公式通用）--------
        [[nodiscard]] constexpr glm::mat4 getViewMatrix() const noexcept
        {
            glm::mat4 rot = glm::mat4_cast(glm::conjugate(m_orientation));
            glm::mat4 trans = glm::translate(glm::mat4(1.0F), -m_position);
            return rot * trans; // 先平移后旋转
        }

        // -------- 世界空间方向向量（手系相关，由模板常量决定）--------
        [[nodiscard]] constexpr glm::vec3 getForward() const noexcept
        {
            return glm::rotate(m_orientation, LOCAL_FORWARD);
        }
        [[nodiscard]] constexpr glm::vec3 getRight() const noexcept
        {
            return glm::rotate(m_orientation, LOCAL_RIGHT);
        }
        [[nodiscard]] constexpr glm::vec3 getUp() const noexcept
        {
            return glm::rotate(m_orientation, LOCAL_UP);
        }

        // -------- 移动控制 --------
        constexpr auto &&moveForward(this auto &&self, float delta) noexcept
        {
            self.m_position += self.getForward() * delta;
            return std::forward<decltype(self)>(self);
        }
        constexpr auto &&moveRight(this auto &&self, float delta) noexcept
        {
            self.m_position += self.getRight() * delta;
            return std::forward<decltype(self)>(self);
        }
        constexpr auto &&moveUp(this auto &&self, float delta) noexcept
        {
            self.m_position += self.getUp() * delta;
            return std::forward<decltype(self)>(self);
        }

        // -------- 旋转控制（与手系无关）--------
        constexpr auto &&rotateLocal(this auto &&self, const glm::quat &delta) noexcept
        {
            self.m_orientation = self.m_orientation * delta; // 局部旋转（右乘）
            return std::forward<decltype(self)>(self);
        }
        constexpr auto &&rotateWorld(this auto &&self, const glm::quat &delta) noexcept
        {
            self.m_orientation = delta * self.m_orientation; // 世界旋转（左乘）
            return std::forward<decltype(self)>(self);
        }

        // -------- 核心生成器 --------
        constexpr glm::mat4 operator()() const noexcept
        {
            return getViewMatrix();
        }
        constexpr static ViewMatrixObject lookAt(const glm::vec3 &eye,
                                                 const glm::vec3 &center,
                                                 const glm::vec3 &up)
        {
            glm::vec3 forward = glm::normalize(center - eye);
            // 根据手系调整向前方向（但 lookAt 通常由外部保证，可内部处理）
            return ViewMatrixObject(eye, H == Handedness::eRIGHT_HANDED
                                             ? glm::quatLookAtRH(forward, up)
                                             : glm::quatLookAtLH(forward, up));
        }
    };

    // -------- 为了方便使用，定义别名 --------
    using RightHandedView = ViewMatrixObject<Handedness::eRIGHT_HANDED>;
    using LeftHandedView = ViewMatrixObject<Handedness::eLEFT_HANDED>;

    // -------- 前置声明：主模板（不实现）--------
    enum class ProjectionMode : uint8_t
    {
        ePERSPECTIVE,
        eORTHOGRAPHIC
    };
    template <Handedness H, typename NDCConfig, ProjectionMode Mode>
    class ProjectionMatrix; // 仅声明，强制特化

    // -------- 透视模式特化（只包含透视参数）--------
    template <Handedness H, typename NDCConfig>
    class ProjectionMatrix<H, NDCConfig, ProjectionMode::ePERSPECTIVE>
    {
      private:
        // 仅透视参数（正交参数根本不存在于这个类中）
        float m_fovDeg = 45.0F;
        float m_aspect = 16.0F / 9.0F;
        float m_zNear = 0.1F;
        float m_zFar = 100.0F;

      public:
        constexpr ProjectionMatrix() = default;
        constexpr ProjectionMatrix(float fovDeg, float aspect, float zNear,
                                   float zFar) noexcept
            : m_fovDeg(fovDeg), m_aspect(aspect), m_zNear(zNear), m_zFar(zFar)
        {
            assert_check();
        }

        constexpr void assert_check() const noexcept
        {
            NDCConfig::AssertPerspective(m_fovDeg, m_aspect, m_zNear, m_zFar);
        }

        // -------- Setter（仅透视相关）--------
        constexpr auto &&setFov(this auto &&self, float fovDeg) noexcept
        {
            self.m_fovDeg = fovDeg;
            self.assert_check();
            return std::forward<decltype(self)>(self);
        }
        constexpr auto &&setAspect(this auto &&self, float aspect) noexcept
        {
            self.m_aspect = aspect;
            self.assert_check();
            return std::forward<decltype(self)>(self);
        }
        constexpr auto &&setNearFar(this auto &&self, float zNear, float zFar) noexcept
        {
            self.m_zNear = zNear;
            self.m_zFar = zFar;
            self.assert_check();
            return std::forward<decltype(self)>(self);
        }

        // -------- 主动修改（仅透视相关）--------
        constexpr auto &&zoomFov(this auto &&self, float deltaDeg) noexcept
        {
            self.m_fovDeg += deltaDeg;
            self.assert_check();
            return std::forward<decltype(self)>(self);
        }
        constexpr auto &&adjustAspect(this auto &&self, float delta) noexcept
        {
            self.m_aspect += delta;
            self.assert_check();
            return std::forward<decltype(self)>(self);
        }

        // -------- Getter --------
        [[nodiscard]] constexpr float getFov() const noexcept
        {
            return m_fovDeg;
        }
        [[nodiscard]] constexpr float getAspect() const noexcept
        {
            return m_aspect;
        }
        [[nodiscard]] constexpr float getNear() const noexcept
        {
            return m_zNear;
        }
        [[nodiscard]] constexpr float getFar() const noexcept
        {
            return m_zFar;
        }

        constexpr void adjustFovSafe(float deltaDeg) noexcept
        {
            float newFov = m_fovDeg + deltaDeg;
            if (newFov > 0.0F && newFov < 180.0F)
            {
                zoomFov(deltaDeg);
            }
        }
        constexpr void adjustAspectSafe(float deltaDeg)
        {
            if (auto value = m_aspect + deltaDeg; value > 0)
            {
                adjustAspect(deltaDeg);
            }
        }
        constexpr void adjustNearSafe(float delta) noexcept
        {
            float newNear = m_zNear + delta;
            if (newNear > 0.0F && newNear < m_zFar)
            {
                setNearFar(newNear, m_zFar);
            }
        }
        constexpr void adjustFarSafe(float delta) noexcept
        {
            float newFar = m_zFar + delta;
            if (newFar > m_zNear)
            {
                setNearFar(m_zNear, newFar);
            }
        }

        // -------- 核心生成器 --------
        constexpr glm::mat4 operator()() const noexcept
        {
            return NDCConfig::template makePerspective<H>(m_fovDeg, m_aspect, m_zNear,
                                                          m_zFar);
        }
    };

    // -------- 正交模式特化（只包含正交参数）--------
    template <Handedness H, typename NDCConfig>
    class ProjectionMatrix<H, NDCConfig, ProjectionMode::eORTHOGRAPHIC>
    {
      private:
        // 仅正交参数（透视参数根本不存在于这个类中）
        float m_left = -1.0F;
        float m_right = 1.0F;
        float m_bottom = -1.0F;
        float m_top = 1.0F;
        float m_zNear = 0.1F;
        float m_zFar = 100.0F;

      public:
        constexpr ProjectionMatrix() = default;
        constexpr ProjectionMatrix(float left, float right, float bottom, float top,
                                   float zNear, float zFar) noexcept
            : m_left(left), m_right(right), m_bottom(bottom), m_top(top), m_zNear(zNear),
              m_zFar(zFar)
        {
            assert_check();
        }
        constexpr void assert_check() const noexcept
        {
            NDCConfig::AssertOrtho(m_left, m_right, m_bottom, m_top, m_zNear, m_zFar);
        }

        // -------- Setter（仅正交相关）--------
        constexpr auto &&setOrthoBounds(this auto &&self, float left, float right,
                                        float bottom, float top) noexcept
        {
            self.m_left = left;
            self.m_right = right;
            self.m_bottom = bottom;
            self.m_top = top;
            self.assert_check();
            return std::forward<decltype(self)>(self);
        }
        constexpr auto &&setNearFar(this auto &&self, float zNear, float zFar) noexcept
        {
            self.m_zNear = zNear;
            self.m_zFar = zFar;
            self.assert_check();
            return std::forward<decltype(self)>(self);
        }

        // -------- 主动修改（仅正交相关）--------
        constexpr auto &&shiftOrthoView(this auto &&self, float dx, float dy) noexcept
        {
            self.m_left += dx;
            self.m_right += dx;
            self.m_bottom += dy;
            self.m_top += dy;
            self.assert_check();
            return std::forward<decltype(self)>(self);
        }
        constexpr auto &&scaleOrthoView(this auto &&self, float factor) noexcept
        {
            float cx = (self.m_left + self.m_right) * 0.5F;
            float cy = (self.m_bottom + self.m_top) * 0.5F;
            float hw = (self.m_right - self.m_left) * 0.5F * factor;
            float hh = (self.m_top - self.m_bottom) * 0.5F * factor;
            self.m_left = cx - hw;
            self.m_right = cx + hw;
            self.m_bottom = cy - hh;
            self.m_top = cy + hh;
            self.assert_check();
            return std::forward<decltype(self)>(self);
        }

        // -------- Getter --------
        [[nodiscard]] constexpr float getLeft() const noexcept
        {
            return m_left;
        }
        [[nodiscard]] constexpr float getRight() const noexcept
        {
            return m_right;
        }
        [[nodiscard]] constexpr float getBottom() const noexcept
        {
            return m_bottom;
        }
        [[nodiscard]] constexpr float getTop() const noexcept
        {
            return m_top;
        }
        [[nodiscard]] constexpr float getNear() const noexcept
        {
            return m_zNear;
        }
        [[nodiscard]] constexpr float getFar() const noexcept
        {
            return m_zFar;
        }

        // -------- 核心生成器 --------
        constexpr glm::mat4 operator()() const noexcept
        {
            return NDCConfig::template makeOrtho<H>(m_left, m_right, m_bottom, m_top,
                                                    m_zNear, m_zFar);
        }
    };

    // -------- 别名（完全解耦）--------
    template <Handedness H, typename NDCConfig>
    using PerspectiveProjection =
        ProjectionMatrix<H, NDCConfig, ProjectionMode::ePERSPECTIVE>;

    template <Handedness H, typename NDCConfig>
    using OrthographicProjection =
        ProjectionMatrix<H, NDCConfig, ProjectionMode::eORTHOGRAPHIC>;

    // 具体到 Vulkan
    using VulkanPerspectiveProjection =
        PerspectiveProjection<Handedness::eRIGHT_HANDED, VulkanNDCConfig>;
    using VulkanOrthographicProjection =
        OrthographicProjection<Handedness::eRIGHT_HANDED, VulkanNDCConfig>;

    using VulkanUIOrthographicProjection =
        OrthographicProjection<Handedness::eLEFT_HANDED, VulkanNDCConfig>;

    template <typename ViewMatrix, typename ProjectionMatrix>
        requires(requires(ViewMatrix view, ProjectionMatrix projection) {
            { view() } noexcept -> std::same_as<glm::mat4>;
            { projection() } noexcept -> std::same_as<glm::mat4>;
        })
    class GenCamera
    {
      public:
        constexpr GenCamera() = default;
        constexpr GenCamera(ViewMatrix view, ProjectionMatrix projection) noexcept
            : view_{std::move(view)}, projection_{std::move(projection)}
        {
        }
        auto &&setView(this auto &&self, ViewMatrix view)
        {
            self.view_ = std::move(view);
            return std::forward<decltype(self)>(self);
        }
        auto &&setProjection(this auto &&self, ProjectionMatrix projection)
        {
            self.projection_ = std::move(projection);
            return std::forward<decltype(self)>(self);
        }

        [[nodiscard]] constexpr glm::mat4 getViewMatrix() const noexcept
        {
            return view_();
        }
        [[nodiscard]] constexpr glm::mat4 getProjMatrix() const noexcept
        {
            return projection_();
        }
        constexpr auto viewsMatrix() const noexcept
        {
            return getViewMatrix();
        }
        constexpr auto perspectiveMatrix() const noexcept
        {
            return getProjMatrix();
        }

        auto &refView(this auto &&self) noexcept
        {
            return self.view_;
        }
        auto &refProjection(this auto &&self) noexcept
        {
            return self.projection_;
        }

      private:
        ViewMatrix view_;
        ProjectionMatrix projection_;
    };

    // NOLINTEND
}; // namespace mcs::vulkan::camera