# 20260714复现场景1Minstrel+Ideal

## 实验目标

复现论文场景1，对比 ns-3 原生 `IdealWifiManager` 与 `MinstrelHtWifiManager` 在 STA 持续远离 AP 时的吞吐量。两种算法使用相同的20个随机种子，并对相同距离的数据点取平均。

## 拓扑与无线参数

- ns-3版本：3.36.1
- 拓扑：1个 AP、1个 STA，AP向STA发送下行业务
- MAC：`ApWifiMac` + `StaWifiMac`
- Wi-Fi标准：802.11n，5 GHz
- 信道：36，带宽20 MHz
- AP速率控制：分别使用 `ns3::IdealWifiManager`、`ns3::MinstrelHtWifiManager`
- STA速率控制：`ConstantRateWifiManager`，`DataMode=HtMcs0`，`ControlMode=HtMcs0`
- 传播延迟：`ConstantSpeedPropagationDelayModel`
- 传播损耗：`LogDistancePropagationLossModel`
- 路径损耗指数：3.0
- 1 m参考损耗：66.6777 dB（命令行覆盖代码默认值）
- PHY误码模型：`NistErrorRateModel`
- 发射功率：20 dBm，`TxPowerStart=TxPowerEnd=20`，功率等级数为1
- 接收机噪声系数：`RxNoiseFigure=0 dB`
- 前导码检测：关闭，使用 `DisablePreambleDetectionModel()`
- CCA门限、RxSensitivity、短GI、RTS/CTS门限：未显式修改，使用ns-3默认配置
- A-MPDU：未显式修改，BE使用ns-3默认上限65535 B；A-MSDU默认关闭

## 业务参数

- 传输协议：UDP
- 业务源：`OnOffHelper`，持续CBR发送
- 提供负载：60 Mbps
- UDP负载大小：1420 B
- UDP端口：5000
- PacketSink运行时间：0–80 s
- 业务发送时间：0.5–80 s
- 仿真时间：80 s

## 移动与采样时序

- AP初始位置：`(0,0,0)`，保持静止
- STA初始位置：`(1,0,0)`
- STA移动模型：`ConstantVelocityMobilityModel`
- 移动开始时间：0 s
- 移动速度：0.5 m/s，沿X轴正方向远离AP
- 理论终点：`1 + 0.5×80 = 41 m`
- 吞吐采样间隔：0.5 s
- 第一次采样：`trafficStartTime + sampleInterval = 1.0 s`
- 第一个样本累计0.5–1.0 s业务数据，距离为1.5 m
- 后续每0.5 s用“当前累计接收字节－上一采样累计字节”计算窗口平均吞吐
- 最后一个采样点：79.5 s，距离40.75 m
- 吞吐单位：`Mbps = ΔRxBytes × 8 / 0.5 / 1,000,000`

## 随机实验

Ideal和Minstrel-HT使用相同的20个随机种子，`Run=1`：

```text
774015, 939968, 515926, 750706, 859569,
189491, 17046, 70198, 941358, 247505,
52249, 863680, 997997, 343976, 454830,
296570, 555664, 585294, 553339, 456740
```

总仿真次数：`20种子 × 2算法 = 40次`，全部成功完成。

## 结果与文件

- Ideal全距离采样均值：29.111500 Mbps
- Minstrel-HT全距离采样均值：28.679764 Mbps
- 每种算法平均曲线包含158个距离点
- 单次结果：`my-project-results/reproduction-scenario1-{ideal|minstrel}-seed<seed>.csv`
- 20次平均数据：`my-project-results/reproduction-scenario1-20seed-average.csv`
- 平均对比图：`my-project-results/reproduction-scenario1-20seed-average.svg`
- 绘图脚本：`scratch/reproduction/result_figure.py`

## ReinRate源码逻辑单次基准（20260715最终代码重跑）

- 参数与上述Minstrel实验完全相同，使用首个共同种子`Seed=774015`、`Run=1`
- 保留源码REINFORCE网络、奖励、30%随机探索、逐反馈更新和`epochs=1000`后的末动作固定逻辑
- 完成19928次共享内存决策、1001次训练调用和1000次梯度更新
- 末次训练动作是MCS0，之后按源码逻辑固定使用MCS0
- 158个吞吐采样点完整覆盖1.0–79.5 s（1.5–40.75 m）
- 全距离采样均值：8.691119 Mbps；最大值：52.074200 Mbps
- 吞吐数据：`my-project-results/reproduction-scenario1-reinrate-seed774015.csv`
- 决策轨迹：`my-project-results/reproduction-scenario1-reinrate-decisions-seed774015.csv`
- 模型：`my-project-results/reproduction-scenario1-reinrate-seed774015.pt`
- 曲线：`my-project-results/reproduction-scenario1-reinrate-seed774015.svg`

Python策略没有额外固定随机种子，因此即使ns-3的Seed/Run相同，不同运行的探索与采样轨迹也会不同；本节记录的是最终代码对应的当前输出，不对源码逻辑作确定性修正。

## ReinRate在线训练逻辑修正测试（20260715）

- 修正奖励归属：当前吞吐反馈直接使用当前实际执行的`env.mcs`计算奖励
- 取消默认1000次回调后固定末次随机动作，训练和动作选择贯穿80 s
- 固定Python/NumPy/PyTorch智能体随机种子为774015，便于复现
- ns-3场景仍使用与Minstrel相同的`Seed=774015`、`Run=1`和全部无线/业务参数
- 完成11495次决策和11494次梯度更新；MCS0非零奖励记录为0
- 158个吞吐采样点均值为16.167721 Mbps，最大值为51.642600 Mbps
- 同种子Ideal均值为29.115966 Mbps，Minstrel-HT均值为29.544335 Mbps
- 在线吞吐CSV：`my-project-results/reproduction-scenario1-reinrate-online-seed774015.csv`
- 在线决策CSV：`my-project-results/reproduction-scenario1-reinrate-online-decisions-seed774015.csv`
- 在线模型：`my-project-results/reproduction-scenario1-reinrate-online-seed774015.pt`
- 在线单曲线：`my-project-results/reproduction-scenario1-reinrate-online-seed774015.svg`
- 同种子三算法对比：`my-project-results/reproduction-scenario1-seed774015-online-comparison.svg`

修正消除了约4 m后因一次随机抽到MCS0而永久锁定的问题，但当前逐包在线训练结果仍未对齐Ideal；远距离尾段吞吐为0，不能据此宣称复现了论文性能。

## PHY发送时间奖励与自适应收敛测试（20260715）

- 奖励吞吐改为成功有效载荷位数除以DATA/AMPDU的PHY发送时间；失败重传时间计入分母
- 决策吞吐最大值从错误的611.557 Mbps降为61.507 Mbps，最大奖励从20308.361降为8.972
- 使用200步滑动窗口判断局部收敛：前后半窗奖励变化不超过20%、全窗成功率至少75%、最近50步成功率至少80%、主动作占比至少45%
- 收敛后关闭epsilon探索并使用策略贪心动作；最近50步成功率低于70%时重新开启探索
- 80 s测试完成11928次决策和11927次更新，发生2次收敛/关闭探索事件，链路恶化后均重新探索
- REINRATE均值16.625719 Mbps、最大59.571800 Mbps；同种子Ideal均值29.115966 Mbps，Minstrel-HT均值29.544335 Mbps
- 结果CSV：`my-project-results/reproduction-scenario1-reinrate-airtime-convergence-seed774015.csv`
- 决策CSV：`my-project-results/reproduction-scenario1-reinrate-airtime-convergence-decisions-seed774015.csv`
- 单曲线SVG：`my-project-results/reproduction-scenario1-reinrate-airtime-convergence-seed774015.svg`
- 对比SVG：`my-project-results/reproduction-scenario1-seed774015-airtime-convergence-comparison.svg`

暂不处理但已确认的问题：失败时没有新的ACK SNR，状态会变成0或保留陈旧值；逐反馈单样本REINFORCE没有批量回报、baseline或优势估计。这两项仍限制远距离适应能力。

## 12 ms固定决策窗口测试（20260716）

- 仅将RL环境的Observation/Action交换从每个DATA发送机会改为从业务开始后的固定12 ms定时器触发；窗口内继续累计成功有效载荷和DATA/AMPDU PHY airtime，失败重传仍计入airtime。
- Python端保持现有逐次REINFORCE更新、奖励函数、30%探索及收敛检测；本测试没有加入K批量回报、baseline、额外状态或新的探索策略。
- 使用`Seed=774015`、`Run=1`、`agentSeed=774015`及与此前在线测试相同的80 s场景参数。
- 共完成6624次决策和6623次梯度更新，约等于79.488 s / 12 ms；说明定时器而非每个发送反馈在驱动决策。
- 外部0.5 s吞吐采样完整为158点，均值12.779713 Mbps，最大59.231000 Mbps，采样点中没有0吞吐量。
- 该均值低于逐发送反馈在线版的16.167721 Mbps，说明单独固定决策周期尚未改善整体性能；但远距离末段仍有0.545280-2.181120 Mbps，未出现此前在线版的整段零吞吐尾部。
- 吞吐CSV：`my-project-results/reproduction-scenario1-reinrate-12ms-seed774015.csv`
- 决策CSV：`my-project-results/reproduction-scenario1-reinrate-12ms-decisions-seed774015.csv`
- 单曲线SVG：`my-project-results/reproduction-scenario1-reinrate-12ms-seed774015.svg`
- 同种子三算法对比SVG：`my-project-results/reproduction-scenario1-seed774015-12ms-comparison.svg`

## Paper20 REINFORCE逻辑修正测试（20260716）

- 将AI动作边界改为固定20个完成的DATA MPDU：首个业务DATA前选择动作，该MCS保持到20个包完成后才交换下一动作。
- 为使20包边界不被单个聚合帧跨越，Paper20、Ideal和Minstrel基线均统一设置`BE_MaxAmpduSize=0`。
- Observation输入改为`[log(SNR+1), CW, MCS, TP]`四维；`max_mcs`仅保留在通信结构中用于诊断，不作为策略输入或动作钳制。
- 每个包的成功有效载荷/PHY airtime产生奖励，窗口回报为20项按`gamma=0.99`折扣的累计值；每个20包窗口只进行一次策略梯度更新。
- 训练动作从`0.7 * policy + 0.3 * uniform`的实际行为分布采样，并用该混合分布的log probability更新，避免把epsilon随机动作误当作纯策略采样。
- 使用`Seed=774015`、`Run=1`、`agentSeed=774015`的80 s移动场景。Paper20完成1989个窗口和1988次梯度更新。
- 同条件基线：Ideal均值18.112006 Mbps、峰值30.581100 Mbps；Minstrel-HT均值17.851019 Mbps、峰值30.467500 Mbps。
- Paper20均值5.034207 Mbps、峰值21.561300 Mbps，158个外部采样点中53个为0；该首次严格窗口版没有优于基线。
- Paper20吞吐CSV：`my-project-results/reproduction-scenario1-reinrate-paper20-seed774015.csv`
- Paper20决策CSV：`my-project-results/reproduction-scenario1-reinrate-paper20-decisions-seed774015.csv`
- 同种子三算法对比SVG：`my-project-results/reproduction-scenario1-seed774015-paper20-comparison.svg`
