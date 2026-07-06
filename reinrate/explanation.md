# REINRATE 802.11 速率自适应项目架构解析

## 1. 系统宏观架构

该项目把 802.11 速率自适应拆成两个协同部分：

- **ns-3 仿真环境（C++）**：`rate-control.cc` 构建 802.11n Wi-Fi 网络、节点移动、干扰 AP、UDP 业务流、PacketSink/FlowMonitor 统计；`rl-env.cc/.h` 实现 `ns3::rl-rateWifiManager`，接入 ns-3 WiFi `RemoteStationManager`，在 MAC 层发数据帧前决定本次使用的 MCS。
- **Python AI 大脑**：`ns3ai.py` 通过 `py_interface.Ns3AIRL` 与 C++ 侧共享内存交互，读取 C++ 写入的状态 `env`，调用 `model.py` 中的 REINFORCE/DQN 智能体选择动作 `next_mcs`，再写回给 C++。

一次闭环可以概括为：

```text
ns-3 产生包/信道事件
  -> rl-rateWifiManager 收集 MAC/PHY 观测量
  -> C++ 通过 ns3-ai 写入 Observation
  -> Python 读取 Observation，执行策略推理/训练
  -> Python 写回 Action(next_mcs)
  -> C++ 把 next_mcs 映射到 HtMcsX 并更新 WifiTxVector
  -> 后续发包结果继续影响吞吐、SNR、CW 和收包统计
```

## 2. 核心模块解析

### `rate-control.cc`

仿真入口与网络场景构建文件。主要职责包括：

- 创建 AP、STA、干扰 AP 节点，并配置 802.11n/5GHz、信道宽度、发射功率、误码模型和传播损耗模型。
- 设置 `staManager`、`apManager`，默认值为 `ns3::rl-rateWifiManager`，这会把速率选择交给 `rl-env.cc` 中的 DRL 控制器。
- 配置移动模型，制造链路质量变化；配置干扰 AP，制造竞争和丢包压力。
- 通过 OnOff 应用产生 UDP 流量，通过 PacketSink/FlowMonitor 统计吞吐、收包数、时延等性能指标。

### `rl-env.h`

C++ 侧强化学习环境和 WiFi 速率控制器的声明文件。最关键的是两个结构体：

```cpp
struct AiConstantRateEnv {
  uint8_t mcs;
  uint16_t cw;
  double throughput;
  double snr;
};

struct AiConstantRateAct {
  uint8_t nss;
  uint8_t next_mcs;
};
```

它们分别定义了 C++ 与 Python 之间交换的状态和动作。`AiConstantRateWifiManager` 类还声明了 `DoGetDataTxVector`、`TrackRxOk`、`TraceTxOk`、`TrackCw`、`TrackMonitorSniffRx`、`MetreRead` 等函数，用来接入 MAC/PHY trace、统计状态并应用动作。

### `rl-env.cc`

项目中最核心的 C++ 速率自适应实现。它注册 `ns3::rl-rateWifiManager`，让仿真脚本可通过字符串创建该 WiFi manager。

主要职责：

- 在 `DoInitialize` 或相关初始化路径中建立 `Ns3AIRL<AiConstantRateEnv, AiConstantRateAct>` 通信对象。
- 在 `DoGetDataTxVector` 中接入 ns-3 MAC 层发包流程：当 MAC 要发送数据帧时，选择 `WifiTxVector` 的 DataMode，也就是具体 MCS。
- 将 SNR、吞吐、CW、当前 MCS 等写入 `AiConstantRateEnv`，等待 Python 端读取。
- 读取 Python 写回的 `AiConstantRateAct.next_mcs`，映射为 `HtMcs0` 到 `HtMcs7` 之一。
- 通过 Trace 回调记录发包、收包、PHY 监听信号强度、竞争窗口变化等底层状态。

### `ns3ai.py`

Python 端主控脚本，负责启动 ns-3 实验、连接 ns3-ai 共享内存、执行训练或推理。

核心职责：

- 用 `ctypes.Structure` 定义 `ReinrateEnv` 和 `ReinrateAct`，字段必须与 C++ 的 `AiConstantRateEnv/AiConstantRateAct` 一一对应。
- 创建 `Ns3AIRL(uid, ReinrateEnv, ReinrateAct)`，其中 `uid` 必须与 C++ 端共享块 ID 保持一致。
- 在主循环中执行 `with c.rl as data:`，读取 `data.env` 并写回 `data.act`。
- 默认调用 `train_reinforce(data.env, data.act)`，根据状态计算 reward、更新策略、输出下一 MCS。
- 如果模型文件存在且不 retrain，则进入推理模式，只选择动作不更新参数。

### `model.py`

深度强化学习模型定义文件。

- `DQNAgent`：备用 DQN 路径，包含经验回放、epsilon-greedy 动作选择和 Bellman 更新。
- `PolicyNetwork`：REINFORCE 的策略网络，输入状态，输出 8 个动作概率。
- `ReinforceAgent`：当前默认主路径，保存 `log_prob` 和 reward，通过 REINFORCE 策略梯度更新策略。

## 3. 强化学习建模解析

该项目的 MDP 可以用如下方式理解。

### 状态空间 Observation

C++ 侧定义：

```text
s_t = [mcs_t, cw_t, throughput_t, snr_t]
```

含义：

- `mcs_t`：当前或上一时刻使用的 802.11n MCS 索引。
- `cw_t`：MAC 层竞争窗口，反映信道竞争和退避强度。
- `throughput_t`：当前统计窗口内吞吐。
- `snr_t`：PHY 侧观测到的信噪比。

但当前 Python 主路径 `train_reinforce` 实际主要使用：

```text
x_t = [log(snr_t + 1)]
```

也就是说，结构体中保留了更完整的状态字段，但策略网络默认输入维度为 1，主要按 SNR 做速率选择。

### 动作空间 Action

动作由 Python 写回：

```text
a_t = next_mcs_t, next_mcs_t ∈ {0,1,2,3,4,5,6,7}
```

对应 802.11n 单空间流的 `HtMcs0` 到 `HtMcs7`。动作越大，理论物理速率越高，但所需 SNR 也越高；在信道质量不足时，高 MCS 会导致更多错误帧和重传，从而降低实际吞吐。

`nss` 字段也在结构体中出现，表示空间流数，但当前代码主要围绕 `next_mcs` 做决策。

### 奖励函数 Reward

代码里存在多种 reward 设计痕迹，主思路是：

```text
reward = 实际吞吐 / 当前 MCS 的理论最大吞吐
reward = (mcs / 7) * reward^3
```

大白话解释：

- 如果某个 MCS 理论上很快，但实际吞吐也能跟上，就给较高奖励。
- 如果盲目选择高 MCS 导致丢包、重传、吞吐下降，实际吞吐/理论吞吐会变低，奖励降低。
- 乘上 `mcs/7` 会鼓励在可靠时使用更高阶 MCS，而不是保守地一直停留在低 MCS。

REINFORCE 更新目标可以写成：

```text
最大化 E[ G_t * log pi(a_t | s_t) ]
G_t = r_t + gamma r_{t+1} + gamma^2 r_{t+2} + ...
```

代码实现中，`ReinforceAgent.update()` 先从 reward 序列反向计算折扣回报 `G_t`，再用 `-log_prob * G_t` 作为 loss 做梯度下降。

## 4. 数据流转时序

下面描述一次完整的 “发包 -> 收集状态 -> AI 推理 -> 更新速率” 生命周期。

1. **应用层产生数据包**：`rate-control.cc` 中的 OnOff 应用向 STA 发送 UDP 流量，数据进入 WiFi MAC 队列。
2. **MAC 层准备发包**：ns-3 WiFi MAC 需要为数据帧选择发送参数，于是调用 `rl-rateWifiManager::DoGetDataTxVector`。
3. **C++ 汇总 Observation**：`rl-env.cc` 根据最近一个统计窗口的 MCS、CW、吞吐、SNR 等信息填充 `AiConstantRateEnv`。
4. **ns3-ai 同步状态**：C++ 通过 `Ns3AIRL<AiConstantRateEnv, AiConstantRateAct>` 将 `env` 暴露给 Python。
5. **Python 读取状态**：`ns3ai.py` 的 `with c.rl as data:` 取出 `data.env`，得到当前链路状态。
6. **Python 计算奖励**：训练模式下，Python 根据当前反馈评价上一动作，例如用实际吞吐与理论吞吐的比例构造 reward。
7. **策略网络选择动作**：`ReinforceAgent.choose_action()` 根据 `log(snr+1)` 输出 MCS 动作概率，并采样或取最大概率动作。
8. **Python 写回 Action**：`data.act.next_mcs = next_mcs`，退出上下文后 ns3-ai 将动作同步回 C++。
9. **C++ 应用 MCS**：`rl-env.cc` 读取 `next_mcs`，映射到 `HtMcsX`，写入 `WifiTxVector` 的 DataMode。
10. **PHY/MAC 反馈结果**：发包成功、收包成功、监听 SNR、CW 变化等 trace 回调继续更新统计量，作为下一轮 Observation。

## 5. 关键实现注意点

- `ReinrateEnv/ReinrateAct` 与 `AiConstantRateEnv/AiConstantRateAct` 的字段顺序、类型、对齐方式必须保持一致，否则共享内存解析会错位。
- 当前 README 提到状态包含 RSS/SNR、CW、MCS、Throughput，但 `ReinforceAgent(1, 8)` 表明默认策略输入只使用 1 维特征，代码中主要是 `log(snr+1)`。
- `calculate_reward()` 的函数签名要求 `achieved_throughput, mcs_index, channel_bandwidth`，但部分调用处参数数量/含义存在不一致痕迹，后续若要复现实验应重点核查。
- `next_mcs` 的动作范围是 0-7，适合 802.11n 单空间流；如果扩展到多空间流、40MHz 或 802.11ac，需要同步扩展动作空间和速率表。
- C++ 侧 Reward 不显式存在；C++ 的职责是提供真实网络反馈，Python 才是 MDP 奖励和策略更新的实现位置。
