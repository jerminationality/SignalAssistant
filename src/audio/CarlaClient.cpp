#include "CarlaClient.h"

// TODO(Copilot): Connect to JACK, create/attach Carla rack/graph, and load default chain
// Chain: Gate → EQ → Amp → IR → Limiter. Expose ports for 2×2 now; plan for 6× later.

bool CarlaClient::start() {
    // TODO(Copilot): Start JACK client, ensure server running, attach Carla, instantiate plugins
    // TODO(Copilot): Connect system:capture_* -> gate input; chain modules; last -> system:playback_*
    return true; // Return false if any step fails
}

void CarlaClient::stop() {
    // TODO(Copilot): Safely disconnect graph and free resources without blocking RT threads
}

void CarlaClient::setBufferSize(int /*frames*/) {
    // TODO(Copilot): Request new JACK buffer size and update latency text via signal
}

void CarlaClient::setSampleRate(int /*sr*/) {
    // TODO(Copilot): Rebuild graph or reconfigure plugins if necessary
}
