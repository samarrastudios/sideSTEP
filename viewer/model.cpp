#include "model.h"

#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_ProgressIndicator.hxx>
#include <NCollection_DataMap.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_Sequence.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TDataStd_Name.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFPrs.hxx>
#include <XCAFPrs_DocumentExplorer.hxx>
#include <XCAFPrs_Style.hxx>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace viewer {
namespace {

class Progress : public Message_ProgressIndicator {
public:
    Progress(const std::atomic<bool>& cancel, const ProgressFn& fn) : cancel_(cancel), fn_(fn) {}
    bool UserBreak() override { return cancel_.load(); }
    void Show(const Message_ProgressScope&, const bool) override {
        if (fn_) fn_("Building geometry", GetPosition());
    }
private:
    const std::atomic<bool>& cancel_;
    const ProgressFn& fn_;
};

std::u16string LabelName(const TDF_Label& label) {
    Handle(TDataStd_Name) name;
    if (label.IsNull() || !label.FindAttribute(TDataStd_Name::GetID(), name)) return {};
    const TCollection_ExtendedString& s = name->Get();
    return std::u16string(s.ToExtString(), (size_t)s.Length());
}

Quantity_Color DefaultColor() {
    return Quantity_Color(150 / 255.0, 160 / 255.0, 172 / 255.0, Quantity_TOC_sRGB);  // neutral steel
}

bool Build(const std::wstring& path, const std::atomic<bool>& cancel, const ProgressFn& progress,
           Model& out, std::string& error) {
    Message::DefaultMessenger()->ChangePrinters().Clear();
    using clk = std::chrono::steady_clock;
    auto ms = [](clk::time_point a, clk::time_point b) {
        return (int)std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };
    auto report = [&](const char* stage, double f) { if (progress) progress(stage, f); };

    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) { error = "The file could not be opened."; return false; }

    report("Reading file", -1);
    const auto t0 = clk::now();
    STEPCAFControl_Reader reader;
    reader.SetColorMode(true);
    reader.SetNameMode(true);
    reader.SetLayerMode(false);
    reader.SetPropsMode(false);
    reader.SetGDTMode(false);
    reader.SetMatMode(false);
    reader.SetViewMode(false);
    if (reader.ChangeReader().ReadStream("model.step", in) != IFSelect_RetDone) {
        error = "This is not a readable STEP file.";
        return false;
    }
    in.close();
    if (cancel) { error = "cancelled"; return false; }
    {
        // Inch files often also carry a helper millimetre entry, so any inch
        // entry makes it an inch file.
        NCollection_Sequence<TCollection_AsciiString> lengths, angles, solidAngles;
        try { reader.ChangeReader().FileUnits(lengths, angles, solidAngles); } catch (...) {}
        out.unitsStated = !lengths.IsEmpty();
        for (int i = 1; i <= lengths.Length(); ++i) {
            TCollection_AsciiString name = lengths.Value(i);
            name.UpperCase();
            if (name.Search("INCH") > 0) out.inches = true;
        }
    }
    const auto t1 = clk::now();
    out.parseMs = ms(t0, t1);

    report("Building geometry", 0);
    out.doc = new TDocStd_Document("BinXCAF");
    {
        Handle(Progress) indicator = new Progress(cancel, progress);
        const bool ok = reader.Transfer(out.doc, indicator->Start());
        if (cancel) { error = "cancelled"; return false; }
        if (!ok) { error = "The STEP file could not be converted to geometry."; return false; }
    }
    const auto t2 = clk::now();
    out.transferMs = ms(t1, t2);

    // Walk the assembly tree. Parents are found through the node id, which is
    // the parent's id followed by "/" and the node's own label.
    std::unordered_map<std::string, int> byId;
    struct LeafStyle { TDF_Label ref; XCAFPrs_Style style; };
    std::vector<LeafStyle> leafStyles;
    std::vector<int> leaves;
    for (XCAFPrs_DocumentExplorer it(out.doc, XCAFPrs_DocumentExplorerFlags_None); it.More(); it.Next()) {
        const XCAFPrs_DocumentNode& dn = it.Current();
        ModelNode node;
        node.isAssembly = dn.IsAssembly;
        node.name = LabelName(dn.RefLabel);
        if (node.name.empty()) node.name = LabelName(dn.Label);
        if (node.name.empty()) node.name = dn.IsAssembly ? u"Assembly" : u"Part";
        if (!dn.IsAssembly) {
            node.shape = XCAFDoc_ShapeTool::GetShape(dn.RefLabel);
            if (node.shape.IsNull()) continue;
            node.trsf = dn.Location.Transformation();
        }
        const std::string id = dn.Id.ToCString();
        const size_t slash = id.find_last_of('/');
        if (slash != std::string::npos) {
            auto p = byId.find(id.substr(0, slash));
            if (p != byId.end()) node.parent = p->second;
        }
        const int index = (int)out.nodes.size();
        byId[id] = index;
        if (node.parent >= 0) out.nodes[node.parent].children.push_back(index);
        else out.roots.push_back(index);
        if (!dn.IsAssembly) {
            leaves.push_back(index);
            leafStyles.push_back({dn.RefLabel, dn.Style});
        }
        out.nodes.push_back(std::move(node));
    }
    if (leaves.empty()) { error = "The file contains no geometry."; return false; }
    out.partCount = (int)leaves.size();

    // Bounds per distinct part (vertex positions are enough and cheap), then
    // one absolute tessellation tolerance for the whole model.
    NCollection_DataMap<TopoDS_Shape, Bnd_Box, TopTools_ShapeMapHasher> boxes;
    for (int li : leaves) {
        ModelNode& n = out.nodes[li];
        if (!boxes.IsBound(n.shape)) {
            Bnd_Box b;
            for (TopExp_Explorer vx(n.shape, TopAbs_VERTEX); vx.More(); vx.Next())
                b.Add(BRep_Tool::Pnt(TopoDS::Vertex(vx.Current())));
            if (b.IsVoid()) {
                try { BRepBndLib::Add(n.shape, b, false); } catch (...) {}
            }
            boxes.Bind(n.shape, b);
        }
        const Bnd_Box& b = boxes.Find(n.shape);
        if (!b.IsVoid()) {
            n.bounds = b.Transformed(n.trsf);
            out.bounds.Add(n.bounds);
        }
    }
    if (out.bounds.IsVoid()) { error = "The file contains no geometry."; return false; }
    double x0, y0, z0, x1, y1, z1;
    out.bounds.Get(x0, y0, z0, x1, y1, z1);
    const double diag = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
    const double deflection = std::max(diag * 0.001, 1e-6);

    out.uniqueShapes = boxes.Extent();
    const auto t3 = clk::now();
    int done = 0;
    NCollection_DataMap<TopoDS_Shape, long long, TopTools_ShapeMapHasher> triCount;
    NCollection_DataMap<TopoDS_Shape, std::shared_ptr<const std::vector<gp_Pnt>>, TopTools_ShapeMapHasher> outlines;
    for (NCollection_DataMap<TopoDS_Shape, Bnd_Box, TopTools_ShapeMapHasher>::Iterator bi(boxes); bi.More(); bi.Next()) {
        if (cancel) { error = "cancelled"; return false; }
        report("Meshing", double(done++) / out.uniqueShapes);
        try {
            BRepMesh_IncrementalMesh mesher(bi.Key(), deflection, false, 0.3, true);
        } catch (...) {
            // A part that fails to mesh is skipped; the rest still displays.
        }
        // Count triangles, and take the part's bounds from what will actually
        // be drawn: some files carry loose points or sketch curves far from
        // the solids, which would throw off fitting the view.
        long long n = 0;
        Bnd_Box drawn;
        std::vector<gp_Pnt> nodes;
        for (TopExp_Explorer ex(bi.Key(), TopAbs_FACE); ex.More(); ex.Next()) {
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(TopoDS::Face(ex.Current()), loc);
            if (tri.IsNull()) continue;
            n += tri->NbTriangles();
            const gp_Trsf& t = loc.Transformation();
            for (int i = 1; i <= tri->NbNodes(); ++i) {
                nodes.push_back(tri->Node(i).Transformed(t));
                drawn.Add(nodes.back());
            }
        }
        triCount.Bind(bi.Key(), n);
        if (!drawn.IsVoid()) bi.ChangeValue() = drawn;

        // Outline points: the farthest node in each of 26 directions, plus an
        // even sample of the rest.
        auto outline = std::make_shared<std::vector<gp_Pnt>>();
        if (!nodes.empty()) {
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dz = -1; dz <= 1; ++dz) {
                        if (!dx && !dy && !dz) continue;
                        size_t best = 0;
                        double bestDot = -1e300;
                        for (size_t i = 0; i < nodes.size(); ++i) {
                            const double d = nodes[i].X() * dx + nodes[i].Y() * dy + nodes[i].Z() * dz;
                            if (d > bestDot) { bestDot = d; best = i; }
                        }
                        outline->push_back(nodes[best]);
                    }
            const size_t stride = std::max<size_t>(1, nodes.size() / 300);
            for (size_t i = 0; i < nodes.size(); i += stride) outline->push_back(nodes[i]);
        }
        outlines.Bind(bi.Key(), outline);
    }
    out.meshMs = ms(t3, clk::now());

    out.bounds = Bnd_Box();
    for (int li : leaves) {
        ModelNode& n = out.nodes[li];
        if (outlines.IsBound(n.shape)) n.outline = outlines.Find(n.shape);
        const Bnd_Box& b = boxes.Find(n.shape);
        if (b.IsVoid()) continue;
        n.bounds = b.Transformed(n.trsf);
        out.bounds.Add(n.bounds);
    }

    report("Preparing view", -1);
    for (size_t i = 0; i < leaves.size(); ++i) {
        ModelNode& n = out.nodes[leaves[i]];
        out.triangles += triCount.Find(n.shape);
        Handle(AIS_ColoredShape) ais = new AIS_ColoredShape(n.shape);
        ais->SetLocalTransformation(n.trsf);
        const XCAFPrs_Style& inherited = leafStyles[i].style;
        ais->SetColor(inherited.IsSetColorSurf() ? inherited.GetColorSurf() : DefaultColor());

        // Colors defined on the part itself: whole part, solids, faces.
        NCollection_IndexedDataMap<TopoDS_Shape, XCAFPrs_Style, TopTools_ShapeMapHasher> styles;
        try { XCAFPrs::CollectStyleSettings(leafStyles[i].ref, TopLoc_Location(), styles); } catch (...) {}
        for (int s = 1; s <= styles.Extent(); ++s) {
            const XCAFPrs_Style& st = styles.FindFromIndex(s);
            if (!st.IsSetColorSurf()) continue;
            const TopoDS_Shape& sub = styles.FindKey(s);
            if (sub.IsSame(n.shape)) {
                if (!inherited.IsSetColorSurf()) ais->SetColor(st.GetColorSurf());
            } else {
                ais->SetCustomColor(sub, st.GetColorSurf());
            }
        }
        n.ais = ais;
    }
    return true;
}

}  // namespace

bool LoadModel(const std::wstring& path, const std::atomic<bool>& cancel,
               const ProgressFn& progress, Model& out, std::string& error) {
    try {
        return Build(path, cancel, progress, out, error);
    } catch (const Standard_Failure& f) {
        error = std::string("OpenCASCADE: ") + (f.GetMessageString() ? f.GetMessageString() : "failure");
    } catch (const std::exception& e) {
        error = e.what();
    } catch (...) {
        error = "unknown failure";
    }
    return false;
}

}  // namespace viewer
