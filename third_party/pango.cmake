# 1. 添加模块
# git submodule add https://github.com/HOST-Oman/libraqm.git third_party/pango

# 2. 进入 目录
# cd third_party/pango

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout 1.57.0

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/pango

# git commit -m "Pin pango to 1.57.0"

# NOTE: 查看项目的所有子模块
# git submodule status

# NOTE: 卸载
# 1. 从 .gitmodules 文件中移除子模块配置
# git config -f .gitmodules --remove-section submodule.third_party/pango

# 2. 从本地 Git 配置中移除子模块记录
# git config --remove-section submodule.third_party/pango

# 3. 确定删除
# git add .gitmodules

# 4. 从 Git 索引中移除子模块（取消跟踪）
# git rm --cached third_party/pango

# 5. 删除子模块的本地目录（如果上一步没有自动删除）。 或 手动删除
# rm -rf third_party/pango

# 6. 提及删除
# git commit -m "Remove submodule third_party/pango"

# 7. 验证。 没看到pango就OK了
# git submodule status