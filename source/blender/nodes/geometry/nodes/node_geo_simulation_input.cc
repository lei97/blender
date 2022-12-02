/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "BKE_compute_cache.hh"
#include "BKE_compute_contexts.hh"
#include "BKE_scene.h"

#include "DEG_depsgraph_query.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_simulation_input_cc {

NODE_STORAGE_FUNCS(NodeGeometrySimulationInput);

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Geometry>(N_("Geometry"));

  b.add_output<decl::Float>(N_("Delta Time"));
  b.add_output<decl::Float>(N_("Elapsed Time"));
  b.add_output<decl::Geometry>(N_("Geometry"));
}

static void node_layout(uiLayout * /*layout*/, bContext * /*C*/, PointerRNA * /*ptr*/)
{
  // const NodeGeometrySimulationInput &storage = node_storage(
  //     *static_cast<const bNode *>(ptr->data));
  // const bNodeTree &node_tree = *reinterpret_cast<const bNodeTree *>(ptr->owner_id);
  // const bNode *sim_output = node_tree.node_by_id(storage.output_node_id);
  // if (sim_output) {
  //   uiItemL(layout, sim_output->name, ICON_PHYSICS);
  // }
}

static void node_init(bNodeTree *tree, bNode *node)
{
  NodeGeometrySimulationInput *data = MEM_cnew<NodeGeometrySimulationInput>(__func__);

  VectorSet<int32_t> sim_output_ids;
  Set<int32_t> sim_input_output_ids;
  for (bNode *other_node : tree->all_nodes()) {
    if (other_node->type == GEO_NODE_SIMULATION_INPUT && other_node != node &&
        other_node->storage) {
      const NodeGeometrySimulationInput &storage = node_storage(*other_node);
      sim_input_output_ids.add_new(storage.output_node_id);
    }
    else if (other_node->type == GEO_NODE_SIMULATION_OUTPUT) {
      sim_output_ids.add_new(other_node->identifier);
    }
  }

  sim_output_ids.remove_if(
      [&](const int32_t identifier) { return sim_input_output_ids.contains(identifier); });

  if (sim_output_ids.size() == 1) {
    data->output_node_id = sim_output_ids[0];
  }
  else {
    data->output_node_id = 0;
  }

  node->storage = data;
}

static void node_geo_exec(GeoNodeExecParams params)
{
  const NodeGeometrySimulationInput &storage = node_storage(params.node());
  const int32_t sim_output_node_id = storage.output_node_id;

  const Scene *scene = DEG_get_input_scene(params.depsgraph());
  const float scene_ctime = BKE_scene_ctime_get(scene);
  const int scene_frame = int(scene_ctime);

  const GeoNodesLFUserData &lf_data = *params.user_data();
  bke::ComputeCaches &all_caches = *lf_data.modifier_data->cache_per_frame;

  const bke::NodeGroupComputeContext cache_context(lf_data.compute_context, sim_output_node_id);
  const bke::SimulationCache *cache = all_caches.lookup_context(cache_context.hash());
  if (!cache) {
    params.set_output("Geometry", params.extract_input<GeometrySet>("Geometry"));
    return;
  }

  if (const bke::GeometryCacheValue *data = cache->value_before_time(scene_frame)) {
    if (params.lazy_output_is_required("Geometry")) {
      params.set_output("Geometry", std::move(data->geometry_set));
    }
    if (params.lazy_output_is_required("Delta Time")) {
      params.set_output("Delta Time", scene_ctime - data->time);
    }
    if (params.lazy_output_is_required("Elapsed Time")) {
      params.set_output("Elapsed Time", scene_ctime - cache->geometry_per_frame.first().time);
    }
    return;
  }

  if (params.lazy_require_input("Geometry")) {
    return;
  }

  GeometrySet geometry_set = params.extract_input<GeometrySet>("Geometry");
  if (params.lazy_output_is_required("Delta Time")) {
    params.set_output("Delta Time", -1.0f); /* TODO: How to get this?*/
  }
  if (params.lazy_output_is_required("Elapsed Time")) {
    if (cache->geometry_per_frame.is_empty()) {
      params.set_output("Elapsed Time", 0.0f);
    }
    else {
      params.set_output("Elapsed Time", scene_ctime - cache->geometry_per_frame.first().time);
    }
  }
  params.set_output("Geometry", std::move(geometry_set));
}

}  // namespace blender::nodes::node_geo_simulation_input_cc

void register_node_type_geo_simulation_input()
{
  namespace file_ns = blender::nodes::node_geo_simulation_input_cc;

  static bNodeType ntype;
  geo_node_type_base(&ntype, GEO_NODE_SIMULATION_INPUT, "Simulation Input", NODE_CLASS_INTERFACE);
  ntype.initfunc = file_ns::node_init;
  ntype.geometry_node_execute = file_ns::node_geo_exec;
  ntype.declare = file_ns::node_declare;
  ntype.draw_buttons = file_ns::node_layout;
  node_type_storage(&ntype,
                    "NodeGeometrySimulationInput",
                    node_free_standard_storage,
                    node_copy_standard_storage);

  ntype.geometry_node_execute_supports_laziness = true;
  // ntype.declaration_is_dynamic = true;
  nodeRegisterType(&ntype);
}