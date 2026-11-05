# Licensing

The root [LICENSE](../LICENSE) covers only original SDK material that this
project can license. [LICENSE-PRIOR](../LICENSE-PRIOR) preserves the MIT text
from the planned `project-igi-natives` predecessor, while
[LICENSE_SCOPE](../LICENSE_SCOPE.md) explains the boundary.

The IGI 1 executable is used only for local compatibility verification. It is
not copied, required in this repository, or redistributed. IGI-RE is a
GPL-2.0-only research reference; this MVP contains no IGI-RE source, object
code, or linked library.

The preserved predecessor baseline contains the MinHook public header and
`nlohmann/json` 3.10.2 header; their notices are under `LICENSES/`. Before
vendoring Dear ImGui, GTLibCpp, or any additional component, record the exact
upstream revision and include its applicable notice under `LICENSES/`.
