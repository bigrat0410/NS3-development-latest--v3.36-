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

## Completed Steps

### Step 7: Reproduce Scenario 1 and average multiple random runs

Files updated:

- `two-node-ht.cc`
- `result_figure.py`
- `Result_recor.md`

What has been completed:

- Changed the topology to one AP and one STA, with rate adaptation on the AP.
- Added a 60 Mbps UDP downlink and periodic distance-throughput sampling.
- Reproduced the 1–41 m moving scenario with the paper-aligned propagation parameters.
- Added `Seed` and `Run` command-line parameters.
- Ran matching 20-seed experiments for `IdealWifiManager` and
  `MinstrelHtWifiManager`, then generated their averaged comparison figure.
- Investigated the non-zero Minstrel tail by experimentally removing `HtMcs0`;
  this was an isolated experiment and the restriction is not present in the
  saved reproduction source.

The detailed parameters and measured averages are recorded in `Result_recor.md`.

## Current Step

### Step 8: Build the RL environment from the communication boundary inward

Reference files:

- `../reinrate/annotated_src/rl-env.h`
- `../reinrate/annotated_src/rl-env.cc`

Compatibility findings:

- The annotated code cannot be copied verbatim into ns-3.36. Its
  `Ns3AIRL`/`ns3-ai-module.h` API and its `DoGetDataTxVector` signature differ
  from the interfaces available in this workspace.
- The implementation will therefore be rebuilt in small, compiling steps while
  preserving the original algorithm's Observation and Action semantics.

Completed in the first RL step:

- Created `rl-env.h` with the packed `AiConstantRateEnv` Observation and
  `AiConstantRateAct` Action structures.
- Created `rl-env.cc` with compile-time layout checks: 20 bytes for Observation
  and 2 bytes for Action.
- Added `rl-env.cc` to the reproduction CMake target so every lesson is compiled
  and verified immediately.

Completed in the second RL step:

- Declared the minimal `RLRateEnv` class derived from
  `WifiRemoteStationManager`.
- Defined its constructor and destructor in `rl-env.cc`.
- Registered the original TypeId name `ns3::rl-rateWifiManager` and its Wi-Fi
  parent type. The class intentionally has no `AddConstructor` yet because the
  mandatory rate-manager virtual functions have not been implemented.

Completed in the third RL step:

- Declared and implemented all mandatory RTS/DATA success and failure report
  callbacks required by `WifiRemoteStationManager`.
- Each callback currently logs its inputs but deliberately leaves the RL state
  unchanged. This establishes the feedback interface before adding statistics.

Scenario 1 MAC behavior audit:

- The UDP flow maps to best effort (BE). In ns-3.36, `BE_MaxAmpduSize`
  defaults to 65535 bytes, so A-MPDU is enabled. Actual aggregation begins only
  after a Block Ack agreement exists and more than one MPDU is queued; the
  sustained 60 Mbps load normally satisfies this condition.
- A-MSDU is separate and remains disabled because `BE_MaxAmsduSize` defaults to
  zero.
- RTS/CTS is available but effectively inactive in this two-node scenario.
  `RtsCtsThreshold` defaults to 65535 bytes and the trigger uses a strict
  `PSDU size > threshold` comparison. There are also no legacy nodes that
  require non-HT protection.
- The RTS report functions are still mandatory parts of the rate-manager
  interface even when the current scenario does not normally call them.

Research direction retained after the original reproduction:

- Classic rate adaptation often observes a missing ACK without knowing whether
  it came from fading, an ordinary collision, a hidden terminal, or queue/channel
  congestion. Minstrel mainly converts all of these into a lower delivery
  probability, while the original REINRATE policy is driven primarily by SNR.
- A future context-aware extension can combine SNR mean/variance, ACK and retry
  rates, CCA busy ratio, CW, queue length/delay, and low-rate RTS probes to infer
  the likely cause.
- A promising action space is joint `next_mcs` and `use_rts`: lower MCS for
  fading, selectively enable RTS for hidden terminals, and avoid treating queue
  congestion as a weak radio link.
- This extension must be evaluated in separate fading, contending-node, hidden
  terminal, and overloaded-queue scenarios. Simulator-only ground truth may be
  used for labels/evaluation, but policy inputs should remain measurements that
  a real sender could obtain.
- Strict REINRATE reproduction remains the current priority; these additional
  observations and actions must not be introduced until the original baseline
  is working.

Completed in the fourth RL step:

- Implemented `DoCreateStation()` using the base `WifiRemoteStation` state.
- The manager can now allocate one independent station record for every remote
  MAC address. Algorithm-specific per-peer fields will be introduced only when
  they are actually needed.

Completed in the fifth RL step:

- Added `m_dataMode` and `m_ctlMode` to store the DATA and RTS transmission
  modes.
- Exposed them as the `DataMode` and `ControlMode` TypeId attributes, both with
  the safe legacy default `OfdmRate6Mbps`.

Completed in the sixth RL step:

- Implemented fixed-rate `DoGetDataTxVector()` and `DoGetRtsTxVector()` using
  the configured DATA and control modes.
- DATA transmission now derives a valid NSS, guard interval, channel width,
  preamble, power level, and aggregation flag from local/peer capabilities.
- Added `AddConstructor<RLRateEnv>()`; the class now implements every mandatory
  pure virtual function and can be instantiated by its TypeId name.
- Verified a 3-second fixed-rate smoke test with
  `rateManager=ns3::rl-rateWifiManager`; the manager was created successfully
  and delivered 5.18016 Mbps using its default 6 Mbps OFDM DATA mode.

Completed in the seventh RL step:

- Added the first RL state members: latest SNR, contention window, and
  effective throughput.
- Updated SNR from ordinary DATA feedback and added the A-MPDU Block ACK status
  callback, which is important because Scenario 1 enables BE A-MPDU by default.
- CW and throughput currently hold initialized values only; their trace/statistic
  sources have deliberately not been mixed into this step.

Completed in the eighth RL step:

- Aligned the signal observation with the paper's sender-side ACK RSS concept:
  ordinary DATA stores ACK SNR and A-MPDU stores Block ACK SNR. With the fixed
  noise floor used by Scenario 1, this is a one-to-one transform of ACK RSS.
- Overrode `SetupMac()` and connected directly to this device's BE `CwTrace`,
  avoiding the original code's global wildcard trace connection.
- `m_cw` now follows the current BE contention window: it grows after failed
  channel access/transmission and resets after success according to ns-3 MAC
  behavior.

Completed in the ninth RL step (C++ environment layer complete):

- Reorganized `rl-env.h/.cc` into a complete ns-3.36-compatible environment
  layer with fixed-rate and AI-enabled operating modes.
- Added `EnableAi` (default false), `PayloadSize` (1420 bytes), and target
  `Ber` (1e-6) attributes.
- Aligned the first baseline with the source's effective per-packet behavior:
  after a DATA result, the next DATA transmission opportunity prepares one new
  Observation. Ordinary DATA therefore updates per packet; with A-MPDU enabled,
  one update corresponds to an aggregate transmission opportunity rather than
  every sub-MPDU.
- Throughput uses effective payload bytes divided by time since the preceding
  decision and retains the source's `1024*1024` unit conversion.
- Failed DATA attempts also prepare a zero-throughput observation.
- Added an SNR-threshold calculation for `max_mcs` and a value-based MCS lookup;
  the code no longer assumes that an operational-MCS list index always equals
  the requested MCS value.
- Added optional ns3-ai Observation/Action exchange. Shared memory is touched
  only when `EnableAi=true`, so the manager remains independently testable.
- Added lifecycle cleanup for the local CW trace.
- Linked the reproduction target with `libai`.

Verification performed:

- Fixed-rate default OFDM smoke test: 5.18016 Mbps over a 3-second run.
- Fixed `HtMcs7` smoke test: 59.2038 Mbps over a 3-second run, exercising the
  HT/A-MPDU path.
- Source-frequency ns3-ai test with A-MPDU: 142 Observation/Action exchanges in
  0.7 seconds of offered traffic; C++ and Python exited normally.
- Source-frequency test with A-MPDU disabled: 509 exchanges in 0.2 seconds of
  offered traffic, confirming approximately one decision per DATA packet at the
  link's service rate.

Global logic constraints before algorithm reproduction:

- Exactly one rate manager should use `EnableAi=true` in the current struct-based
  singleton interface. Scenario 1 already satisfies this by using RL only at the
  AP and ConstantRate at the STA.
- `PayloadSize` must match the application UDP payload; it is intentionally an
  effective-byte estimate and excludes UDP/IP/MAC headers.
- ACK SNR is a one-to-one substitute for ACK RSS only while the receiver noise
  floor is fixed. The current Scenario 1 satisfies that assumption.
- The current Action space is deliberately restricted to single-stream
  `HtMcs0`-`HtMcs7`; the `nss` field is retained for protocol compatibility but
  is not yet applied.
- Python's source inference rule is intentionally retained for the first
  baseline: if a candidate action is below `max_mcs`, it is raised to
  `max_mcs`, even though C++ computes the highest feasible MCS.

Completed in the tenth RL step (Python binding boundary):

- Added the reproduction-local `reproduction_reinrate_py` pybind11 module.
- Exposed every Observation and Action field with the same names as the C++
  packed structures.
- Exposed the ns3-ai receive/send semaphore methods and shared-structure access.
- Kept policy code out of the binding: this module transports state and action
  only; it does not choose MCS or calculate reward.

Completed in the eleventh RL step (reward probe without REINFORCE):

- Added `reward_probe.py`, a minimal synchronized Python loop that reads each
  Observation, prints the source reward components, and returns the current MCS
  unchanged.
- Copied the original REINRATE 20/40 MHz reference-throughput tables and reward
  formula without clipping so mismatches remain visible.
- Deliberately excluded policy-network construction, action sampling, gradients,
  optimizer state, and model persistence.
- Identified a reward-model mismatch: the source 20 MHz MCS7 reference is
  29.6 Mbps, while the current A-MPDU scenario reaches roughly 55-59 Mbps. Even
  with A-MPDU disabled, short-window throughput can slightly exceed the table,
  so the documented 0-1 reward is not mathematically bounded without clipping
  or scenario-matched normalization.

Reward probe verification:

- With the current A-MPDU scenario and source-frequency decisions, MCS7 rewards
  were commonly about 6-7 because measured goodput was roughly 55-57 Mbps while
  the source reference remained 29.6 Mbps.
- With A-MPDU disabled, MCS7 rewards varied around the source reference and
  reached about 1.69 in very short per-packet intervals.
- These outputs confirm that the code implements the source formula correctly;
  whether to preserve or redesign that formula is an algorithm decision.

Completed in the twelfth RL step (source-baseline alignment):

- Removed the corrected 12 ms periodic decision schedule for the first baseline
  because the annotated source declares `m_timeStep=12 ms` but never uses it.
- Added source inference preprocessing: `log(snr+1)`, a five-sample pool, and
  the original 1.5-standard-deviation outlier removal.
- Added a local `MacTx` first-packet trigger and `MeasurementStart=0.5 s`, so
  association/management feedback is not counted as 1420-byte UDP payload.
- Read `BE_MaxAmpduSize` from the local MAC: when A-MPDU is enabled, payload
  accounting uses only Block ACK MPDU counts and ignores ordinary management
  acknowledgments; when disabled, each ordinary DATA success counts once.
- Added explicit candidate/decision logging and the source `max_mcs` lower-bound
  rule. A candidate MCS0 was correctly forced to MCS7 while `max_mcs=7`.
- Kept the original reward table and cubic formula without clipping.
- Verified fixed-rate operation, AI synchronization, Observation ranges, reward
  calculation, and Action application. The environment is ready for the policy
  network baseline.

Completed in the thirteenth RL step (source REINFORCE baseline):

- Added `reinforce_model.py` with the source policy network
  `1 -> 16 -> 16 -> 16 -> 8`, ReLU hidden activations, output Softmax, Adam
  learning rate `1e-4`, and Xavier initialization of the four Linear weights.
- Preserved the source training action rule: 30% uniform random exploration;
  otherwise sample from `Categorical(policy(state))`. Each selected action's
  log probability is retained for the policy-gradient update.
- Preserved the source REINFORCE update with `gamma=0.99` and
  `loss = -log(pi(action|state)) * return`. Because the source calls `update()`
  after every feedback, the buffer normally contains one reward and one action;
  the single-element return is therefore not normalized.
- Added `reinforce_run.py` to connect the Python agent to the reproduction-local
  ns3-ai binding, run Scenario 1, save the learned model, and record a detailed
  per-decision trace. The training input remains only `log(ACK_SNR+1)`; CW,
  throughput, and `max_mcs` are transported and logged but are not policy input.
- Preserved the runnable source main-loop limit: with the default
  `epochs=1000`, calls 0-1000 enter `train_reinforce` (the first call selects an
  action and the following 1000 calls update the policy). All later send
  opportunities reuse the final action without further reward calculation or
  learning.
- Added optional single-series title/label arguments to `result_figure.py` while
  leaving its existing defaults unchanged.

Source-baseline Scenario 1 experiment (superseded by the final-code rerun below):

- Used the same radio, mobility, traffic, and sampling parameters documented in
  `Result_recor.md`: 802.11n/5 GHz/channel 36/20 MHz, LogDistance exponent 3,
  66.6777 dB reference loss, 20 dBm transmit power, zero dB noise figure,
  disabled preamble detection, 60 Mbps UDP load with 1420-byte payloads,
  0.5-second sampling, start distance 1 m, speed 0.5 m/s, and 80-second run.
- Used the first shared Minstrel seed, `Seed=774015`, with `Run=1`.
- The run completed 13,813 shared-memory decisions: 1 initial training action,
  1000 gradient updates, then 12,812 held-action decisions. The last trained
  action was MCS6, matching the source's post-epoch hold behavior.
- The throughput CSV contains all 158 expected samples. Mean throughput was
  13.077374 Mbps, maximum throughput was 53.6192 Mbps, and the final nonzero
  sample was 0.20448 Mbps at 12.75 m (23.5 s); the remaining 112 samples were
  zero. This poor late-range behavior is retained as a source-baseline result,
  not corrected by adding feasibility clamping or continued training.
- Generated outputs:
  `my-project-results/reproduction-scenario1-reinrate-seed774015.csv`,
  `my-project-results/reproduction-scenario1-reinrate-seed774015.svg`,
  `my-project-results/reproduction-scenario1-reinrate-decisions-seed774015.csv`,
  and `my-project-results/reproduction-scenario1-reinrate-seed774015.pt`.

Next step:

- Inspect the first source-faithful curve and decision trace before deciding
  whether the next experiment should evaluate the saved model in inference mode
  or change one documented source behavior at a time.

Final-code rerun after the interrupted session:

- Re-ran the same 80 s Scenario 1 parameters with `Seed=774015`, `Run=1`, and
  `epochs=1000` because the earlier artifacts predated the final rebuilt code.
- Completed 19,928 shared-memory decisions, 1,001 training calls, and 1,000
  gradient updates. The final trained action was MCS0 and was held thereafter,
  exactly following the source main-loop behavior.
- Verified all 158 throughput samples from 1.0-79.5 s and 1.5-40.75 m. Mean
  throughput was 8.691119 Mbps and maximum throughput was 52.074200 Mbps.
- Regenerated the throughput CSV, decision CSV, model, and SVG. Python policy
  randomness is intentionally not seeded because the source does not seed it.

Online-training logic correction and test:

- Corrected reward attribution to use the MCS in the current C++ observation,
  which produced the current throughput feedback, instead of the one-step-old
  `last_state` MCS. Verified zero nonzero-reward rows for current MCS0.
- Made full-simulation online training the default. A configured finite epoch
  limit now transitions to deterministic policy inference instead of holding
  one stochastic boundary action forever.
- Added a reproducible Python agent seed and ran the complete same-seed 80 s
  scenario with `Seed=774015`, `Run=1`, and `agentSeed=774015`.
- Completed 11,495 decisions and 11,494 updates. REINRATE mean throughput was
  16.167721 Mbps versus 29.115966 Mbps for Ideal and 29.544335 Mbps for
  Minstrel-HT on the same ns-3 seed.
- Generated explicit online CSV/model/SVG outputs and a same-seed three-series
  comparison SVG. The corrected online result still does not match Ideal.

PHY-airtime reward and adaptive convergence step:

- Replaced inter-decision elapsed time with ns-3 PHY transmission duration for
  the DATA/AMPDU payload attempts. Failed retry airtime is included in the
  denominator. Maximum observed reward throughput fell from 611.557 to 61.507
  Mbps and maximum reward fell from 20,308.361 to 8.972.
- Added a 200-sample local convergence detector using reward stability, overall
  and recent success rates, and dominant-action share. Convergence disables
  epsilon exploration and uses greedy actions; a recent success-rate drop
  reopens exploration for the moving link.
- The 80 s same-seed run completed 11,928 decisions and 11,927 updates with two
  convergence events. Mean sampled throughput was 16.625719 Mbps, still below
  Ideal 29.115966 Mbps and Minstrel-HT 29.544335 Mbps.
- Deferred two recorded issues: failed transmissions do not provide a fresh ACK
  SNR, and the source-style learner still performs single-sample REINFORCE
  updates without a baseline or multi-step return.

10 m throughput-drop diagnosis and convergence caveat:

- The convergence detector is a local engineering addition, not part of the
  original ReinRate source. It currently declares convergence over 200 samples
  when half-window mean reward changes by at most 20%, overall success is at
  least 75%, recent-50 success is at least 80%, and one MCS occupies at least
  45%. It then disables epsilon exploration but continues gradient updates;
  recent-50 success below 70% reopens exploration.
- The first convergence occurred at decision 1,336 and selected greedy MCS7.
  The policy then kept MCS7 while `max_mcs` fell from 7 to 6 around decision
  3,200 and to 5 around decision 3,600. Throughput consequently fell from
  61.507 to 16.967 Mbps before exploration reopened at decision 3,773.
- The single-sample positive-only policy loss continued reinforcing every
  successful MCS7 transmission even as its reward declined. At decision 3,700,
  `loss/reward` implies an MCS7 policy probability of about 99.7%; zero reward
  stops reinforcement but does not penalize the action. Reopening 30% epsilon
  exploration therefore still leaves roughly 73.5% probability on MCS7.
- The external curve reflects the same transition: 58.731 Mbps at 10.0 m,
  45.054 Mbps at 10.75 m, 31.149 Mbps at 11.0 m, and 14.791 Mbps at 11.25 m.
  The channel was still usable at MCS5/6; the drop was caused by failure to
  downshift, not a sudden propagation collapse.
- A continuously moving online learner should not treat local stability as
  global convergence. Future work should keep a small exploration floor,
  immediately raise exploration when SNR/`max_mcs`/success changes, evaluate
  stability per channel-state region, and continue online learning throughout.
  Stale SNR after failed transmissions and the single-sample/no-baseline update
  remain recorded but were intentionally not changed in this step.

12 ms fixed decision-window experiment:

- Replaced the per-DATA `DoGetDataTxVector()` Observation/Action exchange with
  a `DecisionInterval` timer, defaulting to 12 ms from the traffic measurement
  start. The timer continues producing zero-throughput feedback during a run of
  failed transmissions.
- The window aggregates the existing successful-payload and PHY-airtime
  counters before one exchange. The Python learner remains the same
  single-feedback REINFORCE implementation; batching, a baseline, state changes,
  and exploration changes were deliberately excluded from this experiment.
- With `Seed=774015`, `Run=1`, and `agentSeed=774015`, the 80 s run completed
  6,624 timer-driven decisions and produced a 12.779713 Mbps mean over all 158
  external throughput samples. This is below the 16.167721 Mbps per-feedback
  online result, so fixed timing alone is not a performance improvement.

Paper20 REINFORCE implementation:

- Replaced the 12 ms timer as the RL decision source with a fixed 20-completed-
  DATA-MPDU window. The selected MCS is held for the whole window, and the next
  Observation/Action exchange occurs before the following DATA transmission.
- Added a discounted per-packet return in the environment. Each completed
  packet contributes the source reward using its actual PHY airtime, with
  `gamma=0.99`; the 20 contributions are sent as one window return.
- Added `paper20_run.py` and `paper20_model.py`. The policy consumes four
  normalized inputs: ACK SNR, CW, current MCS, and window throughput. It has
  eight MCS logits and samples from the explicit 70% policy / 30% uniform
  epsilon behavior distribution.
- The first one-seed 80 s comparison disables A-MPDU for all three algorithms
  so a transmission aggregate cannot cross a 20-packet action boundary.
  Paper20 completed 1,989 windows but reached only 5.034207 Mbps mean sampled
  throughput, compared with 18.112006 Mbps for Ideal and 17.851019 Mbps for
  Minstrel-HT. The logic is now aligned to the intended packet-window update,
  but the paper reward still favors high-MCS successes and does not yet adapt
  well to the continuously degrading link.

## 2026-07-16 - Offline-Trained REINFORCE V1 Stage Result

Scope and scenario:

- Replaced the deleted `paper20_model.py` and `paper20_run.py` experiment with
  separate offline-training model, training, and frozen-evaluation entry points.
- Scenario 1 uses 20 MHz, one spatial stream, default A-MPDU aggregation,
  1420-byte UDP payloads, a 60 Mbps offered load, 1 m initial distance, and a
  station moving away at 0.5 m/s for 80 s.
- A policy action is selected after each completed PPDU/A-MPDU attempt and is
  applied to the next aggregate. Aggregate goodput is successful UDP payload
  bits divided by the full simulated interval since the previous action,
  including channel access, failed attempts, and acknowledgements.

V1 learning configuration:

- Policy network: `4 -> 16 -> 16 -> 16 -> 8`, ReLU hidden activations, Xavier
  weight initialization, and a Softmax distribution over HtMcs0-HtMcs7.
- State: `log1p(SNR)/12`, `log2(CW+1)/10`, `current_mcs/7`, and
  `min(previous_goodput/60, 1)`. Previous goodput supplies immediate loss
  information when the latest ACK-derived SNR is stale.
- Immediate reward: `clip(aggregate_goodput_mbps / 60, 0, 1)`. It has no MCS
  index multiplier, per-MCS reference table, exponent, or discounted future
  reward.
- Update: collect `K=20` on-policy aggregate transitions, subtract the batch
  mean reward as a baseline, standardize advantages, and perform one policy
  update. No second value/baseline network is used.
- Optimizer: Adam with learning rate `1e-4`, gradient norm clipping at `1.0`,
  and training actions sampled from `Categorical(policy)`. V1 used entropy
  coefficient `0.01`, decayed to zero during training, without inference-time
  exploration.
- Training retained policy and Adam state across repeated Scenario 1 runs,
  varied the ns-3 training seed from `774015`, and evaluated every five
  episodes with held-out seed `884015` using deterministic `argmax` actions.

V1 convergence and held-out test:

- The stage convergence rule required mean maximum policy probability at least
  `0.90`, action-sequence agreement at least `0.98`, validation-throughput
  change no more than `0.5 Mbps`, and three consecutive successful validation
  checks after at least 15 episodes. V1 met this rule at episode 45; the best
  validation checkpoint reached `25.739021 Mbps` at episode 40.
- A separate held-out test used seed `994015`, deterministic frozen inference,
  and the same A-MPDU/single-stream scenario. Sample means were 29.114 Mbps for
  Ideal, 29.386 Mbps for Minstrel-HT, and 25.741 Mbps for frozen REINFORCE.
  Peaks were 59.617, 59.640, and 53.665 Mbps respectively.
- Frozen REINFORCE reached 88.9% of the Ideal mean. It still produced 29 zero-
  throughput half-second samples at the far-distance tail, so this is recorded
  as a useful stage result rather than a claim of global optimality.
- Fixed comparison artifacts:
  `my-project-results/reproduction-scenario1-offline-reinforce-v1-seed994015.svg`
  and
  `my-project-results/reproduction-scenario1-offline-reinforce-v1-seed994015.csv`.

Follow-up experiments that added a 95%-of-Ideal convergence gate and alternative
exploration/saturation controls were stopped at the user's request. They did not
replace the V1 stage result above.

## 2026-07-16 - Blind-Training State and Exploration Correction

Implementation constraints and changes:

- No distance labels, per-region MCS candidates, `max_mcs` action masking, or
  forced rate downshifts are used. Training explores all eight actions through
  `q(a|s) = (1-epsilon) * pi(a|s) + epsilon/8`; frozen inference remains
  deterministic `argmax(pi)`.
- A missing ACK/Block ACK no longer overwrites the most recent valid SNR with
  zero. Throughput remains zero on a failed aggregate, so the four-dimensional
  state still exposes the failure through TP without creating a false SNR=0
  absorbing state.
- The scenario is consistently 20 MHz, one spatial stream, A-MPDU enabled,
  start distance 0 m, speed 1 m/s, and duration 40 s. Training uses K=20
  aggregate feedback samples per update and a `4 -> 16 -> 16 -> 16 -> 8`
  policy.
- Immediate reward is normalized aggregate application goodput, with `-1` for
  zero-goodput failures. The state baseline is `0.8 * previous_goodput/60`;
  no second value network is used. Policy entropy, rather than epsilon-mixture
  entropy, is regularized to prevent hidden early saturation.
- Validation also records zero-throughput fraction and longest consecutive zero
  run. Ideal is used only as an offline acceptance benchmark and is never
  supplied to policy state, reward, action selection, or gradient updates.
- Evaluation now writes the comparison CSV to a distinct path and no longer
  overwrites the raw REINFORCE throughput CSV.

Training and held-out result:

- Fresh training ran for 60 full trajectories. A generic four-times weight for
  failed samples was then tested from episodes 61-80; it did not improve the
  already saturated checkpoint, so the best validation policy remains episode
  60 at 26.487 Mbps. The 95%-of-Ideal convergence gate was not met.
- Held-out seed 994015 produced means of 30.447 Mbps for Ideal, 30.248 Mbps for
  Minstrel-HT, and 26.271 Mbps for frozen REINFORCE. All three peaks were
  59.595 Mbps.
- The near-distance defect is corrected: the frozen policy selects MCS7 and no
  longer caps the peak at MCS6. No SNR=0 observation occurred after the first
  valid feedback.
- The far-distance defect is not accepted as fixed. The policy used only
  MCS7/MCS3/MCS2, stayed at MCS2 after it ceased working, and produced 13
  consecutive zero-throughput half-second samples beginning at 33.5 m. It did
  not learn the required MCS1/MCS0 recovery states.
- Comparison SVG:
  `my-project-results/reproduction-scenario1-offline-reinforce-test-seed994015.svg`.
  Comparison data:
  `my-project-results/reproduction-scenario1-offline-reinforce-comparison-seed994015.csv`.

## 2026-07-17 - Corrected 80-Second Training and Five-Seed Evaluation

Scenario correction:

- Restored the intended Scenario 1 parameters consistently in training and
  evaluation: 80 s duration, 80 s training trajectory, 1 m start distance,
  and 0.5 m/s movement. Every episode therefore covers the complete 1-41 m
  trajectory; the previous random-start calculation was removed.
- Kept 20 MHz, one spatial stream, A-MPDU enabled, 1420-byte UDP payloads,
  and a 60 Mbps offered load.

Algorithm corrections:

- Added normalized consecutive aggregate failures as a fifth policy input, so
  identical stale-SNR observations can distinguish an isolated loss from a
  sustained outage. The policy is now `5 -> 16 -> 16 -> 16 -> 8`.
- Training still samples from the epsilon mixture
  `q=(1-epsilon)*pi+epsilon/8`, but the policy-gradient term now uses
  importance-corrected `pi(a|s)/q(a|s) * log(pi(a|s))`. Frozen inference
  remains deterministic `argmax(pi)`.
- Wrote corrected checkpoints and history to new paths, preserving the earlier
  experiment artifacts. Extended `result_figure.py` to average multiple
  REINFORCE CSV files instead of plotting one RL run against averaged baselines.

Training result:

- Ran 60 fresh 80-second episodes and validated every five episodes. The 95%
  of Ideal acceptance gate was not reached. The best validation checkpoint was
  episode 25 at 25.330352 Mbps; later training saturated and did not replace it.
- The corrected best policy learned a stable `MCS6 -> MCS3 -> MCS1` path rather
  than the previous `MCS7/MCS3/MCS2` policy that remained stuck at failed MCS2.

Five-seed frozen evaluation (`994015` through `994019`):

- Ideal mean: 29.112397 Mbps.
- Minstrel-HT mean: 28.120631 Mbps.
- Corrected frozen REINFORCE mean: 25.244795 Mbps, or 86.7% of Ideal.
- Four RL runs had no zero-throughput half-second samples; one run had 3 of
  158. The earlier long zero-throughput far-distance tail did not recur.
- Average comparison SVG:
  `my-project-results/reproduction-scenario1-offline-reinforce-corrected-5seed.svg`.
- Average comparison CSV:
  `my-project-results/reproduction-scenario1-offline-reinforce-corrected-5seed.csv`.

## 2026-07-20 - Failure-Triggered Adjacent-MCS Probability Transfer

Experiment design:

- Keep the shared `4 -> 16 -> 16 -> 8` policy network and per-sample
  REINFORCE loss. A batch contains 10 aligned state/action/reward samples.
- Replace cubic throughput reward with signed aggregate feedback:
  `reward = clip(throughput_mbps / 60, 0, 1) - 2.0 * failure_ratio`.
- Compute `failure_ratio = failed_mpdus / (successful_mpdus + failed_mpdus)`
  for every completed aggregate feedback.
- Smooth partial aggregate failures with
  `failure_ewma = 0.8 * previous + 0.2 * failure_ratio`.
- Start downshift pressure at `failure_ewma=0.10` and reach full pressure at
  `failure_ewma=0.40`, using
  `degrade = clip((failure_ewma - 0.10) / 0.30, 0, 1)`.
- Transfer at most 70% of the current MCS probability to the adjacent lower
  MCS: `transfer = 0.7 * degrade * P(current_mcs)`. Network parameters are not
  reset and other action probabilities are unchanged by this transfer.
- During training, mix the adjusted policy with 30% uniform exploration:
  `q = 0.7 * adjusted_policy + 0.3 / 8`, sample from `q`, and use `log q(a|s)`
  in the per-sample REINFORCE loss. Frozen validation uses deterministic
  `argmax(adjusted_policy)` without uniform exploration.
- Record `failure_ratio`, `failure_ewma`, and `degrade` in every decision row.
  Artifacts use the new `offline-failure-transfer` prefix.

The planned check is five complete 80-second training episodes, followed by a
single held-out frozen validation and one SVG containing all five 0.5-second
training throughput curves.

## 2026-07-20 - Per-MCS Reference-Goodput Reward

The failure-triggered adjacent-MCS probability-transfer experiment above was
abandoned for the active implementation. The active code was restored to the
preceding per-sample REINFORCE baseline before applying this replacement.

Reference calibration:

- Added `--fixedMcs` to `two-node-ht.cc` for AP-side ConstantRate calibration;
  the normal Ideal, Minstrel, and RL manager paths are unchanged.
- Calibrated MCS0-MCS7 independently at a fixed 1 m distance, zero mobility,
  60 Mbps offered load, default A-MPDU, seed 664000, and the Scenario 1 PHY.
- The maximum 0.5-second application goodputs for MCS0-MCS7 were respectively
  `[5.81632, 11.8371, 17.7898, 23.8106, 35.7386, 47.6666, 53.6646,
  59.5946] Mbps`.
- Raw calibration files are
  `my-project-results/reproduction-mcs-reference-mcs0.csv` through
  `reproduction-mcs-reference-mcs7.csv`.

Reward definition:

- For the action that produced the current aggregate feedback, compute
  `ratio = clip(actual_goodput / reference_goodput[current_mcs], 0, 1)`.
- At `ratio >= 0.95`, retain the preceding positive cubic reward
  `reward = actual_goodput ** 3`.
- Below `0.95`, first remove the positive reward and set a negative penalty
  whose magnitude grows linearly with the reference-relative deficit:
  `reward = -(reference_goodput ** 3) * (0.95 - ratio) / 0.95`.
- Keep the per-sample loss `-mean(log_pi(a_t|s_t) * reward_t)`. Do not group
  rewards by action, do not use a baseline, and do not retain failure EWMA,
  probability transfer, or adjacent-MCS override logic.
- Record `reference_goodput_mbps`, `goodput_ratio`, and `reward_penalty` in
  each decision row. New artifacts use the `offline-reference-reward` prefix.

Five-episode result:

- Ran five complete 80-second training trajectories with seeds 774015-774019;
  every training CSV contains all 158 samples from 1.0 to 79.5 seconds.
- Episode mean throughputs were 18.559221, 13.539392, 13.522857, 13.510056,
  and 13.560529 Mbps. Held-out seed 884015 frozen validation after episode 5
  reached 13.585689 Mbps versus 29.113229 Mbps for Ideal.
- The reference-ratio threshold generated 5,284 negative feedbacks in episode
  1 and about 2,150-2,290 per later episode, so failed high MCS actions now
  receive explicit negative policy-gradient weight.
- The policy did not learn the desired state-dependent MCS7-to-MCS6 sequence.
  It collapsed to the broadly robust MCS3 after episode 1: episode 5 selected
  MCS3 for 9,834 of 10,119 feedback decisions and selected MCS7 once.
- This experiment confirms that the MCS-relative reward detects action failure,
  but pure policy sampling without an exploration floor loses the high-SNR
  actions after a broadly successful lower MCS dominates repeated updates.
- Combined five-trajectory SVG:
  `my-project-results/reproduction-scenario1-offline-reference-reward-5episodes.svg`.

## 2026-07-20 - Reference Reward V2: Airtime Fix and Exploration Restore

Corrections:

- Corrected the interpretation of cross-episode state: episode 2 inherits the
  network parameters and optimizer state, but every probability vector is
  freshly computed as `policy(current_episode_state; inherited_parameters)`.
  No probability vector is copied directly from episode 1.
- Replaced the invalid per-A-MPDU denominator `callback_time - action_start`
  with PHY transmission duration calculated from the attempted aggregate
  payload, current MCS TxVector, channel width, and PHY band. This removed
  callback-spacing spikes such as the earlier 571.261 Mbps MCS3 observation.
- A 5-second mixed-MCS sanity run observed stable per-MCS maxima from 6.425 to
  64.495 Mbps; all five full training runs had the same 64.495 Mbps global
  observation maximum and no hundreds-of-Mbps spikes.
- Restored 30% uniform exploration. Training samples from
  `q = 0.7 * policy + 0.3 / 8` and uses `log q(action|state)` in the per-sample
  loss. Frozen validation remains deterministic `argmax(policy)`.
- Capped healthy positive reward at the calibrated reference:
  `min(actual_goodput, reference_goodput[mcs]) ** 3`. The existing negative
  reference-relative penalty below the 0.95 health threshold is unchanged.
- New artifacts use the `offline-reference-reward-v2` prefix.

Five-episode result:

- All five training CSVs contain the complete 158 samples from 1.0 to 79.5 s.
  Mean throughputs were 17.472254, 12.859949, 12.400807, 13.121085, and
  13.190255 Mbps. Frozen validation on seed 884015 reached 13.585689 Mbps.
- Exploration remained active after policy concentration: episode 5 selected
  MCS3 7,837 times, while each other MCS was still sampled approximately
  380-426 times.
- The model still concentrated its deterministic policy on MCS3. With the
  measurement and exploration defects removed, the remaining behavior comes
  from the reward distribution over the full one-way trajectory and inadequate
  state-conditioned separation after sequential updates. In episode 5, mean
  sampled rewards for MCS5, MCS6, and MCS7 were negative, while MCS3 remained
  broadly positive over a much larger portion of the trajectory.
- Combined five-trajectory SVG:
  `my-project-results/reproduction-scenario1-offline-reference-reward-v2-5episodes.svg`.

## 2026-07-20 - Full-Episode On-Policy REINFORCE Check

Training organization and scaling:

- Normalize the existing cubic reference reward by the MCS7 reference cubed.
  Healthy rewards and reference-relative penalties now preserve their previous
  inter-MCS scale while remaining within `[-1, 1]`.
- Hold policy parameters fixed throughout each complete 80-second rollout.
  Store every aligned state, sampled action, and immediate reward. After ns-3
  finishes, recompute `log q(action|state)` under the same unchanged behavior
  policy and perform exactly one optimizer step using the mean of all per-sample
  REINFORCE loss terms. Every sample contributes a gradient term; only the
  parameter update is performed once per episode.
- Keep the 30% uniform exploration mixture in the fixed behavior policy.
- Verified in a unit check that parameters remain bitwise unchanged while
  collecting samples and change only when `finish_episode()` is called.

Two-episode learning-rate check:

- First ran two complete episodes at learning rate `3e-4`. Each episode made
  one update, and the final policy spread was only about 0.00047 around the
  initial uniform probability 0.125. Frozen validation reached 17.265039 Mbps.
  Its two-curve SVG is
  `my-project-results/reproduction-scenario1-offline-episodic-reinforce-lr3e4-2episodes.svg`.
- Because the measured change was very small, repeated the two-episode test
  from a fresh initialization at learning rate `1e-3`, without changing any
  other algorithm or scenario parameter.
- The two training means were 12.281166 and 12.246944 Mbps. Held-out frozen
  validation after the second update reached 17.256699 Mbps.
- The final raw policy remained close to uniform, ranging approximately from
  0.1241 to 0.1256 in representative states. MCS4 had the largest probability
  by a small margin at all tested SNR values, so deterministic argmax amplified
  a roughly 0.0015 spread into an all-MCS4 validation policy. Two full-episode
  updates were therefore insufficient to learn an SNR-conditioned partition,
  but they also avoided the rapid softmax saturation seen with thousands of
  online updates per episode.
- Both `1e-3` training CSVs contain all 158 samples from 1.0 to 79.5 seconds.
  Combined SVG:
  `my-project-results/reproduction-scenario1-offline-episodic-reinforce-lr1e3-2episodes.svg`.

## 2026-07-20 - Rollback to 15:37 Standard-REINFORCE Version, 10 Episodes

- Abandoned the later reference-goodput, failure-transfer, PHY-airtime,
  normalized-reward, and full-episode-update experiments in the active code.
- Restored the 15:37 logic: four-state `4 -> 16 -> 16 -> 8` policy, direct
  sampling from the network policy with no epsilon mixture, cubic instantaneous
  throughput reward, per-sample `-log_pi(a_t|s_t) * reward_t`, online updates
  every 10 aligned samples, learning rate `1e-3`, and callback-interval A-MPDU
  throughput measurement.
- Ran ten complete 80-second training episodes with seeds 774015-774024. Every
  throughput CSV contains all 158 external samples from 1.0 to 79.5 seconds.
- Episode mean throughputs were 13.030207, 15.224557, 14.137878, 15.373961,
  17.348015, 18.596753, 16.833072, 17.773222, 17.499289, and 20.102170 Mbps.
- Frozen validation was 15.865746 Mbps after episode 5 and 17.123111 Mbps after
  episode 10. The 95%-of-Ideal convergence target was not reached.
- Episode 10 selected MCS4 5,369 times, MCS7 3,702 times, and MCS3 2,072 times;
  it still had 57 zero-throughput half-second samples and a longest zero run of
  55 samples.
- Artifacts use the `offline-standard-reinforce-10ep` prefix. Combined SVG:
  `my-project-results/reproduction-scenario1-offline-standard-reinforce-10episodes.svg`.

## 2026-07-20 - Continue Standard REINFORCE Through Episode 32

- Resumed the unchanged 15:37 standard-REINFORCE checkpoint after episode 10
  and trained 22 additional complete 80-second episodes, through episode 32.
- Frozen validation was run every four episodes during the continuation. Mean
  validation throughput for episodes 12, 16, 20, 24, 28, and 32 was 18.568993,
  19.884023, 22.019271, 23.262543, 23.382612, and 23.573436 Mbps.
- Mean training throughput at episodes 12, 16, 20, 24, 28, and 32 was
  20.840568, 22.482448, 22.948780, 23.654686, 24.038046, and 23.940265 Mbps.
- The best frozen validation result was 23.573436 Mbps at episode 32. This is
  still below the configured 27.657568 Mbps convergence target.
- The eight-curve plot selects complete episode 4, 8, 12, 16, 20, 24, 28, and
  32 trajectories from 1.0 through 79.5 seconds.

## 2026-07-21 - Full-Episode Discounted REINFORCE Baseline

Algorithm restoration:

- One complete 80-second Scenario 1 simulation is one episode. Policy parameters
  remain fixed while collecting all aligned states, actions, and immediate
  rewards; `finish_episode()` performs exactly one optimizer update and then
  clears the complete trajectory without discarding a tail.
- Restored discounted returns entirely in Python with `gamma=0.99`:
  `G_t = r_t + 0.99 * G_(t+1)`, through the final reward. Returns are normalized
  over the episode before applying the REINFORCE loss.
- Used Adam with the requested learning rate `1e-4`. The immediate reward is the
  stable baseline `clip(throughput_mbps / 60, 0, 1)`; cubic reward, reference-MCS
  penalty, action aggregation, epsilon mixing, and manual probability transfer
  are absent.
- Removed C++ `RewardDiscount` and its cross-packet accumulation. The shared
  observation now names the field `raw_reward`, while simulation time remains in
  `simulation_time`; all temporal discounting is owned by Python.
- Training refreshes one combined SVG at every tenth episode. The final plot
  contains episodes 10 through 110 and is
  `my-project-results/reproduction-scenario1-offline-full-episode-reinforce-every-10-episodes.svg`.

Training result and convergence decision:

- Ran 110 complete 80-second episodes, producing exactly 110 gradient updates.
  Each episode contained approximately 12,000 completed aggregate feedbacks,
  and every history row reports one update and zero discarded samples.
- The best frozen validation throughput was 13.585689 Mbps against a 27.657568
  Mbps target. From episode 30 through episode 110 the frozen policy remained at
  13.585689 Mbps, selected MCS3, and had maximum action probability only about
  0.126, close to the initial uniform probability 0.125.
- Final validation had a 0.386076 zero-throughput fraction and a longest zero run
  of 61 half-second samples, while Ideal had neither. This is not convergence;
  training was stopped at a complete 10-episode reporting boundary because nine
  consecutive validations showed no performance improvement.
- The normalized-throughput reward is retained as the clean diagnostic baseline.
  The result indicates that one `1e-4` update per roughly 12,000-action episode
  does not create useful state-conditioned separation at this decision rate;
  this failure should not be hidden by restoring cubic/reference penalties.

## 2026-07-21 - Mini-Batch Gamma and Action-Relative Reward Experiment

Changes:

- Added six state features: normalized SNR, CW, current MCS, throughput, SNR
  delta, and consecutive zero-throughput streak.
- Added calibrated per-MCS maximum goodputs and an action-relative reward. The
  final tested form was
  `absolute_goodput * (0.5 + 0.5 * relative_goodput^3)`, where absolute goodput
  is normalized by 60 Mbps and relative goodput is normalized by the selected
  MCS maximum. The multiplicative form prevents a low-MCS action from receiving
  a high reward merely because it reached its own low-rate ceiling.
- Replaced the one-update-per-episode collector with discounted mini-batches.
  The selected stable configuration uses `batch_size=20`, `gamma=0.99`, and
  Adam learning rate `1e-4`; each batch computes reverse discounted returns and
  performs one update, with a final short tail batch at episode end.

Small tests and decision:

- Five-episode `batch_size=10`, `lr=3e-4` test collapsed rapidly to MCS1 and
  reached only 11.333692 Mbps validation throughput. It was rejected.
- Five-episode `batch_size=20`, `lr=1e-4` test was stable and increased policy
  concentration without immediate collapse.
- A fresh 25-episode run with the latter configuration produced 14,471 updates
  and a maximum policy probability of about 0.375. However, the policy selected
  MCS2 for most states and validation throughput remained 13.582233 Mbps,
  essentially unchanged from the previous uniform-policy baseline. The Ideal
  target remains 27.657568 Mbps.
- This experiment is not converged. The action-relative reward still favors a
  robust medium MCS when averaged over the complete 1-41 m trajectory. More
  iterations would reinforce that bias; the next correction should add a
  state-value baseline/advantage estimate or use shorter physical-time reward
  windows before further long training.
- The 25-episode curve is
  `my-project-results/reproduction-scenario1-offline-full-episode-reinforce-every-25-episodes.svg`.

## 2026-07-21 - Source-Cadence Static Sanity Check

Mechanism isolated:

- Kept the paper values unchanged: Adam learning rate `1e-4`, `gamma=0.99`,
  and epsilon `0.3`. The static channel is fixed at 1 m, A-MPDU is disabled,
  one action controls a completed 20-packet window, and each episode lasts
  1 second.
- With `update_batch=20`, each short episode supplies only about two to three
  optimizer steps. A 20-sample return is standardized to approximately unit
  scale before the loss is evaluated. This removes the raw reward magnitude
  from the gradient and leaves too few `1e-4` updates; after 100 episodes the
  greedy policy was still MCS6.
- With `update_batch=1`, every completed window performs an optimizer step.
  A one-element rollout has `G_t = r_t`, so `gamma` has no cross-window effect,
  and the return-normalization branch is skipped. The raw window reward, often
  near 100, therefore directly scales `-log(pi(a_t|s_t))` just as in the source
  code's usual single-feedback update path.

Observed `update_batch=1` convergence:

| Episode | Greedy MCS | P(MCS6) | P(MCS7) | Mean raw window reward |
| --- | ---: | ---: | ---: | ---: |
| 1 | 6 | 0.252 | 0.073 | 39.371 |
| 100 | 6 | 0.383 | 0.353 | 77.617 |
| 110 | 7 | 0.345 | 0.428 | 87.524 |
| 200 | 7 | 0.156 | 0.723 | 89.179 |

- The greedy action changed from MCS6 to the known optimum MCS7 at episode
  110 and remained there through episode 200. Mean MCS0 probability fell from
  0.116 at episode 1 to less than 0.001 at episode 200.
- This shows that the nominal `learning_rate` and `gamma` cannot be interpreted
  independently of reward scale, return normalization, and optimizer cadence.
  It validates the update mechanism on a fixed optimum; it does not yet prove
  convergence of the state-dependent policy over the full moving trajectory.

## 2026-07-21 - Scenario 1 Ideal Baseline Without A-MPDU

- Re-ran `ns3::IdealWifiManager` with the current 80-second constant-speed
  Scenario 1 configuration and `BE_MaxAmpduSize=0`: seed 774015, Run 1, 1 m
  initial distance, 0.5 m/s, 60 Mbps offered load, and 0.5-second sampling.
- The STA ended at 41 m. The curve has 158 samples from 1.5 m through 40.75 m,
  mean 18.112006 Mbps, minimum 5.520960 Mbps, maximum 30.581100 Mbps, and no
  zero-throughput samples. The whole 79.5-second traffic interval measurement
  printed by ns-3 was 18.033 Mbps.
- The new CSV is byte-identical to the earlier no-A-MPDU data stored under the
  less explicit `ideal-paper20-baseline` name. The explicit artifacts are
  `my-project-results/reproduction-scenario1-ideal-noampdu-seed774015.csv` and
  `my-project-results/reproduction-scenario1-ideal-noampdu-seed774015.svg`.

## 2026-07-21 - 400-Episode Local MCS4-to-MCS3 Switch Training

Configuration:

- Isolated the Ideal manager's MCS4-to-MCS3 transition. Its debug trace changes
  from HtMcs4 to HtMcs3 at 17.1514 m in the original moving scenario.
- Set `startDistance=14.75 m`, `movingSpeed=0.5 m/s`, traffic start to 0.5 s,
  and simulation time to 10.5 s. The active traffic and policy decisions
  therefore cover exactly 15-20 m.
- Disabled A-MPDU and retained 20 completed packets per action window. Used the
  complete MCS0-MCS7 action space, four normalized state inputs, epsilon 0.3,
  Adam learning rate `1e-4`, and nominal `gamma=0.99`.
- Used `update_batch=1`: every completed 20-packet window performs one update,
  the raw return is not normalized, and gamma has no cross-window effect.
- Trained all 400 requested episodes with no convergence test or early stop.
  Frozen greedy evaluation and plotting ran after episodes 100, 200, 300, and
  400.

Recorded result:

- The history contains exactly 400 episodes and 87,252 gradient updates. Every
  episode satisfies `updates == completed_windows`.
- Frozen mean throughput at episodes 100, 200, 300, and 400 was identically
  17.470487 Mbps. Each frozen trajectory contained 738 decision windows and
  selected MCS4 for all 738 windows.
- Mean frozen MCS4 probability at those milestones was 0.8792, 0.8976, 0.9024,
  and 0.9166. These are observations only; no convergence pass/fail rule was
  applied.
- The independent entry point is
  `scratch/reproduction/offline_local_switch_train.py`. Training history is
  `my-project-results/reproduction-scenario1-offline-local-switch-training.csv`.
  Milestone policies use the suffixes `episode0100.pt` through
  `episode0400.pt`; the final policy is `offline-local-switch-final.pt`.
- Four individual frozen curves use
  `offline-local-switch-validation-episode{0100,0200,0300,0400}.svg`. Their
  combined plot is
  `my-project-results/reproduction-scenario1-offline-local-switch-validation-every-100-episodes.svg`.

## 2026-07-21 - Final 2000-Episode Frozen-Trajectory Design

Objective and scope:

- Treat one complete 15-20 m traversal as one rollout batch. The policy network
  is initialized only once before episode 1 and retained for all 2000 episodes.
- Keep policy parameters frozen while collecting every 20-completed-packet
  state/action/reward sample in a trajectory. Earlier high-SNR MCS4 rewards can
  therefore not change the action distribution used later in the same pass.
- At trajectory end, accumulate every state's loss and perform exactly one Adam
  optimizer step. The next episode uses the resulting shared network parameters;
  no state discretization, per-state model, or parameter reset is introduced.

Environment and reward corrections:

- Keep `startDistance=14.75 m`, traffic start 0.5 s, speed 0.5 m/s, simulation
  time 10.5 s, 60 Mbps offered load, 1420-byte payloads, and A-MPDU disabled.
  The active trajectory is exactly 15-20 m.
- Replace DATA-only PHY-airtime throughput with wall-clock simulation goodput:
  successful payload bits divided by the elapsed simulation time from action
  selection through the completed window. This includes retransmission,
  contention, inter-frame spacing, and ACK costs in the denominator.
- Set the 20 MHz references to 17.5 Mbps for MCS3 and 23.0 Mbps for MCS4. The
  latter is slightly above the observed 15-17 m saturation mean of 22.749214
  Mbps; all other reference values remain unchanged.
- Compute one window reward as
  `completed_packets * (mcs / 7) * (wall_goodput / reference[mcs])^3`.

Policy-gradient organization:

- Use the existing shared `4 -> 16 -> 16 -> 8` Xavier-initialized policy and
  the full MCS0-MCS7 action space. Adam learning rate remains `1e-4` and
  epsilon remains 0.3.
- Set `gamma=0`. Physical distance/SNR evolution is externally fixed, and the
  current wall-clock window reward directly measures the selected rate's local
  quality. Future rewards at larger distances are not attributed to the current
  action.
- Sample from the actual behavior distribution
  `q(a|s) = 0.7 * pi(a|s) + 0.3 / 8` and use `log q(a|s)` in the loss.
- Do not normalize returns and do not average the trajectory loss. With
  `G_t=r_t`, accumulate `sum_t[-log q(a_t|s_t) * r_t]`, then call
  `optimizer.step()` once at episode end.

Run and reporting contract:

- Train exactly 2000 episodes with no convergence threshold or early stopping.
- Save frozen greedy policies and throughput curves at episodes 400, 800, 1200,
  1600, and 2000, plus a combined five-curve SVG.
- Use the independent prefix
  `reproduction-scenario1-offline-local-switch-trajectory-gamma0` so the prior
  400-episode per-window-update artifacts remain unchanged.
- History must report exactly one optimizer update per episode, the trajectory
  sample count, summed loss, gradient norm, action counts, and milestone frozen
  validation results.

## 2026-07-21 - Final 2000-Episode Frozen-Trajectory Result

Completion and integrity:

- Resumed the interrupted run from the episode-1340 checkpoint and completed
  episodes 1341-2000. The process exited successfully after saving the final
  policy and regenerating the five-curve milestone SVG.
- The history contains exactly 2000 unique, consecutive episodes. Every episode
  has exactly one optimizer update, and `gradient_updates_total == episode` for
  all rows. The final checkpoint records episode 2000 and 2000 updates.
- The episode-2000 milestone policy and final policy contain identical tensors.
  All five milestone policies, validation CSVs, decision traces, and individual
  SVGs are present; no trajectory-gamma0 output file is empty.

Frozen greedy observations (no convergence pass/fail rule applied):

| Episode | Greedy action for entire validation | Mean throughput (Mbps) |
| ---: | ---: | ---: |
| 400 | MCS6 | 0.000000 |
| 800 | MCS2 | 14.049342 |
| 1200 | MCS3 | 17.414284 |
| 1600 | MCS3 | 17.414284 |
| 2000 | MCS3 | 17.414284 |

Final artifacts:

- History: `my-project-results/reproduction-scenario1-offline-local-switch-trajectory-gamma0-training.csv`.
- Final policy: `my-project-results/reproduction-scenario1-offline-local-switch-trajectory-gamma0-final.pt`.
- Final checkpoint: `my-project-results/reproduction-scenario1-offline-local-switch-trajectory-gamma0-checkpoint.pt`.
- Combined frozen curves: `my-project-results/reproduction-scenario1-offline-local-switch-trajectory-gamma0-validation-every-400-episodes.svg`.

## 2026-07-21 - 三层 256 网络仍塌缩到全程 MCS3 的原因与第一阶段修正

实验状态：

- 按论文文字将策略网络改为 `4 -> 256 -> 256 -> 256 -> 8`，完成了新的
  2000 回合冻结轨迹训练。每 500 回合的冻结验证都在 15-20 m 全程选择
  MCS3，平均吞吐量均为 17.414284 Mbps。增大网络没有学出 17.1514 m
  附近的 MCS4-to-MCS3 切换，因此问题不是网络表达能力不足。
- 第 100 回合训练动作中 MCS4 为 104 次、MCS3 为 66 次；第 200 回合
  已反转为 MCS3 153 次、MCS4 30 次。网络先形成全局 MCS4 偏置，随后
  被后半程样本推向全局 MCS3，并在 softmax 饱和后锁死。
- 四个里程碑训练轨迹的探索样本表明奖励方向本身正确：15-17.1514 m
  的 MCS4/MCS3 平均奖励为 11.445/8.687，17.1514-20 m 则为
  5.261/8.664。但后半段窗口更多；忽略状态后，MCS3/MCS4 的全局样本
  平均奖励约为 8.674/8.113，因此无法及时学出状态分区时，固定 MCS3
  确实是当前采样目标下更容易得到的解。

主要问题：

1. `gamma=0` 只使用当前窗口奖励，但动作一直保持到完成 20 个包。不同
   MCS 会改变窗口持续时间、下一个决策距离和整段轨迹的窗口数量，因此
   当前梯度不是整段 15-20 m 总吞吐量的严格策略梯度。单独把 gamma 改大
   也不能消除这个动作持续时间问题。
2. 旧损失使用 `log q(a|s)`，其中
   `q=0.7*pi+0.3/8`。最终 `pi(MCS4)` 约为 `7e-5` 时，MCS4 梯度的
   有效系数 `0.7*pi/q` 仅约 0.0013。epsilon 仍能探索 MCS4，但这些
   高奖励探索样本几乎不能再把 MCS4 概率拉起，形成不可恢复的策略锁死。
3. 旧 SNR 输入 `log1p(snr)/12` 在本地轨迹只覆盖 0.295-0.366，切换点
   约为 0.333；相对上一 MCS 和上一吞吐量，这个变化太小。最终网络在
   实际验证状态上三层分别只有 2/256、4/256、2/256 个 ReLU 单元改变
   激活状态，输出在整个区间近似常量，而且 MCS4 概率的 SNR 方向错误。
4. 状态中的上一 MCS 和上一吞吐量是策略自身产生的。选择 MCS3 后会持续
   产生“上一动作=MCS3、吞吐量约 17.4”的状态，容易形成继续选择 MCS3
   的自循环；失败动作还可能携带最近一次成功 ACK 的陈旧 SNR。
5. 当前使用未归一化的正 raw reward 和轨迹求和损失，没有 state-dependent
   baseline。它不构成梯度符号错误，但会产生很大的方差，让全轨迹共享的
   输出 bias 先于微弱的 SNR 条件关系被学到并快速饱和。
6. 决策窗口按“完成 20 个包”结束，训练目标按决策样本计权，而验收目标按
   时间/距离吞吐量计权，两者存在半马尔可夫时间尺度不一致。

第一阶段代码修正（先处理问题 2 和 3）：

- 保持动作行为分布不变：30% 均匀随机探索，70% 从策略分布采样；但策略
  损失改为开源 ReinRate 使用的 `log pi(a|s)`，不再使用 `log q(a|s)`。
  这不是对行为分布 `q` 的无偏策略梯度，而是有意采用源码兼容的探索学习
  规则，使 epsilon 得到的低概率高奖励动作仍有足够梯度，避免 `log q`
  在概率接近零时完全锁死。
- 为 15-20 m 本地切换实验增加独立状态编码器，不影响其他 offline 入口。
  ns-3 回调提供的是线性 SNR 功率比，策略输入直接转换为
  `10*log10(snr)` dB；当前轨迹约为 15.3-19.0 dB。这里不再根据本次
  轨迹端点进行人工居中、缩放或裁剪，也不引入距离和切换点先验。
- 新实验前缀为
  `reproduction-scenario1-offline-local-switch-trajectory-gamma0-hidden256x3-logpi-snrdb`，
  禁止加载此前 `log q` 或旧 SNR 编码产生的 checkpoint。
- 这两项只解决探索梯度锁死和 SNR 表示问题，不能宣称已经解决全部收敛
  问题。下一步应先做短程诊断，确认高 SNR 端的 MCS4 概率能够随训练上升；
  若仍形成全局动作偏置，再处理 state-dependent baseline、熵约束以及固定
  时间决策/半马尔可夫回报。

## 2026-07-21 - log pi 与 SNR dB 的 200 回合诊断

配置与完整性：

- 使用 `4 -> 256 -> 256 -> 256 -> 8` 策略网络、直接 SNR dB 输入、
  epsilon 0.3 行为采样和 `log pi(a|s)` 策略损失，从零训练 200 回合；
  第 100、200 回合分别保存冻结策略并验证。训练历史包含连续 200 行，
  每回合恰好一次梯度更新。
- 为排除上一 MCS 和上一吞吐量的自循环干扰，除检查实际冻结轨迹外，还将
  CW、上一 MCS 和上一吞吐量固定，只把 SNR 分别设为 19.0、17.26 和
  15.26 dB 后比较策略概率。

三项诊断结果：

1. 高 SNR 的 MCS4 概率已经高于低 SNR，方向正确。第 100 回合受控状态下，
   19.0/17.26/15.26 dB 的 `P(MCS4)` 分别为 0.5354/0.5086/0.4761；
   第 200 回合分别为 0.6641/0.6218/0.5695。高低端差距从约 0.059
   扩大到约 0.095，说明网络开始利用 SNR，而不是学习错误方向。
2. softmax 没有在学出 SNR 方向前超过 0.99。训练决策中的最大概率在
   第 1/100/200 回合最高分别为 0.4320/0.5384/0.6794；两个冻结验证的
   最大概率也只有约 0.54 和 0.67。`log q` 实验中的早期不可恢复饱和没有
   再现。
3. 第 100-200 回合没有从全局 MCS4 翻转为全局 MCS3。两个冻结验证均在
   738 个动作中全部贪心选择 MCS4，平均吞吐量均为 17.470487 Mbps。
   训练动作会因 epsilon 和策略概率波动，但第 200 回合仍有 MCS4 104 次、
   MCS3 43 次，没有出现旧实验第 200 回合的 MCS3 主导塌缩。

结论与剩余问题：

- 问题 2 和 3 的修正有效：探索得到的低概率动作不再因 `log q` 被消去
  梯度，直接 dB 状态也让策略学出了正确的 SNR 概率斜率。
- 200 回合时仍未学出贪心切换点，因为低 SNR 端虽然降低了 MCS4 概率，
  但 `P(MCS4)=0.5695` 仍高于 `P(MCS3)=0.2652`。下一步不应继续修改
  网络宽度或 SNR 编码；应优先加入按状态/局部信道条件计算的 advantage
  baseline，使低 SNR MCS4 获得相对 MCS3 的负优势，而不是仅靠正 raw
  reward 缓慢调整两个动作的全局概率。

## 2026-07-22 00:23:49 CST (+0800) - 无 baseline 的校准 SNR、importance correction 与分箱均衡更新

实验目的与隔离：

- 针对高、低 SNR 状态共用相同 ReLU 路径、整轨迹梯度被全局动作 bias
  覆盖的问题，临时测试“校准标准化 + `pi/q` importance correction +
  SNR 分箱等权”更新。策略网络保持 `4 -> 256 -> 256 -> 256 -> 8`，
  `gamma=0`、epsilon 0.3、Adam 学习率 `1e-4`，不减 reward baseline，
  不做 return normalization。
- 独立实验前缀为
  `reproduction-scenario1-offline-local-switch-trajectory-gamma0-hidden256x3-is-snrz-binbalanced-500`，
  从第 1 回合开始训练 500 回合，每 100 回合执行一次冻结贪心验证。

SNR 校准与状态编码：

- 校准数据只使用既有冻结轨迹中的 SNR observation，不使用 reward、距离、
  正确 MCS 或 17.1514 m 切换标签。先把 ns-3 的线性 SNR 转为
  `snr_db=10*log10(snr_linear)`。
- 冻结校准统计为 `mean=17.49275385298103 dB`、
  `std=0.873142781397473 dB`，策略输入使用
  `snr_z=(snr_db-mean)/std`。无有效 ACK SNR 的初始 observation 使用 0
  作为标准化特征占位值。
- 校准四分位点为 16.78860975、17.497094、18.229104 dB，对应标准化
  边界 -0.80644783、0.00497072、0.84333303。它们只用于把每条训练
  轨迹划分成四个 SNR 区间，不代表目标切换点。

无 baseline 的单回合更新逻辑：

1. 行为动作仍从 `q(a|s)=0.7*pi(a|s)+0.3/8` 采样，其中 30% 为均匀
   epsilon 探索，70% 从 softmax 策略采样。
2. 每个窗口保存选择动作时的标准化 state、action 和当前 raw reward。
   `gamma=0`，所以每个样本的 return 就是当前窗口 reward。
3. 重新计算 `pi(a_t|s_t)` 和 `q(a_t|s_t)`，构造停止梯度的 importance
   weight：`rho_t=detach(pi(a_t|s_t)/q(a_t|s_t))`。
4. 每个样本的策略损失为
   `L_t=-rho_t*log(pi(a_t|s_t)+1e-8)*reward_t`。该修正满足
   `E_q[rho*r*grad(log pi)]=E_pi[r*grad(log pi)]`，恢复标准 on-policy
   REINFORCE 的期望梯度，但没有引入 reward baseline。
5. 按 `snr_z` 的三个校准四分位边界将样本分为四组；先对每组内部的
   `L_t` 求平均，再对所有非空组等权平均。这样窗口较多或 reward 总量
   较大的 SNR 区间不会仅凭样本数量覆盖其他区间。
6. 整条轨迹只对最终分箱均衡损失执行一次 `zero_grad/backward/step`。
   历史额外记录每回合 importance weight 均值和四个 SNR bin 的样本数。

500 回合观察：

- 历史包含连续 500 回合，每回合恰好一次更新；importance weight 的全程
  均值为 0.996194，接近理论期望 1。episode 500 策略与 final policy
  张量完全相同。
- 第 100 回合已学出前段 MCS4、后段 MCS3，但边界附近有少量来回切换；
  第 200 回合起只剩一个稳定切换点。实际切换距离在 episode
  100/200/300/400/500 约为 18.52/18.13/17.99/17.95/17.91 m，持续
  向目标 17.1514 m 移动但仍明显偏晚。
- 冻结验证平均吞吐量在 episode 100/200/300/400/500 分别为
  20.693142/20.635737/20.562800/20.553232/20.530511 Mbps。episode 500
  的 898 个动作中，MCS4 为 576 次、MCS3 为 322 次。
- 受控状态下，episode 500 的 19.00 dB `P(MCS4)=0.99984`，15.26 dB
  `P(MCS3)=0.99979`，已经形成明确分区；17.26 dB 仍有
  `P(MCS4)=0.98813`。MCS3/MCS4 概率交点从 episode 100 的约
  16.268 dB 移至 episode 500 的约 16.705 dB，尚未达到目标切换处约
  17.26 dB。
- 在 15.38-19.01 dB 的受控扫描中，episode 500 三层分别有
  83/66/90 个 ReLU 单元改变激活；直接输入原始 dB 的旧实验仅约 0-2 个，
  说明标准化和分箱均衡实质上缓解了高低 SNR 共用梯度路径的问题。

代码恢复状态：

- 该更新逻辑仅作为临时实验实现。实验结束后已恢复本地代码到实验前的
  `log pi + 原始 SNR dB` 版本，实验产物保留但 active 入口不包含上述
  calibration、importance correction 或 bin-balanced loss。
- 恢复后 SHA-256：
  `offline_local_switch_train.py=84ff2fb8777316eba93798cceafa212db6d931d08e9cdea826b784a6d9f6457f`；
  `offline_reinforce_model.py=7301bb82fdc353ff9efae1e01ef1ba2f4746275d1e78ca2935969660bb78081d`；
  `offline_reinforce_train.py=ec384186d43ed55503886cc68d2fcea6ea95e25b39b6adf3c42fa8d6ca73cdca`。
- 合并曲线：
  `my-project-results/reproduction-scenario1-offline-local-switch-trajectory-gamma0-hidden256x3-is-snrz-binbalanced-500-validation-every-100-episodes.svg`。

## 20260722改进方案1

记录时间：`2026-07-22 11:31:40 CST (+0800)`。

### 目标

- 将 2026-07-22 00:07 完成并验证有效的“校准 SNR 标准化 + `pi/q`
  importance correction + SNR 分箱等权”逻辑整理成独立、可恢复、可直接
  执行的正式 2000 回合实验。
- 目标策略是在 15-20 m 冻结验证中，前段贪心选择 MCS4、后段贪心选择
  MCS3，并使唯一切换点继续从已观察的约 17.91 m 向 Ideal 的
  17.1514 m 靠近。
- 本方案不引入 reward baseline、return normalization、距离输入、正确
  MCS 标签、动作 mask 或强制切换规则。

### 代码隔离要求

- 不再临时修改并恢复 `offline_local_switch_train.py`。新建独立入口
  `scratch/reproduction/offline_local_switch_improvement1_train.py`，从
  `offline_reinforce_train.py` 复用 `RESULTS`、`ROOT`、`create_interface`、
  `policy_state` 和 `run_episode`，从 `offline_reinforce_model.py` 复用
  `ReinRateAgent` 与三层 256 策略网络。
- 在新入口内定义实验专用 `Improvement1Agent(ReinRateAgent)`，只覆盖
  `finish_episode()`；不要改变通用模型、通用训练入口、C++ 环境或已有
  实验文件。
- 开始前断言策略 Linear 结构恰好为
  `4 -> 256 -> 256 -> 256 -> 8`。如不一致，停止而不是自动修改网络。
- 方案记录时 active 文件 SHA-256 为：
  `offline_local_switch_train.py=84ff2fb8777316eba93798cceafa212db6d931d08e9cdea826b784a6d9f6457f`；
  `offline_reinforce_model.py=7301bb82fdc353ff9efae1e01ef1ba2f4746275d1e78ca2935969660bb78081d`；
  `offline_reinforce_train.py=ec384186d43ed55503886cc68d2fcea6ea95e25b39b6adf3c42fa8d6ca73cdca`。
  执行前先报告哈希差异；不得覆盖用户后续修改。

### 固定环境和训练参数

- 轨迹：`startDistance=14.75 m`、`movingSpeed=0.5 m/s`、业务从 0.5 s
  开始、仿真到 10.5 s，业务有效距离为 15-20 m。
- 业务与 Wi-Fi：60 Mbps UDP、1420-byte payload、20 MHz、单空间流、
  A-MPDU 关闭、每个动作持续 20 个完成包、完整 MCS0-MCS7 动作空间。
- reward 保持 C++ 当前实现：
  `completed_packets*(mcs/7)*(wall_goodput/reference[mcs])^3`；MCS3/MCS4
  reference 保持 17.5/23.0 Mbps。
- Python：Adam `lr=1e-4`、`gamma=0`、`epsilon=0.3`、agent seed 774015、
  train seed 从 774015 逐回合加一、validation seed 884015。
- 网络每个回合内冻结，收集完整轨迹后只执行一次 optimizer step；训练
  2000 回合，无收敛提前停止。

### 校准 SNR 状态编码

- ns-3 observation 的 SNR 是线性功率比，先计算
  `snr_db=10*log10(snr_linear)`。
- 使用已经由无 reward 标签冻结轨迹得到并验证过的固定统计：
  `SNR_DB_MEAN=17.49275385298103`，
  `SNR_DB_STD=0.873142781397473`。
- 策略第一个状态输入为
  `snr_z=(snr_db-SNR_DB_MEAN)/SNR_DB_STD`；首次尚无有效 ACK SNR 时
  使用 `snr_z=0`。其余三个输入继续使用现有 `policy_state()` 产生的 CW、
  上一 MCS 和上一吞吐量归一化值。
- 必须在 decision trace 同时记录 `snr_linear`、标准化后的 `state_snr`；
  分析时可由线性 SNR 还原 dB，禁止把 `state_snr` 误标为 dB。

### SNR 分箱

- 使用固定校准四分位点 16.78860975、17.497094、18.229104 dB，对应
  标准化边界 -0.8064478319、0.0049707185、0.8433330295。
- 用 `torch.bucketize(states[:,0], edges)` 把每回合样本分为四个区间。
  分箱只用于梯度聚合，不作为策略输入，不代表正确切换点。
- 每回合必须记录四个 `snr_bin_counts`。允许某个 bin 为空；最终 loss 只
  对非空 bin 求平均，但正常完整轨迹应检查四个 bin 是否都被覆盖。

### 无 baseline 的更新公式

对完整轨迹按以下顺序实现，不能把 importance weight 留在 autograd 图中：

```text
states  = stack(saved_states)
actions = tensor(saved_actions)
returns = tensor(raw_rewards)              # gamma=0，G_t=r_t

pi_all = policy(states)
pi_a   = gather(pi_all, actions)
q_a    = (1-epsilon)*pi_a + epsilon/8
rho    = detach(pi_a/q_a)

sample_loss_t = -rho_t*log(pi_a_t+1e-8)*returns_t
bin_loss_b    = mean(sample_loss_t in bin b)
loss          = mean(all non-empty bin_loss_b)

optimizer.zero_grad()
loss.backward()
optimizer.step()
```

- 不减 reward 均值、不除 reward 标准差、不增加 critic/value network。
- 不使用旧的 `log q*reward`，也不使用未经修正的 `log pi*reward`。
- 每回合记录 `loss`、`gradient_norm`、`sample_count`、action counts、
  `importance_weight_mean/min/max`、四个 bin count 和四个 bin loss。
- checkpoint 除通用字段外必须保存 improvement 标识、SNR mean/std、分箱
  边界和 importance-correction 开关；`--resume` 加载时逐项验证，任何不一致
  都应报错退出，禁止误续其他实验 checkpoint。

### 输出和执行命令

- 独立前缀：
  `reproduction-scenario1-offline-local-switch-trajectory-gamma0-hidden256x3-improvement1`。
- 在 episode 500、1000、1500、2000 保存策略、冻结 validation CSV、
  decision trace 和单独 SVG；最终生成包含四条曲线的
  `...-validation-every-500-episodes.svg`。
- 正式命令：

```bash
ns3ai_env/bin/python -B \
  scratch/reproduction/offline_local_switch_improvement1_train.py \
  --maxEpisodes 2000 --validateEvery 500
```

- 标准输出重定向到同前缀 `-run.log`。每回合写 checkpoint 和 flush history，
  允许中断后使用相同配置 `--resume`，但最终必须检查 history episode 唯一、
  连续且 `gradient_updates_total==episode`。

### 里程碑诊断和验收

- 每个里程碑除实际冻结轨迹外，固定 CW=0.4、上一 MCS=3、上一吞吐量为
  `17.414284/29.6`，分别输入 19.00、17.26、15.26 dB 经同一 mean/std
  标准化后的状态，记录完整八动作概率。
- 扫描 15.0-19.2 dB，求 `P(MCS3)=P(MCS4)` 的受控概率交点；统计实际
  validation 的连续动作段和唯一切换距离。禁止只看整条曲线均值判断成功。
- 最低有效性条件：19.00 dB 贪心 MCS4、15.26 dB 贪心 MCS3；冻结轨迹
  不得退化为全程单一 MCS；importance weight 均值应接近 1；四个 SNR bin
  应持续获得样本。
- 目标条件：episode 500-2000 始终保持 MCS4-to-MCS3 单次切换，受控交点
  和实际切换距离总体向 17.26 dB/17.1514 m 靠近。若交点停止移动或反向，
  如实报告，不通过修改 reward、校准统计或分箱边界追赶目标。
- 完整性条件：2000 行历史、每回合一次更新、四个里程碑齐全、episode 2000
  policy 与 final policy 张量一致、所有输出非空。

### 已知基准

- 同逻辑的 500 回合临时实验已经证明可分区：episode 100 开始出现前 MCS4、
  后 MCS3；episode 200 后为单一稳定切换；episode 500 实际切换约
  17.91 m，受控概率交点约 16.705 dB。
- episode 500 的 19.00 dB `P(MCS4)=0.99984`，15.26 dB
  `P(MCS3)=0.99979`，冻结吞吐量 20.530511 Mbps。正式方案的意义是验证
  继续训练能否校准切换位置，而不是再次证明网络能够分区。

## 20260722 - pi/q 与熵约束简明执行记录

- 行为采样分布为 `q(a|s)=0.7*pi(a|s)+0.3/8`：70% 按策略采样，30% 在
  八个 MCS 中均匀探索。目标策略仍是 `pi`，冻结验证关闭 epsilon 并使用
  `argmax(pi)`。
- 因为样本来自 `q` 而目标是 `pi`，每个样本使用
  `rho=detach(pi(a|s)/q(a|s))`，策略项为
  `-rho*log(pi(a|s))*reward`。这使
  `E_q[rho*reward*grad(log pi)] = E_pi[reward*grad(log pi)]`，不把
  epsilon 随机动作错误地当成策略主动选择；`rho` 必须 detach，不能参与
  反向传播。
- 熵约束只作用于策略分布，不是 reward baseline：
  `H(pi)=-sum(pi*log(pi))`，训练损失加入 `-beta*H(pi)`。它防止某个
  SNR 区间在局部排序尚未学完前把某个动作概率压到近零；epsilon 负责实际
  探索，importance weight 负责采样校正，entropy 负责维持 `pi` 的可恢复性。
  冻结验证仍然只取 `argmax(pi)`。
- 若按 SNR 分箱均衡梯度，actor loss 和 entropy 都应先在每个非空 SNR bin
  内求平均，再对 bin 等权平均，避免某区间的样本数量同时主导 reward 梯度
  和熵保护。

## 20260722 - 全场景 Raw-dB RBF + pi/q + Entropy 300 回合执行

执行范围：

- 直接修改 `scratch/reproduction/offline_reinforce_*` 通用 offline 系列，
  使用完整 Scenario 1：80 s、1-41 m、0.5 m/s、60 Mbps、20 包决策窗口、
  A-MPDU 关闭、MCS0-MCS7，训练 300 回合。
- SNR 只做物理转换 `snr_db=10*log10(snr_linear)`，不做减均值、除标准差
  或负值标准化。以全场景校准得到的原始 dB 范围生成 RBF 特征；RBF 特征
  由原始 dB 计算并保持非负，不能把它误称为 SNR z-score。
- 使用 RBF 局部状态特征减少不同距离之间的 reward 外溢；保留
  `rho=detach(pi/q)`；加入 `-beta*entropy(pi)`；每 50 回合冻结监控并在
  最终只生成一张包含所有冻结曲线的汇总 SVG。训练 history 必须记录
  entropy、importance weight、SNR bin 计数、action counts、gradient norm
  和冻结概率诊断。
- 本轮执行前应先做无 reward 标签的全场景 SNR 校准，冻结 centers、sigma、
  RBF 范围和分箱边界，并把这些值保存进 checkpoint；不能使用距离、Ideal
  MCS 或人工切换点生成 RBF 中心。

### 300 回合执行结果

- 新增独立入口 `scratch/reproduction/offline_full_rbf_train.py`，未覆盖旧
  offline checkpoint。使用 95 个 RBF 中心：原始 SNR dB 范围 5-52 dB、
  间隔 0.5 dB、`sigma=0.6 dB`；状态总维度为 100，包括 95 维 RBF、
  SNR-valid、CW、上一 MCS、上一吞吐量和窗口成功率。
- 使用八个由既有完整场景 observation 无标签校准得到的原始 dB 分位区间；
  每个区间先平均 `-detach(pi/q)*log(pi)*reward - 0.01*entropy`，再对非空
  区间等权平均。300 回合均为完整 80 s 轨迹，每回合恰好一次更新。
- 冻结监控结果：episode 50 为全程 MCS7，吞吐量 7.284780 Mbps；episode
  100 开始出现 MCS7/MCS3 分区，11.227995 Mbps；episode 150 增加 MCS2
  远端分区，15.467577 Mbps；episode 200/250/300 分别为
  15.504961/15.507260/15.503668 Mbps，随后基本停滞。
- episode 300 的冻结动作形成三个稳定连续区间：约 1.25-10.72 m 为 MCS7、
  10.73-22.77 m 为 MCS3、22.78-41 m 为 MCS2。RBF 局部特征确实阻止了
  单一动作覆盖全场景，但尚未形成 Ideal 所需的更细多 MCS 阶梯。
- 冻结零吞吐比例从 episode 50 的 73.42% 降至 episode 150/200/300 的
  约 18.35%，最长连续零吞吐采样从 116 降至 29；说明分区显著改善链路，
  但 MCS2 在最远端仍产生持续 outage。
- importance weight 均值在各里程碑约为 0.99-1.02，八个 SNR bin 每回合
  均有样本，证明 `pi/q` 和分箱逻辑工作正常。主要失败点是 entropy 从
  episode 50 的 1.9457 快速降至 episode 100/150/200/300 的
  0.8444/0.3502/0.1788/0.1082；最低 importance weight 最终约
  `6.7e-6`，`beta=0.01` 没有阻止局部动作概率过早接近零。
- 完整性：history 为连续 300 行，`gradient_updates_total==episode`，最终
  checkpoint 为 episode 300，episode 300 policy 与 final policy 张量
  完全一致。最终只生成一张六曲线汇总 SVG：
  `my-project-results/reproduction-scenario1-offline-full-rbf-piq-entropy-300-validation-every-50-episodes.svg`。
- 结论：Raw-dB RBF 对“reward 外溢/全局动作塌缩”有效，`pi/q` 修正也保持
  正常；本配置未达到完整场景收敛。下一轮应优先提高或自适应调整 entropy
  约束，并检查 far-range stale SNR/动作持续时间，而不是继续扩大网络。

## 2026-07-22 14:13 - 0.5 dB 局部 Advantage 奖励修正版（续训至 600 轮）

### 修改和算法

- 修正 `scratch/reproduction/rl-env.cc` 的 MCS 奖励权重：从 `mcs/7`
  改为严格的 `(mcs+1)/8`。MCS0 成功窗口现在具有正奖励；80 s 预检中
  240 个 MCS0 窗口全部为正，示例约 2.69-2.75。
- 完整场景仍为 80 s、1-41 m、0.5 m/s、A-MPDU 关闭、每个动作保持到
  20 个完成包；网络保持 `100 -> 256 -> 256 -> 256 -> 8`，参数量
  159,496。状态包含 95 个 Raw-dB RBF 特征和 5 个尾部状态。
- RBF 中心为 5-52 dB、间隔 0.5 dB、`sigma=0.6 dB`。上一轮虽然有
  95 个 RBF 输入，却把梯度聚合压缩为 8 个宽分位区；本轮取消该压缩，
  直接用最大响应 RBF 中心作为 advantage 区号，即 95 个 0.5 dB 有效区，
  并为无效 SNR 单列第 96 区。历史轨迹每轮实际覆盖约 89-92 个有效区，
  各区样本量足以计算局部统计。
- 每个 80 s episode 内冻结策略、收集完整轨迹，结束后恰好执行一次
  `optimizer.step()`。仍使用 `gamma=0`，所以 `return_t=reward_t`。
- 每个非空 0.5 dB 区独立计算：
  `adv=(reward-mean_bin)/(std_bin+1e-8)`；零方差或单样本区的 advantage
  置零。每区 actor loss 与 entropy 先求均值，再对非空区等权平均。
- 行为策略为 `q=(1-epsilon)*pi+epsilon/8`，`epsilon=0.3`；策略项为
  `-detach(pi/q)*log(pi)*adv`。entropy 系数提高为 0.05，Adam 学习率
  `1e-4`。这是一项改进实验，不是论文 19.52 原算法的严格复现。
- 新增严格 `--resume`：校验 RBF、分区、reward、gamma、epsilon、entropy
  和 history 连续性，并从第 301 轮加载 policy 与 Adam 状态续训。第 301
  轮后 checkpoint 还保存 Python/Torch RNG 状态。

### 里程碑结果

```text
episode  throughput(Mbps)  entropy   冻结策略动作
50        7.284780         1.854127  MCS7
100       7.920365         0.725322  MCS7/MCS2/MCS1（边界尚不稳定）
150      16.380981         0.474664  MCS7/MCS2/MCS1
200      17.186241         0.359002  MCS7/MCS3/MCS2/MCS1
250      18.064269         0.280056  MCS7/MCS4/MCS3/MCS2/MCS1
300      18.291754         0.206425  MCS7/MCS5/MCS4/MCS3/MCS2/MCS1
350      18.449360         0.114717  同六档，MCS5 区继续扩大
400      18.466324         0.078855  同六档，开始平台化
450      18.469636         0.066358  同六档
500      18.470931         0.061690  同六档
550      18.478118         0.044126  同六档
600      18.484014         0.045809  同六档
```

- 零吞吐比例从 episode 50 的 73.42% 降到 episode 100 的 5.06%，从
  episode 150 到 600 均为 0。
- episode 600 的主要连续距离分段为：
  `1.25-10.32 m MCS7`、`10.32-12.49 m MCS5`、
  `12.50-17.79 m MCS4`、`17.80-22.45 m MCS3`、
  `22.48-30.34 m MCS2`、`30.35-41.00 m MCS1`。
- `pi/q` 正常：各里程碑 importance mean 约 0.98-1.02。600 行 history
  连续且每行 `updates=1`、`gradient_updates_total=episode`；checkpoint、
  episode-600 policy 和 final policy 张量完全一致。

### 结论

- 95 个 0.5 dB 局部 advantage 区有效解决了旧 8 宽区的优化粒度问题：
  策略由上一轮最终三个动作提高为六个稳定、连续、按 SNR 降档的动作区，
  吞吐量由约 15.50 Mbps 提高到 18.48 Mbps，且消除了远端 outage。
- 300 轮确实不够完全确认收敛，因为 250-300 轮仍新增 MCS5；但 400-600
  轮吞吐量只增加约 0.018 Mbps、边界和动作计数基本不变，继续增加轮数
  已不是产生 MCS6/MCS0 的有效手段。
- 缺少 MCS0/MCS6 并非当前证据下的学习失败。episode 600 训练样本中，
  30-41 m 的 MCS1/MCS0 平均 reward 分别约 4.23/2.73；1-12.5 m 的
  MCS7/MCS6 平均 reward 分别约 22.15/18.07。当前 reward 本身分别偏好
  MCS1 和 MCS7，因此贪心策略合理地跳过 MCS0 与 MCS6。
- 输出前缀最初包含 `-300`，续训时为保持 checkpoint/config 一致没有改名；
  其中 training CSV、checkpoint、final policy 和最终 SVG 的实际终点均为
  episode 600。最终 SVG 含 episode 50-600 的 12 条冻结曲线：
  `my-project-results/reproduction-scenario1-offline-full-rbf05dbadv-piq-entropy05-rewardshift-300-validation-every-50-episodes.svg`。

## 2026-07-22 - RTX 5060 CUDA 12.8 训练环境

- 原环境为 `torch 2.12.1+cu130`，RTX 5060 驱动 572.90 只支持 CUDA
  Driver API 12.8，因此 `torch.cuda.is_available()` 为 false，训练实际使用 CPU。
- 改装为 `torch 2.7.1+cu128`，CUDA 12.8 依赖从南京大学国内镜像安装。
  实测识别 `NVIDIA GeForce RTX 5060 Laptop GPU`、compute capability 12.0，
  CUDA 矩阵乘法、反向传播和 Adam 更新均通过。
- offline agent 新增 `device=auto|cpu|cuda`；`auto` 默认在 CUDA 可用时使用
  GPU，`--device cuda` 会在 CUDA 不可用时直接报错，禁止静默回退 CPU。
- 为避免约 1700 次单样本 CUDA kernel 调用，逐窗口动作推理由 CPU 策略副本
  完成；完整 80 s 轨迹结束后一次性把批量状态迁到 GPU，进行 advantage、
  loss、反向传播和 Adam 更新，再同步一次 CPU 推理副本。96 个分箱统计已用
  `bincount/scatter_add` 向量化，CPU/GPU loss 差约 `7.5e-9`。
- 完整 80 s 对照：CPU 约 4.90 s；GPU 首轮因 CUDA 初始化约 8.49 s，第二轮
  稳态约 4.77 s。GPU 已生效，但加速有限，因为主要耗时在 ns-3 仿真与共享
  内存交互，网络只有 159,496 个参数且每个 episode 只更新一次。

## 2026-07-22 - 0.85 指数 MCS 奖励的 300 轮实验

- 保持 default ns-3 构建和现有算法不变，仅将奖励中 MCS 权重从
  `(mcs+1)/8` 改为 `0.85**(7-mcs)`。Python 状态里的 `(mcs+1)/8`
  仍是状态特征，没有误改。新实验从随机初始模型开始，使用 CUDA。
- 冻结验证策略演化：episode 50 为全局 MCS7；100 为 MCS7/MCS1；
  150 仍为 MCS7/MCS1；200 为 MCS7/MCS2/MCS1；250 首次稳定出现
  MCS0，为 MCS7/MCS2/MCS1/MCS0；300 保持相同四段。
- episode 300 的距离分段约为：`1-10.67 m MCS7`、`10.68-29.54 m MCS2`、
  `29.55-37.19 m MCS1`、`37.20-41 m MCS0`。窗口计数分别为
  MCS7=2487、MCS2=2331、MCS1=681、MCS0=187，说明新奖励确实解决了
  贪心策略远端不出现 MCS0 的问题。
- 验证吞吐量从 episode 200 的 16.289 Mbps 到 250 的 16.348 Mbps，
  episode 300 为 16.313 Mbps。250-300 分段结构稳定，loss 已降至
  -0.00256、梯度范数 0.0549、熵 0.1502，因此不续训到 500；预期继续
  训练主要是小幅调整边界，而不是新增关键分区。
- 最终合并 SVG：
  `my-project-results/reproduction-scenario1-offline-full-rbf05dbadv-piq-entropy05-reward085pow-300-validation-every-50-episodes.svg`。

## 2026-07-22 - 1 m 固定 MCS 参考速率重新校准

- 检查确认旧表 `[5.4,9.8,13.6,17.5,23.0,26.1,28.1,29.6]`
  不是全部本地实测：主体来自 REINRATE，仅 MCS3/MCS4 曾被调整。
  在上一次 300 轮数据里，八个 MCS 都出现过
  `(window_goodput/reference)^3 > 1`，最大为 MCS7 的约 1.485。
- 新增 `calibrate_mcs_references.py`，用 default ns-3、seed 990001、固定
  1 m、速度 0、A-MPDU 关闭、20 包窗口，对 MCS0-7 分别强制运行
  40 s。完整窗口数从 MCS0 的 964 到 MCS7 的 5260。
- 八个 MCS 的实测最大窗口 goodput 为
  `[5.659,10.523,14.733,18.470,24.533,29.350,31.639,33.505] Mbps`。
  为公平起见，八档都采用相同规则：“40 s 完整窗口最大值向上取
  0.1 Mbps”，新表为 `[5.7,10.6,14.8,18.5,24.6,29.4,31.7,33.6]`。
- 用新表回算所有校准窗口，八档最大 ratio 为 0.993-0.998，最大
  ratio 三次方为 0.978-0.995，全部不超过 1。default 可执行文件已
  按新表重编。
- 之前分析中的 `10.67-20 m` 是为了概括最终连续 MCS2 策略段而
  做的事后距离汇总，不是训练 advantage 分箱。训练仍然使用 95 个
  0.5 dB SNR 局部分箱；该粗汇总不能用来判断分箱是否足够细。

## 2026-07-22 - `(mcs+2)/9` 奖励的 1000 轮收敛结果
- 网络256*256

- 在新校准参考表 `[5.7,10.6,14.8,18.5,24.6,29.4,31.7,33.6]`
  上，将 MCS 奖励系数改为 `(mcs+2)/9`。其余保持 95 个 0.5 dB
  局部 advantage 分箱、pi/q、熵 0.05、每条 80 s 轨迹只更新一次。
- 先训练到 500 轮；因 400 轮新增 MCS0、500 轮新增 MCS6，判定未
  收敛并续训至 800，随后按用户要求续训到 1000。每 100 轮做一次
  冻结验证，最终 SVG 包含 10 条曲线。
- 验证吞吐量：100/200/300/400/500 轮分别为
  `7.921/16.814/17.282/17.367/17.562 Mbps`；600 轮新增 MCS4 后跃升至
  `18.420 Mbps`；700-1000 轮稳定在 `18.438/18.458/18.448/18.453 Mbps`。
- episode 1000 主要距离分段为：`1.25-10.07 m MCS7`、
  `10.07-11.24 m MCS6`、`11.25-17.66 m MCS4`、`17.67-22.49 m MCS3`、
  `22.49-30.07 m MCS2`、`30.08-37.81 m MCS1`、`37.82-41 m MCS0`。
  边界处有两个单窗口 MCS2/MCS3 交错，不影响主分段。
- 600-1000 轮动作集合和主要边界稳定，吞吐波动小于 0.04 Mbps；
  episode 1000 梯度范数为 0.01986，熵为 0.04343，判定已收敛。
  最终贪心策略包含 7 个 MCS，唯一未选中的是 MCS5。
- 最终 SVG：
  `my-project-results/reproduction-scenario1-offline-full-rbf05dbadv-piq-entropy05-rewardmcs2over9-500-validation-every-100-episodes.svg`。
  文件前缀保留初始 `-500`名称，但 history、checkpoint、final policy 和 SVG
  的实际终点均为 episode 1000。
