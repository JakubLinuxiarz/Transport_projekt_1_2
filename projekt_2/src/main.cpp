
#include "gtfs.hpp"
#include "event_graph.hpp"
#include "gui.hpp"

#include <iostream>

int main() {
    try {
        GTFSData bus =
            load_gtfs_folder("data/bus", "bus");

        GTFSData tram =
            load_gtfs_folder("data/tram", "tram");

        GTFSData data =
            merge_gtfs(bus, tram);

        EventGraph empty_graph;

        GuiApp app(data, empty_graph);
        app.run();

    } catch (const std::exception& e) {
        std::cerr
            << "Blad: "
            << e.what()
            << "\n";

        return 1;
    }

    return 0;
}
