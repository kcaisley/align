import json

import pytest

from align.pnr import hanan2

PLACEMENT = {
    "modules": [
        {
            "concrete_name": "TOP_0",
            "constraints": [
                {"constraint": "DoNotRoute", "nets": ["VDD", "VSS"]},
                {"constraint": "SymmetricNets", "net1": "INP", "net2": "INN", "direction": "V"},
                {"constraint": "SymmetricNets", "net1": "VDD", "net2": "VSS", "direction": "V"},
            ],
        },
        {"concrete_name": "LEAF_0", "constraints": []},
    ]
}


def test_ndr_adds_do_not_route_and_symmetric_nets(tmp_path):
    out = hanan2.ndr("", PLACEMENT, tmp_path / "ndr.json")
    entries = json.loads((tmp_path / "ndr.json").read_text())
    assert out == str(tmp_path / "ndr.json")
    assert entries == [{"module": "TOP_0", "do_not_route": ["VDD", "VSS"], "symmetric_nets": [["INP", "INN"]]}]


def test_ndr_keeps_the_designs_own_entries(tmp_path):
    own = tmp_path / "own.json"
    own.write_text(json.dumps([{"module": "TOP_0", "routing_order": ["CLK"], "symmetric_nets": [["INN", "INP"]]}]))
    hanan2.ndr(own, PLACEMENT, tmp_path / "ndr.json")
    (entry,) = json.loads((tmp_path / "ndr.json").read_text())
    assert entry["routing_order"] == ["CLK"]
    assert entry["symmetric_nets"] == [["INN", "INP"]]  # the stated pair, not repeated reversed


def test_ndr_nothing_to_say(tmp_path):
    assert hanan2.ndr("", {"modules": [{"concrete_name": "A", "constraints": []}]}, tmp_path / "n.json") == ""


def test_lef_states_one_database_unit_per_micron(tmp_path):
    text = "MACRO A\n  UNITS\n    DATABASE MICRONS UNITS 1000;\n  END UNITS\n  SIZE 2880 BY 3780 ;\nEND A\n"
    hanan2.lef(text, tmp_path / "a.lef")
    written = (tmp_path / "a.lef").read_text()
    assert "DATABASE MICRONS UNITS 1;" in written
    assert "SIZE 2880 BY 3780" in written


def test_summary_reads_each_modules_open_nets(tmp_path):
    log = tmp_path / "route.log"
    log.write_text(
        "ROUTE_SUMMARY module=LEAF_0 nets=3 unrouted=0\n"
        "ROUTE_SUMMARY module=TOP_0 nets=18 unrouted=2\n"
        "OPEN NET: CLK\n  OPEN_WIRE : CLK__a__b\nOPEN NET: TAIL\n"
    )
    assert hanan2.summary(log) == {"LEAF_0": (3, []), "TOP_0": (18, ["CLK", "TAIL"])}


def test_binary_missing(monkeypatch, tmp_path):
    monkeypatch.setenv("ALIGN_HANAN2", str(tmp_path / "absent"))
    with pytest.raises(FileNotFoundError, match="ALIGN_HANAN2"):
        hanan2.binary()
