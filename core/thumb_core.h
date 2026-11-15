// Portable thumbnail core: STEP bytes in, premultiplied BGRA pixels out.
// This header is deliberately free of OpenCASCADE and Windows types so the
// shell extension, the CLI and (later) the viewer can all share it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stepcore {

struct Triangle {
    float p[3][3];   // positions
    float n[3][3];   // per-vertex normals (may be zero -> flat shaded)
    uint8_t rgb[3];  // surface color
};

struct Mesh {
    std::vector<Triangle> tris;
    float bmin[3] = {0, 0, 0};
    float bmax[3] = {0, 0, 0};
    int partCount = 0;     // leaf instances in the assembly
    int uniqueShapes = 0;  // distinct shapes that were tessellated
    int parseMs = 0, transferMs = 0, meshMs = 0;  // where the time went
};

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> bgra;  // premultiplied alpha, top-down, 4 bytes/pixel
};

struct LoadOptions {
    // Tessellation tolerance as a fraction of the model's bounding diagonal.
    // 0.002 is plenty for a 256px thumbnail; the viewer will use finer values.
    double relativeDeflection = 0.002;
    double angularDeflection = 0.5;  // radians
    bool parallel = true;
};

// Parse a STEP file held in memory and tessellate it. Returns false and fills
// `error` on failure. Never throws.
bool LoadStep(const char* data, size_t size, const LoadOptions& opt, Mesh& out, std::string& error);
bool LoadStepFile(const std::string& path, const LoadOptions& opt, Mesh& out, std::string& error);

// Software-render an isometric view of the mesh into a size x size image with
// a transparent background. No GPU needed (thumbnail hosts often have none).
void RenderIso(const Mesh& mesh, int size, Image& out);

// Convenience: bytes -> image.
bool RenderStepThumbnail(const char* data, size_t size, int px, Image& out, std::string& error);

// Minimal PNG writer (used by the CLI and tests).
bool WritePng(const std::string& path, const Image& img);

}  // namespace stepcore
