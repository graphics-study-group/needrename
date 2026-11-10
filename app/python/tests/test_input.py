"""Keyboard queries: the API exists, and headless mode reports nothing pressed.

The windowed path (real SDL key events) needs a window and a person at the keyboard,
so these cases pin down the contract a script can rely on: an app without a window
answers "not pressed" instead of raising, and the key names are the engine input
map's names rather than free-form text.
"""

from __future__ import annotations

from conftest import assert_scenario_passed, run_scenario, scenario_result

HEADLESS_SCENARIO = """
import needrename_sim as s

def main():
    app = s.PhysicsApp.create(s.CreateInfo(mode=s.AppMode.physics_only))
    app.add_box(s.BoxDesc(position=s.Vec3(0.0, 0.0, 0.0), half_extents=s.Vec3(0.1, 0.1, 0.1)))
    app.commit_scene()

    keys = ["w", "space", "left", "f1", "left shift"]
    print("RESULT:" + __import__("json").dumps({
        "down": [app.is_key_down(key) for key in keys],
        "pressed": [app.is_key_pressed(key) for key in keys],
        "released": [app.is_key_released(key) for key in keys],
        "all_down": app.keys_down(),
        "should_quit": app.should_quit,
        "unknown_key_down": app.is_key_down("definitely not a key"),
        "upper_case": app.is_key_down("W"),
    }))

main()
"""


def test_headless_keyboard_queries_report_nothing_pressed() -> None:
    result = run_scenario(HEADLESS_SCENARIO)
    assert_scenario_passed(result)
    observed = scenario_result(result)

    assert observed["down"] == [False] * 5
    assert observed["pressed"] == [False] * 5
    assert observed["released"] == [False] * 5
    assert observed["all_down"] == []
    # Headless has no window-close event source.
    assert observed["should_quit"] is False
    # An unknown name is a miss, not an error, and names are case-insensitive.
    assert observed["unknown_key_down"] is False
    assert observed["upper_case"] is False
