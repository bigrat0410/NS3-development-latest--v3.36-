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

## Completed Steps

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
- Created a `WifiHelper` and selected `WIFI_STANDARD_80211n`.
- Created a `WifiMacHelper` and selected `ns3::AdhocWifiMac`.
- Installed one Wi-Fi net device on each node with:
  `wifi.Install (wifiPhy, wifiMac, wifiNodes)`.
- Kept the default Wi-Fi rate control manager for now.
- Printed the number of installed Wi-Fi devices.
- Linked the scratch executable with `libwifi` and `libpropagation`.

What this version proves:

- The mobile nodes now also have Wi-Fi net devices.
- The 802.11n device setup builds before adding IP, applications, or throughput measurement.

Learning checkpoint:

- `WifiMacHelper` only stores the MAC configuration; it does not create devices
  on its own.
- `wifi.Install (...)` applies the PHY and MAC configuration to both nodes and
  returns the resulting `NetDeviceContainer`.
- The current source already contains this installation call, so the next
  layer to learn is IP rather than another Wi-Fi helper.

## Completed Steps

### Step 3: Add IPv4 to the Wi-Fi devices

Files updated:

- `two-node-ht.cc`
- `CMakeLists.txt`

What has been done:

- Added the ns-3 Internet module include.
- Linked the scratch executable with `libinternet`.
- Installed the Internet protocol stack on both nodes.
- Assigned IPv4 addresses to the two Wi-Fi devices from
  `10.1.1.0/24`.
- Printed the assigned address for each node.

The key code is:

```cpp
InternetStackHelper internet;
internet.Install (wifiNodes);

Ipv4AddressHelper ipv4;
ipv4.SetBase ("10.1.1.0", "255.255.255.0");
Ipv4InterfaceContainer wifiInterfaces = ipv4.Assign (wifiDevices);
```

Why these lines are needed:

- `InternetStackHelper` adds IPv4, ARP, UDP, and TCP support to each node.
- `Ipv4AddressHelper::Assign` gives each Wi-Fi device an address on the same
  subnet, allowing applications to address the other node.
- `NetDeviceContainer` contains link-layer devices; it is not itself an IP
  address container. `Ipv4InterfaceContainer` stores the assigned interfaces.

Before adding traffic, verify that the two interfaces receive `10.1.1.1` and
`10.1.1.2`.

What this version proves:

- The Wi-Fi devices now have both link-layer identity and IPv4 identity.
- The nodes are ready for applications to communicate over UDP or TCP.
- No application traffic has been generated yet, so there is still no
  throughput measurement.

Command-line test from the ns-3 root directory:

```bash
cmake --build cmake-cache --target scratch_learning_two-node-ht -j2
./build/scratch/learning/ns3.36.1-two-node-ht-default --PrintHelp
./build/scratch/learning/ns3.36.1-two-node-ht-default \\
  --simulationTime=2 --startDistance=10 --movingSpeed=2
```

The last command should print `10.1.1.1`, `10.1.1.2`, and an ending position
of `14:0:0` for node 1. The repository's `./ns3` wrapper currently fails with
the system Python 3.14 `argparse`, so the direct CMake target and executable
are the reliable commands for this workspace.

## Completed Steps

### Step 4: Add a UDP flow and measure throughput

Files updated:

- `two-node-ht.cc`
- `CMakeLists.txt`

What has been done:

- Added the ns-3 applications module include.
- Linked the scratch executable with `libapplications`.
- Installed a UDP `PacketSink` on node 1.
- Installed a `UdpClient` on node 0 targeting node 1 at UDP port `5000`.
- Added command-line parameters for traffic start time, packet interval,
  packet size, and maximum packet count.
- Calculated throughput from `PacketSink::GetTotalRx()` after the simulation.

The measured throughput is calculated as:

```text
throughput = receivedBytes * 8 / measurementTime / 1,000,000
```

Test result with a 3-second simulation, 1-second traffic start time, 1024-byte
packets, and a 10 ms interval:

- received bytes: `204800`
- measured UDP throughput: `0.8192 Mbps`
- node 1 moved from 5 m to 8 m

## Completed Steps

### Step 5: Measure the default Wi-Fi rate control

Files updated:

- `two-node-ht.cc`

What has been done:

- Kept the `WifiHelper` rate control configuration unchanged.
- Printed the rate manager installed on the first Wi-Fi device.
- Printed a one-line summary containing initial distance, rate manager,
  received bytes, and throughput.

In ns-3.36, the unchanged `WifiHelper` default is `ns3::IdealWifiManager`.
To compare distances fairly, run with `movingSpeed=0` so that the distance does
not change during a measurement. Use `packetInterval=0.0001` to offer
`81.92 Mbps` of UDP traffic; the previous 10 ms interval only offered
`0.8192 Mbps` and could not expose link capacity changes:

```bash
./build/scratch/learning/ns3.36.1-two-node-ht-default \\
  --simulationTime=3 --movingSpeed=0 --startDistance=5 --packetInterval=0.0001
./build/scratch/learning/ns3.36.1-two-node-ht-default \\
  --simulationTime=3 --movingSpeed=0 --startDistance=50 --packetInterval=0.0001
./build/scratch/learning/ns3.36.1-two-node-ht-default \\
  --simulationTime=3 --movingSpeed=0 --startDistance=150 --packetInterval=0.0001
```

Use the `Measurement summary` line from each run to compare the default rate
manager before selecting or implementing another algorithm. Verified results:

| Initial distance | Rate manager | Throughput |
| --- | --- | --- |
| 5 m | `ns3::IdealWifiManager` | `58.5728 Mbps` |
| 50 m | `ns3::IdealWifiManager` | `58.5687 Mbps` |
| 150 m | `ns3::IdealWifiManager` | `35.1396 Mbps` |

## Completed Steps

### Step 6: Compare a different rate-control manager

Files updated:

- `two-node-ht.cc`

What has been done:

- Added the `--rateManager` command-line parameter.
- Kept `ns3::IdealWifiManager` as the default value.
- Passed the selected TypeId to `WifiHelper::SetRemoteStationManager` before
  installing Wi-Fi devices.

Both `ns3::IdealWifiManager` and `ns3::MinstrelHtWifiManager` are built-in
ns-3.36 rate managers. For the current 802.11n scenario, compare them with:

```bash
./build/scratch/learning/ns3.36.1-two-node-ht-default \\
  --rateManager=ns3::IdealWifiManager --simulationTime=3 --movingSpeed=0 \\
  --startDistance=150 --packetInterval=0.0001
./build/scratch/learning/ns3.36.1-two-node-ht-default \\
  --rateManager=ns3::MinstrelHtWifiManager --simulationTime=3 --movingSpeed=0 \\
  --startDistance=150 --packetInterval=0.0001
```

The program prints the actual installed manager in `Wi-Fi rate control manager`
and the measurement result in `Measurement summary`.

Verified result at 150 m, with a 3-second fixed-distance run and a 0.1 ms
packet interval:

| Rate manager | Throughput |
| --- | --- |
| `ns3::IdealWifiManager` | `35.1396 Mbps` |
| `ns3::MinstrelHtWifiManager` | `30.6094 Mbps` |

The Minstrel HT result includes its initial probing and learning period, so a
single short run is not a final performance conclusion.

## Next Step

### Step 7: Repeat experiments with multiple random runs

The next lesson can add random seed and run parameters, then calculate average
throughput for each distance and rate manager.
