#include <meta>
#include <span>
using namespace std::meta;

// ========== 1. 成员访问级别测试 ==========
struct Base
{
  public:
    int pub_mem;
    static double pub_static;
    void pub_func() {}

  protected:
    int prot_mem;
    void prot_func() {}

  private:
    int priv_mem;
    void priv_func() {}

  public:
    // 公开测试接口
    static consteval bool test_members()
    {
        // 公有成员
        if (!is_public(^^pub_mem))
            return false;
        if (is_protected(^^pub_mem))
            return false;
        if (is_private(^^pub_mem))
            return false;

        if (!is_public(^^pub_func))
            return false;
        if (!is_public(^^pub_static))
            return false;

        // 受保护成员
        if (!is_protected(^^prot_mem))
            return false;
        if (is_public(^^prot_mem))
            return false;
        if (is_private(^^prot_mem))
            return false;

        // 私有成员
        if (!is_private(^^priv_mem))
            return false;
        if (is_public(^^priv_mem))
            return false;
        if (is_protected(^^priv_mem))
            return false;

        return true;
    }
};
static_assert(Base::test_members()); // 外部调用 public 接口

// ========== 2. 基类访问级别测试 ==========
// 公有继承
struct PublicDerived : public Base
{
    static consteval bool test_public_base()
    {
        auto bases = bases_of(^^PublicDerived, access_context::current());
        // 用 span 避免 vector::operator[] 常量求值问题
        std::span<const info> sp(bases);
        if (sp.size() != 1)
            return false;
        return is_public(sp[0]); // 公有基类
    }
};
static_assert(PublicDerived::test_public_base());

// 私有继承 —— 测试必须放在 PrivateDerived 自身内部（它有访问权限）
struct PrivateDerived : private Base
{
    static consteval bool test_private_base()
    {
        auto bases = bases_of(^^PrivateDerived, access_context::current());
        std::span<const info> sp(bases);
        if (sp.size() != 1)
            return false;
        return is_private(sp[0]); // 私有基类
    }
};
static_assert(PrivateDerived::test_private_base());

// ========== 3. 嵌套类访问级别 ==========
struct Outer
{
  public:
    struct PublicNested
    {
    };

  protected:
    struct ProtectedNested
    {
    };

  private:
    struct PrivateNested
    {
    };

  public:
    static consteval bool test_nested()
    {
        if (!is_public(^^PublicNested))
            return false;
        if (!is_protected(^^ProtectedNested))
            return false;
        if (!is_private(^^PrivateNested))
            return false;
        return true;
    }
};
static_assert(Outer::test_nested());

// ========== 4. 无效实体（编译失败预期） ==========
// 以下任何 static_assert 若取消注释都应导致编译错误
// static_assert(is_public(^^int)); // 错误
// static_assert(is_public(^^::));            // 错误
// 注意：无法用 static_assert 验证“一定编译失败”，因此本测试仅保留注释说明。

int main()
{
    return 0;
}