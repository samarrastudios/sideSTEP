// The viewer's document: a STEP file loaded into an XCAF document, flattened
// into a tree of nodes with one displayable object per part instance.
// OpenCASCADE types only; no Qt and no Windows headers here.
#pragma once
#include <AIS_ColoredShape.hxx>
#include <Bnd_Box.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace viewer {

struct ModelNode {
    std::u16string name;
    int parent = -1;
    std::vector<int> children;
    bool isAssembly = false;

    // Leaf (part instance) only:
    TopoDS_Shape shape;             // the part, in its own coordinates
    gp_Trsf trsf;                   // placement in the assembly
    Bnd_Box bounds;                 // in assembly coordinates
    // A few hundred points on the part's surface (its own coordinates, shared
    // between instances), enough to fit the view tightly around it.
    std::shared_ptr<const std::vector<gp_Pnt>> outline;
    Handle(AIS_ColoredShape) ais;   // what the 3D view displays
};

struct Model {
    Handle(TDocStd_Document) doc;
    std::vector<ModelNode> nodes;
    std::vector<int> roots;
    Bnd_Box bounds;
    // Length units as the file states them. OpenCASCADE converts the geometry
    // to millimetres either way; this decides which unit is shown first.
    bool unitsStated = false;
    bool inches = false;
    int partCount = 0;
    int uniqueShapes = 0;
    long long triangles = 0;
    int parseMs = 0, transferMs = 0, meshMs = 0;
};

// stage: short text for the user. fraction: 0..1, or negative when unknown.
using ProgressFn = std::function<void(const char* stage, double fraction)>;

// Runs on a worker thread. Returns false and fills `error` on failure or when
// `cancel` becomes true. Never throws.
bool LoadModel(const std::wstring& path, const std::atomic<bool>& cancel,
               const ProgressFn& progress, Model& out, std::string& error);

}  // namespace viewer
