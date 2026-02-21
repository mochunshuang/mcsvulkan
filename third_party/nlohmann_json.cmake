# NOTE: 1. 千万别手动 修改.gitmodules
# git submodule add  https://gh-proxy.org/https://github.com/nlohmann/json.git third_party/nlohmann_json

# 2. 进入 volk 目录
# cd third_party/nlohmann_json

# NOTE:查看可以checkout的： git tag -l | sort -V
# 3. 切换到指定 tag
# git checkout v3.12.0

# 验证确实在标签上
# git describe --tags

# NOTE: 下面是有用的信息
# 查看当前状态
# git status

# NOTE: 查看项目的所有子模块
# cd ../../
# git submodule status
# NOTE: 打印如下
# PS E:\0_github_project\mcsvulkan> git submodule status
# +55f93686c01528224f448c19128836e7df245f72 third_party/nlohmann_json (v3.12.0)