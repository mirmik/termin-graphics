from array import array

import numpy as np
import pytest

from termin.render_framework import RenderContext


def _matrix_memoryview(values):
    return memoryview(array("f", values)).cast("B").cast("f", shape=(4, 4))


def test_render_context_accepts_numpy_matrix_inputs():
    values = np.arange(16, dtype=np.float32).reshape((4, 4))

    ctx = RenderContext(view=values, projection=values, model=values)

    np.testing.assert_allclose(ctx.view.to_rows(), values)
    np.testing.assert_allclose(ctx.projection.to_rows(), values)
    np.testing.assert_allclose(ctx.model.to_rows(), values)


def test_render_context_accepts_buffer_compatible_matrix_inputs():
    values = [float(i) for i in range(16)]
    matrix = _matrix_memoryview(values)

    ctx = RenderContext(view=matrix, projection=matrix, model=matrix)

    expected = [values[row * 4:row * 4 + 4] for row in range(4)]
    np.testing.assert_allclose(ctx.view.to_rows(), expected)
    np.testing.assert_allclose(ctx.projection.to_rows(), expected)
    np.testing.assert_allclose(ctx.model.to_rows(), expected)


def test_render_context_rejects_wrong_matrix_dtype():
    values = array("d", [float(i) for i in range(16)])
    matrix = memoryview(values).cast("B").cast("d", shape=(4, 4))

    with pytest.raises(RuntimeError, match="4x4 C-contiguous float32"):
        RenderContext(view=matrix)
