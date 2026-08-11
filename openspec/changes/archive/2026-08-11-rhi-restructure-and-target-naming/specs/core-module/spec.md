# core-module Specification (delta)

## MODIFIED Requirements

### Requirement: Core is a standalone shared library
The engine SHALL compile Core as a separate `EngineCore.dll` shared library that does not depend on any other engine module except Reflection.dll.

#### Scenario: Core.dll has no Framework or Render dependencies
- **WHEN** the build system links EngineCore.dll
- **THEN** EngineCore.dll SHALL NOT link to Framework.dll, Render.dll, Asset.dll, Physics.dll, or UI.dll

#### Scenario: Core.dll links only Reflection and external libraries
- **WHEN** the build system links EngineCore.dll
- **THEN** EngineCore.dll SHALL link only to Reflection.dll, SDL3, glm, and system libraries (rpcrt4 on Windows, uuid on Unix)
