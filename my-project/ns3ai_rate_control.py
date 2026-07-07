#!/usr/bin/env python3

import argparse
import csv
import os
import signal
import subprocess
import sys
import traceback

import my_project_rate_control_py as py_binding

from dqn_agent import DqnAgent
from mab_agent import MabAgent


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
EXECUTABLE = os.path.join(
    ROOT, "build", "scratch", "my-project", "ns3.36.1-my-project-rate-control-default"
)


def make_agent(name, num_actions):
    if name == "dqn":
        return DqnAgent(num_actions=num_actions)
    if name == "mab":
        return MabAgent(num_actions=num_actions)
    raise ValueError(f"Python/ns3ai mode supports dqn or mab, got {name}")


def run_ns3(args):
    cmd = [
        EXECUTABLE,
        f"--algorithm={args.algorithm}",
        f"--simulationTime={args.simulation_time}",
        f"--fastMobility={'true' if args.fast_mobility else 'false'}",
        f"--Seed={args.seed}",
        f"--Run={args.run}",
        f"--decisionInterval={args.decision_interval}",
    ]
    if args.convergence_start is not None:
        cmd.append(f"--convergenceStart={args.convergence_start}")
    if args.max_distance is not None:
        cmd.append(f"--maxDistance={args.max_distance}")
    if args.min_distance is not None:
        cmd.append(f"--minDistance={args.min_distance}")
    if args.mobility_period is not None:
        cmd.append(f"--mobilityPeriod={args.mobility_period}")

    env = os.environ.copy()
    env["LD_LIBRARY_PATH"] = os.path.join(ROOT, "build", "lib") + ":" + env.get("LD_LIBRARY_PATH", "")
    return subprocess.Popen(
        cmd,
        cwd=ROOT,
        env=env,
        text=True,
        stdout=None if args.show_ns3_output else subprocess.PIPE,
        stderr=None if args.show_ns3_output else subprocess.PIPE,
        preexec_fn=os.setpgrp,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--algorithm", choices=["dqn", "mab"], default="dqn")
    parser.add_argument("--simulationTime", dest="simulation_time", type=float, default=50.0)
    parser.add_argument("--fastMobility", dest="fast_mobility", action="store_true")
    parser.add_argument("--no-fastMobility", dest="fast_mobility", action="store_false")
    parser.set_defaults(fast_mobility=True)
    parser.add_argument("--Seed", dest="seed", type=int, default=1)
    parser.add_argument("--Run", dest="run", type=int, default=31)
    parser.add_argument("--convergenceStart", dest="convergence_start", type=float, default=None)
    parser.add_argument("--minDistance", dest="min_distance", type=float, default=None)
    parser.add_argument("--maxDistance", dest="max_distance", type=float, default=None)
    parser.add_argument("--mobilityPeriod", dest="mobility_period", type=float, default=None)
    parser.add_argument("--decisionInterval", dest="decision_interval", type=float, default=0.001)
    parser.add_argument("--showNs3Output", dest="show_ns3_output", action="store_true")
    args = parser.parse_args()

    os.makedirs(os.path.join(ROOT, "my-project-results"), exist_ok=True)
    log_path = os.path.join(ROOT, "my-project-results", f"python-agent-{args.algorithm}.csv")

    msg_interface = py_binding.Ns3AiMsgInterfaceImpl(
        True,
        False,
        True,
        4096,
        "My Seg",
        "My Cpp to Python Msg",
        "My Python to Cpp Msg",
        "My Lockable",
    )
    proc = run_ns3(args)
    agent = make_agent(args.algorithm, 8)

    try:
        with open(log_path, "w", newline="") as log_file:
            writer = csv.writer(log_file)
            writer.writerow(
                [
                    "step",
                    "algorithm",
                    "snr_db",
                    "ack_ratio",
                    "reward",
                    "last_action",
                    "next_action",
                    "num_actions",
                    "epsilon",
                ]
            )
            while True:
                msg_interface.PyRecvBegin()
                msg_interface.PySendBegin()
                if msg_interface.PyGetFinished():
                    break

                env = msg_interface.GetCpp2PyStruct()
                if args.algorithm == "mab":
                    agent.observe(env)
                action, epsilon = agent.select_action(env)
                action = max(1, min(int(action), max(1, int(env.numActions))))

                act = msg_interface.GetPy2CppStruct()
                act.nextAction = action
                act.nextMcs = action - 1
                act.epsilon = float(epsilon)
                writer.writerow(
                    [
                        env.step,
                        args.algorithm,
                        env.snrDb,
                        env.ackRatio,
                        env.reward,
                        env.lastAction,
                        action,
                        env.numActions,
                        epsilon,
                    ]
                )
                msg_interface.PyRecvEnd()
                msg_interface.PySendEnd()

        stdout, stderr = proc.communicate(timeout=5)
        if stdout:
            print(stdout, end="")
        if stderr:
            print(stderr, end="", file=sys.stderr)
        return proc.returncode or 0
    except Exception:
        traceback.print_exc()
        return 1
    finally:
        if proc.poll() is None:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        del msg_interface


if __name__ == "__main__":
    raise SystemExit(main())
