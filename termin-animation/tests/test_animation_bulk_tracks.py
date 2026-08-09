import uuid

import pytest

from termin.animation import TcAnimationClip


def _clip() -> TcAnimationClip:
    clip = TcAnimationClip.create("BulkTracks", str(uuid.uuid4()))
    clip.set_tps(1.0)
    clip.set_loop(False)
    return clip


def _track(
    *,
    path="translation",
    interpolation="linear",
    components=3,
    times=None,
    values=None,
    target_node_index=7,
):
    return {
        "target_node_index": target_node_index,
        "path": path,
        "interpolation": interpolation,
        "components": components,
        "times": [0.0, 1.0] if times is None else times,
        "values": [0.0] * components + [1.0] * components if values is None else values,
    }


def test_bulk_tracks_own_flat_payload_and_sample_linear_vec3_scale() -> None:
    clip = _clip()
    times = [0.0, 2.0]
    values = [1.0, 2.0, 3.0, 3.0, 6.0, 9.0]
    clip.set_tracks([
        _track(path="scale", times=times, values=values),
    ])

    times[0] = 99.0
    values[0] = 99.0

    assert clip.track_count == 1
    assert clip.duration == pytest.approx(2.0)
    assert clip.tracks == [{
        "target_node_index": 7,
        "path": "scale",
        "interpolation": "linear",
        "components": 3,
        "times": [0.0, 2.0],
        "values": [1.0, 2.0, 3.0, 3.0, 6.0, 9.0],
    }]
    assert clip.sample_track(0, 1.0) == pytest.approx([2.0, 4.0, 6.0])


def test_step_track_holds_previous_value_and_changes_at_key_time() -> None:
    clip = _clip()
    clip.set_tracks([
        _track(
            interpolation="step",
            times=[0.0, 1.0, 2.0],
            values=[0.0, 0.0, 0.0, 10.0, 20.0, 30.0, 40.0, 50.0, 60.0],
        ),
    ])

    assert clip.sample_track(0, 0.75) == pytest.approx([0.0, 0.0, 0.0])
    assert clip.sample_track(0, 1.0) == pytest.approx([10.0, 20.0, 30.0])
    assert clip.sample_track(0, 1.75) == pytest.approx([10.0, 20.0, 30.0])
    assert clip.sample_track(0, 3.0) == pytest.approx([40.0, 50.0, 60.0])


def test_bulk_track_replacement_is_transactional() -> None:
    clip = _clip()
    original = _track(values=[1.0, 2.0, 3.0, 4.0, 5.0, 6.0])
    clip.set_tracks([original])
    version = clip.version

    invalid = _track(times=[0.0, 1.0], values=[1.0, 2.0])
    with pytest.raises(RuntimeError, match="previous payload was preserved"):
        clip.set_tracks([invalid])

    assert clip.version == version
    assert clip.track_count == 1
    assert clip.tracks[0]["values"] == original["values"]
    assert clip.sample_track(0, 0.5) == pytest.approx([2.5, 3.5, 4.5])


@pytest.mark.parametrize(
    "track",
    [
        _track(
            interpolation="cubic_spline",
            times=[0.0, 1.0],
            values=[float(value) for value in range(18)],
        ),
        _track(path="weights", components=2, values=[0.25, 0.75, 0.5, 0.5]),
    ],
)
def test_unsupported_tracks_are_preserved_exactly_but_sampling_fails(track) -> None:
    clip = _clip()
    clip.set_tracks([track])

    assert clip.tracks[0] == track
    with pytest.raises(RuntimeError, match="sampling is unsupported"):
        clip.sample_track(0, 0.5)


def test_legacy_channels_remain_available_and_successful_bulk_replace_is_authoritative() -> None:
    from termin.geombase import Vec3

    clip = _clip()
    clip.set_channels([
        {
            "target_name": "Root",
            "translation_keys": [(0.0, Vec3(0.0, 0.0, 0.0))],
            "rotation_keys": [],
            "scale_keys": [],
        }
    ])
    assert clip.channel_count == 1

    clip.set_tracks([_track()])

    assert clip.channel_count == 0
    assert clip.track_count == 1
