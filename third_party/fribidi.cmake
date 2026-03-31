# 1. 进入 FriBidi 源码目录
# cd /d E:\mysoftware\fribidi-1.0.16

# 2. 执行 meson 配置命令
# python E:\mysoftware\meson-1.10.2\meson.py setup build_release --prefix=E:/mysoftware/fribidi-install --buildtype=release --default-library=static -Ddocs=false

# setup：配置构建。
# build_release：构建目录（会在当前目录下新建 build_release 文件夹）。
# --prefix：安装目录，建议单独指定，如 E:/mysoftware/fribidi-install。

# 3. 编译
# ninja -C build_release

# 4. 安装
# ninja -C build_release install
set(FRIBIDI_ROOT "E:/mysoftware/fribidi-install" CACHE PATH "FriBidi installation directory")

add_library(FriBiDi::FriBiDi STATIC IMPORTED)
set_target_properties(FriBiDi::FriBiDi PROPERTIES
    IMPORTED_LOCATION "${FRIBIDI_ROOT}/lib/libfribidi.a"
    INTERFACE_INCLUDE_DIRECTORIES "${FRIBIDI_ROOT}/include"
    INTERFACE_COMPILE_DEFINITIONS "FRIBIDI_LIB_STATIC" # 关键修正
)
