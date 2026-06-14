蓝图类修改有丢失风险。 BlueprintEditor 会发出 bpClassModified，但 EditorWindow.cpp (line 191) 只连接了 documentModified。Cmd+S 和“全部保存”只保存关卡/UI，不保存 .bp；关闭窗口时 EditorWindow.cpp (line 840) 也只检查关卡脏状态，然后直接释放 m_openBpClasses。建议先给 BPClass 加 dirty 状态，并统一接入当前保存、全部保存、关闭提示。

UI 控件引用系统实现和设计文档已经漂移。 设计文档要求 UI.Ref 有固定“UI引用”引脚，动态控件引脚只用于具体控件；但代码里 UI.Ref 静态引脚为空，节点高度也只按“选择器 + 动态控件”计算：BlueprintEditor.cpp (line 211)、BlueprintEditor.cpp (line 414)。同时 UI.Create/Show/Hide/Destroy 也被改成了 widgetRef，和 设计文档 (line 44) 不一致。这个需要统一口径，否则后续蓝图节点会越修越乱。

Actor 级蓝图的 UI 点击事件大概率触发不了。 UI.Ref 输出的是 uiName::widgetName，关卡蓝图运行时会把实例 ID 反查成 UI 名再匹配：BPRuntime.cpp (line 357)。但 Actor 蓝图运行时只拿 refUi == instanceId 判断：ActorBPRuntime.cpp (line 256)。结果是 Actor 蓝图里从 UI引用 连出的按钮/下拉事件，用 UI 名匹配不上运行时实例 ID。

新建项目/新建关卡的摄像机默认值不一致。 文档说 cameraSize = 分辨率高度 / 2，1920×1080 应为 540：摄像机参数说明.md (line 54)。代码模型默认也是 540：LevelDocument.h (line 33)。但反序列化默认值和新建关卡写入都是 5.0：LevelDocument.cpp (line 81)、ContentBrowser.cpp (line 762)。另外新建项目的 Default.level 直接写空对象数组，没有默认主摄像机：LauncherWindow.cpp (line 205)。这会让游戏视图一开始不可用或缩放异常。

下拉菜单运行时只有渲染，没有真实交互。 UI.下拉菜单 能画出来，也有 notifyDropdownChanged 信号，但全项目只有定义和连接，没有任何地方调用它。GameViewport::mousePressEvent 当前只命中按钮并触发 notifyButtonClicked：GameViewport.cpp (line 309)。所以“下拉选项改变时”节点目前基本不可用。

架构问题
EditorWindow 现在是核心上帝类：同时管停靠布局、文档 Tab、关卡文档、BPClass、UIDocument、运行时、内容浏览器和保存流程，文件已经到 1200 行。建议拆出三个边界：DocumentRegistry 统一管理关卡/BP/UI 的打开、dirty、保存、关闭；RuntimeController 管 BPRuntime/UIRuntime/ActorBPRuntime 生命周期；AssetService 处理资源新建、删除、重命名并通知打开文档。
蓝图系统也需要收束：节点定义在 BlueprintEditor，解释逻辑散在 BPRuntime 和 ActorBPRuntime，现在已经出现行为不一致。更稳的做法是建立共享的节点 schema 和执行 helper，关卡蓝图/Actor 蓝图只提供不同的 execution context。