## MODIFIED Requirements

### Requirement: Install components partition the data

Installation SHALL use components: `runtime` (engine/application binaries, Python binding files, and the required `builtin_assets/` data), `apps-data` (project templates), `examples` (example executables), `examples-data` (sample content used by installed examples), `test-data` (test executables and their data, only with `ENGINE_INSTALL_TESTS=ON`), and `sdk` (development files for external projects). `assets/img/` (documentation images) SHALL NOT be installed by any component.

A `runtime`-only install SHALL be a working distribution for engine binaries plus built-in data and SHALL NOT require example executables to be built. An `examples` install SHALL place the example executables in the same `bin/` directory used by an all-component install.

#### Scenario: Runtime-only install works

- **WHEN** only the `runtime` component is installed
- **THEN** engine binaries and `share/needrename/builtin_assets/` are present
- **AND** no `assets/img/` content is present anywhere in the prefix

#### Scenario: Examples have an independent component

- **WHEN** only the `examples` component is selected from a configured build with examples enabled
- **THEN** its install rules cover example executables rather than treating them as core runtime binaries
- **AND** installing all components still places those executables in `bin/`

#### Scenario: Test data follows its switch

- **WHEN** `ENGINE_INSTALL_TESTS=OFF`
- **THEN** installing all components installs no test executable and no `test_data/`
- **WHEN** `ENGINE_INSTALL_TESTS=ON`
- **THEN** test executables install into `bin/` and their data into `share/needrename/test_data/`
