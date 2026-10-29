# How changes to this tree are reviewed

The review process this fork follows before anything is pushed. It exists because
three rounds of review each caught what the previous round missed -- including two
defects introduced by the fixing rounds themselves (a duplicated call from a
too-narrow edit anchor, and a `kvcalloc` whose third release site stayed `kfree`
because the grep matched the variable's first name and not every name the buffer
carries).

## 1. Edit discipline

- Every edit re-reads the complete function from disk afterwards. The diff shows
  the intent; the file shows the truth.
- Changing a buffer's allocator exhaustively re-enumerates every allocation AND
  every release of that buffer under every name it carries through its lifetime.
  A pool that moves between `masks`, `l.masks` and `p->mask_pool` has three names
  and three release sites; grepping one instance name is how the third site gets
  missed.
- Changing behavior greps for every caller of the contract first (who reads what
  this writes, who depends on the old semantics).

## 2. Error returns in init code

- Before adding an error return to an init function, enumerate what has been
  INSTALLED, not what has been allocated. Installed state -- inode_operations
  swaps, taken references, scheduled works, published pointers -- survives the
  module's own memory and dies with a failed init, because the loader frees a
  module whose init failed even when the module is pinned.
- The last error return must precede the first live installation, and fallible
  setup goes before live installation. (src/main.c registers netlink before
  tosya_dir_init() puts private inode_operations on the protected parents,
  exactly for this reason.)

## 3. Verification ladder

1. Compiler: the tool builds and links on a host (scripts/build-host.sh). The
   linker is exhaustive where a review is not, and CI gates the package on it.
2. Whole-function read from disk, for every changed function.
3. Execution trace: one concrete scenario walked end to end in writing (boot:
   post-fs-data -> load -> init -> service -> sync; sync: token -> policy ->
   names -> apks -> status), answering every conditional on the way.
4. Cold read: an independent reviewer with no author context (a second AI, or a
   human) reviews the whole tree. The author then ADJUDICATES every finding
   against the code -- verifying before accepting, and pushing back with
   evidence where a finding misreads intent (the constant-time probe designs
   here arrived as "findings" whose proposed fixes would have been regressions).
5. CI: the eight-kernel matrix, the format gate, the host-build gate, and the
   package, with the package gated on the host build.

## 4. Review freshness

- A review is pinned to the commit it read, and findings against commit X are
  re-verified against HEAD before anything is acted on. Some will already be
  fixed, some will be stale noise -- and some are real anyway: a stale review
  still caught a third release site that the fix round had missed. Line numbers
  are checked against the current file, never trusted, and never used to decide
  whether a finding is already obsolete.

## 5. What is not fixed

- Findings that survive adjudication but are design trade-offs (constant-time
  probes, retention bounds, unreachable unload paths) are recorded in
  CHANGELOG.md with the reason. The next round then does not re-litigate them
  or "fix" them into a regression.
