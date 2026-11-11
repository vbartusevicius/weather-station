"""Shared helpers for the SKiDL circuit sources.

Every part carries sourcing fields (Manufacturer / Manufacturer_Part_Number /
LCSC / Source / Assembly). Resistors and capacitors take their LCSC part from
PASSIVES, checked against the JLCPCB catalogue; a passive without an entry
stops the build. Small passives are 0805 (hand soldering: no 0603 / 0402). Parts JLCPCB does not place are marked Assembly=hand.
"""

from pathlib import Path

from product_spec import spec_path
from skidl import KICAD9 as KICAD
from skidl import Net, Part

REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC_PATH = spec_path()
PROJECT_LIB = Path(__file__).resolve().parent / "lib" / "gateway.kicad_sym"


PLACE_BLOCK = "misc"


def block(name):
    """Set the functional block recorded on subsequently created parts."""
    global PLACE_BLOCK
    PLACE_BLOCK = name


PASSIVES = {
    ("R", "1R", "0805"): ("C25271", "UNI-ROYAL", "0805W8F100KT5E"),
    ("R", "470k", "0805"): ("C17709", "UNI-ROYAL", "0805W8F4703T5E"),
    ("R", "330R", "0805"): ("C17630", "UNI-ROYAL", "0805W8F3300T5E"),
    ("R", "680R", "0805"): ("C17798", "UNI-ROYAL", "0805W8F6800T5E"),
    ("R", "750R", "0805"): ("C17818", "UNI-ROYAL", "0805W8F7500T5E"),
    ("R", "1k", "0805"): ("C17513", "UNI-ROYAL", "0805W8F1001T5E"),
    ("R", "2k2", "0805"): ("C17520", "UNI-ROYAL", "0805W8F2201T5E"),
    ("R", "6k49", "0805"): ("C17769", "UNI-ROYAL", "0805W8F6491T5E"),
    ("R", "82k5", "0805"): ("C276314", "YAGEO", "RC0805FR-0782K5L"),
    ("R", "130k", "0805"): ("C17450", "UNI-ROYAL", "0805W8F1303T5E"),
    ("R", "140k", "0805"): ("C17460", "UNI-ROYAL", "0805W8F1403T5E"),
    ("R", "10R", "0805"): ("C17415", "UNI-ROYAL", "0805W8F100JT5E"),
    ("R", "6k8", "0805"): ("C17772", "UNI-ROYAL", "0805W8F6801T5E"),
    ("R", "47k", "0805"): ("C17713", "UNI-ROYAL", "0805W8F4702T5E"),
    ("R", "1M", "0805"): ("C17514", "UNI-ROYAL", "0805W8F1004T5E"),
    ("R", "4M7", "0805"): ("C17674", "UNI-ROYAL", "0805W8F4704T5E"),
    ("R", "10M", "0805"): ("C26108", "UNI-ROYAL", "0805W8F1005T5E"),
    ("R", "120R", "0805"): ("C17437", "UNI-ROYAL", "0805W8F1200T5E"),
    ("R", "390R", "0805"): ("C17655", "UNI-ROYAL", "0805W8F3900T5E"),
    ("R", "30k", "0805"): ("C17621", "UNI-ROYAL", "0805W8F3002T5E"),
    ("R", "511k", "0805"): ("C17735", "UNI-ROYAL", "0805W8F5113T5E"),
    ("R", "4k7", "0805"): ("C17673", "UNI-ROYAL", "0805W8F4701T5E"),
    ("R", "5k1", "0805"): ("C27834", "UNI-ROYAL", "0805W8F5101T5E"),
    ("R", "10k", "0805"): ("C17414", "UNI-ROYAL", "0805W8F1002T5E"),
    ("R", "20k", "0805"): ("C4328", "UNI-ROYAL", "0805W8F2002T5E"),
    ("R", "22k", "0805"): ("C17560", "UNI-ROYAL", "0805W8F2202T5E"),
    ("R", "51k", "0805"): ("C17737", "UNI-ROYAL", "0805W8F5102T5E"),
    ("R", "10k2", "0805"): ("C17402", "UNI-ROYAL", "0805W8F1022T5E"),
    ("R", "33k2", "0805"): ("C37252", "UNI-ROYAL", "0805W8F3322T5E"),
    ("R", "53k6", "0805"): ("C21398", "UNI-ROYAL", "0805W8F5362T5E"),
    ("R", "200k", "0805"): ("C17539", "UNI-ROYAL", "0805W8F2003T5E"),
    ("R", "100k", "0805"): ("C149504", "UNI-ROYAL", "0805W8F1003T5E"),
    ("R", "120k", "0805"): ("C17436", "UNI-ROYAL", "0805W8F1203T5E"),
    ("R", "390k", "0805"): ("C17656", "UNI-ROYAL", "0805W8F3903T5E"),
    ("R", "0R1", "1206"): ("C25334", "UNI-ROYAL", "1206W4F100LT5E"),
    ("R", "3R3", "1206"): ("C17945", "UNI-ROYAL", "1206W4F330KT5E"),
    ("R", "120R", "1206"): ("C17909", "UNI-ROYAL", "1206W4F1200T5E"),
    ("R", "680R", "1206"): ("C48533685", "FOJAN", "FRP1206F6800TS"),     # 1/2 W 1206 (EasyEDA data, 58k stock)
    ("R", "120R", "2512"): ("C7276687", "YAGEO", "RC2512FK-7W120RL"),    # 2 W "double power" 2512
    ("R", "330R", "1206"): ("C17930", "UNI-ROYAL", "1206W4F3300T5E"),
    ("R", "470R", "1206"): ("C4502", "UNI-ROYAL", "1206W4F4700T5E"),
    ("R", "27R", "2512"): ("C2791259", "UNI-ROYAL", "HP122WJ0270T4E"),   # 2 W high-power 2512
    ("R", "100R", "2512"): ("C24920", "UNI-ROYAL", "25121WJ0101T4E"),
    ("C", "18p C0G 50V", "0805"): ("C113825", "YAGEO", "CC0805JRNPO9BN180"),
    ("C", "22p C0G 50V", "0805"): ("C105623", "YAGEO", "CC0805JRNPO9BN220"),
    ("C", "3n3 50V", "0805"): ("C107149", "YAGEO", "CC0805KRX7R9BB332"),
    ("C", "470p C0G 50V", "0805"): ("C62771", "YAGEO", "CC0805JRNPO9BN471"),
    ("C", "47n 50V", "0805"): ("C107154", "YAGEO", "CC0805KRX7R9BB473"),
    ("C", "33n 50V", "0805"): ("C107150", "YAGEO", "CC0805KRX7R9BB333"),
    ("C", "18n 50V", "0805"): ("C520032", "YAGEO", "CC0805JRX7R9BB183"),
    ("C", "22n 50V", "0805"): ("C107147", "YAGEO", "CC0805KRX7R9BB223"),
    ("C", "1n 50V", "0805"): ("C94121", "YAGEO", "CC0805KRX7R9BB102"),
    ("C", "33p C0G 50V", "0805"): ("C107115", "YAGEO", "CC0805JRNPO9BN330"),
    ("C", "220p C0G 50V", "0805"): ("C107112", "YAGEO", "CC0805JRNPO9BN221"),
    ("C", "10n 50V", "0805"): ("C83170", "YAGEO", "CC0805KRX7R9BB103"),
    ("C", "68p C0G 50V", "0805"): ("C107120", "YAGEO", "CC0805JRNPO9BN680"),
    ("C", "100n 100V", "0805"): ("C106243", "YAGEO", "CC0805KKX7R0BB104"),
    ("C", "2u2 100V", "1206"): ("C170101", "Samsung", "CL31B225KCHSNNE"),
    ("C", "100n 50V", "0805"): ("C49678", "YAGEO", "CC0805KRX7R9BB104"),
    ("C", "220n 50V", "0805"): ("C107131", "YAGEO", "CC0805KKX7R9BB224"),
    ("C", "10u 10V", "0805"): ("C17024", "Samsung", "CL21A106KPFNNNE"),
    ("C", "1u 50V", "0805"): ("C28323", "Samsung", "CL21B105KBFNNNE"),
    ("C", "10u 25V", "0805"): ("C15850", "Samsung", "CL21A106KAYNNNE"),
    ("C", "10u 50V", "0805"): ("C440198", "Murata", "GRM21BR61H106KE43L"),
    ("C", "22u 25V", "0805"): ("C45783", "Samsung", "CL21A226MAQNNNE"),
    ("C", "47u 10V", "1206"): ("C96123", "Samsung", "CL31A476MPHNNNE"),
    ("C", "22u 25V", "1206"): ("C12891", "Samsung", "CL31A226KAHNNNE"),
}


def passive_source(symbol, value, footprint):
    package = footprint.split(":")[1].split("_")[1]
    key = (symbol, value.split(" 1%")[0], package)
    if key not in PASSIVES:
        raise KeyError(f"no verified LCSC part for {key}; add it to PASSIVES")
    lcsc, mfr, mpn = PASSIVES[key]
    return {"Manufacturer": mfr, "Manufacturer_Part_Number": mpn, "LCSC": lcsc}


def part(lib, symbol, value="", footprint="", fields=None, ref=None):
    """Instantiate a stock or project-library part with sourcing fields."""
    lib_ref = str(PROJECT_LIB) if lib == "gateway" else lib
    p = Part(lib_ref, symbol, tool=KICAD, tag=PLACE_BLOCK, **({"ref": ref} if ref else {}))
    if value:
        p.value = value
    if footprint:
        p.footprint = footprint
    p.fields["Place"] = PLACE_BLOCK
    p.fields["Assembly"] = "jlc"
    for key, val in (fields or {}).items():
        p.fields[key] = val
    if lib == "Device" and symbol in ("R", "C"):
        for key, val in passive_source(symbol, value, footprint).items():
            p.fields[key] = val
    return p


def src(mpn="", mfr="", lcsc="", source="", hand=False, footprint_ref="", board_only=False, stock_optional=False):
    """Sourcing fields. `hand`: fitted by hand (not JLC-placed); `board_only`: copper-only feature (test pad,
    programming pad) with nothing to buy or place."""
    fields = {"Manufacturer": mfr, "Manufacturer_Part_Number": mpn, "LCSC": lcsc,
              "Source": source, "Footprint_Reference": footprint_ref}
    if board_only:
        fields["Assembly"] = "none"
    if hand:
        fields["Assembly"] = "hand"
    if stock_optional:   # JLC-placed when in stock; skipped at assembly otherwise (release does not fail on stock)
        fields["Stock_Optional"] = "yes"
    return fields


def power(name):
    net = Net(name)
    sym = Part("power", name, tool=KICAD, tag="power")
    sym.footprint = ""
    sym["1"] += net
    return net


def power_flag(net):
    flag = Part("power", "PWR_FLAG", tool=KICAD, tag="power")
    flag.footprint = ""
    flag["1"] += net
