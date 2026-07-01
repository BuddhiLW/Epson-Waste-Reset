# Overarch architecture model

A C4 model of EWR expressed as [Overarch](https://github.com/soulspace-org/overarch)
EDN data — the single source of truth for the diagrams in
[`../ARCHITECTURE.md`](../ARCHITECTURE.md).

- **`model.edn`** — elements (person, system + external systems, containers,
  components) and their relations.
- **`views.edn`** — four views: `context-view`, `container-view`,
  `component-view`, and a `dynamic-view` of the CLI reset flow.

## Regenerate the diagrams

Requires `overarch`, `plantuml`, and `graphviz` (`dot`) on `PATH`.

    # from the repo root
    overarch -m docs/arch -r plantuml -R /tmp/ewr-render
    plantuml -tpng -o "$(pwd)/docs/images" /tmp/ewr-render/plantuml/ewr/*.puml

Overarch validates every `:ref` and reports unresolved ones; PlantUML bundles
the C4 standard library, so rendering works offline. Output PNGs land in
[`../images/`](../images/) and are embedded from `ARCHITECTURE.md`.

## Editing

Element ids are namespaced (`:ewr/…`, `:ewr.app/…`, `:ewr.core/…`,
`:ewr.ext/…`). Keep relations as top-level `{:el :rel …}` maps in `model.edn`
and reference them from a view's `:ct` by `:ref`. Keyword names cannot start
with a digit (use `step-1`, not `1`).
