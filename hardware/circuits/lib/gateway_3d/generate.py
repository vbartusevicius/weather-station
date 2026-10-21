"""Box models (STEP) for the project parts KiCad ships no 3D model for.

The STEP files next to this script are library assets referenced by
hardware/scripts/layout.py; rerun after changing a body size (`cad` container):

    python3 hardware/circuits/lib/gateway_3d/generate.py
"""

from pathlib import Path

import cadquery as cq

OUT_DIR = Path(__file__).resolve().parent

PARTS = {
    "XIAO_ESP32C6": (17.8, 22.0, 3.5),
    "TerminalBlock_Kefa_KF142R-5.08-2P": (11.7, 14.2, 11.1),
    "Fuse_1812_4532Metric": (4.5, 3.2, 1.2),
    "LED_SK6805-EC15_1.5x1.5mm": (1.5, 1.5, 0.65),
    "Sensirion_SHT45_DFN-4": (1.5, 1.5, 0.5),
    "Bosch_BMP581_LGA-10": (2.0, 2.0, 0.75),
    "Bosch_BMP585_LGA-9": (3.25, 3.25, 1.96),
    "TI_ADS1115_VSSOP-10": (3.0, 3.0, 1.1),
    "Sensirion_SGP41_DFN-6": (2.4, 2.4, 0.9),
    "Sensirion_SCD41_LGA-20": (10.1, 10.1, 6.5),
    "Vishay_VEML7700-TT": (6.8, 2.35, 3.0),
    "LiteOn_LTR-390UV-01": (2.0, 2.0, 0.7),
    "AMS_TSL25911FN": (2.0, 2.4, 0.65),
    "GenUV_GUVB-C31SM": (2.0, 2.3, 1.4),
    "ST_LIS2MDLTR_LGA-12": (2.0, 2.0, 0.7),
    "ST_LSM6DS3TR-C_LGA-14": (2.5, 3.0, 0.86),
    "ScioSense_AS3935_QFN-16": (4.0, 4.0, 0.9),
    "TI_CD4052BM96_SOIC-16": (9.9, 6.0, 1.65),
    "TI_SN74LVC2G04_SOT-23-6": (2.9, 2.8, 1.15),
    "TI_TLV9062IDR_SOIC-8": (4.9, 6.0, 1.65),
    "ON_LM393DR2G_SOIC-8": (4.9, 6.0, 1.65),
    "AOS_AO3400A_SOT-23": (2.9, 2.4, 1.1),
    "JST_B6B-ZR-SM4-TF": (12.0, 4.6, 4.5),
    "JST_BM02B-SRSS-TB": (4.0, 2.9, 4.25),
    "HX_PZ2.54-1x4P-SMD": (10.16, 5.0, 2.5),
    "HX_PM2.54-1x4P-RA-SMD": (10.16, 8.5, 2.5),
    "TerminalBlock_Kefa_KF250-3.5-4P": (15.5, 12.0, 11.0),
    "MEMSIC_MMC5603NJ_WLCSP-4": (0.8, 0.8, 0.5),
}


def make_box_model(name, width, depth, height):
    result = cq.Workplane("XY").box(width, depth, height, centered=(True, True, False))
    step_path = OUT_DIR / f"{name}.step"
    cq.exporters.export(result, str(step_path))
    print(f"  {name}: {width}x{depth}x{height} mm -> {step_path.name}")
    return step_path


def main():
    print("Generating 3D box models:")
    for name, (w, d, h) in PARTS.items():
        make_box_model(name, w, d, h)
    print(f"\nAll models saved to {OUT_DIR}")


if __name__ == "__main__":
    main()
