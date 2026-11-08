# INTENT7_HANDOFF 鈥?鏁版嵁 + 鎿嶄綔 + 閫忔槑 emitter

> 2026-11-08 閲嶆瀯鍚庣殑浜ゆ帴璇存槑銆?> 杩欎唤鏂囨。鎻忚堪鐨勬槸 **涓夎酱妯″瀷** 鐨勫疄鐜帮紝鑰屼笉鏄棫鐨勨€淧assCompiler 鎶?Cmd union 缈绘垚 vkCmd*鈥濄€?
## 1. 鐩爣

`test_intent7` 瑕侀獙璇侊細

> UIWidget 鐨勬剰鍥撅紝缁忚繃鏄惧紡鐨勬暟鎹褰曞拰鎿嶄綔灞曞紑锛屾渶缁堢敓鎴?> **鍜屾墜鍐欎竴鏍锋樉寮忋€佸彲璋冩帶鐨?Vulkan C 鍛戒护鍒楄〃**锛屽苟涓斾互
> `[beginRendering ... endRendering]` 涓?pass 杈圭晫銆?
閾捐矾锛?
```
UIWidget
  -> CommandBuilder / DrawSegment锛圲I 鎰忓浘锛?  -> shader_data::emit锛堟剰鍥?-> 鎿嶄綔锛歜ind/set/draw锛?  -> FrameBuilder锛堟搷浣?-> 绾挎€?OpStream锛?  -> VkEmitter锛圤p -> 鏄惧紡 vkCmd*锛?  -> Vulkan C command list
```

## 1.1 鏍规湰搴曞眰锛歚[pass ... pass] => [Vulkan C API list]`

鎶借薄灞備笉鏄粓鐐癸紱姣忎竴灞傛渶缁堥兘蹇呴』鍙樻垚鏄惧紡銆佹湁搴忕殑 `vkCmd*` 鍒楄〃銆?
- `Opcode -> c_api_name()`锛氭瘡涓?Opcode 蹇呴』鏄犲皠鍒颁竴涓畼鏂?`vkCmd*`锛屾病鏈?abstract-only op銆?- `OpStream -> c_api_list()`锛氭寜褰曞埗椤哄簭杈撳嚭 C API 鍒楄〃銆?- `c_api_coverage_ok()`锛氭鏌ユ墍鏈?Opcode 閮芥湁 C API 鍚嶇О銆?- `VkEmitter` 鐨?`switch` 鏈?`default: assert(false)`锛氭湭鏄犲皠鐨?Opcode 涓嶈兘鍦?debug 涓嬮潤榛橀€氳繃銆?- 杩愯鏃舵棩蹇楋細`[pass-c-api] opcodes=37 calls=54`锛岄殢鍚庨€愯鍒楀嚭 `#N vkCmd*`銆?
褰撳墠涓€甯х殑灏鹃儴绀轰緥锛?
```text
#45 vkCmdFillBuffer
#46 vkCmdPipelineBarrier2
#47 vkCmdCopyBuffer
#48 vkCmdFillBuffer
#49 vkCmdCopyImageToBuffer
#50 vkCmdPipelineBarrier2
```

杩欐潯鍒楄〃灏辨槸鈥滄牴鏈簳灞傗€濈殑鐩存帴璇佹嵁锛歱ass 鐨勬暟鎹?鎿嶄綔鏈€缁堣惤鍒版樉寮?C API銆?
### 1.2 绯荤粺灞傚櫒瀹橈紙鏈疆琛ワ級

| 鍣ㄥ畼 | 褰撳墠瀹炵幇 | 鐘舵€?|
|---|---|---|
| Pass domain | `PassKind { Graphics, Compute, Transfer }`锛沗beginComputePass/beginTransferPass` | transfer pass + compute pass 閮藉凡鎺ュ叆锛沜ompute 鐢?push constants锛屾殏鏃?compute descriptor set 鐢ㄤ緥 |
| Queue/submission | `FrameScheduler` + `QueueRole`/`QueueBinding`锛沘cquire/submit/present | 宸叉帴鍏ヤ富寰幆锛涘綋鍓嶅彧鏈?graphics+present 鍗曢槦鍒?|
| Resource state | `ResourceStateTracker` + `BarrierPlanner`锛涙樉寮?`VkImageMemoryBarrier2` | 宸叉帴鍏?pick/present 涓や釜 transition锛涗粛鏄?per-transition seed锛屼笉鏄叏甯?registry |

### 1.3 鏁版嵁鑼冨洿锛堟湰杞ˉ鍏級

鏁版嵁鍒嗙被淇濇寔鏈夐檺锛屼絾姣忕被鏀规垚瀹屾暣 Vulkan 鏁版嵁瀹瑰櫒锛屾柊澧炲懡浠ゅ彧鍔?opcode/payload/emitter锛屼笉鍐嶆敼 `PassData`锛?
| 鍒嗙被 | 瀹屾暣鑼冨洿 |
|---|---|
| Descriptor | 澶?`VkDescriptorSet`銆乫irstSet銆乨ynamic offsets銆乨escriptor buffer bindings/offsets銆乺esource/sampler heap銆乸ush descriptor writes |
| VertexInput | 澶?vertex binding銆乷ffsets銆乻izes銆乻trides銆乫irstBinding |
| DynamicState | `StateOwnershipMap` 浠ョ湡瀹?`VkDynamicState` 涓?key锛沗DynamicStateValue` 浠?bytes 瀛樹换鎰忕姸鎬佸€硷紝涓嶅啀鎸夌姸鎬佸姞瀛楁 |
| Sync | memory/buffer/image barrier 鏁扮粍銆乨ependencyFlags銆乪vent銆乧onditional銆乹uery/timestamp銆乼imeline semaphore銆乫ence |
| Inheritance | 瀹屾暣 `VkCommandBufferInheritanceInfo` + rendering/conditional/heap/viewport-scissor + pNext锛坈aller-owned锛?|
| Transfer | `VkBufferCopy2` / `VkImageCopy2` / `VkBufferImageCopy2` / `VkImageBlit2` / `VkImageResolve2` |
| Feature | `VkPhysicalDeviceFeatures2` + Vulkan11/12/13/14 features + properties/limits + extension names + raw extension feature |
| pNext | `const void* pNext`锛沜aller-owned chain锛屼笉浜х敓 per-pass 鍫嗗垎閰?|
| Queue/Submission | queue family properties銆乣VkSubmitInfo2` 鐨?semaphore/command buffer submit info銆乤cquire銆乸resent銆乧ommand-buffer inheritance |
| ResourceState | image/buffer state + queue family ownership |
| Classic | `VkRenderPass` / framebuffer / clear values / subpass contents |
| Other | clear attachments銆乹uery results銆乨ebug label銆乼ransform/raw command data |

`RawCommandData { name, function, arguments }` 鏄渶鍚庣殑 escape hatch锛? 
vendor/future 鍛戒护鍙互鐩存帴璧?raw data锛屼笉闇€瑕佹墿 `PassData`锛涘懡浠ゆ槧灏勪粛鏄蒋鐨勩€?
### 1.4 鐑矾寰勫垎閰嶇瓥鐣?
- `PassData` 涓嶅惈 `std::vector` / `std::string` / `shared_ptr`锛涘彉閲忔暟鎹敤 `std::span` 鎸囧悜璋冪敤鏂瑰瓨鍌ㄣ€?- `PassData` 鍙繚鐣?core state锛歍arget / Pipeline / Descriptor / PushConstants / VertexInput / DynamicState / Draw / Sync / Inheritance + `FeatureSet*`銆俆ransfer / classic / clear / query / debug / raw / frame data 鏀惧湪鍚勮嚜 struct 鍜?OpPayload锛屼笉杩涘叆 per-pass cache銆?- `StateOwnershipMap`锛?6 椤?inline銆?- `DynamicState`锛?6 椤?脳 128B inline銆?- `PushConstants`锛?56B inline銆?- `BarrierPayload`锛? 涓?memory/buffer/image barrier inline锛屽父瑙?1 涓?barrier 闆跺爢鍒嗛厤銆?- `CopyImageToBuffer` / `CopyBuffer` / `ExecuteCommands`锛? 椤?inline锛屼笉鍐嶆瘡甯у垎閰?vector銆?- `FeatureSet`锛歚PassData` 鍙瓨鎸囬拡锛屼笉鍐嶆瘡娆?pass 鎷疯礉瀹屾暣 features/properties銆?- `OpStream`锛欶rameBuilder reserve 64锛孲econdaryBuilder reserve 16銆?- legacy `PassRecorder` / `FrameRecorder` 宸插垹闄わ紱`pass_stream.hpp` 鍙繚鐣?`pass_boundary_barrier()`銆?
### 1.5 鏈€缁?review锛歵est_ui_ability2 杩佺Щ

`ui_shader_device` / `ui_backend` 鐢ㄥ埌鐨?pass 绾ц兘鍔涢€愰」鏍稿锛?
| 鑳藉姏 | 鍛戒护 | 鐘舵€?|
|---|---|---|
| begin/end rendering | `vkCmdBeginRendering` / `vkCmdEndRendering` | typed |
| pipeline | `vkCmdBindPipeline` | typed |
| descriptor set | `vkCmdBindDescriptorSets` | typed |
| index buffer | `vkCmdBindIndexBuffer` | typed |
| viewport / scissor | `vkCmdSetViewport` / `vkCmdSetScissor` | typed |
| stencil reference | `vkCmdSetStencilReference` | typed锛堟柊澧烇級 |
| push constants | `vkCmdPushConstants` | typed |
| indirect draw | `vkCmdDrawIndexedIndirect` | typed |
| mip blit | `vkCmdBlitImage` | typed锛堟柊澧烇級 |
| barrier | `vkCmdPipelineBarrier2` | typed |
| multi-color / depth / resolve | `PassTarget` + `VkRenderingAttachmentInfo` | typed |

vkspec 鏍稿缁撴灉锛?
- `vkCmdSetStencilReference` / `vkCmdSetViewport` / `vkCmdSetScissor` / `vkCmdBindPipeline` / `vkCmdBindDescriptorSets` / `vkCmdBindIndexBuffer` / `vkCmdPushConstants` 鐨?Render Pass Scope 鏄?**Both**锛泇alidator 宸插厑璁?pass 澶栬皟鐢ㄣ€?- `vkCmdSetStencilReference` 鍙湁鍦?pipeline 鐨?`VK_DYNAMIC_STATE_STENCIL_REFERENCE` 鏄?dynamic 鏃舵墠鏈夋晥锛泇alidator 鐢?`StateOwnershipMap` 妫€鏌ャ€?- `vkCmdBlitImage` 鏄?Outside锛泇alidator 涓嶅湪 graphics pass 鍐呭厑璁革紝logical transfer pass 鍏佽銆?- `vkCmdExecuteCommands` 鍦?dynamic rendering 鍐呰姹?`VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT`锛泇alidator 宸叉鏌ャ€?- `c_api_coverage_ok()` 瑕嗙洊鍏ㄩ儴 Opcode锛沗VkEmitter` 鏈槧灏?opcode 浼?assert銆?
缁撹锛歚test_ui_ability2` 鐨?pass 褰曞埗鑳藉姏鍙互杩佺Щ鍒版湰妗嗘灦锛涘皻鏈疄闄呮敼鍐?`test_ui_ability2.cpp` 婧愮爜銆?
### 1.6 Widget 绾ц縼绉昏惤鍦?
`test_intent7_ability_test.hpp` 鐨?`ability_test_scene` 宸叉浛浠?`pass_test_scene` 浣滀负 test_intent7 褰撳墠鍦烘櫙锛?
- pass 0 base锛氳儗鏅?+ 18 涓?card frame + composed frame銆?- pass 1 shape/path锛?1 涓?shape/path tile銆?- pass 2 texture/effect锛? 涓?texture/effect tile + composed UI锛涜 pass 鐢?secondary command buffer 鎵ц銆?- pass 3 text overlay锛歁SDF glyph锛沗LOAD/DONT_CARE`銆?- `CommandBuilder::setPipeline` 鍒囨崲 opaque / alpha / add / multiply 鍥涚 pipeline銆?- `CommandBuilder::setScissor` 鍋?per-tile clip锛堥€?segment锛夛紱`pushScissor/popScissor` 鍋氬祵濂?clip 浜ら泦銆?- `ui_dao` 浣跨敤 7 涓?program锛歋hape / PathFill / PathStroke / Texture / Blur / ColorMatrix / Noise銆?- 鐗规€ц鐩栵細linear/radial/conic/per-corner/ring/arc銆乼riangle+AA/star/dash/rotate/skew銆乶ine-slice/masked/mipmap銆乮nner shadow/blur/sepia/noise/text銆?- 杩愯鏃ュ織锛歚[ability-test] passes=4 glyphs=427`锛涚 3 涓?effect pass 鐢?secondary command buffer 鎵ц锛坄sources[3]`锛夛紝`vkCmdExecuteCommands` 鍑虹幇鍦?dump銆?
## 2. 涓夎酱妯″瀷

- **绾靛悜锛氭暟鎹祦**锛堝畼鏂?Pipelines 绔犵殑 block diagram锛?  `Draw -> IA -> VS -> Tess -> GS -> VertexPost -> Raster -> EarlyFrag -> FS -> LateFrag -> Blend -> Attachments`
  璧勬簮锛歏ertex/Index/Indirect Buffer銆丏escriptor Sets/Buffers/Heaps銆丳ush Constants銆乁niform/Storage Buffer銆丼ampled/Storage Image銆?- **妯悜锛氬懡浠よ鑹?*锛堝畼鏂?Fundamentals 绔犵殑 Command Type锛?  `Control / State / Action / Synchronization / Indirection`銆?- **娣卞害锛氱敓鍛藉懆鏈?+ Scope**
  Device / Frame / Pass / CommandBuffer / Draw / Sync / Inheritance / Feature锛?  Inside / Outside / Both锛汸rimary / Secondary锛泀ueue锛泇ideo銆?
## 3. 鏂囦欢鑱岃矗

| 鏂囦欢 | 鑱岃矗 |
|---|---|
| `test_intent7_pass_data.hpp` | **鏁版嵁 schema**锛歚PassTarget`銆乣PipelineBinding`銆乣DescriptorBinding`銆乣PushConstants`銆乣VertexInputBinding`銆乣DynamicState`銆乣DrawArgs`銆乣SyncState`銆乣Inheritance`銆乣FeatureSet`銆乣PassData`锛涗互鍙?`PassDesc = PassTarget` 鍏煎鍒悕銆乣query_features()`銆?|
| `test_intent7_pass_opcode.hpp` | **鎿嶄綔灞?*锛歚Opcode`銆乣OpKind`銆乣Scope`銆乣Lifecycle`銆乣Op`銆乣OpStream`銆乣dump()`锛沗c_api_name()` / `c_api_list()` / `c_api_coverage_ok()` 鎶?`[pass...pass]` 鐩存帴鏄犲皠鍒?`[vkCmd*]` 鍒楄〃銆?|
| `test_intent7_pass_emitter.hpp` | **閫忔槑 lowering**锛歚VkEmitter::emit(Op)`锛屾瘡涓?opcode 1:1 鏄犲皠鍒版樉寮?`vkCmd*`銆?|
| `test_intent7_pass_validator.hpp` | **澧為噺鏍￠獙**锛歴cope / lifecycle / basic compatibility锛涗笉鍋氬叏鐘舵€佹爲銆?|
| `test_intent7_pass_builder.hpp` | **FrameBuilder**锛歚beginPass/state/action/sync/endPass` -> 鏇存柊 `PassData` -> 杩藉姞 `OpStream` -> 鏍￠獙 -> emit锛涘彲閫夌敤 `deferEmit` + `emitAll()` 鍋氱函 build-then-emit銆?*StreamRecorder**锛氭棤鏍￠獙鐨?secondary 鐘舵€?鍔ㄤ綔褰曞埗鍣紱**SecondaryBuilder**锛氬叡浜?`PassData`/`PassValidator` 鐨?secondary 褰曞埗鍣ㄣ€?|
| `test_intent7_pass_compiler.hpp` | `recordPasses()`锛氭寜 `PassSource[]` 椤哄簭 `beginPass -> emit -> endPass`锛屽苟鍦?pass 涔嬮棿鎻?`pass_boundary_barrier()`銆?|
| `test_intent7_frame_scheduler.hpp` | **绯荤粺灞?*锛歚QueueRole` / `QueueBinding` / `FrameScheduler`锛歛cquire (`vkAcquireNextImageKHR`)銆乻ubmit (`vkQueueSubmit`)銆乸resent (`vkQueuePresentKHR`)锛泀ueue role 寤烘ā銆?|
| `test_intent7_resource_state.hpp` | **绯荤粺灞?*锛歚ResourceStateTracker` (layout/access/stage) + `BarrierPlanner` (鏄惧紡 `VkImageMemoryBarrier2` -> `FrameBuilder::barrier`)锛涗笉闅愯棌鍚屾銆?|
| `test_intent7_pass_abi.hpp` | `PipelineState` / `Pass` / `PassManager` / `PassSource`锛氭瘡甯?extra pass 鐨勬暟鎹笌 GPU buffer銆?|
| `test_intent7_frame_context.hpp` | `ShaderBuffers` + `CommandBuilder` + `shader_data::emit()`锛歎I 鎰忓浘 -> 鎿嶄綔銆?|
| `test_intent7_pass_tests.hpp` | 鍙鍖栫敤渚嬶細main 鑳屾櫙 + 3 涓?card pass + 1 涓?overlay pass銆?|
| `test_intent7_ability_test.hpp` | **test_ui_ability2 鐨?Widget 绾ц縼绉?*锛? 涓?program銆?8 涓壒鎬?tile銆乧lip/layer/blend/mipmap/text锛涢€氳繃 `PassManager + FrameBuilder` 璧板畬鏁?`[pass...pass]`銆?|
| `shaders/test_intent7.comp` | 鏈€灏?compute shader锛歱ush constants + 绌?body锛岀敤浜?`beginComputePass + BindPipeline(COMPUTE) + Dispatch`銆?|
| `test_intent7.cpp` | 涓?pass target銆丗rameBuilder銆乸ass sources銆乨ump銆乻ubmit銆?|

## 4. 鏁版嵁 schema

`PassData` 鏄懡浠ょ紦鍐插尯鐘舵€佺殑璁板綍锛?
```cpp
PassData
鈹溾攢 target          PassTarget锛圴kRenderingInfo + color[N]/depth/stencil/resolve/load-store/clear锛?鈹溾攢 pipeline        PipelineBinding锛圴kPipeline + VkPipelineLayout + rendering formats + state ownership锛?鈹溾攢 descriptors     DescriptorBinding锛坙ayout + set锛?鈹溾攢 pushConstants   PushConstants锛坙ayout/stages/offset/size/data锛?鈹溾攢 vertexInput     VertexInputBinding锛坕ndex buffer/offset/type锛?鈹溾攢 dynamicState    DynamicState锛坴iewport/scissor/... + valid[]锛?鈹溾攢 draw            DrawArgs锛坘ind + direct/indirect operands锛?鈹溾攢 sync            SyncState锛坆oundary barrier锛?鈹溾攢 inheritance     Inheritance锛坰econdary rendering info锛?鈹溾攢 features        FeatureSet锛坒eature gates锛?鈹斺攢 passActive      bool
```

鐢熷懡鍛ㄦ湡锛?
| 鏁版嵁 | 鐢熷懡鍛ㄦ湡 | 璇存槑 |
|---|---|---|
| `PassTarget` | pass | 鍙湪 `beginRendering/endRendering` 涔嬮棿鏈夋晥 |
| `PipelineBinding` | device / bind | pipeline 鍒涘缓涓€娆★紝缁戝畾鎸佺画鍒版敼鍙?|
| `DescriptorBinding` | frame / bind | set 鍙互鏄?frame 鐢熷懡鍛ㄦ湡锛岀粦瀹氭寔缁埌鏀瑰彉 |
| `PushConstants` | command buffer / draw | 璁剧疆鍚庢寔缁埌鏀瑰彉 |
| `VertexInputBinding` | device / bind | buffer 鏄?device/frame锛宐inding 鏄?command buffer 鐘舵€?|
| `DynamicState` | command buffer | begin 鏃?undefined锛沺ipeline static 鐘舵€佷細瑕嗙洊锛沗commandBufferInheritance` 鍙户鎵?|
| `DrawArgs` | draw | 涓€娆?draw 鍛戒护 |
| `SyncState` | sync op | barrier/query/event锛涙湰 demo 鍙敤 pass boundary memory barrier |
| `Inheritance` | secondary | secondary 褰曞埗/鎵ц鏈熼棿 |

## 5. Opcode table

| Opcode | Role | Payload | 澶囨敞 |
|---|---|---|---|
| `BeginRendering` | Control | `PassTarget` | 鍦?pass 澶栬皟鐢?|
| `EndRendering` | Control | - | 鍦?pass 鍐呰皟鐢?|
| `BindPipeline` | State | `VkPipeline` + layout | graphics bind point |
| `BindDescriptorSet` | State | layout + set | firstSet=0 |
| `BindDescriptorBuffer` | State | layout + `VkDescriptorBufferBindingInfoEXT` + offset | descriptor buffer 璺緞 |
| `BindResourceHeap` / `BindSamplerHeap` | State | `VkBindHeapInfoEXT` | descriptor heap锛涢€氳繃 `vkGetDeviceProcAddr` 鍙栧嚱鏁?|
| `PushConstants` | State | layout/stages/offset/size/data | 鏈€澶?128 bytes锛坉emo锛?|
| `SetViewport` | State | `VkViewport` | first=0, count=1 |
| `SetScissor` | State | `VkRect2D` | first=0, count=1 |
| `SetDepthTestEnable` / `SetDepthWriteEnable` | State | `VkBool32` | 鍔ㄦ€佹繁搴︾姸鎬?|
| `SetCullMode` / `SetFrontFace` / `SetPrimitiveTopology` / `SetStencilReference` | State | 瀵瑰簲鏋氫妇 | 鍔ㄦ€佸浐瀹氬姛鑳界姸鎬?|
| `BindIndexBuffer` | State | buffer/offset/type | |
| `Draw` / `DrawIndexed` / `DrawIndirect` | Action | draw operands | 鐩存帴/绱㈠紩/闂存帴 |
| `DrawIndexedIndirect` | Action | buffer/offset/drawCount/stride | demo 涓昏矾寰?|
| `CopyImageToBuffer` | Action | image/layout/buffer/regions | pick 鍥炶 |
| `CopyBuffer` / `CopyBufferToImage` / `CopyImage` / `BlitImage` | Action | src/dst/regions/filter | transfer锛沗CopyBuffer` 鍦?per-frame scratch 涓婂疄闄呬娇鐢?|
| `FillBuffer` / `UpdateBuffer` | Action | buffer/offset/data | transfer锛沗FillBuffer` 鍦?pick 鍥炶鍓嶅疄闄呬娇鐢?|
| `ClearColorImage` / `ClearDepthStencilImage` | Action | image/layout/clear/ranges | outside pass |
| `Dispatch` / `DispatchIndirect` | Action | group counts / indirect buffer | compute, outside pass |
| `ClearAttachments` | Action | attachments + rects | pass 鍐?clear |
| `BeginQuery` / `EndQuery` | Sync | query pool/query/flags | occlusion/timestamp 绛?|
| `Barrier` | Sync | memory/buffer/image barrier arrays | pass 杈圭晫 + frame sync |
| `ExecuteCommands` | Indirection | secondary command buffers | CASE 2 鐨?pass body 璧?secondary |

鏂板涓€涓懡浠ゅ彉浣撴椂锛?
1. 鍦?`Opcode`/`OpKind` 鍔犳灇涓撅紱
2. 鍦?`test_intent7_pass_opcode.hpp` 鍔?payload 缁撴瀯锛?3. 鍦?`dump()` 鍔?C 浼唬鐮侊紱
4. 鍦?`VkEmitter::emit()` 鍔犳樉寮?`vkCmd*`锛?5. 鍦?`FrameBuilder` 鍔犺涔夋柟娉曪紱
6. 鍦?`PassValidator` 鍔?scope/lifecycle 妫€鏌ャ€?
**涓嶈**涓烘瘡涓懡浠ゅ姞涓€涓被锛沷pcode + payload + emitter case 灏卞浜嗐€?
## 6. 閫忔槑 emitter 涓庢墜鍐欒川閲?
`VkEmitter::emit(Op)` 鏄竴涓洿鎺ョ殑 switch锛?
```cpp
case Opcode::BindPipeline:
    vk.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline);
    break;
case Opcode::DrawIndexedIndirect:
    vk.vkCmdDrawIndexedIndirect(cmd, p.buffer, p.offset, p.drawCount, p.stride);
    break;
```

娌℃湁闅愬紡閲嶆帓銆佹病鏈夐殣钘?barrier銆佹病鏈夐殣钘?pass銆? 
`FrameBuilder::dump()` 杈撳嚭涓庢墜鍐欎竴鑷寸殑 C 浼唬鐮侊紝渚嬪锛?
```text
#0 // [Outside / Sync] vkCmdPipelineBarrier2
vkCmdPipelineBarrier2(cmd, &dependency{memory=0, buffer=0, image=5}); // layout transition
#1 // [Outside / Pass] vkCmdBeginRendering
vkCmdBeginRendering(cmd, &rendering_info{area=1280x900, colorCount=2, hasDepth=true});
#2 // [Inside / CommandBuffer] vkCmdBindIndexBuffer
vkCmdBindIndexBuffer(cmd, buffer=..., offset=0, type=1);
#3 // [Inside / CommandBuffer] vkCmdBindPipeline
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline=...);
#4 // [Inside / CommandBuffer] vkCmdBindDescriptorSets
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout=..., 0, 1, &set=..., 0, nullptr);
#5 vkCmdSetViewport(...)
#6 vkCmdSetScissor(...)
#7 vkCmdPushConstants(...)
#8 vkCmdDrawIndexedIndirect(...)
#9 vkCmdEndRendering(cmd);
#10 // [Outside / Sync] vkCmdPipelineBarrier2
vkCmdPipelineBarrier2(cmd, &dependency{memory=1, buffer=0, image=0}); // pass boundary
```

`dump()` 鍙湪绗竴甯ф墦鍗帮紱濡傞渶姣忔閮芥墦鍗帮紝鍙妸 `static bool dumped` 鏀规垚鐜鍙橀噺鎺у埗銆?
## 7. 鍚屾

- 鏄惧紡鍚屾鏄?Op锛歚Barrier`锛宲ayload 鎷ユ湁 `memoryBarriers/bufferBarriers/imageBarriers` 涓変釜 vector锛沞mitter 鍦ㄨ皟鐢ㄦ椂鏋勯€犱复鏃剁殑 `VkDependencyInfo`锛屼笉鐣欎笅鎮┖鎸囬拡銆?- frame sync 宸茬粡杩涘叆 OpStream锛?  - `#0`锛? 寮?image 鐨?`UNDEFINED -> ATTACHMENT_OPTIMAL` layout transition锛坈olor/MSAA/depth/pick/resolve锛夛紱
  - 姣忎釜 pass 涔嬮棿锛歚pass_boundary_barrier()` 鐨?memory barrier锛坰tore -> load锛夛紱
  - pass 涔嬪悗锛歱ick resolve `COLOR_ATTACHMENT -> TRANSFER_SRC`锛?  - 鏈€鍚庯細swapchain `COLOR_ATTACHMENT -> PRESENT_SRC`銆?- `pass_boundary_barrier()` 鐨?src/dst stage 鏄?color/depth framebuffer stages锛宎ccess 鏄?attachment read/write銆?- pass 鍐?barrier 浠嶉€氳繃 `FrameBuilder::barrier()` 鍙敤锛屼絾瑕佹敞鎰忥細
  - `vkCmdPipelineBarrier2` 鍦?`beginRendering` 鍐呴渶瑕?`dynamicRenderingLocalRead` 绛夋潯浠讹紱
  - 鏈?demo 鐨?barrier 閮藉湪 pass 涔嬮棿锛圤utside锛夛紝鎵€浠ユ病鏈夎繖涓檺鍒躲€?
## 8. 姣忓抚 PassManager

`passManagers[MAX_FRAMES_IN_FLIGHT]` 涓?`drawRecorder` 甯фЫ浣嶅榻愶紝閬垮厤 CPU 鍐?pass buffer 鏃?GPU 杩樺湪璇讳笂涓€甯с€? 
`FrameBuilder` 姣忓抚鏂板缓锛宍PassData`/`OpStream`/`Validator` 閮芥槸甯у唴涓存椂瀵硅薄銆?
## 9. 宸茬煡缂哄彛

- typed opcode 鍙鐩?test_intent7 鐢ㄥ埌鐨勫懡浠わ紱鍏朵綑 `vkCmd*` 浠嶅彲閫氳繃 `PassRecorder::vk()` / `FrameRecorder::vk()` 鎴栧悗缁柊澧?opcode 鎺ュ叆銆?- `DescriptorBinding` 宸叉湁 set / descriptor buffer / resource heap / sampler heap 鍙樹綋锛沨eap 鐨?emitter 閫氳繃 `vkGetDeviceProcAddr` 鍙栧嚱鏁帮紙VolkDeviceTable 鐗堟湰鏈寘鍚級銆?- `Inheritance` / `commandBufferInheritance` 鏈夌湡瀹炵敤渚嬶細CASE 2 鐨?pass body 褰曡繘 secondary command buffer锛宲rimary pass 鐢?`vkCmdExecuteCommands` 鎵ц锛沗ExecuteCommandsPayload` 鎼哄甫 `InheritanceRenderingInfo`锛宍PassValidator` 鐙珛鏍￠獙 flags/viewMask/color formats/depth/stencil/samples 涓?pass target 鍖归厤銆俿econdary 鏈韩鐜板湪鐢?`SecondaryBuilder` 褰曞埗锛岃蛋鍚屼竴濂?`PassData`/`PassValidator`/`VkEmitter`銆?- `PipelineBinding.ownership` 鐜板湪浠?`StateOwnershipMap` 濉厖锛沗PassValidator` 浼氭鏌?`vkCmdSet*` 瀵瑰簲鐨勭姸鎬佸湪 bound pipeline 涓槸 `Dynamic`/`Inherited`锛岃€屼笉鏄?`Static`/`Absent`銆?- `DynamicState` 宸茶鐩?viewport/scissor/depth test/depth write/cull/front/topology锛涘叾浠栫姸鎬佸瓧娈靛彲缁х画鍔?opcode銆?- `PassValidator` 宸插仛 pipeline-target 鐨?color/depth/stencil format銆乿iewMask銆乻ample count 鍏煎鎬э紝descriptor layout == pipeline layout锛宒ynamic-state ownership锛宻econdary inheritance锛屼互鍙?descriptorBuffer/descriptorHeap/dynamicRenderingLocalRead feature gate锛涘叾浠?feature gate 鍙互缁х画鍔犮€?- `FrameSync` 鍜?`CopyImageToBuffer` 宸茶繘鍏?OpStream锛沗vkCmdBeginCommandBuffer`/`vkCmdEndCommandBuffer`銆乹ueue submit/present 浠嶅湪 FrameBuilder 澶栵紙灞炰簬 frame/queue 灞傦級銆?- `ClearAttachments` / `BeginQuery` / `EndQuery` 宸叉湁 typed opcode锛屼絾 demo 鏈疄闄呬娇鐢紱`ExecuteCommands` 宸插疄闄呬娇鐢ㄣ€?- `FillBuffer` / `CopyBuffer` 宸插湪 per-frame transfer scratch + pick 鍥炶璺緞瀹為檯浣跨敤锛沗CopyBufferToImage` / `CopyImage` / `UpdateBuffer` / `ClearColorImage` / `ClearDepthStencilImage` / `Dispatch` / `DispatchIndirect` 宸叉湁 typed opcode锛屼絾 demo 鏈疄闄呬娇鐢紱瀹冧滑缁熶竴璧?`push_transfer`锛圤utside锛夈€?- `PassKind` 宸叉湁 Graphics/Compute/Transfer锛泃ransfer pass 宸叉帴鍏ュ苟浜х敓 `[Inside / Sync]` 鐨?Fill/Copy/Barrier锛沜ompute pass 宸叉帴鍏?compute pipeline + push constants + `vkCmdDispatch`锛屼絾杩樻病鏈?compute descriptor set 鐢ㄤ緥銆?- 鏁版嵁 schema 宸叉墿鍒板 descriptor set/dynamic offsets銆乿ertex binding 鏁扮粍銆侀€氱敤 `VkDynamicState`銆佸畬鏁?sync/inheritance/transfer2/features/properties銆乸Next 鎸囬拡锛坈aller-owned锛夈€乹ueue/submission/present銆乧lassic render pass銆乺aw command锛汻T/video/acceleration-structure 浠嶅彧鏈?`RawCommandData`锛屾湭 typed銆?- `FrameScheduler` 宸叉帴绠?acquire/submit/present锛宍QueueRole`/`QueueBinding` 宸插缓妯★紱褰撳墠鍙湁 graphics+present 鍗曢槦鍒楋紝compute/transfer queue role 灏氭湭瀹為檯浣跨敤銆?- `ResourceStateTracker`/`BarrierPlanner` 宸叉帴鍏?pick/present 涓や釜 transition锛涘綋鍓嶆槸 per-transition seed锛屼笉鏄叏甯?resource registry銆?- `test_intent7.cpp` 鐨勬敞閲婂湪缂栬緫杩囩▼涓缂栫爜杞崲鎹熷潖锛屽凡鍋?ASCII 娓呯悊锛涗唬鐮侀€昏緫鍜屾瀯寤轰笉鍙楀奖鍝嶃€?
## 10. 鏋勫缓 / 杩愯

```powershell
cd E:\0_github_project\mcsvulkan\build
ninja mcsvulkan-vma-test_intent7
E:\0_github_project\mcsvulkan\output\test_program\mcsvulkan\vma\test_intent7.exe
```

棰勬湡鏃ュ織锛?
```text
[frame-scheduler] graphics_family=0 present_family=0
[pass-test] passes=4 card_cmds=1 overlay_cmds=1
[pass-dump]
#0 // [Outside / Pass] vkCmdBeginRendering
...
[pass-c-api] opcodes=37 calls=54
  #0   vkCmdPipelineBarrier2
  #1   vkCmdBeginRendering
  ...
  #45  vkCmdFillBuffer
  #46  vkCmdPipelineBarrier2
  #47  vkCmdCopyBuffer
  #48  vkCmdBindPipeline          // COMPUTE
  #49  vkCmdPushConstants
  #50  vkCmdDispatch
  #51  vkCmdFillBuffer
  #52  vkCmdCopyImageToBuffer
  #53  vkCmdPipelineBarrier2
```

楠岃瘉灞傛棤 `VUID` 閿欒銆? 
鏈€缁堢敾闈細`output/intent7_ability.png`锛坅bility_test_scene锛夛紝鏃?pass 娴嬭瘯鎴浘淇濈暀涓?`output/intent7_exec.png`銆?
## 11. 璁捐瀹¤锛?026-11-08锛?
**瀵归綈鐨勯儴鍒?*

- 涓绘祦绋嬩粛鏄?`UIWidget -> DrawSegment intent -> FrameBuilder(OpStream) -> VkEmitter -> vkCmd*`銆?- 涓夎酱妯″瀷浠嶆垚绔嬶細绾靛悜鏁版嵁娴侊紙Target/Pipeline/Resources/VertexInput/DynamicState锛夈€佹í鍚?Opcode锛圕ontrol/State/Action/Sync/Indirection锛夈€佺敓鍛藉懆鏈?Scope锛坉evice/frame/pass/command-buffer/draw/sync/inheritance锛夈€?- main/extra pass 閮芥槸 `PassSource`锛屾病鏈夆€滈噸寮€ main pass鈥濈殑鐗规畩璺緞銆?- frame sync銆乸ass boundary銆乧opy銆乸resent 閮藉湪 OpStream锛沞mitter 浠嶆槸閫忔槑 1:1銆?- 楠岃瘉灞?provider銆丆ASE 2 secondary銆乮nheritance銆乻tate ownership銆乫eature gate 閮藉湪鍚屼竴鏉¤矾寰勪笂銆?
**涓昏鍋忓樊 / 椋庨櫓**

1. ~~secondary 鐨?`StreamRecorder` 缁曡繃浜?`PassData`/`PassValidator`~~ **宸蹭慨**锛? 
   鐜板湪 secondary 鐢?`SecondaryBuilder` 褰曞埗锛屽叡浜?`PassData`/`PassValidator`锛屽彧绂佺敤 `BeginRendering/EndRendering`锛沺ipeline-target銆乨escriptor layout銆乨ynamic ownership 閮戒細鍦?secondary 鍐呮牎楠屻€?
2. ~~`FeatureSet` 鏄墜宸ラ暅鍍忥紝涓嶆槸浠?`VkPhysicalDeviceFeatures2` 鏌ヨ寰楀埌~~ **閮ㄥ垎淇?*锛? 
   `pass_data::query_features(physical_device)` 鏌ヨ鏈?app 瀹為檯鍚敤鐨?core/Vulkan12/Vulkan13/ExtendedDynamicState feature锛沝escriptorBuffer/descriptorHeap/dynamicRenderingLocalRead 浠嶆樉寮?false锛屽洜涓?app 鐨?device enable chain 娌″惎鐢ㄥ畠浠€傛煡璇⑩€滄敮鎸佲€濅笉绛変簬鈥滃惎鐢ㄢ€濓紝鎵€浠ユ病鏈夋妸鎵╁睍鏀寔鐩存帴褰撴垚鍙敤銆?
3. ~~`PassTarget` 鐨?colorFormats/samples 鏄?app 鎻愪緵鐨勫厓鏁版嵁锛屼笉鏄粠鐪熷疄 `VkImageView` 鎺ㄥ鐨剘~ **宸蹭慨**锛? 
   `initPipeline` 浠?`msaaResourcesBuild` / `pickResourcesBuild` / `depthResourcesBuild` 鐨?create-info 鏋勯€?`passTargetTemplate`锛沗makeMainPassDesc` 浠庡畠鎷疯礉 color/depth/samples锛屼笉鍐嶄粠 pipeline 甯搁噺鎵嬪啓銆倂alidator 鐜板湪姣旇緝 pipeline metadata vs **璧勬簮娲剧敓鐨?target metadata**銆?
4. **legacy `PassRecorder` / `FrameRecorder` 浠嶅湪**锛屼絾宸查檷绾т负 raw fallback锛? 
   `Pass::record` 宸插垹闄わ紱`pass_stream.hpp` 椤堕儴鏄庣‘鏍囨敞 LEGACY / RAW FALLBACK锛涗富璺緞鍙蛋 `FrameBuilder` / `SecondaryBuilder`銆傚畬鏁村垹闄ゅ彲鍚庣画鍋氥€?
5. **`FrameBuilder` 榛樿鈥滆竟鏋勫缓杈瑰彂灏勨€?*锛屼絾宸插姞 `deferEmit` + `emitAll()`锛氬彲浠ュ厛鏋勫缓/鏍￠獙鏁翠釜 `OpStream`锛屽啀鍗曠偣鍙戝皠銆傞粯璁や粛鏄嵆鏃舵ā寮忥紝涓嶆敼鍙?demo 琛屼负銆?
6. **`screen.makeRenderDirty(currentFrame)` 浠嶆槸 workaround**銆? 
   灏濊瘯杩囧彧鏇存柊 pass target 鐨?resolve view銆佷笉閲嶅缓 UI锛涗絾 main pass 鐨?UI stream / camera 鏁版嵁鏈韩涔熸槸閫愬抚閲嶅缓鐨勶紝璐哥劧鍘绘帀浼氳 main pass 浣跨敤鏃?stream銆傛纭媶娉曢渶瑕佹妸鈥渕ain pass 閫愬抚褰曞埗鈥濆拰鈥渆xtra pass target 鏇存柊鈥濆垎寮€锛屽綋鍓嶄繚鐣欏己鍒堕噸寤烘洿瀹夊叏銆?
7. **`BarrierPayload` 宸插鍒?`dependencyFlags`锛屼絾浠嶄笉澶嶅埗 `VkDependencyInfo::pNext`**锛沗PassTarget::pNext` 涔熸槸瑁告寚閽堛€? 
   褰撳墠 demo 鐨?pNext 涓?null锛涗竴鑸墿灞曢摼闇€瑕?deep-copy 鎴栨槑纭€滃彧鍦?immediate emit 鏃舵湁鏁堚€濄€?
8. **compute/transfer opcode**锛歚FillBuffer` + `CopyBuffer` 宸插湪 per-frame scratch 璺緞瀹為檯浣跨敤锛團ill -> barrier -> Copy锛夛紱`CopyImage`/`UpdateBuffer`/`Dispatch` 绛変粛 typed 浣?demo 鏈娇鐢紝鐪熷疄 compute pass 鐢ㄤ緥杩樻病鍋氥€?
**缁撹**

鏍稿績璁捐娌℃湁鍋忕锛氭暟鎹?鎿嶄綔/emitter/涓夎酱浠嶅湪銆傚亸宸泦涓湪鈥滃畬鏁存€р€濆拰鈥滃崟涓€浜嬪疄婧愨€濓細
feature/format 鍏冩暟鎹€乴egacy 鍙岃矾寰勩€乸Next 鐢熷懡鍛ㄦ湡銆俿econdary 鏍￠獙宸茬敱 `SecondaryBuilder` 琛ヤ笂銆?杩欎簺鍙互鍦ㄤ笉鏀逛笁杞存ā鍨嬬殑鍓嶆彁涓嬩慨鎺夛紝浼樺厛绾у缓璁細

1. ~~`SecondaryBuilder`锛堝叡浜?PassData/Validator锛屽彧绂佺敤 Begin/End锛墌~ 宸插仛锛?2. ~~`FeatureSet::from_device`锛堢湡瀹?feature query锛墌~ 閮ㄥ垎鍋氾紙`query_features` 鏌?core锛涙墿灞?gate 闇€鍦ㄥ惎鐢ㄦ墿灞曟椂鍚屾锛夛紱
3. ~~浠庤祫婧愬眰鎺ㄥ `PassTarget` metadata~~ 宸插仛锛坄passTargetTemplate` 鏉ヨ嚜璧勬簮 create-info锛夛紱
4. ~~鍒犻櫎/deprecate legacy `PassRecorder`/`FrameRecorder`/`Pass::record`~~ 閮ㄥ垎鍋氾細`Pass::record` 鍒犻櫎锛宭egacy 鏍囪 raw fallback锛涘畬鏁村垹闄ゅ彲閫夛紱
5. ~~`FrameBuilder` 鐨?build-only 妯″紡~~ 宸插姞 `deferEmit`/`emitAll`锛沚arrier pNext 鏀寔浠嶅緟鍋氥€?
## 12. Ability matrix final (ASCII)

- 21 representative tiles + wide composed UI.
- 7 passes: base / shape-path / texture-effect / offscreen layer / stencil clip / offscreen composite (secondary) / text overlay.
- offscreen layer: MSAA color + sampled resolve; explicit pre/post image barriers in PassSource.
- group opacity: offscreen group flattened, composite alpha.
- backdrop blur: offscreen texture sampled through blur.
- stencil clip: D32_SFLOAT_S8_UINT combined depth/stencil; mask REPLACE -> content EQUAL.
- picking: 21 card entities are written to the MRT pick attachment; hover highlights the picked card.
- text: 430 glyphs including RTL/Chinese label.
- blend: opaque / alpha / additive / multiply pipelines.
- clip: per-segment scissor, nested push/pop intersection, alpha mask.
- MSAA / resolve / 2 MRT / depth target / load-store.
- indexed + instanced + indirect + BDA.
- 7 DAO programs; 430 glyphs.
- secondary: offscreen composite pass recorded into secondary command buffer.
- C API list: opcodes=37, calls=193.
- No VUID, no validation error, no pass-validator, no assert.

## 13. Final status addendum (2026-11-08, ASCII)

The gaps named in sections 3 and 9 are closed for the runtime path:

- Fixed-pixel UI: 170x126 tiles, 7x6 grid, 36 feature cases + composed panel.
  Window growth reveals space; it never scales the widgets. Pixel -> NDC uses
  the live framebuffer extent.
- Runtime resize/fullscreen: offscreen MSAA color, sampled resolve and the
  D32S8 stencil attachment are rebuilt with the swapchain. The sampled resolve
  is replaced in the same texture slot and the descriptor set is refreshed.
  The ability scene receives the new handles via updateResources().
- 4x MSAA is capped explicitly (SAMPLE_COUNT) so the 128-bit pick MRT fits the
  device-local heap at 1920x1080.
- Old extent resources are released before allocating the new ones.
- F11 is an edge-triggered fullscreen toggle in test_intent7.cpp.
- Color emoji are supported and linearized from the sRGB UNORM atlas.
- Text cases: MSDF, RTL Arabic, CJK, color emoji, UAX #14 wrapping, mixed bidi.
- Retained offscreen composition layer: rebuilt on resize/fullscreen, skipped on
  clean frames (7 -> 5 recorded passes); the composite pass samples the retained
  resolve texture.
- Deleted shaders/test_intent7.comp and the no-op compute pipeline; the transfer
  pass (FillBuffer -> barrier -> CopyBuffer) remains.
- Verified: 1280x900 -> 1582x953 -> 1920x1080 fullscreen -> back, no VUID, no
  validation error, no pass-validator failure, no assert.
- Updated report: INTENT7_MIGRATION_REPORT.md
- Screenshots: output/intent7_ability.png, output/intent7_resize_1600.png
