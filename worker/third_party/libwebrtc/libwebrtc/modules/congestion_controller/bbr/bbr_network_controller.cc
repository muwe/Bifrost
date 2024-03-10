/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/congestion_controller/bbr/bbr_network_controller.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "absl/base/macros.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/field_trial.h"



namespace webrtc {
namespace bbr {
namespace {

// 如果大于零，平均 RTT 变化乘以指定的因子并加到拥塞窗口限制上。
const double kBbrRttVariationWeight = 0.0f;

// QUIC BBR 在 PROBE_BW 阶段的拥塞窗口增益。
const double kProbeBWCongestionWindowGain = 2.0f;

// 任何 QUIC 数据包的最大包大小，基于以太网的最大大小，
// 减去 IP 和 UDP 头部。IPv6 有一个 40 字节的头部，UDP 再增加 8 字节。
// 这是总共 48 字节的开销。以太网的最大包大小是 1500 字节，1500 - 48 = 1452。
const DataSize kMaxPacketSize = DataSize::bytes(1452);

// Linux TCP 实现中使用的默认最大包大小。
// 在 QUIC 中用于字节单位的拥塞窗口计算。
const DataSize kDefaultTCPMSS = DataSize::bytes(1460);
// 基于 TCP 默认值的常量。
const DataSize kMaxSegmentSize = kDefaultTCPMSS;

// 慢启动使用的增益，等于 2/ln(2)。
const double kHighGain = 2.885f;
// 在检测到丢失后 STARTUP 阶段使用的增益。
// 1.5 足以允许 25% 的外部丢失并且仍然观察到测量带宽的 25% 增长。
const double kStartupAfterLossGain = 1.5;
// 慢启动后排空队列使用的增益。
const double kDrainGain = 1.f / kHighGain;

// 增益周期的长度。
const size_t kGainCycleLength = 8;
// 带宽滤波器窗口的大小，以往返次数计。
const BbrRoundTripCount kBandwidthWindowSize = kGainCycleLength + 2;

// 当前 min_rtt 值过期的时间。
constexpr int64_t kMinRttExpirySeconds = 10;
// 连接在 PROBE_RTT 模式下花费的最短时间。
constexpr int64_t kProbeRttTimeMs = 200;
// 如果带宽在 |kRoundTripsWithoutGrowthBeforeExitingStartup| 轮内没有增长到 |kStartupGrowthTarget| 的因子，
// 连接将退出 STARTUP 模式。
const double kStartupGrowthTarget = 1.25;
// 系数，用于确定新的 RTT 是否与 min_rtt 足够相似，以致我们不需要进入 PROBE_RTT。
const double kSimilarMinRttThreshold = 1.125;

constexpr int64_t kInitialBandwidthKbps = 300;

const int64_t kInitialCongestionWindowPackets = 32;
// 保证延迟确认不会降低带宽测量的最小 CWND。
// 不会增加发送速率。
const int64_t kDefaultMinCongestionWindowPackets = 20;//4;
const int64_t kDefaultMaxCongestionWindowPackets = 2000;

const TimeDelta kTargetMinRtt = TimeDelta::ms(50);
const char kBbrConfigTrial[] = "WebRTC-BweBbrConfig";

}  // namespace

BbrNetworkController::BbrControllerConfig::BbrControllerConfig(
    std::string field_trial)
    : probe_bw_pacing_gain_offset("probe_bw_pacing_gain_offset", 0.25),
      encoder_rate_gain("encoder_rate_gain", 1),
      encoder_rate_gain_in_probe_rtt("encoder_rate_gain_in_probe_rtt", 1),
      exit_startup_rtt_threshold("exit_startup_rtt_threshold",
                                 TimeDelta::PlusInfinity()),
      initial_congestion_window(
          "initial_cwin",
          kInitialCongestionWindowPackets * kDefaultTCPMSS),
      min_congestion_window(
          "min_cwin",
          kDefaultMinCongestionWindowPackets * kDefaultTCPMSS),
      max_congestion_window(
          "max_cwin",
          kDefaultMaxCongestionWindowPackets * kDefaultTCPMSS),
      probe_rtt_congestion_window_gain("probe_rtt_cwin_gain", 0.75),
      pacing_rate_as_target("pacing_rate_as_target", false),
      exit_startup_on_loss("exit_startup_on_loss", true),
      num_startup_rtts("num_startup_rtts", 3),
      rate_based_recovery("rate_based_recovery", false),
      max_aggregation_bytes_multiplier("max_aggregation_bytes_multiplier", 0),
      slower_startup("slower_startup", false),
      rate_based_startup("rate_based_startup", false),
      initial_conservation_in_startup("initial_conservation",
                                      CONSERVATION,
                                      {
                                          {"NOT_IN_RECOVERY", NOT_IN_RECOVERY},
                                          {"CONSERVATION", CONSERVATION},
                                          {"MEDIUM_GROWTH", MEDIUM_GROWTH},
                                          {"GROWTH", GROWTH},
                                      }),
      fully_drain_queue("fully_drain_queue", false),
      max_ack_height_window_multiplier("max_ack_height_window_multiplier", 1),
      probe_rtt_based_on_bdp("probe_rtt_based_on_bdp", false),
      probe_rtt_skipped_if_similar_rtt("probe_rtt_skipped_if_similar_rtt",
                                       false),
      probe_rtt_disabled_if_app_limited("probe_rtt_disabled_if_app_limited",
                                        false) {
  ParseFieldTrial(
      {
          &exit_startup_on_loss,
          &encoder_rate_gain,
          &encoder_rate_gain_in_probe_rtt,
          &exit_startup_rtt_threshold,
          &fully_drain_queue,
          &initial_congestion_window,
          &initial_conservation_in_startup,
          &max_ack_height_window_multiplier,
          &max_aggregation_bytes_multiplier,
          &max_congestion_window,
          &min_congestion_window,
          &num_startup_rtts,
          &pacing_rate_as_target,
          &probe_bw_pacing_gain_offset,
          &probe_rtt_based_on_bdp,
          &probe_rtt_congestion_window_gain,
          &probe_rtt_disabled_if_app_limited,
          &probe_rtt_skipped_if_similar_rtt,
          &rate_based_recovery,
          &rate_based_startup,
          &slower_startup,
      },
      field_trial);
}
BbrNetworkController::BbrControllerConfig::~BbrControllerConfig() = default;
BbrNetworkController::BbrControllerConfig::BbrControllerConfig(
    const BbrControllerConfig&) = default;
BbrNetworkController::BbrControllerConfig
BbrNetworkController::BbrControllerConfig::FromTrial() {
  return BbrControllerConfig(
      webrtc::field_trial::FindFullName(kBbrConfigTrial));
}

BbrNetworkController::DebugState::DebugState(const BbrNetworkController& sender)
    : mode(sender.mode_),
      max_bandwidth(sender.max_bandwidth_.GetBest()),
      round_trip_count(sender.round_trip_count_),
      gain_cycle_index(sender.cycle_current_offset_),
      congestion_window(sender.congestion_window_),
      is_at_full_bandwidth(sender.is_at_full_bandwidth_),
      bandwidth_at_last_round(sender.bandwidth_at_last_round_),
      rounds_without_bandwidth_gain(sender.rounds_without_bandwidth_gain_),
      min_rtt(sender.min_rtt_),
      min_rtt_timestamp(sender.min_rtt_timestamp_),
      recovery_state(sender.recovery_state_),
      recovery_window(sender.recovery_window_),
      last_sample_is_app_limited(sender.last_sample_is_app_limited_),
      end_of_app_limited_phase(sender.sampler_.end_of_app_limited_phase()) {}

BbrNetworkController::DebugState::DebugState(const DebugState& state) = default;

BbrNetworkController::BbrNetworkController(NetworkControllerConfig config)
    : config_(BbrControllerConfig::FromTrial()),
      rtt_stats_(),
      random_(10),
      loss_rate_(),
      mode_(STARTUP),
      // sampler_(new BandwidthSampler()),
      round_trip_count_(0),
      last_sent_packet_(0),
      current_round_trip_end_(0),
      max_bandwidth_(kBandwidthWindowSize, DataRate::Zero(), 0),
      default_bandwidth_(DataRate::kbps(kInitialBandwidthKbps)),
      max_ack_height_(kBandwidthWindowSize, DataSize::Zero(), 0),
      aggregation_epoch_start_time_(),
      aggregation_epoch_bytes_(DataSize::Zero()),
      bytes_acked_since_queue_drained_(DataSize::Zero()),
      max_aggregation_bytes_multiplier_(0),
      min_rtt_(TimeDelta::Zero()),
      last_rtt_(TimeDelta::Zero()),
      min_rtt_timestamp_(Timestamp::MinusInfinity()),
      congestion_window_(config_.initial_congestion_window),
      initial_congestion_window_(config_.initial_congestion_window),
      min_congestion_window_(config_.min_congestion_window),
      max_congestion_window_(config_.max_congestion_window),
      pacing_rate_(DataRate::Zero()),
      pacing_gain_(1),
      congestion_window_gain_constant_(kProbeBWCongestionWindowGain),
      rtt_variance_weight_(kBbrRttVariationWeight),
      cycle_current_offset_(0),
      last_cycle_start_(Timestamp::MinusInfinity()),
      is_at_full_bandwidth_(false),
      rounds_without_bandwidth_gain_(0),
      bandwidth_at_last_round_(DataRate::Zero()),
      exiting_quiescence_(false),
      exit_probe_rtt_at_(),
      probe_rtt_round_passed_(false),
      last_sample_is_app_limited_(false),
      recovery_state_(NOT_IN_RECOVERY),
      end_recovery_at_(),
      recovery_window_(max_congestion_window_),
      app_limited_since_last_probe_rtt_(false),
      min_rtt_since_last_probe_rtt_(TimeDelta::PlusInfinity()) {
  RTC_LOG(LS_INFO) << "RTC::Creating BBR controller";

  if (config.constraints.starting_rate)
    default_bandwidth_ = *config.constraints.starting_rate;
  constraints_ = config.constraints;
  Reset();
}

BbrNetworkController::~BbrNetworkController() {

  RTC_LOG(LS_INFO) << "~RTC::BbrNetworkController";

}

void BbrNetworkController::Reset() {
  round_trip_count_ = 0;
  rounds_without_bandwidth_gain_ = 0;
  if (config_.num_startup_rtts > 0) {
    is_at_full_bandwidth_ = false;
    EnterStartupMode();
  } else {
    is_at_full_bandwidth_ = true;
    EnterProbeBandwidthMode(constraints_->at_time);
  }
}

NetworkControlUpdate BbrNetworkController::CreateRateUpdate(
    Timestamp at_time) const {
  DataRate bandwidth = BandwidthEstimate();
  if (bandwidth.IsZero())
    bandwidth = default_bandwidth_;
  TimeDelta rtt = GetMinRtt();
  DataRate pacing_rate = PacingRate();
  DataRate target_rate =
      config_.pacing_rate_as_target ? pacing_rate : bandwidth;

  if (mode_ == PROBE_RTT)
    target_rate = target_rate * config_.encoder_rate_gain_in_probe_rtt;
  else
    target_rate = target_rate * config_.encoder_rate_gain;
  target_rate = std::min(target_rate, pacing_rate);

  if (constraints_) {
    if (constraints_->max_data_rate) {
      target_rate = std::min(target_rate, *constraints_->max_data_rate);
      pacing_rate = std::min(pacing_rate, *constraints_->max_data_rate);
    }
    if (constraints_->min_data_rate) {
      target_rate = std::max(target_rate, *constraints_->min_data_rate);
      pacing_rate = std::max(pacing_rate, *constraints_->min_data_rate);
    }
  }

  NetworkControlUpdate update;

  TargetTransferRate target_rate_msg;
  target_rate_msg.network_estimate.at_time = at_time;
  target_rate_msg.network_estimate.round_trip_time = rtt;

  // TODO(srte): 使用正确的值填充下面的字段。
  target_rate_msg.network_estimate.loss_rate_ratio = 0;
  // 在 PROBE_BW 模式中，目标带宽预计会在周期内变化。
  // 在其他模式中没有给定的周期，因此为了一致性，使用与 PROBE_BW 相同的值。
  target_rate_msg.network_estimate.bwe_period =
      rtt * static_cast<int64_t>(kGainCycleLength);

  target_rate_msg.target_rate = target_rate;
  target_rate_msg.at_time = at_time;
  update.target_rate = target_rate_msg;

  PacerConfig pacer_config;
  // 一个小的时间窗口确保了均匀的发送速率。
  pacer_config.time_window = rtt * 0.25;
  pacer_config.data_window = pacer_config.time_window * pacing_rate;


  pacer_config.local_data_rate = pacing_rate;

  if (IsProbingForMoreBandwidth()) {
    pacer_config.pad_window = pacer_config.data_window;
    pacer_config.local_pad_rate = pacer_config.local_data_rate;
  }
  else {
    pacer_config.pad_window = DataSize::Zero();
    pacer_config.local_pad_rate = DataRate::Zero();

  }


  pacer_config.at_time = at_time;
  update.pacer_config = pacer_config;

  update.congestion_window = GetCongestionWindow();

  // RTC_LOG(LS_INFO) << " bbr CreateRateUpdate" 
  //                 // 打印实时数据
  //           << " ,min rtt=" << rtc::ToString(rtt.ms()) << " ms"
  //           << " ,local pacing_rate=" << rtc::ToString(pacing_rate.bps())   << " bps"
  //           << " ,local target_rate=" << rtc::ToString(target_rate.bps())   << " bps"
  //           << " ,target_rate=" << rtc::ToString(update.target_rate->target_rate.bps()) << " bps"
  //           << " ,data_rate=" << rtc::ToString(update.pacer_config->data_rate().bps())   << " bps"
  //           << " ,pad_rate=" << rtc::ToString(update.pacer_config->pad_rate().bps())  << " bps"
  //           << " ,data_window=" << rtc::ToString(update.pacer_config->data_window.bytes())  << " bytes"
  //           << " ,pad_window=" << rtc::ToString(update.pacer_config->pad_window.bytes())  << " bytes"
  //           << " ,time_window=" << rtc::ToString(update.pacer_config->time_window.ms())  << " ms"
  //           << " ,congestion_window=" << rtc::ToString(update.congestion_window->bytes())  << " bytes"
  //           ;


  return update;
}

NetworkControlUpdate BbrNetworkController::OnNetworkAvailability(
    NetworkAvailability msg) {
  Reset();
  rtt_stats_.OnConnectionMigration();
  return CreateRateUpdate(msg.at_time);
}

NetworkControlUpdate BbrNetworkController::OnNetworkRouteChange(
    NetworkRouteChange msg) {
  constraints_ = msg.constraints;
  Reset();
  if (msg.constraints.starting_rate)
    default_bandwidth_ = *msg.constraints.starting_rate;

  rtt_stats_.OnConnectionMigration();
  return CreateRateUpdate(msg.at_time);
}

NetworkControlUpdate BbrNetworkController::OnProcessInterval(
    ProcessInterval msg) {
  return CreateRateUpdate(msg.at_time);
}

NetworkControlUpdate BbrNetworkController::OnStreamsConfig(StreamsConfig msg) {
  return NetworkControlUpdate();
}

NetworkControlUpdate BbrNetworkController::OnTargetRateConstraints(
    TargetRateConstraints msg) {
  constraints_ = msg;
  return CreateRateUpdate(msg.at_time);
}

bool BbrNetworkController::InSlowStart() const {
  return mode_ == STARTUP;
}

NetworkControlUpdate BbrNetworkController::OnSentPacket(SentPacket msg) {
  last_sent_packet_ = msg.sequence_number;

  if (msg.data_in_flight.IsZero() && sampler_.is_app_limited()) {
    exiting_quiescence_ = true;
  }

  if (!aggregation_epoch_start_time_) {
    aggregation_epoch_start_time_ = msg.send_time;
  }

  sampler_.OnPacketSent(msg.send_time, msg.sequence_number, msg.size,
                         msg.data_in_flight);
  return NetworkControlUpdate();
}

bool BbrNetworkController::CanSend(DataSize bytes_in_flight) {
  return bytes_in_flight < GetCongestionWindow();
}

DataRate BbrNetworkController::PacingRate() const {
  if (pacing_rate_.IsZero()) {
    return kHighGain * initial_congestion_window_ / GetMinRtt();
  }
  return pacing_rate_;
}

DataRate BbrNetworkController::BandwidthEstimate() const {
  return max_bandwidth_.GetBest();
}

DataSize BbrNetworkController::GetCongestionWindow() const {
  if (mode_ == PROBE_RTT) {
    return ProbeRttCongestionWindow();
  }

  if (InRecovery() && !config_.rate_based_recovery &&
      !(config_.rate_based_startup && mode_ == STARTUP)) {
    return std::min(congestion_window_, recovery_window_);
  }

  return congestion_window_;
}

double BbrNetworkController::GetPacingGain(int round_offset) const {
  if (round_offset == 0)
    return 1 + config_.probe_bw_pacing_gain_offset;
  else if (round_offset == 1)
    return 1 - config_.probe_bw_pacing_gain_offset;
  else
    return 1;
}

bool BbrNetworkController::InRecovery() const {
  return recovery_state_ != NOT_IN_RECOVERY;
}

bool BbrNetworkController::IsProbingForMoreBandwidth() const {
  return (mode_ == PROBE_BW && pacing_gain_ > 1) || mode_ == STARTUP;
}

NetworkControlUpdate BbrNetworkController::OnTransportPacketsFeedback(
    TransportPacketsFeedback msg) {
  if (msg.packet_feedbacks.empty())
    return NetworkControlUpdate();

  Timestamp feedback_recv_time = msg.feedback_time;
  SentPacket last_sent_packet = msg.PacketsWithFeedback().back().sent_packet;

  Timestamp send_time = last_sent_packet.send_time;
  TimeDelta send_delta = feedback_recv_time - send_time;
  rtt_stats_.UpdateRtt(send_delta, TimeDelta::Zero(), feedback_recv_time);

  const DataSize total_data_acked_before = sampler_.total_data_acked();

  bool is_round_start = false;
  bool min_rtt_expired = false;

  std::vector<PacketResult> lost_packets = msg.LostWithSendInfo();
  DiscardLostPackets(lost_packets);

  std::vector<PacketResult> acked_packets = msg.ReceivedWithSendInfo();

  int packets_sent =
      static_cast<int>(lost_packets.size() + acked_packets.size());
  int packets_lost = static_cast<int>(lost_packets.size());
  loss_rate_.UpdateWithLossStatus(msg.feedback_time.ms(), packets_sent,
                                  packets_lost);

  // 将新数据输入到 BBR 模型中。
  if (!acked_packets.empty()) {
    int64_t last_acked_packet =
        acked_packets.rbegin()->sent_packet.sequence_number;

    is_round_start = UpdateRoundTripCounter(last_acked_packet);
    min_rtt_expired =
        UpdateBandwidthAndMinRtt(msg.feedback_time, acked_packets);
    UpdateRecoveryState(last_acked_packet, !lost_packets.empty(),
                        is_round_start);

    const DataSize data_acked =
        sampler_.total_data_acked() - total_data_acked_before;

    UpdateAckAggregationBytes(msg.feedback_time, data_acked);
    if (max_aggregation_bytes_multiplier_ > 0) {
      if (msg.data_in_flight <=
          1.25 * GetTargetCongestionWindow(pacing_gain_)) {
        bytes_acked_since_queue_drained_ = DataSize::Zero();
      } else {
        bytes_acked_since_queue_drained_ += data_acked;
      }
    }
  }

  // 处理 PROBE_BW 模式特有的逻辑。
  if (mode_ == PROBE_BW) {
    UpdateGainCyclePhase(msg.feedback_time, msg.prior_in_flight,
                         !lost_packets.empty());
  }

  // 处理 STARTUP 和 DRAIN 模式特有的逻辑。
  if (is_round_start && !is_at_full_bandwidth_) {
    CheckIfFullBandwidthReached();
  }
  MaybeExitStartupOrDrain(msg);

  // 处理 PROBE_RTT 特有的逻辑。
  MaybeEnterOrExitProbeRtt(msg, is_round_start, min_rtt_expired);

  // 计算确认和丢失的数据包数量。
  DataSize data_acked = sampler_.total_data_acked() - total_data_acked_before;
  DataSize data_lost = DataSize::Zero();
  for (const PacketResult& packet : lost_packets) {
    data_lost += packet.sent_packet.size;
  }

  // 在模型更新后，重新计算发送速率和拥塞窗口。
  CalculatePacingRate();
  CalculateCongestionWindow(data_acked);
  CalculateRecoveryWindow(data_acked, data_lost, msg.data_in_flight);
  // 清理内部状态。
  if (!acked_packets.empty()) {
    sampler_.RemoveObsoletePackets(
        acked_packets.back().sent_packet.sequence_number);
  }
      // RTC_LOG(LS_INFO) << "Exiting startup due to rtt increase from: "
      //                  << ToString(max_bandwidth_.GetBest()) << " to:" << ToString(last_rtt_)
      //                  << " > " << ToString(min_rtt_ + exit_threshold);


  RTC_LOG(LS_INFO) << " bbrbw " 
                  // 打印实时数据
                  // << " bandwidth 1first " << ToString(max_bandwidth_.GetBest()) << " bps "
                  // << " bandwidth 2first " << ToString(max_bandwidth_.GetSecondBest()) << " bps " 
                  // << " bandwidth 3first " << ToString(max_bandwidth_.GetThirdBest()) << " bps "
                  // 打印带宽
                  << " ,bandw first " << rtc::ToString(max_bandwidth_.GetBest().bps()) << " bps "
                  // << " ,bandw sec " << ToString(max_bandwidth_.GetSecondBest()) << " bps " 
                  // << " ,bandw third " << ToString(max_bandwidth_.GetThirdBest()) << " bps "
                  // 打印rtt
                  << " ,sm_rtt " << rtc::ToString(rtt_stats_.smoothed_rtt().ms()) << " ms"
                  << " ,pre_srtt " << rtc::ToString(rtt_stats_.previous_srtt().ms()) << " ms"
                  << " ,latest_rtt " << rtc::ToString(rtt_stats_.latest_rtt().ms()) << " ms"
                  << " ,min_rtt " << rtc::ToString(rtt_stats_.min_rtt().ms()) << " ms"
                  // 打印丢包
                  << " ,loss rate " << rtc::ToString(loss_rate_.GetLossRate()) << " %"
                  // 打印拥塞窗口
                  << " ,1 bdp " << rtc::ToString((min_rtt_*max_bandwidth_.GetBest()).bytes())  << " bytes "
                  << " ,con_win_gain " << rtc::ToString(congestion_window_gain_)
                  << " ,con_win " << rtc::ToString(congestion_window_.bytes()) << " bytes"
                  << " ,min con_win " << rtc::ToString(min_congestion_window_.bytes()) << " bytes"
                  << " ,max con_win " << rtc::ToString(max_congestion_window_.bytes()) << " bytes"
                  // 打印发送速率
                  << " ,is_full_bandwith " << rtc::ToString(is_at_full_bandwidth_)
                  << " ,pace_rate " << rtc::ToString(pacing_rate_.bps())  << " bps "
                  << " ,pace_gain " << rtc::ToString(pacing_gain_)
                  << " ,prior_in_flight " << rtc::ToString(msg.prior_in_flight.bytes())  << " bytes "
                  << " ,lost_packets " << rtc::ToString(lost_packets.size())
                  // data in flight
                  // << " ,data_sent " << rtc::ToString(msg.data_sent.bytes())  << " bytes"
                  << " ,data_acked " << rtc::ToString(data_acked.bytes())  << " bytes"
                  << " ,data_in_flight " << rtc::ToString(msg.data_in_flight.bytes())  << " bytes"
                  // 打印模式
                  << " ,mode_ " << mode_
                  << " ,round_count " << round_trip_count_
                  ;


  
  return CreateRateUpdate(msg.feedback_time);
}

NetworkControlUpdate BbrNetworkController::OnRemoteBitrateReport(
    RemoteBitrateReport msg) {
  return NetworkControlUpdate();
}
NetworkControlUpdate BbrNetworkController::OnRoundTripTimeUpdate(
    RoundTripTimeUpdate msg) {
  return NetworkControlUpdate();
}
NetworkControlUpdate BbrNetworkController::OnTransportLossReport(
    TransportLossReport msg) {
  return NetworkControlUpdate();
}

// NetworkControlUpdate BbrNetworkController::OnReceivedPacket(
//     ReceivedPacket msg) {
//   return NetworkControlUpdate();
// }

NetworkControlUpdate BbrNetworkController::OnNetworkStateEstimate(
    NetworkStateEstimate msg) {
  return NetworkControlUpdate();
}

TimeDelta BbrNetworkController::GetMinRtt() const {
  return !min_rtt_.IsZero() ? min_rtt_
                            : TimeDelta::ms(rtt_stats_.initial_rtt_us());
}

DataSize BbrNetworkController::GetTargetCongestionWindow(double gain) const {
  TimeDelta min_rtt = GetMinRtt();
  min_rtt = std::max(min_rtt,kTargetMinRtt);
  DataSize bdp = min_rtt * BandwidthEstimate();
  DataSize congestion_window = gain * bdp;

  // BDP estimate will be zero if no bandwidth samples are available yet.
  if (congestion_window.IsZero()) {
    congestion_window = gain * initial_congestion_window_;
  }

  return std::max(congestion_window, min_congestion_window_);
}

DataSize BbrNetworkController::ProbeRttCongestionWindow() const {
  if (config_.probe_rtt_based_on_bdp) {
    return GetTargetCongestionWindow(config_.probe_rtt_congestion_window_gain);
  }
  return min_congestion_window_;
}

void BbrNetworkController::EnterStartupMode() {
  mode_ = STARTUP;
  pacing_gain_ = kHighGain;
  congestion_window_gain_ = kHighGain;
}

void BbrNetworkController::EnterProbeBandwidthMode(Timestamp now) {
  mode_ = PROBE_BW;
  congestion_window_gain_ = congestion_window_gain_constant_;

  // 从 {0, 2..7} 范围中随机选择一个增益周期的偏移量。1 被排除在外，因为在那种情况下，增加的增益和减少的增益不会相继发生。
  cycle_current_offset_ = random_.Rand(kGainCycleLength - 2);
  if (cycle_current_offset_ >= 1) {
    cycle_current_offset_ += 1;
  }

  last_cycle_start_ = now;
  pacing_gain_ = GetPacingGain(cycle_current_offset_);
}

void BbrNetworkController::DiscardLostPackets(
    const std::vector<PacketResult>& lost_packets) {
  for (const PacketResult& packet : lost_packets) {
    sampler_.OnPacketLost(packet.sent_packet.sequence_number);
  }
}

bool BbrNetworkController::UpdateRoundTripCounter(int64_t last_acked_packet) {
  if (last_acked_packet > current_round_trip_end_) {
    round_trip_count_++;
    current_round_trip_end_ = last_sent_packet_;
    return true;
  }

  return false;
}

bool BbrNetworkController::UpdateBandwidthAndMinRtt(
    Timestamp now,
    const std::vector<PacketResult>& acked_packets) {
  TimeDelta sample_rtt = TimeDelta::PlusInfinity();
  for (const auto& packet : acked_packets) {
    BandwidthSample bandwidth_sample =
        sampler_.OnPacketAcknowledged(now, packet.sent_packet.sequence_number);
    last_sample_is_app_limited_ = bandwidth_sample.is_app_limited;
    if (!bandwidth_sample.rtt.IsZero()) {
      sample_rtt = std::min(sample_rtt, bandwidth_sample.rtt);
    }

    // 如果样本不受应用程序限制或带宽大于当前带宽估计，则更新最大带宽。
    if (!bandwidth_sample.is_app_limited ||
        bandwidth_sample.bandwidth > BandwidthEstimate()) {
      max_bandwidth_.Update(bandwidth_sample.bandwidth, round_trip_count_);
    }
  }

  // 如果没有有效的 RTT 样本，则立即返回。
  if (sample_rtt.IsInfinite()) {
    return false;
  }

  // 更新最近的 RTT 和自上次探测 RTT 以来的最小 RTT。
  last_rtt_ = sample_rtt;
  min_rtt_since_last_probe_rtt_ =
      std::min(min_rtt_since_last_probe_rtt_, sample_rtt);

  // 检查最小 RTT 是否过期。
  const TimeDelta kMinRttExpiry = TimeDelta::seconds(kMinRttExpirySeconds);
  bool min_rtt_expired =
      !min_rtt_.IsZero() && (now > (min_rtt_timestamp_ + kMinRttExpiry));

  // 更新最小 RTT 和相关时间戳。
  if (min_rtt_expired || sample_rtt < min_rtt_ || min_rtt_.IsZero()) {
    if (ShouldExtendMinRttExpiry()) {
      min_rtt_expired = false;
    } else {
      min_rtt_ = sample_rtt;
    }
    min_rtt_timestamp_ = now;
    // 重置自上次探测 RTT 以来的相关字段。
    min_rtt_since_last_probe_rtt_ = TimeDelta::PlusInfinity();
    app_limited_since_last_probe_rtt_ = false;
  }

  return min_rtt_expired;
}

bool BbrNetworkController::ShouldExtendMinRttExpiry() const {
  // 如果最近受到应用程序限制，则延长当前的最小 RTT。
  if (config_.probe_rtt_disabled_if_app_limited &&
      app_limited_since_last_probe_rtt_) {
    return true;
  }
  // 如果自上次探测 RTT 以来的最小 RTT 增加了，则延长当前的最小 RTT。
  const bool min_rtt_increased_since_last_probe =
      min_rtt_since_last_probe_rtt_ > min_rtt_ * kSimilarMinRttThreshold;
  if (config_.probe_rtt_skipped_if_similar_rtt &&
      app_limited_since_last_probe_rtt_ &&
      !min_rtt_increased_since_last_probe) {
    return true;
  }
  return false;
}

void BbrNetworkController::UpdateGainCyclePhase(Timestamp now,
                                                DataSize prior_in_flight,
                                                bool has_losses) {
  // 在大多数情况下，周期在一个 RTT 后推进。
  bool should_advance_gain_cycling = now - last_cycle_start_ > GetMinRtt();

  // 如果发送增益大于 1.0，则连接试图通过增加在途字节量至少达到 pacing_gain * BDP 来探测带宽。
  // 确保实际达到目标，只要没有丢失表明缓冲区无法容纳那么多数据。
  if (pacing_gain_ > 1.0 && !has_losses &&
      prior_in_flight < GetTargetCongestionWindow(pacing_gain_)) {
    should_advance_gain_cycling = false;
  }

  // 如果发送增益小于 1.0，则连接试图排空之前探测时可能产生的额外队列。
  // 如果在途字节量提前降至估计的 BDP 值，则认为队列已成功排空，并提前结束此周期。
  if (pacing_gain_ < 1.0 && prior_in_flight <= GetTargetCongestionWindow(1)) {
    should_advance_gain_cycling = true;
  }

  // 推进增益周期。
  if (should_advance_gain_cycling) {
    cycle_current_offset_ = (cycle_current_offset_ + 1) % kGainCycleLength;
    last_cycle_start_ = now;
    // 保持低增益模式直到达到目标 BDP。
    // 当达到目标 BDP 时立即退出低增益模式。
    if (config_.fully_drain_queue && pacing_gain_ < 1 &&
        GetPacingGain(cycle_current_offset_) == 1 &&
        prior_in_flight > GetTargetCongestionWindow(1)) {
      return;
    }
    pacing_gain_ = GetPacingGain(cycle_current_offset_);
  }
}

void BbrNetworkController::CheckIfFullBandwidthReached() {
  if (last_sample_is_app_limited_) {
    return;
  }

  DataRate target = bandwidth_at_last_round_ * kStartupGrowthTarget;
  if (BandwidthEstimate() >= target) {
    bandwidth_at_last_round_ = BandwidthEstimate();
    rounds_without_bandwidth_gain_ = 0;
    return;
  }

  rounds_without_bandwidth_gain_++;
  if ((rounds_without_bandwidth_gain_ >= config_.num_startup_rtts) ||
      (config_.exit_startup_on_loss && InRecovery())) {
    is_at_full_bandwidth_ = true;
  }
}

void BbrNetworkController::MaybeExitStartupOrDrain(
    const TransportPacketsFeedback& msg) {
  TimeDelta exit_threshold = config_.exit_startup_rtt_threshold;
  TimeDelta rtt_delta = last_rtt_ - min_rtt_;
  if (mode_ == STARTUP &&
      (is_at_full_bandwidth_ || rtt_delta > exit_threshold)) {
    if (rtt_delta > exit_threshold)
      RTC_LOG(LS_INFO) << "Exiting startup due to rtt increase from: "
                       << ToString(min_rtt_) << " to:" << ToString(last_rtt_)
                       << " > " << ToString(min_rtt_ + exit_threshold);
    mode_ = DRAIN;
    pacing_gain_ = kDrainGain;
    congestion_window_gain_ = kHighGain;
  }
  if (mode_ == DRAIN && msg.data_in_flight <= GetTargetCongestionWindow(1)) {
    EnterProbeBandwidthMode(msg.feedback_time);
  }
}

void BbrNetworkController::MaybeEnterOrExitProbeRtt(
    const TransportPacketsFeedback& msg,
    bool is_round_start,
    bool min_rtt_expired) {
  if (min_rtt_expired && !exiting_quiescence_ && mode_ != PROBE_RTT) {
    mode_ = PROBE_RTT;
    pacing_gain_ = 1;
    // 不要决定退出 PROBE_RTT 的时间，直到 |bytes_in_flight| 达到目标小值。
    exit_probe_rtt_at_.reset();
  }

  if (mode_ == PROBE_RTT) {
    sampler_.OnAppLimited();

    if (!exit_probe_rtt_at_) {
      // 如果窗口已达到适当大小，则计划退出 PROBE_RTT。
      // PROBE_RTT 期间的 CWND 是 kMinimumCongestionWindow，但我们允许额外的一个数据包，
      // 因为 QUIC 在发送数据包之前检查 CWND。
      if (msg.data_in_flight < ProbeRttCongestionWindow() + kMaxPacketSize) {
        exit_probe_rtt_at_ =
            msg.feedback_time + TimeDelta::ms(kProbeRttTimeMs);
        probe_rtt_round_passed_ = false;
      }
    } else {
      if (is_round_start) {
        probe_rtt_round_passed_ = true;
      }
      if (msg.feedback_time >= *exit_probe_rtt_at_ && probe_rtt_round_passed_) {
        min_rtt_timestamp_ = msg.feedback_time;
        if (!is_at_full_bandwidth_) {
          EnterStartupMode();
        } else {
          EnterProbeBandwidthMode(msg.feedback_time);
        }
      }
    }
  }

  exiting_quiescence_ = false;
}

void BbrNetworkController::UpdateRecoveryState(int64_t last_acked_packet,
                                               bool has_losses,
                                               bool is_round_start) {
  // 当一个轮次中没有损失时退出恢复。
  if (has_losses) {
    end_recovery_at_ = last_sent_packet_;
  }

  switch (recovery_state_) {
    case NOT_IN_RECOVERY:
      // 在第一次损失时进入保守模式。
      if (has_losses) {
        recovery_state_ = CONSERVATION;
        if (mode_ == STARTUP) {
          recovery_state_ = config_.initial_conservation_in_startup;
        }
        // 这将导致在 CalculateRecoveryWindow() 中将 |recovery_window_| 设置为正确的值。
        recovery_window_ = DataSize::Zero();
        // 由于保守阶段意味着持续一个完整的轮次，因此延长当前轮次，就像它现在才开始一样。
        current_round_trip_end_ = last_sent_packet_;
      }
      break;

    case CONSERVATION:
    case MEDIUM_GROWTH:
      if (is_round_start) {
        recovery_state_ = GROWTH;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GROWTH:
      // 如果适当，则退出恢复。
      if (!has_losses &&
          (!end_recovery_at_ || last_acked_packet > *end_recovery_at_)) {
        recovery_state_ = NOT_IN_RECOVERY;
      }

      break;
  }
}

void BbrNetworkController::UpdateAckAggregationBytes(
    Timestamp ack_time,
    DataSize newly_acked_bytes) {
  if (!aggregation_epoch_start_time_) {
    RTC_LOG(LS_ERROR)
        << "Received feedback before information about sent packets.";
    RTC_DCHECK(aggregation_epoch_start_time_.has_value());
    return;
  }
  // 计算在假设最大带宽正确的情况下预期会被确认的字节数。
  DataSize expected_bytes_acked =
      max_bandwidth_.GetBest() * (ack_time - *aggregation_epoch_start_time_);
  // 一旦确认到达速率小于或等于最大带宽，就重置当前聚合时代。
  if (aggregation_epoch_bytes_ <= expected_bytes_acked) {
    // 重置以开始测量新的聚合时代。
    aggregation_epoch_bytes_ = newly_acked_bytes;
    aggregation_epoch_start_time_ = ack_time;
    return;
  }

  // 计算与最大带宽相比额外交付的字节数。
  // 包括最近确认的字节以考虑延伸的确认。
  aggregation_epoch_bytes_ += newly_acked_bytes;
  max_ack_height_.Update(aggregation_epoch_bytes_ - expected_bytes_acked,
                         round_trip_count_);
}

void BbrNetworkController::CalculatePacingRate() {
  if (BandwidthEstimate().IsZero()) {
    return;
  }

  // added by weiqing.ywq
  DataRate start_rate = DataRate::bps(100000);//bps

  DataRate target_rate = pacing_gain_ * BandwidthEstimate();
  if (config_.rate_based_recovery && InRecovery()) {
    pacing_rate_ = pacing_gain_ * max_bandwidth_.GetThirdBest();
  }
  if (is_at_full_bandwidth_) {
    pacing_rate_ = target_rate;
    return;
  }

  // 一旦 RTT 测量可用，就以 initial_window / RTT 的速率发送。
  if (pacing_rate_.IsZero() && !rtt_stats_.min_rtt().IsZero()) {
    // pacing_rate_ = initial_congestion_window_ / rtt_stats_.min_rtt();
    pacing_rate_ = start_rate;
    return;
  }
  // 一旦检测到丢包，就在 STARTUP 期间减慢发送速率。
  const bool has_ever_detected_loss = end_recovery_at_.has_value();
  if (config_.slower_startup && has_ever_detected_loss) {
    pacing_rate_ = kStartupAfterLossGain * BandwidthEstimate();
    return;
  }

  // 在启动期间不降低发送速率。
  pacing_rate_ = std::max(pacing_rate_, target_rate);
}

void BbrNetworkController::CalculateCongestionWindow(DataSize bytes_acked) {
  if (mode_ == PROBE_RTT) {
    return;
  }

  DataSize target_window = GetTargetCongestionWindow(congestion_window_gain_);

  if (rtt_variance_weight_ > 0.f && !BandwidthEstimate().IsZero()) {
    target_window += rtt_variance_weight_ * rtt_stats_.mean_deviation() *
                     BandwidthEstimate();
  } else if (max_aggregation_bytes_multiplier_ > 0 && is_at_full_bandwidth_) {
    // 只减去一半的 bytes_acked_since_queue_drained 可以确保如果队列最近没有被排空，
    // 发送不会完全停止很长时间。
    if (max_aggregation_bytes_multiplier_ * max_ack_height_.GetBest() >
        bytes_acked_since_queue_drained_ / 2) {
      target_window +=
          max_aggregation_bytes_multiplier_ * max_ack_height_.GetBest() -
          bytes_acked_since_queue_drained_ / 2;
    }
  } else if (is_at_full_bandwidth_) {
    target_window += max_ack_height_.GetBest();
  }

  // 与其立即将目标 CWND 设置为新的，BBR 通过一次仅增加它 |bytes_acked| 来增长 CWND 向 |target_window|。
  if (is_at_full_bandwidth_) {
    congestion_window_ =
        std::min(target_window, congestion_window_ + bytes_acked);
  } else if (congestion_window_ < target_window ||
             sampler_.total_data_acked() < initial_congestion_window_) {
    // 如果连接尚未退出启动阶段，不要减小窗口。
    congestion_window_ = congestion_window_ + bytes_acked;
  }

  // 强制执行拥塞窗口的限制。
  congestion_window_ = std::max(congestion_window_, min_congestion_window_);
  congestion_window_ = std::min(congestion_window_, max_congestion_window_);
}

void BbrNetworkController::CalculateRecoveryWindow(DataSize bytes_acked,
                                                   DataSize bytes_lost,
                                                   DataSize bytes_in_flight) {
  if (config_.rate_based_recovery ||
      (config_.rate_based_startup && mode_ == STARTUP)) {
    return;
  }

  if (recovery_state_ == NOT_IN_RECOVERY) {
    return;
  }

  // 设置初始恢复窗口。
  if (recovery_window_.IsZero()) {
    recovery_window_ = bytes_in_flight + bytes_acked;
    recovery_window_ = std::max(min_congestion_window_, recovery_window_);
    return;
  }

  // 从恢复窗口中移除损失，同时考虑潜在的整数下溢。
  recovery_window_ = recovery_window_ >= bytes_lost
                         ? recovery_window_ - bytes_lost
                         : kMaxSegmentSize;

  // 在 CONSERVATION 模式下，仅减去损失就足够了。在 GROWTH 中，
  // 释放额外的 |bytes_acked| 以实现类似慢启动的行为。
  // 在 MEDIUM_GROWTH 中，释放 |bytes_acked| / 2 来折中。
  if (recovery_state_ == GROWTH) {
    recovery_window_ += bytes_acked;
  } else if (recovery_state_ == MEDIUM_GROWTH) {
    recovery_window_ += bytes_acked / 2;
  }

  // 合理性检查。确保我们始终允许至少发送 |bytes_acked| 作为响应。
  recovery_window_ = std::max(recovery_window_, bytes_in_flight + bytes_acked);
  recovery_window_ = std::max(min_congestion_window_, recovery_window_);
}

void BbrNetworkController::OnApplicationLimited(DataSize bytes_in_flight) {
  if (bytes_in_flight >= GetCongestionWindow()) {
    return;
  }

  app_limited_since_last_probe_rtt_ = true;
  sampler_.OnAppLimited();

  RTC_LOG(LS_INFO) << "Becoming application limited. Last sent packet: "
                   << last_sent_packet_
                   << ", CWND: " << ToString(GetCongestionWindow());
}
}  // namespace bbr
}  // namespace webrtc
