from __future__ import annotations

import unittest

from tools.long_context_matrix import (
    SCHEMA,
    parse_contexts,
    parse_depths,
    render_context_args,
    validate_config,
)


class LongContextMatrixTests(unittest.TestCase):
    def test_parse_contexts_rejects_duplicates_and_out_of_range_values(self):
        self.assertEqual(parse_contexts("131072, 262144"), [131072, 262144])
        with self.assertRaisesRegex(ValueError, "duplicado"):
            parse_contexts("131072,131072")
        with self.assertRaisesRegex(ValueError, "fuera de rango"):
            parse_contexts("1048577")

    def test_render_context_args_rewrites_ctx_size_fit_ctx_and_placeholder(self):
        rendered = render_context_args([
            "--ctx-size", "131072", "--fit-ctx=131072", "--tag", "ctx-{context}"
        ], 262144)
        self.assertIn("--ctx-size", rendered)
        self.assertEqual(rendered[rendered.index("--ctx-size") + 1], "262144")
        self.assertIn("--fit-ctx=262144", rendered)
        self.assertIn("ctx-262144", rendered)

    def test_render_context_args_adds_allocation_when_recipe_omits_it(self):
        rendered = render_context_args(["--flash-attn", "on"], 524288)
        self.assertEqual(rendered[-2:], ["--ctx-size", "524288"])

    def test_parse_depths_rejects_nan_and_accepts_edge_depths(self):
        self.assertEqual(parse_depths("0, 0.5, 1"), [0.0, 0.5, 1.0])
        with self.assertRaisesRegex(ValueError, "fuera de rango"):
            parse_depths("nan")

    def test_validate_config_accepts_lid_matrix_shape(self):
        config = validate_config({
            "schema": SCHEMA,
            "serverExe": "llama-server",
            "modelPath": "model.gguf",
            "commonArgs": ["--fit-ctx", "{context}"],
            "variant": {"id": "lid", "args": ["--cache-type-k", "f16"], "env": {"X": "1"}},
            "contexts": [131072, 262144, 524288, 1048576],
        })
        self.assertEqual(config["contexts"], [131072, 262144, 524288, 1048576])
        self.assertEqual(config["variant"]["id"], "lid")

    def test_validate_config_requires_one_variant(self):
        with self.assertRaisesRegex(ValueError, "variant"):
            validate_config({
                "serverExe": "llama-server",
                "modelPath": "model.gguf",
                "contexts": [131072],
            })


if __name__ == "__main__":
    unittest.main()
