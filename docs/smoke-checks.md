# Graphics SDK smoke checks

The release smoke operates only through the installed SDK boundary. First build
against an absolute installed Core SDK:

```bash
task build -- --core-sdk /absolute/path/to/termin-core/sdk
```

Then run the public smoke task:

```bash
TERMIN_SLANGC=/absolute/path/to/slangc task smoke
```

The check copies the composed SDK to a temporary location and removes ambient
Core, Graphics, and Python source paths. It verifies:

- `sdk-product.json`, `sdk-inputs.json`, and the exact Core identity;
- isolated Python imports from the composed runtime;
- shader and material compilation through installed `termin_shaderc`;
- graphics-owned MCP readback to PNG;
- GLB loading with skeleton and animation data;
- failure of a native consumer when the installed `termin_glb` package is
  removed;
- a native CMake consumer using installed package configs only;
- all headless showcase sections, including animated skinned GLB rendering.

Success ends with `Installed relocated Graphics consumers: OK`.
