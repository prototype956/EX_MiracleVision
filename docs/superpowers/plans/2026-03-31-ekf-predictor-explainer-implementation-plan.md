# EKF Predictor Explainer Documentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite `docs/modules/ekf/ekf_predictor.md` into a newcomer-friendly explainer that maps EX runtime flow to EKF math and includes minimal SP comparison.

**Architecture:** Keep a single source document focused on EX runtime pipeline, then layer state machine and EKF math in plain language. Preserve existing migration/card notes in an appendix so historical context is not lost.

**Tech Stack:** Markdown, existing C++ source references in EX_MiracleVision and sp_vision_25.

---

### Task 1: Build the new document skeleton and narrative order

**Files:**
- Modify: `docs/modules/ekf/ekf_predictor.md`

- [ ] **Step 1: Replace the top-level structure with explainer sections**

Write these sections in order: module role, one-frame data flow, state machine intuition, EKF model intuition, trajectory and firing boundary, EX vs SP minimal comparison, common misunderstandings, troubleshooting index.

- [ ] **Step 2: Keep historical card notes in appendix**

Move current Card notes to an appendix section named `附录：历史行为对齐记录` to avoid deleting prior migration evidence.

- [ ] **Step 3: Run a quick format sanity pass**

Run: `rg "^#|^##|^###" docs/modules/ekf/ekf_predictor.md`
Expected: Heading hierarchy appears in intended order without duplicated level jumps.

### Task 2: Add precise code mapping and minimal formulas

**Files:**
- Modify: `docs/modules/ekf/ekf_predictor.md`

- [ ] **Step 1: Add source mapping table**

Document exact mapping from concept to source files: `predict_node.cpp`, `ekf_predictor.cpp`, `ekf_tracker.cpp`, `ekf_track_target.cpp`, `trajectory_solver.cpp`, `i_predictor.hpp`, `i_voter.hpp`, `shooter` path.

- [ ] **Step 2: Add minimal EKF equations with symbol legend**

Include only the needed equations and explain symbols in plain language:
`x^- = f(x)` / `P^- = FPF^T + Q` / `K = PH^T(HPH^T+R)^{-1}` / `x = x^- + K(z-h(x^-))`.

- [ ] **Step 3: Verify terminology consistency**

Run: `rg "雅可比|状态机|观测|预测|Voter|Shooter|Tracker" docs/modules/ekf/ekf_predictor.md`
Expected: Core terms are present and used consistently across sections.

### Task 3: Validate readability for newcomer audience

**Files:**
- Modify: `docs/modules/ekf/ekf_predictor.md`

- [ ] **Step 1: Add a newcomer reading path**

Add a final `建议阅读顺序` section with 4 steps: flow first, state machine second, EKF third, troubleshooting fourth.

- [ ] **Step 2: Add common misunderstandings section**

Write at least three misunderstandings and corrections, including Jacobian misunderstanding and tracker responsibility misunderstanding.

- [ ] **Step 3: Self-review for ambiguity and placeholders**

Run: `rg "TODO|TBD|待补|后续补充" docs/modules/ekf/ekf_predictor.md`
Expected: no matches.

### Task 4: Verify and stage outcome summary

**Files:**
- Modify: `docs/modules/ekf/ekf_predictor.md`

- [ ] **Step 1: Run final diff check**

Run: `git --no-pager diff -- docs/modules/ekf/ekf_predictor.md | cat`
Expected: Document now centers on explainer narrative while retaining historical appendix.

- [ ] **Step 2: Record verification output summary**

Capture the heading scan, terminology scan, placeholder scan, and diff summary in the implementation response so reviewers can quickly validate quality gates.

- [ ] **Step 3: Commit (optional, only if requested)**

Run only when user asks:
`git add docs/modules/ekf/ekf_predictor.md docs/superpowers/plans/2026-03-31-ekf-predictor-explainer-implementation-plan.md`
`git commit -m "docs(ekf): rewrite predictor explainer for newcomers"`
