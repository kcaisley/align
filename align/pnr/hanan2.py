"""The rewritten hanan router (github.com/srini229/hanan_router), ALIGN's ``--router hanan2``.

Its source is PlaceRouteHierFlow/hanan2 (a subtree of that repository); ALIGN's build makes it a
program of its own, installed as align/bin/hanan2, not part of PnR. ALIGN hands it the files the
in-process hanan router reads (layers.json, the scaled placement JSON, the LEF and an NDR file)
and reads back its DEF per routed module, as for hanan (hanan_output.write_variant).

``ALIGN_HANAN2`` names another build of the program (e.g. the repository's own, ``make``).
``ALIGN_HANAN2_ARGS`` adds options (shell syntax, e.g. ``-threads 4 -reorder 20``);
``ALIGN_HANAN2_TIMEOUT`` stops it after that many seconds.

The NDR file is the design's own (``inputs/ndr.json``, may be absent) with each module's
DoNotRoute nets as its ``do_not_route`` and its SymmetricNets pairs as its ``symmetric_nets``
(the router mirrors the first net's route as a guide for the second; it finds the axis from
the two nets' pins). A module's Route layers the router reads from the placement itself.
"""

import json
import logging
import os
import pathlib
import re
import shlex
import subprocess

logger = logging.getLogger(__name__)

SUMMARY = re.compile(r"^ROUTE_SUMMARY module=(\S+) nets=(\d+) unrouted=(\d+)")
OPEN = re.compile(r"^OPEN NET: (\S+)")
DATABASE = re.compile(r"(DATABASE\s+MICRONS\s+(?:UNITS\s+)?)\d+(?:\.\d*)?")


def lef(text, out_file):
    """The LEF the router reads, written to ``out_file`` (returned): ``text`` with each MACRO's
    DATABASE MICRONS as 1. ALIGN's LEF states 1000 but gives its coordinates in database units
    (nm), as its placement and layers.json do; the router reads LEF coordinates as microns and
    would shrink every pin a thousandfold (the in-process hanan never reads the statement)."""
    pathlib.Path(out_file).write_text(DATABASE.sub(r"\g<1>1", text))
    return out_file


INSTALLED = pathlib.Path(__file__).resolve().parents[1] / "bin" / "hanan2"


def binary():
    """The router's program: ``ALIGN_HANAN2`` where set, else the one ALIGN's build installed."""
    stated = os.environ.get("ALIGN_HANAN2")
    path = pathlib.Path(stated) if stated else INSTALLED
    if not path.is_file() or not os.access(path, os.X_OK):
        where = f"ALIGN_HANAN2={stated}" if stated else str(INSTALLED)
        raise FileNotFoundError(f"hanan2: no router program at {where}; build and install ALIGN again")
    return str(path)


def ndr(ndr_file, placement_verilog_d, out_file):
    """The NDR file the router reads: ``ndr_file`` (may be absent) with each module's DoNotRoute
    nets and SymmetricNets pairs added; written to ``out_file``, whose path is returned ("" when
    there is nothing to say)."""
    entries = []
    if ndr_file and pathlib.Path(ndr_file).is_file():
        with open(ndr_file, "rt") as fp:
            entries = json.load(fp)
    for module in placement_verilog_d["modules"]:
        constraints = module.get("constraints", [])
        excluded = {
            net
            for constraint in constraints
            if constraint.get("constraint") == "DoNotRoute"
            for net in constraint.get("nets", [])
        }
        pairs = [
            [constraint["net1"], constraint["net2"]]
            for constraint in constraints
            if constraint.get("constraint") == "SymmetricNets"
            and constraint.get("net1") not in excluded
            and constraint.get("net2") not in excluded
        ]
        if not excluded and not pairs:
            continue
        entry = next((e for e in entries if e.get("module") == module["concrete_name"]), None)
        if entry is None:
            entry = {"module": module["concrete_name"]}
            entries.append(entry)
        if excluded:
            entry["do_not_route"] = sorted(set(entry.get("do_not_route", [])) | excluded)
        stated = {tuple(p[:2]) for p in entry.get("symmetric_nets", [])}
        new = [p for p in pairs if tuple(p) not in stated and tuple(reversed(p)) not in stated]
        if new:
            entry["symmetric_nets"] = entry.get("symmetric_nets", []) + new
    if not entries:
        return ""
    with open(out_file, "wt") as fp:
        json.dump(entries, fp, indent=2)
    return str(out_file)


def summary(log_file):
    """Each routed module's nets, and its open ones, from the router's log: {module: (nets, [open])}."""
    modules, current = {}, None
    for line in pathlib.Path(log_file).read_text(errors="replace").splitlines():
        m = SUMMARY.match(line)
        if m:
            current = m.group(1)
            modules[current] = (int(m.group(2)), [])
            continue
        m = OPEN.match(line)
        if m and current is not None:
            modules[current][1].append(m.group(1))
    return modules


def route(*, layers_file, placement_file, lef_file, ndr_file, out_dir, name):
    """Route the placement (each module, bottom up) into ``out_dir``: a DEF and an interim LEF per
    module. The router's log is ``out_dir/<name>.hanan2.log``; returns its summary (summary())."""
    out_dir = pathlib.Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    log_file = out_dir / f"{name}.hanan2.log"
    command = [binary(), "-d", str(layers_file), "-p", str(placement_file), "-l", str(lef_file),
               "-o", f"{out_dir}/", "-log", str(log_file)]  # fmt: skip
    if ndr_file:
        command += ["-ndr", str(ndr_file)]
    command += shlex.split(os.environ.get("ALIGN_HANAN2_ARGS", ""))
    timeout = float(os.environ["ALIGN_HANAN2_TIMEOUT"]) if os.environ.get("ALIGN_HANAN2_TIMEOUT") else None
    logger.info(f"hanan2: {shlex.join(command)}")
    with (out_dir / f"{name}.hanan2.out").open("wt") as out:
        result = subprocess.run(command, stdout=out, stderr=subprocess.STDOUT, timeout=timeout)
    if result.returncode != 0:
        raise RuntimeError(f"hanan2 router failed (exit {result.returncode}); see {log_file} and {out.name}")
    modules = summary(log_file) if log_file.exists() else {}
    for module, (nets, opened) in modules.items():
        if opened:
            logger.warning(f"hanan2: {module} has {len(opened)} of {nets} nets open: {', '.join(opened)}")
        else:
            logger.info(f"hanan2: {module} routed ({nets} nets)")
    return modules
