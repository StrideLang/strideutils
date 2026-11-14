# Namespacing plan

Plan to make Stride imports and qualified names behave like the module systems of
Rust, Python and TypeScript: every file sees only what it imports, aliases are
local to the importing file, barrels can re-export, and two packages may declare
the same name.

Status: approved (decisions in section 7). Phase 1 is partly implemented and
not yet built (see 1.1).

## 1. Current behaviour

What the compiler does today, with the code that does it.

| # | Behaviour | Where |
|---|---|---|
| B1 | An imported declaration gets one namespace, written onto the declaration node as the `namespaceTree` compiler property. | `StrideLibrary::loadImport` (`strideutils/src/stridelibrary.cpp`) |
| B2 | That namespace was the file's `package` path; the `as` alias was used only when the file had no `package` line. Directory imports appended the alias to the package path (`Dsp::Filters::Filters`). | same |
| B3 | A call resolves only if its qualifier equals the declaration's namespace exactly. An unqualified call never reaches a namespaced declaration. | `ASTQuery::namespaceMatch` (`strideutils/src/astquery.cpp`) |
| B4 | 69 lookups pass `ASTQuery::getNodeName(node)` (the bare name) to `findDeclarationByName`/`findAllDeclarations` without the node's namespace list, so qualified calls fail there even when B3 would match. | `strideutils`, `stridegen`, `stridejit` |
| B5 | Only the compiled file's own imports are loaded. Imports inside library files (barrels, `Compressor` importing `EnvelopeFollower`) are ignored, so library-to-library calls resolve only by accident. | `ASTFunctions::preprocess` (`astfunctions.cpp`), `CodeResolver` (`coderesolver.cpp`) → `StrideSystem::loadImportTree` |
| B6 | Framework imports ignore the alias and assign no namespace. | `StrideFramework::loadImport` (`stridegen/src/strideframework.cpp`) |
| B7 | Generated symbols and the function map key on the bare declaration name, so two packages declaring the same module name would collide. | `stridegenerator.cpp` (`mangler.mangle(getNodeName(funcDecl))`, `functionMap[decl->getName()]`), `functionast.cpp` (child lookup by name) |
| B8 | A call to a module in another package compiled with a wrong argument count (`expected 11 params, got 7`) and crashed. Likely a default-property or type lookup that misses the namespaced declaration (B4). | seen with `Control::Pid` in `stridetest` |

| B9 | `import Control::Foc as Foc` was loaded as `Foc`: the parser keeps the scope (`Control`) apart from `ImportNode::importName()` (`Foc`), and both import paths passed only `importName()`. Scoped imports loaded nothing; `Control::Park` worked only because the whole library is always loaded under its package names. | `ASTFunctions::preprocess`, `CodeResolver`, `StrideSystem` |
| B10 | The default-property pass looked up called modules with `destFunc->getName()` and no namespace, so a qualified call got no defaults and the JIT call had too few arguments (the B8 crash). 35 more lookups keyed on `x->getName()` had the same gap. | `ASTFunctions::fillDefaultPropertiesForNode` and others |

Effect: 77 of 129 strideroot library tests fail, almost all because package
modules never resolve and the stream silently produces zeros.

### 1.1 Already changed (uncommitted, not built)

- B4: `ASTQuery::findDeclarationForNode` / `findAllDeclarationsForNode` added
  and used at every lookup that passed `getNodeName(node)` without a namespace
  list (strideutils, stridegen, stridejit). The four domain-crossing lookups in
  `StrideSystem` that are unqualified on purpose now pass `{}` explicitly.
  `LookupLint.NoLookupDropsTheNamespaceQualifier` fails on any new one.
- B10: the 36 lookups keyed on `x->getName()` now pass `x`'s namespace list;
  three that look up a declaration's own name pass `{}`. The lint covers
  `getName()` too.
- B9: `ASTQuery::getImportPath(import)` joins the import's scope and name;
  `preprocess`, `CodeResolver` and `StrideSystem` load and de-duplicate
  imports by it.
- B2: `loadImport` now applies one rule in all four branches: an alias replaces
  the namespace; without one, the `package` path is used. Test:
  `Library.ImportAliasReplacesPackageNamespace`.

These are interim fixes. Phase 2 moves aliases out of the declaration (see 2.1).

### 1.2 Phase 1 result (2026-11-13)

Compiler suites unchanged against a clean strideroot: strideutils 65/65,
stridegen 78/80 (the two failures are `HardwareResolver` tests that need
untracked AVR platform files), stridejit 271/271, boardgame `Reader.*` and
`Tutorials.*` 40/40.

Library tests, run one test per process (129 tests):

| Group | Tests | Cause | Where it gets fixed |
|---|---|---|---|
| Pass | 80 | | |
| A module calls another library module unqualified (`LowPass` → `BiQuad` → `Section`, `Norm` → `Dot`, `Normalize` → `Norm`/`Scale`, `Line`, `Detector`) | 22 | B5: the callee is never inserted and the call never resolves | Phase 2 (own-package lookup) and Phase 3 (a library file's own imports) |
| Array-port modules output zeros or crash (Foc, Dot, Wedge, Scale, Transform, MatMul, Quaternion, Pan) | 22 | Compiler bug, reproduced with a local copy of the module (no imports) | Separate issue |
| `Compressor`: `Unknown port property name: OutputPort_rate` | 1 | Port `.rate` not supported in that position | Separate issue |
| `Oscillator`: unknown intermediate variable; `Error` arity | 1 | Compiler bug | Separate issue |
| `Kalman`: wrong value | 1 | Library or expectation | Library |
| `GoertzelOffTone`, `FollowerRectifies`: fail without a reported value | 2 | Not yet diagnosed | |
| `RisingEdge`, `FallingEdge`, `Latch` (counted as passing above) | 3 | Pass alone, fail when run after other tests in the file: state leaks between tests in `stridetest` | Test runner |

Phase 1 exit is met for qualified calls from user code; the remaining
resolution failures are library-to-library calls, which are Phase 2/3 scope.

## 2. Target semantics

1. **Canonical name.** Every declaration has one canonical path: its file's
   `package` path, or, when there is no `package` line, the path derived from
   the file's location under the library root. It never depends on who imports it.
2. **Imports are per file.** Each file has an import table. `import A::B` makes
   `A::B::X` visible in that file, by its full path only (D1). `import A::B as C`
   makes `C::X` visible in that file only.
3. **Resolution.** A qualified call `Q::X` in file F is looked up by mapping `Q`
   through F's import table to a canonical path. An unqualified `X` is looked up
   in this order: local scope, the file's own package, names imported by name
   (rule 5), the root namespace (the prelude).
4. **Re-export.** A barrel file re-exports what it imports under its own
   aliases, so `import Control` followed by `Control::Foc::Park` works, where
   `Foc` is the alias in `Control.stride`'s `import Control::Foc as Foc` (D2).
5. **Import by name** (optional, Phase 5): `import Control::Foc::{Park, Clarke}` makes
   `Park` usable unqualified.
6. **Distinct identity.** Declarations with the same name in different packages
   coexist: codegen symbols include the canonical path.
7. **Ambiguity is an error.** If an unqualified name matches in two imported
   sources, report it instead of picking one.

### 2.1 Why aliases must leave the declaration

Today the alias is written onto the shared declaration node (B1), so a module has
one name per compilation. Two files importing it under different aliases, or a
library file and the root file disagreeing, cannot both work. Phase 2 keeps
`namespaceTree` canonical and moves aliases into the import table of each file.
The Phase 1 replace rule is then deleted.

## 3. Phases

Each phase ends with the full test set in section 5 green, and lands as one
commit per submodule (strideutils, stridegen, stridejit), then a submodule bump.

### Phase 1 - Make qualified calls work (interim)

Goal: the strideroot library tests pass with the current single-namespace model.

1. Add `ASTQuery::findDeclarationForNode(node, scope, tree, framework)` and
   `findAllDeclarationsForNode(...)` that take the name and the namespace list
   from the node itself.
2. Replace the 69 `findDeclarationByName(getNodeName(x), ...)` call sites (B4)
   with the new helpers, including the 11 already patched. Leave the bare-name
   overloads only for lookups of declaration names.
3. Add a check to the test build (a gtest that greps the sources, or a lint
   script) failing on new `findDeclarationByName(ASTQuery::getNodeName(` uses.
4. Find and fix B8: rerun the `Control::Pid` probe after step 2. If it still
   fails, trace the property-default and port-type lookups for the call.
5. Keep the alias-replaces-package rule from 1.1.

Exit: `stridetest strideroot/library/1.0/tests/` has no failures caused by
resolution (remaining failures are triaged as library bugs); `Library.*`,
stridegen and stridejit suites and boardgame `Reader.*` unchanged.

### Phase 2 - Canonical names and per-file import tables

1. Canonical namespace (rule 1): `loadImport` and `loadAllInDirectory` always
   write the package path; files without `package` get their path under the
   library root. No alias is written to declarations. Warn when a `package`
   line disagrees with the file's location (D3).
2. Record each file's import table (alias → canonical path) as a compiler
   property on the file's tree and on each declaration it contains
   (`_importTable`), so a declaration body can be resolved later without its
   file.
3. Resolution (rule 3): `findDeclarationForNode` maps the call's qualifier
   through the import table of the enclosing declaration (or the root file),
   then matches the canonical path. Unqualified names follow the rule-3 order.
4. Delete the Phase 1 alias rule and the `namespaceTree` mutation in
   `loadImport`.
5. Frameworks (B6): apply the same import table to `StrideFramework::loadImport`.

Exit: a module can be imported under different aliases by two files in one
compilation; the existing tests are unchanged.

**Implementation (2026-11-13/14).** Rather than changing every
lookup, names are made canonical once, while imported declarations are
inserted:

- Loading (`StrideLibrary::loadImport`, `ASTFunctions::loadAllInDirectory`)
  keeps `namespaceTree` = the `package` path and no longer applies aliases.
  `ASTFunctions::tagSourceFile` records on each top-level declaration
  `_sourcePath` (the import path of its file, e.g. `Control::Foc`), `_package`
  and `_imports` (the file's `alias=path` table).
  `ASTFunctions::checkPackageLocation` warns when a package matches neither the
  file's path nor its directory (D3).
- `ASTFunctions::canonicalizeQualifier`, called for every call, entity and
  array reference in `insertRequiredObjectsForNode`, rewrites the qualifier
  to the canonical package of the declaration it names, as seen from the
  innermost library declaration being walked (or the root file):
  an alias expands to its import path; a qualifier that is not an alias is a
  full path (D1); either matches a declaration by package or by
  `_sourcePath`. An unqualified name (call, constant or signal) inside a
  packaged declaration tries its own package first, unless the declaration
  declares that name itself (its own `Input`, `Output`, blocks).
  The search covers both pending imports and declarations already inserted
  into the tree (a second use of a name must still resolve).
- When an imported declaration is inserted, "is it already in the tree?" is
  asked with its `namespaceTree`, so a root `Gain` is not mistaken for
  `Pkg::Gain`.
- Every later lookup then sees canonical qualifiers (Phase 1 helpers).
- Barrel re-export (D2) falls out of matching by `_sourcePath`:
  `Control::Foc::Park` reaches `Control/Foc.stride`.
- `STRIDE_DEBUG_NAMES=1` logs each resolution: name, qualifier, context and
  result.
- Tests: `Namespacing.*` (aliased call, sibling call inside a package, second
  use of an inserted declaration, unqualified package constant shadowing a
  root constant, full import path),
  `Library.ImportKeepsPackageNamespaceAndRecordsSource`.

**Phase 2 result (2026-11-14).** strideutils 69/69, stridegen 80/80, stridejit
276/276, libgame `Reader.*`/`Tutorials.*` 40/40. The library suite run test by
test is unchanged at 80/129, but no failure is a name-resolution failure any
more: library-to-library calls now resolve, and the filters, Delay and LinAlg
reach code generation, where they fail on compiler bugs outside namespacing
(array ports output zeros; `(1.0 - Alpha)` with a port-derived type; invalid
GEP in `Delay::Line`; `OutputPort.rate` in Compressor). Reaching
`FilterDesign::Coefficients` also exposed a stridegen crash on unary
expressions in `CodeAnalysis::processStreamNode`, fixed with Phase 2.

### Phase 3 - Transitive imports and re-export

1. Load imports recursively (B5): when a library file is loaded, process its
   own `import` lines, with a visited set keyed on canonical path to stop cycles.
2. Barrels re-export their imports into their own namespace (rule 4).
3. Library-to-library calls (`Compressor` → `EnvelopeFollower`, filters →
   `FilterDesign`) resolve through the calling file's import table.

Exit: `import Dsp` lets a file call `Dsp::Dynamics::Compressor` with no further
imports; library files compile on their own.

### Phase 4 - Distinct identity in codegen

1. Mangle generated function and state names with the canonical path (B7).
2. Key `functionMap` and the child-instance lookup in `functionast.cpp` on the
   declaration node or its canonical path, not the bare name.
3. Ambiguity errors (rule 7) with the candidates listed.

Exit: a test with `module Filter` in two packages, both used in one domain.

### Phase 5 - Import by name (deferred, D4)

1. Parser: `import A::B::{X, Y}` and `import A::B::X` (strideparser grammar,
   `ImportNode` gains a name list).
2. Resolver: these names join the file's unqualified scope.

## 4. Library follow-up (strideroot)

- After Phase 1: no library change needed; tests already use aliases.
- After Phase 3: drop imports from tests that the barrels provide, and remove
  workarounds for library-to-library calls if any were added.
- The roadmap decisions on stuttering names (`Kalman::Filter`) and the `core/`
  move depend on Phases 2 and 3; revisit them then.

## 5. Tests

| Suite | What it covers |
|---|---|
| `strideutils` `Library.*` | namespace assignment per import form; canonical names; import tables |
| `strideutils` new `Namespacing.*` | resolution: qualified, aliased, unqualified-same-package, ambiguity, two aliases for one module |
| `stridegen` resolver tests | calls into packages, framework imports |
| `stridejit` tests | codegen with namespaced modules, same name in two packages (Phase 4) |
| `stridetest strideroot/library/1.0/tests/` | the library end to end |
| boardgame `Reader.*`, `Tutorials.*` | games import without aliases; must not change |

Every phase adds its tests before the code change, and they fail first.

## 6. Risks

- **Many lookup sites.** 69 sites in three repos; a mechanical change but easy
  to miss one. Mitigation: the grep check in Phase 1 step 3.
- **Cache.** `.strdcache` stores parsed trees with `namespaceTree`. Phase 2
  changes what is stored; caches are keyed on the compiler commit, so old
  entries are not reused, but delete them when testing uncommitted builds.
- **Two copies of the compiler.** boardgame builds from `libgame/external/*`;
  the Stride repo has its own checkouts at other commits. Land in the
  submodule repos and bump both.
- **Behaviour change for aliases.** After Phase 2, `import X as Y` no longer
  exposes `X`'s package path in that file. Nothing in the games or Stride test
  data relies on that today.

## 7. Decisions

| # | Question | Decision (2026-11-13) |
|---|---|---|
| D1 | Does `import A::B` (no alias) expose `B::X` as well as `A::B::X`? | Full path only, as in Python's `import a.b`. |
| D2 | After `import Control`, what is visible? | The barrel's own aliases are re-exported: `Control::Foc::Park`, as with Rust's `pub use`. |
| D3 | Canonical path from the `package` line or the file location? | The `package` line, with a warning when it disagrees with the location. |
| D4 | Phase 5 (import by name)? | Deferred: decide after Phases 1-4 are done. |
