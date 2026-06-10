/Users/kwy/Documents/2Dyinqing/项目参考/7c4d2acb-6680-40cf-a8dd-fca340c887e0.png
当前实现仍然不符合我的需求，请只修正右侧“大纲 / 细节”的 Tab 样式和对齐方式，不要修改拖拽逻辑、还原布局逻辑和其他功能。

请先区分两个 Tab 栏：

一、左上角红框区域是 MainEditorTabBar
位置：界面左上方。
内容示例：NewLevel、关卡蓝图。
作用：管理主编辑区打开的页面。
我把它作为视觉参考。

二、右侧白框区域是 RightDockTabBar
位置：右侧面板顶部。
内容示例：大纲、细节。
作用：当大纲和细节被拖拽合并后，用来切换右侧 Dock 面板。
它不是 MainEditorTabBar，但视觉样式要参考 MainEditorTabBar。

当前错误：
右侧“大纲 / 细节”现在还是被做成了居中的按钮组 / 分段按钮效果，不是我想要的编辑器 Tab 效果。

我真正要的效果：
右侧“大纲 / 细节”应该像左上角红框里的 NewLevel / 关卡蓝图 标签一样，从右侧面板顶部区域的左侧开始排列。

具体要求：

1. 右侧“大纲 / 细节”必须靠左排列。
2. 不要居中。
3. 不要右对齐。
4. 不要做成蓝色圆角按钮。
5. 不要做成 SegmentedControl / ToggleButtonGroup。
6. 不要让两个 Tab 平分整行宽度。
7. 不要让 TabItem 使用 flex: 1。
8. Tab 外观要参考左上角红框里的编辑器标签页：

   * 深色矩形标签
   * 标签从左向右排列
   * 当前选中 Tab 使用底部蓝色激活线或类似激活状态
   * 未选中 Tab 是普通深色标签
   * 不是蓝色 pill 按钮
9. 外层右侧 Header 区域可以占满右侧面板宽度，但内部“大纲 / 细节”两个 Tab 本身不要被拉伸。
10. 右侧“大纲 / 细节”只是视觉参考 MainEditorTabBar，不要和 MainEditorTabBar 共用业务状态。

请重点检查：

1. RightDockTabBar 是否用了 justify-content: center。
2. RightDockTabBar 是否用了 text-align: center。
3. RightDockTabBar 内部是否用了 margin: auto。
4. TabItem 是否用了 flex: 1。
5. 是否把右侧 Tab 写成了 SegmentedControl / ButtonGroup。
6. 是否可以复用 MainEditorTabBar 的视觉样式，但保持独立的右侧 Dock 面板状态。

修正要求：

1. 只修正右侧“大纲 / 细节”的 Tab 样式和左对齐。
2. 不要修改 MainEditorTabBar 的业务逻辑。
3. 不要把“大纲 / 细节”加入左上角 MainEditorTabBar。
4. 不要修改蓝图编辑器、中央视口、顶部工具栏、底部栏。
5. 不要修改拖拽合并、拖出弹窗、还原布局逻辑。
6. 修改前先说明会改哪些文件。
7. 修改后给出验证步骤。

验收标准：

1. 左上角 MainEditorTabBar 仍然正常显示 NewLevel / 关卡蓝图。
2. 右侧 RightDockTabBar 显示 大纲 / 细节。
3. 右侧 大纲 / 细节 靠左排列，不再居中。
4. 右侧 大纲 / 细节 的视觉效果接近左上角红框中的编辑器标签页。
5. 右侧 Tab 不是蓝色圆角按钮，不是分段按钮。
6. 点击“大纲”显示大纲内容。
7. 点击“细节”显示细节内容。
8. 其他拖拽和还原布局功能不受影响。
