// Generates a small colored test assembly (plate, posts, bolts as instances).
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRep_Builder.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Ax2.hxx>

int main(int argc, char** argv) {
    Handle(TDocStd_Document) doc = new TDocStd_Document("BinXCAF");
    Handle(XCAFDoc_ShapeTool) st = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    Handle(XCAFDoc_ColorTool) ct = XCAFDoc_DocumentTool::ColorTool(doc->Main());

    TopoDS_Shape plate = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 200, 120, 12).Shape();
    for (int i = 0; i < 4; ++i) {
        gp_Pnt c(i % 2 ? 180 : 20, i / 2 ? 100 : 20, -1);
        plate = BRepAlgoAPI_Cut(plate, BRepPrimAPI_MakeCylinder(gp_Ax2(c, gp::DZ()), 6, 14).Shape()).Shape();
    }
    TopoDS_Shape post = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp::DZ()), 14, 90).Shape();
    TopoDS_Shape bolt = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp::DZ()), 9, 6).Shape();
    TopoDS_Shape knob = BRepPrimAPI_MakeSphere(gp_Pnt(0, 0, 0), 22).Shape();
    TopoDS_Shape top = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 140, 60, 10).Shape();
    {
        BRepFilletAPI_MakeFillet f(top);
        for (TopExp_Explorer e(top, TopAbs_EDGE); e.More(); e.Next()) f.Add(3.0, TopoDS::Edge(e.Current()));
        top = f.Shape();
    }

    TDF_Label asmL = st->NewShape();
    auto addPart = [&](const TopoDS_Shape& s, Quantity_NameOfColor col) {
        TDF_Label l = st->AddShape(s, false);
        ct->SetColor(l, Quantity_Color(col), XCAFDoc_ColorSurf);
        return l;
    };
    auto place = [&](const TDF_Label& l, double x, double y, double z) {
        gp_Trsf t; t.SetTranslation(gp_Vec(x, y, z));
        st->AddComponent(asmL, l, TopLoc_Location(t));
    };
    TDF_Label lPlate = addPart(plate, Quantity_NOC_GRAY50);
    TDF_Label lPost = addPart(post, Quantity_NOC_ORANGE);
    TDF_Label lBolt = addPart(bolt, Quantity_NOC_GRAY20);
    TDF_Label lKnob = addPart(knob, Quantity_NOC_RED3);
    TDF_Label lTop = addPart(top, Quantity_NOC_STEELBLUE);
    // One face of the top painted differently (tests per-face color).
    {
        TopExp_Explorer e(top, TopAbs_FACE);
        TDF_Label fl = st->AddSubShape(lTop, e.Current());
        if (!fl.IsNull()) ct->SetColor(fl, Quantity_Color(Quantity_NOC_YELLOW), XCAFDoc_ColorSurf);
    }
    place(lPlate, 0, 0, 0);
    place(lPost, 60, 60, 12);
    place(lPost, 140, 60, 12);
    for (int i = 0; i < 4; ++i) place(lBolt, i % 2 ? 180 : 20, i / 2 ? 100 : 20, 12);
    place(lTop, 30, 30, 102);
    place(lKnob, 100, 60, 130);
    st->UpdateAssemblies();

    STEPCAFControl_Writer w;
    w.SetColorMode(true);
    w.Transfer(doc, STEPControl_AsIs);
    return w.Write(argc > 1 ? argv[1] : "sample.step") == IFSelect_RetDone ? 0 : 1;
}
