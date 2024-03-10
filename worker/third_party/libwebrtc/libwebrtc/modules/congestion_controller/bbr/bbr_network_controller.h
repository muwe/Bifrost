/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// BBR (Bottleneck Bandwidth and RTT) congestion control algorithm.
// Based on the Quic BBR implementation in Chromium.

#ifndef MODULES_CONGESTION_CONTROLLER_BBR_BBR_NETWORK_CONTROLLER_H_
#define MODULES_CONGESTION_CONTROLLER_BBR_BBR_NETWORK_CONTROLLER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/types/optional.h"
#include "api/transport/network_control.h"
#include "api/transport/network_types.h"
#include "modules/congestion_controller/bbr/bandwidth_sampler.h"
#include "modules/congestion_controller/bbr/loss_rate_filter.h"
#include "modules/congestion_controller/bbr/rtt_stats.h"
#include "modules/congestion_controller/bbr/windowed_filter.h"
#include "rtc_base/experiments/field_trial_parser.h"
#include "rtc_base/experiments/field_trial_units.h"
#include "rtc_base/random.h"

namespace webrtc {
namespace bbr {

typedef int64_t BbrRoundTripCount;

// BbrNetworkController 实现了 BBR 拥塞控制算法。BBR 的目标是估算当前可用的瓶颈带宽和 RTT（因此得名），
// 并基于这些信号调节发送速率和拥塞窗口的大小。
//
// BBR 依赖于发送速率控制以正常工作。当发送速率控制被禁用时，不应使用 BBR。
class BbrNetworkController : public NetworkControllerInterface {
 public:
  enum Mode {
    // 连接的启动阶段。
    STARTUP,
    // 在启动阶段达到最高可能带宽后，降低发送速率以排空队列。
    DRAIN,
    // 巡航模式。
    PROBE_BW,
    // 临时减慢发送速度以清空缓冲区并测量真实的最小 RTT。
    PROBE_RTT,
  };

  // 指示拥塞控制如何限制在途字节量。
  enum RecoveryState {
    // 不限制。
    NOT_IN_RECOVERY = 0,
    // 每确认一个字节就允许一个额外的在途字节。
    CONSERVATION = 1,
    // 每确认一个字节就允许 1.5 个额外的在途字节。
    MEDIUM_GROWTH = 2,
    // 每确认一个字节就允许两个额外的在途字节（慢启动）。
    GROWTH = 3
  };
  struct BbrControllerConfig {
    FieldTrialParameter<double> probe_bw_pacing_gain_offset;
    FieldTrialParameter<double> encoder_rate_gain;
    FieldTrialParameter<double> encoder_rate_gain_in_probe_rtt;
    // 判断是否应该退出启动阶段的 RTT 增量阈值。
    FieldTrialParameter<TimeDelta> exit_startup_rtt_threshold;

    FieldTrialParameter<DataSize> initial_congestion_window;
    FieldTrialParameter<DataSize> min_congestion_window;
    FieldTrialParameter<DataSize> max_congestion_window;

    FieldTrialParameter<double> probe_rtt_congestion_window_gain;
    FieldTrialParameter<bool> pacing_rate_as_target;

    // 在 QUIC BBR 中可配置：
    FieldTrialParameter<bool> exit_startup_on_loss;
    // 在 STARTUP 模式中停留的 RTT 数量，默认为 3。
    FieldTrialParameter<int> num_startup_rtts;
    // 当为 true 时，恢复是基于速率的而不是基于拥塞窗口的。
    FieldTrialParameter<bool> rate_based_recovery;
    FieldTrialParameter<double> max_aggregation_bytes_multiplier;
    // 当为 true 时，在 STARTUP 中以 1.5x 的速率发送并禁用数据包保护。
    FieldTrialParameter<bool> slower_startup;
    // 当为 true 时，禁用 STARTUP 中的数据包保护。
    FieldTrialParameter<bool> rate_based_startup;
    // 首次进入恢复时作为初始数据包保护模式使用。
    FieldTrialEnum<RecoveryState> initial_conservation_in_startup;
    // 如果为 true，在字节在途量低于 BDP 或进入高增益模式之前，不会退出低增益模式。
    FieldTrialParameter<bool> fully_drain_queue;

    FieldTrialParameter<double> max_ack_height_window_multiplier;
    // 如果为 true，在 probe_rtt 中使用 0.75*BDP 的 CWND 而不是 4 个数据包。
    FieldTrialParameter<bool> probe_rtt_based_on_bdp;
    // 如果为 true，在上一个周期的 min_rtt 在当前 min_rtt 的 12.5% 内时，跳过 probe_rtt 并将现有 min_rtt 的时间戳更新为现在。
    // 即使 min_rtt 低估了 12.5%，25% 的增益循环和 2x CWND 增益也应该能克服过小的 min_rtt。
    FieldTrialParameter<bool> probe_rtt_skipped_if_similar_rtt;
    // 如果为 true，只要连接最近受到应用程序限制，就完全禁用 PROBE_RTT。
    FieldTrialParameter<bool> probe_rtt_disabled_if_app_limited;

    explicit BbrControllerConfig(std::string field_trial);
    ~BbrControllerConfig();
    BbrControllerConfig(const BbrControllerConfig&);
    static BbrControllerConfig FromTrial();
  };

  // 调试状态可以被导出以便于排查潜在的拥塞控制问题。
  struct DebugState {
    explicit DebugState(const BbrNetworkController& sender);
    DebugState(const DebugState& state);

    Mode mode;
    DataRate max_bandwidth;
    BbrRoundTripCount round_trip_count;
    int gain_cycle_index;
    DataSize congestion_window;

    bool is_at_full_bandwidth;
    DataRate bandwidth_at_last_round;
    BbrRoundTripCount rounds_without_bandwidth_gain;

    TimeDelta min_rtt;
    Timestamp min_rtt_timestamp;

    RecoveryState recovery_state;
    DataSize recovery_window;

    bool last_sample_is_app_limited;
    int64_t end_of_app_limited_phase;
  };

  explicit BbrNetworkController(NetworkControllerConfig config);
  ~BbrNetworkController() override;

  // NetworkControllerInterface 的实现
  NetworkControlUpdate OnNetworkAvailability(NetworkAvailability msg) override;
  NetworkControlUpdate OnNetworkRouteChange(NetworkRouteChange msg) override;
  NetworkControlUpdate OnProcessInterval(ProcessInterval msg) override;
  NetworkControlUpdate OnSentPacket(SentPacket msg) override;
  NetworkControlUpdate OnStreamsConfig(StreamsConfig msg) override;
  NetworkControlUpdate OnTargetRateConstraints(
      TargetRateConstraints msg) override;
  NetworkControlUpdate OnTransportPacketsFeedback(
      TransportPacketsFeedback msg) override;

  // 远程比特率估算 API 的一部分，BBR 未实现
  NetworkControlUpdate OnRemoteBitrateReport(RemoteBitrateReport msg) override;
  NetworkControlUpdate OnRoundTripTimeUpdate(RoundTripTimeUpdate msg) override;
  NetworkControlUpdate OnTransportLossReport(TransportLossReport msg) override;
  // NetworkControlUpdate OnReceivedPacket(ReceivedPacket msg) override;
  NetworkControlUpdate OnNetworkStateEstimate(
      NetworkStateEstimate msg) override;

  NetworkControlUpdate CreateRateUpdate(Timestamp at_time) const;

 private:
  void Reset();
  bool InSlowStart() const;
  bool InRecovery() const;
  bool IsProbingForMoreBandwidth() const;

  bool CanSend(DataSize bytes_in_flight);
  DataRate PacingRate() const;
  DataRate BandwidthEstimate() const;
  DataSize GetCongestionWindow() const;

  double GetPacingGain(int round_offset) const;

  void OnApplicationLimited(DataSize bytes_in_flight);
  // 结束 SendAlgorithmInterface 的实现。

  typedef WindowedFilter<DataRate,
                         MaxFilter<DataRate>,
                         BbrRoundTripCount,
                         BbrRoundTripCount>
      MaxBandwidthFilter;

  typedef WindowedFilter<TimeDelta,
                         MaxFilter<TimeDelta>,
                         BbrRoundTripCount,
                         BbrRoundTripCount>
      MaxAckDelayFilter;

  typedef WindowedFilter<DataSize,
                         MaxFilter<DataSize>,
                         BbrRoundTripCount,
                         BbrRoundTripCount>
      MaxAckHeightFilter;

  // 返回连接的 RTT 估计值。除了边缘情况外，这是最小 RTT。
  TimeDelta GetMinRtt() const;
  // 返回连接是否已达到退出慢启动所需的全带宽。
  bool IsAtFullBandwidth() const;
  // 使用指定的增益计算目标拥塞窗口。
  DataSize GetTargetCongestionWindow(double gain) const;
  // 在 PROBE_RTT 期间的目标拥塞窗口。
  DataSize ProbeRttCongestionWindow() const;
  // 如果当前的 min_rtt 应该保持，并且我们不应立即进入 PROBE_RTT，则返回 true。
  bool ShouldExtendMinRttExpiry() const;

  // 进入 STARTUP 模式。
  void EnterStartupMode();
  // 进入 PROBE_BW 模式。
  void EnterProbeBandwidthMode(Timestamp now);

  // 从 BandwidthSampler 状态中丢弃丢失的数据包。
  void DiscardLostPackets(const std::vector<PacketResult>& lost_packets);
  // 如果已经过了一个往返，则更新往返计数器。如果计数器已提前，则返回 true。
  // |last_acked_packet| 是最后一个被确认的数据包的序列号。
  bool UpdateRoundTripCounter(int64_t last_acked_packet);
  // 根据接收到的确认的样本更新当前带宽和 min_rtt 估计值。如果 min_rtt 已过期，则返回 true。
  bool UpdateBandwidthAndMinRtt(Timestamp now,
                                const std::vector<PacketResult>& acked_packets);
  // 更新 PROBE_BW 模式中使用的当前增益。
  void UpdateGainCyclePhase(Timestamp now,
                            DataSize prior_in_flight,
                            bool has_losses);
  // 跟踪带宽在多少个往返中没有显著增加。
  void CheckIfFullBandwidthReached();
  // 从 STARTUP 转换到 DRAIN，从 DRAIN 转换到 PROBE_BW（如果适当）。
  void MaybeExitStartupOrDrain(const TransportPacketsFeedback&);
  // 决定是否进入或退出 PROBE_RTT。
  void MaybeEnterOrExitProbeRtt(const TransportPacketsFeedback& msg,
                                bool is_round_start,
                                bool min_rtt_expired);
  // 确定 BBR 是否需要进入、退出或推进恢复状态。
  void UpdateRecoveryState(int64_t last_acked_packet,
                           bool has_losses,
                           bool is_round_start);

  // 以字节为单位更新 ack 聚合的最大过滤器。
  void UpdateAckAggregationBytes(Timestamp ack_time,
                                 DataSize newly_acked_bytes);

  // 确定连接的适当发送速率。
  void CalculatePacingRate();
  // 确定连接的适当拥塞窗口。
  void CalculateCongestionWindow(DataSize bytes_acked);
  // 确定在恢复期间限制在途数据的适当窗口。
  void CalculateRecoveryWindow(DataSize bytes_acked,
                               DataSize bytes_lost,
                               DataSize bytes_in_flight);

  BbrControllerConfig config_;

  RttStats rtt_stats_;
  webrtc::Random random_;
  LossRateFilter loss_rate_;

  absl::optional<TargetRateConstraints> constraints_;

  Mode mode_;

  // 带宽采样器为 BBR 提供了在个别点上的带宽测量。
  BandwidthSampler sampler_;

  // 在连接期间发生的往返次数。
  BbrRoundTripCount round_trip_count_ = 0;

  // 最近发送的数据包的包号。
  int64_t last_sent_packet_;
  // 在 |current_round_trip_end_| 之后确认任何数据包将导致往返计数器前进。
  int64_t current_round_trip_end_;

  // 跟踪多个最近往返中的最大带宽的过滤器。
  MaxBandwidthFilter max_bandwidth_;

  DataRate default_bandwidth_;

  // 跟踪比发送速率更快地确认的最大字节数。
  MaxAckHeightFilter max_ack_height_;

  // 此聚合开始的时间和在此期间确认的字节数。
  absl::optional<Timestamp> aggregation_epoch_start_time_;
  DataSize aggregation_epoch_bytes_;

  // 自从字节在途量下降到目标窗口以下以来确认的字节数。
  DataSize bytes_acked_since_queue_drained_;

  // 计算额外增加的最大 CWND 量以补偿 ack 聚合的乘数。
  double max_aggregation_bytes_multiplier_;

  // 最小 RTT 估计值。如果在该期间没有采样到新值，则会在 10 秒内自动过期（并触发 PROBE_RTT 模式）。
  TimeDelta min_rtt_;
  TimeDelta last_rtt_;
  // 分配当前 |min_rtt_| 值的时间。
  Timestamp min_rtt_timestamp_;

  // 允许的最大在途字节数。
  DataSize congestion_window_;

  // |congestion_window_| 的初始值。
  DataSize initial_congestion_window_;

  // |congestion_window_| 可以达到的最小值。
  DataSize min_congestion_window_;

  // |congestion_window_| 可以达到的最大值。
  DataSize max_congestion_window_;

  // 连接的当前发送速率。
  DataRate pacing_rate_;

  // 当前应用于发送速率的增益。
  double pacing_gain_;
  // 当前应用于拥塞窗口的增益。
  double congestion_window_gain_;

  // 在 PROBE_BW 期间用于拥塞窗口的增益。从 quic_bbr_cwnd_gain 标志中锁定。
  const double congestion_window_gain_constant_;
  // 将平均 RTT 方差添加到拥塞窗口的系数。从 quic_bbr_rtt_variation_weight 标志中锁定。
  const double rtt_variance_weight_;

  // 在 PROBE_BW 模式下的往返回合数，用于确定当前发送速率增益周期。
  int cycle_current_offset_;
  // 最后一个发送速率增益周期开始的时间。
  Timestamp last_cycle_start_;

  // 表示连接是否已达到全带宽模式。
  bool is_at_full_bandwidth_;
  // 没有显著带宽增加的往返回合数。
  BbrRoundTripCount rounds_without_bandwidth_gain_;
  // 与之比较的带宽增加的带宽。
  DataRate bandwidth_at_last_round_;

  // 在退出静默期后设置为 true。
  bool exiting_quiescence_;

  // 必须退出 PROBE_RTT 的时间。将其设置为零表示时间尚未知，因为在途字节数尚未达到所需值。
  absl::optional<Timestamp> exit_probe_rtt_at_;
  // 表示自 PROBE_RTT 激活以来是否已经过了一个往返。
  bool probe_rtt_round_passed_;

  // 表示最近的带宽样本是否被标记为应用程序受限。
  bool last_sample_is_app_limited_;

  // 恢复的当前状态。
  RecoveryState recovery_state_;
  // 在 |end_recovery_at_| 之后接收数据包的确认将导致 BBR 退出恢复模式。设置的值表示至少检测到一个丢失，因此不得重置。
  absl::optional<int64_t> end_recovery_at_;
  // 用于在丢失恢复期间限制在途字节数的窗口。
  DataSize recovery_window_;

  bool app_limited_since_last_probe_rtt_;
  TimeDelta min_rtt_since_last_probe_rtt_;

//  RTC_DISALLOW_COPY_AND_ASSIGN(BbrNetworkController);
};

// 用于日志输出
std::ostream& operator<<(  // no-presubmit-check TODO(webrtc:8982)
    std::ostream& os,      // no-presubmit-check TODO(webrtc:8982)
    const BbrNetworkController::Mode& mode);

}  // namespace bbr
}  // namespace webrtc

#endif  // MODULES_CONGESTION_CONTROLLER_BBR_BBR_NETWORK_CONTROLLER_H_
