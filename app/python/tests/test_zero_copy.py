"""Zero-copy readback semantics: arrays view engine memory, not copies."""

from __future__ import annotations

from conftest import assert_scenario_passed, run_scenario, scenario_result

SCENARIO = """
import json
import numpy as np
import needrename_sim as s

def main():
    app = s.PhysicsApp.create(s.CreateInfo(mode=s.AppMode.offscreen, resol_x=64, resol_y=48))
    app.add_box(
        s.BoxDesc(position=s.Vec3(0.0, 0.0, 5.0), half_extents=s.Vec3(0.5, 0.5, 0.5))
    )
    app.commit_scene()

    view = app.get_body_states()
    positions = view.positions
    before = positions.copy()
    step_id_before = view.step_id

    app.step()

    result = {
        "positions_shape": list(positions.shape),
        "positions_dtype": str(positions.dtype),
        "rotations_shape": list(view.rotations.shape),
        "slot_indices_shape": list(view.slot_indices.shape),
        "slot_indices_dtype": str(view.slot_indices.dtype),
        "com_offsets_shape": list(view.com_offsets.shape),
        # A copy would leave `positions` stale; shared memory reflects the step.
        "mutated_in_place": bool((positions != before).any()),
        "step_id_before": step_id_before,
        "step_id_after": view.step_id,
        "app_step_id": app.step_id,
    }

    app.set_render_readback_enabled(True)
    app.render_next_frame()
    output = app.get_render_output()
    pixels = output.pixels
    pointer = pixels.__array_interface__["data"][0]

    app.render_next_frame()
    output_again = app.get_render_output()
    result.update(
        {
            "pixels_shape": list(pixels.shape),
            "pixels_dtype": str(pixels.dtype),
            "pixels_bytes": int(pixels.nbytes),
            "width": int(output.width),
            "height": int(output.height),
            # Same staging buffer, so the pointer is stable across frames.
            "pixel_pointer_stable": pointer == pixels.__array_interface__["data"][0],
            "second_frame_id": int(output_again.frame_id),
            "first_frame_id": int(output.frame_id),
        }
    )

    print("RESULT:" + json.dumps(result))

main()
"""

MEMORY_SAFETY_SCENARIO = """
import gc
import json
import needrename_sim as s

def main():
    app = s.PhysicsApp.create(s.CreateInfo(mode=s.AppMode.physics_only))
    app.add_box(
        s.BoxDesc(position=s.Vec3(0.0, 0.0, 5.0), half_extents=s.Vec3(0.5, 0.5, 0.5))
    )
    app.commit_scene()
    app.step()

    view = app.get_body_states()
    positions = view.positions
    expected = positions.copy()

    # Drop the only explicit reference to the app: the live view must keep it
    # (and its readback memory) alive, so this must neither crash nor read freed
    # memory.
    del app
    gc.collect()

    result = {
        "still_readable": bool((positions == expected).all()),
        "sum": float(positions.sum()),
        "step_id": view.step_id,
    }
    print("RESULT:" + json.dumps(result))

main()
"""


def test_body_state_arrays_are_zero_copy_views() -> None:
    result = run_scenario(SCENARIO)
    assert_scenario_passed(result)
    observed = scenario_result(result)

    assert observed["positions_shape"] == [1, 4]
    assert observed["positions_dtype"] == "float32"
    assert observed["rotations_shape"] == [1, 4]
    assert observed["slot_indices_shape"] == [1]
    assert observed["slot_indices_dtype"] == "uint32"
    assert observed["com_offsets_shape"] == [1, 3]
    assert observed["mutated_in_place"] is True
    assert observed["step_id_before"] == 0
    assert observed["step_id_after"] == 1
    assert observed["app_step_id"] == 1


def test_render_output_pixels_are_zero_copy_views() -> None:
    result = run_scenario(SCENARIO)
    assert_scenario_passed(result)
    observed = scenario_result(result)

    assert observed["pixels_shape"] == [observed["height"], observed["width"], 4]
    assert observed["pixels_dtype"] == "uint8"
    assert observed["pixels_bytes"] == 64 * 48 * 4
    assert observed["pixel_pointer_stable"] is True
    assert observed["second_frame_id"] > observed["first_frame_id"]


def test_views_keep_the_app_alive_after_destruction() -> None:
    result = run_scenario(MEMORY_SAFETY_SCENARIO)
    assert_scenario_passed(result)
    observed = scenario_result(result)
    assert observed["still_readable"] is True
    assert observed["step_id"] == 1
