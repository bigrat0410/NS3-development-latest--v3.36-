#!/usr/bin/env python3

import argparse
import os
import signal
import subprocess
import sys
import traceback

import reinrate_py
from ns3ai import ReinrateAct, ReinrateContainer, ReinrateEnv


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
EXECUTABLE = os.path.join(
    ROOT, "build", "scratch", "reinrate", "ns3.36.1-reinrate-scenario1-default"
)


def run_ns3(args):
    cmd = [
        EXECUTABLE,
        "--apManager=ns3::rl-rateWifiManager",
        "--label=reinrate",
        f"--simulationTime={args.simulation_time}",
        f"--startDistance={args.start_distance}",
        f"--speed={args.speed}",
        f"--sampleInterval={args.sample_interval}",
        f"--Seed={args.seed}",
        f"--Run={args.run}",
        f"--lossModel={args.loss_model}",
        f"--matrixLoss={args.matrix_loss}",
        f"--referenceLoss={args.reference_loss}",
        f"--pathLossExponent={args.path_loss_exponent}",
        f"--txPower={args.tx_power}",
        f"--ccaSensitivity={args.cca_sensitivity}",
        f"--rxSensitivity={args.rx_sensitivity}",
        f"--rxNoiseFigure={args.rx_noise_figure}",
    ]
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
    parser.add_argument("--simulationTime", dest="simulation_time", type=float, default=100.0)
    parser.add_argument("--startDistance", dest="start_distance", type=float, default=0.0)
    parser.add_argument("--speed", type=float, default=0.5)
    parser.add_argument("--sampleInterval", dest="sample_interval", type=float, default=1.0)
    parser.add_argument("--Seed", dest="seed", type=int, default=1)
    parser.add_argument("--Run", dest="run", type=int, default=201)
    parser.add_argument("--lossModel", dest="loss_model", choices=["matrix", "log-distance"], default="log-distance")
    parser.add_argument("--matrixLoss", dest="matrix_loss", type=float, default=30.0)
    parser.add_argument("--referenceLoss", dest="reference_loss", type=float, default=30.0)
    parser.add_argument("--pathLossExponent", dest="path_loss_exponent", type=float, default=3.0)
    parser.add_argument("--txPower", dest="tx_power", type=float, default=20.0)
    parser.add_argument("--ccaSensitivity", dest="cca_sensitivity", type=float, default=-110.0)
    parser.add_argument("--rxSensitivity", dest="rx_sensitivity", type=float, default=-110.0)
    parser.add_argument("--rxNoiseFigure", dest="rx_noise_figure", type=float, default=0.0)
    parser.add_argument("--model", default="model.pt")
    parser.add_argument("--retrain", action="store_true")
    parser.add_argument("--showNs3Output", dest="show_ns3_output", action="store_true")
    args = parser.parse_args()

    msg_interface = reinrate_py.Ns3AiMsgInterfaceImpl(
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
    agent = ReinrateContainer(
        model_name=os.path.join(os.path.dirname(__file__), args.model), retrain=args.retrain
    )
    try:
        while True:
            msg_interface.PyRecvBegin()
            if msg_interface.PyGetFinished():
                msg_interface.PyRecvEnd()
                break
            env = msg_interface.GetCpp2PyStruct()
            msg_interface.PyRecvEnd()

            msg_interface.PySendBegin()
            act = msg_interface.GetPy2CppStruct()
            act.nss = 1
            agent.train_reinforce(env, act)
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
