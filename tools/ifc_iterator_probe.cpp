#include <ifcgeom/iterator.h>
#include <ifcgeom/kernel_registry.h>
#include <ifcparse/file.h>
#include <plugin/plugin.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef PARAMETRIC_CAD_IFCOPENSHELL_PLUGIN_DIR
#define PARAMETRIC_CAD_IFCOPENSHELL_PLUGIN_DIR ""
#endif

namespace {

void stage(const char* name)
{
    std::cout << "[ifc_iterator_probe][thread=" << std::this_thread::get_id()
              << "] " << name << '\n' << std::flush;
}

void loadPlugin(ifcopenshell::plugin::manager& manager, const char* name)
{
    stage(name);
    const auto paths = manager.discover_exact(name);
    if (paths.empty()) throw std::runtime_error(std::string("Plugin not found: ") + name);
    const auto module = manager.load(paths.front());
    std::cout << "  loaded=" << module.path() << " id=" << module.meta().id
              << " schema=" << module.meta().schema << '\n' << std::flush;
}

}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: ifc_iterator_probe <file.ifc> [plugin-directory]\n";
        return 2;
    }

    try {
        const std::string pluginDirectory = argc >= 3
            ? argv[2] : PARAMETRIC_CAD_IFCOPENSHELL_PLUGIN_DIR;
        if (pluginDirectory.empty()) throw std::runtime_error("Plugin directory is empty");
        ifcopenshell::plugin::set_search_paths({pluginDirectory});
        ifcopenshell::plugin::manager manager;
        manager.add_search_path(pluginDirectory);
        stage("plugin manager initialized");
        loadPlugin(manager, "parse_schema_ifc2x3");
        loadPlugin(manager, "parse_schema_ifc4");
        loadPlugin(manager, "geometry_mapping_ifc2x3");
        loadPlugin(manager, "geometry_mapping_ifc4");
        loadPlugin(manager, "geometry_kernel_opencascade");

        ifcopenshell::file file(ifcopenshell::uninitialized_tag{});
        stage("before schema initialize");
        if (!file.initialize(argv[1]) || !file.good())
            throw std::runtime_error("IFC schema initialization failed");
        stage("after schema initialize");

        ifcopenshell::geom::settings settings;
        ifcopenshell::logger logger;
        stage("before kernel construct");
        auto kernel = ifcopenshell::geom::kernels::construct(
            &file, "opencascade", settings, logger);
        stage("after kernel construct");
        stage("before iterator construct / mapping instance creation");
        ifcopenshell::geom::iterator iterator(std::move(kernel), settings, &file, logger);
        stage("after iterator construct / mapping instance creation");
        stage("before iterator initialize");
        if (!iterator.initialize()) throw std::runtime_error("Iterator initialization failed");
        stage("after iterator initialize");
        std::cout << "standalone iterator probe passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ifc_iterator_probe failed: " << exception.what() << '\n';
        return 1;
    }
}
