# peek

单文件 C 实现的迷你 HTML5+CSS3 解析器，把网页渲染到终端

## 构建与运行

```sh
xmake run peek demo.html
```

## 支持范围

**HTML5**：标签/属性解析、void 标签（br/hr/img/meta/link/input）、注释、`<style>` 提取、栈式 DOM 构建；`<button>` 线框按钮、`<br>` 换行、`<hr>` 横线、`<li>` 项目符；h1-h6 内置粗体；`script`/`title`/`head` 不落地；语义块级标签（header/footer/section/article/nav/aside/main/blockquote/ul/ol）渲染后换行。实体解码：`&amp;` `&lt;` `&gt;` `&quot;` `&apos;` `&nbsp;` 与十进制 `&#NN;`（文本与属性值均生效）。

**选择器**：标签、`.class`、`#id`、`[attr]`、`[attr=val]`、`[attr^=val]`、`[attr$=val]`、`[attr*=val]`、通配 `*`、`:first-child`、`:last-child`；组合器——后代（空格）、子 `>`、相邻兄弟 `+`、通用兄弟 `~`；逗号分组共享声明；CSS `/* */` 注释原地剥离。特异度位打包为 `(id << 8) | (class << 4) | tag`，属性选择器与伪类计入 class 桶，`*` 计 0；内联 `style` 恒为最高位 `1 << 16`。

**级联与继承**：同属性按特异度覆盖；`color`、`font-weight`、`font-style`、`text-transform` 自父向子传播，子节点显式声明优先。

**颜色**：`38;5;n` / `48;5;n` 256 色输出，支持六种写法——

| 写法 | 示例 |
|---|---|
| 直接色号 | `color: 231` |
| 十六进制 | `#0af` / `#00aaff` |
| 色名 | 基础 8 色 + lime/navy/teal/maroon/purple/olive/fuchsia/gray/silver/aqua |
| rgb 函数 | `rgb(20,90,160)`、空格写法 `rgb(20 90 160)` |
| alpha | `rgba(255,0,0,0.6)`、`rgb(255 0 0 / .5)` |
| hsl 函数 | `hsl(210,100%,60%)`、`hsla(0deg,100%,50%,.8)` |

灰阶（r==g==b）映射到 232-255 灰度带，彩色量化到 6x6x6 色立方。

**属性**：`color`、`background-color`、`font-weight: bold`、`font-style: italic`、`text-decoration: underline / line-through`、`padding` / `width` / `border: none`、`display: none`、`text-align: center`、`text-transform: uppercase/lowercase`。

## 实现要点

- **零拷贝**：直接在源缓冲上写 `\0` 切分 token，不复制字符串、不 malloc
- **静态池**：`POOL[256]` 节点池 + `BUF[64K]` 输入缓冲，静态分配一次到位
- **SWAR 比较**：`pk()` 把 8 字节打包成 `uint64_t` 一次比较，消灭 `strcmp`；`K4('h','t','m','l')` 编译期常量打包
- **两段预计算**：选择器 `presplit` 预分词（组合符沉淀进 `sep[]`），声明键 `pk` 哈希只算一次，匹配与渲染端全查表
- **乘移魔法数**：`(v * 1287 + 32896) >> 16` 代替 `(v + 25) / 51` 等全部除法；HSL 转 RGB 全程整数
- **跳表**：色名、void 标签、块级标签、属性分发均用 `switch(uint64_t)` 代替 if-else 链
- **位运算**：空白判断位图、按钮 `flags` 位收集样式（1=bold 2=italic 4=underline 8=center 16=borderless）、`(80-len) >> 1` 居中

## 已知限制

- 静态容量上限：节点 256、子节点/样式各 32、规则 64、选择器段 8、输入 64KB
- `pk()` 仅比较前 8 字节，超长标签/属性值靠首 8 字节区分
- 无错误恢复；伪类仅结构性 `:first-child`/`:last-child`；无 `:nth-child()`、`::before` 等内容生成
- `rgba`/`hsla` 的 alpha 按暗背景混合近似，终端无真透明

## License

MIT
