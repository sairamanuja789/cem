# Upstream Bug Report: NCollection_IncAllocator bump allocation breaks alignment contract

- Package: Open CASCADE Technology (OCCT)
- Affected versions: 8.0.0 (`V8_0_0_p1`)
- Component: FoundationClasses / TKernel / NCollection
- Severity: Undefined behavior / crash under UBSan (`-fsanitize=alignment`)

## Summary

`NCollection_IncAllocator` bump-allocates arbitrary byte counts (`theSize`) without aligning the bump pointer or allocation size to `alignof(std::max_align_t)` (or even pointer alignment, 8 bytes on 64-bit systems).

Although the class documentation in `NCollection_IncAllocator.hxx` explicitly specifies:
> All pointers returned by Allocate() are aligned to the size of the data type "aligned_t".

in OCCT 8.0.0, `AllocateOptimal` performs:
```cpp
char* aRes = aBlock->CurPointer.fetch_add(theSize, std::memory_order_relaxed);
```
and `allocateSlow` performs:
```cpp
aBlock->CurPointer.store(static_cast<char*>(aRes) + theSize, std::memory_order_relaxed);
```

When an allocation request has a size that is not a multiple of the platform alignment requirement (for example, size 12 or 20), subsequent allocations in that block receive addresses that are not aligned to 8 or 16 bytes.

When nodes such as `NCollection_TListNode<TheItemType>` (which contain pointers and require 8-byte alignment on 64-bit platforms) are allocated and destroyed via `NCollection_TListNode<int>::delNode`:
```cpp
((NCollection_TListNode*)theNode)->myValue.~TheItemType();
```
the pointer `theNode` is misaligned. Under Clang `-fsanitize=alignment`, this trips UBSan:
```
UndefinedBehaviorSanitizer: undefined-behavior NCollection_TListNode.hxx:60:6
member access within misaligned address 0x... for type 'NCollection_TListNode<int>', which requires 8 byte alignment
```

## Reproduction

1. Build an application linking OCCT 8.0.0 with Clang 20 `-fsanitize=address,undefined`.
2. Perform B-rep meshing using `BRepMesh_IncrementalMesh` (which uses `NCollection_IncAllocator` internally).
3. The process aborts with an alignment violation inside `NCollection_TListNode<int>::delNode`.

## Proposed Solution

1. Align `struct IBlock` in `NCollection_IncAllocator.hxx`:
   ```cpp
   struct alignas(std::max_align_t) IBlock
   ```
2. Round up `theSize` to `alignof(std::max_align_t)` in `AllocateOptimal` and `allocateSlow`:
   ```cpp
   static constexpr size_t THE_ALIGNMENT = alignof(std::max_align_t);
   inline size_t alignSize(const size_t theSize) {
     return (theSize + THE_ALIGNMENT - 1) & ~(THE_ALIGNMENT - 1);
   }
   ```
   and bump by `anAlignedSize`.
