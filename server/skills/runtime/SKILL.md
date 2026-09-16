---
name: ue-runtime
description: Control PIE and inspect actors, components, and runtime properties.
metadata:
  dcc-mcp:
    dcc: unreal
    tools: tools.json
---

# Runtime Skill

Runtime reads are scoped to the active PIE world. Starting and stopping PIE
are editor control actions and are intentionally not marked read-only.
