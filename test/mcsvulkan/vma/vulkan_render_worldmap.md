# Vulkan 渲染世界图 · Render World Map

> 依据 Vulkan 1.4.341 规范（`chapters/cmdbuffers.html`，并串联 `renderpass` / `synchronization` / `pipelines` / `descriptorsets` / `wsi` 各章）。
> 读法：**节点 = 结构体 / 句柄 / 命令**，**连线 = 创建、引用、录制、提交、同步**。
> 配套交互版：`vulkan_render_worldmap.html`（可缩放、可悬停高亮、可按边类型过滤）。

---

## 0. 读图约定

| 记号 | 含义 |
|---|---|
| `VkXxxCreateInfo` | 创建信息结构体：只在创建那一刻存在，之后可以销毁 |
| `VkXxx`（句柄） | 设备对象：由 `VkDevice` 持有，创建/销毁必须成对 |
| `vkCmd*` | 录制命令：只写命令流，不执行 |
| `->` | 主流程（数据/控制向前推进） |
| `..>` | 资源依赖（谁引用谁） |
| `==>` | 同步（屏障 / 信号量 / 栅栏） |
| `~~>` | 并行分支 / 帧循环回边 |

一条贯穿全图的事实：

> `vkCmd*` 把命令写进 `VkCommandBuffer` 的内存；`vkQueueSubmit2` 才把这段内存交给 GPU；
> `vkQueuePresentKHR` 把结果交给显示引擎。**三者之间只有同步原语，没有隐式顺序。**

---

## 1. 世界地图（九块大陆 + 分支）

```
VULKAN RENDER WORLD
|
+-- (1) FOUNDATION ......... VkInstance -> VkPhysicalDevice -> VkDevice -> VkQueue
|     +-- app/instance ...... VkApplicationInfo -> VkInstanceCreateInfo -> VkInstance
|     +-- features/props .... VkPhysicalDeviceFeatures2 / Properties2 / MemoryProperties
|     +-- queue families .... VkQueueFamilyProperties -> VkDeviceQueueCreateInfo
|     +-- device ............ VkDeviceCreateInfo -> VkDevice -> vkGetDeviceQueue
|
+-- (2) RESOURCES .......... VkBufferCreateInfo -> VkBuffer
|     +-- image ............. VkImageCreateInfo -> VkImage
|     +-- view .............. VkImageViewCreateInfo -> VkImageView
|     +-- sampler ........... VkSamplerCreateInfo -> VkSampler
|     +-- memory ............ VkMemoryRequirements2 -> VkMemoryAllocateInfo -> VkDeviceMemory
|     +-- bind .............. VkBindBufferMemoryInfo / VkBindImageMemoryInfo
|     +-- subresource ....... VkImageSubresourceRange / VkImageSubresourceLayers
|
+-- (3) DESCRIPTORS ........ VkDescriptorSetLayoutCreateInfo -> VkDescriptorSetLayout
|     +-- binding ........... VkDescriptorSetLayoutBinding (type / count / stageFlags)
|     +-- pool .............. VkDescriptorPoolCreateInfo -> VkDescriptorPool
|     +-- set ............... VkDescriptorSetAllocateInfo -> VkDescriptorSet
|     +-- update ............ VkWriteDescriptorSet -> vkUpdateDescriptorSets
|     +-- template .......... VkDescriptorUpdateTemplateCreateInfo
|     +-- push (1.4) ........ VkPushDescriptorSetInfo -> vkCmdPushDescriptorSet2
|
+-- (4) PIPELINE ........... VkShaderModuleCreateInfo -> VkShaderModule
|     +-- stage ............. VkPipelineShaderStageCreateInfo (+ VkSpecializationInfo)
|     +-- fixed states ...... VkPipelineVertexInputStateCreateInfo
|     |                       VkPipelineInputAssemblyStateCreateInfo
|     |                       VkPipelineTessellationStateCreateInfo
|     |                       VkPipelineViewportStateCreateInfo
|     |                       VkPipelineRasterizationStateCreateInfo
|     |                       VkPipelineMultisampleStateCreateInfo
|     |                       VkPipelineDepthStencilStateCreateInfo
|     |                       VkPipelineColorBlendStateCreateInfo
|     |                       VkPipelineDynamicStateCreateInfo
|     +-- layout ............ VkPipelineLayoutCreateInfo -> VkPipelineLayout
|     +-- PSO ............... VkGraphicsPipelineCreateInfo -> VkPipeline
|     +-- compute ........... VkComputePipelineCreateInfo -> VkPipeline
|     +-- cache ............. VkPipelineCacheCreateInfo -> VkPipelineCache
|     +-- dynamic rendering . VkPipelineRenderingCreateInfo (pNext, renderPass = NULL)
|
+-- (5) RENDER TARGET ...... VkRenderPassCreateInfo -> VkRenderPass
|     +-- attachments ....... VkAttachmentDescription / VkAttachmentReference
|     +-- subpasses ......... VkSubpassDescription / VkSubpassDependency
|     +-- framebuffer ....... VkFramebufferCreateInfo -> VkFramebuffer
|     +-- begin ............. VkRenderPassBeginInfo -> vkCmdBeginRenderPass
|     +-- dynamic (1.3) ..... VkRenderingInfo -> vkCmdBeginRendering
|     |                       VkRenderingAttachmentInfo (load/store/resolve/clear)
|     +-- local read (1.4) .. VkRenderingAttachmentLocationInfo
|
+-- (6) RECORD ............. VkCommandPoolCreateInfo -> VkCommandPool
|     +-- allocate .......... VkCommandBufferAllocateInfo -> VkCommandBuffer
|     +-- begin ............. VkCommandBufferBeginInfo (+ VkCommandBufferInheritanceInfo)
|     +-- bind .............. vkCmdBindPipeline / vkCmdBindDescriptorSets / vkCmdPushConstants
|     +-- geometry .......... vkCmdBindVertexBuffers / vkCmdBindIndexBuffer
|     +-- draw .............. vkCmdDrawIndexed / vkCmdDrawIndirectCount
|     +-- dispatch .......... vkCmdDispatch / vkCmdDispatchIndirect
|     +-- copy .............. vkCmdCopyBufferToImage / vkCmdBlitImage / vkCmdResolveImage
|     +-- barrier ........... vkCmdPipelineBarrier2 (VkDependencyInfo)
|     +-- queries ........... vkCmdBeginQuery / vkCmdWriteTimestamp2
|     +-- secondary ......... vkCmdExecuteCommands
|     +-- end ............... vkEndCommandBuffer  ->  executable
|
+-- (7) SYNC & SUBMIT ...... VkDependencyInfo -> vkCmdPipelineBarrier2
|     +-- barriers .......... VkMemoryBarrier2 / VkBufferMemoryBarrier2 / VkImageMemoryBarrier2
|     +-- layouts ........... VkImageLayout (oldLayout -> newLayout)
|     +-- semaphore ......... VkSemaphoreCreateInfo -> VkSemaphore (+ timeline)
|     +-- fence ............. VkFenceCreateInfo -> VkFence
|     +-- submit ............ VkSubmitInfo2 -> vkQueueSubmit2
|     |                       VkSemaphoreSubmitInfo / VkCommandBufferSubmitInfo
|     +-- wait .............. vkWaitForFences / vkQueueWaitIdle
|
+-- (8) PRESENT ............ VkSurfaceKHR -> VkSwapchainCreateInfoKHR -> VkSwapchainKHR
|     +-- capabilities ...... VkSurfaceCapabilitiesKHR / VkSurfaceFormatKHR / VkPresentModeKHR
|     +-- images ............ vkGetSwapchainImagesKHR -> VkImage[] -> VkImageView[]
|     +-- acquire ........... VkAcquireNextImageInfoKHR -> vkAcquireNextImageKHR
|     +-- present ........... VkPresentInfoKHR -> vkQueuePresentKHR
|
+-- (9) EXTENSIONS ......... VkRayTracingPipelineCreateInfoKHR / VkAccelerationStructureKHR
      +-- mesh .............. VkPhysicalDeviceMeshShaderFeaturesEXT -> vkCmdDrawMeshTasksEXT
      +-- video ............. VkVideoSessionCreateInfoKHR -> VkVideoSessionKHR
```

主干航路（执行顺序，不是创建顺序）：

```
(1) FOUNDATION
      |
      v
(2) RESOURCES --.        (5) RENDER TARGET
      |          \             |
      v           '--> (4) PIPELINE --.
(3) DESCRIPTORS                       |
                                      v
                                 (6) RECORD --(submit)--> (7) SYNC --(present)--> (8) PRESENT
                                      ^                                              |
                                      '----------------- frame loop -----------------'
```

---

## 2. 每帧时序（Frame Graph）

```
[1] vkWaitForFences(frame.fence)              <- 上一帧 GPU 完成，复用第 N 帧资源
[2] vkAcquireNextImageKHR(swapchain, ...)     <- 拿 VkImage 索引，signal imageAvailable
[3] vkResetCommandBuffer / vkBeginCommandBuffer
[4] record:
      barrier: UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
      vkCmdBeginRenderPass / vkCmdBeginRendering
        vkCmdBindPipeline
        vkCmdBindDescriptorSets / vkCmdPushConstants
        vkCmdBindVertexBuffers / vkCmdBindIndexBuffer
        vkCmdDrawIndexed ...
      vkCmdEndRenderPass / vkCmdEndRendering
      barrier: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
[5] vkEndCommandBuffer
[6] vkQueueSubmit2(queue, &VkSubmitInfo2, fence)
        wait   : imageAvailableSem  @ COLOR_ATTACHMENT_OUTPUT
        signal : renderFinishedSem
[7] vkQueuePresentKHR(&VkPresentInfoKHR)      <- wait renderFinishedSem
[8] 下一帧，回到 [1]
```

关键点：
- **[2] 与 [6] 之间**：`VkImage` 的所有权从 presentation engine 转到你的 queue，必须靠 `imageAvailableSem`。
- **[6] 与 [7] 之间**：靠 `renderFinishedSem`，否则会在渲染未完成时 present。
- **[1] 的 fence** 只保证这一帧的 GPU 工作完成，不保证 present 完成；present 完成要靠 `VkSwapchainPresentFenceInfoKHR`（swapchain maintenance 1）。

---

## 3. 九块大陆详解

### (1) 地基 · Instance / Device / Queue

```
VkApplicationInfo --.
                   '--> VkInstanceCreateInfo ==> VkInstance
layers/extensions --'                              |
                                                   +--> vkEnumeratePhysicalDevices
                                                   v
                                           VkPhysicalDevice
                                                   |
        +------------------------------------------+---------------------------+
        v                                          v                           v
VkPhysicalDeviceFeatures2              VkPhysicalDeviceProperties2   VkPhysicalDeviceMemoryProperties
VkPhysicalDeviceVulkan13/14Features    VkPhysicalDeviceLimits        VkMemoryType[32] / VkMemoryHeap[16]
        |                                          |
        '-------------------+----------------------'
                            v
        VkDeviceQueueCreateInfo[] --> VkDeviceCreateInfo ==> VkDevice
              (queueFamilyIndex,                |                |
               pQueuePriorities)                |                +--> VkQueue (graphics)
                                                v                +--> VkQueue (compute)
                                    pNext -> VkPhysicalDeviceFeatures2   +--> VkQueue (transfer)
                                                                 +--> VkQueue (present)
```

分支：
- **扩展/层**：`VkInstanceCreateInfo::ppEnabledExtensionNames`（`VK_KHR_surface`、`VK_KHR_swapchain`、`VK_EXT_debug_utils` …）。
- **API 版本**：`VkApplicationInfo::apiVersion`；设备能力用 `VkPhysicalDeviceVulkan11/12/13/14Features` 一次查询。
- **队列族**：一个 family 可含多种 `VkQueueFlagBits`；`VkQueue` 由 `vkGetDeviceQueue` 取出，不是创建出来的。

---

### (2) 资源 · Memory / Buffer / Image / Sampler

```
                +----------------------+        +----------------------+
                | VkBufferCreateInfo   |        | VkImageCreateInfo    |
                | size/usage/sharing   |        | format/extent/mips   |
                +----------+-----------+        +----------+-----------+
                           v                               v
                       VkBuffer                        VkImage
                           |                               |
         +-----------------+---------------+               +--------------+
         v                 v               v               v              v
VkBufferViewCreateInfo  vkCmdBind*  VkImageViewCreateInfo  vkCmdCopy*  VkFramebuffer
         |                 |               |                               |
         v                 |               v                               |
   VkBufferView            |          VkImageView --> VkDescriptorImageInfo |
                           |               |                               |
                           +-------+-------+-------------------------------+
                                   v
                     VkMemoryRequirements2 (size/alignment/typeBits)
                                   |
                                   v
                     VkMemoryAllocateInfo ==> VkDeviceMemory
                                   |
                                   v
           VkBindBufferMemoryInfo / VkBindImageMemoryInfo
           (vkBindBufferMemory2 / vkBindImageMemory2)
```

分支：
- **内存路径（两条）**
  - 传统：`vkGetBufferMemoryRequirements` -> `VkMemoryRequirements` -> `vkAllocateMemory` -> `vkBindBufferMemory`。
  - 1.1+：`VkMemoryRequirements2` + `VkBindBufferMemoryInfo`（可链 `VkBindBufferMemoryDeviceGroupInfo`）。
- **专用分配**：`VkMemoryDedicatedAllocateInfo`（pNext 链到 `VkMemoryAllocateInfo`）。
- **稀疏**：`VK_IMAGE_CREATE_SPARSE_*` + `VkBindSparseInfo`（走 `vkQueueBindSparse`，不在渲染主流程）。
- **采样器**：`VkSamplerCreateInfo` -> `VkSampler`，只被描述符引用，不直接进命令。
- **布局**：`VkImage` 创建时 `initialLayout` 通常是 `UNDEFINED`；进渲染前必须靠 `VkImageMemoryBarrier2` 转成目标布局。

---

### (3) 描述符 · SetLayout / Pool / Set

```
VkDescriptorSetLayoutBinding[] --> VkDescriptorSetLayoutCreateInfo ==> VkDescriptorSetLayout
      (binding, type, stageFlags, immutableSamplers)                        |
                                                                            |
VkDescriptorPoolSize[] --> VkDescriptorPoolCreateInfo ==> VkDescriptorPool  |
                                                              |             |
                                                              v             v
                                            VkDescriptorSetAllocateInfo ==> VkDescriptorSet
                                                                            |
                   VkDescriptorBufferInfo / VkDescriptorImageInfo --.       |
                   VkBufferView (texel) ----------------------------'---> VkWriteDescriptorSet
                   VkSampler + VkImageView -------------------------'       |
                                                                            v
                                                              vkUpdateDescriptorSets
                                                                            |
                              +---------------------------------------------+
                              v                                             v
               VkDescriptorUpdateTemplateCreateInfo          VkPushDescriptorSetInfo (1.4)
               (批量 / 模板更新)                               (免 set，直接推)
```

分支：
- **传统绑定**：`vkCmdBindDescriptorSets(layout, firstSet, sets, dynamicOffsets)`。
- **Push descriptor**：`vkCmdPushDescriptorSet` / `vkCmdPushDescriptorSet2`（`VK_KHR_push_descriptor`，1.4 提升）。
- **Descriptor buffer**：`VkDescriptorBufferBindingInfoEXT` + `vkCmdSetDescriptorBufferOffsetsEXT`（绕开 set）。
- **动态 offset**：`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`，offset 在 bind 时给。

---

### (4) 管线 · Shader / Layout / PSO

```
VkShaderModuleCreateInfo ==> VkShaderModule
                                   |
                                   v
                 VkPipelineShaderStageCreateInfo (stage, module, pName, spec)
                                   |
        +--------------------------+-------------------------------+
        v                          v                               v
 VkPipelineVertexInputState  VkPipelineInputAssemblyState   VkPipelineTessellationState
 VkPipelineViewportState     VkPipelineRasterizationState   VkPipelineMultisampleState
 VkPipelineDepthStencilState VkPipelineColorBlendState      VkPipelineDynamicState
        |                          |                               |
        '--------------+-----------+---------------+---------------'
                       v                           v
        VkPipelineLayoutCreateInfo ==> VkPipelineLayout
        (pSetLayouts, pPushConstantRanges)         |
                       |                           |
                       '-------------+-------------'
                                     v
                VkGraphicsPipelineCreateInfo ==> VkPipeline
                (+ VkPipelineRenderingCreateInfo 走 pNext)
                                     |
              +----------------------+----------------------+
              v                      v                      v
    VkComputePipelineCreateInfo  VkRayTracingPipeline...  VkPipelineCache
    (compute 只有 1 个 stage)     (KHR/NV, SBT)            (磁盘缓存)
```

分支：
- **图形 / 计算 / 光追**：`VkPipelineBindPoint` = `GRAPHICS` / `COMPUTE` / `RAY_TRACING_KHR`。
- **静态 vs 动态状态**：`VkPipelineDynamicStateCreateInfo` 里列出的状态改由 `vkCmdSet*` 在录制时给；1.3 extended dynamic state 把 `cullMode`、`depthTest` 等也动态化。
- **派生管线**：`VkPipelineCreateFlags::DERIVATIVE` + `basePipelineHandle`。
- **库管线**：`VK_PIPELINE_CREATE_LIBRARY_BIT_KHR` + `VkPipelineLibraryCreateInfoKHR`（1.4 graphics pipeline library）。
- **Shader object**：`VkShaderCreateInfoEXT`，完全跳过 PSO（`VK_EXT_shader_object`）。
- **动态渲染管线**：`renderPass = VK_NULL_HANDLE`，用 `VkPipelineRenderingCreateInfo` 在 pNext 里声明附件格式。

---

### (5) 渲染目标 · Render Pass / Framebuffer / Dynamic Rendering

**分支 A：传统 Render Pass（帧缓冲）**

```
VkAttachmentDescription[] --.
                           +--> VkRenderPassCreateInfo ==> VkRenderPass
VkSubpassDescription[] ----'        (attachments / subpasses / dependencies)
   +-- VkAttachmentReference (color)
   +-- VkAttachmentReference (input)      <- 上一 subpass 的 color 变成这一 pass 的 input
   +-- VkAttachmentReference (resolve)
   +-- VkAttachmentReference (depth/stencil)
   +-- VkSubpassDependency[]  (src/dst stage+access, byRegion)
                           |
                           v
                 VkFramebufferCreateInfo ==> VkFramebuffer
                 (renderPass + VkImageView[] + width/height/layers)
                           |
                           v
                 VkRenderPassBeginInfo ==> vkCmdBeginRenderPass
                 (renderArea, pClearValues)
                           |
                     vkCmdNextSubpass x N
                           |
                     vkCmdEndRenderPass
```

**分支 B：动态渲染（1.3 core）**

```
VkRenderingAttachmentInfo[] --.
  (imageView, imageLayout,   +--> VkRenderingInfo ==> vkCmdBeginRendering
   resolveMode, loadOp,      |      (renderArea, layerCount, viewMask,
   storeOp, clearValue)      |       pColorAttachments, pDepthAttachment, pStencilAttachment)
                             v
                    vkCmdEndRendering
```

**分支 C：多 subpass / 延迟渲染（G-buffer）**

```
subpass0: MRT 写 G-buffer (VkAttachmentReference x N)
    |  input attachment（VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 或 GENERAL）
    v
subpass1: 读 G-buffer 做 shading，写 swapchain image
```

分支细节：
- **兼容性**：`VkFramebuffer` 必须与 `VkRenderPass` 兼容；`VkGraphicsPipelineCreateInfo::renderPass/subpass` 必须与录制时一致。
- **动态渲染管线**：`renderPass` 为 `VK_NULL_HANDLE`，附件格式来自 `VkPipelineRenderingCreateInfo`。
- **本地读**：`VkRenderingAttachmentLocationInfo` / `VkRenderingInputAttachmentIndexInfo`（1.4 dynamic rendering local read）。
- **imageless framebuffer**：`VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT` + `VkRenderPassAttachmentBeginInfo`。

---

### (6) 命令录制 · Command Pool / Command Buffer

```
VkCommandPoolCreateInfo ==> VkCommandPool
   (queueFamilyIndex, TRANSIENT / RESET_COMMAND_BUFFER)
                                |
                                v
VkCommandBufferAllocateInfo ==> VkCommandBuffer
   (LEVEL_PRIMARY | LEVEL_SECONDARY)
                                |
                                v
VkCommandBufferBeginInfo ==> vkBeginCommandBuffer
   (ONE_TIME_SUBMIT | SIMULTANEOUS_USE | RENDER_PASS_CONTINUE)
                                |
        +-----------------------+----------------------------------+
        v                       v                                  v
  vkCmdBindPipeline      vkCmdBindDescriptorSets          vkCmdPipelineBarrier2
  vkCmdBindVertexBuffers vkCmdPushConstants                vkCmdBeginQuery
  vkCmdBindIndexBuffer   vkCmdDrawIndexed                  vkCmdWriteTimestamp2
                         vkCmdDrawIndirectCount            vkCmdExecuteCommands
                         vkCmdDispatch(Indirect)
                         vkCmdCopyBufferToImage / Blit / Resolve
                                |
                                v
                     vkEndCommandBuffer  -->  Executable
                                |
                                v
              (由 (7) vkQueueSubmit2 提交) --> Pending --> Executable / Invalid
```

状态机（规范原文的五态）：

```
 allocate           vkBeginCommandBuffer        vkEndCommandBuffer
   |                       |                          |
   v                       v                          v
Initial ------------> Recording ----------------> Executable
   ^                       |                          |
   | reset/free            |                          | submit
   |                       |                          v
   +-----------------------+---------------------- Pending
                                                      |
                                     +----------------+----------------+
                                     v                                 v
                                 Executable                       Invalid (ONE_TIME_SUBMIT)
```

分支细节：
- **主/次缓冲**：`PRIMARY` 可提交给队列；`SECONDARY` 只能被 `vkCmdExecuteCommands` 调用，且不继承任何状态（除 render pass 实例内）。
- **并行录制**：每个线程一个 `VkCommandPool`（池必须外部同步），主线程用 `vkCmdExecuteCommands` 合并。
- **状态重置**：`vkBeginCommandBuffer` 开始时所有状态 undefined；每次 bind 都要重新给。
- **一次性**：`ONE_TIME_SUBMIT` 提交后进入 Invalid，必须重新录制。

---

### (7) 同步与提交 · Barrier / Semaphore / Fence / Submit

```
VkImageMemoryBarrier2 / VkBufferMemoryBarrier2 / VkMemoryBarrier2
        |
        v
   VkDependencyInfo ==> vkCmdPipelineBarrier2   (录制期写入命令流)
   (srcStageMask, srcAccessMask, dstStageMask, dstAccessMask,
    oldLayout -> newLayout, src/dstQueueFamilyIndex, subresourceRange)
        |
        v
   VkSemaphoreCreateInfo ==> VkSemaphore     (GPU<->GPU，队列/交换链之间)
   VkFenceCreateInfo     ==> VkFence         (GPU->CPU)
        |
        v
   VkSubmitInfo2 ==> vkQueueSubmit2(queue, ..., fence)
     +-- VkSemaphoreSubmitInfo  (wait)   : semaphore + value + stageMask
     +-- VkCommandBufferSubmitInfo      : commandBuffer + deviceMask
     +-- VkSemaphoreSubmitInfo  (signal)
        |
        v
   vkWaitForFences / vkQueueWaitIdle / vkDeviceWaitIdle
```

分支：
- **旧式**：`VkSubmitInfo` + `pWaitDstStageMask`（每信号量一个 stage）；1.3 起推荐 `VkSubmitInfo2`。
- **Timeline semaphore**：`VkSemaphoreTypeCreateInfo`（`TIMELINE`）+ `VkSemaphoreWaitInfo` / `vkWaitSemaphores`，用 64 位值代替 binary 信号。
- **队列族所有权转移**：`srcQueueFamilyIndex != dstQueueFamilyIndex` 的 barrier，配套 `VK_SHARING_MODE_CONCURRENT` 或 acquire/release。
- **Event**：`vkCmdSetEvent2` / `vkCmdWaitEvents2`，用于命令流内部细粒度同步。

屏障要回答的三个问题（写错 90% 出在这里）：

| 问题 | 字段 |
|---|---|
| 之前谁写完了？ | `srcStageMask` + `srcAccessMask` |
| 之后谁要读？ | `dstStageMask` + `dstAccessMask` |
| 图像布局怎么变？ | `oldLayout` -> `newLayout`（必须与 usage 匹配） |

---

### (8) 呈现 · WSI / Surface / Swapchain

```
VkWin32SurfaceCreateInfoKHR ==> VkSurfaceKHR
        |
        v
VkSurfaceCapabilitiesKHR + VkSurfaceFormatKHR[] + VkPresentModeKHR[]
        |
        v
VkSwapchainCreateInfoKHR ==> VkSwapchainKHR
  (minImageCount, imageFormat, imageColorSpace, imageExtent,
   imageUsage, preTransform, compositeAlpha, presentMode, clipped, oldSwapchain)
        |
        +--> vkGetSwapchainImagesKHR --> VkImage[] --> VkImageView[] --> VkFramebuffer
        |
        v
VkAcquireNextImageInfoKHR ==> vkAcquireNextImageKHR --> imageIndex
        |                                    (signal VkSemaphore / VkFence)
        v
VkPresentInfoKHR ==> vkQueuePresentKHR
  (pWaitSemaphores, pSwapchains, pImageIndices, pResults)
```

分支：
- **resize / out-of-date**：`VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` -> 重建 swapchain（`oldSwapchain` 传入，避免闪烁）。
- **present mode**：`FIFO`（必支持，垂直同步）、`MAILBOX`（三缓冲低延迟）、`IMMEDIATE`（撕裂）。
- **HDR**：`VkHdrMetadataEXT` + `VK_EXT_hdr_metadata`。
- **present timing**：`VkPresentIdKHR` + `vkWaitForPresentKHR`（present wait/id）。

---

### (9) 支线 · Extensions

```
VkAccelerationStructureCreateInfoKHR ==> VkAccelerationStructureKHR
        |  (BLAS: 几何；TLAS: 实例)
        v
VkRayTracingPipelineCreateInfoKHR ==> VkPipeline
        |  (pGroups: raygen / miss / hit / callable, SBT)
        v
vkCmdTraceRaysKHR

VkPhysicalDeviceMeshShaderFeaturesEXT ==> vkCmdDrawMeshTasksEXT
VkVideoSessionCreateInfoKHR ==> VkVideoSessionKHR ==> vkCmdDecodeVideoKHR / vkCmdEncodeVideoKHR
```

---

## 4. 资源关系矩阵（谁引用谁 / 谁拥有谁）

| 结构体 / 对象 | 被谁引用 | Owner | 销毁时机 |
|---|---|---|---|
| `VkInstance` | `VkPhysicalDevice`、`VkSurfaceKHR`、`VkDebugUtilsMessengerEXT` | 应用 | 最后 |
| `VkDevice` | 几乎全部设备对象 | `VkPhysicalDevice` | 倒数第二 |
| `VkQueue` | `vkQueueSubmit2`、`vkQueuePresentKHR` | `VkDevice`（不销毁） | — |
| `VkCommandPool` | `VkCommandBufferAllocateInfo` | `VkDevice` | 其下 command buffer 之后 |
| `VkCommandBuffer` | `VkCommandBufferSubmitInfo`、`vkCmdExecuteCommands` | `VkCommandPool` | 随 pool |
| `VkBuffer` / `VkImage` | `VkDescriptorBufferInfo`、`VkBufferImageCopy`、`VkFramebuffer` | `VkDevice` | 所有引用它的 command buffer 之后 |
| `VkDeviceMemory` | `VkBindBufferMemoryInfo` / `VkBindImageMemoryInfo` | `VkDevice` | 解绑后 |
| `VkImageView` | `VkFramebufferCreateInfo`、`VkDescriptorImageInfo`、`VkRenderingAttachmentInfo` | `VkDevice` | 引用它的 framebuffer 之后 |
| `VkSampler` | `VkDescriptorImageInfo::sampler`、`VkDescriptorSetLayoutBinding::pImmutableSamplers` | `VkDevice` | 引用它的 set 之后 |
| `VkDescriptorSetLayout` | `VkPipelineLayoutCreateInfo::pSetLayouts` | `VkDevice` | pipeline layout 之后 |
| `VkDescriptorPool` | `VkDescriptorSetAllocateInfo` | `VkDevice` | 其下 set 之后 |
| `VkDescriptorSet` | `vkCmdBindDescriptorSets` | `VkDescriptorPool` | pool 之后 |
| `VkPipelineLayout` | `VkGraphicsPipelineCreateInfo::layout`、`vkCmdBindDescriptorSets` | `VkDevice` | pipeline 之后 |
| `VkRenderPass` | `VkFramebufferCreateInfo`、`VkGraphicsPipelineCreateInfo` | `VkDevice` | framebuffer / pipeline 之后 |
| `VkFramebuffer` | `VkRenderPassBeginInfo::framebuffer` | `VkDevice` | 录制它的 command buffer 之后 |
| `VkPipeline` | `vkCmdBindPipeline`、`VkPipelineLibraryCreateInfoKHR` | `VkDevice` | 使用它的 command buffer 之后 |
| `VkSwapchainKHR` | `VkPresentInfoKHR`、`VkImageSwapchainCreateInfoKHR` | `VkDevice` | 其下 `VkImage` 之后 |
| `VkSurfaceKHR` | `VkSwapchainCreateInfoKHR::surface` | `VkInstance` | swapchain 之后 |
| `VkSemaphore` / `VkFence` | `VkSubmitInfo2`、`VkPresentInfoKHR` | `VkDevice` | 最后一次等待之后 |

销毁顺序 = 创建顺序的逆序；`vkDeviceWaitIdle` 是兜底，不是设计。

---

## 5. 三条并行线

**5.1 多队列并行**

```
                 +-- graphics queue --> 渲染主通道（render pass / draw）
VkDevice -------- +-- compute  queue --> 异步计算（后处理 / 剔除 / 物理）
                 +-- transfer queue --> DMA 上传（staging buffer -> image）
                          ^
                 semaphore 建立跨队列依赖（VkSemaphoreSubmitInfo::stageMask）
```

**5.2 多线程录制**

```
thread 0: VkCommandPool(0) -> VkCommandBuffer(0, SECONDARY) -.
thread 1: VkCommandPool(1) -> VkCommandBuffer(1, SECONDARY) -+--> primary vkCmdExecuteCommands
thread 2: VkCommandPool(2) -> VkCommandBuffer(2, SECONDARY) -'
```
规则：一个 `VkCommandPool` 同一时刻只能被一个线程使用（外部同步）；command buffer 的录制互不影响。

**5.3 Render Pass 内并行（subpass / tile）**

```
subpass0 (G-buffer) --input attachment--> subpass1 (lighting) --> subpass2 (post)
        |                                        |
        '-------- VkSubpassDependency 定义 stage/access 的可见性 --------'
```
移动端 TBDR：`loadOp = CLEAR/DONT_CARE`、`storeOp = DONT_CARE` 能避免把 tile 写回内存。

---

## 6. 常用布局转换（屏障速查）

| 场景 | oldLayout -> newLayout | src -> dst stage |
|---|---|---|
| 上传后第一次采样 | `UNDEFINED` -> `SHADER_READ_ONLY_OPTIMAL` | `TRANSFER` -> `FRAGMENT_SHADER` |
| 作为颜色附件 | `UNDEFINED` -> `COLOR_ATTACHMENT_OPTIMAL` | `TOP_OF_PIPE` -> `COLOR_ATTACHMENT_OUTPUT` |
| 颜色附件 -> 采样 | `COLOR_ATTACHMENT_OPTIMAL` -> `SHADER_READ_ONLY_OPTIMAL` | `COLOR_ATTACHMENT_OUTPUT` -> `FRAGMENT_SHADER` |
| 深度附件 | `UNDEFINED` -> `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` | `EARLY_FRAGMENT_TESTS` |
| 渲染完 -> 呈现 | `COLOR_ATTACHMENT_OPTIMAL` -> `PRESENT_SRC_KHR` | `COLOR_ATTACHMENT_OUTPUT` -> `BOTTOM_OF_PIPE` |
| 计算写 -> 图形读 | `GENERAL` -> `SHADER_READ_ONLY_OPTIMAL` | `COMPUTE_SHADER` -> `FRAGMENT_SHADER` |

---

## 7. 常见坑（按图定位）

1. **在 `vkCmdBeginRenderPass` 之外调用 `vkCmdDraw`** —— 管线创建时的 `renderPass/subpass` 必须与当前 render pass 实例匹配。
2. **忘了 `initialLayout`** —— `VkImageCreateInfo::initialLayout` 写 `UNDEFINED`，却在首个 barrier 里写 `oldLayout = COLOR_ATTACHMENT_OPTIMAL`，验证层直接报错。
3. **`vkCmdPipelineBarrier2` 的 stageMask 写成 `ALL_COMMANDS`** —— 能跑，但等于放弃同步精度；应精确到 `COLOR_ATTACHMENT_OUTPUT` 等。
4. **每个 swapchain image 共用一个 renderFinishedSem** —— present 还没完成就复用信号量，导致死锁/闪烁；正确做法是 per-image 或 per-frame-in-flight。
5. **command buffer 在 pending 状态被 reset** —— 规范明令禁止；必须等 fence。
6. **secondary command buffer 期待继承状态** —— 除 render pass 实例外不继承任何状态；`VkCommandBufferInheritanceInfo` 只声明兼容性。
7. **`VkFramebuffer` 的 attachment 数量/格式与 `VkRenderPass` 不一致** —— 创建时就要匹配，运行时无法改。
8. **动态渲染下仍给 `renderPass` 传非空句柄** —— 1.3 之后应传 `VK_NULL_HANDLE`，格式走 `VkPipelineRenderingCreateInfo`。

---

## 8. 一页纸记忆法

```
Instance -> PhysicalDevice -> Device -> Queue
                                   |
      +----------------------------+----------------------------+
      |                            |                            |
   Resources                  Descriptors                   Pipeline
   Buffer/Image/View          SetLayout/Pool/Set            Shader/Layout/PSO
      |                            |                            |
      +--------------+-------------+--------------+-------------+
                     |                            |
              Render Target                 Command Buffer
              RenderPass/Framebuffer        vkCmd* recording
                     |                            |
                     +-------------+--------------+
                                   |
                          Sync: Barrier/Semaphore/Fence
                                   |
                          Submit -> Present -> next frame
```
