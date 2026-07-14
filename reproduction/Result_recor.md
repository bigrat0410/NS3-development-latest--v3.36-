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

