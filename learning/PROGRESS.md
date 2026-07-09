# scratch/learning Progress

This folder is being built as a step-by-step learning project for a minimal
ns-3 802.11n simulation.

## Goal

Build the project from zero in small steps:

1. Create two ns-3 nodes.
2. Add a simple mobility model where one node moves away at constant speed.
3. Configure an 802.11n Wi-Fi link.
4. Run the default Wi-Fi rate control algorithm first.
5. Add traffic and measure basic throughput.

## Current Status

### Step 0: Minimal ns-3 program

Files already present:

- `two-node-ht.cc`
- `CMakeLists.txt`

What has been done:

- Created a standalone scratch executable named `two-node-ht`.
- Added a command-line parameter `simulationTime`.
- Created two empty ns-3 nodes with `NodeContainer`.
- Started and stopped the simulator cleanly.

What this version proves:

- The folder is connected to the ns-3 build system.
- The executable can be built and run.
- We have a clean starting point before adding Wi-Fi, mobility, IP, or traffic.

### Step 1: Simple mobility model

Files updated:

- `two-node-ht.cc`
- `CMakeLists.txt`

What has been done:

- Added the ns-3 mobility module include.
- Added command-line parameters:
  - `startDistance`
  - `movingSpeed`
- Placed node 0 at `(0, 0, 0)`.
- Placed node 1 at `(startDistance, 0, 0)`.
- Kept node 0 fixed with `ConstantPositionMobilityModel`.
- Made node 1 move in the positive X direction with `ConstantVelocityMobilityModel`.
- Printed each node's starting and ending position.
- Linked the scratch executable with `libmobility`.

What this version proves:

- The two nodes now have physical positions.
- Node 1 can move away from node 0 at a constant speed.
- Mobility can be tested before adding Wi-Fi.

## Next Step

### Step 2: Minimal 802.11n Wi-Fi link

Files updated:

- `two-node-ht.cc`
- `CMakeLists.txt`

What has been done:

- Added the ns-3 Wi-Fi module include.
- Configured a YANS Wi-Fi channel with:
  - constant-speed propagation delay
  - log-distance propagation loss
- Configured an 802.11n 5 GHz, 20 MHz PHY.
- Installed one adhoc Wi-Fi net device on each node.
- Kept the default Wi-Fi rate control manager for now.
- Printed the number of installed Wi-Fi devices.
- Linked the scratch executable with `libwifi` and `libpropagation`.

What this version proves:

- The mobile nodes now also have Wi-Fi net devices.
- The 802.11n device setup builds before adding IP, applications, or throughput measurement.

## Next Step

Step 3 will add IP and a simple traffic flow:

- Install the internet stack.
- Assign IPv4 addresses to the Wi-Fi devices.
- Send one UDP flow between the two nodes.
- Measure received bytes and basic throughput.
