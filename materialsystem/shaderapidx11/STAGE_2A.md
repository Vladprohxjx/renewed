# Stage 2A — Source-facing D3D11 device module

This step adds a Windows-only `shaderapidx11` Waf module which exports the existing Source `IShaderDeviceMgr`, `IShaderAPI`, and `IShaderShadow` interfaces. It is based on the existing Source DX10 implementation, adapted to D3D11 immediate-context semantics.

Implemented in this step:
- D3D11 hardware device + immediate context creation.
- DXGI swap chain and back-buffer RTV.
- D24S8 depth/stencil resource + DSV.
- D3D11 state submission through `ID3D11DeviceContext`.
- D3D11 vertex/index buffer creation and mapping through the immediate context.
- Vertex/geometry/pixel shader creation + `D3DCompile`/`D3DReflect`.
- Source interface exports and Waf/VPC module definitions.
- No Android/GLES path changes.

Not claimed complete yet:
- Texture/SRV implementation.
- Functional `CMeshDx11::Draw`/static mesh path.
- Full stdshader material validation on a BSP.

The legacy DX9 module remains the compatibility fallback. Select the new module explicitly with the existing `SetShaderAPI` mechanism (for example `-shader shaderapidx11`) until the next stage wires runtime/default selection and completes the mesh/texture path.
