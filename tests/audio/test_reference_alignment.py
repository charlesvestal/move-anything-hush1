def test_cutoff_curve_matches_reference(with_metrics):
    m = with_metrics("tests/fixtures/reference/cutoff_sweep.json")
    assert m["spectral_rmse_db"] < 3.0
    assert m["env_decay_error_ms"] < 25.0
