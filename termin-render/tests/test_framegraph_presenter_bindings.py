from pathlib import Path


def test_framegraph_presenter_binds_capture_texture_by_resource_name():
    source = (
        Path(__file__).resolve().parents[1]
        / "src"
        / "frame_graph_debugger_core.cpp"
    ).read_text(encoding="utf-8")

    assert "tc_shader_set_resource_layout(shader, &u_tex, 1)" in source
    assert "use_shader_resource_layout(tc_shader_get(shader_handle_))" in source
    assert 'bind_texture("u_tex", capture_tex)' in source
    assert "bind_sampled_texture(0" not in source
