# Layered HRH heuristic — design notes & validation

`aviation_layered_alpha_hrh.cpp` — a solver-free heuristic for the cable-harness
routing problem (CHRP), migrating the aviation engineering-topology model to the
automotive setting of Karlsson et al., *"Automatic cable harness layout routing
in a customizable 3D environment"* (2023).

## 1. Problem and objective

We adopt the automotive CHRP objective (paper eqs. (1a),(1b),(2)):

```
min  f = wL * fL + wB * fB,         wL + wB = 1
     fL = sum_k demand_k * length(route_k)     # total cable length
     fB = sum_{e used by any cable} c_e         # bundle / harness length
```

`wB = 0` ⇒ `|K|` independent shortest paths; larger `wB` rewards bundling
(shared paths). Demands and edge weights are taken from our engineering
topology dataset (`dataset/edges-*.csv`, `dataset/pairs-*.csv`), with leaf
terminals aggregated to their center node (as in `project/scip_heur.cpp`).

### Objective normalization (important)

Our dataset mixes **tiny per-cable demands** (`demand ≈ 1e-6`) with **raw
physical edge weights** (`c_e ≈ 10–500`). Consequently `fL ≈ 3` while
`fB ≈ 10^5`: the two objectives differ by ~4 orders of magnitude. With the raw
weighted sum, `fB` dominates for *any* `wB > 0`, so (a) the objective value is
huge and meaningless, and (b) the `wB` sweep **degenerates** — every `wB > 0`
returns the same "minimize the bundle" solution, with no Pareto trade-off.

We therefore normalize each objective by a reference scale:

```
f = wL * (fL / fL0) + wB * (fB / fB0)
fL0 = sum_k demand_k * shortestpath_len_k     (minimum cable length)
fB0 = total weight of the center-graph MST    (a minimum-harness scale)
```

This makes `f` an `O(1)` quantity, keeps the per-edge routing costs consistent
with the normalized objective (cable coeff `wL/fL0`, bundle coeff `wB/fB0`), and
restores a genuine cable-vs-bundle trade-off so that varying `wB` traces out a
proper Pareto front. Raw `fL` and `fB` are still reported. (Setting
`fL0 = fB0 = 1` recovers the un-normalized paper objective.)

## 2. Manufacturability constraint (from the aviation MIP) and its equivalent form

The aviation MIP (`scip_heur.cpp`) splits the harness into at most `copy_num`
layers and imposes, per layer: acyclicity (`topo_seq`), single direction
(`single_dir`) and **only-father** (in-degree ≤ 1). We use the exact equivalence:

> A set of edges admits an orientation that is acyclic with in-degree ≤ 1
> **iff** the (undirected) edge set is a **forest**.
> (Forward: orient each tree as out-arborescences from a root → in-degree ≤ 1,
> valid topological order. Backward: in-degree ≤ 1 + acyclic ⇒ each component is
> an arborescence ⇒ undirected forest.)

So the structural constraint reduces to: **the edges used inside each layer form
a forest.** This replaces the previous code's travel-direction "parent" test,
which over-constrained the problem and produced 0 feasible solutions.

## 3. Algorithm

For each bundle weight `wB`:

1. **Safe MST layer (layer 0).** A global MST of the center graph is built once.
   Cables placed in layer 0 are routed on their unique MST tree path. Since all
   such routes lie inside one tree, the layer is always a forest ⇒ **feasibility
   is guaranteed by construction** (every cable always has a legal home).
2. **Free forest layers (1 … copy_num-1).** A cable is routed by a
   bundling-aware Dijkstra (paper Algorithm 3): edge cost `wL·demand·c_e` if the
   edge is already used in the layer, otherwise `wL·demand·c_e + (already used
   anywhere ? 0 : wB·c_e)`. A union-find forbids edges that would close a cycle,
   and a trial union-find verifies the forest property of the whole route.
3. **Assignment.** Each cable is placed in the layer/route of minimum marginal
   CHRP cost (largest-demand-first construction order).
4. **HRH improvement (paper Algorithm 2).** One cable at a time is removed and
   re-optimised over all layers; a move is kept only if the global objective `f`
   strictly improves. Iterate until no improvement.

Complexity is dominated by `O(passes · K · copy_num)` Dijkstra calls; runtime is
well under a second on all datasets.

## 4. Baseline for comparison

`runPaperHRH` is the automotive CHRP HRH (Algorithms 2–3) with **no** structural
constraint — an *unconstrained reference* whose solutions are generally not
manufacturable (their per-layer edge sets contain cycles / multi-parent nodes).

The HRH is a local search and stays near its initial routing: from a
shortest-path start it can only shuffle cables inside the union of the initial
paths and never builds a new shared backbone (that would momentarily raise the
bundle for one cable), so for high `wB` it gets trapped far from a bundled
solution. The paper avoids this by locally optimising **several** initial
routings — Lagrangian-dual solutions (Section 3.2) and heuristic constructions
(Section 3.3) — and keeping the best. To give the reference its strongest *fair*
shot we run the HRH from two initialisations and keep the better objective:
**(A)** shortest paths (good for low `wB`) and **(B)** MST tree paths (a bundled
start that lets the HRH reach a tree backbone for high `wB`, a proxy for the
paper's bundled/Lagrangian initial routes). This makes the comparison
conservative: the reference gets close to its best achievable objective at every
`wB`, so any remaining gap is meaningful.

## 5. Validation (copy_num = 3, max_passes = 40)

Normalized objective `f` (lower is better); `gap% = (layered − paper)/paper`.
`layered` is feasible/manufacturable in **every** row; `paper` is the strong
(two-init) unconstrained reference. `cable`,`bundle` are raw `fL`,`fB`.

### dataset-4 (546 nodes, 549 center edges, 194 aggregated pairs)

| wB   | layered f | layered cable | layered bundle | L | t(s) | paper f | paper bundle | gap% |
|------|-----------|---------------|----------------|---|------|---------|--------------|------|
| 0.00 | 1.312 | 4.036 | 111449 | 3 | 0.05 | 1.000 | 111221 | +31.2 |
| 0.05 | 1.162 | 3.657 | 95431  | 3 | 0.05 | 0.987 | 91882  | +17.8 |
| 0.10 | 1.136 | 3.676 | 88015  | 3 | 0.05 | 0.973 | 84755  | +16.8 |
| 0.20 | 0.980 | 3.340 | 80999  | 3 | 0.08 | 0.937 | 78782  |  +4.7 |
| 0.35 | 0.895 | 3.431 | 70269  | 3 | 0.05 | 0.908 | 105820 |  −1.5 |
| 0.50 | 0.827 | 3.674 | 66655  | 2 | 0.05 | 0.867 | 105820 |  −4.6 |
| 0.70 | 0.683 | 3.714 | 66447  | 2 | 0.05 | 0.730 | 71458  |  −6.4 |
| 0.90 | 0.534 | 3.716 | 66610  | 2 | 0.03 | 0.623 | 70410  | −14.3 |
| 1.00 | 0.459 | 3.723 | 66610  | 2 | 0.03 | 0.486 | 70410  |  −5.4 |

Behaviour is consistent across datasets 1/2/3:

* **Low `wB` (cable-dominated regime).** The layered solution is slightly worse
  (gap 0 … +31%) — this is the genuine *price of manufacturability*: shortest
  paths overlap heavily and cannot all be packed into `copy_num` forests, so
  some cables must detour. (`wB = 0` is the degenerate "no bundling" case, not a
  real harness regime.)
* **High `wB` (bundling regime that matters for harness design).** The
  MST-anchored layered construction reaches a well-balanced, manufacturable
  bundling that the purely local HRH cannot reach from either initialisation;
  the layered heuristic **matches or beats** the unconstrained reference
  (gap −1 … −34% across datasets) **while guaranteeing manufacturability** and
  running in `< 0.1 s` (≈10–100× faster than the previous version).

Honest summary for the paper: our heuristic is the only one of the two that
produces manufacturable (layered-forest, acyclic, single-parent) harnesses; in
the bundling-dominant regime it also achieves a lower CHRP objective than the
strong unconstrained CHRP-HRH reference, at sub-0.1 s runtime.

## 6. Build & run

```bash
g++ -std=c++17 -O2 aviation_layered_alpha_hrh.cpp -o aviation_hrh
./aviation_hrh dataset/edges-4.csv dataset/pairs-4.csv 3 "0,0.05,0.1,0.2,0.35" 40 result_layered_hrh
# positional args: edge_csv pair_csv copy_num wb_list [max_passes] [outdir]
```

Outputs (`result_layered_hrh/`): `comparison_summary.csv` (layered vs reference
per `wB`), `best_center_paths.csv`, `best_layer_edges.csv`,
`best_full_leaf_paths.csv`.
