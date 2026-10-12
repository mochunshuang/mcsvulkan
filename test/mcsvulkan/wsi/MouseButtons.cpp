#include <iostream>
#include <exception>
#include <utility>
// NOLINTBEGIN

enum class MouseButtons : std::uint8_t
{
    eUNDEFINED = 0xFF,
    eMOUSE_BUTTON_1 = 0,
    eMOUSE_BUTTON_2 = 1,
    eMOUSE_BUTTON_3 = 2,
    eMOUSE_BUTTON_4 = 3,
    eMOUSE_BUTTON_5 = 4,
    eMOUSE_BUTTON_6 = 5,
    eMOUSE_BUTTON_7 = 6,
    eMOUSE_BUTTON_8 = 7,

    eMOUSE_BUTTON_LAST = eMOUSE_BUTTON_8,   // = 7
    eMOUSE_BUTTON_LEFT = eMOUSE_BUTTON_1,   // = 0
    eMOUSE_BUTTON_RIGHT = eMOUSE_BUTTON_2,  // = 1
    eMOUSE_BUTTON_MIDDLE = eMOUSE_BUTTON_3, // = 2

    eSIZE // ← 没写 = ?
};
static_assert((int)(MouseButtons::eMOUSE_BUTTON_MIDDLE) == 2);

//NOTE: 别名要小心 BUG. 存在别名,要么全部设置,要么别使用
static_assert((int)(MouseButtons::eSIZE) == 3);

enum class MouseButtons2 : std::uint8_t
{
    eUNDEFINED = 0xFF,
    eMOUSE_BUTTON_1 = 0,
    eMOUSE_BUTTON_2 = 1,
    eMOUSE_BUTTON_3 = 2,
    eMOUSE_BUTTON_4 = 3,
    eMOUSE_BUTTON_5 = 4,
    eMOUSE_BUTTON_6 = 5,
    eMOUSE_BUTTON_7 = 6,
    eMOUSE_BUTTON_8 = 7,

    // NOTE: 别名
    eMOUSE_BUTTON_LAST = eMOUSE_BUTTON_8,   // = 7
    eMOUSE_BUTTON_LEFT = eMOUSE_BUTTON_1,   // = 0
    eMOUSE_BUTTON_RIGHT = eMOUSE_BUTTON_2,  // = 1
    eMOUSE_BUTTON_MIDDLE = eMOUSE_BUTTON_3, // = 2

    eSIZE = static_cast<std::uint8_t>(eMOUSE_BUTTON_LAST) + 1
};
static_assert((int)(MouseButtons2::eSIZE) == 8); //NOTE: 这样确实好多了
// static_assert((int)(MouseButtons2{}) == 0xFF);

static_assert(std::to_underlying(MouseButtons2::eUNDEFINED) == 0xFF); // ✅
static_assert(std::to_underlying(MouseButtons2{}) == 0); // ✅ 想验证零值就写 0

int main()
try
{
    MouseButtons2 e{};

    // NOTE: 别名也是有问题的: 2. 如果语义上必须区分，就不能用别名
    switch (e)
    {
    case MouseButtons2::eMOUSE_BUTTON_1:;

        // NOTE: case 不允许重复.
        // Duplicate case value: 'eMOUSE_BUTTON_1' and 'eMOUSE_BUTTON_LEFT' both equal '0'
        // case MouseButtons2::eMOUSE_BUTTON_LEFT:;
    }

    std::cout << "main done\n";
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
}
catch (...)
{
    std::cerr << "Unknown exception\n";
    return 1;
} // NOLINTEND