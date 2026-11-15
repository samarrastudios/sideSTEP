// Command-line test harness: renders a STEP file to a PNG exactly the way the
// Explorer thumbnail will look, without involving Explorer.
#include "thumb_core.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s input.step output.png [size=256]\n", argv[0]);
        return 2;
    }
    const int size = argc > 3 ? std::atoi(argv[3]) : 256;
    using clk = std::chrono::steady_clock;
    auto t0 = clk::now();
    stepcore::Mesh mesh;
    std::string err;
    if (!stepcore::LoadStepFile(argv[1], stepcore::LoadOptions(), mesh, err)) {
        std::fprintf(stderr, "failed: %s\n", err.c_str());
        return 1;
    }
    auto t1 = clk::now();
    stepcore::Image img;
    stepcore::RenderIso(mesh, size, img);
    auto t2 = clk::now();
    if (!stepcore::WritePng(argv[2], img)) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    auto ms = [](clk::duration d) { return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(d).count(); };
    std::printf("parts=%d unique=%d triangles=%zu parse=%dms transfer=%dms mesh=%dms total-load=%lldms render=%lldms\n",
                mesh.partCount, mesh.uniqueShapes, mesh.tris.size(), mesh.parseMs, mesh.transferMs, mesh.meshMs,
                ms(t1 - t0), ms(t2 - t1));
    return 0;
}
