#include <any>
#include <cassert>
#include <map>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

// NOLINTBEGIN
// ---------- 原始框架（已修正） ----------
struct dom
{
    using template_type = std::any;
    using script_type = std::any;
    using id_type = std::string;

    constexpr virtual void update_dom(int cmd_id) = 0;

    id_type id;
    template_type template_;
    script_type script;
};

template <auto updateimpl, typename Template, typename Script>
    requires(requires(dom *self, int cmd_id, Template &templateRef, Script &script) {
        updateimpl(self, cmd_id, templateRef, script);
    })
struct button : dom
{
    constexpr void update_dom(int cmd_id) override
    {
        auto &templateRef = std::any_cast<Template &>(this->template_);
        auto &scriptRef = std::any_cast<Script &>(this->script);
        updateimpl(static_cast<dom *>(this), cmd_id, templateRef, scriptRef);
    }
};

// ---------- 模拟 Vue3 的类型定义 ----------
// 虚拟节点，对应 Vue 模板编译后的结果
struct VNode
{
    std::string tag;
    std::string children;
};

// 模板类型：可以扩展为树结构，此处仅做示意
struct Template
{
    VNode root;
};

// 脚本类型：组件响应式数据与方法
struct Script
{
    int count = 0;
    std::string message = "Hello Vue3";
    void increment()
    {
        ++count;
    }
};

// ---------- 更新实现（类似 Vue3 的渲染/patch 函数） ----------
void vue3_update(dom *self, int cmd_id, Template &tmpl, Script &scr)
{
    std::cout << "[update] component: " << self->id << ", cmd: " << cmd_id << '\n';
    switch (cmd_id)
    {
    case 0: // 挂载（mount）
        std::cout << "  mount -> <" << tmpl.root.tag << ">" << tmpl.root.children << "</"
                  << tmpl.root.tag << ">\n";
        break;
    case 1: // 更新（patch）
        std::cout << "  patch -> <" << tmpl.root.tag << ">" << scr.message
                  << " (count=" << scr.count << ")"
                  << "</" << tmpl.root.tag << ">\n";
        break;
    default:
        std::cout << "  unknown command\n";
    }
}

// 定义具体组件类型（相当于 Vue 的单文件组件）
using App = button<vue3_update, Template, Script>;

void test()
{
    /***
<template>
  <!-- 根节点，v-if 条件渲染 -->
  <div v-if="ready" id="app" @click.self="backgroundClick">
    <!-- 1. 动态绑定与插值 -->
    <h1>{{ title }}</h1>
    <!-- 动态 attribute 名 -->
    <a v-bind:[dynamicAttr]="url" @click.prevent="handleClick('参数值', $event)">链接</a>

    <!-- 2. class / style 绑定 -->
    <p :class="['static', { active: isActive }]" :style="{ color: activeColor, fontSize: fontSize + 'px' }">文本</p>

    <!-- 3. v-model 双向绑定，支持修饰符 -->
    <input v-model.trim.lazy="inputText" placeholder="输入" />
    <p>输入值：{{ inputText }}</p>

    <!-- 4. v-if / v-else-if / v-else -->
    <span v-if="score >= 90">优秀</span>
    <span v-else-if="score >= 60">及格</span>
    <span v-else>不及格</span>

    <!-- 5. v-show -->
    <p v-show="showHint">提示消息</p>

    <!-- 6. v-for 列表，遍历数组和对象 -->
    <ul>
      <li v-for="(item, index) in filteredList" :key="item.id">
        {{ index }}: {{ item.name }}
      </li>
    </ul>
    <ul>
      <li v-for="(value, key, index) in person" :key="key">{{ key }}: {{ value }} ({{ index }})</li>
    </ul>

    <!-- 7. 事件修饰符：.stop .prevent .capture .self .once .passive -->
    <div @click.capture.once="onceCapture">测试</div>

    <!-- 8. 动态组件 -->
    <component :is="currentComponent" />

    <!-- 9. 子组件，使用 defineModel 实现 v-model -->
    <ChildComponent
      v-model:title="modelTitle"
      v-model:count="modelCount"
      @update="handleUpdate"
    >
      <!-- 默认插槽 -->
      <template #default>
        <span>默认插槽内容</span>
      </template>
      <!-- 具名插槽 -->
      <template #footer="{ slotCount }">
        <span>作用域插槽: {{ slotCount }}</span>
      </template>
    </ChildComponent>

    <!-- 10. Teleport -->
    <Teleport to="body">
      <div v-if="showModal" class="modal">模态框</div>
    </Teleport>

    <!-- 11. Suspense -->
    <Suspense @resolve="onResolve">
      <AsyncComponent />
      <template #fallback> 加载中... </template>
    </Suspense>

    <!-- 12. Transition / TransitionGroup -->
    <Transition name="fade">
      <p v-if="showHint">淡入淡出</p>
    </Transition>
    <TransitionGroup name="list" tag="ul">
      <li v-for="item in list" :key="item.id">{{ item.name }}</li>
    </TransitionGroup>

    <!-- 13. 自定义指令 -->
    <div v-focus v-color="'red'">自定义指令</div>

    <!-- 14. 模板引用 -->
    <p ref="pRef">引用测试</p>
  </div>
</template>

<script setup>
import { ref, reactive, computed, watch, watchEffect, onMounted, onUnmounted,
         provide, inject, defineAsyncComponent, nextTick, useTemplateRef } from 'vue'
import ChildComponent from './ChildComponent.vue'
import { vFocus, vColor } from './directives'  // 自定义指令

// ---------- 响应式数据 ----------
const title = ref('完整示例')
const isActive = ref(false)
const inputText = ref('')
const score = ref(85)
const showHint = ref(true)
const showModal = ref(false)
const ready = ref(false)
const url = ref('https://example.com')
const activeColor = ref('blue')
const fontSize = ref(16)
const dynamicAttr = ref('href')

// 对象
const person = reactive({ name: 'John', age: 30 })
const list = reactive([
  { id: 1, name: 'Item 1' },
  { id: 2, name: 'Item 2' }
])

// 组件上 v-model 绑定的值
const modelTitle = ref('子组件标题')
const modelCount = ref(0)

// 动态组件名称
const currentComponent = ref('ChildComponent')  // 可以是字符串

// ---------- 计算属性 ----------
const filteredList = computed(() =>
  list.filter(item => item.name.includes(inputText.value))
)
// 可写计算属性
const double = computed({
  get: () => inputText.value.repeat(2),
  set: val => { inputText.value = val.slice(0, val.length / 2) }
})

// ---------- 侦听器 ----------
watch(inputText, (newVal, oldVal) => {
  console.log(`inputText 从 ${oldVal} 变为 ${newVal}`)
}, { immediate: true })

watch([isActive, score], ([newActive, newScore]) => {
  console.log('组合侦听', newActive, newScore)
})

watchEffect(() => {
  // 自动追踪依赖
  console.log('inputText 或 list 变化了', inputText.value, list.length)
})

// ---------- 生命周期 ----------
onMounted(() => {
  ready.value = true
  console.log('组件挂载完毕')
})
onUnmounted(() => console.log('组件卸载'))

// ---------- 方法 ----------
function toggle() {
  isActive.value = !isActive.value
  showHint.value = !showHint.value
  showModal.value = !showModal.value
}
function handleClick(arg, event) {
  console.log('点击了，参数:', arg, 'event:', event)
}
function backgroundClick() {
  console.log('背景点击（仅点击自身时触发）')
}
function handleUpdate(payload) {
  console.log('子组件更新', payload)
}
function onResolve() {
  console.log('异步组件解析完成')
}

// ---------- 依赖注入 ----------
provide('globalTitle', title)
const injectedData = inject('someKey', '默认值')

// ---------- 异步组件 ----------
const AsyncComponent = defineAsyncComponent(() => import('./HeavyComponent.vue'))

// ---------- 模板引用 ----------
const pRef = useTemplateRef('pRef')
onMounted(() => {
  console.log('pRef 元素:', pRef.value)
})

// ---------- 组合函数 ----------
function useMouse() {
  const x = ref(0)
  const y = ref(0)
  const update = (e) => { x.value = e.clientX; y.value = e.clientY }
  onMounted(() => window.addEventListener('mousemove', update))
  onUnmounted(() => window.removeEventListener('mousemove', update))
  return { x, y }
}
const { x, y } = useMouse()

// ---------- nextTick ----------
nextTick(() => {
  console.log('DOM 更新完成')
})

// 暴露给父组件的方法（如果需要）
defineExpose({ toggle })
</script>

<style scoped>
.static
{
    font - weight : bold;
}
.active
{
color:
    blue;
}

.fade - enter - active, .fade - leave - active
{
transition:
    opacity 0.5s;
}
.fade - enter - from, .fade - leave - to
{
opacity:
    0;
}
.list - enter - active, .list - leave - active
{
transition:
    all 0.5s;
}
.list - enter - from, .list - leave - to
{
opacity:
    0;
transform:
    translateX(30px);
}
</ style> 
*/
}

void test0()
{
    using AnyMap = std::map<std::string, std::any, std::less<>>;
    auto a1 = std::any(int{12});
    AnyMap data;
    data["a1"] = std::move(a1);
    assert(not a1.has_value());
    auto v = std::any_cast<int>(data["a1"]);
    assert(v == 12);
}

int main()
{
    try
    {
        test0();
        App app;

        // 组件配置：id、模板、数据
        app.id = "app";
        app.template_ = Template{VNode{"div", "loading..."}};
        app.script = Script{0, "Hello Vue3"};

        // 模拟 Vue3 的挂载阶段
        app.update_dom(0);

        // 模拟响应式数据变化，触发更新
        auto &state = std::any_cast<Script &>(app.script);
        state.count = 42;
        state.message = "Data changed";
        app.update_dom(1);

        std::cout << "main done\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
// NOLINTEND