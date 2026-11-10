"""Statistics: paired candidate-vs-baseline comparisons and a Bayesian logistic surrogate.

Every game has a cell = (opponent, map, side). The side and the map decide many games on their own
(team A moves first), so comparisons are made within cells:

* paired(): candidate and baseline played the exact same (opponent, map, seed, side) slots; the mean
  of the per-slot score differences, with its standard error, per opponent and map size.
* Logistic: logit P(win) = cell effect + f(design features); fit by penalised maximum likelihood with a
  Gaussian prior (so it is a Bayesian posterior mode), and the Laplace approximation for uncertainty.
  Draws count as half a win. One model per map size (big-map games say nothing about small-map values).
"""

from __future__ import annotations

import math
from collections import defaultdict

import numpy as np
from scipy import optimize


def wilson(w: float, n: int, z: float = 1.96) -> tuple[float, float]:
    if n == 0:
        return (0.0, 1.0)
    p = w / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return (c - h, c + h)


def norm_cdf(z: float) -> float:
    return 0.5 * (1 + math.erf(z / math.sqrt(2)))


def summarize(rows: list[dict]) -> dict:
    """Plain scores of one configuration's games, per opponent and per (opponent, size)."""
    out = {}
    groups = defaultdict(list)
    for r in rows:
        groups["all"].append(r["score"])
        groups[r["opponent"]].append(r["score"])
        groups[f"{r['opponent']}/{r['size']}"].append(r["score"])
        groups[r["size"]].append(r["score"])
    for k, v in groups.items():
        n = len(v)
        s = sum(v)
        lo, hi = wilson(s, n)
        out[k] = {"n": n, "score": s / n, "lo": lo, "hi": hi}
    return out


def paired(cand: list[dict], base: list[dict]) -> dict:
    """Per-slot score differences where both played the same slot."""
    b = {(r["opponent"], r["map"], r["seed"], r["side"]): r["score"] for r in base}
    diffs = defaultdict(list)
    for r in cand:
        k = (r["opponent"], r["map"], r["seed"], r["side"])
        if k in b:
            d = r["score"] - b[k]
            diffs["all"].append(d)
            diffs[r["opponent"]].append(d)
            diffs[r["size"]].append(d)
            diffs[f"{r['opponent']}/{r['size']}"].append(d)
            diffs["map:" + r["map"]].append(d)
    out = {}
    for k, v in diffs.items():
        n = len(v)
        m = sum(v) / n
        var = sum((x - m) ** 2 for x in v) / (n - 1) if n > 1 else 0.25
        se = math.sqrt(max(var, 1e-6) / n)
        out[k] = {"n": n, "delta": m, "se": se, "p_better": norm_cdf(m / se)}
    return out


def base_rates(base: list[dict]) -> dict:
    """Baseline score per cell (opponent, map, side) and per (opponent, size), with counts."""
    cells = defaultdict(list)
    for r in base:
        cells[(r["opponent"], r["map"], r["side"])].append(r["score"])
        cells[(r["opponent"], r["size"])].append(r["score"])
        cells[(r["opponent"],)].append(r["score"])
    return {k: (sum(v) / len(v), len(v)) for k, v in cells.items()}


def stratified(cand: list[dict], rates: dict, shrink: float = 4.0) -> dict:
    """Candidate score minus the baseline's rate in the same cell (cell rate shrunk to the opponent/size
    rate with `shrink` pseudo-games). Usable when the slots don't match exactly."""
    diffs = defaultdict(list)
    for r in cand:
        cell = rates.get((r["opponent"], r["map"], r["side"]))
        grp = rates.get((r["opponent"], r["size"]), (0.5, 0))[0]
        if cell:
            p = (cell[0] * cell[1] + grp * shrink) / (cell[1] + shrink)
        else:
            p = grp
        d = r["score"] - p
        for k in ("all", r["opponent"], r["size"], f"{r['opponent']}/{r['size']}"):
            diffs[k].append(d)
    out = {}
    for k, v in diffs.items():
        n = len(v)
        m = sum(v) / n
        var = sum((x - m) ** 2 for x in v) / (n - 1) if n > 1 else 0.25
        out[k] = {"n": n, "delta": m, "se": math.sqrt(max(var, 1e-6) / n)}
    return out


def fitness(score19: float, score20: float, robust: float) -> float:
    return 0.45 * score19 + 0.45 * score20 + 0.10 * robust


# ---------------------------------------------------------------------------------------------
class Logistic:
    """logit p = cell[c] + X @ beta, Gaussian priors: cell ~ N(0, cell_sd), beta_j ~ N(0, sd_j)."""

    def __init__(self, cell_sd: float = 2.0):
        self.cell_sd = cell_sd

    def fit(self, cells: list, X: np.ndarray, y: np.ndarray, prior_sd: np.ndarray, w: np.ndarray | None = None):
        self.cell_names = sorted(set(cells))
        idx = {c: i for i, c in enumerate(self.cell_names)}
        ci = np.array([idx[c] for c in cells])
        nc, d = len(self.cell_names), X.shape[1]
        w = np.ones(len(y)) if w is None else w
        prior = np.concatenate([np.full(nc, 1 / self.cell_sd ** 2), 1 / np.asarray(prior_sd, float) ** 2])

        def unpack(th):
            return th[:nc], th[nc:]

        def nll(th):
            c, b = unpack(th)
            eta = c[ci] + X @ b
            p = 1 / (1 + np.exp(-eta))
            ll = np.sum(w * (y * np.log(p + 1e-12) + (1 - y) * np.log(1 - p + 1e-12)))
            g_eta = w * (p - y)
            gc = np.bincount(ci, weights=g_eta, minlength=nc)
            gb = X.T @ g_eta
            return -ll + 0.5 * np.sum(prior * th * th), np.concatenate([gc, gb]) + prior * th

        th0 = np.zeros(nc + d)
        res = optimize.minimize(nll, th0, jac=True, method="L-BFGS-B", options={"maxiter": 2000})
        th = res.x
        c, b = unpack(th)
        eta = c[ci] + X @ b
        p = 1 / (1 + np.exp(-eta))
        s = w * p * (1 - p)
        # Hessian blocks (cells are one-hot): [[diag(sum s), C^T S X], [X^T S C, X^T S X]] + prior.
        H = np.zeros((nc + d, nc + d))
        H[np.arange(nc), np.arange(nc)] = np.bincount(ci, weights=s, minlength=nc)
        CSX = np.zeros((nc, d))
        np.add.at(CSX, ci, X * s[:, None])
        H[:nc, nc:] = CSX
        H[nc:, :nc] = CSX.T
        H[nc:, nc:] = (X * s[:, None]).T @ X
        H += np.diag(prior)
        cov = np.linalg.inv(H)
        self.cells_hat = c
        self.beta = b
        self.cov = cov[nc:, nc:]
        self.n = len(y)
        self.p_mean = float(np.mean(p))
        return self

    def se(self) -> np.ndarray:
        return np.sqrt(np.diag(self.cov))

    def sample(self, rng: np.random.Generator, k: int = 1) -> np.ndarray:
        return rng.multivariate_normal(self.beta, self.cov, size=k)


def logit_to_pct(beta: float, p: float = 0.5) -> float:
    """Change in win probability at base rate p for a logit shift beta."""
    q = 1 / (1 + math.exp(-(math.log(p / (1 - p)) + beta)))
    return q - p
