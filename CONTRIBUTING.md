# Contributing to Aero

Aero is in early development and uses the [Aero License](LICENSE). Unpaid
contributions and evaluation are permitted under its terms. Contributions retain
their authors' copyrights; commercial licensing authority for contributed code
must be arranged separately when needed.

Read [the build guide](docs/building.md) for requirements and test commands.
Keep changes focused and include a small reproducible example for execution bugs.

## Code conventions

- Use camelCase for identifiers defined by Aero, including test helpers and
  generated code. Preserve names mandated by platform APIs, language keywords,
  executable formats, and third-party interfaces.
- Preserve existing type naming conventions.
- Keep public files portable: no local machine paths or unrelated project history.
- Do not introduce dependencies or automatic downloads without discussion.
- Report unsupported instructions and runtime features explicitly.

## Validation

Run the execution tests for changes to loading, instructions, system calls, or
native export. Check both interpreted and exported behavior. Compiler-generated
fixtures exercise real calls, recursion, memory operations, and relocations.
Add an independent expected result or a native-machine comparison when changing
instruction semantics; agreement between two implementations of the same bug
is insufficient.

Include the host OS, CPU architecture, compiler version, build command, test
results, and any limitations in a pull request. Remove sensitive arguments,
paths, and program data from diagnostics before sharing them.
