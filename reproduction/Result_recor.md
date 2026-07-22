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

## 开源更新粒度与论文超参数的静态诊断（20260721）

本轮只在1 m静态强信道上诊断训练机制。`learningRate=1e-4`、
`gamma=0.99`、`epsilon=0.3`均保持论文值，A-MPDU关闭，每个动作持续20个完成包，
每轮仿真1 s。已知该场景的最优动作是MCS7，因此它可以把训练机制问题与移动场景的
状态泛化问题分开。

### 核心机制

- `updateBatch=20`时，每轮约30至50个窗口，只产生约2至3次参数更新；100轮也只有
  数百次更新。每批20个折扣回报还会做均值/标准差归一化，原始reward约100的尺度被
  压到标准差约1。在`lr=1e-4`下，有效学习量不足，100轮后贪心动作仍停在MCS6。
- `updateBatch=1`时，每个20包窗口都更新。单样本回报恒为`G_t=r_t`，所以
  `gamma=0.99`在这条路径上实际不参与跨窗口折扣；单样本也跳过回报归一化，raw
  reward直接缩放`-log(pi(a_t|s_t))`的梯度。这与开源代码“每次新反馈立即调用
  update，更新后清空buffer”的真实运行路径一致。
- 因此，论文中的`lr=1e-4`和`gamma=0.99`本身没有错，但不能脱离开源实现的reward
  数值尺度和更新频率来复现。把更新改为整轮或20窗口一次，同时做return标准化，虽然
  参数名仍相同，实际优化器已经不是同一个工作点。

### `updateBatch=1`的200轮结果

| 轮次 | 贪心MCS | P(MCS6) | P(MCS7) | 平均raw window reward |
| --- | ---: | ---: | ---: | ---: |
| 1 | 6 | 0.252 | 0.073 | 39.371 |
| 100 | 6 | 0.383 | 0.353 | 77.617 |
| 110 | 7 | 0.345 | 0.428 | 87.524 |
| 200 | 7 | 0.156 | 0.723 | 89.179 |

MCS7在第110轮反超MCS6成为贪心动作，并保持到第200轮；MCS7概率从0.073升到
0.723，MCS0概率从第1轮的0.116降到第200轮的小于0.001。静态测试证明逐窗口、
raw-reward更新可以在论文学习率下学到已知最优动作，但它尚不能证明80 s移动场景中的
状态相关策略已经收敛。

## 80 s匀速场景Ideal无A-MPDU单曲线（20260721）

使用当前Scenario 1参数重新实跑一组`ns3::IdealWifiManager`：

```bash
./build/scratch/reproduction/ns3.36.1-two-node-ht-default \
  --simulationTime=80 --trafficStartTime=0.5 \
  --startDistance=1 --movingSpeed=0.5 \
  --rateManager=ns3::IdealWifiManager \
  --lossModel=log-distance --pathLossExponent=3 --referenceLoss=66.6777 \
  --dataRate=60Mbps --packetSize=1420 --sampleInterval=0.5 \
  --Seed=774015 --Run=1 \
  --outputFile=my-project-results/reproduction-scenario1-ideal-noampdu-seed774015.csv \
  --ns3::WifiMac::BE_MaxAmpduSize=0
```

- 移动轨迹：STA从1 m出发，以0.5 m/s匀速远离，80 s结束时位于41 m。
- 曲线采样：158点，覆盖1.0–79.5 s，对应距离1.5–40.75 m。
- 采样均值：18.112006 Mbps；最小值：5.520960 Mbps；最大值：30.581100 Mbps。
- 0吞吐采样：0；ns-3按完整79.5 s业务区间统计的整体吞吐为18.033 Mbps。
- CSV：`my-project-results/reproduction-scenario1-ideal-noampdu-seed774015.csv`。
- SVG：`my-project-results/reproduction-scenario1-ideal-noampdu-seed774015.svg`。

新CSV与此前`reproduction-scenario1-ideal-paper20-baseline-seed774015.csv`逐字节一致，
说明无A-MPDU基线可重复。原来的`reproduction-scenario1-ideal-seed774015.csv`是开启
A-MPDU的结果（均值29.115966 Mbps），不能与本轮无A-MPDU曲线混用。

## 15–20 m局部切换400轮训练（20260721）

### 场景和算法设定

- Ideal调试日志确认该范围内的基准切换是MCS4到MCS3，发生在约17.1514 m。
- `startDistance=14.75 m`、`movingSpeed=0.5 m/s`、业务从0.5 s开始、仿真在
  10.5 s结束，因此有业务和AI决策的实际距离严格覆盖15–20 m。
- A-MPDU关闭；每个动作保持到20个DATA MPDU完成；动作空间保留MCS0–MCS7。
- 状态为归一化后的SNR、CW、当前MCS和窗口吞吐量，策略网络为`4-16-16-8`。
- Adam学习率`1e-4`、`gamma=0.99`、`epsilon=0.3`、`updateBatch=1`。
- 每个20包窗口立即执行一次更新。单样本return不归一化，`gamma`不产生跨窗口折扣。
- 固定训练400轮，不使用收敛标准和提前停止。第100、200、300、400轮分别关闭探索、
  关闭更新并运行一次冻结贪心验证，然后绘制曲线。

### 完成情况和客观结果

- 400轮全部完成，训练历史正好400行，累计87,252次梯度更新。
- 每轮均满足完成窗口数等于梯度更新数，没有尾部样本被丢弃。
- 第100、200、300、400轮冻结验证平均吞吐均为17.470487 Mbps。
- 每次冻结验证有738个决策窗口，738个窗口全部选择MCS4。
- 四个里程碑的冻结平均`P(MCS4)`依次为0.8792、0.8976、0.9024、0.9166。
- 本节按要求只记录训练和曲线，不应用收敛通过/失败判定。

文件：

- 训练入口：`scratch/reproduction/offline_local_switch_train.py`。
- 历史：`my-project-results/reproduction-scenario1-offline-local-switch-training.csv`。
- 模型：`my-project-results/reproduction-scenario1-offline-local-switch-episode0100.pt`、
  `episode0200.pt`、`episode0300.pt`、`episode0400.pt`及`offline-local-switch-final.pt`。
- 单曲线：`my-project-results/reproduction-scenario1-offline-local-switch-validation-episode0100.svg`、
  `episode0200.svg`、`episode0300.svg`、`episode0400.svg`。
- 叠加图：`my-project-results/reproduction-scenario1-offline-local-switch-validation-every-100-episodes.svg`。

## 15–20 m整轨迹单次更新2000轮结果（20260721）

- 从第1340轮checkpoint续训，完成第1341–2000轮，进程正常退出。
- 训练历史正好2000行，轮次1–2000连续且无重复；每轮均为1次optimizer
  update，累计更新数在每一行都与轮次相等。
- 冻结贪心验证：第400轮全程MCS6/0 Mbps，第800轮全程MCS2/
  14.049342 Mbps，第1200、1600、2000轮均全程MCS3/17.414284 Mbps。
- 按实验约定只记录客观结果，不应用中途或最终收敛通过/失败判定。
- 第2000轮策略与final policy的参数逐张量一致；五个里程碑的模型、冻结
  CSV、decision trace和SVG均完整存在。

主要文件：

- 历史：`my-project-results/reproduction-scenario1-offline-local-switch-trajectory-gamma0-training.csv`。
- 最终模型：`my-project-results/reproduction-scenario1-offline-local-switch-trajectory-gamma0-final.pt`。
- 叠加曲线：`my-project-results/reproduction-scenario1-offline-local-switch-trajectory-gamma0-validation-every-400-episodes.svg`。
