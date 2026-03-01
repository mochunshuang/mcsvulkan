# 1. 添加模块
# git submodule add  https://gh-proxy.org/https://github.com/harfbuzz/harfbuzz.git third_party/harfbuzz

# 2. 进入 harfbuzz 目录
# cd third_party/harfbuzz

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout 12.3.2

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/harfbuzz

# git commit -m "Pin harfbuzz to 12.3.2"

# NOTE: 查看项目的所有子模块
# git submodule status

add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/harfbuzz)
