#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pandas", "matplotlib", "numpy"]
# ///
"""
EnergyTrace profile plotter.

Reads the energy.csv produced by `make energy` (columns: time[s]
current[A] voltage[V] energy[J], '#' comments) and renders the power
profile + phase analysis. The demo's blinking LED dominates the
average; this script separates LED-on / LED-off phases by current
threshold so the MCU-only baseline (the number that matters for the
kernel) falls out of the LED-off phase.

  uv run tools/plot_energy.py [energy.csv] [-o energy.png]
"""
from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


def main() -> None:
    ap = argparse.ArgumentParser(description="EnergyTrace profile plot")
    ap.add_argument("csv", nargs="?", default="energy.csv", type=Path)
    ap.add_argument("-o", "--output", default=None, type=Path,
                    help="output image (default: <csv>.png)")
    opts = ap.parse_args()
    out = opts.output or opts.csv.with_suffix(".png")

    df = pd.read_csv(opts.csv, sep=r"\s+", comment="#",
                     names=["t", "i", "v", "e"])
    df["i_ua"] = df.i * 1e6

    # Phase separation: LED on/off makes the trace bimodal; split at
    # the midpoint between the 5th and 95th percentiles.
    lo, hi = df.i_ua.quantile([0.05, 0.95])
    thresh = (lo + hi) / 2
    off = df[df.i_ua < thresh]
    on = df[df.i_ua >= thresh]

    stats = {
        "duration [s]": df.t.iloc[-1] - df.t.iloc[0],
        "samples": len(df),
        "avg current [uA]": df.i_ua.mean(),
        "LED-off avg [uA] (MCU baseline)": off.i_ua.mean(),
        "LED-on avg [uA]": on.i_ua.mean(),
        "LED-only est. [uA]": on.i_ua.mean() - off.i_ua.mean(),
        "total energy [mJ]": df.e.iloc[-1] * 1e3,
        "avg voltage [V]": df.v.mean(),
    }

    fig, axes = plt.subplots(3, 1, figsize=(11, 9))
    fig.suptitle("mRTOS demo on MSP430FR5994 — EnergyTrace profile "
                 "(T8)", fontweight="bold")

    ax = axes[0]
    ax.plot(df.t, df.i_ua, lw=0.3, color="tab:blue")
    ax.axhline(off.i_ua.mean(), color="tab:green", ls="--", lw=1,
               label=f"LED-off (MCU) {off.i_ua.mean():.0f} µA")
    ax.axhline(on.i_ua.mean(), color="tab:red", ls="--", lw=1,
               label=f"LED-on {on.i_ua.mean():.0f} µA")
    ax.set_ylabel("current [µA]")
    ax.set_title("full capture")
    ax.legend(loc="center right", fontsize=8)

    ax = axes[1]
    t0 = df.t.iloc[0] + (df.t.iloc[-1] - df.t.iloc[0]) / 2
    zoom = df[(df.t >= t0) & (df.t <= t0 + 4)]
    ax.plot(zoom.t, zoom.i_ua, lw=0.6, color="tab:blue")
    ax.set_ylabel("current [µA]")
    ax.set_title("4 s zoom — LED1 square wave at 1 Hz")

    ax = axes[2]
    ax.hist(df.i_ua, bins=200, color="tab:blue", alpha=0.8)
    ax.axvline(thresh, color="k", ls=":", lw=1, label="phase threshold")
    ax.set_xlabel("current [µA]")
    ax.set_ylabel("samples")
    ax.set_title("current distribution (bimodal: LED off / on)")
    ax.set_yscale("log")
    ax.legend(fontsize=8)

    for axx in axes[:2]:
        axx.set_xlabel("time [s]")
        axx.grid(alpha=0.3)
    axes[2].grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(out, dpi=130)

    print(f"wrote {out}")
    width = max(len(k) for k in stats)
    for k, v in stats.items():
        print(f"  {k:<{width}} : "
              f"{v:,.1f}" if isinstance(v, float) else
              f"  {k:<{width}} : {v}")


if __name__ == "__main__":
    main()
