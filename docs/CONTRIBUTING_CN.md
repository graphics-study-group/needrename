## 如何贡献

### 报告问题

- 创建新 issue 前请先搜索是否已有相同问题
- 请使用问题报告模板（如果有）
- 内容应包含：
  - 环境信息（操作系统、编译器、SDK 版本）
  - 最小复现步骤
  - 预期行为与实际行为
  - 相关的日志或崩溃报告

### 建议新功能

- 创建新 issue 描述你想要解决的问题
- 如果可以，提供解决方案和代码示例

### 提交 Pull Request

1. **Fork 仓库**，从 `main` 分支创建你的分支
2. **分支命名**：`feature/your-feature` 或 `fix/your-bugfix`
3. **进行修改**：
   - 遵循 [代码规范](./CODE_STYLE_CN.md)
   - 提交前运行 `clang-format` 格式化代码
   - 为新功能添加测试
   - 保持 commit 原子性，描述清晰
4. **Commit 信息**：使用清晰描述性的信息：
   - `Add Vulkan pipeline cache support`
   - `Fix memory leak in AssetManager::Unload`
   - `Refactor Scene serialization to use new archive format`
5. **提交 Pull Request**：
   - 填写 PR 模板
   - 关联相关 issue
   - 等待 review 并处理反馈

### Code Review 流程

- 至少需要一名维护者 review
- 处理所有 review 评论
- 确保 CI 通过后再合并
- 保持尊重和建设性的讨论

## OpenSpec 工作流

对于重要功能和架构变更，我们采用 **OpenSpec** 规范驱动开发流程：

1. **Explore**（`/openspec-explore`）— 探索问题空间、明确需求、权衡方案
2. **Propose**（`/openspec-propose`）— 一步生成 proposal、design、specs 和 tasks
3. **Design review**（`/grill-me`）— *（可选项）* 通过穷追猛打的访谈来压力测试提案。请优先使用 `/grill-me` 而非 `/grill-with-docs`——所有决策都应记录在 OpenSpec 产物中，而非独立的 ADR 文档。参见 [grilling skill](https://github.com/mattpocock/skills)。
4. **Apply**（`/openspec-apply-change`）— 逐任务实现变更
5. **Archive**（`/openspec-archive-change`）— 归档已完成的变更

所有 OpenSpec 产物位于 `openspec/` 目录：

- `openspec/specs/` — 已实现功能的单一规范来源
- `openspec/changes/archive/` — 归档的变更记录，包含 proposal、design、tasks 和 delta specs

简单修复和琐碎修改可跳过 OpenSpec 工作流。

## 开发流程

```
main 分支 (稳定版) <- 功能分支 <- 你的工作
                          |
                          v
                   Pull Request review
                          |
                          v
                     合并到 main
```

## 代码标准

- 遵循 [代码规范](./CODE_STYLE_CN.md)
- 使用 `clang-format` 格式化代码
- 编写有意义的 commit 信息
- 为新公开 API 添加文档
- 为新功能添加单元测试

## 测试

- 提交前运行现有测试
- 为新功能添加测试
- （对内部维护者）如果认为某项测试已过时应该放弃，可以在经过项目组讨论后删除

## 许可证

贡献代码即表示您同意您的代码将根据项目的 [LICENSE](../LICENSE) 获得许可。

## 问题？

- 通过 issue 反馈 bug 或功能请求
- 查看 [wiki](../wiki) 获取技术文档
