#!/usr/bin/env python3
import functools

import offline_reinforce_train as T
from offline_full_rbf_train import (
    ENTROPY_COEF, RBF_CENTERS_DB, RbfImportanceEntropyAgent, rbf_policy_state,
)


MODEL = T.RESULTS / (
    "reproduction-scenario1-offline-window20-episodic-reinforce-rbf-auto-"
    "ampdu-on-per-ampdu-final.pt"
)


def main():
    agent = RbfImportanceEntropyAgent(
        len(RBF_CENTERS_DB) + 5, 1e-4, 0.0, 0.0, ENTROPY_COEF, device="cpu"
    )
    agent.load_policy(MODEL)
    encoder = functools.partial(
        rbf_policy_state, max_reference_mbps=T.max_reference_mbps(65535)
    )
    result = T.run_episode(
        agent, 1999001, 80.0, 1.0, 0.5,
        "ampdu-final-failure-fix-verification", False,
        log_decisions=True, state_encoder=encoder,
        be_max_ampdu_size=65535, decision_per_ampdu=True,
        collect_segments=True, print_every=10 ** 9,
    )
    last_time = result["segments"][-1]["simulation_time_s"]
    print(f"segments={len(result['segments'])}")
    print(f"last_decision_time_s={last_time:.6f}")
    print(f"throughput_mbps={result['throughput']:.6f}")
    if last_time < 75.0:
        raise RuntimeError("A-MPDU decisions still stop before the simulation tail")


if __name__ == "__main__":
    main()
