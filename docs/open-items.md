# Open Items (Human Acceptance / Actions Required)

This document tracks items that require human owner action, verification, or review.

| ID | Item | Origin | Notes / Action Required |
|---|---|---|---|
| HI-008 | Open STEP/STL in FreeCAD (human check, T10) | T10 | Export the test solid (`export_to_directory`, STEP and STL; see `kernel/cemkit/geometry/occt/tests/test_occt_backend.cpp`) and confirm both files open in FreeCAD as one closed solid (STEP in millimetres, ADR-004). IDs HI-001..HI-007 are used on another branch. |
| HI-009 | Decide how clang-asan handles OCCT 8.0.0's misaligned list nodes | T10 | `NCollection_IncAllocator` bump-allocates without alignment, so `NCollection_TListNode<int>::delNode` (OCCT header, instantiated in cemkit tests) trips UBSan `alignment` during `BRepMesh_IncrementalMesh` (STL export): 2 clang-asan tests abort. Options: (a) clang `-fsanitize-ignorelist` with `[alignment] src:*/include/opencascade/*`; (b) an overlay patch aligning IncAllocator allocations (OCCT rebuild, about 30 min); (c) report upstream and wait. Not changed without owner approval (sanitizer configuration). |
