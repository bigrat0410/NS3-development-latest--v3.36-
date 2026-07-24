##

场景2训练命令：

  scratch/reproduction/latest-version/project/run_simulation/
  run_offline_auto.sh \
    --scenario 2 \
    --beMaxAmpduSize=65535

  启动后仍可选择 default 或 optimized。场景1继续使用：

  scratch/reproduction/latest-version/project/run_simulation/
  run_offline_auto.sh \
    --scenario 1 \
    --beMaxAmpduSize=65535





# 最新可复现版本

这是离线Window-20 REINFORCE工作流的精选可运行快照，将训练流程、ns-3集成与SVG生成模块相互解耦。

## 目录结构

- `project/`：训练控制器、策略实现、固定MCS校准、评估辅助工具与启动脚本。
- `project/run_simulation/`：交互式训练入口。
- `project/ns3_scenario/`：C++编写的ns-3仿真场景、强化学习环境、CMake快照，以及编译完成的Python消息绑定。
- `project/references/`：已校准的固定MCS CSV参考表，分别对应A-MPDU关闭与开启两种模式。
- `plotting/`：将CSV结果文件转换为SVG图表的脚本目录。

## 本快照外的必要依赖文件

本快照复用仓库根目录下已编译的ns-3可执行文件，以及`ns3ai_env`虚拟环境。场景源码与当前平台专属的Python消息绑定存放在`project/ns3_scenario/`路径下；若C++源码发生改动，请重新编译ns-3常规目标文件。

## A-MPDU 模式说明

设置`BE_MaxAmpduSize=0`即可关闭A-MPDU功能。在ns-3.36版本中，HT PPDU场景下BE模式的A-MPDU默认开启值为65535字节。

训练控制器支持传入`--beMaxAmpduSize`参数；输出文件名会自动附带`ampdu-off`（关闭）或`ampdu-on`（开启）标识，避免不同模式的结果混淆。奖励参考值与RBF吞吐量归一化逻辑均会匹配当前所选模式。

MCS校准工具默认以开启状态的65535字节为基准；如需复现关闭状态的基准数据，请传入`--beMaxAmpduSize=0`参数。

执行示例（在仓库根目录下运行）：

```bash
ns3ai_env/bin/python -B scratch/reproduction/latest-version/project/calibrate_mcs_references.py
ns3ai_env/bin/python -B scratch/reproduction/latest-version/project/calibrate_mcs_references.py --beMaxAmpduSize=0
scratch/reproduction/latest-version/project/run_simulation/run_offline_auto.sh --beMaxAmpduSize=65535
```

交互式启动时会先询问使用`default`还是`optimized`构建。也可以通过参数直接指定，适合非交互运行：

```bash
scratch/reproduction/latest-version/project/run_simulation/run_offline_auto.sh --ns3Profile optimized --beMaxAmpduSize=65535
```

也仍可设置环境变量`REPRODUCTION_NS3_PROFILE=default|optimized`；命令行参数优先。

## 场景2：一维 Random Walk

训练控制器现在支持`--scenario 2`。场景2保持场景1的Wi-Fi、信道、业务、
A-MPDU、奖励和模型参数不变，仅替换移动模型和episode时长：

- AP固定在0 m，STA初始距离1 m；
- STA使用`ns3::RandomWalk2dMobilityModel`，严格沿x轴在0–24 m内移动；
- 速度恒定3.0 m/s；
- `Mode=Time`，每2 s独立随机选择`+x`或`-x`，因此可能保持原方向；
- 到0 m或24 m边界时按RandomWalk2d规则反弹；
- 每个episode为40 s。

从仓库根目录启动场景2训练：

```bash
scratch/reproduction/latest-version/project/run_simulation/run_offline_auto.sh \
  --scenario 2 --beMaxAmpduSize=65535
```

场景1仍是默认值，行为与已有实验一致。场景2的模型和训练历史使用
`reproduction-scenario2-...`前缀，不会覆盖场景1文件。可选参数
`--randomWalkMinDistance`、`--randomWalkMaxDistance`和
`--randomWalkDirectionInterval`用于受控实验；上述命令使用论文场景默认值。

详细的逐 A-MPDU 分段 CSV 默认关闭。如需输出，启动训练时增加
`--writeSegmentCsv`（也可使用`--outputCsv`）。
