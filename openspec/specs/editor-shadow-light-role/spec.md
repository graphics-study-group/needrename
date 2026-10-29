# editor-shadow-light-role Specification

## Purpose

Presents the scene's unique shadow-caster role inside the editor: which directional light casts cascaded shadows, why the other flagged lights do not, and which shadow settings are editable where, so that shadow configuration is explicit instead of silently resolved by the engine.

## Requirements

### Requirement: Editing a light never modifies another light

Enabling or disabling the `m_cast_shadow` flag on a directional light through the editor's inspector SHALL modify only that light: no other component's fields MAY be changed, so enabling a second light SHALL NOT clear the first one. When several directional lights are flagged, the engine's resolution (the earliest created light casts shadows, every other flagged light is reported with a warning) SHALL be surfaced in the inspector of every flagged light that does not hold the role as a warning that it will not cast shadows.

#### Scenario: Enabling a second light leaves the first one flagged

- **WHEN** the user enables Cast Shadow on directional light B while directional light A holds the role
- **THEN** A's and B's flags both stay set, A keeps casting shadows because it was created first, and the inspector of B warns that it does not cast shadows while A holds the role

#### Scenario: Unchecking the only flagged light releases the role

- **WHEN** the user disables Cast Shadow on the current role holder while no other directional light is flagged
- **THEN** no directional light casts shadows, and shadow rendering stops on the following frames

#### Scenario: Another flagged light takes over

- **WHEN** the user disables Cast Shadow on the current role holder while another flagged directional light exists
- **THEN** the earliest created remaining flagged light casts shadows from the following frames

### Requirement: Role state is visible on every light

A `LightComponent` inspector SHALL state whether that light currently holds the shadow-caster role. A light that does not hold it SHALL show a notice naming the current holder (or stating that no light casts shadows) instead of appearing silently ineffective.

#### Scenario: The holder is labelled

- **WHEN** the user selects the light that currently holds the shadow-caster role
- **THEN** its inspector labels it as the scene's shadow-casting light and shows its shadow settings

#### Scenario: A flagged non-holder warns

- **WHEN** the user selects a directional light whose `m_cast_shadow` flag is set while another light holds the role
- **THEN** its inspector warns that it does not cast shadows and names the light that holds the role

#### Scenario: A non-holder explains itself

- **WHEN** the user selects an unflagged, non-holder directional light
- **THEN** its inspector shows a notice naming the light that currently casts shadows

#### Scenario: No light holds the role

- **WHEN** no directional light in the scene has `m_cast_shadow` set
- **THEN** the inspector of a directional light states that no light currently casts shadows

### Requirement: Shadow settings are editable on the holder only

`m_cascade_count` and `m_shadow_distance` SHALL be editable only on the current role holder. On every other light they SHALL be displayed read-only with their stored values preserved, so the configuration travels with the light when the role passes to another light.

#### Scenario: Settings disabled on a non-holder

- **WHEN** the user views a directional light that does not hold the role
- **THEN** the cascade count and shadow distance fields are visible but not editable

#### Scenario: Settings preserved when the role passes

- **WHEN** the role moves from light A to light B and back to light A
- **THEN** each light's stored cascade count and shadow distance are unchanged
