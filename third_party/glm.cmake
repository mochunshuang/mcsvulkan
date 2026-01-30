# NOTE: -b 是分支。以下命令是不允许的
# git submodule add -b 1.0.2 https://gh-proxy.com/https://github.com/g-truc/glm.git third_party/glm-1.0.2
# NOTE: 只能先下载，然后cheout 到指定的 tag
# git submodule add https://gh-proxy.com/https://github.com/g-truc/glm.git third_party/glm-1.0.2
# cd third_party/glm-1.0.2
# git checkout tags/1.0.2
# cd ../..
# git add .
# git commit -m "Add glm submodule at tag 1.0.2"

set(GLM_DIR ${CMAKE_SOURCE_DIR}/third_party/glm-1.0.2)

# NOTE: #include 和 import 是不兼容的。写的.cppm不好就是G
# NOTE: 保守一些。
# glm
add_library(glm INTERFACE)
target_sources(glm INTERFACE ${GLM_DIR}/glm/glm.hpp)
target_include_directories(glm SYSTEM INTERFACE ${GLM_DIR})

# NOTE: .cpp内部写了 无需再配置。避免重定义警告
# target_compile_definitions(glm INTERFACE
# GLM_FORCE_SWIZZLE
# GLM_FORCE_RADIANS
# GLM_FORCE_CTOR_INIT
# GLM_ENABLE_EXPERIMENTAL
# )
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_definitions(glm INTERFACE GLM_FORCE_CXX14)
endif()

# NOTE: 删除
# rm -rf third_party/glm-1.0.2
# git rm --cached third_party/glm-1.0.2