
#include <assert.h>
#include <cassert>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <print>

#include "../head.hpp"

/*
在图形学中，齐次坐标（x,y,z,w）通过一个统一的公式区分点和方向：

w=1 表示空间中的点（位置）
w=0 表示方向向量（无位置）
*/
/*
简而言之，矩阵是具有预定义行数和列数的数字数组。例如，2x3矩阵可以如下所示：
在3D图形中，我们将主要使用4x4矩阵。它们将允许我们转换（x， y，z，w）顶点

*/
// NOTE: 列储存和数学表达式不一样
void Translation_matrices()
{
    /*
平移操作，映射的操作矩阵
[
    1 0 0 X
    0 1 0 Y
    0 0 1 Z
    0 0 0 1
]
平移：vector (10,10,10,1) 在 X 轴上 10 个单位向量
*/
    {

        constexpr glm::mat4 myMatrix{{1, 0, 0, 10}, //
                                     {0, 1, 0, 0},  //
                                     {0, 0, 1, 0},  //
                                     {0, 0, 0, 1}};
        constexpr glm::vec4 myVector{10, 10, 10, 1};

        // https://www.opengl-tutorial.org/assets/images/tuto-3-matrix/translationExamplePosition1.png
        constexpr auto ret = myMatrix * myVector;
        constexpr auto expect_ret = glm::vec4{
            myMatrix[0][0] * myVector.x + myMatrix[0][1] * myVector.y +
                myMatrix[0][2] * myVector.z + myMatrix[0][3] * myVector.w,
            myMatrix[1][0] * myVector.x + myMatrix[1][1] * myVector.y +
                myMatrix[1][2] * myVector.z + myMatrix[1][3] * myVector.w,
            myMatrix[2][0] * myVector.x + myMatrix[2][1] * myVector.y +
                myMatrix[2][2] * myVector.z + myMatrix[2][3] * myVector.w,
            myMatrix[3][0] * myVector.x + myMatrix[3][1] * myVector.y +
                myMatrix[3][2] * myVector.z + myMatrix[3][3] * myVector.w,
        };
        // NOTE:GLM默认使用列主序存储矩阵，而你手动计算时却按行主序的方式访问了矩阵元素
        // NOTE: 列储存和数学表达式不一样。太坑了
        static_assert(ret != expect_ret);

        constexpr auto expect_ret2 = glm::vec4{
            // 注意索引顺序：matrix[列][行]
            myMatrix[0][0] * myVector.x + myMatrix[1][0] * myVector.y +
                myMatrix[2][0] * myVector.z + myMatrix[3][0] * myVector.w, // 行0
            myMatrix[0][1] * myVector.x + myMatrix[1][1] * myVector.y +
                myMatrix[2][1] * myVector.z + myMatrix[3][1] * myVector.w, // 行1
            myMatrix[0][2] * myVector.x + myMatrix[1][2] * myVector.y +
                myMatrix[2][2] * myVector.z + myMatrix[3][2] * myVector.w, // 行2
            myMatrix[0][3] * myVector.x + myMatrix[1][3] * myVector.y +
                myMatrix[2][3] * myVector.z + myMatrix[3][3] * myVector.w, // 行3
        };
        static_assert(ret == expect_ret2);

        // NOTE: 列存储，而不是行存储
        static_assert(ret != glm::vec4{20, 10, 10, 1});
        // 正确的列主序平移矩阵 (将向量沿X轴平移10个单位)
        constexpr glm::mat4 correctMatrix{
            {1, 0, 0, 0}, // 第0列: (1, 0, 0, 0)
            {0, 1, 0, 0}, // 第1列: (0, 1, 0, 0)
            {0, 0, 1, 0}, // 第2列: (0, 0, 1, 0)
            {10, 0, 0, 1} // 第3列: (10, 0, 0, 1)  <--- 平移量在这里！
        };
        static_assert(correctMatrix * myVector == glm::vec4{20, 10, 10, 1});
        /*
输入的w=1：说明我们变换的是一个位置点（在空间中有具体坐标的点）
输出的w=1：说明变换后，它仍然是一个位置点，没有莫名其妙变成一个方向
"这很好"：因为平移变换就应该把点变成另一个点，而【不是把点变成方向】（那会出大问题）
*/
    }
    {
        glm::mat4 myMatrix{//
                           {1, 0, 0, 10},
                           {0, 1, 0, 0},
                           {0, 0, 1, 0},
                           {0, 0, 0, 1}}; // 打印矩阵看看实际布局
        std::print("[row][col]\n");
        for (int row = 0; row < 4; row++)
        {
            for (int col = 0; col < 4; col++)
            {
                std::print("{:f} ", myMatrix[row][col]); // 注意是[row][col]
            }
            std::print("\n");
        }
        std::print("\n[col][row]\n");
        for (int row = 0; row < 4; row++)
        {
            for (int col = 0; col < 4; col++)
            {
                std::print("{:f} ", myMatrix[col][row]); // 注意是[col][row]
            }
            std::print("\n");
        }
    }
    {
        glm::mat4 myMatrix{{1, 0, 0, 10},           // 这 fills 内存的前4个位置
                           {0, 1, 0, 0},            // 这 fills 内存的次4个位置
                           {0, 0, 1, 0},            // 这 fills 内存的再次4个位置
                           {0, 0, 0, 1}};           // 这 fills 内存的最后4个位置
        glm::mat4 myMatrix2{glm::vec4{1, 0, 0, 10}, //
                            glm::vec4{0, 1, 0, 0},  //
                            glm::vec4{0, 0, 1, 0},  //
                            glm::vec4{0, 0, 0, 1}};
        assert(myMatrix == myMatrix2);
    }
    {
        glm::mat4 myMatrix{//
                           {1, 0, 0, 10},
                           {0, 1, 0, 0},
                           {0, 0, 1, 0},
                           {0, 0, 0, 1}};

        // 关键测试：myMatrix[0] 是什么类型？包含什么值？
        auto firstElement = myMatrix[0]; // firstElement 是 glm::vec4

        // 打印 firstElement 的四个分量
        std::print("myMatrix[0] = ({}, {}, {}, {})\n", firstElement.x, firstElement.y,
                   firstElement.z, firstElement.w);
    }
    // 现在让我们看看表示操作 朝向-z轴的方向的向量会发生什么：（0,0，-1,0）
    {
        constexpr glm::mat4 myMatrix{//
                                     {1, 0, 0, 10},
                                     {0, 1, 0, 0},
                                     {0, 0, 1, 0},
                                     {0, 0, 0, 1}};

        // NOTE: 如果w==0，则向量（x， y，z，0）是一个方向。
        constexpr glm::vec4 myVector{0, 0, -1, 0};

        constexpr auto ret = myMatrix * myVector;
        static_assert(ret == myVector); // NOTE: 操作方向向量 没用意义
        // diff: 重点： 移动一个方向没有意义。这个例子前后没用变化
    }
    // 那么，这如何转化为代码呢？
    {
        constexpr glm::mat4 myMatrix =
            glm::translate(glm::mat4(), glm::vec3(10.0F, 0.0F, 0.0F));
        constexpr glm::vec4 myVector(10.0F, 10.0F, 10.0F, 0.0F);
        constexpr glm::vec4 transformedVector = myMatrix * myVector; // guess the result
        static_assert(transformedVector == glm::vec4{0, 0, 0, 0});
        /*
嗯，事实上，在GLSL中你几乎从来没有这样做过。
大多数时候，你使用glm::C++翻译（）来计算你的矩阵，将其发送到GLSL，并且只做乘法：
*/
        /*
合理之处：

实际项目中，确实95%的时间在用高层API

GLM的辅助函数（translate、rotate等）帮你处理了列主序的细节，让你不用手动构造矩阵

不合理之处（或者说“找补”的漏洞）：

你仍然需要理解w=0和w=1的区别——这在着色器里天天都会遇到

调试的时候你必须懂底层——当渲染出错，你需要判断是矩阵构造错了，还是上传错了，还是着色器用错了

glm::translate 返回的矩阵到底是什么顺序的？——如果不懂列主序，你可能还是会用错

一个更诚实的说法可能是
“虽然GLM提供了方便的辅助函数让你不用手动构造矩阵，但理解列主序存储和齐次坐标的含义仍然是必要的——否则当你的物体飞到宇宙尽头时，你都不知道是矩阵用反了还是w写错了。”
*/
    }
}

void identityMatrix()
{
    // 这个很特别。它什么也做不了。但我提到它是因为它和知道A乘以1.0得到A一样重要。
    constexpr glm::mat4 myIdentityMatrix = glm::mat4(1.0F);
    std::print("identityMatrix:\n");
    for (int row = 0; row < 4; row++)
    {
        for (int col = 0; col < 4; col++)
        {
            std::print("{:f} ", myIdentityMatrix[row][col]); // 注意是[row][col]
        }
        std::print("\n");
    }
    // NOTE: 存在是列的，不符合教科书的样子。打印却是符合.....
    static_assert(myIdentityMatrix[0][0] == 1.0F);
    static_assert(myIdentityMatrix[1][1] == 1.0F);
    static_assert(myIdentityMatrix[2][2] == 1.0F);
    static_assert(myIdentityMatrix[3][3] == 1.0F);

    // 证明
    constexpr glm::vec4 myVector{10, 10, 10, 1};
    static_assert(myIdentityMatrix * myVector == myVector);
    {
        constexpr glm::vec4 myVector{12, 10, 10, 1};
        static_assert(myIdentityMatrix * myVector == myVector);
    }
    {
        constexpr glm::vec4 myVector{12, 10, 10, 0};
        static_assert(myIdentityMatrix * myVector == myVector);
    }
    {
        constexpr glm::vec4 myVector{5, 6, 10, 0};
        static_assert(myIdentityMatrix * myVector == myVector);
    }
}
void Scaling_matrices()
{
    /*
缩放矩阵也很容易：
[
    X 0 0 0
    0 Y 0 0
    0 0 Z 0
    0 0 0 1
]
*/
    /*
    template<typename T, qualifier Q>
    GLM_FUNC_QUALIFIER mat<4, 4, T, Q> scale(mat<4, 4, T, Q> const& m, vec<3, T, Q> const&
    v)
    {
        mat<4, 4, T, Q> Result;
        Result[0] = m[0] * v[0];
        Result[1] = m[1] * v[1];
        Result[2] = m[2] * v[2];
        Result[3] = m[3];
        return Result; //NOTE: 打印的w是1的由来
    }
*/
    glm::mat4 myScalingMatrix = glm::scale(glm::mat4(1), glm::vec3(2, 2, 2));
    std::print("myScalingMatrix:\n");
    for (int row = 0; row < 4; row++)
    {
        for (int col = 0; col < 4; col++)
        {
            std::print("{:f} ", myScalingMatrix[row][col]); // 注意是[row][col]
        }
        std::print("\n");
    }
    assert(myScalingMatrix[0][0] == 2.0F);
    assert(myScalingMatrix[1][1] == 2.0F);
    assert(myScalingMatrix[2][2] == 2.0F);
    assert(myScalingMatrix[3][3] == 1.0F);

    {
        constexpr glm::mat4 myScalingMatrix = {//
                                               {2, 0, 0, 0},
                                               {0, 2, 0, 0},
                                               {0, 0, 2, 0},
                                               {0, 0, 0, 1}};
        constexpr glm::vec4 myVector{1, 1, 1, 1};
        static_assert(myScalingMatrix * myVector == glm::vec4{2, 2, 2, 1});

        {
            // w: 不变
            constexpr glm::vec4 myVector{1, 1, 1, 0};
            static_assert(myScalingMatrix * myVector == glm::vec4{2, 2, 2, 0});
        }
        {
            // w: 不变
            constexpr glm::vec4 myVector{1, 1, 1, 4};
            static_assert(myScalingMatrix * myVector == glm::vec4{2, 2, 2, 4});
        }
        {
            // w: 不变
            constexpr glm::vec4 myVector{1, 2, 1, 4};
            static_assert(myScalingMatrix * myVector == glm::vec4{2, 4, 2, 4});
        }
    }
}

void Rotation_matrices()
{
    /*
这些是相当复杂的。我将在这里跳过细节，因为知道它们在日常使用中的确切布局并不重要。
有关更多信息，请查看矩阵和四元数常见问题解答（流行资源，可能也以您的语言提供）。您还可以查看旋转教程。

// Use #include <glm/gtc/matrix_transform.hpp> and #include <glm/gtx/transform.hpp>
glm::vec3 myRotationAxis(??,??,??);
glm::rotate( angle_in_degrees, myRotationAxis );
*/
    glm::mat4 rotMatrix =
        glm::rotate(glm::mat4(1.0F), glm::radians(45.0F), glm::vec3(0.0F, 1.0F, 0.0F));
    std::print("rotMatrix:\n");
    for (int row = 0; row < 4; row++)
    {
        for (int col = 0; col < 4; col++)
        {
            std::print("{:f} ", rotMatrix[row][col]); // 注意是[row][col]
        }
        std::print("\n");
    }
}

void Cumulating_transformations()
{
    /*
所以现在我们知道如何旋转、平移和缩放我们的向量。将这些转换结合起来会很棒。这是通过将矩阵相乘在一起来完成的，例如：
！！！小心！！！这一行实际上是先缩放，然后旋转，然后平移。这就是矩阵乘法的工作原理。

glm::mat4 myModelMatrix = myTranslationMatrix * myRotationMatrix * myScaleMatrix;
glm::vec4 myTransformedVector = myModelMatrix * myOriginalVector;

mat4 transform = mat2 * mat1;
vec4 out_vec = transform * in_vec;
*/
}

int main()
{
#ifdef GLM_FORCE_RADIANS
    std::cout << "define: " << "GLM_FORCE_RADIANS\n";
#endif // GLM_FORCE_RADIANS
#ifdef GLM_FORCE_DEPTH_ZERO_TO_ONE
    std::cout << "define: " << "GLM_FORCE_DEPTH_ZERO_TO_ONE\n";
#endif // GLM_FORCE_DEPTH_ZERO_TO_ONE
#ifdef GLM_ENABLE_EXPERIMENTAL
    std::cout << "define: " << "GLM_FORCE_DEPTH_ZERO_TO_ONE\n";
#endif // GLM_ENABLE_EXPERIMENTAL

    glm::vec4 point(1.0F, 0.0F, 0.0F, 1.0F);     // 位置 (1,0,0)
    glm::vec4 direction(1.0F, 0.0F, 0.0F, 0.0F); // 方向沿 X 轴

    {
        /*
        vec4 是“数据”：它描述了一个位置、方向或颜色。
        mat4
        是“操作”：它描述了你打算如何改变这些数据（例如，把物体从局部坐标系移动到世界坐标系）
        */
        glm::mat4 myMatrix;
        glm::vec4 myVector;
        static_assert(sizeof(myMatrix) == 4 * 4 * sizeof(float));
        static_assert(sizeof(myVector) == 4 * 1 * sizeof(float));

        // auto v = myMatrix[5][5]; //NOTE: 只能运行时检查

        using T0 = decltype(myMatrix * myVector);
        static_assert(std::is_same_v<T0, glm::vec4>);

        using T1 = decltype(myVector * myMatrix);
        static_assert(std::is_same_v<T0, T1>);

        /*
       In C++, with GLM:

       glm::mat4 myMatrix;
       glm::vec4 myVector;
       // fill myMatrix and myVector somehow
       glm::vec4 transformedVector = myMatrix * myVector; // Again, in this order! This is
       important. In GLSL:

       mat4 myMatrix;
       vec4 myVector;
       // fill myMatrix and myVector somehow
       vec4 transformedVector = myMatrix * myVector; // Yeah, it's pretty much the same as
       GLM
       */
    }

    // Translation matrices
    Translation_matrices();
    identityMatrix();
    Scaling_matrices();
    Rotation_matrices();
    /*
NOTE: 结论：
The Model, View and Projection matrices

The Model matrix:
进行以下操作：
Model Space（相对于模型中心定义的所有顶点）到World Space（相对于世界中心定义的所有顶点）

The View matrix：
引擎根本不会移动船。船呆在原地，引擎在它周围移动宇宙。
所以最初你的相机是在世界空间的原点。为了移动世界，你只需引入另一个矩阵。假设你想把3个单位的相机向右移动（+X）。
这相当于把你的整个世界（包括网格）向左移动3个单位！（-X）。当你的大脑融化时，让我们开始吧：
同样，下图说明了这一点：我们从世界空间（相对于世界中心定义的所有顶点，正如我们在上一节中所做的那样）到相机空间（相对于相机定义的所有顶点）
glm::mat4 ViewMatrix = glm::translate(glm::mat4(), glm::vec3(-3.0f, 0.0f ,0.0f));

在你的头爆炸之前，享受GLM的伟大glm::lookAt功能：
glm::mat4 CameraMatrix = glm::lookAt(
    cameraPosition, // the position of your camera, in world space
    cameraTarget,   // where you want to look at, in world space
    upVector        // probably glm::vec3(0,1,0), but (0,-1,0) would make you looking
upside-down, which can be great too
);

The Projection matrix [投影矩阵]：
我们现在在相机空间。这意味着在所有这些转换之后，一个碰巧有x==0和y==0的顶点应该呈现在屏幕中心。但是我们不能只使用x和y坐标来确定一个物体应该放在屏幕上的位置：
 它到相机的距离（z）也很重要！对于具有相似x和y坐标的两个顶点，z坐标最大的顶点将比另一个更位于屏幕中心。
这称为透视投影：
幸运的是，一个4x4的矩阵可以表示这个投影:
// Generates a really hard-to-read matrix, but a normal, standard 4x4 matrix nonetheless
glm::mat4 projectionMatrix = glm::perspective(
    glm::radians(FoV), // The vertical Field of View, in radians: the amount of "zoom".
Think "camera lens". Usually between 90° (extra wide) and 30° (quite zoomed in) 4.0f
/ 3.0f,       // Aspect Ratio. Depends on the size of your window. Notice that 4/3 ==
800/600 == 1280/960, sounds familiar? 0.1f,              // Near clipping plane. Keep as
big as possible, or you'll get precision issues. 100.0f             // Far clipping plane.
Keep as little as possible.
);

这是另一个图表，这样你就可以更好地理解投影会发生什么。
在投影之前，我们在相机空间里有蓝色的物体，红色的形状代表相机的平截头体：相机实际能够看到的场景的一部分。
https://www.opengl-tutorial.org/assets/images/tuto-3-matrix/nondeforme.png
   glm::mat4 projectionMatrix = glm::perspective(
        glm::radians(
            glm::degrees(90)), // The vertical Field of View, in radians: the amount of
                               // "zoom". Think "camera lens". Usually between 90° (extra
                               // wide) and 30° (quite zoomed in)
        4.0f / 3.0f, // Aspect Ratio. Depends on the size of your window. Notice that 4/3
                     // == 800/600 == 1280/960, sounds familiar?
        0.1f,  // Near clipping plane. Keep as big as possible, or you'll get precision
               // issues.
        100.0f // Far clipping plane. Keep as little as possible.
    );

Cumulating transformations: the ModelViewProjection matrix:
// C++: compute the matrix
glm::mat4 MVPmatrix = projection * view * model; // Remember: inverted!
// GLSL: apply it
transformed_vertex = MVP * in_vertex;

*/
    // translate
    {
        std::cout << "translate:\n";
        constexpr glm::mat4 ret =
            glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
        for (int row = 0; row < 4; row++)
        {
            for (int col = 0; col < 4; col++)
            {
                std::print("{:f} ", ret[row][col]); // 注意是[row][col]
            }
            std::print("\n");
        }
        static_assert(ret == glm::mat4{//
                                       {1, 0, 0, 0},
                                       {0, 1, 0, 0},
                                       {0, 0, 1, 0},
                                       {1, 2, 3, 1}});

        constexpr glm::mat4 RET2 = glm::translate(ret, glm::vec3(1.0f, 0.0f, 0.0f));
        static_assert(RET2 == glm::mat4{//
                                        {1, 0, 0, 0},
                                        {0, 1, 0, 0},
                                        {0, 0, 1, 0},
                                        {2, 2, 3, 1}});
        constexpr glm::vec4 MY_VECTOR{10, 10, 10, 1};
        static_assert(RET2 * MY_VECTOR == glm::vec4{12, 12, 13, 1});
    }

    std::cout << "main done\n";
    return 0;
}