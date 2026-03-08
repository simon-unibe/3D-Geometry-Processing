#include "IsoContouringPlugin.hh"
#include "IsoContouringToolbar.hh"

#include <OpenFlipper/BasePlugin/PluginFunctions.hh>
#include <ObjectTypes/TriangleMesh/PluginFunctionsTriangleMesh.hh>
#include <ObjectTypes/PolyhedralMesh/PluginFunctionsPolyhedralMesh.hh>
#include <ACG/Utils/ColorCoder.hh>

#define ASSIGNMENT_03_SOLUTION 1

using SVH = OpenMesh::SmartVertexHandle;
using VH = OpenMesh::VertexHandle;
using EH = OpenMesh::EdgeHandle;
using FH = OpenMesh::FaceHandle;
using Point = TriMesh::Point;

void IsoContouringPlugin::initializePlugin()
{
    tool_ = new IsoContouringToolbar();
    connect(tool_, &IsoContouringToolbar::valueChanged, this, &IsoContouringPlugin::slotUpdateIsoContours);
    emit addToolbox(tr("GP Ex03: Iso-Contouring") , tool_ );
}

void IsoContouringPlugin::extractIsoContour(PolyhedralMeshObject &output, TriMesh& input,
        ScalarVProp const &func, double value, OpenMesh::Vec4f const &color)
{
    PolyhedralMesh &out_mesh = *output.mesh();


    // This "property" allows us to save an _output_ OVM vertex handle per _input_ edge
    auto out_vert = OpenMesh::makeTemporaryProperty<EH, OpenVolumeMesh::VH>(input);

    // Initially, no edge contains any output vertex, mark them all invalid:
    out_vert.set_range(input.edges(), PolyhedralMesh::InvalidVertexHandle);

    std::set<FH> relevant_faces;
    for (const auto eh: input.edges()) {
        SVH v0 = eh.v0();
        SVH v1 = eh.v1();

        if((func[v0] - value) * (func[v1] - value) < 0.0)
            // TODO: test if we have to insert a vertex on this edge
            // You can access the per-vertex function values using func[v0]
        {
            auto pos = Point(0.);
            // TODO: compute interpolated output vertex position on the edge
            // you can access input vertex positions like this:
            // const Point &p0 = input.point(eh.v0());
            // const Point &p1 = input.point(eh.v1());

            double t = (func[v0] - value) / (func[v0] - func[v1]);
            std::cout << value << '\t' << func[v0] << '\t' << func[v1] << '\t' << t << '\n';
            pos = (1.0 - t) * input.point(v0) + t * input.point(v1);

            out_vert[eh] = out_mesh.add_vertex(pos);
            // The faces incident to this edge contain iso-contour edge segments:
            for (const auto fh: eh.faces()) {
                relevant_faces.insert(fh);
            }
        }
    }
    for (const auto _fh: relevant_faces) {
        auto fh = input.make_smart(_fh);
        // TODO: If output vertices were generated for any edges of this face,
        //       connect the generated vertices by adding edges to `out_mesh`
        // You can use fh.edges() to iterate over the face's edges:
        //     for (const auto eh: fh.edges()) { /* ... */ }
        // and access values you saved in out_vert like this:
        //     auto out_v = out_vert[eh];
        // Use `out_v.is_valid()` to figure out if that vertex was initialized or is invalid
        // You can use this snippet to generate an output edge:
        //     auto eh = out_mesh.add_edge(out_v0, out_v1);
        //     output.colors()[eh] = color;

        const auto ehs = fh.edges().to_vector();
        std::vector<OpenVolumeMesh::VH> vertices;

        if (out_vert[ehs[0]].is_valid()) vertices.push_back(out_vert[ehs[0]]);
        if (out_vert[ehs[1]].is_valid()) vertices.push_back(out_vert[ehs[1]]);
        if (out_vert[ehs[2]].is_valid()) vertices.push_back(out_vert[ehs[2]]);

        auto eh = out_mesh.add_edge(vertices[0], vertices[1]);
        output.colors()[eh] = color;
    }
}

void IsoContouringPlugin::sliceMesh(TriMesh &mesh)
{
    // A property saves data per entity (in this case, a `double` per vertex)
    auto prop = OpenMesh::getOrMakeProperty<OpenMesh::VertexHandle, double>(mesh, "func");

    /// Compute per-vertex function value:
    auto eval_f = [&](VH vh) -> double {
        const auto pos = mesh.point(vh);
        return pos[2];
        // Note: This is a good place to try more interesting functions than just
        //       the z coordinate of the vertex positions.
        //       This is optional :)
        // These functions in the x-y plane could be a start:
        //    f(x,y) = \sqrt{x^2+y^2}-1
        //    f(x,y) = y^2 - \sin{(x^2)}
        //    f(x,y) = \sin{(2x+2y)} - \cos{(4xy)} +1
        //    f(x,y) = (3x^2 - y^2)^2y^2-(x^2+y^2)^4
    };

    for (const auto vh: mesh.vertices()) {
        prop[vh] = eval_f(vh);
    }

    /// We use a OpenVolumeMesh PolyhedralMesh for visualisation purposes:
    PolyhedralMeshObject &output_mesh_object = get_output_mesh_object();
    output_mesh_object.mesh()->clear(); // we re-use this mesh, so get rid of the old stuff

    // We want to distribute iso-contours uniformly among the entire function range:
    const double min_value = mesh.vertices().min(prop);
    const double max_value = mesh.vertices().max(prop);

    const int n_slices = tool_->n_slices();
    const double offset = tool_->offset(); // this is in the range [0..1]
    emit log(LOGINFO, "Countouring into " + QString::number(n_slices)
            + " slices, offset " + QString::number(offset));

    const auto coco = ACG::ColorCoder(min_value, max_value);

    for (int i = 0; i < n_slices; ++i) {
        double t = (i + offset) / n_slices;
        double value = (1-t) * min_value + t * max_value;
        auto color = coco.color_float4_raw(value);
        extractIsoContour(output_mesh_object, mesh, prop, value, color);
    }
    // We need to let OpenFlipper know that we modified the object,
    // so it will re-upload it to the GPU (and let other plugins know that something changed)
    emit updatedObject(output_mesh_object.id(), UPDATE_ALL);
}

PolyhedralMeshObject& IsoContouringPlugin::get_output_mesh_object()
{
    if (auto *pmo = PluginFunctions::polyhedralMeshObject(output_pmo_id_)) {
        return *pmo;
    } else {
        emit addEmptyObject(DATA_POLYHEDRAL_MESH, output_pmo_id_);
        pmo = PluginFunctions::polyhedralMeshObject(output_pmo_id_);
        pmo->setName("IsoContouring result");
        pmo->setObjectDrawMode(
                  ACG::SceneGraph::DrawModes::getDrawMode("Edges (colored per edge)")
                  // You may want to comment out the next line if the vertices annoy you:
                | ACG::SceneGraph::DrawModes::getDrawMode("Vertices")
                , true);
        pmo->materialNode()->set_point_size(3); // you can tweak this for debug visualisation purposes
        pmo->materialNode()->set_line_width(3);
        return *pmo;
    }
}

void IsoContouringPlugin::slotUpdateIsoContours()
{
    // Open the "Data Control" toolbox (in the plugin list on the right hand side)
    // to see and adjust which objects are marked as "target"
    for (BaseObjectData * obj: PluginFunctions::objects(
                PluginFunctions::TARGET_OBJECTS,
                DATA_TRIANGLE_MESH))
    {
        TriMeshObject *tmo = PluginFunctions::triMeshObject(obj);
        sliceMesh(*tmo->mesh());
    }
}


void IsoContouringPlugin::slotObjectUpdated(int _identifier, const UpdateType& _type)
{
    const bool geometry_changed =
        _type.contains(UPDATE_ALL)
        || _type.contains(UPDATE_GEOMETRY)
        || _type.contains(UPDATE_TOPOLOGY);
    if (!geometry_changed) return;
    TriMeshObject *tmo = PluginFunctions::triMeshObject(_identifier);
    if (tmo == nullptr) { // not a trimesh
        return;
    }
    sliceMesh(*tmo->mesh());
}
