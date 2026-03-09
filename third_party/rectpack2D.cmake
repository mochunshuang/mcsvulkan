# 1. 添加模块
# git submodule add  https://gh-proxy.org/https://github.com/TeamHypersomnia/rectpack2D.git third_party/rectpack2D

# 2. 进入 rectpack2D 目录
# cd third_party/rectpack2D

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v1.3

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/rectpack2D

# git commit -m "Pin rectpack2D"
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/rectpack2D)
