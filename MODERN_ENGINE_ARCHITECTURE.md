# Modern Engine Architecture — Stage 0 Audit

**Scope.** This is a repository-grounded foundation audit for the `nillerusr/source-engine` branch. It records the current state and safe migration seams; it does not introduce a renderer, physics replacement, ECS, or a global job system.

## 1. Current architecture and dependency map

The engine retains the classic Source split: shared utility tiers (`tier0`–`tier3`, `mathlib`, `vstdlib`), loadable engine/game modules, and public versioned interfaces in `public/`. Waf builds three project sets:

```text
launcher_main -> launcher -> engine <-> game/client + game/server
                             |        -> gameui/serverbrowser
                             |        -> filesystem_stdio -> vpklib
                             |        -> materialsystem -> shaderlib + shaderapidx9
                             |                              -> stdshaders
                             |        -> vphysics -> IVP/Havana libraries
                             |        -> vgui2 + vguimatsurface
                             +--------> vtf, datacache/mdlcache, particles, soundemittersystem
```

The root `wscript` selects one of three build graphs: `game`, `dedicated`, or `tests`. The game graph includes the renderer, VGUI, physics, filesystem, VTF/VPK, client/server game DLLs, and launcher. The test graph intentionally excludes rendering and gameplay and builds tier/math/filesystem/VPK unit-test modules.

### Stable public contracts to preserve

* `IMaterialSystem`, `IMatRenderContext`, `IShaderDevice`, `IShaderDeviceMgr`, and `IShaderAPI` are the existing rendering contracts. Resource-facing Source APIs (for example `ITexture`, meshes, materials, render contexts, shader snapshots, and `ShaderAPITextureHandle_t`) must remain valid through migration.
* `IPhysics`, `IPhysicsEnvironment`, `IPhysicsObject`, collision/query, controller, constraint, and surface-property interfaces in `public/vphysics_interface.h` are the correct compatibility boundary for a future physics backend.
* `IFileSystem`/`IBaseFileSystem` and `IQueuedLoader` are the filesystem and asynchronous preload contracts. VPK support lives in `vpklib`; do not bypass those APIs for a parallel asset path.
* VGUI’s `IVGui`, `IPanel`, `IInput`, and `ISurface` interfaces remain the legacy UI boundary.

## 2. Renderer and material flow

### Current flow

```text
engine view/world/model/decal/particle code
  -> IMaterialSystem / IMatRenderContext
  -> material, texture manager, shader system, shaderlib/stdshaders
  -> IShaderAPI + IShaderDevice (legacy Source compatibility interfaces)
  -> IRenderBackend legacy adapter
  -> shaderapidx9
  -> native D3D9 on Windows OR DX9-to-GL translation library (ToGL/ToGLES)
  -> OS window/context and GPU
```

`engine/matsys_interface.cpp`, `gl_rsurf.cpp`, `gl_draw.cpp`, `l_studio.cpp`, `r_decal.cpp`, and the client view code are the main legacy render consumers. They create Source meshes through `IMatRenderContext`; they should **not** be rewritten per backend.

`CMaterialSystem` dynamically owns the shader API layer and `g_pShaderAPI` is the active implementation. `shaderapidx9` provides the existing `IShaderAPI`/`IShaderDevice` implementation, including texture handles, render targets, buffers, shader state, device lifecycle, and capabilities. On Windows with ToGL disabled it links D3D9/D3DX9; elsewhere the same DX9-oriented shader API is translated by `togl`. `togles` is the GLES-oriented variant and is selected by `--togles`.

Materials are parsed into `CMaterial`/material variables, compiled and drawn by `CShaderSystem`/`shaderlib`, and use `stdshaders`. The existing PBR shader (`pbr_dx9.cpp` with SM2b/SM3 variants) is already compiled into the standard shader DLL. Stage 4 must extend or reuse it rather than claim PBR is newly introduced.

### Safe Stage 1 entry point

Add the new backend-neutral layer *behind* the existing Source-facing interfaces, adjacent to `materialsystem/shaderapidx9`, not above every engine render call. First make an adapter that maps the lifetime and calls already represented by `IShaderDevice`, `IShaderAPI`, and `IMatRenderContext` to a narrowly scoped internal render-device contract. Keep `shaderapidx9` as the first legacy backend. This retains material shader snapshots, `ShaderAPITextureHandle_t`, dynamic/static mesh behavior, queued render-context behavior, and device-reset semantics while allowing a later D3D11 backend.

The abstraction needs explicit ownership for device/context/resource objects; texture/buffer/shader/render-target/pipeline descriptors; submit/flush and frame/present boundaries; and capabilities. It must not expose D3D11, Vulkan, or OpenGL types through public Source interfaces.

### Stage 1 implementation status

The private `materialsystem/renderbackend.*` boundary now owns the active render-backend adapter through `CMaterialSystem`. `IRenderBackend` exposes a backend kind, backend name, and capability snapshot, while the legacy adapter borrows the existing shader API/device/device-manager interfaces. It is created only after the legacy interfaces have been acquired and is destroyed before the shader module unloads, preserving the existing module ownership order. No public Source rendering interface or engine render consumer was changed; `shaderapidx9` remains the active compatibility backend.

## 3. Physics

`vphysics` is an IVP/Havana-backed implementation of the public VPhysics interfaces. It already owns worlds/environments, collision models, static/dynamic poly and sphere objects, traces, vehicles, ragdolls, fluids, springs, controllers, constraints, and save/restore. Client/server physics initialize `physics->CreateEnvironment()`; static props and gameplay entities consume `IPhysicsEnvironment` directly.

**Stage 5 boundary:** retain the public VPhysics ABI and introduce backend selection/factory ownership beneath it. Start with static collision/props, dynamic poly bodies, and trace/query parity. Preserve VPhysics as the default/fallback until Jolt covers each surface (characters, vehicles, ragdolls, constraints, save/restore) with regression comparisons.

## 4. UI and scripting

The legacy UI is VGUI2, with GameUI and server browser panels on top. `vguimatsurface` implements VGUI `ISurface` using the material system; it also routes input through VGUI input interfaces and supports 3D panel drawing. GameUI includes options screens, including touch options. Keep this path intact.

No gameplay VScript VM was found in the active game/engine build graph. The existing `CTestScriptMgr` is a debug test-command runner for text files under `testscripts`, not a general scripting runtime. Stage 6 therefore requires a new versioned Script API facade, but it must bind to public/entity/gameplay interfaces rather than engine implementation classes. Stage 7 should add a separate modern UI layer beside—not inside—VGUI2 and bridge it through input/render adapters.

## 5. Android and platform baseline

Supported repository targets documented by the build scripts and README are Linux (glibc/musl), Windows, macOS, FreeBSD-family targets, Android, ARM (except Windows), and 64-bit targets. Waf has Linux, Android, Windows, macOS, and BSD platform defines and selects CPU-specific flags for x86/x86_64, arm, and aarch64.

Android is an existing code path: Waf accepts `--android=<arch>,<toolchain>,<api>`, sets Android/Posix defines, uses prebuilt/mobile dependencies, automatically enables Opus, and supports ToGLES. The launcher exports JNI entry points, configures SDL touch behavior, and initializes the engine on Android. The checked-in Android helper targets `armeabi-v7a-hard`, NDK r10e, API 21, ToGLES, and debug builds. ARM64 must be validated explicitly in future stages; it is not established by this Stage 0 host build.

Every renderer/device addition must retain a non-D3D desktop/mobile path, avoid Windows-only public API leakage, and plan for Android lifecycle and surface/context recreation before device ownership is finalized.

## 6. Assets and formats

* **BSP:** `public/bspfile.h` accepts versions 19 through 21; `engine/modelloader.cpp` loads map/world data, lightmaps, displacements, static props, and render-facing brush data.
* **Models:** `datacache/mdlcache.cpp` contains MDL49 handling; the documented supported range is MDL 46–49. `studiorender` and engine studio/model rendering consume cached models.
* **Textures/materials:** VTF is handled by `vtf` and the texture manager. `public/vtf/vtf.h` declares VTF 7.5. VMT/KeyValues-based material loading is in `materialsystem`.
* **Packages and filesystem:** `filesystem_stdio` composes base filesystem, async filesystem, packfile support, and `QueuedLoader`; `vpklib` implements packed store support. Existing queued loading already batches map resource IO into high-priority and low-priority phases, including dynamic resource callbacks.
* **Other Source assets:** particles, sound emitter scripts, choreography/VCD support (`choreoobjects` and `scenefilecache`), and VGUI localization are separate existing subsystems and remain compatibility-critical.

Stage 3 should first add coverage and adapters around these loaders, rather than replace parsers. Stage 9 must evolve `IQueuedLoader`/filesystem async facilities into streaming with explicit budgets and GPU-upload integration; it must not introduce an unrelated global job system.

## 7. Build, CI, and baseline

Waf is the sole build system and remains authoritative. `waf.bat` locates Python and dispatches to `waf` on Windows. Root options select tests, dedicated/game build, SDL, ToGL/ToGLES, Opus, sanitizers, architecture, and Android cross compilation. Platform dependencies are detected in `wscript`; Windows uses D3D9/D3DX9 for the native legacy renderer, while Android links mobile libraries and Android support.

No checked-in CI workflow/configuration files were present in this checkout despite README status badges. Future CI work should be added only after preserving the current Waf command lines.

### Recorded Stage 0 baseline (2026-09-08, Linux host)

| Check | Command | Result |
|---|---|---|
| Waf command/options discovery | `./waf --help` | passed |
| Unit-test configuration | `./waf configure --tests --use-sdl=0 --use-togl=0 --disable-warns -T debug` | passed; GCC/x86_64 Linux; no graphics/external UI dependency checks required |
| Unit-test build | `./waf build -j2` | passed; test graph compiled 167 tasks |
| Unit-test execution | `./waf build --alltests -j2` | passed; Waf has no standalone `test` command in this checkout, and this invocation completed without reporting runnable test cases |

The initial configure attempt without `-T` correctly stopped because Waf requires an explicit build type. That is a usage correction, not a source regression. `./waf test --alltests` is not valid here because no `test` command is defined; `--alltests` is a Waf unit-test option passed to `build`. The recorded baseline does not validate a game binary, Windows, macOS/BSD, Android, runtime rendering, or execution of named test cases because this environment only built the isolated Linux test graph.

## 8. Roadmap and acceptance focus

1. **Renderer abstraction:** internal adapter behind `IShaderAPI`/`IShaderDevice` and compatibility backend; compile and run the legacy shader/material path unchanged.
2. **DX11:** implement D3D11 device/swap chain/resources/states under that adapter; validate a Source map, models, materials, particles, decals, skybox, and VGUI.
3. **Materials and formats:** add format/load-to-render regression coverage for BSP 19–21, MDL 46–49, VTF 7.5, VMT, VMF, VCD, PCF, VPK, particles, sound, and localization.
4. **Modern rendering:** incrementally layer HDR/post processing/shadows/AO/AA/instancing over the working compatibility path; reuse existing PBR support.
5. **Jolt:** implement a selectable backend behind VPhysics public interfaces, in static-prop → dynamic-body → trace/query order.
6. **VScript:** add an engine-independent, versioned Script API and lifecycle; initially bind entities/events/timers/commands/ConVars, then physics through the physics abstraction.
7. **Modern UI:** add a lightweight retained/declarative layer with input, scaling, controller/touch, styling and VScript bridge while preserving VGUI2.
8. **Android:** validate ARM64 and lifecycle/surface recreation on the accumulated architecture; retain ToGLES/GLES fallback and mobile constraints.
9. **Asset streaming:** extend existing filesystem/queued-loader flow with async CPU decode, GPU upload, cache/budget/eviction, texture/model streaming, and Android memory policies.

## 9. Compatibility rules for all stages

1. Preserve Source public interfaces and file formats; use adapters/backends, not a consumer rewrite.
2. Keep legacy renderer, VPhysics, VGUI2, filesystem APIs, networking, entities, and save/load available until replacement coverage is proven.
3. Make backend selection explicit and reversible; default to the proven legacy path until the new backend passes focused runtime validation.
4. Treat Android/ARM64 and GLES/ToGLES as design inputs for every platform-sensitive API.
5. Add tests at the closest existing layer and record unvalidated platforms honestly.
6. Do not introduce a global ECS or global job system. Reuse scoped existing queues/threads only where a subsystem already owns them.

### Stage 2 implementation status

A private D3D11 backend now creates a hardware D3D11 device, immediate context, DXGI swap chain, back-buffer render target, and D24S8 depth/stencil target. It supports resize and present lifecycle operations and reports feature level, instancing, compute-shader availability, and supported MSAA sample count through the backend capability contract. The backend is compiled on Windows and remains an inert factory on non-Windows targets, preserving Linux/Android builds. The existing Source shader API continues to use the legacy compatibility adapter: rendering legacy D3D9 shader bytecode through D3D11 requires the next compatibility translation step and is not falsely selected as the default path.
