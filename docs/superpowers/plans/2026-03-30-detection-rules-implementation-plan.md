# Detection Rules Compliance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land instruction-driven compliance for detection with concrete updates to comments, docs, and tests while preserving current detector behavior.

**Architecture:** Use a contract-first flow: lock interface semantics, then align implementation comments, then enforce through tests, and finally synchronize docs/PR evidence. Keep ROI state-machine outside detector logic and preserve the detector->PnP corner-order contract.

**Tech Stack:** C++20, OpenCV, Eigen, CMake, YAML-CPP, project scripts (`scripts/format_code.sh`, `scripts/check_code.sh`).

---

### Task 1: Build Rule-to-Artifact Matrix

**Files:**
- Create: `docs/superpowers/specs/2026-03-30-detection-rule-design.md`
- Modify: `docs/superpowers/plans/2026-03-30-detection-rules-implementation-plan.md`

- [ ] **Step 1: Write matrix section in design spec (failing gap first)**

```markdown
## Rule Mapping (R1-R5)
| Rule | Code Files | Tests | Docs | Evidence in PR |
|------|------------|-------|------|----------------|
| R1 | src/modules/armor_detector/* | roi_manager_test.cpp | docs/modules/armor_detector.md | scope checklist |
```

- [ ] **Step 2: Verify matrix includes all five rules**

Run: `rg -n "R1|R2|R3|R4|R5" docs/superpowers/specs/2026-03-30-detection-rule-design.md`
Expected: all rules present at least once.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-03-30-detection-rule-design.md docs/superpowers/plans/2026-03-30-detection-rules-implementation-plan.md
git commit -m "docs(spec): add detection rule mapping matrix"
```

### Task 2: Lock Interface Contract Comments

**Files:**
- Modify: `src/interfaces/i_detector.hpp`
- Modify: `src/interfaces/types.hpp`
- Test: `src/test/detector_contract_test.cpp`

- [ ] **Step 1: Write failing contract tests for semantics**

```cpp
TEST(DetectorContractTest, EmptyFrameReturnsEmpty) {
  mv::modules::BasicArmorDetector detector;
  EXPECT_TRUE(detector.Detect(cv::Mat{}, mv::ArmorColor::RED).empty());
}
```

- [ ] **Step 2: Run single test to verify baseline behavior**

Run: `cmake --build build --target detector_contract_test && ./build/bin/detector_contract_test`
Expected: test executes (failures are acceptable before fixes).

- [ ] **Step 3: Add/align Doxygen contract comments**

```cpp
/**
 * @brief Detect targets in the input frame coordinate system.
 * @return Empty vector on no-target or invalid frame.
 * @thread_safety Not thread-safe
 */
```

- [ ] **Step 4: Re-run contract test**

Run: `cmake --build build --target detector_contract_test && ./build/bin/detector_contract_test`
Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add src/interfaces/i_detector.hpp src/interfaces/types.hpp src/test/detector_contract_test.cpp
git commit -m "docs(detector): formalize interface contract comments"
```

### Task 3: Align Basic Detector and ROI Comments

**Files:**
- Modify: `src/modules/armor_detector/basic_armor_detector.hpp`
- Modify: `src/modules/armor_detector/basic_armor_detector.cpp`
- Modify: `src/modules/armor_detector/roi_manager.hpp`

- [ ] **Step 1: Add explicit corner-order and ROI responsibility comments**

```cpp
// Corner order contract: BL, BR, TR, TL (shared with PnP world point template).
```

- [ ] **Step 2: Add why-comments for thresholds/kernels/recovery**

```cpp
// Use 11x11 dilation so colored edges can bridge overexposed white centers.
```

- [ ] **Step 3: Build affected targets**

Run: `cmake --build build --target armor_detector`
Expected: build success.

- [ ] **Step 4: Commit**

```bash
git add src/modules/armor_detector/basic_armor_detector.hpp src/modules/armor_detector/basic_armor_detector.cpp src/modules/armor_detector/roi_manager.hpp
git commit -m "docs(armor_detector): clarify ROI boundary and corner-order contract"
```

### Task 4: Add Detector/ROI Contract Tests

**Files:**
- Create: `src/test/detector/armor_detector_contract_test.cpp`
- Create: `src/test/detector/roi_manager_contract_test.cpp`
- Modify: `src/test/CMakeLists.txt` (or test target registry file)

- [ ] **Step 1: Add failing detector contract tests**

```cpp
TEST(ArmorDetectorContractTest, CornerOrderIsBL_BR_TR_TL) {
  // Construct synthetic pair and validate geometric order relation.
}
```

- [ ] **Step 2: Add failing ROI contract tests**

```cpp
TEST(RoiManagerContractTest, LostFallbackAfterMaxLost) {
  mv::modules::RoiManager roi;
  std::vector<mv::Detection> empty;
  for (int i = 0; i < 5; ++i) roi.RestoreAndUpdate(empty, {0, 0}, {1280, 1024});
  EXPECT_FALSE(roi.IsActive());
}
```

- [ ] **Step 3: Register tests in CMake and run**

Run: `cmake --build build --target armor_detector_contract_test roi_manager_contract_test`
Expected: both binaries built.

- [ ] **Step 4: Execute tests**

Run: `ctest --test-dir build -R "armor_detector_contract_test|roi_manager_contract_test" --output-on-failure`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/test/detector/armor_detector_contract_test.cpp src/test/detector/roi_manager_contract_test.cpp src/test/CMakeLists.txt
git commit -m "test(detector): add detector and ROI contract tests"
```

### Task 5: Sync Module Docs and PR Evidence

**Files:**
- Create: `docs/modules/armor_detector.md`
- Modify: `docs/refractor/modules/BASIC_ARMOR_DETECTOR.md`
- Modify: `docs/refractor/modules/ROI_MANAGER.md`

- [ ] **Step 1: Write module doc with explicit contract section**

```markdown
## Contracts
- Corner order: BL, BR, TR, TL.
- Detector coordinates: relative to input frame.
- ROI restores coordinates to full image frame.
```

- [ ] **Step 2: Add migration note in refractor docs**

Run: `rg -n "Corner order|ROI|BL, BR, TR, TL" docs/modules/armor_detector.md docs/refractor/modules/BASIC_ARMOR_DETECTOR.md docs/refractor/modules/ROI_MANAGER.md`
Expected: all key phrases exist.

- [ ] **Step 3: Commit**

```bash
git add docs/modules/armor_detector.md docs/refractor/modules/BASIC_ARMOR_DETECTOR.md docs/refractor/modules/ROI_MANAGER.md
git commit -m "docs(modules): sync armor detector contracts and ROI responsibilities"
```

### Task 6: Quality Gate and Final Evidence

**Files:**
- Modify: `docs/superpowers/specs/2026-03-30-detection-rule-design.md`
- Modify: `docs/superpowers/plans/2026-03-30-detection-rules-implementation-plan.md`

- [ ] **Step 1: Run format and static checks**

Run: `scripts/format_code.sh && scripts/check_code.sh --fix && scripts/check_code.sh`
Expected: no new warnings/errors.

- [ ] **Step 2: Run build + target tests**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: detector-related tests pass; if environment blocks full build, record exact blocker and run nearest subset.

- [ ] **Step 3: Produce final evidence mapping in spec**

```markdown
| Rule | Files | Tests | Docs | Status |
|------|-------|-------|------|--------|
| R2 | src/interfaces/i_detector.hpp | detector_contract_test | docs/modules/armor_detector.md | PASS |
```

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/2026-03-30-detection-rule-design.md docs/superpowers/plans/2026-03-30-detection-rules-implementation-plan.md
git commit -m "chore(quality): record detector compliance evidence"
```
