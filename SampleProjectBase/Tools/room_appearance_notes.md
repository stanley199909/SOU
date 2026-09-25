# Room appearance repair

- The source FBX names collision **nodes** `UCX_Forge2_*`, but their meshes are named `Cube.*`. Filtering mesh names after Assimp PreTransformVertices did not exclude them. ModelImport removes collision node references and unreferenced meshes before that pass. The forge loses 260 collision triangles; all four visible material groups retain their triangle counts.
- Forge2 UV3 belongs to the chimney; the available Forge1 UV3 atlas includes ash in that region. The forge now uses the existing RockWall17 tile with world-space triplanar sampling. The original purchased asset stays unchanged.
- Coal is a small, static faceted mesh; vertex data separates charcoal from glowing gaps. No additional purchased/generated textures or per-frame geometry creation.
- Water uses the saved basin fit: world Y 0.89938, below the rim, with a rounded capsule footprint. These are layout data and remain editable. This is a fit for the current trough, not automatic fitting for arbitrary replacement models.
- Outdoor grass patches A-D overlapped the cottage footprint after scale; E-H remain.

## Verification

Run `Tools/build_room_inspect.cmd` from the project directory to compare the original and corrected Assimp import. Run `Tools/build_room_appearance.cmd`, then `Tools/room_appearance_probe.exe` to render the forge. Set `PROBE_WATER=1` to render the basin. Output: `Tools/room_forge.png`, `Tools/room_water.png`.

The probe uses the production model loader, shaders, charcoal geometry and saved surface transforms. It uses an offscreen WARP device without gameplay, bloom or particles. It is a geometry/material check, not a substitute for reviewing animation and bloom with F5.
