# intent8 next blueprint: DrawPipelineData

Status: design document, new generation.
This document replaces the previous intent8 framing. It does not assume the
old intent8 code is correct or reusable.

Scope: Vulkan graphics draw pipeline and its storage/command model.
Initialization code, WSI setup, memory allocation and resource upload are
outside this blueprint. Configuration is allowed to be global.

The model must remain physically valid: every configuration must be buildable
into a real Vulkan object, every draw must be recordable into a real command
buffer, and the generated C API list must be the real command sequence.

---

## 1. The one-sentence model

```text
Draw = DrawPipelineData(config/resources) + DrawCall(operation)
Pass = RenderTarget + sequence<Draw/Bind/Barrier>
Frame = sequence<Pass>
```

`DrawPipelineData` is not one giant struct. It is a **view over the data that a
draw pipeline needs**, split into:

```text
PipelineConfig       creation-time configuration
PipelineResource     built Vulkan objects
ShaderProgram        programmable-stage programs and interfaces
DescriptorLayout     descriptor set layout configuration
DescriptorResources  descriptor sets and their resources
RenderTarget         attachments and target configuration
DrawResources        vertex/index/indirect/push resources
```

The C API list is a direct projection of the command sequence, not a separate
registry report.

---

## 2. Why start from the Vulkan pipeline

The previous approach started from passes and commands, then tried to add
"data" afterwards. That produced a generic command container but did not
explain a draw.

A Vulkan graphics draw is a pipeline execution:

```text
Input Assembly
  -> Vertex Shader
  -> [Tessellation Control / Evaluation]
  -> [Geometry Shader]
  -> Vertex Post-Processing
  -> Rasterization
  -> Early Fragment Tests
  -> Fragment Shader
  -> Late Fragment Tests
  -> Color Blending
  -> Attachment Writes
```

Some stages are programmable, some are fixed-function:
- Programmable: vertex, tessellation, geometry, fragment (and mesh/task).
- Fixed: input assembly, vertex post-processing, rasterization, depth/stencil,
  blending, multisample, attachment output.

A draw therefore needs both:
1. programmable-stage code and resources;
2. fixed-function pipeline configuration;
3. render-target resources/config;
4. draw resources and a draw command.

This is the primary model. Passes and frames are grouping layers above it.

Official background:

- [Vulkan 1.4 specification, 10. Pipelines](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#pipelines)
- [10.4 Graphics Pipelines](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#pipelines-graphics)
- [27. Drawing Commands](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#drawing)
- [28. Fixed-Function Vertex Processing](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#fxvertex)
- [29. Tessellation](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#tessellation)
- [30. Geometry Shading](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#geometry)
- [33. Fixed-Function Vertex Post-Processing](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#vertexpostproc)
- [34. Rasterization](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#primsrast)
- [35. Fragment Operations](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#fragops)
- [36. The Framebuffer](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#framebuffer)

---

## 3. D and O

### 3.1 D is configuration + resources

D is not `DataRef`, not raw bytes, not a lifetime tag.

```text
D = ConfigStore + ResourceTable
```

#### ConfigStore

Creation-time or state-time descriptions:

- `ImageConfig` / `BufferConfig` / `ImageViewConfig` / `SamplerConfig`
- `ShaderModuleConfig` / `ShaderInterfaceConfig`
- `DescriptorSetLayoutConfig` / `DescriptorBindingConfig`
- `PipelineLayoutConfig` / `PushConstantRangeConfig`
- `GraphicsPipelineConfig` (programmable + fixed stages)
- `RenderPassConfig` / `FramebufferConfig`
- `RenderTargetConfig`
- `DynamicStateConfig`
- `FeatureConfig` / `FormatConfig`

#### ResourceTable

Real Vulkan objects plus metadata:

- `VkImage` / `VkBuffer` / `VkImageView` / `VkSampler`
- `VkShaderModule`
- `VkDescriptorSetLayout` / `VkDescriptorPool` / `VkDescriptorSet`
- `VkPipelineLayout` / `VkPipeline`
- `VkRenderPass` / `VkFramebuffer`
- `VkCommandBuffer`
- `VkQueryPool` / `VkEvent` / `VkSemaphore` / `VkFence`
- `VkQueue` / `VkSwapchainKHR`

A resource has:
- handle;
- type;
- creation config reference;
- current layout/access/queue-ownership state;
- lifetime/owner.

### 3.2 O is operation

```text
O = bind / push / draw / dispatch / barrier / copy / ...
```

For this blueprint, the primary O is the draw path:

```text
vkCmdBindPipeline
vkCmdBindDescriptorSets
vkCmdPushConstants
vkCmdBindIndexBuffer
vkCmdBindVertexBuffers       (if not using BDA)
vkCmdDraw / vkCmdDrawIndexed / vkCmdDraw*Indirect
vkCmdPipelineBarrier2
```

A `DrawCall` is an operation group:

```text
DrawCall = bind pipeline
         + bind descriptors
         + push constants
         + bind index/vertex/indirect
         + draw
```

### 3.3 C API list

`c_api_list` is a **view** of the recorded operations:

```text
for each Pass
  for each Op
    print op.vk_name and op.args
```

It must not be generated from a separate metadata table after the fact.
If the emitter and `c_api_list` read different structures, the design is wrong.

---

## 4. DrawPipelineData: the central model

### 4.1 PipelineConfig

This is the creation-time configuration of one graphics pipeline.

#### Programmable stage configuration

- vertex shader module + entry point
- fragment shader module + entry point
- optional tessellation control/evaluation, geometry, mesh/task
- stage specialization constants
- stage interfaces (`location` inputs/outputs)
- shader stage flags
- descriptor usage per stage
- push constant ranges

#### Fixed-function configuration

- vertex input / input assembly
  - topology
  - primitive restart
  - vertex bindings/attributes, or BDA-only model
- viewport/scissor
  - static or dynamic
- rasterization
  - polygon mode
  - cull mode
  - front face
  - depth bias
  - line width
- multisample
  - rasterization samples
  - sample shading
  - alpha-to-coverage
- depth/stencil
  - test/write enable
  - compare op
  - stencil front/back state
  - depth bounds
- color blend
  - attachment count
  - blend enable/factors/ops
  - write masks
  - logic op
- dynamic state list

#### Pipeline interface

- `VkPipelineLayout` configuration
- descriptor set layout references
- push constant ranges
- render pass / dynamic rendering interface:
  - color formats
  - depth/stencil formats
  - sample count
  - view mask
- pipeline cache / flags

### 4.2 PipelineResource

Real Vulkan objects built from the config:

```cpp
struct PipelineResource {
    VkShaderModule vertexModule;
    VkShaderModule fragmentModule;
    VkPipelineLayout layout;
    VkPipeline pipeline;
};
```

`PipelineConfig` is data.  
`PipelineResource` is the built object.  
Do not mix them into one opaque handle.

### 4.3 Descriptor layout/resources

```cpp
struct DescriptorSetLayoutConfig {
    std::span<const VkDescriptorSetLayoutBinding> bindings;
    VkDescriptorSetLayoutCreateFlags flags;
};

struct DescriptorSetResources {
    VkDescriptorSet set;
    // buffers/images/samplers referenced by the set
};
```

Rules:
- binding numbers/types/stageFlags must match shader usage;
- descriptor set layout must be in pipeline layout;
- descriptor set must be allocated from that layout;
- update/allocate happens outside the command stream;
- bind before draw.

### 4.4 RenderTarget

```cpp
struct RenderTargetConfig {
    VkRenderingInfo / VkRenderPassBeginInfo target data;
    color formats;
    depth/stencil formats;
    samples;
    load/store;
    clear values (owned);
    render area;
};
```

`RenderTarget` references resources (image views, framebuffer, render pass)
and owns configuration values that must outlive emission.

### 4.5 DrawResources

```cpp
struct DrawResources {
    VkDescriptorSet descriptorSet;
    VkBuffer indexBuffer;
    VkDeviceSize indexOffset;
    VkIndexType indexType;

    VkBuffer vertexBuffer;      // optional, if not BDA
    VkDeviceSize vertexOffset;

    VkBuffer indirectBuffer;    // optional
    VkDeviceSize indirectOffset;
    std::uint32_t drawCount;
    std::uint32_t stride;

    PushConstantsData pushConstants;
};
```

BDA addresses, if used, are part of push constants or a resource block; they
must point to real buffers.

### 4.6 DrawPipelineData as a view

```cpp
struct DrawPipelineData {
    PipelineConfig config;
    PipelineResource resource;
    DescriptorSetLayoutConfig descriptorLayout;
    RenderTargetConfig target;
    FeatureSet requiredFeatures;
};
```

This is a **logical view**, not a requirement to physically copy all fields.

A draw call:

```cpp
struct DrawCall {
    DrawPipelineData *pipeline;
    DrawResources *resources;
    DrawArgs args;
};
```

The emitter expands a `DrawCall` into:

```text
bind pipeline
bind descriptor set(s)
push constants
bind index/vertex/indirect
draw
```

and `c_api_list` reads the same expansion.

---

## 5. Global configuration

Initialization code is out of scope, so configuration can be global. That is
allowed, provided it remains real and immutable.

```cpp
struct GlobalConfig {
    DeviceCapabilities capabilities; // version/extensions/features/limits/formats

    ShaderLibrary shaders;           // loaded SPIR-V modules
    DescriptorLayoutTable descriptorSetLayouts;
    PipelineLayoutTable pipelineLayouts;
    PipelineConfigTable pipelines;
    RenderTargetTable targets;
    SamplerTable samplers;
};

struct GlobalResources {
    ResourceTable resources;         // stable IDs -> real Vulkan handles
    VkDevice device;
    VkQueue queue;
};
```

Rules:
- config IDs are stable;
- resources are stable handles;
- config can be global/immutable;
- global config must not pretend a pipeline exists before it is built;
- pipeline building happens in initialization/owner code outside the draw
  blueprint;
- all features used by a pipeline must be enabled at device creation;
- all formats/samples used by a target must be supported.

"Global" means **lifetime and addressability**, not "skip Vulkan validity".

---

## 6. Draw flow

### 6.1 Initialization (outside blueprint)

1. create device, enable features/extensions;
2. create shader modules;
3. create descriptor set layouts;
4. create pipeline layouts;
5. create pipelines using `GraphicsPipelineConfig`;
6. create images/buffers/views/samplers;
7. allocate/update descriptor sets;
8. create render targets/framebuffers;
9. create command buffers.

### 6.2 Recording (inside blueprint)

For each draw:

```text
begin render target/pass
bind pipeline
bind descriptor sets
push constants
bind index/vertex/indirect
draw
end pass
```

Plus barriers/layout transitions where required.

### 6.3 C API list

The list must be identical in order to the emitted calls:

```text
vkCmdBeginRendering / vkCmdBeginRenderPass2
vkCmdBindPipeline
vkCmdBindDescriptorSets
vkCmdPushConstants
vkCmdBindIndexBuffer
vkCmdDrawIndexedIndirect
vkCmdEndRendering / vkCmdEndRenderPass2
```

---

## 7. Reality constraints

These are invariants, not suggestions.

### 7.1 Pipeline creation

- shader modules must contain valid SPIR-V;
- entry points must exist;
- pipeline layout must include all descriptor set layouts and push ranges;
- descriptor bindings must match shader usage;
- render target formats must match pipeline rendering info;
- sample count must match target sample count;
- dynamic state must be declared in the pipeline;
- feature bits used by shaders must be enabled;
- vertex input or BDA layout must match shader inputs;
- push constant data must fit the declared range.

### 7.2 Draw recording

- pipeline must be bound before draw;
- descriptor set layout must match pipeline layout;
- descriptor set must be compatible with bound layout;
- index/vertex/indirect buffers must be valid and alive;
- push constants must match declared ranges/stages;
- target image layouts must be correct;
- barriers must cover producer/consumer access;
- render area must fit attachments;
- secondary command buffers must inherit the correct target.

### 7.3 No fake data

- `DataRef` is not D;
- raw bytes are not a resource;
- a string name is not a command;
- a descriptor set is not just a handle without layout/resource metadata;
- a pipeline is not just `VkPipeline` without its config/interface;
- C API list must not claim calls that were not emitted.

---

## 8. Passes and frames

Passes are grouping, not the primary model.

```cpp
struct Pass {
    RenderTarget target;
    std::span<const Op> ops;
};

struct Frame {
    std::span<const Pass> passes;
};
```

A pass contains:
- target (D);
- draw/bind/barrier ops (O).

A frame contains passes.  
`c_api_list` walks passes and ops.

Classic render pass/subpass and dynamic rendering are **two target modes**:
- dynamic rendering: `vkCmdBeginRendering` / `vkCmdEndRendering`
- classic: `vkCmdBeginRenderPass2` / `vkCmdNextSubpass2` / `vkCmdEndRenderPass2`

The draw pipeline config must be compatible with the selected target:
- dynamic pipeline: rendering info formats/depth/samples;
- classic pipeline: render pass/subpass index and compatible attachments.

---

## 9. What is out of scope

This blueprint does not attempt to model:

- initialization code;
- WSI/swapchain details;
- memory allocation/upload;
- asset loading;
- HDR/color management policy;
- damage/dirty region/layer cache policy;
- video/ray tracing/acceleration structures;
- full render graph;
- multi-queue scheduling.

Those are separate layers. They can be added later without changing the draw
pipeline model, provided their resources/config are represented as D.

---

## 10. Migration path

### Phase 1: one dod18 pipeline

Use `test_dod18` as the reference:

- one VS/FS pair;
- one descriptor set layout:
  - UBO;
  - sampled images;
  - samplers;
  - storage buffer;
- push constants with BDA addresses;
- one BDA vertex/instance/indirect path;
- two color attachments (color + pick);
- depth attachment;
- one indirect draw.

Acceptance:
- `DrawPipelineData` represents this pipeline exactly;
- `c_api_list` matches the actual bind/push/draw sequence;
- no VUID.

### Phase 2: second pipeline and target

- opaque/transparent or additive/multiply;
- same target, different blend/pipeline;
- swap pipeline between draws;
- verify pipeline/target compatibility.

### Phase 3: classic render pass

- `RenderPassConfig` / `FramebufferConfig`;
- two subpasses;
- MSAA resolve/depth/input attachment;
- pipeline subpass compatibility.

### Phase 4: UI consumer

- tile/scene data -> DrawCall sequence;
- descriptor/texture/font resources;
- offscreen/stencil;
- ability regression.

Do not start with the 36-tile ability. Start with one dod18 draw.

---

## 11. Acceptance criteria

### Functional

- [ ] `DrawPipelineData` can represent dod18's pipeline.
- [ ] a draw call emits bind/push/draw in the correct order.
- [ ] `c_api_list` equals the emitted sequence.
- [ ] no VUID on the test path.

### Data

- [ ] pipeline config is distinct from pipeline resource.
- [ ] descriptor layout/config is distinct from descriptor set resource.
- [ ] target config is distinct from target image resources.
- [ ] resources have stable IDs and real handles.
- [ ] config can be global/immutable.

### Validity

- [ ] pipeline layout matches descriptors/push constants.
- [ ] render target formats/samples match pipeline.
- [ ] dynamic states are declared.
- [ ] features are enabled.
- [ ] barriers/layout transitions exist where needed.

### Extensibility

- [ ] add a second pipeline without changing draw/pass core.
- [ ] add a second target without changing the draw core.
- [ ] add classic render pass without changing the draw pipeline config.
- [ ] raw is only an escape, not the core.

---

## 12. Open questions

These are intentionally unresolved and should be tested, not assumed:

1. How much shader interface reflection is required?
2. Should descriptor set layout be generated from SPIR-V reflection or authored?
3. How are pipeline caches/config variants keyed?
4. How much dynamic state should be used versus baked state?
5. How are vertex input and BDA layouts represented together?
6. How are descriptor sets allocated/updated across frames?
7. How are resource layouts/queue ownership tracked?
8. How are classic render pass and dynamic rendering unified without losing
   pipeline compatibility?
9. How are push constants represented for large/indirect data?
10. What is the minimum raw escape surface?

None of these may be answered by adding an opaque `DataRef`.

---

## 13. Official references

Vulkan specification (local copy):

- [10. Pipelines](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#pipelines)
- [10.4 Graphics Pipelines](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#pipelines-graphics)
- [10.13 Dynamic State](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#pipelines-dynamic-state)
- [9. Shaders](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#shaders)
- [19. Shader Interfaces](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#interfaces)
- [27. Drawing Commands](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#drawing)
- [28. Fixed-Function Vertex Processing](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#fxvertex)
- [34. Rasterization](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#primsrast)
- [35. Fragment Operations](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#fragops)
- [36. Framebuffer](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#framebuffer)
- [8. Render Pass](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#renderpass)
- [6. Command Buffers](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#commandbuffers)
- [7. Synchronization and Cache Control](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#synchronization)
- [12. Resource Creation](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#resources)
- [14. Samplers](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#samplers)
- [15. Resource Descriptors](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#descriptors)
- [17. Descriptor Sets](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#descriptors-sets)
- [56. Features](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#features)
- [57. Limits](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#limits)
- [58. Formats](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#formats)

LunarG Vulkan Guide:

- [What Vulkan Can Do](https://vulkan.lunarg.com/doc/view/1.4.341.0/windows/antora/guide/latest/what_vulkan_can_do.html)
- [Pipeline Dynamic State](https://vulkan.lunarg.com/doc/view/1.4.341.0/windows/antora/guide/latest/dynamic_state.html)
- [Synchronization](https://vulkan.lunarg.com/doc/view/1.4.341.0/windows/antora/guide/latest/synchronization.html)
- [Mapping Data to Shaders](https://vulkan.lunarg.com/doc/view/1.4.341.0/windows/antora/guide/latest/mapping_data_to_shaders.html)
- [Vertex Input Data Processing](https://vulkan.lunarg.com/doc/view/1.4.341.0/windows/antora/guide/latest/vertex_input_data_processing.html)
- [Descriptor Buffer](https://vulkan.lunarg.com/doc/view/1.4.341.0/windows/antora/guide/latest/descriptor_buffer.html)
- [Enabling Extensions](https://vulkan.lunarg.com/doc/view/1.4.341.0/windows/antora/guide/latest/enabling_extensions.html)
- [Querying Properties, Extensions, Features, Limits, and Formats](https://vulkan.lunarg.com/doc/view/1.4.341.0/windows/antora/guide/latest/querying_extensions_features.html)

Official headers/bindings:

- [Vulkan-Headers / vk.xml](https://github.com/KhronosGroup/Vulkan-Headers)
- [Vulkan-Hpp](https://github.com/KhronosGroup/Vulkan-Hpp)
- [Vulkan Registry](https://registry.khronos.org/vulkan/)

Note: the local `vkspec.html` under the 1.4.341.0 path reports a 1.4.368
title in its header. Check the actual header version when relying on
version-specific behavior.

---

## 14. Final guidance

- Start from the draw pipeline, not from pass containers.
- D is configuration + resources.
- A pipeline is config + built resource; do not collapse them.
- A descriptor set is layout + set + referenced resources; not just a handle.
- A render target is config + attachment resources; not just a view.
- O is bind/push/draw/barrier; C API list is its projection.
- Config may be global; initialization code is outside this blueprint.
- Do not use `DataRef` or raw bytes as the core data model.
- Do not claim "all Vulkan"; prove one dod18 pipeline first, then extend.
