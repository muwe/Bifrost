/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_CONGESTION_CONTROLLER_GOOG_CC_BITRATE_ESTIMATOR_H_
#define MODULES_CONGESTION_CONTROLLER_GOOG_CC_BITRATE_ESTIMATOR_H_

#include "api/transport/webrtc_key_value_config.h"
#include "api/units/data_rate.h"
#include "api/units/timestamp.h"
#include "rtc_base/experiments/field_trial_parser.h"

#include <absl/types/optional.h>
#include <stdint.h>

namespace webrtc {

// 根据包含到达时间和有效载荷大小的确认信息计算吞吐量的贝叶斯估计。
// 与当前估计值相差较远或基于较少数据包的样本
// 将被赋予较小的权重，因为它们被认为更有可能是由于与拥塞无关的延迟峰值等原因造成的。
class BitrateEstimator {
 public:
  explicit BitrateEstimator(const WebRtcKeyValueConfig* key_value_config);
  virtual ~BitrateEstimator();
  virtual void Update(Timestamp at_time, DataSize amount, bool in_alr);

  virtual absl::optional<DataRate> bitrate() const;
  absl::optional<DataRate> PeekRate() const;

  virtual void ExpectFastRateChange();

 private:
  float UpdateWindow(int64_t now_ms, int bytes, int rate_window_ms);
  int sum_;
  FieldTrialConstrained<int> initial_window_ms_;
  FieldTrialConstrained<int> noninitial_window_ms_;
  FieldTrialParameter<double> uncertainty_scale_;
  FieldTrialParameter<double> uncertainty_scale_in_alr_;
  FieldTrialParameter<DataRate> uncertainty_symmetry_cap_;
  FieldTrialParameter<DataRate> estimate_floor_;
  int64_t current_window_ms_;
  int64_t prev_time_ms_;
  float bitrate_estimate_kbps_;
  float bitrate_estimate_var_;
};

}  // namespace webrtc

#endif  // MODULES_CONGESTION_CONTROLLER_GOOG_CC_BITRATE_ESTIMATOR_H_
