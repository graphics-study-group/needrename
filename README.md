# 未命名项目

cmake构建项目

克隆时需要克隆所有子项目(submodule)



third_party，第三方库

engine，引擎代码

example，示例代码



## engine 架构

主循环在MainClass里

Framework里包含GameObject，Component组件，Level关卡等游戏内容相关，并有一个世界WorldSystem来管理



将窗口、渲染、世界视为”系统“，MainClass里会启动窗口系统、渲染系统，*未来会添加更多系统（物理、粒子、控制、消息等）*



主循环：

1. *控制系统读取操作*
2. 世界系统```Framework/World/WorldSystem```按照delta t更新逻辑，*更新时需要渲染的组件向渲染系统登记*
3. *渲染系统```Render/RenderSystem```读取网格模型点坐标，并使用预处理的三角面片，材质，shader等进行渲染*
4. 窗口系统```Functional/SDLWindow```将渲染的画面显示