## How to Contribute

### Reporting Bugs

- Search existing issues before creating a new one
- Use the bug report template when available
- Include:
  - Environment details (OS, compiler, SDK versions)
  - Minimal reproduction steps
  - Expected vs actual behavior
  - Relevant logs or crash reports

### Suggesting Features

- Create a new issue to describe the problem you want to solve
- Propose a solution with code examples if applicable

### Pull Requests

1. **Fork the repository** and create your branch from `main`
2. **Branch naming**: `feature/your-feature` or `fix/your-bugfix`
3. **Make your changes**:
   - Follow the [Code Style Guide](./CODE_STYLE.md)
   - Run `clang-format` on modified files before committing
   - Add tests for new functionality
   - Keep commits atomic and well-described
4. **Commit messages**: Use clear, descriptive messages:
   - `Add Vulkan pipeline cache support`
   - `Fix memory leak in AssetManager::Unload`
   - `Refactor Scene serialization to use new archive format`
5. **Submit a Pull Request**:
   - Fill out the PR template
   - Link related issues
   - Wait for review and address feedback

### Code Review Process

- At least one maintainer review is required
- Address all review comments
- Ensure CI passes before merging
- Be respectful and constructive in discussions

## OpenSpec Workflow

For non-trivial features and architecture changes, we use an **OpenSpec** spec-driven development workflow:

1. **Explore** (`/openspec-explore`) — Investigate the problem space, clarify requirements, think through tradeoffs
2. **Propose** (`/openspec-propose`) — Generate proposal, design, specs, and tasks in one step
3. **Apply** (`/openspec-apply-change`) — Implement the change task by task
4. **Archive** (`/openspec-archive-change`) — Finalize and archive the completed change

All OpenSpec artifacts live under the `openspec/` directory:

- `openspec/specs/` — Single source of truth for implemented features
- `openspec/changes/archive/` — Archived change records with proposal, design, tasks, and delta specs

Quick fixes and trivial changes may skip the OpenSpec workflow.

## Development Workflow

```
main branch (stable) <- feature branches <- your work
                          |
                          v
                   Pull Request review
                          |
                          v
                    Merge to main
```

## Coding Standards

- Follow the [Code Style Guide](./CODE_STYLE.md)
- Use `clang-format` for code formatting
- Write meaningful commit messages
- Add documentation for new public APIs
- Include unit tests for new features

## Testing

- Run existing tests before submitting PR
- Add tests for new functionality
- (For internal maintainers) If a certain test is deemed outdated and should be abandoned, it can be deleted after discussion within the project team

## License

By contributing, you agree that your contributions will be licensed under the project's [LICENSE](./LICENSE).

## Questions?

- Open an issue for bugs or feature requests
- Check the [wiki](./wiki) for technical documentation
