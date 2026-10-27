"""Outputs of the hanan router: the GDS JSON, LEF, viewer JSON and error file of a routed variant.

The hanan router (PlaceRouteHierFlow/hanan_router) writes only a DEF per routed module: its
components and, per net, the route rectangles (or, for a net it did not route, one shape of its
pins). The astar flow writes its outputs from the PnR database (pnr/main.py, _generate_json);
for hanan they are written here from the placement (instances and their transformations), the
primitives' GDS JSON in the PnR input directory and the DEF's rectangles, on the PDK's GDS layers
(layers.json). The top-level ports are labelled on their nets' first metal rectangle, where the
layer's LabelLayerNo says, else on its Label datatype, as the astar flow labels them.
"""

import json
import logging
import pathlib
import re

logger = logging.getLogger(__name__)

RECT = re.compile(r"\+\s*RECT\s+(\S+)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)")
NET = re.compile(r"^\s*-\s+(\S+)\s*$")
TIME = [2019, 1, 1, 0, 0, 0, 2019, 1, 1, 0, 0, 0]


def read_def_nets(path):
    """The DEF's nets: name -> [(layer, (x0, y0, x1, y1))], in DEF order."""
    nets, current, inside = {}, None, False
    for line in pathlib.Path(path).read_text().splitlines():
        stripped = line.strip()
        if stripped.startswith("NETS "):
            inside = True
            stripped = stripped.split(";", 1)[1].strip()  # the first net may follow on the same line
            if not stripped:
                continue
        if not inside:
            continue
        if stripped.startswith("END NETS"):
            break
        m = NET.match(stripped)
        if m:
            current = nets.setdefault(m.group(1), [])
            continue
        for m in RECT.finditer(stripped):
            if current is not None:
                x0, y0, x1, y1 = (int(v) for v in m.groups()[1:])
                current.append((m.group(1), (min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1))))
    return nets


def gds_layers(layers_file):
    """Layer name -> (GDS layer, drawing datatype, label (layer, datatype) or None), from
    layers.json (the label where LabelLayerNo says, else the layer's Label datatype); and the top
    metal's width (the outline's margin, as PnRDB/WriteJSON.cpp draws it)."""
    with open(layers_file, "rt") as fp:
        data = json.load(fp)
    out = {}
    for layer in data["Abstraction"]:
        datatypes = layer.get("GdsDatatype", {})
        if "GdsLayerNo" in layer and "Draw" in datatypes:
            labels = layer.get("LabelLayerNo") or []
            label = (
                (labels[0][0], labels[0][1] if len(labels[0]) > 1 else 0)
                if labels
                else (layer["GdsLayerNo"], datatypes["Label"]) if "Label" in datatypes else None
            )
            out[layer["Layer"]] = (layer["GdsLayerNo"], datatypes["Draw"], label)
    metals = [layer for layer in data["Abstraction"] if "Pitch" in layer]
    return out, (metals[-1].get("Width") or 0) if metals else 0


def outline_boundaries(bbox, layers, margin):
    """The block's boundary markers as the astar flow writes them (PnRDB/WriteJSON.cpp): the PR
    boundary (235/5) on the block and the PDK's Outline layer around it by ``margin``."""
    x0, y0, x1, y1 = bbox
    marks = [{**boundary(235, 5, (0, 0, x1 - x0, y1 - y0)), "propattr": 126, "propvalue": "oaBoundary:pr"}]
    if "Outline" in layers:
        layer, datatype, _ = layers["Outline"]
        rect = (-margin, -margin, x1 - x0 + margin, y1 - y0 + margin)
        marks.append({**boundary(layer, datatype, rect), "propattr": 126, "propvalue": "oaBoundary:pr"})
    return marks


def sref_transform(transformation):
    """A placement transformation (oX, oY, sX, sY) as a GDS SREF's strans, angle and origin."""
    tr = transformation or {}
    sx, sy = tr.get("sX", 1), tr.get("sY", 1)
    xy = [int(round(tr.get("oX", 0))), int(round(tr.get("oY", 0)))]
    strans, angle = {(1, 1): (0, 0.0), (-1, 1): (32768, 180.0), (1, -1): (32768, 0.0), (-1, -1): (0, 180.0)}[(sx, sy)]
    return strans, angle, xy


def boundary(layer, datatype, rect):
    x0, y0, x1, y1 = rect
    return {"type": "boundary", "layer": layer, "datatype": datatype, "xy": [x0, y0, x0, y1, x1, y1, x1, y0, x0, y0]}


def _leaf_gds_json(ctn, input_dir, cap_gds):
    candidates = [input_dir / f"{ctn}.gds.json"]
    if ctn in cap_gds:  # a capacitor's own GDS beside the PnR directory
        candidates.append(pathlib.Path(cap_gds[ctn]).with_suffix(".gds.json"))
    for candidate in candidates:
        if candidate.is_file():
            with candidate.open("rt") as fp:
                return json.load(fp)
    raise FileNotFoundError(f"hanan output: no GDS JSON for the primitive {ctn} in {input_dir}")


def write_variant(*, variant, routed_top, placement_verilog_d, def_dir, results_dir, input_dir, layers_file, cap_map=(), json_dir=None):
    """Write ``variant``'s GDS JSON (and its python.gds.json copy), LEF, viewer JSON and error file
    into ``results_dir`` (the viewer JSON and error file into ``json_dir``, the PnR directory, as
    the astar flow writes them) from the hanan router's DEFs in ``def_dir``; ``routed_top`` is the
    top module's name in the routed placement (its DEF's name). Returns the files as the astar
    flow's variants map them (pnr/main.py)."""
    def_dir, results_dir, input_dir = pathlib.Path(def_dir), pathlib.Path(results_dir), pathlib.Path(input_dir)
    json_dir = pathlib.Path(json_dir) if json_dir is not None else results_dir
    layers, margin = gds_layers(layers_file)
    modules = {m["concrete_name"]: m for m in placement_verilog_d["modules"]}
    leaves = [leaf["concrete_name"] for leaf in placement_verilog_d["leaves"]]
    cap_gds = {pathlib.Path(gds).stem: gds for _, gds in cap_map}
    if routed_top not in modules:
        raise KeyError(f"hanan output: no module {routed_top} in the placement")

    header, units, structures = 600, [1e-09, 1e-09], {}
    for ctn in leaves:
        data = _leaf_gds_json(ctn, input_dir, cap_gds)
        header = data.get("header", header)
        units = data["bgnlib"][0].get("units", units)
        for cell in data["bgnlib"][0]["bgnstr"]:
            structures.setdefault(cell["strname"], cell)

    terminals, unknown = [], set()

    def build(name, top):
        if name in structures:
            return
        module = modules[name]
        elements = []
        for instance in module["instances"]:
            ctn = instance["concrete_template_name"]
            if ctn in modules:
                build(ctn, False)
            strans, angle, xy = sref_transform(instance.get("transformation"))
            elements.append({"type": "sref", "sname": ctn, "strans": strans, "angle": angle, "xy": xy})
        elements += outline_boundaries(module.get("bbox", [0, 0, 0, 0]), layers, margin)
        def_file = def_dir / f"{name}.def"
        nets = read_def_nets(def_file) if def_file.exists() else {}
        if not def_file.exists():
            logger.warning(f"hanan output: no DEF for {name}; its instances only")
        for net, rects in nets.items():
            for layer, rect in rects:
                if layer not in layers:
                    unknown.add(layer)
                    continue
                gds_layer, datatype, _ = layers[layer]
                elements.append(boundary(gds_layer, datatype, rect))
                if top:
                    terminals.append({"layer": layer, "netName": net, "netType": "drawing", "rect": list(rect)})
        if top:
            for port in module.get("parameters", []):
                rect = next(((l, r) for l, r in nets.get(port, []) if l in layers and layers[l][2] is not None), None)
                if rect is None:
                    logger.warning(f"hanan output: no metal shape to label port {port} of {name}")
                    continue
                layer, (x0, y0, x1, y1) = rect
                elements.append(
                    {"type": "text", "layer": layers[layer][2][0], "texttype": layers[layer][2][1], "presentation": 21,
                     "strans": 0, "mag": 3e-08, "width": -1, "xy": [(x0 + x1) // 2, (y0 + y1) // 2], "string": port}
                )  # fmt: skip
        structures[name] = {"time": TIME, "strname": name, "elements": elements}

    build(routed_top, True)
    if unknown:
        logger.warning(f"hanan output: layers not in layers.json, left out: {sorted(unknown)}")
    top = structures.pop(routed_top)
    top["strname"] = variant
    for cell in structures.values():  # an instance of a renamed module keeps pointing at it
        for element in cell.get("elements", []):
            if element.get("type") == "sref" and element.get("sname") == routed_top:
                element["sname"] = variant
    gds = {"header": header, "bgnlib": [{"time": TIME, "libname": "hanan", "units": units, "bgnstr": [*structures.values(), top]}]}

    out = {"errors": 0}
    out["gdsjson"] = results_dir / f"{variant}.gds.json"
    out["python_gds_json"] = results_dir / f"{variant}.python.gds.json"
    for path in (out["gdsjson"], out["python_gds_json"]):
        with path.open("wt") as fp:
            json.dump(gds, fp)
    bbox = modules[routed_top].get("bbox", [0, 0, 0, 0])
    out["json"] = json_dir / f"{variant}.json"
    with out["json"].open("wt") as fp:
        json.dump({"bbox": bbox, "globalRoutes": [], "globalRouteGrid": [], "terminals": terminals}, fp, indent=2)
    out["errfile"] = json_dir / f"{variant}.errors"
    out["errfile"].write_text("")
    out["lef"] = results_dir / f"{variant}.lef"
    interim = def_dir / f"{routed_top}_interim_hier.lef"
    if interim.exists():
        text = interim.read_text()
        out["lef"].write_text(re.sub(rf"\b{re.escape(routed_top)}\b", variant, text) if routed_top != variant else text)
    else:
        x0, y0, x1, y1 = bbox
        out["lef"].write_text(f"MACRO {variant}\n  ORIGIN 0 0 ;\n  SIZE {x1 - x0} BY {y1 - y0} ;\nEND {variant}\n")
    logger.info(f"OUTPUT hanan gds.json {out['gdsjson']}")
    return out


def ndr_with_do_not_route(ndr_file, placement_verilog_d, out_file):
    """The NDR file the hanan router reads (``ndr_file``, may be empty) with each module's
    DoNotRoute nets added as its ``do_not_route`` (the router reads no other exclusion); written to
    ``out_file``, whose path is returned ("" when there is nothing to say)."""
    entries = []
    if ndr_file and pathlib.Path(ndr_file).is_file():
        with open(ndr_file, "rt") as fp:
            entries = json.load(fp)
    for module in placement_verilog_d["modules"]:
        nets = [
            net
            for constraint in module.get("constraints", [])
            if constraint.get("constraint") == "DoNotRoute"
            for net in constraint.get("nets", [])
        ]
        if not nets:
            continue
        entry = next((e for e in entries if e.get("module") == module["concrete_name"]), None)
        if entry is None:
            entry = {"module": module["concrete_name"]}
            entries.append(entry)
        entry["do_not_route"] = sorted(set(entry.get("do_not_route", [])) | set(nets))
    if not entries:
        return ""
    with open(out_file, "wt") as fp:
        json.dump(entries, fp, indent=2)
    return str(out_file)

