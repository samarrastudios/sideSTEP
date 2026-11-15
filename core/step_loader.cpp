#include "thumb_core.h"

#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <NCollection_DataMap.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFPrs.hxx>
#include <XCAFPrs_DocumentExplorer.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <XCAFPrs_Style.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <istream>
#include <streambuf>

namespace stepcore {
namespace {

// Read-only streambuf over a memory block, so we never copy the file.
class MemBuf : public std::streambuf {
public:
    MemBuf(const char* d, size_t n) {
        char* p = const_cast<char*>(d);
        setg(p, p, p + n);
    }
protected:
    pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override {
        char* base = eback();
        char* target = dir == std::ios_base::beg ? base + off
                     : dir == std::ios_base::cur ? gptr() + off
                                                 : egptr() + off;
        if (target < base || target > egptr()) return pos_type(off_type(-1));
        setg(base, target, egptr());
        return pos_type(target - base);
    }
    pos_type seekpos(pos_type pos, std::ios_base::openmode m) override {
        return seekoff(off_type(pos), std::ios_base::beg, m);
    }
};

const uint8_t kDefaultRgb[3] = {150, 160, 172};  // neutral steel

void ToRgb(const Quantity_Color& c, uint8_t out[3]) {
    // XCAF stores linear RGB; convert to sRGB for display.
    NCollection_Vec3<float> s = Quantity_Color::Convert_LinearRGB_To_sRGB(c.Rgb());
    for (int i = 0; i < 3; ++i)
        out[i] = (uint8_t)std::lround(std::min(1.0f, std::max(0.0f, s[i])) * 255.0f);
}

struct Rgb { uint8_t v[3]; bool set = false; };

bool StyleColor(const XCAFPrs_Style& st, Rgb& out) {
    if (st.IsSetColorSurf()) {
        ToRgb(st.GetColorSurfRGBA().GetRGB(), out.v);
        out.set = true;
        return true;
    }
    return false;
}

void AppendFace(const TopoDS_Face& face, const gp_Trsf& inst, const uint8_t rgb[3], Mesh& out) {
    TopLoc_Location loc;
    Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
    if (tri.IsNull() || tri->NbTriangles() == 0) return;
    if (!tri->HasNormals()) {
        try { tri->ComputeNormals(); } catch (...) {}
    }
    const bool hasN = tri->HasNormals();
    const gp_Trsf trsf = inst * loc.Transformation();
    const bool rev = (face.Orientation() == TopAbs_REVERSED) != (trsf.IsNegative());

    const int nbNodes = tri->NbNodes();
    std::vector<gp_Pnt> pts(nbNodes + 1);
    std::vector<gp_Dir> nrm;
    if (hasN) nrm.resize(nbNodes + 1);
    for (int i = 1; i <= nbNodes; ++i) {
        pts[i] = tri->Node(i).Transformed(trsf);
        if (hasN) {
            gp_Dir d = tri->Normal(i);
            nrm[i] = d.Transformed(trsf);
        }
    }
    for (int t = 1; t <= tri->NbTriangles(); ++t) {
        int a, b, c;
        tri->Triangle(t).Get(a, b, c);
        if (rev) std::swap(b, c);
        Triangle T;
        const int idx[3] = {a, b, c};
        for (int k = 0; k < 3; ++k) {
            const gp_Pnt& p = pts[idx[k]];
            T.p[k][0] = (float)p.X(); T.p[k][1] = (float)p.Y(); T.p[k][2] = (float)p.Z();
            if (hasN) {
                const gp_Dir& d = nrm[idx[k]];
                T.n[k][0] = (float)d.X(); T.n[k][1] = (float)d.Y(); T.n[k][2] = (float)d.Z();
            } else {
                T.n[k][0] = T.n[k][1] = T.n[k][2] = 0.0f;
            }
        }
        T.rgb[0] = rgb[0]; T.rgb[1] = rgb[1]; T.rgb[2] = rgb[2];
        out.tris.push_back(T);
    }
}

struct Leaf {
    TDF_Label label;
    TopoDS_Shape shape;
    gp_Trsf trsf;
    Rgb color;
};

bool Build(std::istream& in, const LoadOptions& opt, Mesh& out, std::string& error) {
    // Keep OCCT quiet: a thumbnail host has no console.
    Message::DefaultMessenger()->ChangePrinters().Clear();

    using clk = std::chrono::steady_clock;
    auto ms = [](clk::time_point a, clk::time_point b) { return (int)std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count(); };
    const auto t0 = clk::now();
    STEPCAFControl_Reader reader;
    reader.SetColorMode(true);
    reader.SetNameMode(false);
    reader.SetLayerMode(false);
    reader.SetPropsMode(false);
    reader.SetGDTMode(false);
    reader.SetMatMode(false);
    reader.SetViewMode(false);
    if (reader.ChangeReader().ReadStream("model.step", in) != IFSelect_RetDone) {
        error = "not a readable STEP file";
        return false;
    }
    const auto t1 = clk::now();
    out.parseMs = ms(t0, t1);
    Handle(TDocStd_Document) doc = new TDocStd_Document("BinXCAF");
    if (!reader.Transfer(doc)) {
        error = "STEP transfer failed";
        return false;
    }

    const auto t2 = clk::now();
    out.transferMs = ms(t1, t2);

    // Collect leaf instances (parts) with their placement and inherited color.
    std::vector<Leaf> leaves;
    for (XCAFPrs_DocumentExplorer it(doc, XCAFPrs_DocumentExplorerFlags_OnlyLeafNodes); it.More(); it.Next()) {
        const XCAFPrs_DocumentNode& node = it.Current();
        Leaf lf;
        lf.label = node.RefLabel;
        lf.shape = XCAFDoc_ShapeTool::GetShape(node.RefLabel);
        if (lf.shape.IsNull()) continue;
        lf.trsf = node.Location.Transformation();
        StyleColor(node.Style, lf.color);
        leaves.push_back(lf);
    }
    if (leaves.empty()) {
        error = "no geometry in file";
        return false;
    }
    out.partCount = (int)leaves.size();

    // Global bounds (from geometry, cheap) decide one absolute tolerance, so
    // tiny fasteners in a big assembly get coarse meshes instead of thousands
    // of triangles nobody can see at 256px.
    NCollection_DataMap<TopoDS_Shape, Bnd_Box, TopTools_ShapeMapHasher> boxes;
    Bnd_Box global;
    for (const Leaf& lf : leaves) {
        if (!boxes.IsBound(lf.shape)) {
            Bnd_Box b;
            // Vertex positions are enough to size the tolerance, and far
            // cheaper than exact geometric bounds on big assemblies.
            for (TopExp_Explorer vx(lf.shape, TopAbs_VERTEX); vx.More(); vx.Next())
                b.Add(BRep_Tool::Pnt(TopoDS::Vertex(vx.Current())));
            if (b.IsVoid()) {
                try { BRepBndLib::Add(lf.shape, b, false); } catch (...) {}
            }
            boxes.Bind(lf.shape, b);
        }
        const Bnd_Box& b = boxes.Find(lf.shape);
        if (!b.IsVoid()) global.Add(b.Transformed(lf.trsf));
    }
    if (global.IsVoid()) {
        error = "empty bounds";
        return false;
    }
    double x0, y0, z0, x1, y1, z1;
    global.Get(x0, y0, z0, x1, y1, z1);
    const double diag = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
    const double defl = std::max(diag * opt.relativeDeflection, 1e-6);

    out.uniqueShapes = boxes.Extent();
    const auto t3 = clk::now();
    for (NCollection_DataMap<TopoDS_Shape, Bnd_Box, TopTools_ShapeMapHasher>::Iterator bi(boxes); bi.More(); bi.Next()) {
        try {
            BRepMesh_IncrementalMesh mesher(bi.Key(), defl, false, opt.angularDeflection, opt.parallel);
        } catch (...) {
            // A part that fails to mesh is skipped; the rest still renders.
        }
    }

    out.meshMs = ms(t3, clk::now());

    for (const Leaf& lf : leaves) {
        // Per-face / per-solid colors defined on the part itself.
        NCollection_IndexedDataMap<TopoDS_Shape, XCAFPrs_Style, TopTools_ShapeMapHasher> styles;
        try { XCAFPrs::CollectStyleSettings(lf.label, TopLoc_Location(), styles); } catch (...) {}
        NCollection_DataMap<TopoDS_Shape, Rgb, TopTools_ShapeMapHasher> faceColor;
        // Coarser shapes first so face-level colors win.
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 1; i <= styles.Extent(); ++i) {
                const TopoDS_Shape& s = styles.FindKey(i);
                const bool isFace = s.ShapeType() == TopAbs_FACE;
                if ((pass == 1) != isFace) continue;
                Rgb c;
                if (!StyleColor(styles.FindFromIndex(i), c)) continue;
                for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
                    TopoDS_Shape key = ex.Current().Located(TopLoc_Location());
                    if (faceColor.IsBound(key)) faceColor.ChangeFind(key) = c;
                    else faceColor.Bind(key, c);
                }
            }
        }
        for (TopExp_Explorer ex(lf.shape, TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Face& face = TopoDS::Face(ex.Current());
            const uint8_t* rgb = lf.color.set ? lf.color.v : kDefaultRgb;
            TopoDS_Shape key = face.Located(TopLoc_Location());
            if (const Rgb* fc = faceColor.Seek(key)) rgb = fc->v;
            AppendFace(face, lf.trsf, rgb, out);
        }
    }
    if (out.tris.empty()) {
        error = "no surfaces to draw";
        return false;
    }
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (const Triangle& t : out.tris)
        for (int k = 0; k < 3; ++k)
            for (int a = 0; a < 3; ++a) {
                mn[a] = std::min(mn[a], t.p[k][a]);
                mx[a] = std::max(mx[a], t.p[k][a]);
            }
    for (int a = 0; a < 3; ++a) { out.bmin[a] = mn[a]; out.bmax[a] = mx[a]; }
    return true;
}

}  // namespace

bool LoadStep(const char* data, size_t size, const LoadOptions& opt, Mesh& out, std::string& error) {
    try {
        MemBuf buf(data, size);
        std::istream in(&buf);
        return Build(in, opt, out, error);
    } catch (const Standard_Failure& f) {
        error = std::string("OpenCASCADE: ") + (f.GetMessageString() ? f.GetMessageString() : "failure");
    } catch (const std::exception& e) {
        error = e.what();
    } catch (...) {
        error = "unknown failure";
    }
    return false;
}

bool LoadStepFile(const std::string& path, const LoadOptions& opt, Mesh& out, std::string& error) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { error = "cannot open file"; return false; }
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<char> data((size_t)n);
    if (n > 0 && !f.read(data.data(), n)) { error = "cannot read file"; return false; }
    return LoadStep(data.data(), data.size(), opt, out, error);
}

bool RenderStepThumbnail(const char* data, size_t size, int px, Image& out, std::string& error) {
    Mesh mesh;
    LoadOptions opt;
    if (!LoadStep(data, size, opt, mesh, error)) return false;
    RenderIso(mesh, px, out);
    return true;
}

}  // namespace stepcore
