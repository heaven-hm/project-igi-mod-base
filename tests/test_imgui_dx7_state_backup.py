#!/usr/bin/env python3
"""Regression guard for D3D7 state save/restore symmetry."""

from pathlib import Path


SOURCE = (
    Path(__file__).resolve().parents[1] / "third_party/imgui/imgui_impl_dx7.cpp"
).read_text(encoding="utf-8")


def test_sampler_states_are_captured_and_restored():
    for state in ("MINFILTER", "MAGFILTER", "MIPFILTER", "ADDRESSU", "ADDRESSV"):
        assert f"GetTextureStageState(0, D3DTSS_{state}" in SOURCE
        assert f"SetTextureStageState(0, D3DTSS_{state}, tss0_" in SOURCE


def test_render_skips_when_capture_fails():
    assert "bool Capture(IDirect3DDevice7* d3d)" in SOURCE
    assert "if (!backup.Capture(d3d))" in SOURCE
    assert "if (FAILED(call))" in SOURCE


if __name__ == "__main__":
    test_sampler_states_are_captured_and_restored()
    test_render_skips_when_capture_fails()
