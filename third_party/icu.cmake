# 1. 添加模块
# git submodule add https://gh-proxy.org/https://github.com/unicode-org/icu.git third_party/icu

# 2. 进入 目录
# cd third_party/icu

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v1.3.2

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/icu

# git commit -m "Pin icu to v1.3.2"

# git submodule status

# ============================================================
# 设置 ICU 根目录（请根据实际解压路径修改）
set(ICU_ROOT "E:/mysoftware/icu4c-78.3-Win64-MSVC2022" CACHE PATH "ICU installation directory")

# 创建导入库目标（每个库对应一个 .lib 文件）
add_library(ICU::uc STATIC IMPORTED)
set_target_properties(ICU::uc PROPERTIES
    IMPORTED_LOCATION "${ICU_ROOT}/lib64/icuuc.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${ICU_ROOT}/include"
)

add_library(ICU::i18n STATIC IMPORTED)
set_target_properties(ICU::i18n PROPERTIES
    IMPORTED_LOCATION "${ICU_ROOT}/lib64/icuin.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${ICU_ROOT}/include"
)

add_library(ICU::dt STATIC IMPORTED)
set_target_properties(ICU::dt PROPERTIES
    IMPORTED_LOCATION "${ICU_ROOT}/lib64/icudt.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${ICU_ROOT}/include"
)

# python meson.py setup E:/mysoftware/fribidi-1.0.16/build_release --prefix=E:/mysoftware/fribidi-1.0.16 --buildtype=release --default-library=static -Ddocs=false