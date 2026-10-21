"""ngspice check of the crown v2 ultrasonic drive / T-R isolation / receive chain (hardware/sim/crown_us_link.py)."""

import shutil
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sim"))

pytestmark = pytest.mark.skipif(shutil.which("ngspice") is None, reason="ngspice not installed (eda container)")


@pytest.fixture(scope="module")
def sim():
    import crown_us_link
    return crown_us_link.run()


def test_h_bridge_drive_level_and_rail(sim):
    assert 20.0 <= sim["tx_drive_vpp"] <= 24.0      # 2 x 12 V minus two diode drops
    assert sim["vdrv_min_v"] > 11.0                  # 10R / 22 uF filter holds the rail through the burst


def test_receive_path_is_isolated_and_protected(sim):
    assert sim["mux_injection_ma"] < 1.0             # 74HC4052 input diodes rated +/-20 mA
    # the diodes keep the idle driver from shorting the echo: >= 40 dB better than without them
    assert sim["rx_mux_mvpp"] > 100.0 * sim["rx_without_isolation_mvpp"]
    assert sim["rx_mux_mvpp"] > 0.8 * 1e3 * sim["rx_emf_vpp_scaled"]   # < 2 dB loss from EMF to the mux
    # electrical crosstalk while another transducer fires, and the residue before the echo, are far below the echo
    assert sim["rx_during_tx_mvpp"] < 0.1 * sim["rx_mux_mvpp"]
    assert sim["rx_before_echo_mvpp"] < 0.03 * sim["rx_mux_mvpp"]


def test_preamp_leaves_pga_headroom(sim):
    # ADC span 3.3 V: the preamp output must fit at the lowest PGA gain (x2) with margin
    assert 2.0 * sim["preamp_out_mvpp"] < 2600.0
