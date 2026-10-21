"""Shared helpers for the SKiDL circuit sources.

Every part carries sourcing fields (Manufacturer / Manufacturer_Part_Number /
LCSC / Source / Assembly). Resistors and capacitors take their LCSC part from
PASSIVES, checked against the JLCPCB catalogue; a passive without an entry
stops the build. Parts JLCPCB does not place are marked Assembly=hand.
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
    ("R", "1R", "0603"): ("C22936", "UNI-ROYAL", "0603WAF100KT5E"),
    ("R", "470k", "0603"): ("C23178", "UNI-ROYAL", "0603WAF4703T5E"),
    ("R", "330R", "0603"): ("C23138", "UNI-ROYAL", "0603WAF3300T5E"),
    ("R", "680R", "0603"): ("C23228", "UNI-ROYAL", "0603WAF6800T5E"),
    ("R", "750R", "0603"): ("C23241", "UNI-ROYAL", "0603WAF7500T5E"),
    ("R", "1k", "0603"): ("C21190", "UNI-ROYAL", "0603WAF1001T5E"),
    ("R", "2k2", "0603"): ("C4190", "UNI-ROYAL", "0603WAF2201T5E"),
    ("R", "6k49", "0603"): ("C23088", "UNI-ROYAL", "0603WAF6491T5E"),
    ("R", "82k5", "0603"): ("C23251", "UNI-ROYAL", "0603WAF8252T5E"),
    ("R", "130k", "0603"): ("C22795", "UNI-ROYAL", "0603WAF1303T5E"),
    ("R", "140k", "0603"): ("C22875", "UNI-ROYAL", "0603WAF1403T5E"),
    ("R", "10R", "0603"): ("C22859", "UNI-ROYAL", "0603WAF100JT5E"),
    ("R", "6k8", "0603"): ("C23212", "UNI-ROYAL", "0603WAF6801T5E"),
    ("R", "47k", "0603"): ("C25819", "UNI-ROYAL", "0603WAF4702T5E"),
    ("R", "1M", "0603"): ("C22935", "UNI-ROYAL", "0603WAF1004T5E"),
    ("R", "4M7", "0603"): ("C177475", "Ralec", "RCT034M7FLF"),
    ("R", "120R", "0603"): ("C22787", "UNI-ROYAL", "0603WAF1200T5E"),
    ("R", "390R", "0603"): ("C23151", "UNI-ROYAL", "0603WAF3900T5E"),
    ("R", "30k", "0603"): ("C22984", "UNI-ROYAL", "0603WAF3002T5E"),
    ("R", "511k", "0603"): ("C23194", "UNI-ROYAL", "0603WAF5113T5E"),
    ("R", "4k7", "0603"): ("C23162", "UNI-ROYAL", "0603WAF4701T5E"),
    ("R", "5k1", "0603"): ("C23186", "UNI-ROYAL", "0603WAF5101T5E"),
    ("R", "10k", "0603"): ("C25804", "UNI-ROYAL", "0603WAF1002T5E"),
    ("R", "20k", "0603"): ("C4184", "UNI-ROYAL", "0603WAF2002T5E"),
    ("R", "22k", "0603"): ("C31850", "UNI-ROYAL", "0603WAF2202T5E"),
    ("R", "51k", "0603"): ("C23196", "UNI-ROYAL", "0603WAF5102T5E"),
    ("R", "10k2", "0603"): ("C22772", "UNI-ROYAL", "0603WAF1022T5E"),
    ("R", "33k2", "0603"): ("C23003", "UNI-ROYAL", "0603WAF3322T5E"),
    ("R", "53k6", "0603"): ("C23074", "UNI-ROYAL", "0603WAF5362T5E"),
    ("R", "200k", "0603"): ("C25811", "UNI-ROYAL", "0603WAF2003T5E"),
    ("R", "100k", "0603"): ("C25803", "UNI-ROYAL", "0603WAF1003T5E"),
    ("R", "120k", "0603"): ("C25808", "UNI-ROYAL", "0603WAF1203T5E"),
    ("R", "390k", "0603"): ("C23150", "UNI-ROYAL", "0603WAF3903T5E"),
    ("R", "0R1", "1206"): ("C25334", "UNI-ROYAL", "1206W4F100LT5E"),
    ("R", "3R3", "1206"): ("C17945", "UNI-ROYAL", "1206W4F330KT5E"),
    ("R", "120R", "1206"): ("C17909", "UNI-ROYAL", "1206W4F1200T5E"),
    ("R", "680R", "1206"): ("C48533685", "FOJAN", "FRP1206F6800TS"),     # 1/2 W 1206 (EasyEDA data, 58k stock)
    ("R", "120R", "2512"): ("C7276687", "YAGEO", "RC2512FK-7W120RL"),    # 2 W "double power" 2512
    ("R", "330R", "1206"): ("C17930", "UNI-ROYAL", "1206W4F3300T5E"),
    ("R", "470R", "1206"): ("C4502", "UNI-ROYAL", "1206W4F4700T5E"),
    ("R", "27R", "2512"): ("C2791259", "UNI-ROYAL", "HP122WJ0270T4E"),   # 2 W high-power 2512
    ("R", "100R", "2512"): ("C24920", "UNI-ROYAL", "25121WJ0101T4E"),
    ("C", "18p C0G 50V", "0603"): ("C1647", "Samsung", "CL10C180JB8NNNC"),
    ("C", "22p C0G 50V", "0603"): ("C1653", "Samsung", "CL10C220JB8NNNC"),
    ("C", "3n3 50V", "0603"): ("C1613", "Samsung", "CL10B332KB8NNNC"),
    ("C", "470p C0G 50V", "0603"): ("C27694", "Samsung", "CL10C471JB8NNNC"),
    ("C", "47n 50V", "0603"): ("C1622", "Samsung", "CL10B473KB8NNNC"),
    ("C", "33n 50V", "0603"): ("C106856", "YAGEO", "CC0603KRX7R9BB333"),
    ("C", "18n 50V", "0603"): ("C107079", "YAGEO", "CC0603KRX7R9BB183"),
    ("C", "22n 50V", "0603"): ("C106222", "YAGEO", "CC0603KRX7R9BB223"),
    ("C", "1n 50V", "0603"): ("C100040", "YAGEO", "CC0603KRX7R9BB102"),
    ("C", "33p C0G 50V", "0603"): ("C1663", "Samsung", "CL10C330JB8NNNC"),
    ("C", "220p C0G 50V", "0603"): ("C27675", "Samsung", "CL10C221JB8NNNC"),
    ("C", "10n 50V", "0603"): ("C1589", "Samsung", "CL10B103KB8NNNC"),
    ("C", "68p C0G 50V", "0603"): ("C28262", "Samsung", "CL10C680JB8NNNC"),
    ("C", "100n 100V", "0603"): ("C113803", "YAGEO", "CC0603KRX7R0BB104"),
    ("C", "2u2 100V", "1206"): ("C170101", "Samsung", "CL31B225KCHSNNE"),
    ("C", "100n 50V", "0603"): ("C14663", "YAGEO", "CC0603KRX7R9BB104"),
    ("C", "220n 50V", "0603"): ("C64705", "Samsung", "CL10B224KB8NNNC"),
    ("C", "1u 50V", "0603"): ("C15849", "Samsung", "CL10A105KB8NNNC"),
    ("C", "10u 10V", "0603"): ("C19702", "Samsung", "CL10A106KP8NNNC"),
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


def src(mpn="", mfr="", lcsc="", source="", hand=False, footprint_ref="", board_only=False):
    """Sourcing fields. `hand`: fitted by hand (not JLC-placed); `board_only`: copper-only feature (test pad,
    programming pad) with nothing to buy or place."""
    fields = {"Manufacturer": mfr, "Manufacturer_Part_Number": mpn, "LCSC": lcsc,
              "Source": source, "Footprint_Reference": footprint_ref}
    if board_only:
        fields["Assembly"] = "none"
    if hand:
        fields["Assembly"] = "hand"
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
