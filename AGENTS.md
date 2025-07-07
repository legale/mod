This AGENTS.md describes guidelines for working with this repository.

## Code Style

- Use 2 spaces per indentation level. Do not use tabs.
- Keep lines under 100 characters where possible.

## Running Tests

- Before committing changes, run `make test` in the repository root. This runs tests for all modules.
- If you only modify code inside a single module, you may run `make test` inside that module's directory instead.
- Some modules support a `coverage` target. Run `make coverage` if you need a coverage report.

## Documentation

- Update `README.md` or module-specific documentation when adding or changing functionality.
- Describe any new build or test dependencies.

## Adding Modules

- Each new module should have its own directory with a standalone `Makefile`, source files, and tests.
- Add the module name to the `SUBDIRS` variable in the root `Makefile` so that it is included in top-level `make` targets.

