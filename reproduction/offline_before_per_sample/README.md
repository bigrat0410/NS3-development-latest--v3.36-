# Offline code backup before per-sample updates

This directory preserves the offline implementation immediately before the
per-sample Actor-Critic change. Its policy update groups each 10-sample batch
by MCS, averages reward and log probability per action, and uses cubic
throughput reward.

The files are an archival snapshot. Run the active implementation from the
parent `scratch/reproduction` directory for current experiments.
