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
