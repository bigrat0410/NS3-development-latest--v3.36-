# Scenario 1 Round 2

This directory is a copy of `scratch/reinrate/annotated_src`. The original
complex topology remains in `rate-control.cc` as `LegacyMain`; the executable
entry point builds the earliest recorded Scenario 1 baseline:

- one AP and one STA with a 60 Mbps UDP downlink;
- 802.11n, 5 GHz channel 36, 20 MHz, one spatial stream;
- STA movement from 1 m to 41 m over 80 seconds at 0.5 m/s;
- LogDistance exponent 3.0 and 66.6777 dB reference loss at 1 m;
- 1420-byte payloads, traffic from 0.5-80 s, and 0.5 s sampling;
- AP manager selected between Ideal and Minstrel-HT;
- STA fixed at HtMcs0 for DATA and control responses.

Build and reproduce the 20-seed comparison from the ns-3 root:

```bash
./ns3 build round2-scenario1
ns3ai_env/bin/python -B scratch/reproduction_round2/run_baselines.py
```

Outputs are written under `my-project-results/round2-scenario1-*`.
