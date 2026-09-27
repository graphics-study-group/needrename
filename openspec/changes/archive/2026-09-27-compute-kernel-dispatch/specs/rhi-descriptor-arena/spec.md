# rhi-descriptor-arena

## MODIFIED Requirements

### Requirement: Descriptor-set layouts are shared

The arena SHALL resolve descriptor-set layouts from the device's immutable resource cache, and every descriptor set it allocates SHALL be allocated against a layout it resolved that way. A caller SHALL obtain the layout it hands to the arena from the arena itself, so that equal layout descriptions resolve to one layout object and the layout a pipeline layout is built over is the layout a set is allocated against.

A compute kernel SHALL obtain its descriptor-set layout from the arena when it is created, build its pipeline layout over that resolved object, and hand that same object back on every acquisition it makes; no compute pipeline object SHALL resolve a layout description of its own. The compute pipeline object the second scenario below speaks of is therefore the kernel: `ComputeStage` is deleted by this change and the kernel is its replacement, and the scenario keeps its name because a modified requirement cannot rename one.

#### Scenario: Equal layouts resolve to one object

- **WHEN** two callers resolve the same descriptor set layout description through the arena
- **THEN** both receive the same layout object

#### Scenario: A compute stage's allocation layout is its pipeline layout's layout

- **WHEN** a compute kernel is created and a set is later acquired for the same bindings
- **THEN** both name the layout object the arena resolved for that description
- **AND** the kernel hands the arena that same object rather than a description it resolves again
- **AND** no separate layout is created outside the cache
