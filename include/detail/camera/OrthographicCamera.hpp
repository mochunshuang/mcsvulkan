#pragma once

#include "../__glm_import.hpp"
#include <print>

namespace mcs::vulkan::camera
{

    // ==================== 2D UI正交投影相机 ====================
    /*
    [2D UI坐标系]
    屏幕空间（像素坐标）：
        (0,0) 左上角
        (width-1, height-1) 右下角
        Y轴向下（与Vulkan屏幕空间一致）

    目标：将像素坐标映射到NDC [-1, 1]

    正交投影矩阵公式：
        [ 2/width,     0,       0,  -1 ]
        [    0,    -2/height,   0,   1 ]  // 负号使Y轴向下
        [    0,         0,   1/(f-n), -n/(f-n) ]
        [    0,         0,       0,     1 ]

    对于2D UI，我们通常使用：
        left = 0, right = width
        top = 0, bottom = height
        near = 0, far = 1
    */
    struct orthographic_camera
    {
      public:
        // ==================== 投影参数 ====================
        float left = 0.0f; // 屏幕左边界（像素）
        float right = 0;   // 屏幕右边界（像素）
        float bottom = 0;  // 屏幕下边界（像素） - Vulkan Y向下
        float top = 0.0f;  // 屏幕上边界（像素）
        float near = 0.0f; // 近平面
        float far = 1.0f;  // 远平面

        // ==================== 相机变换参数 ====================
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f); // 相机位置
        float rotation = 0.0f;                            // 旋转角度（弧度）
        glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);    // 缩放

        // ==================== 构造函数 ====================
        // orthographic_camera() = default;

        orthographic_camera(float left, float right, float bottom, float top,
                            float near = 0.0f, float far = 1.0f)
            : left(left), right(right), bottom(bottom), top(top), near(near), far(far)
        {
        }

        // ==================== 投影矩阵（Vulkan标准） ====================
        /*
        构建Vulkan标准的正交投影矩阵：
        1. X轴：将[left, right]线性映射到[-1, 1]
           proj[0][0] = 2/(r-l)      // X缩放
           proj[3][0] = -(r+l)/(r-l) // X平移

        2. Y轴：将[top, bottom]映射到[-1, 1]，且Y向下
           proj[1][1] = -2/(b-t)     // 负号实现Y轴翻转（向下）
           proj[3][1] = -(b+t)/(b-t) // Y平移

        3. Z轴：将[near, far]映射到[0, 1]（Vulkan深度范围）
           proj[2][2] = 1/(f-n)      // Z缩放
           proj[3][2] = -n/(f-n)     // Z平移（使near映射到0）
        */
        glm::mat4 getProjectionMatrix() const
        {
            // Vulkan NDC坐标系：
            // X: 左(-1) → 右(+1)
            // Y: 上(-1) → 下(+1)  // 注意：Y轴向下！
            // Z: 近(0) → 远(1)     // 与OpenGL [-1,1] 不同！

            glm::mat4 proj = glm::mat4(1.0f);

            // X轴映射：left→right → -1→1
            float invWidth = 1.0f / (right - left);
            proj[0][0] = 2.0f * invWidth;
            proj[3][0] = -(right + left) * invWidth;

            // Y轴映射：top→bottom → -1→1，Y向下（负号翻转）
            float invHeight = 1.0f / (bottom - top);
            proj[1][1] = -2.0f * invHeight; // 关键：负号实现Vulkan Y向下
            proj[3][1] = -(bottom + top) * invHeight;

            // Z轴映射：near→far → 0→1（Vulkan深度范围）
            float invDepth = 1.0f / (far - near);
            proj[2][2] = invDepth;         // Z缩放
            proj[3][2] = -near * invDepth; // Z平移，使near映射到0

            return proj;
        }

        // ==================== 视图矩阵（2D相机变换） ====================
        /*
        2D相机视图矩阵构成（按顺序应用）：
        1. 平移：-position（相机移动反向影响物体位置）
        2. 旋转：绕Z轴旋转-rotation（相机旋转反向影响物体）
        3. 缩放：scale（相机缩放正向影响物体）

        注意：这不是传统3D的"lookAt"视图矩阵，而是2D相机变换矩阵。
        在2D中，我们通常想要：
        - 相机右移 → 画面左移（所以用-position）
        - 相机顺时针旋转 → 画面逆时针旋转（所以用rotation）
        - 相机放大 → 画面放大（所以用scale，不是1/scale）
        */
        glm::mat4 getViewMatrix() const
        {
            glm::mat4 view = glm::mat4(1.0f);

            // 1. 平移：相机位置的反向
            view = glm::translate(view, -position);

            // 2. 旋转：绕Z轴旋转（2D旋转）
            if (rotation != 0.0f)
            {
                view = glm::rotate(view, rotation, glm::vec3(0.0f, 0.0f, 1.0f));
            }

            // 3. 缩放：相机缩放的正向影响
            // 注意：这里不是1/scale，因为我们希望相机放大时物体也放大
            view = glm::scale(view, scale);

            return view;
        }

        // ==================== 视图投影矩阵 ====================
        glm::mat4 getViewProjectionMatrix() const
        {
            return getProjectionMatrix() * getViewMatrix();
        }

        // ==================== 设置屏幕尺寸 ====================
        void setScreenSize(float width, float height)
        {
            right = left + width;
            bottom = top + height; // 保持bottom > top
        }

        // ==================== 变换控制方法 ====================

        // 移动相机
        void translate(const glm::vec2 &translation)
        {
            position.x += translation.x;
            position.y += translation.y;
        }

        void setPosition(const glm::vec2 &newPosition)
        {
            position.x = newPosition.x;
            position.y = newPosition.y;
            position.z = 0.0f; // 2D相机保持在Z=0平面
        }

        // 旋转相机（2D旋转，绕Z轴）
        void rotate(float angleRadians)
        {
            rotation += angleRadians;
            // 保持角度在[0, 2π)范围内
            rotation = fmod(rotation, glm::two_pi<float>());
        }

        void setRotation(float angleRadians)
        {
            rotation = angleRadians;
        }

        // 缩放相机（同时调整视口大小保持内容居中）
        /*
        缩放逻辑：
        1. 更新缩放因子：scale *= zoomFactor
        2. 调整视口边界，保持中心点不变：
           - 计算新的宽度/高度 = 原始宽度/高度 ÷ zoomFactor
           - 重新计算左右/上下边界
        */
        void zoom(float zoomFactor)
        {
            if (zoomFactor <= 0.0f)
                return; // 防止无效缩放

            // 更新缩放因子
            scale *= zoomFactor;

            // 调整视口大小（保持中心点不变）
            float centerX = (left + right) * 0.5f;
            float centerY = (top + bottom) * 0.5f;
            float newWidth = (right - left) / zoomFactor;
            float newHeight = (bottom - top) / zoomFactor;

            left = centerX - newWidth * 0.5f;
            right = centerX + newWidth * 0.5f;
            top = centerY - newHeight * 0.5f;
            bottom = centerY + newHeight * 0.5f;
        }

        void setZoom(float zoomLevel)
        {
            // 重置缩放
            scale = glm::vec3(1.0f);
            zoom(zoomLevel);
        }

        // 重置所有变换
        void resetTransform()
        {
            position = glm::vec3(0.0f);
            rotation = 0.0f;
            scale = glm::vec3(1.0f);
            left = 0.0f;
            // right = WIDTH;
            top = 0.0f;
            // bottom = HEIGHT;
        }

        // ==================== 坐标变换方法 ====================

        // 像素坐标 → NDC坐标（不考虑相机变换）
        // 这是简单的线性映射：像素坐标 → [-1, 1]
        struct PixelToNDC
        {
            glm::vec2 pixel; // 像素坐标（屏幕空间）
            glm::vec2 ndc;   // NDC坐标（规范化设备坐标）
            glm::vec2 world; // 世界坐标（不考虑相机变换）
            bool isVisible;  // 是否在屏幕内
        };

        PixelToNDC transformPixel(float pixelX, float pixelY) const
        {
            PixelToNDC result;
            result.pixel = glm::vec2(pixelX, pixelY);

            // 检查可见性
            result.isVisible =
                (pixelX >= left && pixelX <= right && pixelY >= top && pixelY <= bottom);

            // 像素 → NDC（线性映射公式）
            // ndcX = 2 * (pixelX - left) / (right - left) - 1
            // ndcY = 2 * (pixelY - top) / (bottom - top) - 1
            float invWidth = 1.0f / (right - left);
            float invHeight = 1.0f / (bottom - top);

            result.ndc.x = 2.0f * (pixelX - left) * invWidth - 1.0f;
            result.ndc.y = 2.0f * (pixelY - top) * invHeight - 1.0f;

            // NDC → 世界坐标（不考虑相机变换，只使用投影逆矩阵）
            glm::mat4 invProj = glm::inverse(getProjectionMatrix());
            glm::vec4 worldPos =
                invProj * glm::vec4(result.ndc.x, result.ndc.y, 0.0f, 1.0f);

            if (worldPos.w != 0.0f)
            {
                worldPos /= worldPos.w; // 透视除法（正交投影w=1，但保持通用性）
            }

            result.world = glm::vec2(worldPos.x, worldPos.y);

            return result;
        }

        // 像素坐标 → 世界坐标（考虑完整的相机变换）
        glm::vec2 pixelToWorld(float pixelX, float pixelY) const
        {
            // 1. 像素 → NDC
            float ndcX = 2.0f * (pixelX - left) / (right - left) - 1.0f;
            float ndcY = 2.0f * (pixelY - top) / (bottom - top) - 1.0f;

            // 2. NDC → 裁剪空间 → 世界空间（使用完整的视图投影逆矩阵）
            glm::mat4 inverseVP = glm::inverse(getViewProjectionMatrix());
            glm::vec4 worldPos = inverseVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);

            // 3. 透视除法
            if (worldPos.w != 0.0f)
            {
                worldPos.x /= worldPos.w;
                worldPos.y /= worldPos.w;
            }

            return glm::vec2(worldPos.x, worldPos.y);
        }

        // 世界坐标 → 像素坐标（考虑相机变换）
        glm::vec2 worldToPixel(const glm::vec2 &worldPos) const
        {
            // 1. 世界 → 裁剪空间（应用视图投影矩阵）
            glm::vec4 clipPos =
                getViewProjectionMatrix() * glm::vec4(worldPos, 0.0f, 1.0f);

            // 2. 透视除法得到NDC
            if (clipPos.w != 0.0f)
            {
                clipPos.x /= clipPos.w;
                clipPos.y /= clipPos.w;
            }

            // 3. NDC → 像素（逆映射公式）
            // pixelX = left + (ndcX + 1) * 0.5 * (right - left)
            // pixelY = top + (ndcY + 1) * 0.5 * (bottom - top)
            float pixelX = left + (clipPos.x + 1.0f) * 0.5f * (right - left);
            float pixelY = top + (clipPos.y + 1.0f) * 0.5f * (bottom - top);

            return glm::vec2(pixelX, pixelY);
        }

        // ==================== 创建特定UI区域的相机 ====================
        static orthographic_camera createForUI(float x, float y, float width,
                                               float height)
        {
            // UI坐标系：Y向下，与Vulkan一致
            // (x, y)是左上角，(x+width, y+height)是右下角
            return orthographic_camera(x, x + width, y + height, y);
        }

        // ==================== 辅助方法 ====================

        // 获取相机朝向（2D）
        glm::vec2 getForward() const
        {
            // 在2D中，前向通常指向屏幕外，这里返回旋转方向
            return glm::vec2(cos(rotation), sin(rotation));
        }

        glm::vec2 getRight() const
        {
            return glm::vec2(-sin(rotation), cos(rotation));
        }

        // 获取视口中心（像素坐标）
        glm::vec2 getViewportCenter() const
        {
            return glm::vec2(left + (right - left) * 0.5f, top + (bottom - top) * 0.5f);
        }

        // 获取视口大小
        glm::vec2 getViewportSize() const
        {
            return glm::vec2(right - left, bottom - top);
        }

        // 设置视口（保持宽高比）
        void setViewport(float width, float height, bool keepAspectRatio = true,
                         float targetAspectRatio = 16.0f / 9.0f)
        {
            if (keepAspectRatio)
            {
                float currentAspect = width / height;
                if (currentAspect > targetAspectRatio)
                {
                    // 太宽，调整高度
                    float newHeight = width / targetAspectRatio;
                    float centerY = (top + bottom) * 0.5f;
                    top = centerY - newHeight * 0.5f;
                    bottom = centerY + newHeight * 0.5f;
                    right = left + width;
                }
                else
                {
                    // 太高，调整宽度
                    float newWidth = height * targetAspectRatio;
                    float centerX = (left + right) * 0.5f;
                    left = centerX - newWidth * 0.5f;
                    right = centerX + newWidth * 0.5f;
                    bottom = top + height;
                }
            }
            else
            {
                right = left + width;
                bottom = top + height;
            }
        }

        // 更新相机，使其包含特定区域
        void fitToBounds(const glm::vec2 &minBounds, const glm::vec2 &maxBounds,
                         float padding = 0.1f)
        {
            // 计算边界框
            glm::vec2 boundsSize = maxBounds - minBounds;
            glm::vec2 boundsCenter = (minBounds + maxBounds) * 0.5f;

            // 设置相机位置
            setPosition(boundsCenter);

            // 计算需要的缩放
            glm::vec2 viewportSize = getViewportSize();
            float scaleX = boundsSize.x * (1.0f + padding) / viewportSize.x;
            float scaleY = boundsSize.y * (1.0f + padding) / viewportSize.y;
            float requiredScale = glm::max(scaleX, scaleY);

            // 设置缩放
            setZoom(1.0f / requiredScale);
        }

        // ==================== 调试方法 ====================
        void printInfo() const
        {
            // std::cout << "\nOrthographicCamera信息：" << std::endl;
            // std::cout << "  视口: left=" << left << ", right=" << right
            //           << ", bottom=" << bottom << ", top=" << top << std::endl;
            // std::cout << "  深度: near=" << near << ", far=" << far << std::endl;
            // std::cout << "  变换: position=(" << position.x << "," << position.y
            //           << "), rotation=" << glm::degrees(rotation) << "°, scale=("
            //           << scale.x << "," << scale.y << ")" << std::endl;
            std::print("");
        }
    };
} // namespace mcs::vulkan::camera