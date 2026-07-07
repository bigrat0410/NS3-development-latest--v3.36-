# MATLAB RA Project Migration for ns-3

This folder is a self-contained ns-3 scratch migration of:

`C:\Users\bigrat\Desktop\毕业设计\matlab\论文最终实验备份\AAA_场景1_main_project`

The migration follows the same broad development pattern as `scratch/reinrate`: all research code lives inside the scratch subdirectory, while ns-3 provides the build system, RNGs, and runtime.

## Files

- `rate-adaptation-main.cc`: ns-3 scratch entry point and algorithm dispatcher.
- `wifi-mcs-manager.*`: migrated MCS/action table and ns-3-derived SNR/PER curves.
- `wifi-environment.*`: migrated packet-level Wi-Fi RA environment.
- `ideal-throughput-manager.*`: migrated Oracle throughput baseline.
- `minstrel-agent.*`: migrated Minstrel-style baseline.
- `mab-agent.*`: migrated sliding-window epsilon-greedy MAB.
- `dqn-agent.*`: self-contained DQN-style linear Q agent preserving the MATLAB DQN control flow.

## Run

From the ns-3 root:

```bash
./ns3 run "scratch/my-project/rate-adaptation-main --algorithm=0 --totalPackets=1000"
./ns3 run "scratch/my-project/rate-adaptation-main --algorithm=1 --totalPackets=1000"
./ns3 run "scratch/my-project/rate-adaptation-main --algorithm=2 --totalPackets=1000"
./ns3 run "scratch/my-project/rate-adaptation-main --algorithm=3 --totalPackets=1000"
```

`algorithm` values:

- `0`: Oracle
- `1`: Minstrel
- `2`: DQN-style RA
- `3`: MAB

Outputs are written to `my-project-results/*.csv`.
