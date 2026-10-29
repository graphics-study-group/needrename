## Purpose

Makes a renderer's shadow-casting flag decide whether that renderer is drawn into shadow maps, so individual objects can opt out of shadow casting instead of every renderer casting unconditionally.

## ADDED Requirements

### Requirement: Shadow-cast flag controls shadow map participation

A renderer SHALL be drawn into shadow maps if and only if its `m_cast_shadow` flag is true. Renderers excluded by the flag SHALL still be drawn in the lit pass and SHALL NOT affect the shadows cast by other renderers.

#### Scenario: Renderer opted out of shadow casting
- **WHEN** a renderer has `m_cast_shadow` set to false and a shadow-casting light illuminates the scene
- **THEN** that renderer contributes no shadow to any shadow map, while its own lit appearance and other renderers' shadows are unchanged

#### Scenario: Renderer opted in
- **WHEN** a renderer has `m_cast_shadow` set to true
- **THEN** it is drawn into the shadow maps of the shadow-casting lights that cover it

### Requirement: Default value preserves existing appearance

The default value of the shadow-cast flag SHALL be true, so renderers created programmatically or without an explicit setting continue to cast shadows once the flag becomes effective.

#### Scenario: Newly created renderer casts shadows
- **WHEN** a static mesh component is created without explicitly setting the shadow-cast flag
- **THEN** the renderer casts shadows

#### Scenario: Serialized renderer that stored an explicit false
- **WHEN** a serialized scene stores `m_cast_shadow` as false (written while the flag had no effect)
- **THEN** that renderer no longer casts shadows, and the behaviour change is documented as a migration note for existing content
