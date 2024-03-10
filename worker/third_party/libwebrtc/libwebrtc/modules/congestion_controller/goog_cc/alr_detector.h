/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_CONGESTION_CONTROLLER_GOOG_CC_ALR_DETECTOR_H_
#define MODULES_CONGESTION_CONTROLLER_GOOG_CC_ALR_DETECTOR_H_

#include <absl/types/optional.h>
#include <stddef.h>
#include <stdint.h>

#include "api/transport/webrtc_key_value_config.h"
#include "modules/pacing/interval_budget.h"
#include "rtc_base/experiments/alr_experiment.h"
#include "rtc_base/experiments/field_trial_units.h"
#include "uv_loop.h"

namespace webrtc {

// 应用程序限制区域检测器是一个类，它使用经过的时间和发送的字节信号来估计网络流量是否
// 当前受到应用程序生成流量能力的限制。
//
// AlrDetector 提供了一个信号，可用于调整
// 估计带宽。
// 注意：此类不是线程安全的。
class AlrDetector {
 public:
  explicit AlrDetector(const WebRtcKeyValueConfig* key_value_config,
                       bifrost::UvLoop* loop);
  ~AlrDetector();

  void OnBytesSent(size_t bytes_sent, int64_t send_time_ms);

  // 设置当前估计带宽。
  void SetEstimatedBitrate(int bitrate_bps);

  // 返回当前应用程序限制区域开始的时间（以毫秒为单位）
  // 如果发送方当前不受应用程序限制，则返回空结果。
  absl::optional<int64_t> GetApplicationLimitedRegionStartTime() const;

  void UpdateBudgetWithElapsedTime(int64_t delta_time_ms);
  void UpdateBudgetWithBytesSent(size_t bytes_sent);

 private:
  // 作为网络容量使用率的已发送流量比率，用于确定
  // 应用程序限制区域。当带宽使用率下降时，ALR 区域开始
  // 低于 kAlrStartUsageRatio，并在上升到
  // kAlrEndUsageRatio。注意：这在目前是有意保守的
  // 直到对应用程序限制区域的带宽调整进行微调。
  static constexpr double kDefaultBandwidthUsageRatio = 0.65;
  static constexpr double kDefaultStartBudgetLevelRatio = 0.80;
  static constexpr double kDefaultStopBudgetLevelRatio = 0.50;

  AlrDetector(const WebRtcKeyValueConfig* key_value_config,
              absl::optional<AlrExperimentSettings> experiment_settings);

  friend class GoogCcStatePrinter;
  FieldTrialParameter<double> bandwidth_usage_ratio_;
  FieldTrialParameter<double> start_budget_level_ratio_;
  FieldTrialParameter<double> stop_budget_level_ratio_;

  absl::optional<int64_t> last_send_time_ms_;

  IntervalBudget alr_budget_;
  absl::optional<int64_t> alr_started_time_ms_;

  bifrost::UvLoop* loop_;
};

}  // namespace webrtc

#endif  // MODULES_CONGESTION_CONTROLLER_GOOG_CC_ALR_DETECTOR_H_
